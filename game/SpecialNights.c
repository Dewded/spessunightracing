#include <common.h>

#if defined(CTR_NATIVE)
#include <platform/native_log.h>
#endif

// Special Nights gameplay mods (native builds only).

#if defined(CTR_NATIVE)

// NOTE: Per-player engine class picked on character select. ENGINE_CLASS_DEFAULT
// keeps the character's own class; human drivers use the pick at race start.
#define SPECIAL_NIGHTS_ENGINE_DEFAULT (-1)
#define SPECIAL_NIGHTS_PLAYER_COUNT   4

global_variable s32 s_specialNightsEngine[SPECIAL_NIGHTS_PLAYER_COUNT] = {
    SPECIAL_NIGHTS_ENGINE_DEFAULT,
    SPECIAL_NIGHTS_ENGINE_DEFAULT,
    SPECIAL_NIGHTS_ENGINE_DEFAULT,
    SPECIAL_NIGHTS_ENGINE_DEFAULT,
};

global_variable char *s_specialNightsEngineLabels[NUM_CLASSES] = {
    "BALANCED",
    "ACCEL",
    "SPEED",
    "TURN",
};

void SpecialNights_CycleEngineClass(s32 playerIndex, u32 buttonTaps)
{
	s32 engine;

	if ((playerIndex < 0) || (playerIndex >= SPECIAL_NIGHTS_PLAYER_COUNT) || ((buttonTaps & (BTN_L1 | BTN_R1)) == 0))
	{
		return;
	}

	// Cycle DEFAULT -> BALANCED -> ACCEL -> SPEED -> TURN -> DEFAULT (R1 forward, L1 back).
	engine = s_specialNightsEngine[playerIndex] + 1;
	engine += ((buttonTaps & BTN_R1) != 0) ? 1 : -1;
	engine = (engine + NUM_CLASSES + 1) % (NUM_CLASSES + 1);
	s_specialNightsEngine[playerIndex] = engine - 1;

	OtherFX_Play(0, 1);
}

s32 SpecialNights_DriverEngineClass(s32 driverID)
{
	struct GameTracker *gGT = GAME_TRACKER;
	s32 engine = GAME_CHARACTER_METADATA[GAME_CHARACTER_IDS[driverID]].engineID;

	// Human drivers are driverID 0..numPlyr-1; AI keep their character's class.
	if ((driverID >= 0) && (driverID < gGT->numPlyrCurrGame) && (driverID < SPECIAL_NIGHTS_PLAYER_COUNT) &&
	    (s_specialNightsEngine[driverID] != SPECIAL_NIGHTS_ENGINE_DEFAULT))
	{
		engine = s_specialNightsEngine[driverID];
	}

	return engine;
}

void SpecialNights_DrawEngineClass(s32 playerIndex, s32 characterID, s32 posX, s32 posY, s32 fontType)
{
	s32 engine;
	b32 isDefault;

	if ((playerIndex < 0) || (playerIndex >= SPECIAL_NIGHTS_PLAYER_COUNT))
	{
		return;
	}

	isDefault = (s_specialNightsEngine[playerIndex] == SPECIAL_NIGHTS_ENGINE_DEFAULT);
	engine = isDefault ? GAME_CHARACTER_METADATA[characterID].engineID : s_specialNightsEngine[playerIndex];

	// Orange: character's own class. White: overridden.
	DecalFont_DrawLine(s_specialNightsEngineLabels[engine], (s16)posX, (s16)posY, (s16)fontType, (JUSTIFY_CENTER | (isDefault ? ORANGE : WHITE)));
}

// NOTE: Nitros Oxide shares the Fake Crash slot on character select (the PS1
// select-grid tables are fixed at 15 icons). L2/R2 toggles the slot per player;
// the race loads character 15, and the menu folds it back into the slot.
global_variable b32 s_specialNightsOxide[SPECIAL_NIGHTS_PLAYER_COUNT];

void SpecialNights_ToggleOxide(s32 playerIndex, s32 hoveredCharacterID, u32 buttonTaps)
{
	if ((playerIndex < 0) || (playerIndex >= SPECIAL_NIGHTS_PLAYER_COUNT) || (hoveredCharacterID != FAKE_CRASH) ||
	    ((buttonTaps & (BTN_L2 | BTN_R2)) == 0))
	{
		return;
	}

	s_specialNightsOxide[playerIndex] = !s_specialNightsOxide[playerIndex];
	OtherFX_Play(0, 1);
}

s32 SpecialNights_DisplayCharacterID(s32 playerIndex, s32 characterID)
{
	if ((playerIndex >= 0) && (playerIndex < SPECIAL_NIGHTS_PLAYER_COUNT) && (characterID == FAKE_CRASH) && s_specialNightsOxide[playerIndex])
	{
		return NITROS_OXIDE;
	}

	return characterID;
}

void SpecialNights_DrawOxideHint(s32 characterID, s32 posX, s32 posY)
{
	if (characterID == FAKE_CRASH)
	{
		DecalFont_DrawLine("L2/R2 OXIDE", (s16)posX, (s16)posY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
	}
}

// Race load: players holding the Oxide toggle on the Fake Crash slot race as Oxide.
void SpecialNights_ApplyRaceCharacters(s16 levelID)
{
	if (levelID >= GEM_STONE_VALLEY)
	{
		return;
	}

	for (s32 playerIndex = 0; playerIndex < GAME_TRACKER->numPlyrNextGame && playerIndex < SPECIAL_NIGHTS_PLAYER_COUNT; playerIndex++)
	{
		GAME_CHARACTER_IDS[playerIndex] = (s16)SpecialNights_DisplayCharacterID(playerIndex, GAME_CHARACTER_IDS[playerIndex]);
	}
}

// NOTE: The main menu LEV has no Oxide model, so character select loads his HI
// racer file on demand into host memory (outside the PS1-sized mempack) and
// keeps it for the session. Textures come from whatever the menu VRAM holds.
// The file is checked before relocation and the model before use; anything
// unexpected leaves the Fake Crash kart in place and logs why.
#define SPECIAL_NIGHTS_OXIDE_FILE_MAX_BYTES 0x100000
#define SPECIAL_NIGHTS_OXIDE_MAX_HEADERS    8
#define SPECIAL_NIGHTS_OXIDE_MAX_ANIMS      64

global_variable struct Model *s_specialNightsOxideModel;
global_variable b32 s_specialNightsOxideModelTried;

internal b32 SpecialNights_InFile(const void *ptr, const u8 *base, u32 size, u32 bytes)
{
	const u8 *p = (const u8 *)ptr;

	return (p >= base) && (bytes <= size) && ((u32)(p - base) <= size - bytes);
}

internal b32 SpecialNights_InFileOrNull(const void *ptr, const u8 *base, u32 size, u32 bytes)
{
	return (ptr == NULL) || SpecialNights_InFile(ptr, base, size, bytes);
}

internal const char *SpecialNights_CheckOxideModel(const struct Model *model, const u8 *base, u32 size)
{
	if (!SpecialNights_InFile(model, base, size, sizeof(struct Model)))
	{
		return "model outside file";
	}
	if ((model->numHeaders <= 0) || (model->numHeaders > SPECIAL_NIGHTS_OXIDE_MAX_HEADERS))
	{
		return "bad header count";
	}
	if (!SpecialNights_InFile(model->headers, base, size, (u32)model->numHeaders * sizeof(struct ModelHeader)))
	{
		return "headers outside file";
	}

	for (s32 i = 0; i < model->numHeaders; i++)
	{
		const struct ModelHeader *mh = &model->headers[i];

		if ((mh->ptrCommandList == 0) || !SpecialNights_InFile((const void *)(size_t)mh->ptrCommandList, base, size, sizeof(u32)) ||
		    !SpecialNights_InFileOrNull(mh->ptrFrameData, base, size, 1) || !SpecialNights_InFileOrNull(mh->ptrTexLayout, base, size, sizeof(void *)) ||
		    !SpecialNights_InFileOrNull(mh->ptrColors, base, size, sizeof(u32)) || !SpecialNights_InFileOrNull(mh->animtex, base, size, 1))
		{
			return "header data outside file";
		}
		if (mh->numAnimations > SPECIAL_NIGHTS_OXIDE_MAX_ANIMS)
		{
			return "bad animation count";
		}
		if (mh->numAnimations != 0)
		{
			if (!SpecialNights_InFile(mh->ptrAnimations, base, size, mh->numAnimations * sizeof(void *)))
			{
				return "animation table outside file";
			}
			for (u32 a = 0; a < mh->numAnimations; a++)
			{
				if (!SpecialNights_InFile(mh->ptrAnimations[a], base, size, 1))
				{
					return "animation outside file";
				}
			}
		}
		else if (mh->ptrFrameData == NULL)
		{
			return "no frame data";
		}
	}

	return NULL;
}

struct Model *SpecialNights_GetOxidePreviewModel(void)
{
	struct BigHeader *bigfile = sdata->ptrBigfile1;
	struct BigEntry *entry;
	struct LoadQueueSlot savedSlot;
	struct DramPointerMap *dpm;
	struct Model *model;
	const char *problem;
	s32 ptrMapOffset;
	u32 fileSize;
	u32 dataSize;
	u32 bufferSize;
	u8 *buffer;
	u8 *fileData;
	int *offsets;

	if (s_specialNightsOxideModelTried)
	{
		return s_specialNightsOxideModel;
	}

	// Only read while the load queue is idle; the read reuses its slot state.
	if ((bigfile == NULL) || (sdata->queueLength != 0) || (sdata->load_inProgress != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		return NULL;
	}

	s_specialNightsOxideModelTried = true;

	entry = BIG_GETENTRY((struct BigHeader *)((char *)bigfile + (BI_RACERMODELHI + NITROS_OXIDE) * sizeof(struct BigEntry)));
	fileSize = (u32)entry->size;
	if ((fileSize < (2 * sizeof(s32) + sizeof(struct Model))) || (fileSize > SPECIAL_NIGHTS_OXIDE_FILE_MAX_BYTES))
	{
		Platform_LogWarn("[Special Nights] Oxide preview: unexpected file size %u\n", fileSize);
		return NULL;
	}

	bufferSize = ((fileSize + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT) << LOAD_CD_DATA_SECTOR_SHIFT;
	buffer = (u8 *)calloc(1, bufferSize);
	if (buffer == NULL)
	{
		return NULL;
	}

	// Synchronous raw read; the slot the reader scribbles on is restored after.
	savedSlot = data.currSlot;
	fileData = (u8 *)LOAD_ReadFile_ex(bigfile, LT_GETADDR, BI_RACERMODELHI + NITROS_OXIDE, buffer, &fileSize, NULL);
	data.currSlot = savedSlot;
	if (fileData != buffer)
	{
		Platform_LogWarn("[Special Nights] Oxide preview: disc read failed\n");
		free(buffer);
		return NULL;
	}

	// DRAM file: [ptrMapOffset][data ...][numBytes][offsets ...]
	memcpy(&ptrMapOffset, buffer, sizeof(ptrMapOffset));
	fileData = buffer + sizeof(s32);
	dataSize = fileSize - sizeof(s32);
	if ((ptrMapOffset < (s32)sizeof(struct Model)) || ((u32)ptrMapOffset > dataSize - sizeof(struct DramPointerMap)))
	{
		Platform_LogWarn("[Special Nights] Oxide preview: bad pointer map offset %d (size %u)\n", ptrMapOffset, fileSize);
		free(buffer);
		return NULL;
	}

	dpm = (struct DramPointerMap *)(fileData + ptrMapOffset);
	offsets = DRAM_GETOFFSETS(dpm);
	if ((dpm->numBytes < 0) || ((u32)dpm->numBytes > dataSize - (u32)ptrMapOffset - sizeof(struct DramPointerMap)))
	{
		Platform_LogWarn("[Special Nights] Oxide preview: bad pointer map size %d\n", dpm->numBytes);
		free(buffer);
		return NULL;
	}
	for (s32 i = 0; i < (dpm->numBytes >> 2); i++)
	{
		u32 slotOffset = (u32)offsets[i] & ~3u;

		if (slotOffset > (u32)ptrMapOffset - sizeof(u32))
		{
			Platform_LogWarn("[Special Nights] Oxide preview: pointer slot %u outside data\n", slotOffset);
			free(buffer);
			return NULL;
		}
	}

	LOAD_RunPtrMap((char *)fileData, offsets, dpm->numBytes >> 2);

	// Same as the race loader: model = raw file + LOAD_MODEL_FILE_HEADER_BYTES (the map offset word).
	model = (struct Model *)(buffer + LOAD_MODEL_FILE_HEADER_BYTES);
	problem = SpecialNights_CheckOxideModel(model, fileData, (u32)ptrMapOffset);
	if (problem != NULL)
	{
		Platform_LogWarn("[Special Nights] Oxide preview: %s\n", problem);
		free(buffer);
		return NULL;
	}

	Platform_Log("[Special Nights] Oxide preview: loaded '%.16s' (%d LOD, %u anims, %u bytes)\n", model->name, model->numHeaders,
	             model->headers[0].numAnimations, fileSize);
	s_specialNightsOxideModel = model;
	return s_specialNightsOxideModel;
}

// Character select: Oxide has no grid icon, so fold him back into the Fake Crash slot.
void SpecialNights_FoldMenuCharacters(void)
{
	for (s32 playerIndex = 0; playerIndex < SPECIAL_NIGHTS_PLAYER_COUNT; playerIndex++)
	{
		if (GAME_CHARACTER_IDS[playerIndex] == NITROS_OXIDE)
		{
			GAME_CHARACTER_IDS[playerIndex] = FAKE_CRASH;
			s_specialNightsOxide[playerIndex] = true;
		}
	}
}

#endif
