#include <common.h>

// Special Nights: the "SPESSU STATS" main menu screen (native builds only).
//
// Reads special_nights.db (see game/SpecialNightsStats.c) when the screen
// opens and shows it as pages: cup leaderboard, a points race chart per year,
// recent cups, head to head, best laps, top speeds, and one page per player.
// L1/R1 or left/right flip
// pages, up/down scroll, Triangle/Square returns to the main menu.

#if defined(CTR_NATIVE)

#define SN_SCREEN_MAX_CHARTS  8
#define SN_SCREEN_MAX_PAGES   (5 + SN_SCREEN_MAX_CHARTS + SPECIAL_NIGHTS_ROSTER_MAX)
#define SN_SCREEN_MAX_ROWS    32
#define SN_SCREEN_MAX_COLS    6
#define SN_SCREEN_CELL_BYTES  20
#define SN_SCREEN_VISIBLE     14
#define SN_SCREEN_ROW_Y0      52
#define SN_SCREEN_ROW_STEP    10

// Year chart: each player's running cup points after each cup of the year.
#define SN_CHART_MAX_CUPS    96
#define SN_CHART_MAX_PLAYERS 12
#define SN_CHART_X0          44
#define SN_CHART_X1          296
#define SN_CHART_Y0          50
#define SN_CHART_Y1          176
#define SN_CHART_LEGEND_X    304
#define SN_CHART_TOTAL_X     504
#define SN_SMALL_CHAR_W      13

struct SpecialNightsChart
{
	char year[8];
	s32 numCups;
	s32 numPlayers;
	s32 maxValue;
	char names[SN_CHART_MAX_PLAYERS][SN_SCREEN_CELL_BYTES];
	char totals[SN_CHART_MAX_PLAYERS][8];
	s16 color[SN_CHART_MAX_PLAYERS];
	s16 order[SN_CHART_MAX_PLAYERS];
	s32 points[SN_CHART_MAX_PLAYERS][SN_CHART_MAX_CUPS + 1]; // [0] is the year's start (0)
};

struct SpecialNightsScreenPage
{
	char title[32];
	s32 chart; // index into s_snCharts, or -1 for a table page
	s32 numCols;
	s16 colX[SN_SCREEN_MAX_COLS];
	u16 colJustify[SN_SCREEN_MAX_COLS];
	char header[SN_SCREEN_MAX_COLS][12];
	s32 numRows;
	char cells[SN_SCREEN_MAX_ROWS][SN_SCREEN_MAX_COLS][SN_SCREEN_CELL_BYTES];
};

global_variable struct SpecialNightsScreenPage s_snPages[SN_SCREEN_MAX_PAGES];
global_variable s32 s_snNumPages;
global_variable s32 s_snPage;
global_variable s32 s_snScroll;
global_variable char s_snMessage[48];
global_variable struct SpecialNightsChart s_snCharts[SN_SCREEN_MAX_CHARTS];
global_variable s32 s_snNumCharts;

// Line and legend colours (font colour styles), one per roster slot.
global_variable const s16 s_snChartColors[SN_CHART_MAX_PLAYERS] = {
    POLAR_CYAN, PAPU_YELLOW,  TINY_GREEN,       COCO_MAGENTA, ROO_ORANGE,        CRASH_BLUE,
    PURA_VIOLET, CORTEX_RED, OXIDE_LIGHT_GREEN, PENTA_WHITE,  TROPY_LIGHT_BLUE, DINGODILE_OLIVE,
};

global_variable struct RectMenu s_snStatsMenu = {
    .stringIndexTitle = RECTMENU_STRING_NONE,
    .state = DISABLE_INPUT_ALLOW_FUNCPTRS,
    .funcPtr = MM_SpecialNightsStats_MenuProc,
};

// ---------------------------------------------------------------------------
// Main menu row

// Unused "na" slot in every language file; it becomes the row label.
#define SN_SCREEN_LNG_ROW  LNG_NA_241
#define SN_MAIN_ROW_HIGH   5

global_variable struct MenuRow s_snMainRowsBasic[8];
global_variable struct MenuRow s_snMainRowsScrapbook[9];

void SpecialNights_PatchLanguageStrings(void)
{
	if ((GAME_LANGUAGE_STRINGS != NULL) && (GAME_LANGUAGE_STRING_COUNT > SN_SCREEN_LNG_ROW))
	{
		GAME_LANGUAGE_STRINGS[SN_SCREEN_LNG_ROW] = "SPESSU STATS";
	}
}

// Retail rows with "SPESSU STATS" inserted after HIGH SCORE.
internal void SpecialNights_BuildMainRows(struct MenuRow *out, const struct MenuRow *retail, s32 retailRows)
{
	s32 outIndex = 0;

	for (s32 i = 0; i < retailRows; i++)
	{
		out[outIndex] = retail[i];
		if (i > SN_MAIN_ROW_HIGH)
		{
			// Rows below the new one move down by one.
			out[outIndex].rowOnPressUp = (char)(retail[i].rowOnPressUp + 1);
			out[outIndex].rowOnPressDown = (char)(retail[i].rowOnPressDown + 1);
			out[outIndex].rowOnPressLeft = (char)(retail[i].rowOnPressLeft + 1);
			out[outIndex].rowOnPressRight = (char)(retail[i].rowOnPressRight + 1);
		}
		outIndex++;

		if (i == SN_MAIN_ROW_HIGH)
		{
			const s32 statsRow = outIndex;

			out[i].rowOnPressDown = (char)statsRow;
			out[outIndex].stringIndex = SN_SCREEN_LNG_ROW;
			out[outIndex].rowOnPressUp = (char)i;
			out[outIndex].rowOnPressDown = (char)((i + 1 < retailRows) ? statsRow + 1 : statsRow);
			out[outIndex].rowOnPressLeft = (char)statsRow;
			out[outIndex].rowOnPressRight = (char)statsRow;
			outIndex++;
		}
	}

	out[outIndex].stringIndex = RECTMENU_STRING_NONE;
}

struct MenuRow *SpecialNights_MainMenuRows(b32 withScrapbook)
{
	if (withScrapbook)
	{
		SpecialNights_BuildMainRows(s_snMainRowsScrapbook, D230.rowsMainMenuWithScrapbook, (s32)len(D230.rowsMainMenuWithScrapbook) - 1);
		return s_snMainRowsScrapbook;
	}

	SpecialNights_BuildMainRows(s_snMainRowsBasic, D230.rowsMainMenuBasic, (s32)len(D230.rowsMainMenuBasic) - 1);
	return s_snMainRowsBasic;
}

b32 SpecialNights_IsStatsRow(s32 stringIndex)
{
	return stringIndex == SN_SCREEN_LNG_ROW;
}

// ---------------------------------------------------------------------------
// Pages

internal struct SpecialNightsScreenPage *SpecialNights_NewPage(const char *title)
{
	struct SpecialNightsScreenPage *page;

	if (s_snNumPages >= SN_SCREEN_MAX_PAGES)
	{
		return NULL;
	}

	page = &s_snPages[s_snNumPages++];
	memset(page, 0, sizeof(*page));
	page->chart = -1;
	SpecialNights_FoldDisplayName(title, page->title, sizeof(page->title));
	return page;
}

internal void SpecialNights_AddColumn(struct SpecialNightsScreenPage *page, s16 x, u16 justify, const char *header)
{
	if (page->numCols < SN_SCREEN_MAX_COLS)
	{
		page->colX[page->numCols] = x;
		page->colJustify[page->numCols] = justify;
		snprintf(page->header[page->numCols], sizeof(page->header[page->numCols]), "%s", header);
		page->numCols++;
	}
}

internal void SpecialNights_FormatMs(char *out, u32 outBytes, sqlite3_stmt *stmt, s32 column)
{
	s64 ms;

	if (sqlite3_column_type(stmt, column) == SQLITE_NULL)
	{
		snprintf(out, outBytes, "-");
		return;
	}

	ms = sqlite3_column_int64(stmt, column);
	snprintf(out, outBytes, "%d:%02d.%02d", (int)(ms / 60000), (int)((ms / 1000) % 60), (int)((ms / 10) % 100));
}

// Each column of the result becomes a cell; columns named in msColumns (bit
// mask) are formatted as lap times. Text is folded to the font's characters.
internal void SpecialNights_FillPage(sqlite3 *db, struct SpecialNightsScreenPage *page, const char *sql, const char *bindText, u32 msColumns)
{
	sqlite3_stmt *stmt = NULL;

	if (page == NULL)
	{
		return;
	}

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		SpecialNights_DbLogError(db, "stats screen query");
		return;
	}
	if (bindText != NULL)
	{
		sqlite3_bind_text(stmt, 1, bindText, -1, SQLITE_TRANSIENT);
	}

	while ((page->numRows < SN_SCREEN_MAX_ROWS) && (sqlite3_step(stmt) == SQLITE_ROW))
	{
		for (s32 col = 0; col < page->numCols; col++)
		{
			char *cell = page->cells[page->numRows][col];

			if (((msColumns >> col) & 1) != 0)
			{
				SpecialNights_FormatMs(cell, SN_SCREEN_CELL_BYTES, stmt, col);
			}
			else if (sqlite3_column_type(stmt, col) == SQLITE_NULL)
			{
				snprintf(cell, SN_SCREEN_CELL_BYTES, "-");
			}
			else
			{
				SpecialNights_FoldDisplayName((const char *)sqlite3_column_text(stmt, col), cell, SN_SCREEN_CELL_BYTES);
			}
		}
		page->numRows++;
	}

	sqlite3_finalize(stmt);
}

internal s32 SpecialNights_ChartColor(const char *player, s32 fallback)
{
	for (s32 i = 0; i < s_specialNightsRosterCount; i++)
	{
		if (strcmp(s_specialNightsRoster[i], player) == 0)
		{
			return s_snChartColors[i % SN_CHART_MAX_PLAYERS];
		}
	}
	return s_snChartColors[fallback % SN_CHART_MAX_PLAYERS];
}

// One chart page per year, newest first: running cup points of every player.
internal void SpecialNights_LoadYearCharts(sqlite3 *db)
{
	struct SpecialNightsChart *chart = NULL;
	char rawNames[SN_CHART_MAX_PLAYERS][SPECIAL_NIGHTS_NAME_BYTES];
	char year[8] = "";
	char cupRun[32] = "";
	sqlite3_stmt *stmt = NULL;
	s32 chartFirst = s_snNumCharts;

	if (sqlite3_prepare_v2(db,
	                       "SELECT substr(c.started_at, 1, 4), c.cup_run, cr.player, cr.points FROM cup_results cr "
	                       "JOIN cup_runs c ON c.cup_run = cr.cup_run WHERE c.completed = 1 AND cr.human = 1 "
	                       "ORDER BY c.started_at, c.cup_run",
	                       -1, &stmt, NULL) != SQLITE_OK)
	{
		SpecialNights_DbLogError(db, "year chart query");
		return;
	}

	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		const char *rowYear = (const char *)sqlite3_column_text(stmt, 0);
		const char *rowRun = (const char *)sqlite3_column_text(stmt, 1);
		const char *player = (const char *)sqlite3_column_text(stmt, 2);
		s32 playerIndex;

		if ((rowYear == NULL) || (rowRun == NULL) || (player == NULL))
		{
			continue;
		}

		// New year: new chart (older years drop out once the chart slots run out).
		if ((chart == NULL) || (strcmp(rowYear, year) != 0))
		{
			if (s_snNumCharts >= SN_SCREEN_MAX_CHARTS)
			{
				memmove(&s_snCharts[chartFirst], &s_snCharts[chartFirst + 1], (s_snNumCharts - chartFirst - 1) * sizeof(s_snCharts[0]));
				s_snNumCharts--;
			}
			chart = &s_snCharts[s_snNumCharts++];
			memset(chart, 0, sizeof(*chart));
			snprintf(year, sizeof(year), "%s", rowYear);
			snprintf(chart->year, sizeof(chart->year), "%s", year);
			cupRun[0] = 0;
		}

		// New cup: every line carries its total forward.
		if (strcmp(rowRun, cupRun) != 0)
		{
			if (chart->numCups >= SN_CHART_MAX_CUPS)
			{
				continue;
			}
			chart->numCups++;
			for (s32 i = 0; i < chart->numPlayers; i++)
			{
				chart->points[i][chart->numCups] = chart->points[i][chart->numCups - 1];
			}
			snprintf(cupRun, sizeof(cupRun), "%s", rowRun);
		}

		for (playerIndex = 0; playerIndex < chart->numPlayers; playerIndex++)
		{
			if (strcmp(rawNames[playerIndex], player) == 0)
			{
				break;
			}
		}
		if (playerIndex == chart->numPlayers)
		{
			if (chart->numPlayers >= SN_CHART_MAX_PLAYERS)
			{
				continue;
			}
			chart->numPlayers++;
			snprintf(rawNames[playerIndex], sizeof(rawNames[playerIndex]), "%s", player);
			SpecialNights_FoldDisplayName(player, chart->names[playerIndex], sizeof(chart->names[playerIndex]));
			chart->color[playerIndex] = (s16)SpecialNights_ChartColor(player, playerIndex);
		}
		chart->points[playerIndex][chart->numCups] += sqlite3_column_int(stmt, 3);
	}
	sqlite3_finalize(stmt);

	// Pages newest year first; legend sorted by the year's total.
	for (s32 c = s_snNumCharts - 1; c >= chartFirst; c--)
	{
		struct SpecialNightsScreenPage *page;
		char title[32];

		chart = &s_snCharts[c];
		snprintf(title, sizeof(title), "%s Points Race", chart->year);
		page = SpecialNights_NewPage(title);
		if (page == NULL)
		{
			break;
		}
		page->chart = c;

		chart->maxValue = 1;
		for (s32 i = 0; i < chart->numPlayers; i++)
		{
			const s32 total = chart->points[i][chart->numCups];

			snprintf(chart->totals[i], sizeof(chart->totals[i]), "%d", total);
			if (total > chart->maxValue)
			{
				chart->maxValue = total;
			}
			chart->order[i] = (s16)i;
		}
		for (s32 i = 1; i < chart->numPlayers; i++)
		{
			for (s32 j = i; (j > 0) && (chart->points[chart->order[j]][chart->numCups] > chart->points[chart->order[j - 1]][chart->numCups]); j--)
			{
				const s16 swap = chart->order[j];
				chart->order[j] = chart->order[j - 1];
				chart->order[j - 1] = swap;
			}
		}
	}
}

internal void SpecialNights_LoadStatsPages(void)
{
	struct SpecialNightsScreenPage *page;
	sqlite3_stmt *stmt = NULL;
	sqlite3 *db = NULL;
	char players[SPECIAL_NIGHTS_ROSTER_MAX][SPECIAL_NIGHTS_NAME_BYTES];
	s32 numPlayers = 0;

	s_snNumPages = 0;
	s_snNumCharts = 0;
	s_snMessage[0] = 0;

	if (sqlite3_open_v2(SPECIAL_NIGHTS_DB_FILE, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
	{
		sqlite3_close(db);
		snprintf(s_snMessage, sizeof(s_snMessage), "NO CUPS RECORDED YET");
		return;
	}
	sqlite3_busy_timeout(db, 1000);

	page = SpecialNights_NewPage("Cup Leaderboard");
	if (page != NULL)
	{
		SpecialNights_AddColumn(page, 16, 0, "PLAYER");
		SpecialNights_AddColumn(page, 250, JUSTIFY_RIGHT, "CUPS");
		SpecialNights_AddColumn(page, 310, JUSTIFY_RIGHT, "WIN");
		SpecialNights_AddColumn(page, 370, JUSTIFY_RIGHT, "POD");
		SpecialNights_AddColumn(page, 432, JUSTIFY_RIGHT, "AVG");
		SpecialNights_AddColumn(page, 496, JUSTIFY_RIGHT, "PTS");
		SpecialNights_FillPage(db, page,
		                       "SELECT player, cups, cup_wins, cup_podiums, printf('%.2f', avg_final_place), total_points FROM player_cup_summary "
		                       "ORDER BY cup_wins DESC, avg_final_place, total_points DESC",
		                       NULL, 0);
	}

	SpecialNights_LoadYearCharts(db);

	page = SpecialNights_NewPage("Recent Cups");
	if (page != NULL)
	{
		SpecialNights_AddColumn(page, 16, 0, "DATE");
		SpecialNights_AddColumn(page, 160, 0, "CUP");
		SpecialNights_AddColumn(page, 312, 0, "WINNER");
		SpecialNights_FillPage(db, page,
		                       "SELECT substr(c.started_at, 1, 10), c.cup_name, "
		                       "(SELECT COALESCE(w.player, w.character) FROM cup_results w WHERE w.cup_run = c.cup_run ORDER BY w.final_place LIMIT 1) "
		                       "FROM cup_runs c WHERE c.completed = 1 ORDER BY c.cup_run DESC",
		                       NULL, 0);
	}

	page = SpecialNights_NewPage("Head to Head");
	if (page != NULL)
	{
		SpecialNights_AddColumn(page, 180, JUSTIFY_RIGHT, "");
		SpecialNights_AddColumn(page, 218, JUSTIFY_CENTER, "RACES");
		SpecialNights_AddColumn(page, 256, 0, "");
		SpecialNights_AddColumn(page, 470, JUSTIFY_CENTER, "CUPS");
		SpecialNights_FillPage(db, page,
		                       "SELECT r.player, r.ahead || '-' || r.behind, r.opponent, COALESCE(c.ahead || '-' || c.behind, '-') "
		                       "FROM head_to_head_races r LEFT JOIN head_to_head_cups c ON c.player = r.player AND c.opponent = r.opponent "
		                       "WHERE r.player < r.opponent ORDER BY r.races DESC, r.player",
		                       NULL, 0);
	}

	page = SpecialNights_NewPage("Best Laps");
	if (page != NULL)
	{
		SpecialNights_AddColumn(page, 16, 0, "TRACK");
		SpecialNights_AddColumn(page, 300, JUSTIFY_RIGHT, "LAP");
		SpecialNights_AddColumn(page, 316, 0, "BY");
		SpecialNights_FillPage(db, page, "SELECT track_name, best_lap_ms, best_lap_player FROM track_records ORDER BY level_id", NULL, 1u << 1);
	}

	page = SpecialNights_NewPage("Top Speeds");
	if (page != NULL)
	{
		SpecialNights_AddColumn(page, 16, 0, "TRACK");
		SpecialNights_AddColumn(page, 300, JUSTIFY_RIGHT, "SPEED");
		SpecialNights_AddColumn(page, 316, 0, "BY");
		SpecialNights_FillPage(db, page, "SELECT track_name, top_speed, top_speed_player FROM track_records ORDER BY level_id", NULL, 0);
	}

	// One page per player who has played a counted cup.
	if (sqlite3_prepare_v2(db, "SELECT player FROM player_cup_summary ORDER BY player", -1, &stmt, NULL) == SQLITE_OK)
	{
		while ((numPlayers < SPECIAL_NIGHTS_ROSTER_MAX) && (sqlite3_step(stmt) == SQLITE_ROW))
		{
			snprintf(players[numPlayers++], SPECIAL_NIGHTS_NAME_BYTES, "%s", (const char *)sqlite3_column_text(stmt, 0));
		}
		sqlite3_finalize(stmt);
	}

	for (s32 i = 0; i < numPlayers; i++)
	{
		page = SpecialNights_NewPage(players[i]);
		if (page == NULL)
		{
			break;
		}
		SpecialNights_AddColumn(page, 16, 0, "TRACK");
		SpecialNights_AddColumn(page, 250, JUSTIFY_RIGHT, "RCS");
		SpecialNights_AddColumn(page, 304, JUSTIFY_RIGHT, "WIN");
		SpecialNights_AddColumn(page, 384, JUSTIFY_RIGHT, "AVG");
		SpecialNights_AddColumn(page, 496, JUSTIFY_RIGHT, "BEST LAP");
		SpecialNights_FillPage(db, page,
		                       "SELECT track_name, races, wins, printf('%.2f', avg_place), best_lap_ms FROM player_track_summary "
		                       "WHERE player = ?1 ORDER BY level_id",
		                       players[i], 1u << 4);
	}

	sqlite3_close(db);

	if ((s_snNumPages == 0) || (s_snPages[0].numRows == 0))
	{
		snprintf(s_snMessage, sizeof(s_snMessage), "NO CUPS RECORDED YET");
	}
}

// ---------------------------------------------------------------------------
// Screen

internal void SpecialNights_ChartLine(s32 x0, s32 y0, s32 x1, s32 y1, u32 rgb, u32 *ot)
{
	struct PrimMem *primMem = &GAME_TRACKER->backBuffer->primMem;

	// Two passes a pixel apart so lines stay visible at high render scales.
	CTR_Box_DrawWirePrims((s16)x0, (s16)y0, (s16)x1, (s16)y1, rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff, ot, primMem);
	CTR_Box_DrawWirePrims((s16)x0, (s16)(y0 + 1), (s16)x1, (s16)(y1 + 1), rgb & 0xff, (rgb >> 8) & 0xff, (rgb >> 16) & 0xff, ot, primMem);
}

internal void SpecialNights_DrawChart(const struct SpecialNightsChart *chart, u32 *ot)
{
	char label[16];
	const s32 width = SN_CHART_X1 - SN_CHART_X0;
	const s32 height = SN_CHART_Y1 - SN_CHART_Y0;
	const s32 cups = (chart->numCups > 0) ? chart->numCups : 1;

	// Names get whatever the widest total leaves free.
	s32 totalChars = 1;
	for (s32 i = 0; i < chart->numPlayers; i++)
	{
		if ((s32)strlen(chart->totals[i]) > totalChars)
		{
			totalChars = (s32)strlen(chart->totals[i]);
		}
	}
	const s32 nameChars = (SN_CHART_TOTAL_X - SN_CHART_LEGEND_X - totalChars * SN_SMALL_CHAR_W - 4) / SN_SMALL_CHAR_W;

	// Text first: within one OT slot, later primitives are drawn underneath.
	for (s32 i = 0; i < chart->numPlayers; i++)
	{
		const s32 p = chart->order[i];
		const s32 y = SN_CHART_Y0 + i * SN_SCREEN_ROW_STEP;
		char name[SN_SCREEN_CELL_BYTES];

		snprintf(name, sizeof(name), "%.*s", (nameChars > 0) ? nameChars : 1, chart->names[p]);
		DecalFont_DrawLine(name, SN_CHART_LEGEND_X, (s16)y, FONT_SMALL, chart->color[p]);
		DecalFont_DrawLine((char *)chart->totals[p], SN_CHART_TOTAL_X, (s16)y, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	}

	snprintf(label, sizeof(label), "%d", chart->maxValue);
	DecalFont_DrawLine(label, SN_CHART_X0 - 4, SN_CHART_Y0 - 4, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	DecalFont_DrawLine("0", SN_CHART_X0 - 4, SN_CHART_Y1 - 4, FONT_SMALL, JUSTIFY_RIGHT | WHITE);
	snprintf(label, sizeof(label), "%d CUPS", chart->numCups);
	DecalFont_DrawLine(label, (SN_CHART_X0 + SN_CHART_X1) / 2, SN_CHART_Y1 + 4, FONT_SMALL, JUSTIFY_CENTER | PERIWINKLE);

	// Player lines (each starts at 0 before the year's first cup).
	for (s32 p = 0; p < chart->numPlayers; p++)
	{
		const u32 rgb = data.ptrColor[chart->color[p]][0];

		for (s32 cup = 1; cup <= chart->numCups; cup++)
		{
			const s32 xa = SN_CHART_X0 + ((cup - 1) * width) / cups;
			const s32 xb = SN_CHART_X0 + (cup * width) / cups;
			const s32 ya = SN_CHART_Y1 - (chart->points[p][cup - 1] * height) / chart->maxValue;
			const s32 yb = SN_CHART_Y1 - (chart->points[p][cup] * height) / chart->maxValue;

			SpecialNights_ChartLine(xa, ya, xb, yb, rgb, ot);
		}
	}

	// Axes and quarter gridlines behind the lines.
	for (s32 q = 1; q < 4; q++)
	{
		const s32 y = SN_CHART_Y1 - (q * height) / 4;
		CTR_Box_DrawWirePrims(SN_CHART_X0, (s16)y, SN_CHART_X1, (s16)y, 70, 70, 90, ot, &GAME_TRACKER->backBuffer->primMem);
	}
	CTR_Box_DrawWirePrims(SN_CHART_X0, SN_CHART_Y1, SN_CHART_X1, SN_CHART_Y1, 200, 200, 200, ot, &GAME_TRACKER->backBuffer->primMem);
	CTR_Box_DrawWirePrims(SN_CHART_X0, SN_CHART_Y0, SN_CHART_X0, SN_CHART_Y1, 200, 200, 200, ot, &GAME_TRACKER->backBuffer->primMem);
}

void MM_SpecialNightsStats_Init(void)
{
	s_snPage = 0;
	s_snScroll = 0;
	SpecialNights_LoadRoster();
	SpecialNights_LoadStatsPages();
}

struct RectMenu *MM_SpecialNightsStats_Menu(void)
{
	return &s_snStatsMenu;
}

void MM_SpecialNightsStats_MenuProc(struct RectMenu *menu)
{
	u32 *ot = GAME_TRACKER->backBuffer->otMem.uiOT;
	u32 taps = sdata->AnyPlayerTap;
	struct SpecialNightsScreenPage *page = (s_snNumPages > 0) ? &s_snPages[s_snPage] : NULL;
	char footer[64];
	RECT panel;

	(void)menu;

	if ((taps & (BTN_TRIANGLE | BTN_SQUARE_one)) != 0)
	{
		OtherFX_Play(2, 1);
		RECTMENU_ClearInput();
		MM_JumpTo_Title_Returning();
		return;
	}

	if ((s_snNumPages > 1) && ((taps & (BTN_RIGHT | BTN_R1 | BTN_LEFT | BTN_L1)) != 0))
	{
		s_snPage += ((taps & (BTN_RIGHT | BTN_R1)) != 0) ? 1 : -1;
		s_snPage = (s_snPage + s_snNumPages) % s_snNumPages;
		s_snScroll = 0;
		page = &s_snPages[s_snPage];
		OtherFX_Play(0, 1);
	}

	if ((page != NULL) && (page->numRows > SN_SCREEN_VISIBLE))
	{
		if (((taps & BTN_DOWN) != 0) && (s_snScroll + SN_SCREEN_VISIBLE < page->numRows))
		{
			s_snScroll++;
		}
		if (((taps & BTN_UP) != 0) && (s_snScroll > 0))
		{
			s_snScroll--;
		}
	}

	DecalFont_DrawLine("SPESSU STATS", 256, 14, FONT_BIG, JUSTIFY_CENTER | ORANGE);

	if ((page == NULL) || (s_snMessage[0] != 0))
	{
		DecalFont_DrawLine((s_snMessage[0] != 0) ? s_snMessage : "NO CUPS RECORDED YET", 256, 100, FONT_SMALL, JUSTIFY_CENTER | WHITE);
		DecalFont_DrawLine("TRIANGLE BACK", 256, 194, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
	}
	else if (page->chart >= 0)
	{
		DecalFont_DrawLine(page->title, 256, 34, FONT_SMALL, JUSTIFY_CENTER | WHITE);
		SpecialNights_DrawChart(&s_snCharts[page->chart], ot);
		snprintf(footer, sizeof(footer), "L1/R1 PAGE %d/%d  TRIANGLE BACK", s_snPage + 1, s_snNumPages);
		DecalFont_DrawLine(footer, 256, 194, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
	}
	else
	{
		DecalFont_DrawLine(page->title, 256, 34, FONT_SMALL, JUSTIFY_CENTER | WHITE);

		for (s32 col = 0; col < page->numCols; col++)
		{
			DecalFont_DrawLine(page->header[col], page->colX[col], SN_SCREEN_ROW_Y0 - 11, FONT_SMALL, page->colJustify[col] | PERIWINKLE);
		}

		if (page->numRows == 0)
		{
			DecalFont_DrawLine("NOTHING YET", 256, SN_SCREEN_ROW_Y0 + 20, FONT_SMALL, JUSTIFY_CENTER | WHITE);
		}

		for (s32 line = 0; (line < SN_SCREEN_VISIBLE) && (s_snScroll + line < page->numRows); line++)
		{
			s32 row = s_snScroll + line;

			for (s32 col = 0; col < page->numCols; col++)
			{
				DecalFont_DrawLine(page->cells[row][col], page->colX[col], SN_SCREEN_ROW_Y0 + line * SN_SCREEN_ROW_STEP, FONT_SMALL,
				                   page->colJustify[col] | ((col == 0) ? ORANGE : WHITE));
			}
		}

		snprintf(footer, sizeof(footer), "L1/R1 PAGE %d/%d  %sTRIANGLE BACK", s_snPage + 1, s_snNumPages,
		         (page->numRows > SN_SCREEN_VISIBLE) ? "UP/DOWN  " : "");
		DecalFont_DrawLine(footer, 256, 194, FONT_SMALL, JUSTIFY_CENTER | ORANGE);
	}

	panel.x = 6;
	panel.y = 6;
	panel.w = 500;
	panel.h = 204;
	RECTMENU_DrawInnerRect(&panel, 4, ot);
}

#endif
