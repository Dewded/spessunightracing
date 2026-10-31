#include "platform/native_quickstart.h"

#include <macros.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NativeQuickStartName
{
	const char *name;
	s16 id;
};

// Race tracks in LevelID order; names are matched with case, spaces and punctuation ignored.
global_variable const struct NativeQuickStartName s_quickStartTracks[] = {
    {"dingocanyon", 0},   {"dragonmines", 1}, {"blizzardbluff", 2}, {"crashcove", 3},    {"tigertemple", 4},   {"papupyramid", 5},
    {"roostubes", 6},     {"hotairskyway", 7}, {"sewerspeedway", 8}, {"mysterycaves", 9}, {"cortexcastle", 10}, {"nginlabs", 11},
    {"polarpass", 12},    {"oxidestation", 13}, {"cocopark", 14},    {"tinyarena", 15},   {"slidecoliseum", 16}, {"turbotrack", 17},
};

global_variable const struct NativeQuickStartName s_quickStartCharacters[] = {
    {"crash", 0},  {"cortex", 1}, {"tiny", 2}, {"coco", 3},    {"ngin", 4},     {"dingodile", 5}, {"polar", 6},     {"pura", 7},
    {"pinstripe", 8}, {"papu", 9}, {"roo", 10}, {"joe", 11},   {"ntropy", 12},  {"penta", 13},    {"fakecrash", 14}, {"oxide", 15},
};

global_variable b32 s_quickStartEnabled = false;
global_variable b32 s_quickStartPending = false;
global_variable s16 s_quickStartLevel = 0;
global_variable s16 s_quickStartCharacter = 0;

internal const char *NativeQuickStart_ArgValue(int argc, char **argv, const char *name)
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

internal void NativeQuickStart_Normalize(const char *src, char *dst, size_t dstSize)
{
	size_t length = 0;

	for (; (*src != '\0') && (length + 1 < dstSize); src++)
	{
		if (isalnum((unsigned char)*src))
		{
			dst[length++] = (char)tolower((unsigned char)*src);
		}
	}

	dst[length] = '\0';
}

internal b32 NativeQuickStart_Lookup(const char *arg, const struct NativeQuickStartName *names, int count, s16 *idOut)
{
	char key[64];
	char *end = NULL;
	const long number = strtol(arg, &end, 10);

	if ((end != arg) && (*end == '\0'))
	{
		for (int i = 0; i < count; i++)
		{
			if (names[i].id == number)
			{
				*idOut = names[i].id;
				return true;
			}
		}

		return false;
	}

	NativeQuickStart_Normalize(arg, key, sizeof(key));
	for (int i = 0; i < count; i++)
	{
		// Prefix match, so "roo" picks Roo's Tubes and "crash" picks Crash Cove.
		if (strncmp(names[i].name, key, strlen(key)) == 0)
		{
			*idOut = names[i].id;
			return true;
		}
	}

	return false;
}

internal void NativeQuickStart_PrintNames(const char *label, const struct NativeQuickStartName *names, int count)
{
	fprintf(stderr, "[CTR Native] %s:", label);
	for (int i = 0; i < count; i++)
	{
		fprintf(stderr, " %s", names[i].name);
	}
	fprintf(stderr, "\n");
}

int NativeQuickStart_ConfigureFromArgs(int argc, char **argv)
{
	const char *trackArg = NativeQuickStart_ArgValue(argc, argv, "--track");
	const char *characterArg = NativeQuickStart_ArgValue(argc, argv, "--character");
	const int trackCount = (int)(sizeof(s_quickStartTracks) / sizeof(s_quickStartTracks[0]));
	const int characterCount = (int)(sizeof(s_quickStartCharacters) / sizeof(s_quickStartCharacters[0]));

	if (trackArg == NULL)
	{
		return 0;
	}

	if (!NativeQuickStart_Lookup(trackArg, s_quickStartTracks, trackCount, &s_quickStartLevel))
	{
		fprintf(stderr, "[CTR Native] unknown --track: %s\n", trackArg);
		NativeQuickStart_PrintNames("tracks", s_quickStartTracks, trackCount);
		return 1;
	}

	if ((characterArg != NULL) && !NativeQuickStart_Lookup(characterArg, s_quickStartCharacters, characterCount, &s_quickStartCharacter))
	{
		fprintf(stderr, "[CTR Native] unknown --character: %s\n", characterArg);
		NativeQuickStart_PrintNames("characters", s_quickStartCharacters, characterCount);
		return 1;
	}

	s_quickStartEnabled = true;
	s_quickStartPending = true;
	printf("[CTR Native] Quick start: track %d, character %d\n", s_quickStartLevel, s_quickStartCharacter);
	return 0;
}

b32 NativeQuickStart_IsEnabled(void)
{
	return s_quickStartEnabled;
}

b32 NativeQuickStart_TakePending(s16 *levelID, s16 *characterID)
{
	if (!s_quickStartPending)
	{
		return false;
	}

	s_quickStartPending = false;
	*levelID = s_quickStartLevel;
	*characterID = s_quickStartCharacter;
	return true;
}
