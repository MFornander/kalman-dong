#include "status_display.h"

#include <Seeed_GFX.h>

#include "board/boards/XIAO_LCD_Board.h"
#include "kd/color.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"

namespace status_display {

namespace {

constexpr int8_t kLcdRst = 13;
constexpr int8_t kLcdBacklight = 12;
constexpr uint8_t kBacklightOn = 160;
constexpr int kLineHeight = 22;
constexpr int kWidth = 135;

Seeed_GFX display;
bool ready = false;
bool awake = false;
uint32_t awakeUntilMs = 0;

void line(int row, const char* text, bool highlight) {
    display.setTextColor(highlight ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    display.setTextPadding(kWidth);
    display.drawString(text, 4, 6 + row * kLineHeight);
}

void draw(const Status& s) {
    char buf[32];
    display.setTextSize(2);
    display.setTextDatum(TL_DATUM);
    using kd::Setting;

    line(0, kd::palette(s.settings.palette).name, s.changed == Setting::Palette);
    snprintf(buf, sizeof(buf), "Bright %3d%%", int(s.settings.brightnessValue() * 100 + 0.5f));
    line(1, buf, s.changed == Setting::Brightness);
    snprintf(buf, sizeof(buf), "Girth %.2fm", s.settings.girthValue());
    line(2, buf, s.changed == Setting::Girth);
    snprintf(buf, sizeof(buf), "Sky %3d%%", int(s.settings.clearSkyValue() * 100 + 0.5f));
    line(3, buf, s.changed == Setting::ClearSky);

    if (s.batteryFraction >= 0.f)
        snprintf(buf, sizeof(buf), "Batt %3d%%", int(s.batteryFraction * 100 + 0.5f));
    else
        snprintf(buf, sizeof(buf), "Batt --");
    line(5, buf, false);
    snprintf(buf, sizeof(buf), "%4.0f fps", s.fps);
    line(6, buf, false);
    snprintf(buf, sizeof(buf), "%4.0f Hz IMU", s.imuHz);
    line(7, buf, false);

    display.setTextSize(1);
    display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    display.setTextPadding(kWidth);
    snprintf(buf, sizeof(buf), "%s %s%s", s.profile, s.imuChip, s.recording ? " REC" : (s.atRest ? " rest" : ""));
    display.drawString(buf, 4, 228);
}

}  // namespace

bool begin() {
    ready = display.begin<Board_XIAO_1inch14_LCD<kLcdRst, kLcdBacklight>, Config_Seeed_1inch14_LCD_ST7789>();
    if (!ready) return false;
    display.fillScreen(TFT_BLACK);
    display.panel().setBacklight(0);
    return true;
}

void wake(const Status& status, uint32_t nowMs) {
    if (!ready) return;
    if (!awake) {
        display.panel().wake();
        display.fillScreen(TFT_BLACK);
        display.panel().setBacklight(kBacklightOn);
        awake = true;
    }
    awakeUntilMs = nowMs + kAwakeMs;
    draw(status);
}

void update(const Status& status, uint32_t nowMs) {
    if (!ready || !awake) return;
    if (static_cast<int32_t>(nowMs - awakeUntilMs) >= 0) {
        display.panel().setBacklight(0);
        display.panel().sleep();
        awake = false;
        return;
    }
    draw(status);
}

}  // namespace status_display
