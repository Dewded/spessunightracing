#include <common.h>

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
global_variable struct Model *s_specialNightsOxideModel;
global_variable b32 s_specialNightsOxideModelTried;

struct Model *SpecialNights_GetOxidePreviewModel(void)
{
	struct BigHeader *bigfile = sdata->ptrBigfile1;
	struct BigEntry *entry;
	u32 size;
	u32 bufferSize;
	u8 *buffer;
	u8 *fileData;

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
	bufferSize = (((u32)entry->size + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT) << LOAD_CD_DATA_SECTOR_SHIFT;
	buffer = (u8 *)calloc(1, bufferSize);
	if (buffer == NULL)
	{
		return NULL;
	}

	// Synchronous DRAM read + pointer-map relocation; returns the data past the map header.
	fileData = (u8 *)LOAD_DramFile(bigfile, BI_RACERMODELHI + NITROS_OXIDE, buffer, &size, -1);
	if (fileData == NULL)
	{
		free(buffer);
		return NULL;
	}

	s_specialNightsOxideModel = (struct Model *)(fileData + LOAD_MODEL_FILE_HEADER_BYTES);
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
