#ifndef NATIVE_QUICKSTART_H
#define NATIVE_QUICKSTART_H

#include <ctr_types.h>

// NOTE: Debug quick start. --track boots straight into a 1P Arcade race,
// replacing the first intro/main-menu load after the Naughty Dog crate.

int NativeQuickStart_ConfigureFromArgs(int argc, char **argv);
b32 NativeQuickStart_TakePending(s16 *levelID, s16 *characterID);
// True for the whole session once --track was given; used to skip the fly-in.
b32 NativeQuickStart_IsEnabled(void);

#endif
