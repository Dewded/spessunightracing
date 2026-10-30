#ifndef NATIVE_DISPLAY_H
#define NATIVE_DISPLAY_H

#include <ctr_types.h>

// NOTE: Host display options. Widescreen widens 3D viewports that touch the
// left/right screen edges ("Hor+"), while 2D UI keeps its 4:3 layout centered.
// Render scale draws the frame above PS1 resolution before presentation.

#define NATIVE_DISPLAY_MAX_RENDER_SCALE 8

int NativeDisplay_ConfigureFromArgs(int argc, char **argv);
b32 NativeDisplay_IsWidescreen(void);
b32 NativeDisplay_IsFullscreen(void);
s32 NativeDisplay_GetRenderScaleOverride(void);
s32 NativeDisplay_GetSideExtension(s32 screenWidth);
void NativeDisplay_WidenViewport(s16 *x, s16 *w, s32 screenWidth);
void NativeDisplay_ClampDrawArea(s16 *x, s16 *w, s32 pageX, s32 pageW);

#endif
