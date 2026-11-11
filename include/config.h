// Settings for the Daily Fish firmware.
#pragma once

// --- When ----------------------------------------------------------------------

// Seconds between fish. The panel has no clock, only a sleep timer that is
// good to a few minutes a day, so the change time drifts slowly. To move it,
// press Reset at the time of day you want the fish to change.
#ifndef REFRESH_INTERVAL_S
#define REFRESH_INTERVAL_S (24UL * 60 * 60)
#endif

// --- Drawing -------------------------------------------------------------------

// Line width in pixels (1 = hairline, as in the original plotter drawings).
#ifndef LINE_THICKNESS
#define LINE_THICKNESS 2
#endif

// Show "No. 42", the count of fish this panel has generated, in the bottom
// right corner. Set to 0 for a frame with nothing but the fish and its name.
#ifndef SHOW_FISH_NUMBER
#define SHOW_FISH_NUMBER 1
#endif

// --- Uploading -----------------------------------------------------------------

// Deep sleep switches the USB port off, so new firmware cannot be uploaded
// while the panel sleeps. If a computer is on the USB port, the panel stays
// awake this many seconds after drawing before it sleeps. Press Reset, wait
// for the picture to redraw, then upload. (A charger does not trigger this.)
#ifndef USB_UPLOAD_WINDOW_S
#define USB_UPLOAD_WINDOW_S 60
#endif

// --- Display -------------------------------------------------------------------

// Wiring of the XIAO ePaper driver board inside the panel (GPIO numbers on
// the XIAO ESP32-C3). SCK is GPIO8 (D8) and MOSI is GPIO10 (D10), the board's
// default SPI pins.
#define EPD_PIN_RST 2   // D0
#define EPD_PIN_CS 3    // D1
#define EPD_PIN_BUSY 4  // D2
#define EPD_PIN_DC 5    // D3

// GxEPD2 driver class for the 7.5" 800x480 panel. GxEPD2_750_T7 is confirmed
// working on this panel. GxEPD2_750_GDEY075T7 is the alternative.
#ifndef EPD_DRIVER_CLASS
#define EPD_DRIVER_CLASS GxEPD2_750_T7
#endif
