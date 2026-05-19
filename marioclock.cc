#include "led-matrix.h"
#include "graphics.h"

#include <unistd.h>
#include <stdio.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <string>
#include <iostream>

using namespace std;

using rgb_matrix::GPIO;
using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo)
{
    syslog(LOG_NOTICE, "interrupt handler ");
    interrupt_received = true;
}

// ---------------------------------------------------------------------------
// Tiny 3x5 digit font (plus 1-wide colon).  Designed to fit HH:MM:SS in 32px.
// ---------------------------------------------------------------------------

static const char* DIGITS[10][5] = {
    { "XXX",
      "X.X",
      "X.X",
      "X.X",
      "XXX" },  // 0
    { ".X.",
      "XX.",
      ".X.",
      ".X.",
      "XXX" },  // 1
    { "XXX",
      "..X",
      "XXX",
      "X..",
      "XXX" },  // 2
    { "XXX",
      "..X",
      "XXX",
      "..X",
      "XXX" },  // 3
    { "X.X",
      "X.X",
      "XXX",
      "..X",
      "..X" },  // 4
    { "XXX",
      "X..",
      "XXX",
      "..X",
      "XXX" },  // 5
    { "XXX",
      "X..",
      "XXX",
      "X.X",
      "XXX" },  // 6
    { "XXX",
      "..X",
      "..X",
      "..X",
      "..X" },  // 7
    { "XXX",
      "X.X",
      "XXX",
      "X.X",
      "XXX" },  // 8
    { "XXX",
      "X.X",
      "XXX",
      "..X",
      "XXX" }   // 9
};

static const char* COLON[5] = {
    ".",
    "X",
    ".",
    "X",
    "."
};

static void draw_digit(Canvas* canvas, int x, int y, int d,
                       uint8_t r, uint8_t g, uint8_t b)
{
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 5; ++row)
    {
        const char* line = DIGITS[d][row];
        for (int col = 0; col < 3; ++col)
        {
            if (line[col] == 'X')
                canvas->SetPixel(x + col, y + row, r, g, b);
        }
    }
}

static void draw_colon(Canvas* canvas, int x, int y,
                       uint8_t r, uint8_t g, uint8_t b)
{
    for (int row = 0; row < 5; ++row)
    {
        if (COLON[row][0] == 'X')
            canvas->SetPixel(x, y + row, r, g, b);
    }
}

static void draw_clock(Canvas* canvas, int hour, int minute, int second)
{
    const uint8_t cr = 240, cg = 200, cb = 60;  // coin gold
    int x = 2;
    const int y = 1;

    draw_digit(canvas, x, y, hour / 10, cr, cg, cb); x += 4;
    draw_digit(canvas, x, y, hour % 10, cr, cg, cb); x += 4;
    draw_colon(canvas, x, y, cr, cg, cb);            x += 2;
    draw_digit(canvas, x, y, minute / 10, cr, cg, cb); x += 4;
    draw_digit(canvas, x, y, minute % 10, cr, cg, cb); x += 4;
    draw_colon(canvas, x, y, cr, cg, cb);            x += 2;
    draw_digit(canvas, x, y, second / 10, cr, cg, cb); x += 4;
    draw_digit(canvas, x, y, second % 10, cr, cg, cb);
}

// ---------------------------------------------------------------------------
// Small Mario sprite.  12 wide x 16 tall.  Two walking frames.
//   . transparent
//   R red       (hat / shirt)
//   F skin
//   H dark brown (hair / hatband)
//   B blue      (overalls)
//   Y yellow    (buttons)
//   K black     (shoes / outline)
// ---------------------------------------------------------------------------

#define MARIO_W 12
#define MARIO_H 16

static const char* MARIO_FRAME_A[MARIO_H] = {
    "....RRRR....",
    "...RRRRRRR..",
    "...HHHFFFK..",
    "..HFHFFFFKK.",
    "..HFHHFFFKK.",
    "..HHFFFFKKK.",
    "....FFFF....",
    "...RRBRRR...",
    "..RRRBBRRR..",
    ".RRRBBBBRRR.",
    ".FFBYBBYBFF.",
    ".FFBBBBBBFF.",
    "..FBBBBBBF..",
    "...BB..BB...",
    "...BB..BB...",
    "..KKK..KKK.."
};

static const char* MARIO_FRAME_B[MARIO_H] = {
    "....RRRR....",
    "...RRRRRRR..",
    "...HHHFFFK..",
    "..HFHFFFFKK.",
    "..HFHHFFFKK.",
    "..HHFFFFKKK.",
    "....FFFF....",
    "...RRBRRR...",
    "..RRRBBRRR..",
    ".RRRBBBBRRR.",
    ".FFBYBBYBFF.",
    ".FFBBBBBBFF.",
    "...BBBBBB...",
    "...BBBBBB...",
    "..KKK..KKK..",
    ".KKK....KKK."
};

static void mario_color(char c, uint8_t& r, uint8_t& g, uint8_t& b, bool& on)
{
    on = true;
    switch (c)
    {
        case 'R': r = 220; g = 40;  b = 40;  break;
        case 'F': r = 252; g = 188; b = 140; break;
        case 'H': r = 110; g = 60;  b = 20;  break;
        case 'B': r = 60;  g = 100; b = 220; break;
        case 'Y': r = 240; g = 200; b = 60;  break;
        case 'K': r = 30;  g = 15;  b = 5;   break;
        default:  on = false; r = g = b = 0; break;
    }
}

static void draw_mario(Canvas* canvas, int x_offset, int y_offset, int frame)
{
    const char** sprite = (frame == 0) ? MARIO_FRAME_A : MARIO_FRAME_B;
    for (int row = 0; row < MARIO_H; ++row)
    {
        const char* line = sprite[row];
        for (int col = 0; col < MARIO_W; ++col)
        {
            int px = x_offset + col;
            int py = y_offset + row;
            if (px < 0 || px >= 32 || py < 0 || py >= 32) continue;

            uint8_t r, g, bl;
            bool on;
            mario_color(line[col], r, g, bl, on);
            if (on)
                canvas->SetPixel(px, py, r, g, bl);
        }
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    setlogmask(LOG_UPTO(LOG_NOTICE));
    openlog("marioclock", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    int maxtime = 0;
    if (argc > 1)
    {
        string test(argv[1]);
        syslog(LOG_NOTICE, "running for %s seconds then quitting\n", test.c_str());
        maxtime = std::stoi(test);
    }

    GPIO io;
    if (!io.Init())
        return 1;

    Canvas* canvas = new RGBMatrix(&io, 32, 1);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    time_t startTime = time(0);
    const int mario_y = 16;          // top of mario in 32-row display
    int mario_x = -MARIO_W;          // start off-screen left
    int frame = 0;
    int frame_counter = 0;           // controls walk-cycle speed
    int step_counter = 0;            // controls horizontal speed

    bool cont = true;
    while (cont)
    {
        time_t t = time(0);
        struct tm* now = localtime(&t);

        if (maxtime > 0 && difftime(t, startTime) > maxtime)
        {
            cont = false;
            printf("stopping now\n");
        }

        canvas->Clear();

        draw_clock(canvas, now->tm_hour, now->tm_min, now->tm_sec);
        draw_mario(canvas, mario_x, mario_y, frame);

        // ~ every 50ms move mario 1px to the right
        ++step_counter;
        if (step_counter >= 2)
        {
            step_counter = 0;
            mario_x++;
            if (mario_x > 32)
                mario_x = -MARIO_W;
        }

        // toggle walk frame every ~150ms
        ++frame_counter;
        if (frame_counter >= 6)
        {
            frame_counter = 0;
            frame = 1 - frame;
        }

        usleep(25000);
        if (interrupt_received)
            cont = false;
    }

    syslog(LOG_NOTICE, "end of marioclock, natural end");

    canvas->Clear();
    delete canvas;
    return 0;
}
