#include "status_display.h"

#include <Seeed_GFX.h>

#include <algorithm>
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
constexpr int kLineHeight = 20;
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
// Text on the Status page and in the POS / ROT grid: the 16 px proportional
// font. (The 12 px-per-character one doesn't fit a label and three cells of 3
// characters in 135 px.)
constexpr int kTextFont = 2;
// The POS / ROT grid: a label and three left-justified cells per row, offset
// from the label's x. A cell fits 4 characters ("-500" is 30 px).
constexpr int kGridCellX[3] = {30, 64, 98};
constexpr int kGridCellWidth = 31;

enum class Page { Off, Status, Blade };

Seeed_GFX display;
bool ready = false;
bool bladeViewOn = false;
Page page = Page::Off;
uint32_t statusUntilMs = 0;
uint16_t rowPixels[kBarLength * kRowHeight];
// Text is drawn into this sprite in RAM, then sent to the panel in one burst:
// drawn straight to the panel, this library sends text a pixel at a time
// (about 30 ms per character in the 16 px font).
constexpr int kTextHeight = 16;
Seeed_Sprite textSprite;
uint16_t textPixels[kStatusWidth * kTextHeight];
uint8_t gammaLut[256];
uint32_t textUs = 0, barsUs = 0, statusUs = 0;
char shownHeader[84] = "";  // two rows of large text (a banner), or "" when the grid is up
bool gridShown = false;
char shownCell[2][3][8];
int shownSegments = -1;  // hold-C bar segments on show; -1 none
bool labelsShown = false;
// What each Status line shows now; a line is redrawn only when its text or
// highlight changes.
constexpr int kStatusRows = 11;
char shownLine[kStatusRows][40];
bool shownHighlight[kStatusRows];
char shownFooter[48];

void showPage(Page next) {
    if (next == page) return;
    shownHeader[0] = '\0';
    gridShown = false;
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


SPIClass* panelSpi() { return static_cast<Bus_SPI&>(display.panel().driver().bus()).spiInstance(); }

// Draws `text` left-aligned in a w x h box at (x, y), in the current rotation,
// on black. `font` 1 is the 6x8 font (scaled by `size`), 2 the 16 px one.
void pushText(int x, int y, int w, int h, const char* text, uint16_t color, uint8_t font, uint8_t size = 1) {
    w = std::min(w, kStatusWidth);
    h = std::min(h, kTextHeight);
    textSprite.fillSprite(TFT_BLACK);
    textSprite.setTextSize(size);
    textSprite.setTextDatum(TL_DATUM);
    textSprite.setTextColor(color, TFT_BLACK);
    textSprite.drawString(text, 0, 0, font);
    const uint16_t* src = static_cast<const uint16_t*>(textSprite.getPointer());
    for (int r = 0; r < h; ++r) memcpy(textPixels + r * w, src + r * kStatusWidth, size_t(w) * 2);
    display.startWrite();
    display.setAddrWindow(x, y, w, h);
    panelSpi()->writePixels(textPixels, size_t(w) * h * 2);
    display.endWrite();
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

// Draws the POS and ROT rows with their labels at x, at rows y[0] and y[1].
// Only cells whose text changed are redrawn. Upright rotation.
void drawGrid(int x, const int y[2], const int pos[3], const int rot[3]) {
    if (!gridShown) {
        pushText(x, y[0], kGridCellX[0], kTextHeight, "POS", TFT_LIGHTGREY, kTextFont);
        pushText(x, y[1], kGridCellX[0], kTextHeight, "ROT", TFT_LIGHTGREY, kTextFont);
        for (auto& row : shownCell)
            for (auto& cell : row) cell[0] = '\0';
        gridShown = true;
    }
    const int* values[2] = {pos, rot};
    for (int r = 0; r < 2; ++r) {
        for (int i = 0; i < 3; ++i) {
            char cell[8];
            // A half turn either way is the same pose: show it unsigned.
            if (r == 1 && (values[r][i] == 100 || values[r][i] == -100))
                snprintf(cell, sizeof(cell), "100");
            else
                snprintf(cell, sizeof(cell), "%d", values[r][i]);
            if (strcmp(cell, shownCell[r][i]) == 0) continue;
            strcpy(shownCell[r][i], cell);
            pushText(x + kGridCellX[i], y[r], kGridCellWidth, kTextHeight, cell, TFT_WHITE, kTextFont);
        }
    }
}

void statusLine(int row, const char* raw, bool highlight, uint8_t font = kTextFont) {
    const int height = font == 1 ? 8 : kTextHeight;
    char text[40];
    snprintf(text, sizeof(text), "%s", raw);
    upper(text);
    if (strcmp(shownLine[row], text) == 0 && shownHighlight[row] == highlight) return;
    strncpy(shownLine[row], text, sizeof(shownLine[row]) - 1);
    shownHighlight[row] = highlight;
    pushText(4, 6 + row * kLineHeight, kStatusWidth - 4, height, text, highlight ? TFT_YELLOW : TFT_WHITE, font);
}

void drawStatus(const Status& s) {
    const uint32_t start = micros();
    char buf[40];
    using kd::Setting;

    const int gridY[2] = {6, 6 + kLineHeight};
    drawGrid(4, gridY, s.pos, s.rot);
    statusLine(2, kd::palette(s.settings.palette).name, s.changed == Setting::Palette);
    snprintf(buf, sizeof(buf), "Bright %3d%%", int(s.settings.brightnessValue() * 100 + 0.5f));
    statusLine(3, buf, s.changed == Setting::Brightness);
    snprintf(buf, sizeof(buf), "Girth %.2fm", s.settings.girthValue());
    statusLine(4, buf, s.changed == Setting::Girth);
    snprintf(buf, sizeof(buf), "Sky %3d%%", int(s.settings.clearSkyValue() * 100 + 0.5f));
    statusLine(5, buf, s.changed == Setting::ClearSky);
    if (s.settings.motionValue() == 0.f)
        snprintf(buf, sizeof(buf), "Clouds still");
    else
        snprintf(buf, sizeof(buf), "Clouds %.0fx", s.settings.motionValue());
    statusLine(6, buf, s.changed == Setting::Motion);
    snprintf(buf, sizeof(buf), "Size %.1fm", s.settings.cloudSizeValue());
    statusLine(7, buf, s.changed == Setting::CloudSize);
    // The banner (and under it the press bar) in the small font.
    snprintf(buf, sizeof(buf), "%s", s.banner);
    if (char* nl = strchr(buf, '\n')) *nl = ' ';
    statusLine(8, buf, true, 1);
    snprintf(buf, sizeof(buf), "%.0ffps %.0fHz", s.fps, s.imuHz);
    statusLine(9, buf, false);
    if (!s.micOk)
        snprintf(buf, sizeof(buf), "No mic");
    else if (!s.soundOn)
        snprintf(buf, sizeof(buf), "Sound off");
    else
        snprintf(buf, sizeof(buf), "Sound %3d%%", int(s.loudness * 100 + 0.5f));
    statusLine(10, buf, false);

    drawSegments(4, 6 + 8 * kLineHeight + 10, s.holdSegments);
    snprintf(buf, sizeof(buf), "%s %s%s", s.profile, s.imuChip, s.recording ? " REC" : (s.atRest ? " rest" : ""));
    upper(buf);
    if (strcmp(buf, shownFooter) != 0) {
        strcpy(shownFooter, buf);
        pushText(4, 228, kStatusWidth - 4, 8, buf, TFT_DARKGREY, 1);
    }
    statusUs = micros() - start;
}

// Pixels are linear PWM; the panel expects gamma-encoded values. Encode so
// the screen's brightness roughly matches what the LEDs would emit.
uint16_t toPanel(const kd::Rgb8& c) {
    return display.color565(gammaLut[c.r], gammaLut[c.g], gammaLut[c.b]);
}

void drawBlade(const Status& s, const kd::Rgb8* lines, size_t lineCount, size_t ppl) {
    // Header: a banner as two rows of large text, or else the POS / ROT grid.
    // Only what changed is redrawn, leaving the SPI bus to the bars.
    uint32_t t0 = micros();
    const bool segmentsChanged = s.holdSegments != shownSegments;
    if (s.banner[0]) {
        char row1[40] = "", row2[40] = "";
        const char* nl = strchr(s.banner, '\n');
        if (nl) {
            snprintf(row1, sizeof(row1), "%.*s", static_cast<int>(nl - s.banner), s.banner);
            snprintf(row2, sizeof(row2), "%s", nl + 1);
        } else {
            snprintf(row1, sizeof(row1), "%s", s.banner);
        }
        upper(row1);
        upper(row2);
        char key[84];
        snprintf(key, sizeof(key), "%s|%s", row1, row2);
        if (strcmp(key, shownHeader) != 0) {
            display.setRotation(kUpright);
            if (gridShown) display.fillRect(0, 0, kStatusWidth, kHeaderBar - 1, TFT_BLACK);
            gridShown = false;
            strcpy(shownHeader, key);
            pushText(2, kHeaderRow1, kStatusWidth - 2, kTextHeight, row1, TFT_WHITE, 1, 2);
            pushText(2, kHeaderRow2, kStatusWidth - 2, kTextHeight, row2, TFT_WHITE, 1, 2);
            display.setRotation(kLandscape);
        }
    } else {
        display.setRotation(kUpright);
        if (!gridShown) {
            display.fillRect(0, 0, kStatusWidth, kHeaderBar - 1, TFT_BLACK);
            shownHeader[0] = '\0';
        }
        const int rowY[2] = {kHeaderRow1, kHeaderRow2};
        drawGrid(2, rowY, s.pos, s.rot);
        display.setRotation(kLandscape);
    }
    if (segmentsChanged) {
        display.setRotation(kUpright);
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
    SPIClass* spi = panelSpi();
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
    textSprite.setColorDepth(16);
    if (!textSprite.createSprite(display, kStatusWidth, kTextHeight)) return false;
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
    } else if (bladeViewOn && !status.asleep) {
        showPage(Page::Blade);
        drawBlade(status, lines, lineCount, pixelsPerLine);
    } else {
        showPage(Page::Off);
    }
}

}  // namespace status_display
