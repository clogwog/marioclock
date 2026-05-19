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
// Global LED brightness multiplier (0.0 = off, 1.0 = full).  Applied to every
// pixel before it hits the panel.  Tweak DIM_FACTOR to taste.
// ---------------------------------------------------------------------------

static float DIM_FACTOR = 0.5f;

static inline void put_pixel(Canvas* canvas, int x, int y,
                             uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || x >= 32 || y < 0 || y >= 32) return;
    uint8_t dr = (uint8_t)(r * DIM_FACTOR);
    uint8_t dg = (uint8_t)(g * DIM_FACTOR);
    uint8_t db = (uint8_t)(b * DIM_FACTOR);
    canvas->SetPixel(x, y, dr, dg, db);
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
                put_pixel(canvas, x + col, y + row, r, g, b);
        }
    }
}

static void draw_colon(Canvas* canvas, int x, int y,
                       uint8_t r, uint8_t g, uint8_t b)
{
    for (int row = 0; row < 5; ++row)
    {
        if (COLON[row][0] == 'X')
            put_pixel(canvas, x, y + row, r, g, b);
    }
}

// HH:MM   3+1+3+1+1+1+3+1+3 = 17 px wide.  Centered in 32 px (x = 7).
static void draw_clock(Canvas* canvas, int hour, int minute)
{
    const uint8_t cr = 240, cg = 200, cb = 60;  // coin gold
    int x = 7;
    const int y = 1;

    draw_digit(canvas, x, y, hour / 10,   cr, cg, cb); x += 4;
    draw_digit(canvas, x, y, hour % 10,   cr, cg, cb); x += 4;
    draw_colon(canvas, x, y,              cr, cg, cb); x += 2;
    draw_digit(canvas, x, y, minute / 10, cr, cg, cb); x += 4;
    draw_digit(canvas, x, y, minute % 10, cr, cg, cb);
}

// ---------------------------------------------------------------------------
// Mario sprite — 18 wide x 23 tall.  Three-frame walk cycle, extracted
// directly from the source GIF (pinimg 66c9e8...) at native sprite res,
// sampling each NES-equivalent pixel and aligning all frames to one bbox.
//
//   . transparent
//   R red       (hat / shirt)
//   F skin
//   H dark brown (hair / hatband / shoes)
//   B blue      (overalls)
//   Y yellow    (buttons)
//   K black     (eye / mustache)
//   W white     (undershirt — visible when arms are at sides)
// ---------------------------------------------------------------------------

#define MARIO_W 18
#define MARIO_H 23
#define MARIO_FRAMES 3

static const char* MARIO[MARIO_FRAMES][MARIO_H] = {
    // Frame 0 — arm swung back, leg stride
    {
        "......RRRR........",
        ".....RRRR.........",
        "....RRRRRRRR......",
        "....HHFFKFF.......",
        "...HFFFFKFFFF.....",
        "...HFFHFFFFFF.....",
        "..HHFFFFKKKKK.....",
        "..HHHFFFFKKKK.....",
        "....HHFFFFFF......",
        ".......RBRR.......",
        "........RBR.......",
        ".........BRR......",
        "........RBBR......",
        "....RRRRRBBB......",
        "....BBBBBBYB......",
        "....BBBBBBBBB..HH.",
        ".HBBBBBBBBBBBBHHH.",
        "HHBBBBBBBBBBBHHH..",
        "HHBBBBBB.BBBBHHH..",
        "HHBBBBB....BBHHH..",
        ".HBBB........HH...",
        ".HHH..............",
        "..HH.............."
    },
    // Frame 1 — arms at sides, undershirt visible, legs together
    {
        "......RRRR........",
        ".....RRRR.........",
        "....RRRRRRRR......",
        "....HHFFKFF.......",
        "...HFFFFKFFFF.....",
        "...HFFHFFFFFF.....",
        "..HHFFFFKKKKK.....",
        "..HHHFFFFKKKK.....",
        "....HHFFFFFF......",
        ".....RWWWWR.......",
        "....RRWWWWR.......",
        "....RRWWWWRR......",
        "....RRWWWBBR......",
        "....BRRRRBBB......",
        "....BBBBBBYB......",
        "....BBBBBBBBB..HH.",
        "..BBBBBBBBBBBBHHH.",
        "..BBBBBBBBBBBHHH..",
        ".HBBBBB...BBBHHH..",
        ".HBBBBB....BBHH...",
        ".HBBBB.......H....",
        ".HHH..............",
        "..HH.............."
    },
    // Frame 2 — arm coming forward, opposite stride
    {
        "......RRRR........",
        ".....RRRR.........",
        "....RRRRRRRR......",
        "....HHFFKFF.......",
        "...HFFFFKFFFF.....",
        "...HFFHFFFFFF.....",
        "..HHFFFFKKKKK.....",
        "..HHHFFFFKKKK.....",
        "....HHWWWWFF......",
        ".....RWWWWR.......",
        "....RRWWWWR.......",
        "....RRWWWRRR......",
        "....RRRRBBBR......",
        "....BRRRRBBB......",
        "....BBBBBBYB......",
        "....BBBBBBBBB..H..",
        "...BBBBBBBBBBBHH..",
        "..BBBBBBBBBBBHHH..",
        ".HBBBBB...BBBHHH..",
        ".HBBBBB....BBHH...",
        "HHHBB........HH...",
        "HHHH..............",
        ".HH..............."
    }
};

static void mario_color(char c, uint8_t& r, uint8_t& g, uint8_t& b, bool& on)
{
    on = true;
    switch (c)
    {
        case 'R': r = 252; g = 53;  b = 11;  break;  // red shirt/hat
        case 'F': r = 252; g = 188; b = 100; break;  // skin
        case 'H': r = 140; g = 86;  b = 13;  break;  // brown hair / shoes
        case 'B': r = 4;   g = 90;  b = 188; break;  // blue overalls
        case 'Y': r = 252; g = 220; b = 60;  break;  // yellow button
        case 'K': r = 0;   g = 0;   b = 0;   break;  // black (eye/mustache)
        case 'W': r = 230; g = 230; b = 230; break;  // white undershirt
        default:  on = false; r = g = b = 0; break;
    }
}

static void draw_mario(Canvas* canvas, int x_offset, int y_offset, int frame)
{
    if (frame < 0 || frame >= MARIO_FRAMES) frame = 0;
    for (int row = 0; row < MARIO_H; ++row)
    {
        const char* line = MARIO[frame][row];
        for (int col = 0; col < MARIO_W; ++col)
        {
            int px = x_offset + col;
            int py = y_offset + row;
            if (px < 0 || px >= 32 || py < 0 || py >= 32) continue;

            uint8_t r, g, bl;
            bool on;
            mario_color(line[col], r, g, bl, on);
            if (on)
                put_pixel(canvas, px, py, r, g, bl);
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
    const int mario_y = 32 - MARIO_H; // align Mario flush with bottom of panel
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

        draw_clock(canvas, now->tm_hour, now->tm_min);
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

        // advance walk frame every ~150ms (3-frame cycle => ~450ms full step)
        ++frame_counter;
        if (frame_counter >= 6)
        {
            frame_counter = 0;
            frame = (frame + 1) % MARIO_FRAMES;
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
