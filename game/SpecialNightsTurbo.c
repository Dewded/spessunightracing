#include <common.h>

// Special Nights: light blue exhaust flames above sacred fire speed (native
// builds only).
//
// A boost's fire level sets the kart's speed cap: 0x100 reaches sacred fire
// speed. Super turbo pads give 0x800 (AI: 0x900); everything else stays at or
// below 0x180 (AI on normal pads). Retail draws the same orange flames for all
// of them. While a kart's cap corresponds to SN_FLAME_BLUE_FIRE_LEVEL or more,
// its flames use copies of the eight flame frames with vertex colours
// recoloured light blue; the copies share everything else with the originals.

#if defined(CTR_NATIVE)

#define SN_FLAME_FRAMES      8
#define SN_FLAME_MAX_HEADERS 4
#define SN_FLAME_MAX_COLORS  128
#define SN_FLAME_BLUE_FIRE_LEVEL 0x400

struct SpecialNightsBlueFlame
{
	struct Model *source;
	struct Model model;
	struct ModelHeader headers[SN_FLAME_MAX_HEADERS];
	u32 colors[SN_FLAME_MAX_HEADERS][SN_FLAME_MAX_COLORS];
	b32 valid;
};

global_variable struct SpecialNightsBlueFlame s_snBlueFlames[SN_FLAME_FRAMES];

// MainInit: flame models are reloaded with each level, so rebuild on demand.
void SpecialNights_ResetBlueFlames(void)
{
	memset(s_snBlueFlames, 0, sizeof(s_snBlueFlames));
}

// Number of colour words a header's command list reads: the count retail copies
// to the scratchpad (first word), or the highest index any command uses.
internal s32 SpecialNights_FlameColorCount(const struct ModelHeader *mh)
{
	const u32 *commands = (const u32 *)(size_t)mh->ptrCommandList;
	s32 count = (s32)commands[0];

	for (s32 i = 1; (i < 0x4000) && (commands[i] != 0xffffffffu); i++)
	{
		const s32 first = (s32)(((commands[i] >> 7) & 0x1fc) / sizeof(u32)) + 1;
		const s32 second = (s32)((commands[i] & 0x1fc) / sizeof(u32)) + 1;

		count = (first > count) ? first : count;
		if ((commands[i] >> 16) == 0)
		{
			// Colour-only commands carry a second index in the low bits.
			count = (second > count) ? second : count;
		}
	}
	return (count > SN_FLAME_MAX_COLORS) ? SN_FLAME_MAX_COLORS : count;
}

// Perceived brightness mapped onto light blue, so the flame keeps its gradient:
// the orange rim turns deep sky blue and the white core near-white cyan. The
// GPU code byte stays.
internal u32 SpecialNights_BlueFlameColor(u32 color)
{
	const u32 r = color & 0xff;
	const u32 g = (color >> 8) & 0xff;
	const u32 b = (color >> 16) & 0xff;
	const u32 level = (r * 77 + g * 150 + b * 29) >> 8;
	u32 newR = level * 6 / 10;
	u32 newG = level * 11 / 10;
	u32 newB = level * 14 / 10;

	newR = (newR > 0xff) ? 0xff : newR;
	newG = (newG > 0xff) ? 0xff : newG;
	newB = (newB > 0xff) ? 0xff : newB;
	return (color & 0xff000000u) | newR | (newG << 8) | (newB << 16);
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

		blue->headers[h] = *mh;
		if ((mh->ptrColors == NULL) || (mh->ptrCommandList == 0))
		{
			continue;
		}

		for (s32 c = 0; c < SpecialNights_FlameColorCount(mh); c++)
		{
			blue->colors[h][c] = SpecialNights_BlueFlameColor(mh->ptrColors[c]);
		}
		blue->headers[h].ptrColors = blue->colors[h];
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
