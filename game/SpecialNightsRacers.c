#include <common.h>

// Special Nights: full 8-racer Arcade grids at any player count, and
// high-detail karts for everyone (native builds only).
//
// Retail Arcade runs 8 racers in 1P, 6 in 2P and has no 3P/4P at all; in
// multiplayer every racer uses a lower-detail model, and distant karts become
// sprites (DecalMP). Here Arcade fills the grid to 8 at 1-4 players: the AI
// slots the retail packs leave empty get unique characters, and their karts
// are read from the disc into host memory (outside the PS1-sized mempack).
// Unless --retail-karts is given, every racer in a race or battle also uses
// its high-detail (1P) model.

#if defined(CTR_NATIVE)

#define SN_RACER_SLOTS      8
#define SN_RACER_CACHE_MAX  48

struct SpecialNightsCachedModel
{
	s32 subfileIndex;
	struct Model *model;
};

global_variable struct SpecialNightsCachedModel s_snModelCache[SN_RACER_CACHE_MAX];
global_variable s32 s_snModelCacheCount;
global_variable struct Model *s_snRaceModels[SN_RACER_SLOTS];

// Arcade race (not Adventure, Time Trial, Battle, menus or cutscenes).
b32 SpecialNights_ArcadeFullGrid(struct GameTracker *gGT)
{
	return ((gGT->gameMode1 & ARCADE_MODE) != 0) &&
	       ((gGT->gameMode1 & (ADVENTURE_MODE | ADVENTURE_ARENA | ADVENTURE_CUP | TIME_TRIAL | BATTLE_MODE | MAIN_MENU | GAME_CUTSCENE)) == 0) &&
	       ((gGT->gameMode2 & CREDITS) == 0);
}

// Gives AI slots [first, SN_RACER_SLOTS) unique characters no other slot uses.
// The retail eight come first, then the unlockable racers (not Oxide).
internal void SpecialNights_FillAiSlots(s32 first)
{
	for (s32 slot = first; slot < SN_RACER_SLOTS; slot++)
	{
		for (s32 candidate = 0; candidate < NITROS_OXIDE; candidate++)
		{
			b32 taken = false;

			for (s32 other = 0; other < SN_RACER_SLOTS; other++)
			{
				// Humans and the AIs before this slot keep their characters.
				if ((other < slot) && (GAME_CHARACTER_IDS[other] == candidate))
				{
					taken = true;
					break;
				}
			}
			if (!taken)
			{
				GAME_CHARACTER_IDS[slot] = (s16)candidate;
				break;
			}
		}
	}
}

// LOAD_DriverMPK, before queueing: 3P/4P Arcade picks every AI character
// (the 3P pack is chosen by slot 3, so it must be an AI already).
void SpecialNights_AssignAiBeforePacks(s32 levelLOD)
{
	if (SpecialNights_ArcadeFullGrid(GAME_TRACKER) && ((levelLOD == LOAD_LEVEL_LOD_3P) || (levelLOD == LOAD_LEVEL_LOD_4P)))
	{
		SpecialNights_FillAiSlots(levelLOD);
	}
}

// LOAD_DriverMPK, after queueing: 2P Arcade keeps the retail pack's four AIs
// (slots 2-5) and adds two more.
void SpecialNights_AssignAiAfterPacks(s32 levelLOD)
{
	if (SpecialNights_ArcadeFullGrid(GAME_TRACKER) && (levelLOD == LOAD_LEVEL_LOD_2P))
	{
		SpecialNights_FillAiSlots(6);
	}
}

internal struct Model *SpecialNights_CachedRacerModel(s32 subfileIndex)
{
	u8 *buffer;
	struct Model *model;

	for (s32 i = 0; i < s_snModelCacheCount; i++)
	{
		if (s_snModelCache[i].subfileIndex == subfileIndex)
		{
			return s_snModelCache[i].model;
		}
	}

	model = SpecialNights_LoadRacerModel(subfileIndex, &buffer);
	if ((model != NULL) && (s_snModelCacheCount < SN_RACER_CACHE_MAX))
	{
		// Kept for the session: the next race with this kart reuses it.
		s_snModelCache[s_snModelCacheCount].subfileIndex = subfileIndex;
		s_snModelCache[s_snModelCacheCount].model = model;
		s_snModelCacheCount++;
	}
	return model;
}

// MainInit, before racers spawn (loading is finished, the queue is idle).
// Decides which karts come from here instead of the retail packs.
void SpecialNights_PrepareRaceModels(struct GameTracker *gGT)
{
	const b32 raceOrBattle = (gGT->levelID < GEM_STONE_VALLEY) && ((gGT->gameMode1 & (ADVENTURE_MODE | TIME_TRIAL | MAIN_MENU | GAME_CUTSCENE)) == 0) &&
	                         ((gGT->gameMode2 & CREDITS) == 0);
	const s32 numPlyr = gGT->numPlyrCurrGame;
	s32 firstExtraAi = SN_RACER_SLOTS;

	s32 numDrivers;

	memset(s_snRaceModels, 0, sizeof(s_snRaceModels));
	if (!raceOrBattle)
	{
		return;
	}

	// Arcade fills the grid with AI; VS and Battle are humans only.
	numDrivers = SpecialNights_ArcadeFullGrid(gGT) ? SN_RACER_SLOTS : numPlyr;

	// AI slots no retail pack covers.
	if (SpecialNights_ArcadeFullGrid(gGT))
	{
		firstExtraAi = (numPlyr == 1) ? SN_RACER_SLOTS : (numPlyr == 2) ? 6 : (numPlyr == 3) ? 4 : numPlyr;
	}

	for (s32 slot = 0; (slot < numDrivers) && (slot < SN_RACER_SLOTS); slot++)
	{
		const s32 characterID = GAME_CHARACTER_IDS[slot];
		s32 subfile;

		if ((characterID < 0) || (characterID > NITROS_OXIDE))
		{
			continue;
		}
		if (!NativeDisplay_RetailKarts())
		{
			subfile = BI_RACERMODELHI + characterID;
		}
		else if (slot >= firstExtraAi)
		{
			subfile = ((numPlyr <= 2) ? BI_RACERMODELMED : BI_RACERMODELLOW) + characterID;
		}
		else
		{
			continue;
		}
		s_snRaceModels[slot] = SpecialNights_CachedRacerModel(subfile);
	}
}

// VehBirth_GetModelByName: karts prepared for this race win over the packs.
struct Model *SpecialNights_FindRaceModel(const char *name)
{
	for (s32 slot = 0; slot < SN_RACER_SLOTS; slot++)
	{
		const struct Model *m = s_snRaceModels[slot];

		if ((m != NULL) && (memcmp(m->name, name, MODEL_NAME_BYTE_COUNT) == 0))
		{
			return s_snRaceModels[slot];
		}
	}
	return NULL;
}

#endif
