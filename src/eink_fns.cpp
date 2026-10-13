#include "eink_fns.h"
#include "scd4x_fns.h"
#include "sht30_fns.h"
#include <PixelFonts.h>

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));

#define BUTTON_LABEL_H 22
#define BUTTON_LABEL_GAP 4

#define SCREEN_MARGIN_X 6

static void eink_setFont(const GFXfont* font, uint8_t scale = 1) {
    display.setFont(font);
    display.setTextSize(scale);
}

static const GFXfont* const LABEL_FONT = &pixelfont_13x7_semibold;
static const GFXfont* const TEXT_FONT = &pixelfont_13x7_thin;

// How far print() moves the cursor (getTextBounds() ignores trailing advance)
static int16_t eink_textAdvance(const GFXfont* font, const String& text) {
    int16_t w = 0;
    for (unsigned int i = 0; i < text.length(); i++) {
        uint8_t c = text[i];
        if (c >= font->first && c <= font->last) w += pgm_read_byte(&font->glyph[c - font->first].xAdvance);
    }
    return w;
}

// Text between two EMPHASIS markers is printed in LABEL_FONT
#define EMPHASIS "\x02"

static void eink_printEmphasized(const String& text) {
    bool emphasized = false;
    for (int start = 0; start >= 0; emphasized = !emphasized) {
        int end = text.indexOf(EMPHASIS, start);
        eink_setFont(emphasized ? LABEL_FONT : TEXT_FONT);
        display.print(end < 0 ? text.substring(start) : text.substring(start, end));
        start = end < 0 ? -1 : end + 1;
    }
}

static int16_t eink_infoTextWidth(const String& text) {
    int16_t w = 0;
    bool emphasized = false;
    for (int start = 0; start >= 0; emphasized = !emphasized) {
        int end = text.indexOf(EMPHASIS, start);
        w += eink_textAdvance(emphasized ? LABEL_FONT : TEXT_FONT, end < 0 ? text.substring(start) : text.substring(start, end));
        start = end < 0 ? -1 : end + 1;
    }
    return w;
}

// A break inside an emphasized part closes it at the line end and reopens it on the next line
static String eink_wrapLine(const String& line, int16_t maxWidth) {
    if (eink_infoTextWidth(line) <= maxWidth) return line;
    String out, current;
    for (int start = 0; start <= (int)line.length(); ) {
        int end = line.indexOf(' ', start);
        if (end < 0) end = line.length();
        String word = line.substring(start, end);
        String candidate = current.length() ? current + " " + word : word;
        if (current.length() && eink_infoTextWidth(candidate) > maxWidth) {
            int markers = 0;
            for (unsigned int i = 0; i < current.length(); i++) markers += current[i] == EMPHASIS[0];
            bool open = markers % 2;
            out += current + (open ? EMPHASIS : "") + "\n";
            current = (open ? String(EMPHASIS) : String("")) + word;
        } else {
            current = candidate;
        }
        start = end + 1;
    }
    return out + current;
}

// "Label: value" lines: values aligned after the longest label. Other lines are word-wrapped; an empty line is a
// small paragraph gap. Returns the next baseline.
static int16_t eink_printLines(const String& rawText, int16_t y, int16_t lineHeight = 21) {
    String text;
    for (int start = 0; start >= 0; ) {
        int end = rawText.indexOf('\n', start);
        String line = end < 0 ? rawText.substring(start) : rawText.substring(start, end);
        if (start > 0) text += "\n";
        text += line.indexOf(": ") > 0 ? line : eink_wrapLine(line, display.width() - 2 * SCREEN_MARGIN_X);
        start = end < 0 ? -1 : end + 1;
    }

    int16_t valueX = SCREEN_MARGIN_X;
    for (int start = 0; start >= 0; ) {
        int end = text.indexOf('\n', start);
        String line = end < 0 ? text.substring(start) : text.substring(start, end);
        int labelEnd = line.indexOf(": ");
        if (labelEnd > 0) {
            valueX = max(valueX, (int16_t)(SCREEN_MARGIN_X + eink_textAdvance(LABEL_FONT, line.substring(0, labelEnd + 1))));
        }
        start = end < 0 ? -1 : end + 1;
    }

    for (int start = 0; ; y += lineHeight) {
        int end = text.indexOf('\n', start);
        String line = end < 0 ? text.substring(start) : text.substring(start, end);
        if (line.length() == 0 && end >= 0) {  // paragraph gap: the next line comes only 10 px lower
            y += 10 - lineHeight;
            start = end + 1;
            continue;
        }
        int labelEnd = line.indexOf(": ");
        display.setCursor(SCREEN_MARGIN_X, y);
        if (labelEnd > 0) {
            eink_setFont(LABEL_FONT);
            display.print(line.substring(0, labelEnd + 1));
            display.setCursor(valueX, y);
            line = line.substring(labelEnd + 1);  // value, with its leading space
        }
        eink_printEmphasized(line);
        if (end < 0) return y + lineHeight;
        start = end + 1;
    }
}

static void eink_drawButtonLabel(int16_t x, int16_t y, int16_t w, const String& label) {
    display.fillRect(x, y, w, BUTTON_LABEL_H, GxEPD_BLACK);

    int16_t tbx, tby; uint16_t tbw, tbh;
    eink_setFont(&pixelfont_13x7_semibold);
    display.getTextBounds(label, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(x + (w - tbw) / 2 - tbx, y + (BUTTON_LABEL_H - tbh) / 2 - tby);
    display.print(label);
    display.setTextColor(GxEPD_BLACK);
}

static void eink_drawButtonLabels(const String& btn1Label, const String& btn2Label) {
    int16_t labelY = display.height() - BUTTON_LABEL_H;
    int16_t labelW = (display.width() - BUTTON_LABEL_GAP) / 2;
    eink_drawButtonLabel(0, labelY, labelW, btn1Label);
    eink_drawButtonLabel(display.width() - labelW, labelY, labelW, btn2Label);
}

void eink_init() {
    // GxEPD2 writes these before pinMode(), which logs "IO x is not set as GPIO" on ESP32 core 3.x
    pinMode(EPD_CS, OUTPUT);  digitalWrite(EPD_CS, HIGH);
    pinMode(EPD_RST, OUTPUT); digitalWrite(EPD_RST, HIGH);
    pinMode(EPD_DC, OUTPUT);  digitalWrite(EPD_DC, HIGH);
    // initial = false: one refresh pass (~3 s) instead of a clearing pass + the image (~6 s)
    display.init(115200, false);
    display.epd2.selectFastFullUpdate(false);  // the fast full refresh comes out gray on this panel
    display.setRotation(2);
    display.setTextColor(GxEPD_BLACK);
    Serial.println("Display initialized!");
}

// 3D pixel icons for the sensor screen without CO2, in the style of the 3D fonts (outline + bottom-right shadow,
// 16 rows like the 13-row value font). One uint16_t per row, MSB = left column.
#define ICON_ROWS 16
#define ICON_THERMOMETER_W 11
static const uint16_t ICON_THERMOMETER[ICON_ROWS] = {
    0x1E00,  // ...####....
    0x3300,  // ..##..##...
    0x2180,  // ..#....##..
    0x2180,  // ..#....##..
    0x2180,  // ..#....##..
    0x2D80,  // ..#.##.##..
    0x2D80,  // ..#.##.##..
    0x6D80,  // .##.##.##..
    0xCCC0,  // ##..##..##.
    0x9E60,  // #..####..##
    0xBF60,  // #.######.##
    0xBF60,  // #.######.##
    0x9E60,  // #..####..##
    0xC0E0,  // ##......###
    0x7FE0,  // .##########
    0x3FC0,  // ..########.
};
#define ICON_DROP_W 12
static const uint16_t ICON_DROP_PERCENT[ICON_ROWS] = {
    0x0E00,  // ....###.....
    0x0B00,  // ....#.##....
    0x1B00,  // ...##.##....
    0x1180,  // ...#...##...
    0x3180,  // ..##...##...
    0x20C0,  // ..#.....##..
    0x60C0,  // .##.....##..
    0x5960,  // .#.##..#.##.
    0xDA60,  // ##.##.#..##.
    0x8430,  // #....#....##
    0x8B30,  // #...#.##..##
    0x9330,  // #..#..##..##
    0xC070,  // ##.......###
    0x60F0,  // .##.....####
    0x3FE0,  // ..#########.
    0x1FC0,  // ...#######..
};

static void eink_drawIcon(const uint16_t* rows, int16_t x, int16_t y, uint8_t scale) {
    for (int r = 0; r < ICON_ROWS; r++) {
        for (int c = 0; c < 16; c++) {
            if (rows[r] & (0x8000 >> c)) display.fillRect(x + c * scale, y + r * scale, scale, scale, GxEPD_BLACK);
        }
    }
}

void eink_printSensorsData(bool showCO2, float batteryVoltage, String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label) {
    // Negative temperatures are rounded to whole degrees so "-12°" still fits the left half.
    // Degree sign as a single byte: a UTF-8 "°" would also draw 0xC2 (Â). Humidity capped at 99%: always 2 digits.
    String tempStr = (sht30_temp < 0 ? String(lroundf(sht30_temp)) : String(sht30_temp, 1)) + char(0xB0);
    String humStr = String(min((int)sht30_humidity, 99)) + "%";
    const GFXfont* valueFont = &pixelfont_3d_condensed_extended_latin_13x7_semibold;  // 16 rows at 5x = 80 px

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (showCO2) {
            eink_setFont(&pixelfont_3d_condensed_17x7_bold, 5);
            display.setCursor(SCREEN_MARGIN_X, 106);
            display.print(String((int)scd4x_co2));
            eink_setFont(&pixelfont_3d_condensed_5x5_thin, 4);
            display.setCursor(display.getCursorX() + 4, display.getCursorY());  // 1 font pixel gap
            display.print("PPM");

            int16_t humX = display.width() / 2 + SCREEN_MARGIN_X + 5 + 3 * 5;  // + 3 value-font pixels
            eink_setFont(&pixelfont_3d_condensed_5x5_thin, 4);
            display.setCursor(SCREEN_MARGIN_X, 158);
            display.print("TEMP");
            display.setCursor(humX, 158);
            display.print("HUM");

            eink_setFont(valueFont, 5);
            display.setCursor(SCREEN_MARGIN_X, 242);
            display.print(tempStr);
            display.setCursor(humX, 242);
            display.print(humStr);
        } else {
            // No CO2: two rows, icon + value in the CO2 value font (20 rows at 5x = 100 px). The 80 px icons are
            // centered on the values, in a column as wide as the drop.
            const int16_t rowTop[2] = {18, 136}, rowH = 100;
            const int16_t iconColW = ICON_DROP_W * 5, iconDY = (rowH - ICON_ROWS * 5) / 2;
            const int16_t valueX = SCREEN_MARGIN_X + iconColW + 2 * 5;  // 2 value-font pixels after the icon column
            eink_drawIcon(ICON_THERMOMETER, SCREEN_MARGIN_X + (iconColW - ICON_THERMOMETER_W * 5) / 2, rowTop[0] + iconDY, 5);
            eink_drawIcon(ICON_DROP_PERCENT, SCREEN_MARGIN_X, rowTop[1] + iconDY, 5);

            eink_setFont(&pixelfont_3d_condensed_extended_latin_17x7_bold, 5);
            display.setCursor(valueX, rowTop[0] + rowH);  // value glyphs sit fully above the baseline
            display.print(tempStr);
            display.setCursor(valueX, rowTop[1] + rowH);
            display.print(humStr);
        }

        const int16_t itemGap = 16;
        String labels[] = {ssid.length() ? "WiFi:" : "", "Wake:", "Charge:"};
        String values[] = {ssid.length() ? " " + ssid : String("Offline mode"), " " + String(wakeIntervalMinutes) + " min", " " + String(batteryVoltage, 2) + "V"};
        int16_t infoW = 2 * itemGap;
        for (int i = 0; i < 3; i++) infoW += eink_textAdvance(LABEL_FONT, labels[i]) + eink_textAdvance(TEXT_FONT, values[i]);
        while (infoW > display.width() - 2 * SCREEN_MARGIN_X && values[0].length() > 4) {  // shorten a long SSID
            infoW -= eink_textAdvance(TEXT_FONT, values[0]);
            values[0] = values[0].substring(0, values[0].length() - (values[0].endsWith("...") ? 4 : 1)) + "...";
            infoW += eink_textAdvance(TEXT_FONT, values[0]);
        }
        display.setCursor(SCREEN_MARGIN_X, display.height() - BUTTON_LABEL_H - 7);
        for (int i = 0; i < 3; i++) {
            if (i > 0) display.setCursor(display.getCursorX() + itemGap, display.getCursorY());
            eink_setFont(LABEL_FONT);
            display.print(labels[i]);
            eink_setFont(TEXT_FONT);
            display.print(values[i]);
        }

        eink_drawButtonLabels(btn1Label, btn2Label);

    } while (display.nextPage());

    display.hibernate();
    Serial.println("Display updated!");
}

static void eink_displayInfoPage(const String& title, const String& details, const String& instructions,
                                 const String& btn1Label, const String& btn2Label) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        const GFXfont* titleFont = &pixelfont_3d_condensed_13x7_semibold;
        const int16_t titleLineH = 54;
        String line1 = title, line2 = "";
        while (eink_textAdvance(titleFont, line1) * 3 > display.width() - 2 * SCREEN_MARGIN_X && line1.lastIndexOf(' ') > 0) {
            int split = line1.lastIndexOf(' ');
            line2 = line1.substring(split + 1) + (line2.length() ? " " + line2 : "");
            line1 = line1.substring(0, split);
        }
        eink_setFont(titleFont, 3);
        display.setCursor(SCREEN_MARGIN_X, 56);
        display.print(line1);
        int16_t shift = 0;
        if (line2.length()) {
            shift = titleLineH;
            display.setCursor(SCREEN_MARGIN_X, 56 + shift);
            display.print(line2);
        }

        // Without details the explanation moves up to where the details would start
        int16_t y = details.length() > 0 ? eink_printLines(details, 90 + shift) + 10 : 90 + shift;
        eink_printLines(instructions, y);

        eink_drawButtonLabels(btn1Label, btn2Label);

    } while (display.nextPage());

    display.hibernate();
}

void eink_displayWebServerInfo(String title, String details, String instructions, float batteryVoltage, unsigned long timeoutMs, String btn1Label, String btn2Label) {
    String allDetails = details
                      + "\nBattery: " + String(batteryVoltage, 2) + "V"
                      + "\nTimeout: " + (timeoutMs == 0 ? String("none") : String(timeoutMs / 60000) + " min");
    eink_displayInfoPage(title, allDetails, instructions, btn1Label, btn2Label);
    Serial.println("Webserver info displayed!");
}

void eink_displayFirstReadingNotice(String btn1Label, String btn2Label) {
    eink_displayInfoPage("Config saved", "",
                         "The first reading can be a little off, the electronics are\n"
                         "still warm from running the server. Later readings settle.",
                         btn1Label, btn2Label);
    Serial.println("First reading notice displayed!");
}

void eink_displayUnableToConnect(String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label) {
    eink_displayInfoPage("No connection", "",
                         "Could not connect to the WiFi network " EMPHASIS + ssid + EMPHASIS ".\n"
                         "It will try again in " + String(wakeIntervalMinutes) + " min.\n"
                         "\n"
                         "Push " EMPHASIS "TRY AGAIN" EMPHASIS " to retry now.\n"
                         "Push " EMPHASIS "AP MODE" EMPHASIS " to change the WiFi settings.",
                         btn1Label, btn2Label);
    Serial.println("No connection screen displayed!");
}

void eink_displaySetupNeeded(String btn1Label, String btn2Label) {
    eink_displayInfoPage("First run?", "",
                         "Push " EMPHASIS "AP MODE" EMPHASIS " to start the configuration Access Point.\n"
                         "Push " EMPHASIS "OFFLINE MODE" EMPHASIS " to only show the values, without WiFi.\n"
                         "\n"
                         "If the buttons don't work, try connecting a charger.",
                         btn1Label, btn2Label);
    Serial.println("Setup needed screen displayed!");
}
