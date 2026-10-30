#ifndef PLATFORM_NATIVE_MEMORY_H
#define PLATFORM_NATIVE_MEMORY_H

#include <macros.h>

void Platform_ConfigureMempackArena(void);
void Platform_RepairResidentPointers(s32 activeMempackIndex);
void *Platform_GetMempackBacking(void);
int Platform_GetMempackBackingSize(void);

// NOTE: Pointer-slot tracking. Retail pointer-map patching turns file offsets
// into negative 0x80xxxxxx words, and some renderer paths test that sign.
// Native patches to positive host addresses, so the loader records which
// words it patched and code can ask instead of guessing from the value.
void Platform_ClearPointerSlots(const void *start, u32 size);
void Platform_MarkPointerSlot(const void *slot);
// Returns 1 for a patched pointer word, 0 for plain data, -1 if untracked.
int Platform_QueryPointerSlot(const void *slot);

#endif
