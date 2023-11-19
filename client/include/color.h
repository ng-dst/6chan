#ifndef LAB6_COLOR_H
#define LAB6_COLOR_H
#ifdef USE_COLOR

#include <windows.h>

#define COLORS_ARRAY \
    FOREGROUND_GREEN, \
    FOREGROUND_GREEN | FOREGROUND_BLUE, \
    FOREGROUND_RED | FOREGROUND_BLUE, \
    FOREGROUND_RED, \
    FOREGROUND_RED | FOREGROUND_GREEN, \
    FOREGROUND_BLUE

#define NUM_COLORS 6

#define DEFAULT_COLOR 0

HANDLE hConsole;
CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
WORD saved_attr;

void setColor(int seed);

#endif //USE_COLOR
#endif //LAB6_COLOR_H
