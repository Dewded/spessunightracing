#include <platform.h>
#include "ctr_scratchpad.h"
#include "platform/native_memory.h"
#if defined(CTR_INTERNAL)
#include "platform/native_checkpoint.h"
#endif

#include <common.h>
#include <macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Native uses the NTSC-U 926 mempack window inside the retail 2 MiB address
// space so memory regressions fail here as they would on PSX.
#define CTR_NATIVE_MEMPACK_BUFFER_SIZE  0x200000u
#define CTR_NATIVE_MEMPACK_START_OFFSET 0xba9f0u
#define CTR_NATIVE_MEMPACK_SIZE         0x144e10u

CTR_STATIC_ASSERT(CTR_NATIVE_MEMPACK_START_OFFSET + CTR_NATIVE_MEMPACK_SIZE + MEMPACK_PS1_END_GUARD_SIZE == CTR_NATIVE_MEMPACK_BUFFER_SIZE);

union NativeScratchpadStorage
{
	u8 bytes[CTR_SCRATCHPAD_SIZE];
	u32 words[CTR_SCRATCHPAD_SIZE / sizeof(u32)];
};

CTR_STATIC_ASSERT(sizeof(union NativeScratchpadStorage) == CTR_SCRATCHPAD_SIZE);

global_variable char s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
global_variable struct PlatformMempackArena s_mempackArena;
global_variable union NativeScratchpadStorage s_scratchpadMemory;
u8 *gCTRNativeScratchpadBase;

void Platform_InitScratchpad(void)
{
#if defined(CTR_NATIVE)
	gCTRNativeScratchpadBase = &s_scratchpadMemory.bytes[0];
	memset(&s_scratchpadMemory, 0, sizeof(s_scratchpadMemory));
#endif
}

void Platform_ConfigureMempackArena(void)
{
	s_mempackArena.base = &s_mempackMemory[0];
	s_mempackArena.start = &s_mempackMemory[CTR_NATIVE_MEMPACK_START_OFFSET];
	s_mempackArena.endOfMemory = &s_mempackMemory[CTR_NATIVE_MEMPACK_BUFFER_SIZE];
	s_mempackArena.size = CTR_NATIVE_MEMPACK_SIZE;
	s_mempackArena.backingSize = CTR_NATIVE_MEMPACK_BUFFER_SIZE;
}

const struct PlatformMempackArena *Platform_InitMempackArena(void)
{
	memset(s_mempackMemory, 0, sizeof(s_mempackMemory));
	Platform_ConfigureMempackArena();
#if defined(CTR_INTERNAL)
	NativeCheckpoint_OnMempackArenaReset();
#endif

	return &s_mempackArena;
}

const struct PlatformMempackArena *Platform_GetMempackArena(void)
{
	return &s_mempackArena;
}

void *Platform_GetMempackBacking(void)
{
	return &s_mempackMemory[0];
}

int Platform_GetMempackBackingSize(void)
{
	return (int)sizeof(s_mempackMemory);
}

#define NATIVE_POINTER_SLOT_WORDS (CTR_NATIVE_MEMPACK_BUFFER_SIZE / sizeof(u32))

global_variable u32 s_pointerSlotBits[NATIVE_POINTER_SLOT_WORDS / 32];

internal int Platform_PointerSlotIndex(const void *slot, u32 *indexOut)
{
	const uintptr_t base = (uintptr_t)&s_mempackMemory[0];
	const uintptr_t address = (uintptr_t)slot;

	if ((address < base) || (address >= base + sizeof(s_mempackMemory)))
	{
		return 0;
	}

	*indexOut = (u32)((address - base) / sizeof(u32));
	return 1;
}

void Platform_ClearPointerSlots(const void *start, u32 size)
{
	u32 first;
	u32 last;

	if ((size == 0) || !Platform_PointerSlotIndex(start, &first))
	{
		return;
	}

	if (!Platform_PointerSlotIndex((const u8 *)start + size - 1, &last))
	{
		last = NATIVE_POINTER_SLOT_WORDS - 1;
	}

	for (u32 index = first; index <= last; index++)
	{
		s_pointerSlotBits[index >> 5] &= ~(1u << (index & 31));
	}
}

void Platform_MarkPointerSlot(const void *slot)
{
	u32 index;

	if (Platform_PointerSlotIndex(slot, &index))
	{
		s_pointerSlotBits[index >> 5] |= 1u << (index & 31);
	}
}

int Platform_QueryPointerSlot(const void *slot)
{
	u32 index;

	if (!Platform_PointerSlotIndex(slot, &index))
	{
		return -1;
	}

	return (s_pointerSlotBits[index >> 5] >> (index & 31)) & 1;
}

void Platform_RepairResidentPointers(s32 activeMempackIndex)
{
	if ((activeMempackIndex < 0) || (activeMempackIndex >= 4))
	{
		activeMempackIndex = 0;
	}

	// NOTE(aalhendi): Native keeps retail-shaped global data, but pointer aliases
	// must target this process's static storage. This also moves GCC's
	// initializer-only memcard helper global out of the live state graph so
	// checkpoints capture the actual memcard buffer.
	sdata = &sdata_static;
	sdata_static.gGT = &sdata_static.gameTracker;
	sdata_static.gGamepads = &sdata_static.gamepadSystem;
	sdata_static.PtrMempack = &sdata_static.mempack[activeMempackIndex];
	sdata_static.ptrToMemcardBuffer1 = &sdata_static.memcardBytes[0];
	sdata_static.ptrToMemcardBuffer2 = &sdata_static.memcardBytes[0];
}
