// PC versions of the few libnds functions savebackup uses.
//
//   SB_CWD    what fatGetDefaultCwd() returns (the folder the flashcart reports)
//   SB_DRIVE  what fatGetDefaultDrive() returns (the root of the fake card)
//
// "Pressing A" happens a few frames after the program starts waiting for it, and
// systemShutDown() ends the program, so every run finishes on its own.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"
#include "nds.h"

static int frames;

static const char *env(const char *name)
{
    const char *value = getenv(name);
    return value ? value : "/";
}

void consoleDemoInit(void) {}
void swiWaitForVBlank(void) { frames++; }
void scanKeys(void) {}
int keysDown(void) { return frames > 5 ? KEY_A : 0; }

void systemShutDown(void)
{
    printf("<switched off>\n");
    exit(0);
}

bool fatInitDefault(void) { return true; }
char *fatGetDefaultCwd(void) { return strdup(env("SB_CWD")); }
const char *fatGetDefaultDrive(void) { return env("SB_DRIVE"); }
