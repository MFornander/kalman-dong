#include "status_display.h"

#include <Seeed_GFX.h>

#include <cctype>
#include <cmath>

#include "kd/controls.h"

#include "board/boards/XIAO_LCD_Board.h"
#include "bus/Bus_SPI.h"
#include "driver/tft/Driver_ST7789.h"
#include "panel/Panel_TFT.h"

namespace status_display {

namespace {

constexpr int8_t kLcdRst = 13;
constexpr int8_t kLcdBacklight = 12;
constexpr uint8_t kBacklightOn = 160;
// Text reads upright with the Blade (the screen's right edge) pointing up:
// the Status page and the Blade view's header. The bars run in landscape.
constexpr uint8_t kUpright = 2;
constexpr uint8_t kLandscape = 1;

// Status page (upright, 135 x 240)
constexpr int kLineHeight = 22;
constexpr int kStatusWidth = 135;

// Blade view: bars in landscape (240 x 135) from the Hilt (left) toward the
// tip, then a header band at the tip end, drawn upright: in upright
// coordinates it is the top kHeaderBand rows, 135 wide.
constexpr int kHeaderBand = 56;
constexpr int kLabelBand = 14;  // at the tip end, under the header: which side each bar's Line faces
constexpr int kBarLength = 240 - kHeaderBand - kLabelBand;
constexpr int kBarsTop = 2;
constexpr int kRowHeight = 28;
constexpr int kRowGap = 6;
constexpr int kHeaderRow1 = 2, kHeaderRow2 = 21, kHeaderBar = 42;  // upright y

enum class Page { Off, Status, Blade };

Seeed_GFX display;
bool ready = false;
bool bladeViewOn = false;
Page page = Page::Off;
uint32_t statusUntilMs = 0;
uint16_t rowPixels[kBarLength * kRowHeight];
uint8_t gammaLut[256];
uint32_t textUs = 0, barsUs = 0, statusUs = 0;
char shownHeader[84] = "";
int shownSegments = -1;  // hold-C bar segments on show; -1 none
bool labelsShown = false;
// What each Status line shows now. This library draws text a pixel at a
// time (the whole page takes about a second), so a line is redrawn only when
// its text or highlight changes.
constexpr int kStatusRows = 8;
char shownLine[kStatusRows][40];
bool shownHighlight[kStatusRows];
char shownFooter[48];

void showPage(Page next) {
    if (next == page) return;
    shownHeader[0] = '\0';
    for (auto& line : shownLine) line[0] = '\0';
    for (bool& h : shownHighlight) h = false;
    shownFooter[0] = '\0';
    shownSegments = -1;
    labelsShown = false;
    if (next == Page::Off) {
        display.panel().setBacklight(0);
        display.panel().sleep();
    } else {
        if (page == Page::Off) display.panel().wake();
        display.setRotation(next == Page::Blade ? kLandscape : kUpright);
        display.fillScreen(TFT_BLACK);
        display.panel().setBacklight(kBacklightOn);
    }
    page = next;
}


// Upper case reads better at this size.
void upper(char* s) {
    for (; *s; ++s) *s = static_cast<char>(toupper(static_cast<unsigned char>(*s)));
}

// The press bar: one box per half second, teal while letting go is a Quick
// press, amber once it is a Long one; full at Held.
void drawSegments(int x, int y, int filled) {
    if (filled == shownSegments) return;
    constexpr int kBox = 11, kGap = 2, kH = 10;
    const int n = kd::Controls::kSegments;
    if (filled < 0) {
        display.fillRect(x, y, n * (kBox + kGap), kH, TFT_BLACK);
    } else {
        for (int i = 0; i < n; ++i) {
            const int bx = x + i * (kBox + kGap);
            if (i < filled) {
                const bool longPress = i >= kd::Controls::kLongSegments;
                display.fillRect(bx, y, kBox, kH, longPress ? display.color565(255, 179, 71) : display.color565(63, 208, 201));
            } else {
                display.fillRect(bx, y, kBox, kH, TFT_BLACK);
                display.drawRect(bx, y, kBox, kH, TFT_DARKGREY);
            }
        }
    }
    shownSegments = filled;
}

void statusLine(int row, const char* raw, bool highlight) {
    char text[40];
    snprintf(text, sizeof(text), "%s", raw);
    upper(text);
    if (strcmp(shownLine[row], text) == 0 && shownHighlight[row] == highlight) return;
    strncpy(shownLine[row], text, sizeof(shownLine[row]) - 1);
    shownHighlight[row] = highlight;
    display.setTextColor(highlight ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    display.setTextPadding(kStatusWidth);
    display.drawString(text, 4, 6 + row * kLineHeight);
}

void drawStatus(const Status& s) {
    const uint32_t start = micros();
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
    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "%s", s.banner);
    if (char* nl = strchr(buf, '\n')) *nl = ' ';
    statusLine(4, buf, true);
    display.setTextSize(2);

    if (s.batteryFraction >= 0.f)
        snprintf(buf, sizeof(buf), "Batt %3d%%", int(s.batteryFraction * 100 + 0.5f));
    else
        snprintf(buf, sizeof(buf), "Batt --");
    statusLine(5, buf, false);
    snprintf(buf, sizeof(buf), "%4.0f fps", s.fps);
    statusLine(6, buf, false);
    snprintf(buf, sizeof(buf), "%4.0f Hz IMU", s.imuHz);
    statusLine(7, buf, false);

    drawSegments(4, 6 + 4 * kLineHeight + 12, s.holdSegments);
    snprintf(buf, sizeof(buf), "%s %s%s", s.profile, s.imuChip, s.recording ? " REC" : (s.atRest ? " rest" : ""));
    upper(buf);
    if (strcmp(buf, shownFooter) != 0) {
        strcpy(shownFooter, buf);
        display.setTextSize(1);
        display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        display.setTextPadding(kStatusWidth);
        display.drawString(buf, 4, 228);
    }
    statusUs = micros() - start;
}

// Pixels are linear PWM; the panel expects gamma-encoded values. Encode so
// the screen's brightness roughly matches what the LEDs would emit.
uint16_t toPanel(const kd::Rgb8& c) {
    return display.color565(gammaLut[c.r], gammaLut[c.g], gammaLut[c.b]);
}

void drawBlade(const Status& s, const kd::Rgb8* lines, size_t lineCount, size_t ppl) {
    // Header: two rows of large text. This library draws text a pixel at a
    // time, so only redraw when it changes.
    char row1[40] = "", row2[40] = "";
    uint32_t t0 = micros();
    if (s.banner[0]) {
        const char* nl = strchr(s.banner, '\n');
        if (nl) {
            snprintf(row1, sizeof(row1), "%.*s", static_cast<int>(nl - s.banner), s.banner);
            snprintf(row2, sizeof(row2), "%s", nl + 1);
        } else {
            snprintf(row1, sizeof(row1), "%s", s.banner);
        }
    } else {
        // Labels padded to one width so the numbers start in the same column.
        snprintf(row1, sizeof(row1), "%-4s %+d", "Tip", s.bladePitch);
        if (s.headingKnown)
            snprintf(row2, sizeof(row2), "%-4s %+d", "Side", s.bladeHeading);
        else
            snprintf(row2, sizeof(row2), "%-4s --", "Side");
    }
    upper(row1);
    upper(row2);
    char key[84];
    snprintf(key, sizeof(key), "%s|%s", row1, row2);
    const bool headerChanged = strcmp(key, shownHeader) != 0;
    if (headerChanged || s.holdSegments != shownSegments) {
        display.setRotation(kUpright);
        if (headerChanged) {
            strcpy(shownHeader, key);
            display.setTextSize(2);
            display.setTextDatum(TL_DATUM);
            display.setTextColor(TFT_WHITE, TFT_BLACK);
            display.setTextPadding(kStatusWidth);
            display.drawString(row1, 2, kHeaderRow1);
            display.drawString(row2, 2, kHeaderRow2);
        }
        drawSegments(2, kHeaderBar, s.holdSegments);
        display.setRotation(kLandscape);
    }
    // Which side of the Blade each Line faces, with Line 0 on top, looking
    // along the Blade from the Hilt. Drawn upright above each bar.
    if (!labelsShown && lineCount == 4) {
        static const char* const kSides[4] = {"TOP", "R", "B", "L"};
        display.setRotation(kUpright);
        display.setTextSize(1);
        display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        display.setTextPadding(0);
        display.setTextDatum(TC_DATUM);
        for (int i = 0; i < 4; ++i)
            display.drawString(kSides[i], kBarsTop + i * (kRowHeight + kRowGap) + kRowHeight / 2, kHeaderBand + 3);
        display.setTextDatum(TL_DATUM);
        display.setRotation(kLandscape);
        labelsShown = true;
    }
    uint32_t t1 = micros();
    textUs = t1 - t0;

    if (ppl == 0) return;
    SPIClass* spi = static_cast<Bus_SPI&>(display.panel().driver().bus()).spiInstance();
    for (size_t line = 0; line < lineCount; ++line) {
        const kd::Rgb8* px = lines + line * ppl;
        for (int x = 0; x < kBarLength; ++x) rowPixels[x] = toPanel(px[size_t(x) * ppl / kBarLength]);
        for (int y = 1; y < kRowHeight; ++y) memcpy(rowPixels + y * kBarLength, rowPixels, kBarLength * 2);
        // pushImage() sends one pixel per SPI call; send the bar in one burst.
        display.startWrite();
        display.setAddrWindow(0, kBarsTop + int(line) * (kRowHeight + kRowGap), kBarLength, kRowHeight);
        spi->writePixels(rowPixels, sizeof(rowPixels));
        display.endWrite();
    }
    barsUs = micros() - t1;
}

}  // namespace

bool begin() {
    for (int i = 0; i < 256; ++i) gammaLut[i] = static_cast<uint8_t>(std::pow(i / 255.f, 1.f / 2.2f) * 255.f + 0.5f);
    ready = display.begin<Board_XIAO_1inch14_LCD<kLcdRst, kLcdBacklight>, Config_Seeed_1inch14_LCD_ST7789>();
    if (!ready) return false;
    display.fillScreen(TFT_BLACK);
    display.panel().setBacklight(0);
    page = Page::Off;
    return true;
}

void setBladeView(bool on) { bladeViewOn = on; }
const char* pageName() { return page == Page::Off ? "off" : (page == Page::Status ? "status" : "blade"); }
void drawTimes(uint32_t& text, uint32_t& bars) {
    text = textUs;
    bars = barsUs;
}
uint32_t statusDrawUs() { return statusUs; }
bool bladeView() { return bladeViewOn; }

void wake(const Status& status, uint32_t nowMs) {
    if (!ready) return;
    statusUntilMs = nowMs + kAwakeMs;
    showPage(Page::Status);
    drawStatus(status);
}

void toggle(const Status& status, uint32_t nowMs) {
    if (!ready) return;
    if (page == Page::Status && static_cast<int32_t>(statusUntilMs - nowMs) > 0)
        statusUntilMs = nowMs;  // the next update() closes it
    else
        wake(status, nowMs);
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
