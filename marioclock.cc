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
// hour digits + colon are drawn at hour_y; minute digits at minute_y so the
// minute pair can be flown up / dropped back independently of the hours.
// minute < 0 = don't draw minute digits at all (they're "between" old & new).
static void draw_clock(Canvas* canvas, int hour, int minute,
                       int hour_y, int minute_y)
{
    const uint8_t cr = 240, cg = 200, cb = 60;  // coin gold
    int x = 7;
    draw_digit(canvas, x, hour_y, hour / 10, cr, cg, cb); x += 4;
    draw_digit(canvas, x, hour_y, hour % 10, cr, cg, cb); x += 4;
    draw_colon(canvas, x, hour_y,            cr, cg, cb); x += 2;
    if (minute >= 0)
    {
        draw_digit(canvas, x, minute_y, minute / 10, cr, cg, cb); x += 4;
        draw_digit(canvas, x, minute_y, minute % 10, cr, cg, cb);
    }
}

// ---------------------------------------------------------------------------
// Mario sprite — 15 wide x 15 tall.  Four-frame walk cycle, extracted directly
// from imagesrc/Layer 1-4.png via per-pixel nearest-palette classification
// with alpha >= 240.
//
//   . transparent
//   R red       (hat / shirt)
//   F peach     (skin / face)
//   H brown     (hair / shoes)
//   B blue      (overalls)
//   K black     (eye / mustache)
//   W white     (highlight / undershirt)
// ---------------------------------------------------------------------------

#define MARIO_W 15
#define MARIO_H 15
#define MARIO_FRAMES 4

static const char* MARIO[MARIO_FRAMES][MARIO_H] = {
    // Frame 0 — Layer 1
    {
        "...............",
        "....RRRR.......",
        "...RRRHRR......",
        "...HHFWKF......",
        "..FFHFFHHFW....",
        "..HFHFFFKKH....",
        "....WFFHHH.....",
        "....HFHH.......",
        "...RRBBR.......",
        "..RRHBFHB......",
        "..RRRBBBB......",
        "..HRHWBBB......",
        "...HHWBB.......",
        "....BBHHH......",
        "....HHH........"
    },
    // Frame 1 — Layer 2
    {
        ".....RRRR......",
        "....RRRRRR.....",
        "....HHFWKF.....",
        "...FFHFFHHFW...",
        "...HFHFFFKKH...",
        ".....WWFHHH....",
        "......HFBF.....",
        "......RRBR.....",
        ".....RRRHRFW...",
        "...WFHRRRRFW...",
        "...HHBBBBBB....",
        "...HBBBBBBB....",
        "..HHB...BB.....",
        ".......HH......",
        "........H......"
    },
    // Frame 2 — Layer 3 (matches Layer 1)
    {
        "...............",
        "....RRRR.......",
        "...RRRHRR......",
        "...HHFWKF......",
        "..FFHFFHHFW....",
        "..HFHFFFKKH....",
        "....WFFHHH.....",
        "....HFHH.......",
        "...RRBBR.......",
        "..RRHBFHB......",
        "..RRRBBBB......",
        "..HRHWBBB......",
        "...HHWBB.......",
        "....BBHHH......",
        "....HHH........"
    },
    // Frame 3 — Layer 4
    {
        ".....RRRR......",
        "....RRRRRR.....",
        "....HHFWKH.....",
        "...FFHFFHHFW...",
        "...HFHFFFKKH...",
        ".....WFWHHH....",
        ".....FFHHF.....",
        "..RRRBBRRR.....",
        "WW..RHBBHHBRHWW",
        "WW..BBFFBBF....",
        "....BBBBBBB....",
        "...BBBBBBBBBHH.",
        "..HBB.....BBHH.",
        ".HH............",
        "..HH..........."
    }
};

static void mario_color(char c, uint8_t& r, uint8_t& g, uint8_t& b, bool& on)
{
    on = true;
    switch (c)
    {
        case 'R': r = 255; g = 0;   b = 0;   break;  // red shirt/hat
        case 'F': r = 255; g = 200; b = 190; break;  // peach skin
        case 'H': r = 200; g = 90;  b = 0;   break;  // brown hair / shoes
        case 'B': r = 0;   g = 30;  b = 255; break;  // blue overalls
        case 'Y': r = 252; g = 220; b = 60;  break;  // yellow button
        case 'K': r = 0;   g = 0;   b = 0;   break;  // black (eye/mustache)
        case 'W': r = 255; g = 255; b = 255; break;  // white highlight
        default:  on = false; r = g = b = 0; break;
    }
}

static void draw_mario(Canvas* canvas, int x_offset, int y_offset, int frame,
                       bool flip_x)
{
    if (frame < 0 || frame >= MARIO_FRAMES) frame = 0;
    for (int row = 0; row < MARIO_H; ++row)
    {
        const char* line = MARIO[frame][row];
        for (int col = 0; col < MARIO_W; ++col)
        {
            int src_col = flip_x ? (MARIO_W - 1 - col) : col;
            int px = x_offset + col;
            int py = y_offset + row;
            if (px < 0 || px >= 32 || py < 0 || py >= 32) continue;

            uint8_t r, g, bl;
            bool on;
            mario_color(line[src_col], r, g, bl, on);
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

    srand((unsigned int)time(NULL));

    time_t startTime = time(0);
    const int MARIO_GROUND_Y = 32 - MARIO_H; // 17, feet flush with bottom row
    const int MARIO_BUMP_Y  = 6;             // top of mario just below digits
    const int CLOCK_Y       = 1;             // normal y of HH:MM
    const int CLOCK_GONE_Y  = -5;            // digit-top fully off panel above
    const int BUMP_TARGET_X = 13;            // mario_x when centred under MM

    // ---- mario state ----
    enum MarioState { WALK_RIGHT, WAIT_RIGHT, WALK_LEFT, WAIT_LEFT,
                      BUMP_UP, BUMP_DOWN };
    MarioState state = WALK_RIGHT;
    MarioState pre_bump_state = WALK_RIGHT;  // direction to resume after bump
    int mario_x = -MARIO_W;
    int mario_y = MARIO_GROUND_Y;
    bool flip = false;
    int frame = 0;
    int frame_counter = 0;      // 25ms ticks; 1 walk step every 6 ticks (150ms)
    int bump_counter  = 0;      // faster beat for the jump itself (3 ticks)
    int wait_ticks    = 0;

    // ---- minute digit state ----
    enum MinuteState { MIN_NORMAL, MIN_FLY_UP, MIN_HIDDEN, MIN_FALL };
    MinuteState min_state = MIN_NORMAL;
    int minute_y = CLOCK_Y;
    int hidden_ticks = 0;
    time_t t = time(0);
    struct tm* now = localtime(&t);
    int displayed_minute = now->tm_min;
    int displayed_hour   = now->tm_hour;
    int pending_minute   = displayed_minute;
    int pending_hour     = displayed_hour;
    bool bump_pending    = false;

    auto pick_wait = []() { return (2 + (rand() % 4)) * 40; };   // 2..5 s
    const int HIDDEN_PAUSE_TICKS = 30;                            // ~0.75s

    bool cont = true;
    while (cont)
    {
        t = time(0);
        now = localtime(&t);

        if (maxtime > 0 && difftime(t, startTime) > maxtime)
        {
            cont = false;
            printf("stopping now\n");
        }

        // ---- detect minute change ----
        if (!bump_pending &&
            (now->tm_min != displayed_minute || now->tm_hour != displayed_hour))
        {
            bump_pending  = true;
            pending_minute = now->tm_min;
            pending_hour   = now->tm_hour;
        }

        // ---- minute digit animation (independent of mario except for trigger) ----
        switch (min_state)
        {
        case MIN_NORMAL:
            minute_y = CLOCK_Y;
            break;
        case MIN_FLY_UP:
            // tied to bump_counter so it matches mario's fall speed below
            if (bump_counter == 0)
            {
                if (minute_y > CLOCK_GONE_Y) minute_y--;
                if (minute_y <= CLOCK_GONE_Y)
                {
                    // digits are off-screen; swap to new value while hidden
                    displayed_minute = pending_minute;
                    displayed_hour   = pending_hour;
                    min_state = MIN_HIDDEN;
                    hidden_ticks = HIDDEN_PAUSE_TICKS;
                }
            }
            break;
        case MIN_HIDDEN:
            if (--hidden_ticks <= 0)
            {
                min_state = MIN_FALL;
            }
            break;
        case MIN_FALL:
            if (bump_counter == 0)
            {
                if (minute_y < CLOCK_Y) minute_y++;
                if (minute_y >= CLOCK_Y)
                {
                    minute_y = CLOCK_Y;
                    min_state = MIN_NORMAL;
                    bump_pending = false;
                }
            }
            break;
        }

        // ---- mario state machine ----
        if (state == WALK_RIGHT || state == WALK_LEFT)
        {
            ++frame_counter;
            if (frame_counter >= 6)
            {
                frame_counter = 0;
                frame = (frame + 1) % MARIO_FRAMES;
                mario_x += (state == WALK_RIGHT) ? 1 : -1;

                // detect bump trigger: walking under the minute digits
                if (bump_pending && min_state == MIN_NORMAL &&
                    mario_x == BUMP_TARGET_X)
                {
                    pre_bump_state = state;
                    state = BUMP_UP;
                    mario_y = MARIO_GROUND_Y;
                    bump_counter = 0;
                }
                else if (state == WALK_RIGHT && mario_x > 32)
                {
                    state = WAIT_RIGHT;
                    wait_ticks = pick_wait();
                }
                else if (state == WALK_LEFT && mario_x < -MARIO_W)
                {
                    state = WAIT_LEFT;
                    wait_ticks = pick_wait();
                }
            }
        }
        else if (state == WAIT_RIGHT || state == WAIT_LEFT)
        {
            if (--wait_ticks <= 0)
            {
                if (state == WAIT_RIGHT)
                {
                    state = WALK_LEFT;
                    flip = true;
                    mario_x = 32;
                }
                else
                {
                    state = WALK_RIGHT;
                    flip = false;
                    mario_x = -MARIO_W;
                }
                frame = 0;
                frame_counter = 0;
            }
        }
        else if (state == BUMP_UP)
        {
            // pose frozen — don't cycle walk frames mid-air
            ++bump_counter;
            if (bump_counter >= 2)        // ~50ms per pixel
            {
                bump_counter = 0;
                mario_y--;
                if (mario_y <= MARIO_BUMP_Y)
                {
                    mario_y = MARIO_BUMP_Y;
                    state = BUMP_DOWN;
                    min_state = MIN_FLY_UP;   // hit! digits start flying up now
                }
            }
        }
        else if (state == BUMP_DOWN)
        {
            ++bump_counter;
            if (bump_counter >= 2)
            {
                bump_counter = 0;
                mario_y++;
                if (mario_y >= MARIO_GROUND_Y)
                {
                    mario_y = MARIO_GROUND_Y;
                    state = pre_bump_state;   // resume original walking direction
                    frame_counter = 0;
                }
            }
        }

        // ---- draw ----
        canvas->Clear();
        int draw_minute = (min_state == MIN_HIDDEN) ? -1 : displayed_minute;
        draw_clock(canvas, displayed_hour, draw_minute, CLOCK_Y, minute_y);

        if (state == WALK_RIGHT || state == WALK_LEFT ||
            state == BUMP_UP    || state == BUMP_DOWN)
        {
            draw_mario(canvas, mario_x, mario_y, frame, flip);
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
