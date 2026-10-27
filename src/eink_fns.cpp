#include "eink_fns.h"
#include "scd4x_fns.h"
#include "sht30_fns.h"
#include "utils.h"
#include <PixelFonts.h>

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));

// Distance between the dots on the two enclosure buttons the labels point at
#define BUTTON_DOT_GAP 113
#define BUTTON_LABEL_BAND 37      // bottom edge to the baseline of whatever sits above the labels
#define BUTTON_LABEL_BASELINE 9  // bottom edge to the label baseline
#define BUTTON_LABEL_LINE_GAP 6   // top of the label text to the top of the line over it
#define BUTTON_LABEL_LINE_W 2
#define BUTTON_LABEL_FOOT_W 12
#define BUTTON_LABEL_FOOT_LIFT 3  // the bottom edge would swallow a crossbar sitting on it

#define SCREEN_MARGIN_X 6

static uint16_t paperColor() { return darkMode() ? GxEPD_BLACK : GxEPD_WHITE; }
static uint16_t inkColor() { return darkMode() ? GxEPD_WHITE : GxEPD_BLACK; }

// Border waveform
#define BORDER_LUT_WHITE 0x01
#define BORDER_LUT_BLACK 0x02

static void eink_setBorder(uint8_t value) {
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    digitalWrite(EPD_DC, LOW);
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(0x3C);
    digitalWrite(EPD_CS, HIGH);
    digitalWrite(EPD_DC, HIGH);
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(value);
    digitalWrite(EPD_CS, HIGH);
    SPI.endTransaction();
}

static void eink_beginFrame() {
    display.setFullWindow();
    display.epd2.writeScreenBuffer(darkMode() ? 0x00 : 0xFF);  // runs the driver's init sequence
    eink_setBorder(darkMode() ? BORDER_LUT_BLACK : BORDER_LUT_WHITE);
    display.setTextColor(inkColor());
    display.firstPage();
}

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

// Values are aligned after the longest label of the block. Returns the next baseline
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
            line = line.substring(labelEnd + 1);
        }
        eink_printEmphasized(line);
        if (end < 0) return y + lineHeight;
        start = end + 1;
    }
}

static void eink_drawButtonLabel(const String& label, bool right, int16_t dotX) {
    int16_t tbx, tby; uint16_t tbw, tbh;
    eink_setFont(&pixelfont_13x7_semibold);
    display.getTextBounds(label, 0, 0, &tbx, &tby, &tbw, &tbh);

    int16_t baseline = display.height() - BUTTON_LABEL_BASELINE;
    int16_t textX = right ? display.width() - SCREEN_MARGIN_X - (int16_t)tbw : SCREEN_MARGIN_X;
    display.setCursor(textX - tbx, baseline);
    display.print(label);

    // The drop sits on the outer side of its dot, which mirrors the two around the screen centre
    int16_t lineY = baseline + tby - BUTTON_LABEL_LINE_GAP;
    int16_t edgeX = right ? textX + (int16_t)tbw - 1 : textX;
    int16_t dropX = right ? dotX - BUTTON_LABEL_LINE_W + 1 : dotX;
    int16_t x0 = min(edgeX, dropX);
    int16_t x1 = max(edgeX, (int16_t)(dropX + BUTTON_LABEL_LINE_W - 1));
    display.fillRect(x0, lineY, x1 - x0 + 1, BUTTON_LABEL_LINE_W, inkColor());
    int16_t footY = display.height() - BUTTON_LABEL_LINE_W - BUTTON_LABEL_FOOT_LIFT;
    display.fillRect(dropX, lineY, BUTTON_LABEL_LINE_W, footY + BUTTON_LABEL_LINE_W - lineY, inkColor());
    display.fillRect(dropX - (BUTTON_LABEL_FOOT_W - BUTTON_LABEL_LINE_W) / 2, footY,
                     BUTTON_LABEL_FOOT_W, BUTTON_LABEL_LINE_W, inkColor());
}

static void eink_drawButtonLabels(const String& btn1Label, const String& btn2Label) {
    int16_t center = display.width() / 2;
    // Rounded outwards on the right so an odd gap still moves the pair apart by the full amount
    eink_drawButtonLabel(btn1Label, false, center - BUTTON_DOT_GAP / 2);
    eink_drawButtonLabel(btn2Label, true, center + (BUTTON_DOT_GAP + 1) / 2);
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
    Serial.println("Display initialized!");
}

// One uint16_t per row, MSB = left column
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
            if (rows[r] & (0x8000 >> c)) display.fillRect(x + c * scale, y + r * scale, scale, scale, inkColor());
        }
    }
}

void eink_printSensorsData(bool showCO2, float batteryVoltage, String ssid, int wakeIntervalMinutes, String btn1Label, String btn2Label) {
    // Rounded and capped so the side-by-side layout always fits; 0xB0 as one byte, a UTF-8 degree would add 0xC2
    String tempStr = (sht30_temp < 0 ? String(lroundf(sht30_temp)) : String(sht30_temp, 1)) + char(0xB0);
    String humStr = String(min((int)sht30_humidity, 99)) + "%";
    const GFXfont* valueFont = &pixelfont_3d_condensed_extended_latin_13x7_semibold;  // 16 rows at 5x = 80 px

    eink_beginFrame();
    do {
        display.fillScreen(paperColor());

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
            // The 80 px icons are centered on the 100 px values, in a column as wide as the drop
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
        String labels[] = {ssid.length() ? "WiFi:" : "", "Wake:", "Battery:"};
        String values[] = {ssid.length() ? " " + ssid : String("Offline mode"), " " + String(wakeIntervalMinutes) + " min",
                           batteryCharging(batteryVoltage) ? String(" Charging") : " " + String(batteryVoltage, 2) + "V"};
        int16_t infoW = 2 * itemGap;
        for (int i = 0; i < 3; i++) infoW += eink_textAdvance(LABEL_FONT, labels[i]) + eink_textAdvance(TEXT_FONT, values[i]);
        while (infoW > display.width() - 2 * SCREEN_MARGIN_X && values[0].length() > 4) {  // shorten a long SSID
            infoW -= eink_textAdvance(TEXT_FONT, values[0]);
            values[0] = values[0].substring(0, values[0].length() - (values[0].endsWith("...") ? 4 : 1)) + "...";
            infoW += eink_textAdvance(TEXT_FONT, values[0]);
        }
        display.setCursor(SCREEN_MARGIN_X, display.height() - BUTTON_LABEL_BAND);
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
    eink_beginFrame();
    do {
        display.fillScreen(paperColor());

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

void eink_displayFirstReadingNotice(uint32_t firstReadingS, bool calibrating, String btn1Label, String btn2Label) {
    if (calibrating) {
        eink_displayInfoPage("Config saved", "",
                             "Configuration has been saved, the CO2 sensor will be calibrated on the next wakeup in "
                             EMPHASIS + String(firstReadingS / 60) + " min" EMPHASIS ".",
                             btn1Label, btn2Label);
        Serial.println("Config saved notice displayed, calibration armed!");
        return;
    }

    String text = firstReadingS >= 60
        ? "CO2 measurement has started, the display shows the first reading in "
          EMPHASIS + String(firstReadingS / 60) + " min" EMPHASIS ".\n\n"
        : "";
    text += "The first measurement can be a little off, the electronics are still warm from the WiFi. "
            "Later readings settle.";
    eink_displayInfoPage("Config saved", "", text, btn1Label, btn2Label);
    Serial.println("First reading notice displayed!");
}

void eink_displayCalibrated(uint32_t firstReadingS, String btn1Label, String btn2Label) {
    eink_displayInfoPage("Calibrated", "",
                         "The CO2 sensor has been calibrated, the first measurement will show in "
                         EMPHASIS + String(firstReadingS / 60) + " min" EMPHASIS ".",
                         btn1Label, btn2Label);
    Serial.println("Calibrated screen displayed!");
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
