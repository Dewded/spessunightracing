#include <common.h>

// Special Nights player profiles and cup statistics (native builds only).
//
// Each human picks a player profile on character select (L2/R2). A cup is
// recorded only when every human has a distinct profile. Every finished race
// and every finished cup appends one JSON line to SPECIAL_NIGHTS_STATS_FILE;
// tools/special_nights/stats.py builds a SQLite database and reports from it.

#if defined(CTR_NATIVE)
#include <platform/native_log.h>
#include <stdarg.h>
#include <time.h>

#define SPECIAL_NIGHTS_STATS_FILE       "special_nights_stats.jsonl"
#define SPECIAL_NIGHTS_PLAYERS_FILE     "special_nights_players.txt"
#define SPECIAL_NIGHTS_STATS_VERSION    1
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
// JSON lines

struct SpecialNightsJson
{
	char text[8192];
	u32 length;
};

internal void SpecialNights_JsonRaw(struct SpecialNightsJson *json, const char *fmt, ...)
{
	va_list args;
	s32 written;

	if (json->length >= sizeof(json->text))
	{
		return;
	}

	va_start(args, fmt);
	written = vsnprintf(&json->text[json->length], sizeof(json->text) - json->length, fmt, args);
	va_end(args);

	if (written > 0)
	{
		json->length += (u32)written;
		if (json->length > sizeof(json->text) - 1)
		{
			json->length = sizeof(json->text) - 1;
		}
	}
}

internal void SpecialNights_JsonString(struct SpecialNightsJson *json, const char *str)
{
	SpecialNights_JsonRaw(json, "\"");
	for (const u8 *c = (const u8 *)((str != NULL) ? str : ""); *c != 0; c++)
	{
		if ((*c == '"') || (*c == '\\'))
		{
			SpecialNights_JsonRaw(json, "\\%c", *c);
		}
		else if (*c < 0x20)
		{
			SpecialNights_JsonRaw(json, "\\u%04x", *c);
		}
		else
		{
			SpecialNights_JsonRaw(json, "%c", *c);
		}
	}
	SpecialNights_JsonRaw(json, "\"");
}

internal void SpecialNights_JsonTicksMs(struct SpecialNightsJson *json, s32 ticks)
{
	if (ticks < 0)
	{
		SpecialNights_JsonRaw(json, "null");
	}
	else
	{
		SpecialNights_JsonRaw(json, "%ld", (long)(((s64)ticks * 1000) / SPECIAL_NIGHTS_TICKS_PER_SECOND));
	}
}

internal void SpecialNights_JsonDriverIdentity(struct SpecialNightsJson *json, struct GameTracker *gGT, s32 driverID)
{
	s32 characterID = GAME_CHARACTER_IDS[driverID];
	b32 human = (driverID < gGT->numPlyrCurrGame);

	SpecialNights_JsonRaw(json, "\"slot\":%d,\"human\":%s,\"player\":", driverID, human ? "true" : "false");
	if (human)
	{
		SpecialNights_JsonString(json, s_specialNightsRoster[s_specialNightsProfile[driverID]]);
	}
	else
	{
		SpecialNights_JsonRaw(json, "null");
	}
	SpecialNights_JsonRaw(json, ",\"character_id\":%d,\"character\":", characterID);
	SpecialNights_JsonString(json, GAME_CHARACTER_METADATA[characterID].name_Debug);
	SpecialNights_JsonRaw(json, ",\"engine\":");
	SpecialNights_JsonString(json, s_specialNightsEngineLabels[SpecialNights_DriverEngineClass(driverID)]);
}

internal void SpecialNights_JsonEventHeader(struct SpecialNightsJson *json, struct GameTracker *gGT, const char *type)
{
	char timestamp[32];
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	s32 cupID = gGT->cup.cupID;

	if ((local == NULL) || (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", local) == 0))
	{
		timestamp[0] = 0;
	}

	SpecialNights_JsonRaw(json, "{\"type\":\"%s\",\"version\":%d,\"cup_run\":", type, SPECIAL_NIGHTS_STATS_VERSION);
	SpecialNights_JsonString(json, s_specialNightsCupRun);
	SpecialNights_JsonRaw(json, ",\"recorded_at\":");
	SpecialNights_JsonString(json, timestamp);
	SpecialNights_JsonRaw(json, ",\"mode\":\"%s\",\"cup_id\":%d,\"cup_name\":", (gGT->numBotsNextGame == 0) ? "vs" : "arcade", cupID);
	SpecialNights_JsonString(json, ((cupID >= 0) && (cupID < (s32)len(data.ArcadeCups))) ? sdata->lngStrings[data.ArcadeCups[cupID].lngIndex_CupName] : "");
	SpecialNights_JsonRaw(json, ",\"difficulty\":%d,\"humans\":%d,\"drivers_in_cup\":%d", gGT->arcadeDifficulty, gGT->numPlyrCurrGame,
	                      gGT->numPlyrCurrGame + gGT->numBotsNextGame);
}

internal void SpecialNights_AppendJsonLine(struct SpecialNightsJson *json)
{
	FILE *file = fopen(SPECIAL_NIGHTS_STATS_FILE, "ab");

	if (file == NULL)
	{
		Platform_LogWarn("[Special Nights] could not open %s for writing\n", SPECIAL_NIGHTS_STATS_FILE);
		return;
	}

	fputs(json->text, file);
	fputc('\n', file);
	fclose(file);
}

// ---------------------------------------------------------------------------
// Race and cup records

// Called on the first frame of track standings, after this race's points are added.
void SpecialNights_StatsRecordRace(struct GameTracker *gGT, const int *pointsPerPosition)
{
	struct SpecialNightsJson json;
	s32 numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	s32 levelID = gGT->levelID;

	if (!SpecialNights_InStatsCup(gGT))
	{
		return;
	}

	if (gGT->cup.trackIndex == 0)
	{
		time_t now = time(NULL);
		struct tm *local = localtime(&now);

		// First race of a cup: open a new cup run if every human is a confirmed player.
		s_specialNightsCupRecording = SpecialNights_ProfilesConfirmed(gGT->numPlyrCurrGame);
		if ((local == NULL) || (strftime(s_specialNightsCupRun, sizeof(s_specialNightsCupRun), "%Y%m%d-%H%M%S", local) == 0))
		{
			snprintf(s_specialNightsCupRun, sizeof(s_specialNightsCupRun), "run-%ld", (long)now);
		}
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

	json.length = 0;
	json.text[0] = 0;
	SpecialNights_JsonEventHeader(&json, gGT, "race");
	SpecialNights_JsonRaw(&json, ",\"track_index\":%d,\"level_id\":%d,\"track_name\":", gGT->cup.trackIndex, levelID);
	SpecialNights_JsonString(&json, ((levelID >= 0) && (levelID < (s32)len(data.metaDataLEV))) ? sdata->lngStrings[data.metaDataLEV[levelID].name_LNG] : "");
	SpecialNights_JsonRaw(&json, ",\"laps\":%d,\"results\":[", gGT->numLaps);

	for (s32 place = 0; place < numDrivers; place++)
	{
		struct Driver *d = gGT->driversInRaceOrder[place];
		struct SpecialNightsDriverStats *stats;
		s32 driverID;
		s32 points;

		if (d == NULL)
		{
			continue;
		}

		driverID = d->driverID;
		stats = &s_specialNightsDriverStats[driverID & (SPECIAL_NIGHTS_STATS_DRIVERS - 1)];
		points = (gGT->numBotsNextGame == 0) ? (gGT->numPlyrCurrGame - (place + 1)) : pointsPerPosition[place];

		SpecialNights_JsonRaw(&json, "%s{", (place == 0) ? "" : ",");
		SpecialNights_JsonDriverIdentity(&json, gGT, driverID);
		SpecialNights_JsonRaw(&json, ",\"place\":%d,\"points\":%d,\"cup_points\":%d,\"finished\":%s,\"race_time_ms\":", place + 1, points,
		                      gGT->cup.points[driverID], (stats->finishTicks >= 0) ? "true" : "false");
		SpecialNights_JsonTicksMs(&json, stats->finishTicks);
		SpecialNights_JsonRaw(&json, ",\"best_lap_ms\":");
		SpecialNights_JsonTicksMs(&json, stats->bestLapTicks);
		SpecialNights_JsonRaw(&json, ",\"turbo_ms\":");
		SpecialNights_JsonTicksMs(&json, stats->turboTicks);
		SpecialNights_JsonRaw(&json, ",\"avg_speed\":%ld,\"top_speed\":%d,\"turbos\":%d}",
		                      (long)((stats->sampledFrames != 0) ? (stats->speedSum / stats->sampledFrames) : 0), stats->topSpeed, d->numTurbos);
	}

	SpecialNights_JsonRaw(&json, "]}");
	SpecialNights_AppendJsonLine(&json);
}

// Called when the last race of a cup has been scored, before cup points reset.
void SpecialNights_StatsRecordCupEnd(struct GameTracker *gGT)
{
	struct SpecialNightsJson json;
	s32 numDrivers = gGT->numPlyrCurrGame + gGT->numBotsNextGame;
	b32 first = true;

	if (!SpecialNights_InStatsCup(gGT) || !s_specialNightsCupRecording)
	{
		return;
	}

	if (numDrivers > SPECIAL_NIGHTS_STATS_DRIVERS)
	{
		numDrivers = SPECIAL_NIGHTS_STATS_DRIVERS;
	}

	json.length = 0;
	json.text[0] = 0;
	SpecialNights_JsonEventHeader(&json, gGT, "cup");
	SpecialNights_JsonRaw(&json, ",\"results\":[");

	for (s32 rank = 0; rank < SPECIAL_NIGHTS_STATS_DRIVERS; rank++)
	{
		s32 driverID = data.cupPositionPerPlayer[rank];

		if ((driverID < 0) || (driverID >= numDrivers) || (gGT->drivers[driverID] == NULL))
		{
			continue;
		}

		SpecialNights_JsonRaw(&json, "%s{", first ? "" : ",");
		SpecialNights_JsonDriverIdentity(&json, gGT, driverID);
		SpecialNights_JsonRaw(&json, ",\"final_place\":%d,\"points\":%d}", rank + 1, gGT->cup.points[driverID]);
		first = false;
	}

	SpecialNights_JsonRaw(&json, "]}");
	SpecialNights_AppendJsonLine(&json);
	Platform_Log("[Special Nights] cup %s recorded to %s\n", s_specialNightsCupRun, SPECIAL_NIGHTS_STATS_FILE);

	s_specialNightsCupRecording = false;
}

#endif
