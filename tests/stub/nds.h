// Stand-in for libnds, so source/main.c can run on a PC (see tests/run_tests.sh).
#pragma once
#include <stdint.h>

typedef uint8_t u8;

#define KEY_A 1

void consoleDemoInit(void);
void swiWaitForVBlank(void);
void scanKeys(void);
int keysDown(void);
void systemShutDown(void);
