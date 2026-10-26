// Dong Profiles: one per physical build, chosen by a -DDONG_PROFILE_* build
// flag (see platformio.ini). Add a new build by copying a block.
//
// Free pins on the XIAO ESP32-S3 Plus display carrier are GPIO38, 39, 40 and
// 42 (solder pads). 3.3 V logic: put a 74AHCT125 (or similar) between the
// board and 5 V APA102/SK9822 Pixels.
#pragma once

#include "kd/dong_profile.h"

// SK9822 / APA102 color byte order on the wire, for every profile below.
#define LED_WIRE_ORDER "BGR"

#if defined(DONG_PROFILE_SCREEN)
// On the bare board the most-used button (A: Palette) sits on USR3, the
// easiest one to press.
#define BUTTON_A_PIN 11  // USR3
#define BUTTON_B_PIN 44  // USR2
#define BUTTON_C_PIN 43  // USR1
#endif

#if defined(DONG_PROFILE_PROTO) || defined(DONG_PROFILE_SCREEN)
// 1 m Blade, 60 Pixels/m, one Strand folded up-down-up-down through 4 Lines.
// The "screen" variant is for bench testing the bare board with the Blade
// view: the Blade runs toward the right edge of the screen held in landscape.
#define LED_STRAND0_DATA 38
#define LED_STRAND0_CLOCK 39
#define LED_DATA_MHZ 4  // no level shifter on the first prototype (docs/led-build.md)
#define BATTERY_PIN -1

inline const kd::DongProfile& dongProfile() {
    static const kd::DongProfile p = [] {
        kd::DongProfile p;
        p.name = "proto";
        p.bladeLength = 1.0f;
        p.hiltToFirstPixel = 0.15f;
        p.lineCount = 4;
        p.pixelsPerLine = 60;
        p.strandCount = 1;
        p.strands[0].segmentCount = 4;
        p.strands[0].segments[0] = {0, false, 0};
        p.strands[0].segments[1] = {1, true, 0};
        p.strands[0].segments[2] = {2, false, 0};
        p.strands[0].segments[3] = {3, true, 0};
        p.power.budgetMilliamps = 3000.f;  // a typical 5 V / 3 A power bank
#ifdef DONG_PROFILE_SCREEN
        p.name = "screen";
        // Buttons: see BUTTON_*_PIN below.
        // Body +X = screen right, +Y = screen top, +Z = out of the screen.
        // A guess until Mounting Calibration (hold C for 5 s) is run on the board.
        p.imuAxes = {{0, 1, 2}, {1, 1, 1}};
#endif
        return p;
    }();
    return p;
}

#elif defined(DONG_PROFILE_2M)
// 2 m Blade, 60 Pixels/m, one Strand folded through 4 Lines.
//
// Pack voltage is not sensed: only GPIO1-20 can read analog on the S3, and
// on this carrier they are all taken except GPIO10, which reads the board's
// own JST LiPo through its onboard divider (ratio ~2.975). Use that for a 1S
// pack on the JST, or add an I2C fuel gauge on the Grove port.
#define LED_STRAND0_DATA 38
#define LED_STRAND0_CLOCK 39
#define LED_DATA_MHZ 8
#define BATTERY_PIN -1

inline const kd::DongProfile& dongProfile() {
    static const kd::DongProfile p = [] {
        kd::DongProfile p;
        p.name = "2m";
        p.bladeLength = 2.0f;
        p.hiltToFirstPixel = 0.2f;
        p.lineCount = 4;
        p.pixelsPerLine = 120;
        p.strandCount = 1;
        p.strands[0].segmentCount = 4;
        p.strands[0].segments[0] = {0, false, 0};
        p.strands[0].segments[1] = {1, true, 0};
        p.strands[0].segments[2] = {2, false, 0};
        p.strands[0].segments[3] = {3, true, 0};
        p.power.budgetMilliamps = 6000.f;
        return p;
    }();
    return p;
}

#elif defined(DONG_PROFILE_LIGHTSWARM)
// The LightSwarm Dong: two Strands of 150, each fed at the tip and folded
// into two Lines. Both Strands take turns on the one SPI bus.
#define LED_STRAND0_DATA 38
#define LED_STRAND0_CLOCK 39
#define LED_STRAND1_DATA 40
#define LED_STRAND1_CLOCK 42
#define LED_DATA_MHZ 8
#define BATTERY_PIN -1

inline const kd::DongProfile& dongProfile() {
    static const kd::DongProfile p = [] {
        kd::DongProfile p;
        p.name = "lightswarm";
        p.bladeLength = 1.25f;
        p.hiltToFirstPixel = 0.2f;
        p.lineCount = 4;
        p.pixelsPerLine = 75;
        p.strandCount = 2;
        p.strands[0].segmentCount = 2;
        p.strands[0].segments[0] = {0, true, 0};
        p.strands[0].segments[1] = {1, false, 0};
        p.strands[1].segmentCount = 2;
        p.strands[1].segments[0] = {2, true, 0};
        p.strands[1].segments[1] = {3, false, 0};
        p.power.budgetMilliamps = 1600.f;
        return p;
    }();
    return p;
}

#else
#error "Select a Dong Profile with -DDONG_PROFILE_PROTO, _SCREEN, _2M or _LIGHTSWARM"
#endif

// Buttons A, B, C; a profile may wire them to other pins. The carrier's own
// buttons (checked on the board): USR1 = GPIO43 (also UART0's TX),
// USR2 = GPIO44 (UART0's RX), USR3 = GPIO11.
#ifndef BUTTON_A_PIN
#define BUTTON_A_PIN 43  // USR1
#define BUTTON_B_PIN 44  // USR2
#define BUTTON_C_PIN 11  // USR3
#endif

static_assert(BATTERY_PIN < 0 || (BATTERY_PIN >= 1 && BATTERY_PIN <= 20),
              "BATTERY_PIN must be an ADC pin (GPIO1-20 on the ESP32-S3)");
