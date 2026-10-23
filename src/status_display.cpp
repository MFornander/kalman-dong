#include "status_display.h"

#include <Seeed_GFX.h>

#include <cmath>

#include "board/boards/XIAO_LCD_Board.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"

namespace status_display {

namespace {

constexpr int8_t kLcdRst = 13;
constexpr int8_t kLcdBacklight = 12;
constexpr uint8_t kBacklightOn = 160;
constexpr uint8_t kPortrait = 0;
constexpr uint8_t kLandscape = 1;

// Status page (portrait, 135 x 240)
constexpr int kLineHeight = 22;
constexpr int kStatusWidth = 135;

// Blade view (landscape, 240 x 135)
constexpr int kBladeWidth = 240;
constexpr int kHeaderHeight = 18;
constexpr int kRowHeight = 24;
constexpr int kRowGap = 5;

enum class Page { Off, Status, Blade };

Seeed_GFX display;
bool ready = false;
bool bladeViewOn = false;
Page page = Page::Off;
uint32_t statusUntilMs = 0;
uint16_t rowPixels[kBladeWidth * kRowHeight];
uint8_t gammaLut[256];

void showPage(Page next) {
    if (next == page) return;
    if (next == Page::Off) {
        display.panel().setBacklight(0);
        display.panel().sleep();
    } else {
        if (page == Page::Off) display.panel().wake();
        display.setRotation(next == Page::Blade ? kLandscape : kPortrait);
        display.fillScreen(TFT_BLACK);
        display.panel().setBacklight(kBacklightOn);
    }
    page = next;
}

void statusLine(int row, const char* text, bool highlight) {
    display.setTextColor(highlight ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    display.setTextPadding(kStatusWidth);
    display.drawString(text, 4, 6 + row * kLineHeight);
}

void drawStatus(const Status& s) {
    char buf[40];
    display.setTextSize(2);
    display.setTextDatum(TL_DATUM);
    using kd::Setting;

    statusLine(0, kd::palette(s.settings.palette).name, s.changed == Setting::Palette);
    snprintf(buf, sizeof(buf), "Bright %3d%%", int(s.settings.brightnessValue() * 100 + 0.5f));
    statusLine(1, buf, s.changed == Setting::Brightness);
    snprintf(buf, sizeof(buf), "Girth %.2fm", s.settings.girthValue());
    statusLine(2, buf, s.changed == Setting::Girth);
    snprintf(buf, sizeof(buf), "Sky %3d%%", int(s.settings.clearSkyValue() * 100 + 0.5f));
    statusLine(3, buf, s.changed == Setting::ClearSky);

    if (s.batteryFraction >= 0.f)
        snprintf(buf, sizeof(buf), "Batt %3d%%", int(s.batteryFraction * 100 + 0.5f));
    else
        snprintf(buf, sizeof(buf), "Batt --");
    statusLine(5, buf, false);
    snprintf(buf, sizeof(buf), "%4.0f fps", s.fps);
    statusLine(6, buf, false);
    snprintf(buf, sizeof(buf), "%4.0f Hz IMU", s.imuHz);
    statusLine(7, buf, false);

    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    display.setTextPadding(kStatusWidth);
    snprintf(buf, sizeof(buf), "%s %s%s", s.profile, s.imuChip, s.recording ? " REC" : (s.atRest ? " rest" : ""));
    display.drawString(buf, 4, 228);
}

// Pixels are linear PWM; the panel expects gamma-encoded values. Encode so
// the screen's brightness roughly matches what the LEDs would emit.
uint16_t toPanel(const kd::Rgb8& c) {
    return display.color565(gammaLut[c.r], gammaLut[c.g], gammaLut[c.b]);
}

void drawBlade(const Status& s, const kd::Rgb8* lines, size_t lineCount, size_t ppl) {
    char buf[48];
    display.setTextSize(1);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextPadding(kBladeWidth);
    snprintf(buf, sizeof(buf), "%-8s %3.0f fps %3.0f Hz %s%s", kd::palette(s.settings.palette).name, s.fps, s.imuHz,
             s.atRest ? "rest" : "", s.recording ? " REC" : "");
    display.drawString(buf, 2, 5);

    if (ppl == 0) return;
    for (size_t line = 0; line < lineCount; ++line) {
        const kd::Rgb8* px = lines + line * ppl;
        for (int x = 0; x < kBladeWidth; ++x) rowPixels[x] = toPanel(px[size_t(x) * ppl / kBladeWidth]);
        for (int y = 1; y < kRowHeight; ++y) memcpy(rowPixels + y * kBladeWidth, rowPixels, kBladeWidth * 2);
        display.pushImage(0, kHeaderHeight + int(line) * (kRowHeight + kRowGap), kBladeWidth, kRowHeight, rowPixels);
    }
}

}  // namespace

bool begin() {
    for (int i = 0; i < 256; ++i) gammaLut[i] = static_cast<uint8_t>(std::pow(i / 255.f, 1.f / 2.2f) * 255.f + 0.5f);
    ready = display.begin<Board_XIAO_1inch14_LCD<kLcdRst, kLcdBacklight>, Config_Seeed_1inch14_LCD_ST7789>();
    if (!ready) return false;
    display.setSwapBytes(true);
    display.fillScreen(TFT_BLACK);
    display.panel().setBacklight(0);
    page = Page::Off;
    return true;
}

void setBladeView(bool on) { bladeViewOn = on; }
bool bladeView() { return bladeViewOn; }

void wake(const Status& status, uint32_t nowMs) {
    if (!ready) return;
    statusUntilMs = nowMs + kAwakeMs;
    showPage(Page::Status);
    drawStatus(status);
}

void update(const Status& status, const kd::Rgb8* lines, size_t lineCount, size_t pixelsPerLine, uint32_t nowMs) {
    if (!ready) return;
    bool statusDue = static_cast<int32_t>(statusUntilMs - nowMs) > 0;
    if (statusDue) {
        showPage(Page::Status);
        drawStatus(status);
    } else if (bladeViewOn) {
        showPage(Page::Blade);
        drawBlade(status, lines, lineCount, pixelsPerLine);
    } else {
        showPage(Page::Off);
    }
}

}  // namespace status_display
