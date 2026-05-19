# marioclock

An `HH:MM` clock with a walking Mario, drawn on a 32x32 RGB LED matrix
(Adafruit RGB Matrix HAT for a Raspberry Pi).

![mario bumping the minute digits](imagesrc/mario-change-of-minute.gif)

## What it does

- `HH:MM` is shown across the top of the panel in 3x5 pixel digits (rows 1-5,
  centred, coin-gold colour).
- A 15x15 Mario sprite walks back and forth across the bottom row of the panel:
  - left → right, then off-screen
  - random 2-5 s pause off-screen
  - sprite is mirrored horizontally and he walks right → left
  - random 2-5 s pause again, loop forever

- When the minute is about to change, the **old** time keeps showing until
  Mario walks underneath the minute digits. Then:
  1. Mario jumps straight up (pose frozen mid-air) and bonks the bottom of
     the minute digits.
  2. The digits launch upward; Mario falls back down at the same speed.
  3. The digits are hidden for a brief pause once they've left the panel.
  4. The **new** minute value falls back into place from above.
  5. Mario continues walking in whichever direction he was going before
     the bump.

A global `DIM_FACTOR` (default `0.5f`) multiplies every R/G/B before it hits
the panel — tweak it to taste at the top of `marioclock.cc`.

## Sprite source

The four walk-cycle frames are extracted from `imagesrc/Layer 1.png` …
`imagesrc/Layer 4.png` and embedded as ASCII palette grids in
`marioclock.cc` — no runtime image assets to ship.

| char | colour | rgb |
|------|--------|-----|
| `R`  | red (hat / shirt)     | 255, 0, 0    |
| `F`  | peach skin            | 255, 200, 190 |
| `H`  | brown hair / shoes    | 200, 90, 0   |
| `B`  | blue overalls         | 0, 30, 255   |
| `K`  | black (eye / mustache) | 0, 0, 0    |
| `W`  | white highlight       | 255, 255, 255 |

## Build

The project re-uses the `rgb-matrix` library that lives in `../matrixclock`,
so make sure that directory exists alongside this one.

```
make
sudo ./marioclock          # run forever
sudo ./marioclock 30       # auto-quit after 30 seconds
```
