#include <common.h>

// Special Nights player profiles and cup statistics (native builds only).
//
// Each human picks a player profile on character select (L2/R2). A cup is
// recorded only when every human has a distinct profile. Every scored race and
// every finished cup is written straight into the SQLite database
// SPECIAL_NIGHTS_DB_FILE (schema: tools/special_nights/schema.sql);
// tools/special_nights/stats.py prints reports from it.

#if defined(CTR_NATIVE)
#include <platform/native_log.h>
#include <sqlite3.h>
#include <special_nights_schema.h>
#include <time.h>

#define SPECIAL_NIGHTS_DB_FILE          "special_nights.db"
#define SPECIAL_NIGHTS_PLAYERS_FILE     "special_nights_players.txt"
#define SPECIAL_NIGHTS_ROSTER_MAX       16
#define SPECIAL_NIGHTS_NAME_BYTES       48
#define SPECIAL_NIGHTS_DISPLAY_BYTES    24
#define SPECIAL_NIGHTS_PROFILE_NONE     (-1)
#define SPECIAL_NIGHTS_STATS_DRIVERS    8
#define SPECIAL_NIGHTS_TICKS_PER_SECOND 960
// Shorter "laps" come from crossing the line at the start, not from driving one.
#define SPECIAL_NIGHTS_MIN_LAP_TICKS    (5 * SPECIAL_NIGHTS_TICKS_PER_SECOND)

global_variable const char *s_specialNightsDefaultRoster[] = {
    "JustusJulma", "Zamiza", "Arturo Senna", "Memuli9000", "Fröttis", "Tom Lennon",
};

global_variable char s_specialNightsRoster[SPECIAL_NIGHTS_ROSTER_MAX][SPECIAL_NIGHTS_NAME_BYTES];
global_variable char s_specialNightsRosterDisplay[SPECIAL_NIGHTS_ROSTER_MAX][SPECIAL_NIGHTS_DISPLAY_BYTES];
global_variable s32 s_specialNightsRosterCount = -1;

global_variable s32 s_specialNightsProfile[SPECIAL_NIGHTS_PLAYER_COUNT] = {
    SPECIAL_NIGHTS_PROFILE_NONE,
    SPECIAL_NIGHTS_PROFILE_NONE,
    SPECIAL_NIGHTS_PROFILE_NONE,
    SPECIAL_NIGHTS_PROFILE_NONE,
};

// ---------------------------------------------------------------------------
// Roster

// Folds UTF-8 Latin-1 letters to the uppercase ASCII the race font can draw.
internal void SpecialNights_FoldDisplayName(const char *name, char *out, u32 outBytes)
{
	static const char latin1Fold[65] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTSAAAAAAACEEEEIIIIDNOOOOO/OUUUUYTY";
	const u8 *src = (const u8 *)name;
	u32 length = 0;

	while ((*src != 0) && (length + 1 < outBytes))
	{
		u8 c = *src++;

		if (c < 0x80)
		{
			out[length++] = ((c >= 'a') && (c <= 'z')) ? (char)(c - 'a' + 'A') : (char)c;
		}
		else if ((c == 0xC3) && (*src >= 0x80) && (*src < 0xC0))
		{
			out[length++] = latin1Fold[*src++ - 0x80];
		}
		else
		{
			// Other multi-byte characters: skip the continuation bytes.
			while ((*src & 0xC0) == 0x80)
			{
				src++;
			}
		}
	}

	out[length] = 0;
}

internal void SpecialNights_AddRosterName(const char *name)
{
	s32 index = s_specialNightsRosterCount;

	if ((index >= SPECIAL_NIGHTS_ROSTER_MAX) || (name[0] == 0))
	{
		return;
	}

	snprintf(s_specialNightsRoster[index], sizeof(s_specialNightsRoster[index]), "%s", name);
	SpecialNights_FoldDisplayName(name, s_specialNightsRosterDisplay[index], sizeof(s_specialNightsRosterDisplay[index]));
	s_specialNightsRosterCount++;
}

// special_nights_players.txt (UTF-8, one name per line) replaces the built-in roster.
internal void SpecialNights_LoadRoster(void)
{
	char line[SPECIAL_NIGHTS_NAME_BYTES * 2];
	FILE *file;

	if (s_specialNightsRosterCount >= 0)
	{
		return;
	}

	s_specialNightsRosterCount = 0;
	file = fopen(SPECIAL_NIGHTS_PLAYERS_FILE, "rb");
	if (file != NULL)
	{
		while (fgets(line, sizeof(line), file) != NULL)
		{
			char *name = line;
			size_t length;

			// Skip a UTF-8 byte order mark and surrounding whitespace; '#' starts a comment.
			if (((u8)name[0] == 0xEF) && ((u8)name[1] == 0xBB) && ((u8)name[2] == 0xBF))
			{
				name += 3;
			}
			while ((*name == ' ') || (*name == '\t'))
			{
				name++;
			}
			length = strlen(name);
			while ((length > 0) && ((name[length - 1] == '\n') || (name[length - 1] == '\r') || (name[length - 1] == ' ') || (name[length - 1] == '\t')))
			{
				name[--length] = 0;
			}
			if ((length > 0) && (name[0] != '#'))
			{
				SpecialNights_AddRosterName(name);
			}
		}
		fclose(file);
	}

	if (s_specialNightsRosterCount == 0)
	{
		for (u32 i = 0; i < len(s_specialNightsDefaultRoster); i++)
		{
			SpecialNights_AddRosterName(s_specialNightsDefaultRoster[i]);
		}
	}
	else
	{
		Platform_Log("[Special Nights] %d players loaded from %s\n", s_specialNightsRosterCount, SPECIAL_NIGHTS_PLAYERS_FILE);
	}
}

internal b32 SpecialNights_ProfileTaken(s32 profile, s32 exceptPlayer)
{
	for (s32 playerIndex = 0; playerIndex < GAME_TRACKER->numPlyrNextGame && playerIndex < SPECIAL_NIGHTS_PLAYER_COUNT; playerIndex++)
	{
		if ((playerIndex != exceptPlayer) && (s_specialNightsProfile[playerIndex] == profile))
		{
			return true;
		}
	}

	return false;
}

void SpecialNights_CycleProfile(s32 playerIndex, u32 buttonTaps)
{
	s32 step;
	s32 profile;

	if ((playerIndex < 0) || (playerIndex >= SPECIAL_NIGHTS_PLAYER_COUNT) || ((buttonTaps & (BTN_L2 | BTN_R2)) == 0))
	{
		return;
	}

	SpecialNights_LoadRoster();

	// Cycle "no player" -> roster names -> "no player", skipping names other players hold.
	step = ((buttonTaps & BTN_R2) != 0) ? 1 : -1;
	profile = s_specialNightsProfile[playerIndex];
	for (s32 tries = 0; tries <= s_specialNightsRosterCount; tries++)
	{
		profile += step;
		if (profile >= s_specialNightsRosterCount)
		{
			profile = SPECIAL_NIGHTS_PROFILE_NONE;
		}
		else if (profile < SPECIAL_NIGHTS_PROFILE_NONE)
		{
			profile = s_specialNightsRosterCount - 1;
		}

		if ((profile == SPECIAL_NIGHTS_PROFILE_NONE) || !SpecialNights_ProfileTaken(profile, playerIndex))
		{
			break;
		}
	}

	s_specialNightsProfile[playerIndex] = profile;
	OtherFX_Play(0, 1);
}

void SpecialNights_DrawProfile(s32 playerIndex, s32 posX, s32 posY)
{
	s32 profile;

	if ((playerIndex < 0) || (playerIndex >= SPECIAL_NIGHTS_PLAYER_COUNT))
	{
		return;
	}

	SpecialNights_LoadRoster();
	profile = s_specialNightsProfile[playerIndex];

	// A name another player has since taken (roster reload, player count change) no longer counts.
	if ((profile != SPECIAL_NIGHTS_PROFILE_NONE) && ((profile >= s_specialNightsRosterCount) || SpecialNights_ProfileTaken(profile, playerIndex)))
	{
		s_specialNightsProfile[playerIndex] = profile = SPECIAL_NIGHTS_PROFILE_NONE;
	}

	if (profile == SPECIAL_NIGHTS_PROFILE_NONE)
	{
		DecalFont_DrawLine("L2/R2 PLAYER", (s16)posX, (s16)posY, FONT_SMALL, (JUSTIFY_CENTER | RED));
	}
	else
	{
		DecalFont_DrawLine(s_specialNightsRosterDisplay[profile], (s16)posX, (s16)posY, FONT_SMALL, (JUSTIFY_CENTER | WHITE));
	}
}

// Every human holds a distinct profile.
internal b32 SpecialNights_ProfilesConfirmed(s32 numHumans)
{
	if ((numHumans <= 0) || (numHumans > SPECIAL_NIGHTS_PLAYER_COUNT))
	{
		return false;
	}

	SpecialNights_LoadRoster();
	for (s32 playerIndex = 0; playerIndex < numHumans; playerIndex++)
	{
		s32 profile = s_specialNightsProfile[playerIndex];

		if ((profile == SPECIAL_NIGHTS_PROFILE_NONE) || (profile >= s_specialNightsRosterCount))
		{
			return false;
		}
		for (s32 other = 0; other < playerIndex; other++)
		{
			if (s_specialNightsProfile[other] == profile)
			{
				return false;
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Per-race sampling

struct SpecialNightsDriverStats
{
	u32 sampledFrames;
	double speedSum;
	s32 topSpeed;
	s32 turboTicks;
	s32 lapIndexSeen;
	s32 lapStartTicks;
	s32 bestLapTicks;
	s32 finishTicks;
};

global_variable struct SpecialNightsDriverStats s_specialNightsDriverStats[SPECIAL_NIGHTS_STATS_DRIVERS];
global_variable b32 s_specialNightsCupRecording;
global_variable char s_specialNightsCupRun[32];

internal b32 SpecialNights_InStatsCup(struct GameTracker *gGT)
{
	return ((gGT->gameMode2 & CUP_ANY_KIND) != 0) && ((gGT->gameMode1 & (ADVENTURE_MODE | BATTLE_MODE | TIME_TRIAL)) == 0);
}

internal void SpecialNights_ResetRaceStats(void)
{
	memset(s_specialNightsDriverStats, 0, sizeof(s_specialNightsDriverStats));
	for (s32 i = 0; i < SPECIAL_NIGHTS_STATS_DRIVERS; i++)
	{
		s_specialNightsDriverStats[i].bestLapTicks = -1;
		s_specialNightsDriverStats[i].finishTicks = -1;
	}
}

// Called once per unpaused game frame.
void SpecialNights_StatsFrame(struct GameTracker *gGT)
{
	if (!SpecialNights_InStatsCup(gGT) || (gGT->level1 == NULL))
	{
		return;
	}

	// Countdown: a new race is about to start.
	if (gGT->trafficLightsTimer > 0)
	{
		SpecialNights_ResetRaceStats();
		return;
	}

	if ((gGT->gameMode1 & END_OF_RACE) != 0)
	{
		return;
	}

	for (s32 driverIndex = 0; driverIndex < SPECIAL_NIGHTS_STATS_DRIVERS; driverIndex++)
	{
		struct Driver *d = gGT->drivers[driverIndex];
		struct SpecialNightsDriverStats *stats = &s_specialNightsDriverStats[driverIndex];
		s32 now;
		s32 speed;

		if ((d == NULL) || (stats->finishTicks >= 0))
		{
			continue;
		}

		// Humans carry an exact race clock; AI use the shared event clock.
		now = (driverIndex < gGT->numPlyrCurrGame) ? d->timeElapsedInRace : gGT->elapsedEventTime;

		if ((s32)d->lapIndex > stats->lapIndexSeen)
		{
			s32 lapTicks = now - stats->lapStartTicks;

			if ((lapTicks >= SPECIAL_NIGHTS_MIN_LAP_TICKS) && ((stats->bestLapTicks < 0) || (lapTicks < stats->bestLapTicks)))
			{
				stats->bestLapTicks = lapTicks;
			}
			stats->lapIndexSeen = d->lapIndex;
			stats->lapStartTicks = now;
		}

		if ((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0)
		{
			stats->finishTicks = now;
			continue;
		}

		speed = d->speedApprox;
		if (speed < 0)
		{
			speed = -speed;
		}
		stats->sampledFrames++;
		stats->speedSum += speed;
		if (speed > stats->topSpeed)
		{
			stats->topSpeed = speed;
		}
		if (d->reserves > 0)
		{
			stats->turboTicks += gGT->elapsedTimeMS;
		}
	}
}

// ---------------------------------------------------------------------------
// Database

internal void SpecialNights_DbLogError(sqlite3 *db, const char *what)
{
	Platform_LogWarn("[Special Nights] database: %s failed: %s\n", what, (db != NULL) ? sqlite3_errmsg(db) : "out of memory");
}

internal b32 SpecialNights_DbExec(sqlite3 *db, const char *sql, const char *what)
{
	char *error = NULL;

	if (sqlite3_exec(db, sql, NULL, NULL, &error) != SQLITE_OK)
	{
		Platform_LogWarn("[Special Nights] database: %s failed: %s\n", what, (error != NULL) ? error : "?");
		sqlite3_free(error);
		return false;
	}

	return true;
}

// Opens (creating if needed) the stats database. Kept closed between writes so
// other programs can read it while the game runs.
internal sqlite3 *SpecialNights_DbOpen(void)
{
	sqlite3 *db = NULL;

	if (sqlite3_open_v2(SPECIAL_NIGHTS_DB_FILE, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
	{
		SpecialNights_DbLogError(db, "open " SPECIAL_NIGHTS_DB_FILE);
		sqlite3_close(db);
		return NULL;
	}

	sqlite3_busy_timeout(db, 2000);
	if (!SpecialNights_DbExec(db, SPECIAL_NIGHTS_SCHEMA_SQL, "schema"))
	{
		sqlite3_close(db);
		return NULL;
	}

	return db;
}

internal sqlite3_stmt *SpecialNights_DbPrepare(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *stmt = NULL;

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		SpecialNights_DbLogError(db, "prepare");
		return NULL;
	}

	return stmt;
}

// Runs a prepared statement to completion and resets it for the next bind.
internal b32 SpecialNights_DbStep(sqlite3 *db, sqlite3_stmt *stmt, const char *what)
{
	b32 ok = (sqlite3_step(stmt) == SQLITE_DONE);

	if (!ok)
	{
		SpecialNights_DbLogError(db, what);
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
	return ok;
}

internal void SpecialNights_DbBindText(sqlite3_stmt *stmt, s32 index, const char *text)
{
	if (text != NULL)
	{
		sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT);
	}
	else
	{
		sqlite3_bind_null(stmt, index);
	}
}

internal void SpecialNights_DbBindTicksMs(sqlite3_stmt *stmt, s32 index, s32 ticks)
{
	if (ticks >= 0)
	{
		sqlite3_bind_int64(stmt, index, ((s64)ticks * 1000) / SPECIAL_NIGHTS_TICKS_PER_SECOND);
	}
	else
	{
		sqlite3_bind_null(stmt, index);
	}
}

internal void SpecialNights_LocalTime(char *out, u32 outBytes, const char *format)
{
	time_t now = time(NULL);
	struct tm *local = localtime(&now);

	if ((local == NULL) || (strftime(out, outBytes, format, local) == 0))
	{
		snprintf(out, outBytes, "%ld", (long)now);
	}
}

internal const char *SpecialNights_DriverPlayer(struct GameTracker *gGT, s32 driverID)
{
	return (driverID < gGT->numPlyrCurrGame) ? s_specialNightsRoster[s_specialNightsProfile[driverID]] : NULL;
}

internal const char *SpecialNights_CupName(struct GameTracker *gGT)
{
	s32 cupID = gGT->cup.cupID;

	return ((cupID >= 0) && (cupID < (s32)len(data.ArcadeCups))) ? sdata->lngStrings[data.ArcadeCups[cupID].lngIndex_CupName] : NULL;
}

// Inserts the players and the cup run if missing (every race does this, so a
// race always has its parents even if the first write failed).
internal b32 SpecialNights_DbInsertCupRun(sqlite3 *db, struct GameTracker *gGT, const char *timestamp)
{
	sqlite3_stmt *player = SpecialNights_DbPrepare(db, "INSERT OR IGNORE INTO players (name) VALUES (?1)");
	sqlite3_stmt *cup = SpecialNights_DbPrepare(db, "INSERT OR IGNORE INTO cup_runs (cup_run, started_at, mode, cup_id, cup_name, difficulty, humans, drivers_in_cup) "
	                                                "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
	b32 ok = (player != NULL) && (cup != NULL);

	for (s32 driverID = 0; ok && (driverID < gGT->numPlyrCurrGame); driverID++)
	{
		SpecialNights_DbBindText(player, 1, SpecialNights_DriverPlayer(gGT, driverID));
		ok = SpecialNights_DbStep(db, player, "insert player");
	}

	if (ok)
	{
		SpecialNights_DbBindText(cup, 1, s_specialNightsCupRun);
		SpecialNights_DbBindText(cup, 2, timestamp);
		SpecialNights_DbBindText(cup, 3, (gGT->numBotsNextGame == 0) ? "vs" : "arcade");
		sqlite3_bind_int(cup, 4, gGT->cup.cupID);
		SpecialNights_DbBindText(cup, 5, SpecialNights_CupName(gGT));
		sqlite3_bind_int(cup, 6, gGT->arcadeDifficulty);
		sqlite3_bind_int(cup, 7, gGT->numPlyrCurrGame);
		sqlite3_bind_int(cup, 8, gGT->numPlyrCurrGame + gGT->numBotsNextGame);
		ok = SpecialNights_DbStep(db, cup, "insert cup run");
	}

	sqlite3_finalize(player);
	sqlite3_finalize(cup);
	return ok;
}

// Commits on success, rolls back otherwise, and closes the database.
internal void SpecialNights_DbFinish(sqlite3 *db, b32 ok, const char *what)
{
	if (ok && SpecialNights_DbExec(db, "COMMIT", "commit"))
	{
		Platform_Log("[Special Nights] %s saved to %s (cup %s)\n", what, SPECIAL_NIGHTS_DB_FILE, s_specialNightsCupRun);
	}
	else
	{
		SpecialNights_DbExec(db, "ROLLBACK", "rollback");
		Platform_LogWarn("[Special Nights] %s was NOT saved (cup %s)\n", what, s_specialNightsCupRun);
	}
	sqlite3_close(db);
}

// ---------------------------------------------------------------------------
// Race and cup records

// Called on the first frame of track standings, after this race's points are added.
void SpecialNights_StatsRecordRace(struct GameTracker *gGT, const int *pointsPerPosition)
{
	char timestamp[32];
	sqlite3 *db;
	sqlite3_stmt *race;
	sqlite3_stmt *result;
	s64 raceID;
	s32 numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	s32 levelID = gGT->levelID;
	b32 ok;

	if (!SpecialNights_InStatsCup(gGT))
	{
		return;
	}

	if (gGT->cup.trackIndex == 0)
	{
		// First race of a cup: open a new cup run if every human is a confirmed player.
		s_specialNightsCupRecording = SpecialNights_ProfilesConfirmed(gGT->numPlyrCurrGame);
		SpecialNights_LocalTime(s_specialNightsCupRun, sizeof(s_specialNightsCupRun), "%Y%m%d-%H%M%S");
		Platform_Log("[Special Nights] cup %s %s\n", s_specialNightsCupRun,
		             s_specialNightsCupRecording ? "is being recorded" : "is NOT recorded (every human needs a player name)");
	}

	if (!s_specialNightsCupRecording)
	{
		return;
	}

	if (numDrivers > SPECIAL_NIGHTS_STATS_DRIVERS)
	{
		numDrivers = SPECIAL_NIGHTS_STATS_DRIVERS;
	}

	db = SpecialNights_DbOpen();
	if (db == NULL)
	{
		return;
	}

	SpecialNights_LocalTime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S");
	race = NULL;
	result = NULL;
	ok = SpecialNights_DbExec(db, "BEGIN IMMEDIATE", "begin") && SpecialNights_DbInsertCupRun(db, gGT, timestamp);

	// A repeated race (same cup run and track) replaces the earlier record; its results cascade.
	if (ok)
	{
		race = SpecialNights_DbPrepare(db, "DELETE FROM races WHERE cup_run = ?1 AND track_index = ?2");
		ok = (race != NULL);
		if (ok)
		{
			SpecialNights_DbBindText(race, 1, s_specialNightsCupRun);
			sqlite3_bind_int(race, 2, gGT->cup.trackIndex);
			ok = SpecialNights_DbStep(db, race, "replace race");
		}
		sqlite3_finalize(race);
		race = NULL;
	}

	if (ok)
	{
		race = SpecialNights_DbPrepare(db, "INSERT INTO races (cup_run, track_index, level_id, track_name, laps, recorded_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
		ok = (race != NULL);
		if (ok)
		{
			SpecialNights_DbBindText(race, 1, s_specialNightsCupRun);
			sqlite3_bind_int(race, 2, gGT->cup.trackIndex);
			sqlite3_bind_int(race, 3, levelID);
			SpecialNights_DbBindText(race, 4,
			                         ((levelID >= 0) && (levelID < (s32)len(data.metaDataLEV))) ? sdata->lngStrings[data.metaDataLEV[levelID].name_LNG] : NULL);
			sqlite3_bind_int(race, 5, gGT->numLaps);
			SpecialNights_DbBindText(race, 6, timestamp);
			ok = SpecialNights_DbStep(db, race, "insert race");
		}
	}
	raceID = sqlite3_last_insert_rowid(db);

	if (ok)
	{
		result = SpecialNights_DbPrepare(db, "INSERT INTO race_results (race_id, slot, human, player, character, character_id, engine, place, points, cup_points, "
		                                     "finished, race_time_ms, best_lap_ms, turbo_ms, avg_speed, top_speed, turbos) "
		                                     "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17)");
		ok = (result != NULL);
	}

	for (s32 place = 0; ok && (place < numDrivers); place++)
	{
		struct Driver *d = gGT->driversInRaceOrder[place];
		struct SpecialNightsDriverStats *stats;
		s32 driverID;
		s32 characterID;

		if (d == NULL)
		{
			continue;
		}

		driverID = d->driverID;
		characterID = GAME_CHARACTER_IDS[driverID];
		stats = &s_specialNightsDriverStats[driverID & (SPECIAL_NIGHTS_STATS_DRIVERS - 1)];

		sqlite3_bind_int64(result, 1, raceID);
		sqlite3_bind_int(result, 2, driverID);
		sqlite3_bind_int(result, 3, driverID < gGT->numPlyrCurrGame);
		SpecialNights_DbBindText(result, 4, SpecialNights_DriverPlayer(gGT, driverID));
		SpecialNights_DbBindText(result, 5, GAME_CHARACTER_METADATA[characterID].name_Debug);
		sqlite3_bind_int(result, 6, characterID);
		SpecialNights_DbBindText(result, 7, s_specialNightsEngineLabels[SpecialNights_DriverEngineClass(driverID)]);
		sqlite3_bind_int(result, 8, place + 1);
		sqlite3_bind_int(result, 9, (gGT->numBotsNextGame == 0) ? (gGT->numPlyrCurrGame - (place + 1)) : pointsPerPosition[place]);
		sqlite3_bind_int(result, 10, gGT->cup.points[driverID]);
		sqlite3_bind_int(result, 11, stats->finishTicks >= 0);
		SpecialNights_DbBindTicksMs(result, 12, stats->finishTicks);
		SpecialNights_DbBindTicksMs(result, 13, stats->bestLapTicks);
		SpecialNights_DbBindTicksMs(result, 14, stats->turboTicks);
		sqlite3_bind_int64(result, 15, (stats->sampledFrames != 0) ? (s64)(stats->speedSum / stats->sampledFrames) : 0);
		sqlite3_bind_int(result, 16, stats->topSpeed);
		sqlite3_bind_int(result, 17, d->numTurbos);
		ok = SpecialNights_DbStep(db, result, "insert race result");
	}

	sqlite3_finalize(race);
	sqlite3_finalize(result);
	SpecialNights_DbFinish(db, ok, "race");
}

// Called when the last race of a cup has been scored, before cup points reset.
void SpecialNights_StatsRecordCupEnd(struct GameTracker *gGT)
{
	char timestamp[32];
	sqlite3 *db;
	sqlite3_stmt *stmt = NULL;
	s32 numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	b32 ok;

	if (!SpecialNights_InStatsCup(gGT) || !s_specialNightsCupRecording)
	{
		return;
	}
	s_specialNightsCupRecording = false;

	if (numDrivers > SPECIAL_NIGHTS_STATS_DRIVERS)
	{
		numDrivers = SPECIAL_NIGHTS_STATS_DRIVERS;
	}

	db = SpecialNights_DbOpen();
	if (db == NULL)
	{
		return;
	}

	SpecialNights_LocalTime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S");
	ok = SpecialNights_DbExec(db, "BEGIN IMMEDIATE", "begin") && SpecialNights_DbInsertCupRun(db, gGT, timestamp);

	if (ok)
	{
		stmt = SpecialNights_DbPrepare(db, "DELETE FROM cup_results WHERE cup_run = ?1");
		ok = (stmt != NULL);
		if (ok)
		{
			SpecialNights_DbBindText(stmt, 1, s_specialNightsCupRun);
			ok = SpecialNights_DbStep(db, stmt, "clear cup results");
		}
		sqlite3_finalize(stmt);
		stmt = NULL;
	}

	if (ok)
	{
		stmt = SpecialNights_DbPrepare(db, "INSERT INTO cup_results (cup_run, slot, human, player, character, engine, final_place, points) "
		                                   "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
		ok = (stmt != NULL);
	}

	for (s32 rank = 0; ok && (rank < SPECIAL_NIGHTS_STATS_DRIVERS); rank++)
	{
		s32 driverID = data.cupPositionPerPlayer[rank];

		if ((driverID < 0) || (driverID >= numDrivers) || (gGT->drivers[driverID] == NULL))
		{
			continue;
		}

		SpecialNights_DbBindText(stmt, 1, s_specialNightsCupRun);
		sqlite3_bind_int(stmt, 2, driverID);
		sqlite3_bind_int(stmt, 3, driverID < gGT->numPlyrCurrGame);
		SpecialNights_DbBindText(stmt, 4, SpecialNights_DriverPlayer(gGT, driverID));
		SpecialNights_DbBindText(stmt, 5, GAME_CHARACTER_METADATA[GAME_CHARACTER_IDS[driverID]].name_Debug);
		SpecialNights_DbBindText(stmt, 6, s_specialNightsEngineLabels[SpecialNights_DriverEngineClass(driverID)]);
		sqlite3_bind_int(stmt, 7, rank + 1);
		sqlite3_bind_int(stmt, 8, gGT->cup.points[driverID]);
		ok = SpecialNights_DbStep(db, stmt, "insert cup result");
	}
	sqlite3_finalize(stmt);
	stmt = NULL;

	if (ok)
	{
		stmt = SpecialNights_DbPrepare(db, "UPDATE cup_runs SET completed = 1, ended_at = ?2 WHERE cup_run = ?1");
		ok = (stmt != NULL);
		if (ok)
		{
			SpecialNights_DbBindText(stmt, 1, s_specialNightsCupRun);
			SpecialNights_DbBindText(stmt, 2, timestamp);
			ok = SpecialNights_DbStep(db, stmt, "complete cup run");
		}
		sqlite3_finalize(stmt);
	}

	SpecialNights_DbFinish(db, ok, "cup result");
}

#endif
