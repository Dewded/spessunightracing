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

#endif
