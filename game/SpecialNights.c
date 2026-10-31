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

// NOTE: Nitros Oxide gets a 16th character select icon. The retail layout
// tables hold 15 icons, so each layout is copied into a 16-entry native table
// with Oxide added to the row Fake Crash sits in (re-spaced so the row stays
// centred) and the neighbouring navigation links pointed at him.
#define SPECIAL_NIGHTS_SELECT_LAYOUT_COUNT 6
#define SPECIAL_NIGHTS_SELECT_OXIDE_ICON   15

global_variable struct CharacterSelectMeta s_specialNightsSelectMeta[SPECIAL_NIGHTS_SELECT_LAYOUT_COUNT][SPECIAL_NIGHTS_SELECT_ICON_COUNT];

internal void SpecialNights_SetSelectIcon(struct CharacterSelectMeta *meta, s32 icon, s16 posX, s16 posY, u8 up, u8 down, u8 left, u8 right)
{
	meta[icon].posX = posX;
	meta[icon].posY = posY;
	meta[icon].nextIconByDirection[0] = up;
	meta[icon].nextIconByDirection[1] = down;
	meta[icon].nextIconByDirection[2] = left;
	meta[icon].nextIconByDirection[3] = right;
}

struct CharacterSelectMeta *SpecialNights_CharacterSelectMeta(s32 layoutIndex, const struct CharacterSelectMeta *retail)
{
	struct CharacterSelectMeta *meta;
	s16 rowY;

	if ((layoutIndex < 0) || (layoutIndex >= SPECIAL_NIGHTS_SELECT_LAYOUT_COUNT))
	{
		layoutIndex = 0;
	}

	meta = s_specialNightsSelectMeta[layoutIndex];
	memcpy(meta, retail, SPECIAL_NIGHTS_SELECT_OXIDE_ICON * sizeof(*meta));
	meta[SPECIAL_NIGHTS_SELECT_OXIDE_ICON].characterID = NITROS_OXIDE;
	meta[SPECIAL_NIGHTS_SELECT_OXIDE_ICON].unlockFlags = MM_CHARACTER_UNLOCK_ALWAYS;

	switch (layoutIndex)
	{
	case 2:
		// 3P: Komodo Joe, Penta, Fake Crash, Oxide across the top, one per column.
		rowY = meta[12].posY;
		SpecialNights_SetSelectIcon(meta, 12, 0x20, rowY, 12, 0, 12, 13);
		SpecialNights_SetSelectIcon(meta, 13, 0x60, rowY, 13, 1, 12, 14);
		SpecialNights_SetSelectIcon(meta, 14, 0xA0, rowY, 14, 2, 13, 15);
		SpecialNights_SetSelectIcon(meta, 15, 0xE0, rowY, 15, 3, 14, 15);
		meta[3].nextIconByDirection[0] = 15; // Coco up
		break;

	case 3:
		// 4P: N. Tropy, Fake Crash, Oxide centred along the bottom.
		rowY = meta[9].posY;
		SpecialNights_SetSelectIcon(meta, 8, 0xA0, rowY, 5, 8, 8, 9);
		SpecialNights_SetSelectIcon(meta, 9, 0xE0, rowY, 6, 9, 8, 15);
		SpecialNights_SetSelectIcon(meta, 15, 0x120, rowY, 7, 15, 9, 15);
		meta[7].nextIconByDirection[1] = 15; // Pura down
		break;

	default:
		// 1P/2P: Komodo Joe, Penta, Fake Crash, Oxide under the middle four columns.
		rowY = meta[14].posY;
		SpecialNights_SetSelectIcon(meta, 12, 0x80, rowY, 4, 12, 12, 13);
		SpecialNights_SetSelectIcon(meta, 13, 0xC0, rowY, 5, 13, 12, 14);
		SpecialNights_SetSelectIcon(meta, 14, 0x100, rowY, 6, 14, 13, 15);
		SpecialNights_SetSelectIcon(meta, 15, 0x140, rowY, 7, 15, 14, 15);
		meta[7].nextIconByDirection[1] = 15; // Pura down
		break;
	}

	return meta;
}

// Icon slide-in reuses a neighbour's transition: entry 15 is the title's.
s32 SpecialNights_IconTransitionIndex(s32 layoutIndex, s32 iconIndex)
{
	if (iconIndex != SPECIAL_NIGHTS_SELECT_OXIDE_ICON)
	{
		return iconIndex;
	}

	return (layoutIndex == 3) ? 9 : 14;
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

// Synchronous read + checked relocation of a racer model file into host memory.
internal struct Model *SpecialNights_LoadRacerModel(s32 subfileIndex, u8 **bufferOut)
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

	*bufferOut = NULL;

	entry = BIG_GETENTRY((struct BigHeader *)((char *)bigfile + subfileIndex * sizeof(struct BigEntry)));
	fileSize = (u32)entry->size;
	if ((fileSize < (2 * sizeof(s32) + sizeof(struct Model))) || (fileSize > SPECIAL_NIGHTS_OXIDE_FILE_MAX_BYTES))
	{
		Platform_LogWarn("[Special Nights] racer model %d: unexpected file size %u\n", subfileIndex, fileSize);
		return NULL;
	}

	bufferSize = ((fileSize + LOAD_CD_DATA_SECTOR_ROUND_MASK) >> LOAD_CD_DATA_SECTOR_SHIFT) << LOAD_CD_DATA_SECTOR_SHIFT;
	buffer = (u8 *)calloc(1, bufferSize);
	if (buffer == NULL)
	{
		return NULL;
	}

	// The slot the reader scribbles on is restored after.
	savedSlot = data.currSlot;
	fileData = (u8 *)LOAD_ReadFile_ex(bigfile, LT_GETADDR, subfileIndex, buffer, &fileSize, NULL);
	data.currSlot = savedSlot;
	if (fileData != buffer)
	{
		Platform_LogWarn("[Special Nights] racer model %d: disc read failed\n", subfileIndex);
		free(buffer);
		return NULL;
	}

	// DRAM file: [ptrMapOffset][data ...][numBytes][offsets ...]
	memcpy(&ptrMapOffset, buffer, sizeof(ptrMapOffset));
	fileData = buffer + sizeof(s32);
	dataSize = fileSize - sizeof(s32);
	if ((ptrMapOffset < (s32)sizeof(struct Model)) || ((u32)ptrMapOffset > dataSize - sizeof(struct DramPointerMap)))
	{
		Platform_LogWarn("[Special Nights] racer model %d: bad pointer map offset %d (size %u)\n", subfileIndex, ptrMapOffset, fileSize);
		free(buffer);
		return NULL;
	}

	dpm = (struct DramPointerMap *)(fileData + ptrMapOffset);
	offsets = DRAM_GETOFFSETS(dpm);
	if ((dpm->numBytes < 0) || ((u32)dpm->numBytes > dataSize - (u32)ptrMapOffset - sizeof(struct DramPointerMap)))
	{
		Platform_LogWarn("[Special Nights] racer model %d: bad pointer map size %d\n", subfileIndex, dpm->numBytes);
		free(buffer);
		return NULL;
	}
	for (s32 i = 0; i < (dpm->numBytes >> 2); i++)
	{
		u32 slotOffset = (u32)offsets[i] & ~3u;

		if (slotOffset > (u32)ptrMapOffset - sizeof(u32))
		{
			Platform_LogWarn("[Special Nights] racer model %d: pointer slot %u outside data\n", subfileIndex, slotOffset);
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
		Platform_LogWarn("[Special Nights] racer model %d: %s\n", subfileIndex, problem);
		free(buffer);
		return NULL;
	}

	*bufferOut = buffer;
	return model;
}

internal s16 SpecialNights_ScaleAxis(s32 value, s32 menuReference, s32 raceReference)
{
	if (raceReference == 0)
	{
		return (s16)value;
	}

	return (s16)((value * menuReference) / raceReference);
}

struct Model *SpecialNights_GetOxidePreviewModel(void)
{
	struct Model *model;
	struct Model *menuFake;
	struct Model *raceFake;
	u8 *buffer;
	u8 *raceFakeBuffer;

	if (s_specialNightsOxideModelTried)
	{
		return s_specialNightsOxideModel;
	}

	// Only read while the load queue is idle; the read reuses its slot state.
	if ((sdata->ptrBigfile1 == NULL) || (sdata->queueLength != 0) || (sdata->load_inProgress != 0) || (sdata->Loading.stage != LOAD_IDLE))
	{
		return NULL;
	}

	s_specialNightsOxideModelTried = true;

	model = SpecialNights_LoadRacerModel(BI_RACERMODELHI + NITROS_OXIDE, &buffer);
	if (model == NULL)
	{
		return NULL;
	}

	// Menu karts are authored at a different header scale than race models.
	// Scale Oxide by how Fake Crash's menu kart compares to his race model.
	menuFake = MM_Characters_GetModelByName(GAME_CHARACTER_METADATA[FAKE_CRASH].name_Debug);
	raceFake = SpecialNights_LoadRacerModel(BI_RACERMODELHI + FAKE_CRASH, &raceFakeBuffer);
	if ((menuFake != NULL) && (raceFake != NULL))
	{
		const SVec3 *menuScale = &menuFake->headers[0].scale;
		const SVec3 *raceScale = &raceFake->headers[0].scale;

		for (s32 i = 0; i < model->numHeaders; i++)
		{
			SVec3 *scale = &model->headers[i].scale;

			scale->x = SpecialNights_ScaleAxis(scale->x, menuScale->x, raceScale->x);
			scale->y = SpecialNights_ScaleAxis(scale->y, menuScale->y, raceScale->y);
			scale->z = SpecialNights_ScaleAxis(scale->z, menuScale->z, raceScale->z);
		}
		Platform_Log("[Special Nights] Oxide preview scale: menu fake %d,%d,%d race fake %d,%d,%d -> oxide %d,%d,%d\n", menuScale->x, menuScale->y,
		             menuScale->z, raceScale->x, raceScale->y, raceScale->z, model->headers[0].scale.x, model->headers[0].scale.y,
		             model->headers[0].scale.z);
	}
	free(raceFakeBuffer);

	Platform_Log("[Special Nights] Oxide preview: loaded '%.16s' (%d LOD, %u anims)\n", model->name, model->numHeaders, model->headers[0].numAnimations);
	s_specialNightsOxideModel = model;
	return s_specialNightsOxideModel;
}

#endif
