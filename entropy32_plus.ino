/*
 * Entropy32 Plus - "The Universe Bifurcator"
 * ---------------------------------------------------------
 * Geiger-pulse based TRNG -> BIP39 seed phrase generator
 *
 * Hardware:
 *   - ATmega328P
 *   - 0.91" 128x32 OLED (SSD1306) via I2C (SDA=A4, SCL=A5)
 *   - Processed Geiger pulses (post-LM393) on D2 (INT0)
 *   - Back button  -> D4 (active LOW, internal pull-up)
 *   - Forward button -> D5 (active LOW, internal pull-up)
 *
 * Entropy method:
 *   1. Capture inter-arrival time between successive Geiger pulses.
 *   2. Compare each interval to the previous one:
 *        longer  -> bit 1
 *        shorter -> bit 0
 *        equal   -> discarded (extremely rare at microsecond resolution)
 *   3. Collect a raw pool of comparison-bits (RAW_POOL_BITS).
 *   4. Whiten/condition the raw pool with SHA-256 to remove any
 *      residual structure (dead-time correlation, count-rate drift, etc).
 *   5. Follow the standard BIP39 process on the conditioned entropy:
 *      compute checksum = first ENT/32 bits of SHA256(entropy),
 *      append it, split into 11-bit chunks, map each chunk to a
 *      word in the official 2048-word list.
 *
 * IMPORTANT / CAVEAT:
 *   This is a hobbyist/prototype entropy source. Before trusting output
 *   from this device for a seed that will secure real funds:
 *     - Log raw inter-arrival times over a long run and evaluate them
 *       with the NIST SP 800-90B non-IID min-entropy estimators (not
 *       just SP 800-22 pass/fail randomness tests) to confirm the raw
 *       pool has genuinely enough min-entropy for what you're claiming.
 *     - Consider mixing this source with a second, independently-
 *       validated entropy source (e.g. your avalanche-noise Bifurcator)
 *       before finalizing a seed for anything holding real value.
 *     - Never transcribe a generated phrase for real funds directly off
 *       a device you haven't independently audited end-to-end.
 *
 * Required libraries:
 *   - U8g2 (olikraus) - only the U8x8 text-mode API is used. U8x8 talks
 *     directly to the display a character cell at a time with no RAM
 *     framebuffer and a compact built-in font, unlike Adafruit_GFX +
 *     Adafruit_SSD1306 (which pull in a full graphics stack - shape
 *     drawing, a 512-byte framebuffer, and unrelated SPI-TFT code paths
 *     the linker can't strip because of virtual dispatch). That combo
 *     alone doesn't fit in the ATmega328P's 32KB flash next to the rest
 *     of this sketch. Install "U8g2" via the Arduino IDE Library Manager.
 *
 * Required companion files (same sketch folder):
 *   - sha256.h / sha256.cpp   (included, self-contained, no dependency)
 *   - bip39_wordlist.h        (generate with generate_wordlist.py)
 */

#include <Wire.h>
#include <U8x8lib.h>
#include <ctype.h>
#include "sha256.h"
#include "bip39_wordlist.h"
#include "button.h"
#include "logo_bitmap.h"

// ---------------- Pin assignments ----------------
#define GEIGER_PIN 2   // INT0 - processed pulse edge from LM393
#define BACK_PIN   4
#define FWD_PIN    5

// ---------------- OLED ----------------
// 0.91" SSD1306 modules are almost always 128x32 pixels, giving a 16
// column x 4 row text grid with U8x8's 8px-wide font tiles. Common I2C
// addresses are 0x3C or 0x3D - run an I2C scanner sketch once if the
// display doesn't init, and adjust OLED_ADDR below.
#define OLED_ADDR    0x3C
#define OLED_COLS    16
#define OLED_ROWS    4

U8X8_SSD1306_128X32_UNIVISION_HW_I2C lcd(U8X8_PIN_NONE);

// ---------------- CPM tracking ----------------
#define CPM_WINDOW_SECONDS 60
volatile uint32_t totalPulseCount = 0; // all raw pulses seen by the ISR, unfiltered by MIN_INTERVAL_US

uint16_t cpmSecondBuckets[CPM_WINDOW_SECONDS] = {0};
uint8_t  cpmBucketIdx        = 0;
uint32_t cpmRollingSum       = 0;  // sum of the last <=60 one-second buckets
uint32_t cpmLastPulseSnapshot = 0;
unsigned long cpmLastTickMs  = 0;
uint8_t  cpmSecondsElapsed   = 0;  // caps at CPM_WINDOW_SECONDS; used to scale early readings

// ---------------- Entropy collection ----------------
#define RAW_POOL_BITS    512     // raw comparison-bits collected before offering the menu
#define MIN_INTERVAL_US  200     // reject intervals shorter than this (debounce/glitch guard)

volatile uint8_t  entropyPool[RAW_POOL_BITS / 8]; // bit-packed raw pool
volatile uint16_t poolBitIndex   = 0;
volatile unsigned long lastPulseMicros = 0;
volatile unsigned long prevInterval    = 0;
volatile bool intervalValid = false;

void geigerISR() {
  totalPulseCount++;
  unsigned long now = micros();
  if (lastPulseMicros != 0) {
    unsigned long interval = now - lastPulseMicros;
    if (interval >= MIN_INTERVAL_US) {
      if (intervalValid && poolBitIndex < RAW_POOL_BITS) {
        if (interval != prevInterval) {
          uint8_t bit = (interval > prevInterval) ? 1 : 0;
          uint16_t byteIdx   = poolBitIndex >> 3;
          uint8_t  bitOffset = poolBitIndex & 0x07;
          if (bit) entropyPool[byteIdx] |=  (1 << bitOffset);
          else     entropyPool[byteIdx] &= ~(1 << bitOffset);
          poolBitIndex++;
        }
      }
      prevInterval = interval;
      intervalValid = true;
      lastPulseMicros = now;
    }
  } else {
    lastPulseMicros = now;
  }
}

// ---------------- Buttons (polled, debounced) ----------------
Button backBtn = { BACK_PIN, HIGH, 0 };
Button fwdBtn  = { FWD_PIN,  HIGH, 0 };
#define DEBOUNCE_MS 30

bool buttonPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  bool pressed = false;

  if (reading != b.lastState &&
      (millis() - b.lastChange) > DEBOUNCE_MS) {

    b.lastChange = millis();

    if (reading == LOW)
      pressed = true;

    b.lastState = reading;
  }

  return pressed;
}

// Blocks until both nav buttons are physically released (plus one
// debounce interval to let contact bounce settle) before the caller
// switches into STATE_SHOW_WORD. That state's page navigation is
// driven by buttonPressed()/fwdBtn/backBtn, which is separate debounce
// state from the raw digitalRead() combo-detection in readMenuAction().
// Without this wait, a button still held down at the exact moment of
// the state switch (very common right after a BACK+FWD combo press, or
// a held BACK press) looks to buttonPressed() like a brand-new press,
// silently skipping page 0 of the seed words.
void waitForNavButtonsReleased() {
  while (digitalRead(BACK_PIN) == LOW || digitalRead(FWD_PIN) == LOW) {
    delay(5);
  }
  delay(DEBOUNCE_MS);
}

// ---------------- Menu buttons ----------------
//
// BACK alone   = previous
// FWD alone    = next
// BACK + FWD   = select

#define COMBO_WINDOW_MS 150

enum MenuAction {
  MENU_NONE,
  MENU_BACK,
  MENU_FWD,
  MENU_SELECT
};

MenuAction readMenuAction();

bool menuComboPending = false;
bool menuComboHandled = false;
bool menuNavHandled = false;
bool menuFirstWasBack = false;
unsigned long menuFirstPressMs = 0;

MenuAction readMenuAction() {

  bool backDown = (digitalRead(BACK_PIN) == LOW);
  bool fwdDown  = (digitalRead(FWD_PIN)  == LOW);

  // Both buttons pressed = SELECT
  if (backDown && fwdDown) {
    if (!menuComboHandled) {
      menuComboHandled = true;
      return MENU_SELECT;
    }
    return MENU_NONE;
  }

  // Wait for both buttons to be released after SELECT
  if (menuComboHandled) {
    if (!backDown && !fwdDown) {
      menuComboHandled = false;
    }
    return MENU_NONE;
  }

  // A button is currently held down.
  // Don't generate another action until it is released.
  if (backDown || fwdDown) {
    if (!menuComboPending && !menuNavHandled) {
      menuComboPending = true;
      menuFirstWasBack = backDown;
      menuFirstPressMs = millis();
    }

    // Single button held long enough = one navigation action.
    if (menuComboPending && millis() - menuFirstPressMs >= COMBO_WINDOW_MS) {
      menuComboPending = false;
      menuNavHandled = true;
      return menuFirstWasBack ? MENU_BACK : MENU_FWD;
    }

    return MENU_NONE;
  }

  // Both released.
  menuComboPending = false;
  menuNavHandled = false;

  return MENU_NONE;
}

// ---------------- App state machine ----------------
enum AppState {
  STATE_COLLECTING,
  STATE_MENU_LENGTH,
  STATE_SHOW_WORD,
  STATE_DONE,
  STATE_WIPE_CONFIRM
};
AppState state = STATE_COLLECTING;

uint8_t  selectedLength  = 12;   // toggled between 12 / 24 in the menu
uint8_t  wordCount       = 0;
uint16_t wordIndices[24];

// Seed words are shown WORDS_PER_PAGE at a time (one page fills the whole
// screen) instead of one word per screen, so a 24-word seed takes 6
// button presses to read instead of 24.
#define WORDS_PER_PAGE 4
uint8_t currentPage = 0;

uint8_t totalPages() {
  return (wordCount + WORDS_PER_PAGE - 1) / WORDS_PER_PAGE;
}

// Looks up word `index` (0-2047) from the packed BIP39_WORDLIST_BLOB and
// copies it (plain ASCII, null-terminated) into `out`. The blob has no
// index -> offset table (that alone would cost 4KB of flash we don't
// have), so this walks null-terminated words from the start of the blob.
// Called at most WORDS_PER_PAGE times per screen draw and 24 times per
// generated phrase, so the O(index) scan cost is negligible.
void getWordAtIndex(uint16_t index, char* out, uint8_t outSize) {
  const char* p = BIP39_WORDLIST_BLOB;
  while (index > 0) {
    while (pgm_read_byte(p) != 0) p++;
    p++;
    index--;
  }
  uint8_t i = 0;
  uint8_t c;
  while ((c = pgm_read_byte(p)) != 0 && i < outSize - 1) {
    out[i++] = c;
    p++;
  }
  out[i] = '\0';
}

// ---------------- SHA-256 boot self-test ----------------
// Known-answer test vector: SHA-256("abc")
// Independently verifiable, e.g.:
//   echo -n "abc" | openssl dgst -sha256
//   python3 -c "import hashlib; print(hashlib.sha256(b'abc').hexdigest())"
const uint8_t KAT_INPUT[3] PROGMEM = { 'a', 'b', 'c' };
const uint8_t KAT_EXPECTED[32] PROGMEM = {
  0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
  0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
  0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
  0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

// Runs the KAT and halts on failure (infinite loop, does not return)
// with a FAIL screen. On success, briefly shows PASS + a short hash
// fingerprint before continuing into normal operation.
void runSHA256SelfTest() {
  drawStatusLine("Testing SHA-256");
  delay(1000);

  uint8_t katInputRam[3];
  memcpy_P(katInputRam, KAT_INPUT, sizeof(katInputRam));

  SHA256 sha;
  sha.update(katInputRam, sizeof(katInputRam));
  uint8_t digest[32];
  sha.finalize(digest);

  bool pass = true;
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t expectedByte = pgm_read_byte(&KAT_EXPECTED[i]);
    if (digest[i] != expectedByte) {
      pass = false;
      break;
    }
  }

  if (pass) {
    // Show first 4 hex bytes of the digest as a quick visual
    // fingerprint the user can cross-check against the published
    // vector (ba7816bf...) if they want extra confidence.
    char line[15];
    strcpy(line, "PASS  ");
    for (uint8_t i = 0; i < 4; i++) {
      char hex[3];
      const char* hexDigits = "0123456789abcdef";
      hex[0] = hexDigits[(digest[i] >> 4) & 0x0F];
      hex[1] = hexDigits[digest[i] & 0x0F];
      hex[2] = '\0';
      strcat(line, hex);
    }
    drawStatusLine(line);
    delay(1800);
  } else {
    // Do not proceed. A broken conditioning step must never silently
    // feed into seed generation.
    drawStatusLine("FAIL - HALTED");
    while (true) {
      // halt indefinitely; user must power-cycle after investigating
      delay(1000);
    }
  }
}

// Verifies every word in BIP39_WORDLIST_BLOB fits in wordBuf (see
// drawWordScreen()). Halts on boot if a regenerated/modified wordlist
// ever contains an oversized entry, rather than silently overflowing
// the stack later.
void runWordlistLengthCheck() {
  drawStatusLine("Check wordlist");
  delay(1000);
  const char* p = BIP39_WORDLIST_BLOB;
  for (uint16_t i = 0; i < BIP39_WORD_COUNT; i++) {
    uint8_t len = 0;
    uint8_t c;
    while ((c = pgm_read_byte(p++)) != 0) len++;
    if (len > BIP39_MAX_WORD_LEN) {
      drawStatusLine("Wordlist error!");
      while (true) { delay(1000); } // halt - do not proceed to entropy collection
    }
  }
  drawStatusLine("PASS");
  delay(1800);
}

// Blits LOGO_BITMAP (128x24, SSD1306 page-column format - the top 3 of
// the display's 4 text rows) straight to the panel via drawTile(),
// bypassing the font system entirely. drawTile() reads its source from
// RAM (it just memcpy's into the I2C send buffer), so each 16-tile page
// row is staged through rowBuf rather than handing it a PROGMEM pointer
// directly, which on AVR would copy raw flash addresses instead of the
// flash contents. Row 3 is left untouched for drawStatusLine().
void drawLogoScreen() {
  uint8_t rowBuf[16 * 8]; // one page row: 16 tiles wide x 8 bytes/tile
  for (uint8_t page = 0; page < 3; page++) {
    memcpy_P(rowBuf, &LOGO_BITMAP[page * sizeof(rowBuf)], sizeof(rowBuf));
    lcd.drawTile(0, page, 16, rowBuf);
  }
}

// Draws one line of boot status text in the row beneath the logo,
// space-padded to the full column width so a shorter message can't
// leave stale characters from a longer previous one.
void drawStatusLine(const char* text) {
  char line[OLED_COLS + 1];
  uint8_t i = 0;
  for (; i < OLED_COLS && text[i] != '\0'; i++) line[i] = text[i];
  for (; i < OLED_COLS; i++) line[i] = ' ';
  line[OLED_COLS] = '\0';
  lcd.drawString(0, 3, line);
}

// ---------------- Setup ----------------
void setup() {
  pinMode(GEIGER_PIN, INPUT);
  pinMode(BACK_PIN, INPUT);
  pinMode(FWD_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(GEIGER_PIN), geigerISR, RISING);

  // Confirm the display actually acks on the bus before handing off to
  // U8x8 (whose begin() has no failure path of its own) - a seed device
  // that silently can't show its output is worse than one that halts.
  Wire.begin();
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() != 0) {
    while (true) { delay(1000); }
  }

  lcd.setI2CAddress(OLED_ADDR << 1);
  lcd.begin();
  // begin() powers the panel on with whatever was left in GDDRAM, and the
  // draw below is a second full-frame write on top of that - back to
  // back, that's the double flash on boot. Hold the panel off until the
  // first real frame is drawn, then power on once.
  lcd.setPowerSave(1);
  lcd.setFlipMode(1); // display is mounted upside-down in the enclosure
  lcd.setFont(u8x8_font_5x7_r);
  drawLogoScreen();
  drawStatusLine("Booting...");
  lcd.setPowerSave(0);
  delay(600);

  runSHA256SelfTest(); // halts here if the SHA-256 implementation is broken
  runWordlistLengthCheck(); // halts here if bip39_wordlist.h has an oversized entry

  lcd.clear();

  cpmLastTickMs = millis();
  cpmLastPulseSnapshot = totalPulseCount;
}

// ---------------- Main loop ----------------
void loop() {
  switch (state) {

    case STATE_COLLECTING:
      updateCollectingScreen();
      if (poolBitIndex >= RAW_POOL_BITS) {
        state = STATE_MENU_LENGTH;
        drawMenuScreen();
      }
      break;

    case STATE_MENU_LENGTH: {
      MenuAction action = readMenuAction();

      if (action == MENU_BACK) {
        selectedLength = (selectedLength == 12) ? 24 : 12;
        drawMenuScreen();

      } else if (action == MENU_FWD) {
        selectedLength = (selectedLength == 12) ? 24 : 12;
        drawMenuScreen();

      } else if (action == MENU_SELECT) {
        waitForNavButtonsReleased();
        generatePhrase();
        state = STATE_SHOW_WORD;
        currentPage = 0;
        drawWordScreen();
      }

      break;
    }

    case STATE_SHOW_WORD:
      if (buttonPressed(fwdBtn)) {
        if (currentPage < totalPages() - 1) {
          currentPage++;
          drawWordScreen();
        } else {
          state = STATE_DONE;
          drawDoneScreen();
        }
      }
      if (buttonPressed(backBtn)) {
        if (currentPage > 0) {
          currentPage--;
          drawWordScreen();
        }
      }
      break;

    case STATE_DONE: {
      MenuAction action = readMenuAction();

      if (action == MENU_BACK) {
        waitForNavButtonsReleased();
        state = STATE_SHOW_WORD;
        currentPage = totalPages() - 1;
        drawWordScreen();

      } else if (action == MENU_SELECT) {
        state = STATE_WIPE_CONFIRM;
        drawWipeConfirmScreen();
      }

      break;
    }

    case STATE_WIPE_CONFIRM: {
      MenuAction action = readMenuAction();

      if (action == MENU_SELECT) {
        wipeSeed();
        state = STATE_COLLECTING;
        lcd.clear();
        lcd.drawString(0, 0, "Entropy32");
        lcd.drawString(0, 2, "Collecting...");

      } else if (action == MENU_BACK || action == MENU_FWD) {
        // Any single-button press cancels back to the done screen
        // rather than the wipe.
        state = STATE_DONE;
        drawDoneScreen();
      }

      break;
    }
  }
}

// ---------------- Screens ----------------
unsigned long lastCollectDraw = 0;

// Rolls the pulse count into a 60-second sliding window, called once per
// second. During the first 60s after boot the window isn't full yet, so
// we scale the observed rate up proportionally rather than under-report
// (e.g. 5 counts in the first 5 seconds reads as ~60 CPM, not ~5 CPM).
void updateCPMWindow() {
  if (millis() - cpmLastTickMs < 1000) return;
  cpmLastTickMs += 1000;

  noInterrupts();
  uint32_t currentTotal = totalPulseCount;
  interrupts();

  uint32_t deltaThisSecond = currentTotal - cpmLastPulseSnapshot;
  cpmLastPulseSnapshot = currentTotal;

  cpmRollingSum -= cpmSecondBuckets[cpmBucketIdx];
  cpmSecondBuckets[cpmBucketIdx] = (deltaThisSecond > 65535UL) ? 65535 : (uint16_t)deltaThisSecond;
  cpmRollingSum += cpmSecondBuckets[cpmBucketIdx];

  cpmBucketIdx = (cpmBucketIdx + 1) % CPM_WINDOW_SECONDS;

  if (cpmSecondsElapsed < CPM_WINDOW_SECONDS) cpmSecondsElapsed++;
}

uint32_t getCurrentCPM() {
  if (cpmSecondsElapsed == 0) return 0;
  if (cpmSecondsElapsed < CPM_WINDOW_SECONDS) {
    return (cpmRollingSum * CPM_WINDOW_SECONDS) / cpmSecondsElapsed; // extrapolate partial window
  }
  return cpmRollingSum; // full 60s of data = directly CPM
}

// Formats CPM with K/M/G suffixes so the value can never overrun a fixed-
// width field, regardless of count rate. Max output is 6 chars + null
// (e.g. "12.3K", "4.2G") — uint32_t itself tops out around 4.29 billion,
// so the G tier alone covers the entire representable range.
void formatCPM(uint32_t cpm, char* out) {
  const char* suffix = "";
  uint32_t divisor = 1;
  if (cpm >= 1000000000UL)      { suffix = "G"; divisor = 1000000000UL; }
  else if (cpm >= 1000000UL)    { suffix = "M"; divisor = 1000000UL; }
  else if (cpm >= 1000UL)       { suffix = "K"; divisor = 1000UL; }

  if (divisor == 1) {
    sprintf(out, "%lu", cpm); // 0-999, no suffix, max 3 chars
    return;
  }

  uint32_t whole = cpm / divisor;
  if (whole >= 100) {
    // 3-digit whole + suffix = 4 chars; adding a decimal here would hit 6, so we drop it.
    sprintf(out, "%lu%s", whole, suffix);
  } else {
    // Up to "99.9K" = 5 chars, exactly fits the field.
    uint32_t frac = ((cpm % divisor) * 10) / divisor;
    sprintf(out, "%lu.%lu%s", whole, frac, suffix);
  }
}

// Renders an ASCII progress bar exactly OLED_COLS wide, e.g.
// "[#######-------]", by filling PROGRESS_BAR_WIDTH segments in
// proportion to current/maxVal. `out` must be at least OLED_COLS+1 bytes.
#define PROGRESS_BAR_WIDTH (OLED_COLS - 2) // 2 cols spent on the brackets
void buildProgressBar(uint16_t current, uint16_t maxVal, char* out) {
  uint8_t filled = (uint8_t)(((uint32_t)current * PROGRESS_BAR_WIDTH) / maxVal);
  if (filled > PROGRESS_BAR_WIDTH) filled = PROGRESS_BAR_WIDTH;

  out[0] = '[';
  uint8_t i;
  for (i = 0; i < filled; i++) out[1 + i] = '#';
  for (; i < PROGRESS_BAR_WIDTH; i++) out[1 + i] = '-';
  out[1 + PROGRESS_BAR_WIDTH] = ']';
  out[2 + PROGRESS_BAR_WIDTH] = '\0';
}

// Formats a duration for the "time left" estimate. Caps at "99h+" rather
// than letting an absurdly weak/absent source (near-zero CPM) overflow
// the field with a multi-day estimate.
void formatDuration(uint32_t totalSeconds, char* out, size_t outSize) {
  if (totalSeconds >= 359999UL) { // >= 99h59m59s
    snprintf(out, outSize, "99h+");
    return;
  }
  uint32_t h = totalSeconds / 3600;
  uint32_t m = (totalSeconds % 3600) / 60;
  uint32_t s = totalSeconds % 60;
  if (h > 0)      snprintf(out, outSize, "%luh%02lum", h, m);
  else if (m > 0) snprintf(out, outSize, "%lum%02lus", m, s);
  else            snprintf(out, outSize, "%lus", s);
}

void updateCollectingScreen() {
  updateCPMWindow(); // ticks once per second, independent of the 250ms display throttle below

  if (millis() - lastCollectDraw > 250) {
    lastCollectDraw = millis();

    noInterrupts();
    uint16_t idx = poolBitIndex;
    uint8_t byteSnapshot = (idx > 0) ? entropyPool[(idx - 1) >> 3] : 0;
    interrupts();

    char lastBitChar = '-';
    if (idx > 0) {
      lastBitChar = ((byteSnapshot >> ((idx - 1) & 0x07)) & 1) ? '1' : '0';
    }

    uint32_t cpm = getCurrentCPM();
    char cpmStr[6];
    formatCPM(cpm, cpmStr);

    char lineBuf[OLED_COLS + 1];
    snprintf(lineBuf, sizeof(lineBuf), "LB: %c CPM: %-5.5s", lastBitChar, cpmStr);
    lcd.drawString(0, 0, lineBuf);

    buildProgressBar(idx, RAW_POOL_BITS, lineBuf);
    lcd.drawString(0, 1, lineBuf);

    snprintf(lineBuf, sizeof(lineBuf), "Bits: %u/%u   ", idx, RAW_POOL_BITS);
    lcd.drawString(0, 2, lineBuf);

    // ETA assumes accepted-bit rate roughly tracks pulse rate (CPM), which
    // holds since almost every valid pulse interval yields a bit - see
    // geigerISR(). Shown as "--" until the CPM window has any data yet,
    // or if CPM is genuinely zero (no pulses -> no progress possible).
    char etaLine[OLED_COLS + 1];
    if (cpm == 0) {
      snprintf(etaLine, sizeof(etaLine), "Est: %-10s", "--");
    } else {
      uint16_t remainingBits = RAW_POOL_BITS - idx;
      uint32_t etaSeconds = ((uint32_t)remainingBits * 60UL) / cpm;
      char durBuf[12];
      formatDuration(etaSeconds, durBuf, sizeof(durBuf));
      snprintf(etaLine, sizeof(etaLine), "Est: %-10s", durBuf);
    }
    lcd.drawString(0, 3, etaLine);
  }
}

void drawMenuScreen() {
  lcd.clear();
  lcd.drawString(0, 0, "Seed length:");
  char lineBuf[OLED_COLS + 1];
  snprintf(lineBuf, sizeof(lineBuf), "%u", selectedLength);
  lcd.drawString(0, 2, lineBuf);
}

// Shows up to WORDS_PER_PAGE words per screen, each prefixed with its
// 1-based position in the phrase (e.g. "01 ABANDON") so the current
// page/position is always legible without a separate header line.
// Words are uppercased for display only (the wordlist itself, and any
// index lookups against it, stay lowercase/canonical BIP39) since the
// 0.91" panel's small font makes lowercase ascenders/descenders easy to
// misread when transcribing a seed phrase by hand.
void drawWordScreen() {
  lcd.clear();
  char wordBuf[BIP39_MAX_WORD_LEN + 1];
  char lineBuf[OLED_COLS + 1];

  for (uint8_t row = 0; row < WORDS_PER_PAGE; row++) {
    uint8_t pos = currentPage * WORDS_PER_PAGE + row;
    if (pos >= wordCount) break;

    getWordAtIndex(wordIndices[pos], wordBuf, sizeof(wordBuf));
    for (uint8_t i = 0; wordBuf[i] != '\0'; i++) {
      wordBuf[i] = toupper((unsigned char)wordBuf[i]);
    }
    snprintf(lineBuf, sizeof(lineBuf), "%2u %s", pos + 1, wordBuf);
    lcd.drawString(0, row, lineBuf);
  }
}

void drawDoneScreen() {
  lcd.clear();
  lcd.drawString(0, 0, "Seed complete.");
  lcd.drawString(0, 2, "BACK+FWD=wipe");
}

void drawWipeConfirmScreen() {
  lcd.clear();
  lcd.drawString(0, 0, "Wipe seed now?");
  lcd.drawString(0, 2, "BACK+FWD=confirm");
}

// ---------------- Entropy conditioning + BIP39 generation ----------------
void generatePhrase() {
  // Snapshot the pool with interrupts disabled so the ISR can't
  // modify it mid-copy.
  noInterrupts();
  uint8_t poolCopy[RAW_POOL_BITS / 8];
  memcpy(poolCopy, (const void*)entropyPool, sizeof(poolCopy));
  memset((void*)entropyPool, 0, sizeof(entropyPool));
  interrupts();

  // Step 1: whiten/condition the raw pool via SHA-256.
  SHA256 sha;
  sha.update(poolCopy, sizeof(poolCopy));
  uint8_t conditioned[32];
  sha.finalize(conditioned);

  uint8_t entropyLenBytes = (selectedLength == 24) ? 32 : 16; // 256 or 128 bits
  uint8_t entropyBytes[32];
  memcpy(entropyBytes, conditioned, entropyLenBytes);

  // Step 2: BIP39 checksum = first (ENT/32) bits of SHA256(entropy).
  SHA256 sha2;
  sha2.update(entropyBytes, entropyLenBytes);
  uint8_t checksumHash[32];
  sha2.finalize(checksumHash);

  uint8_t checksumBits = (entropyLenBytes * 8) / 32; // 4 bits (12w) or 8 bits (24w)

  // Step 3: concat entropy bits + checksum bits, split into 11-bit
  // word indices per the BIP39 spec.
  wordCount = (entropyLenBytes * 8 + checksumBits) / 11;

  for (uint8_t w = 0; w < wordCount; w++) {
    uint16_t idx = 0;
    for (uint8_t b = 0; b < 11; b++) {
      uint16_t bitPos = (uint16_t)w * 11 + b;
      bool bitVal;
      if (bitPos < (uint16_t)entropyLenBytes * 8) {
        bitVal = (entropyBytes[bitPos / 8] >> (7 - (bitPos % 8))) & 1;
      } else {
        uint16_t cbit = bitPos - (uint16_t)entropyLenBytes * 8;
        bitVal = (checksumHash[cbit / 8] >> (7 - (cbit % 8))) & 1;
      }
      idx = (idx << 1) | (bitVal ? 1 : 0);
    }
    wordIndices[w] = idx;
  }

  // Wipe sensitive buffers we no longer need in RAM.
  memset(entropyBytes, 0, sizeof(entropyBytes));
  memset(checksumHash, 0, sizeof(checksumHash));
  memset(poolCopy, 0, sizeof(poolCopy));
  memset(conditioned, 0, sizeof(conditioned));
}

// Clears the generated seed and its supporting state from RAM once the
// user has explicitly confirmed they're done copying it down. Restarts
// entropy collection immediately so the device is ready to generate a
// fresh seed rather than sitting in a dead-end wiped state.
void wipeSeed() {
  memset((void*)wordIndices, 0, sizeof(wordIndices));
  wordCount      = 0;
  currentPage    = 0;
  poolBitIndex   = 0;

  cpmLastTickMs = millis();
  cpmLastPulseSnapshot = totalPulseCount;
}
