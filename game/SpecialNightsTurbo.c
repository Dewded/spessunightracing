#include <common.h>

// Special Nights: light blue exhaust flames above sacred fire speed (native
// builds only).
//
// A boost's fire level sets the kart's speed cap: 0x100 reaches sacred fire
// speed. Super turbo pads give 0x800 (AI: 0x900); everything else stays at or
// below 0x180 (AI on normal pads). Retail draws the same orange flames for all
// of them. While a kart's cap corresponds to SN_FLAME_BLUE_FIRE_LEVEL or more,
// its flames use copies of the eight flame frames whose texture layouts carry
// NATIVE_TPAGE_FLAG_TINT_BLUE; the renderer recolours those texels light blue.
// The copies share geometry, colours and textures with the originals.

#if defined(CTR_NATIVE)

#define SN_FLAME_FRAMES          8
#define SN_FLAME_MAX_HEADERS     4
#define SN_FLAME_MAX_LAYOUTS     64
#define SN_FLAME_BLUE_FIRE_LEVEL 0x400

struct SpecialNightsBlueFlame
{
	struct Model *source;
	struct Model model;
	struct ModelHeader headers[SN_FLAME_MAX_HEADERS];
	struct TextureLayout *layoutPtrs[SN_FLAME_MAX_HEADERS][SN_FLAME_MAX_LAYOUTS];
	struct TextureLayout layouts[SN_FLAME_MAX_HEADERS][SN_FLAME_MAX_LAYOUTS];
	b32 valid;
};

global_variable struct SpecialNightsBlueFlame s_snBlueFlames[SN_FLAME_FRAMES];

// MainInit: flame models are reloaded with each level, so rebuild on demand.
void SpecialNights_ResetBlueFlames(void)
{
	memset(s_snBlueFlames, 0, sizeof(s_snBlueFlames));
}

// Texture layouts a header's draw commands use (texture index is 1-based, low 9 bits).
internal s32 SpecialNights_FlameLayoutCount(const struct ModelHeader *mh)
{
	const u32 *commands = (const u32 *)(size_t)mh->ptrCommandList;
	s32 count = 0;

	for (s32 i = 1; (i < 0x4000) && (commands[i] != 0xffffffffu); i++)
	{
		if ((commands[i] >> 16) != 0)
		{
			const s32 texIndex = (s32)(commands[i] & 0x1ff);

			count = (texIndex > count) ? texIndex : count;
		}
	}
	return count;
}

internal struct Model *SpecialNights_BlueFlame(s32 frame, struct Model *source)
{
	struct SpecialNightsBlueFlame *blue = &s_snBlueFlames[frame];

	if (blue->valid && (blue->source == source))
	{
		return &blue->model;
	}

	blue->valid = false;
	blue->source = source;
	if ((source->numHeaders <= 0) || (source->numHeaders > SN_FLAME_MAX_HEADERS) || (source->headers == NULL))
	{
		return source;
	}

	blue->model = *source;
	blue->model.headers = blue->headers;
	for (s32 h = 0; h < source->numHeaders; h++)
	{
		const struct ModelHeader *mh = &source->headers[h];
		s32 count;

		blue->headers[h] = *mh;
		if ((mh->ptrTexLayout == NULL) || (mh->ptrCommandList == 0))
		{
			continue;
		}

		count = SpecialNights_FlameLayoutCount(mh);
		if (count > SN_FLAME_MAX_LAYOUTS)
		{
			return source;
		}
		for (s32 t = 0; t < count; t++)
		{
			if (mh->ptrTexLayout[t] == NULL)
			{
				blue->layoutPtrs[h][t] = NULL;
				continue;
			}
			blue->layouts[h][t] = *mh->ptrTexLayout[t];
			blue->layouts[h][t].tpage |= NATIVE_TPAGE_FLAG_TINT_BLUE;
			blue->layoutPtrs[h][t] = &blue->layouts[h][t];
		}
		blue->headers[h].ptrTexLayout = blue->layoutPtrs[h];
	}
	blue->valid = true;
	return &blue->model;
}

// VehTurbo_ThTick, after picking this frame's flame model.
struct Model *SpecialNights_TurboFlameModel(struct Driver *driver, struct Model *model)
{
	s32 frame;

	// Inverse of VehFire_Increment: cap = single + (fireLevel * (sacred - single) >> 8).
	const s32 single = driver->const_SingleTurboSpeed;
	const s32 span = (s32)driver->const_SacredFireSpeed - single;

	if ((model == NULL) || (span <= 0) || ((((s32)driver->fireSpeedCap - single) << 8) < (SN_FLAME_BLUE_FIRE_LEVEL * span)))
	{
		return model;
	}

	for (frame = 0; frame < SN_FLAME_FRAMES; frame++)
	{
		if (GAME_TRACKER->modelPtr[STATIC_TURBO_EFFECT + frame] == model)
		{
			return SpecialNights_BlueFlame(frame, model);
		}
	}
	return model;
}

#endif
