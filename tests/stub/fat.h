// Stand-in for libnds' fat.h, so source/main.c can run on a PC (see tests/run_tests.sh).
#pragma once
#include <stdbool.h>

bool fatInitDefault(void);
char *fatGetDefaultCwd(void);
const char *fatGetDefaultDrive(void);
