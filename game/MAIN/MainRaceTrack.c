#include <common.h>

// NOTE(aalhendi): The unit's resident binding names the existing requested-level
// slot; native and unbound PSX code use the canonical loading field.
#ifndef MAIN_REQUESTED_LEVEL
#define MAIN_REQUESTED_LEVEL (sdata->Loading.Lev_ID_To_Load)
#endif

void MainRaceTrack_StartLoad(s16 levelID)
{
	// clear backup,
	// keep music,
	// destroy "most" fx, let menu fx play to end
	howl_StopAudio(1, 0, 0);

	ElimBG_Deactivate(sdata->gGT);

	LOAD_LevelFile(levelID);
	return;
}

#if defined(CTR_NATIVE)
// NOTE: Debug quick start (--track/--character). Boots straight into a 1P
// medium Arcade race instead of the Naughty Dog crate.
#define MAIN_RACE_TRACK_QUICK_START_DIFFICULTY 0xA0

static s16 MainRaceTrack_TakeQuickStart(struct GameTracker *gt, s16 levelID)
{
	s16 quickLevel;
	s16 character;

	if (!NativeQuickStart_TakePending(&quickLevel, &character))
	{
		return levelID;
	}

	gt->gameMode1 &= ~(BATTLE_MODE | ADVENTURE_MODE | TIME_TRIAL | ADVENTURE_ARENA | ARCADE_MODE | ADVENTURE_CUP);
	gt->gameMode2 &= ~CUP_ANY_KIND;
	gt->gameMode1 |= ARCADE_MODE;
	gt->boolDemoMode = 0;
	gt->numPlyrNextGame = 1;
	gt->numLaps = 3;
	gt->arcadeDifficulty = MAIN_RACE_TRACK_QUICK_START_DIFFICULTY;

	// Keep the AI roster unique: swap the chosen racer with whoever already holds it.
	for (int i = 1; i < 8; i++)
	{
		if (GAME_CHARACTER_IDS[i] == character)
		{
			GAME_CHARACTER_IDS[i] = GAME_CHARACTER_IDS[0];
		}
	}
	GAME_CHARACTER_IDS[0] = character;

	gt->currLEV = quickLevel;
	return quickLevel;
}

// Boot straight into the race instead of the Naughty Dog crate.
s16 MainRaceTrack_QuickStartBootLevel(struct GameTracker *gt, s16 levelID)
{
	return MainRaceTrack_TakeQuickStart(gt, levelID);
}

// Fallback if the boot hook did not consume it: replace the first intro/menu load.
static s16 MainRaceTrack_ApplyQuickStart(struct GameTracker *gt, s16 levelID)
{
	if ((levelID < INTRO_RACE_TODAY) || (levelID > MAIN_MENU_LEVEL))
	{
		return levelID;
	}

	return MainRaceTrack_TakeQuickStart(gt, levelID);
}
#endif

void MainRaceTrack_RequestLoad(s16 levelID)
{
	struct GameTracker *gt = GAME_TRACKER;

#if defined(CTR_NATIVE)
	levelID = MainRaceTrack_ApplyQuickStart(gt, levelID);
	SpecialNights_ApplyRaceCharacters(levelID);
#endif

	// Turn off HUD
	gt->hudFlags &= HUD_FLAG_CLEAR_RACE_HUD_MASK;

	if (RaceFlag_IsFullyOffScreen() == 1)
	{
		RaceFlag_BeginTransition(1);
	}
	RaceFlag_ResetTextAnim();

	sdata->Loading.stage = LOAD_REQUESTED;
	MAIN_REQUESTED_LEVEL = levelID;
	return;
}
