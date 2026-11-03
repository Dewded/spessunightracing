#include "platform/native_display.h"

#include <macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// NOTE: 16:9 over a 4:3 PS1 screen adds 1/6 of the screen width on each side.
#define NATIVE_DISPLAY_SIDE_EXTENSION_DIVISOR 6

global_variable b32 s_displayWidescreen = true;
global_variable b32 s_displayFullscreen = false;
// Diagnostic: keep the 16:9 host frame but leave game viewports at 4:3.
global_variable b32 s_displayKeepGameViewports = false;
global_variable b32 s_displayRetailKarts = false;
global_variable b32 s_displayRetailTracks = false;
global_variable s32 s_displayRenderScale = 0;

internal const char *NativeDisplay_ArgValue(int argc, char **argv, const char *name)
{
	for (int i = 1; i < argc - 1; i++)
	{
		if (strcmp(argv[i], name) == 0)
		{
			return argv[i + 1];
		}
	}

	return NULL;
}

internal b32 NativeDisplay_ArgPresent(int argc, char **argv, const char *name)
{
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], name) == 0)
		{
			return true;
		}
	}

	return false;
}

int NativeDisplay_ConfigureFromArgs(int argc, char **argv)
{
	const char *scaleArg = NativeDisplay_ArgValue(argc, argv, "--scale");

	if (NativeDisplay_ArgPresent(argc, argv, "--4x3"))
	{
		s_displayWidescreen = false;
	}

	if (NativeDisplay_ArgPresent(argc, argv, "--widescreen"))
	{
		s_displayWidescreen = true;
	}

	if (NativeDisplay_ArgPresent(argc, argv, "--retail-karts"))
	{
		s_displayRetailKarts = true;
	}

	if (NativeDisplay_ArgPresent(argc, argv, "--retail-tracks"))
	{
		s_displayRetailTracks = true;
	}

	if (NativeDisplay_ArgPresent(argc, argv, "--widescreen-host-only"))
	{
		s_displayKeepGameViewports = true;
	}

	if (NativeDisplay_ArgPresent(argc, argv, "--fullscreen"))
	{
		s_displayFullscreen = true;
	}

	if (scaleArg != NULL)
	{
		char *end = NULL;
		const long scale = strtol(scaleArg, &end, 10);

		if ((end == scaleArg) || (*end != '\0') || (scale < 0) || (scale > NATIVE_DISPLAY_MAX_RENDER_SCALE))
		{
			fprintf(stderr, "[CTR Native] --scale expects 0 (auto) to %d, got: %s\n", NATIVE_DISPLAY_MAX_RENDER_SCALE, scaleArg);
			return 1;
		}

		s_displayRenderScale = (s32)scale;
	}

	return 0;
}

b32 NativeDisplay_RetailKarts(void)
{
	return s_displayRetailKarts;
}

b32 NativeDisplay_RetailTracks(void)
{
	return s_displayRetailTracks;
}

b32 NativeDisplay_IsWidescreen(void)
{
	return s_displayWidescreen;
}

b32 NativeDisplay_IsFullscreen(void)
{
	return s_displayFullscreen;
}

s32 NativeDisplay_GetRenderScaleOverride(void)
{
	return s_displayRenderScale;
}

s32 NativeDisplay_GetSideExtension(s32 screenWidth)
{
	if (!s_displayWidescreen || (screenWidth <= 0))
	{
		return 0;
	}

	return screenWidth / NATIVE_DISPLAY_SIDE_EXTENSION_DIVISOR;
}

void NativeDisplay_WidenViewport(s16 *x, s16 *w, s32 screenWidth)
{
	const s32 extension = NativeDisplay_GetSideExtension(screenWidth);
	const s32 left = *x;
	const s32 right = left + *w;
	s32 newLeft = left;
	s32 newRight = right;

	if ((extension == 0) || s_displayKeepGameViewports)
	{
		return;
	}

	// Only edges on the screen border grow, so 4P quadrants keep their center gap.
	if (left <= 0)
	{
		newLeft = left - extension;
	}
	if (right >= screenWidth)
	{
		newRight = right + extension;
	}

	*x = (s16)newLeft;
	*w = (s16)(newRight - newLeft);
}

void NativeDisplay_ClampDrawArea(s16 *x, s16 *w, s32 pageX, s32 pageW)
{
	s32 left = *x;
	s32 right = left + *w;

	if (left < pageX)
	{
		left = pageX;
	}
	if (right > pageX + pageW)
	{
		right = pageX + pageW;
	}
	if (right < left)
	{
		right = left;
	}

	*x = (s16)left;
	*w = (s16)(right - left);
}
