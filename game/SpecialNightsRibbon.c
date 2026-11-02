#include <common.h>

// Special Nights: the title logo ribbon reads "SPESSU NIGHT" (native builds only).
//
// The ribbon under the CTR logo is six 64x24 4-bit texture pieces, each with
// its own CLUT, drawn left to right. Nothing of the original art ships here:
// when the main menu's VRAM is loaded, the pieces are read back, checked
// against the NTSC-U original by hash, and rebuilt from the original glyphs
// (S, E, N, I, G, H, T as they are; P from R without its leg; U from H with
// its crossbar replaced by a bowl), then re-quantised to each piece's CLUT.

#if defined(CTR_NATIVE)

#define SN_RIBBON_PIECES      6
#define SN_RIBBON_PIECE_W     64
#define SN_RIBBON_H           24
#define SN_RIBBON_W           (SN_RIBBON_PIECES * SN_RIBBON_PIECE_W)
#define SN_RIBBON_PIECE_WORDS (SN_RIBBON_PIECE_W / 4)
// FNV-1a over the six pieces (rows of 16 words) then the six CLUTs, little-endian bytes.
#define SN_RIBBON_ORIGINAL_HASH 0xb86cac45u

// Piece texel rects in VRAM words (4-bit: 4 texels per word) and their CLUTs, screen order.
global_variable const s16 s_snRibbonRect[SN_RIBBON_PIECES][2] = {{853, 188}, {621, 444}, {869, 120}, {853, 120}, {869, 48}, {853, 48}};
global_variable const s16 s_snRibbonClut[SN_RIBBON_PIECES][2] = {{912, 510}, {912, 509}, {944, 511}, {944, 510}, {944, 509}, {896, 511}};

global_variable b32 s_snRibbonCheckPending;

// 15-bit colours as the original art uses them.
#define SN_RGB(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))
#define SN_BG    SN_RGB(21, 0, 0)
#define SN_WHITE SN_RGB(31, 31, 31)
#define SN_PINK  SN_RGB(25, 19, 20)
#define SN_BLACK SN_RGB(0, 0, 1)

struct SpecialNightsGlyph
{
	s16 x0, x1; // source columns in the original ribbon, [x0, x1)
};

void SpecialNights_RibbonVramUploaded(void)
{
	s_snRibbonCheckPending = true;
}

internal u32 SpecialNights_Fnv(u32 hash, const u16 *words, s32 count)
{
	for (s32 i = 0; i < count; i++)
	{
		hash = (hash ^ (words[i] & 0xff)) * 0x01000193u;
		hash = (hash ^ (words[i] >> 8)) * 0x01000193u;
	}
	return hash;
}

// Red ribbon background (including its dithered shades): not part of a letter.
internal b32 SpecialNights_RibbonIsBg(u16 c)
{
	return ((c & 31) >= 18) && (((c >> 5) & 31) <= 3) && (((c >> 10) & 31) <= 4);
}

// Copies glyph columns [x0, x1) of src into a scratch glyph (24 rows, stride SN_RIBBON_W).
internal s32 SpecialNights_CutGlyph(u16 glyph[SN_RIBBON_H][32], u16 src[SN_RIBBON_H][SN_RIBBON_W], s32 x0, s32 x1)
{
	for (s32 y = 0; y < SN_RIBBON_H; y++)
	{
		for (s32 x = x0; x < x1; x++)
		{
			glyph[y][x - x0] = src[y][x];
		}
	}
	return x1 - x0;
}

internal void SpecialNights_ComposeRibbon(u16 src[SN_RIBBON_H][SN_RIBBON_W], u16 out[SN_RIBBON_H][SN_RIBBON_W])
{
	// Source columns of the letters in "CRASH TEAM RACING".
	const struct SpecialNightsGlyph glyphS = {73, 95}, glyphH = {96, 123}, glyphT = {133, 158}, glyphE = {158, 181};
	const struct SpecialNightsGlyph glyphI = {311, 323}, glyphN = {323, 350}, glyphG = {349, 380}, glyphR = {31, 54};
	const char *text = "SPESSU NIGHT";
	const s32 spacing = 1;
	const s32 space = 12;
	u16 glyphs[16][SN_RIBBON_H][32];
	s32 widths[16];
	s32 gaps[16];
	s32 count = 0;
	s32 total = 0;
	s32 x;

	for (const char *c = text; (*c != 0) && (count < 16); c++, count++)
	{
		u16(*g)[32] = glyphs[count];
		const struct SpecialNightsGlyph *from = NULL;

		gaps[count] = spacing;
		widths[count] = 0;
		switch (*c)
		{
		case ' ':
			gaps[count] = space;
			continue;
		case 'S': from = &glyphS; break;
		case 'E': from = &glyphE; break;
		case 'N': from = &glyphN; gaps[count] = spacing + 3; break; // keep "NI" from reading as "M"
		case 'I': from = &glyphI; break;
		case 'G': from = &glyphG; break;
		case 'H': from = &glyphH; break;
		case 'T': from = &glyphT; break;
		case 'P':
			// R (x 31..53) without its leg: rows 13+ right of the stem (x >= 40).
			widths[count] = SpecialNights_CutGlyph(g, src, glyphR.x0, glyphR.x1);
			for (s32 y = 13; y < SN_RIBBON_H; y++)
			{
				for (s32 gx = 40 - glyphR.x0; gx < widths[count]; gx++)
				{
					g[y][gx] = SN_BG;
				}
			}
			continue;
		case 'U':
		{
			// H (x 96..122: stems 98-104 and 112-118, crossbar rows 12-14) with a bowl.
			const s32 o = glyphH.x0;
			widths[count] = SpecialNights_CutGlyph(g, src, glyphH.x0, glyphH.x1);
			for (s32 y = 10; y < 20; y++)
			{
				for (s32 hx = 105; hx < 112; hx++)
				{
					g[y][hx - o] = SN_BG; // crossbar, inner shadow and serifs inside the bowl
				}
			}
			for (s32 y = 16; y < 20; y++)
			{
				g[y][96 - o] = g[y][97 - o] = SN_BG; // outer bottom serifs
				g[y][120 - o] = g[y][121 - o] = g[y][122 - o] = SN_BG;
			}
			for (s32 hx = 99; hx < 120; hx++)
			{
				const b32 edge = (hx == 99) || (hx == 119);

				if ((hx >= 100) && (hx <= 118))
				{
					g[15][hx - o] = SN_WHITE;
				}
				g[16][hx - o] = edge ? SN_PINK : SN_WHITE;
				g[17][hx - o] = edge ? SN_BG : (((hx == 100) || (hx == 118)) ? SN_PINK : SN_WHITE);
				g[18][hx - o] = (edge || (hx == 100)) ? SN_BG : SN_PINK;
				g[19][hx - o] = ((hx < 102) || (hx > 119)) ? SN_BG : SN_BLACK;
			}
			continue;
		}
		default:
			continue;
		}
		widths[count] = SpecialNights_CutGlyph(g, src, from->x0, from->x1);
	}

	for (s32 i = 0; i < count; i++)
	{
		total += widths[i] + gaps[i];
	}
	total -= gaps[count - 1];

	for (s32 y = 0; y < SN_RIBBON_H; y++)
	{
		for (s32 ox = 0; ox < SN_RIBBON_W; ox++)
		{
			out[y][ox] = SN_BG;
		}
	}

	// Paste each glyph's letter pixels (not its background) left to right, centred.
	x = (SN_RIBBON_W - total) / 2;
	for (s32 i = 0; i < count; i++)
	{
		for (s32 y = 0; y < SN_RIBBON_H; y++)
		{
			for (s32 gx = 0; gx < widths[i]; gx++)
			{
				const u16 c = glyphs[i][y][gx];

				if (!SpecialNights_RibbonIsBg(c) && (x + gx >= 0) && (x + gx < SN_RIBBON_W))
				{
					out[y][x + gx] = c;
				}
			}
		}
		x += widths[i] + gaps[i];
	}
}

internal u8 SpecialNights_NearestClutIndex(const u16 clut[16], u16 c)
{
	s32 best = 0;
	s32 bestDist = 0x7fffffff;

	for (s32 i = 0; i < 16; i++)
	{
		const s32 dr = (s32)(clut[i] & 31) - (s32)(c & 31);
		const s32 dg = (s32)((clut[i] >> 5) & 31) - (s32)((c >> 5) & 31);
		const s32 db = (s32)((clut[i] >> 10) & 31) - (s32)((c >> 10) & 31);
		const s32 dist = dr * dr + dg * dg + db * db;

		if (dist < bestDist)
		{
			bestDist = dist;
			best = i;
		}
	}
	return (u8)best;
}

// Rebuilds the ribbon pieces in place. words/cluts are what VRAM holds; returns
// false (and leaves words alone) unless they are the untouched NTSC-U original.
b32 SpecialNights_RibbonRebuild(u16 words[SN_RIBBON_PIECES][SN_RIBBON_H][SN_RIBBON_PIECE_WORDS], u16 cluts[SN_RIBBON_PIECES][16])
{
	static u16 src[SN_RIBBON_H][SN_RIBBON_W];
	static u16 out[SN_RIBBON_H][SN_RIBBON_W];
	u32 hash = 0x811c9dc5u;

	for (s32 p = 0; p < SN_RIBBON_PIECES; p++)
	{
		hash = SpecialNights_Fnv(hash, &words[p][0][0], SN_RIBBON_H * SN_RIBBON_PIECE_WORDS);
	}
	for (s32 p = 0; p < SN_RIBBON_PIECES; p++)
	{
		hash = SpecialNights_Fnv(hash, cluts[p], 16);
	}
	if (hash != SN_RIBBON_ORIGINAL_HASH)
	{
		return false;
	}

	for (s32 p = 0; p < SN_RIBBON_PIECES; p++)
	{
		for (s32 y = 0; y < SN_RIBBON_H; y++)
		{
			for (s32 tx = 0; tx < SN_RIBBON_PIECE_W; tx++)
			{
				src[y][p * SN_RIBBON_PIECE_W + tx] = cluts[p][(words[p][y][tx >> 2] >> ((tx & 3) * 4)) & 15];
			}
		}
	}

	SpecialNights_ComposeRibbon(src, out);

	for (s32 p = 0; p < SN_RIBBON_PIECES; p++)
	{
		for (s32 y = 0; y < SN_RIBBON_H; y++)
		{
			for (s32 w = 0; w < SN_RIBBON_PIECE_WORDS; w++)
			{
				u16 packed = 0;

				for (s32 k = 0; k < 4; k++)
				{
					packed |= (u16)(SpecialNights_NearestClutIndex(cluts[p], out[y][p * SN_RIBBON_PIECE_W + w * 4 + k]) << (k * 4));
				}
				words[p][y][w] = packed;
			}
		}
	}
	return true;
}

// Main menu, once after each VRAM load: read the ribbon back, rebuild, upload.
void SpecialNights_RibbonUpdate(void)
{
	static u16 words[SN_RIBBON_PIECES][SN_RIBBON_H][SN_RIBBON_PIECE_WORDS];
	static u16 cluts[SN_RIBBON_PIECES][16];
	RECT rect;

	if (!s_snRibbonCheckPending)
	{
		return;
	}
	s_snRibbonCheckPending = false;

	for (s32 p = 0; p < SN_RIBBON_PIECES; p++)
	{
		setRECT(&rect, s_snRibbonRect[p][0], s_snRibbonRect[p][1], SN_RIBBON_PIECE_WORDS, SN_RIBBON_H);
		StoreImage(&rect, (u32 *)&words[p][0][0]);
		setRECT(&rect, s_snRibbonClut[p][0], s_snRibbonClut[p][1], 16, 1);
		StoreImage(&rect, (u32 *)cluts[p]);
	}

	if (!SpecialNights_RibbonRebuild(words, cluts))
	{
		return;
	}

	for (s32 p = 0; p < SN_RIBBON_PIECES; p++)
	{
		setRECT(&rect, s_snRibbonRect[p][0], s_snRibbonRect[p][1], SN_RIBBON_PIECE_WORDS, SN_RIBBON_H);
		LoadImage(&rect, &words[p][0][0]);
	}
	Platform_Log("[Special Nights] title ribbon now reads SPESSU NIGHT\n");
}

#endif
