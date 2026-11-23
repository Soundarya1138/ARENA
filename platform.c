/* platform.c - the only file with OS-specific code (Sleep/usleep guard). */
#include "arena.h"
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
void sleep_ms(int ms) { Sleep((DWORD)ms); }
double now_ms(void) { return (double)GetTickCount64(); }
int key_poll(void) { return _kbhit() ? _getch() : 0; }
void term_raw(int on) { (void)on; }
void term_init(void) {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD m;
    if (GetConsoleMode(h, &m)) SetConsoleMode(h, m | 0x0004);   /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
}
#else
#include <termios.h>
#include <unistd.h>
#include <time.h>
static struct termios saved; static int raw_on;
void sleep_ms(int ms) { if (ms > 0) usleep((useconds_t)ms * 1000u); }
double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
void term_raw(int on) {
    if (!isatty(0)) return;
    if (on && !raw_on) {
        tcgetattr(0, &saved); struct termios t = saved;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t); raw_on = 1;
    } else if (!on && raw_on) { tcsetattr(0, TCSANOW, &saved); raw_on = 0; }
}
int key_poll(void) { unsigned char c; if (!raw_on) return 0; return read(0, &c, 1) == 1 ? c : 0; }
void term_init(void) {}
#endif
