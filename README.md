# marioclock

A tiny HH:MM clock with a walking Mario, drawn on a 32x32 RGB LED matrix
(Adafruit RGB Matrix HAT for the Raspberry Pi).

Layout on the 32x32 panel:

```
y =  1..5    HH:MMS  (3x5 pixel digits, coin-gold colour)
y = 16..31   12x16 Mario sprite, walking right, wrapping around
```

Two walk-cycle frames are embedded directly in `marioclock.cc` so there are no
external image assets to ship.

## Build

The project re-uses the `rgb-matrix` library that lives in `../matrixclock`,
so make sure that directory exists alongside this one.

```
make
sudo ./marioclock          # run forever
sudo ./marioclock 30       # auto-quit after 30 seconds
```
