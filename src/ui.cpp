#include "ui.h"
#include <Wire.h>
#include <WiFi.h>
#include <U8g2lib.h>
#include "config.h"
#include "state.h"
#include "i18n.h"

#ifdef DISPLAY_SH1106
static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#endif

static bool ok = false;
static String lastSig;
static uint32_t lastDraw = 0;
static int contrastSet = -1;

static const uint8_t *F_SMALL = u8g2_font_6x12_tf;
static const uint8_t *F_BOLD  = u8g2_font_helvB10_tf;
static const uint8_t *F_DIGIT = u8g2_font_logisoso22_tn;

void uiBegin() {
  Wire.beginTransmission(OLED_ADDR);
  ok = Wire.endTransmission() == 0;
  { Lock l; S.displayOk = ok; }
  if (!ok) { Serial.printf("No display found at 0x%02X\n", OLED_ADDR); return; }
  oled.setBusClock(I2C_FREQ);
  oled.begin();
  oled.enableUTF8Print();
}

// --- Drawing -------------------------------------------------------------------

static void centered(int y, const String &s, const uint8_t *font) {
  oled.setFont(font);
  int w = oled.getUTF8Width(s.c_str());
  oled.drawUTF8(max(0, (128 - w) / 2), y, s.c_str());
}

// Word-wraps text to the screen width, at most maxLines lines.
static void wrapped(int y, const String &text, const uint8_t *font, int lineH, int maxLines) {
  oled.setFont(font);
  String line, word;
  int lines = 0;
  auto flush = [&]() {
    if (lines < maxLines) centered(y + lines * lineH, line, font);
    lines++;
    line = "";
  };
  for (size_t i = 0; i <= text.length(); i++) {
    char c = i < text.length() ? text[i] : ' ';
    if (c != ' ' && c != '\n') { word += c; continue; }
    String cand = line.length() ? line + " " + word : word;
    if (oled.getUTF8Width(cand.c_str()) > 124 && line.length()) { flush(); cand = word; }
    line = cand;
    word = "";
    if (c == '\n') flush();
  }
  if (line.length()) flush();
}

static void header(const String &title) {
  oled.setFont(F_SMALL);
  oled.drawUTF8(0, 9, title.c_str());
  oled.drawHLine(0, 12, 128);
}

static void wifiBars(int x, int y, int rssi) {
  int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    if (i < bars) oled.drawBox(x + i * 3, y - h, 2, h);
    else oled.drawPixel(x + i * 3, y - 1);
  }
}

static void checkIcon(int cx, int cy) {
  for (int d = -1; d <= 1; d++) {
    oled.drawLine(cx - 9, cy + d, cx - 3, cy + 6 + d);
    oled.drawLine(cx - 3, cy + 6 + d, cx + 9, cy - 6 + d);
  }
}

static void crossIcon(int cx, int cy) {
  for (int d = -1; d <= 1; d++) {
    oled.drawLine(cx - 7 + d, cy - 7, cx + 7 + d, cy + 7);
    oled.drawLine(cx + 7 + d, cy - 7, cx - 7 + d, cy + 7);
  }
}

static const char *jobLabel(JobKind k) {
  switch (k) { case J_NDEF: return tr(T_JOB_NDEF); case J_RAW: return tr(T_JOB_RAW); case J_ERASE: return tr(T_JOB_ERASE); default: return tr(T_JOB_READ); }
}

void uiBoot(const String &line) {
  if (!ok) return;
  oled.clearBuffer();
  centered(26, "RFID Terminal", F_BOLD);
  centered(40, "v" FW_VERSION, F_SMALL);
  centered(60, line, F_SMALL);
  oled.sendBuffer();
}

// --- Screens -------------------------------------------------------------------

void uiTick() {
  if (!ok) return;
  uint32_t now = millis();
  if (now - lastDraw < 120) return;

  // Copy the state under the lock, draw without it
  enum { UPDATING, RESET, PAIR, MSG, FLASH, WAIT, NOREADER, CARD, SETUP, IDLE } screen;
  String a, b, c;
  int n1 = 0, n2 = 0;
  bool flashOk = false;
  uint8_t contrast;
  {
    Lock l;
    contrast = S.contrast;
    Job *j = activeJob();
    if (S.updating) {
      screen = UPDATING;
    } else if (S.resetHeldSince) {
      screen = RESET; n1 = max(0, (int)(RESET_HOLD_MS - (now - S.resetHeldSince) + 999) / 1000);
    } else if (S.pairCode.length() && (int32_t)(now - S.pairUntil) < 0) {
      screen = PAIR; a = S.pairCode; n1 = (S.pairUntil - now) / 1000;
    } else if (S.msg.length() && (int32_t)(now - S.msgUntil) < 0) {
      screen = MSG; a = S.msg;
    } else if ((int32_t)(now - S.flashUntil) < 0) {
      screen = FLASH; flashOk = S.flashOk; a = S.flashText;
    } else if (j) {
      screen = WAIT; a = jobLabel(j->kind); n1 = max((int32_t)0, (int32_t)(j->deadline - now) / 1000);
      n2 = j->state == S_RUNNING;
    } else if (!S.readerOk) {
      screen = NOREADER;
    } else if (S.present) {
      screen = CARD; a = S.card.type; b = hexStr(S.card.uid, S.card.uidLen, S.card.uidLen > 7 ? 0 : ':');
      if (S.card.kind == K_T2 && S.card.t2Cap) c = trf(T_NDEF_BYTES, S.card.t2Cap);
      else if (S.card.kind == K_OTHER) c = tr(T_UNSUPPORTED);
    } else if (S.apMode) {
      screen = SETUP; a = S.apName; b = S.ip; c = S.apPassword;
    } else {
      screen = IDLE; a = S.name; b = S.ip;
      n1 = (now - S.lastActivity) / 1000;
    }
  }

  // Screen saver: after 5 idle minutes, dim and slowly shift the text (OLED burn-in)
  bool saver = screen == IDLE && n1 > 300;
  int wantContrast = saver ? 1 : contrast;
  if (wantContrast != contrastSet) { oled.setContrast(wantContrast); contrastSet = wantContrast; }
  int shift = saver ? (now / 60000) % 5 : 0;

  int rssi = WiFi.RSSI();
  String sig = String(screen) + a + b + c + String(screen == IDLE ? shift : n1) + n2 + flashOk + (rssi / 10);
  if (sig == lastSig) return;
  lastSig = sig;
  lastDraw = now;

  oled.clearBuffer();
  switch (screen) {
    case UPDATING:
      centered(36, tr(T_UPDATING), F_BOLD);
      break;
    case RESET:
      crossIcon(64, 18);
      centered(48, n1 ? trf(T_RESET_HOLD, n1) : String(tr(T_RESETTING)), F_SMALL);
      break;
    case PAIR:
      header(tr(T_PAIRING));
      centered(28, tr(T_ENTER_CODE), F_SMALL);
      centered(56, a, F_DIGIT);
      oled.setFont(F_SMALL);
      oled.drawUTF8(110, 9, (String(n1) + "s").c_str());
      break;
    case MSG:
      wrapped(14, a, F_BOLD, 15, 4);
      break;
    case FLASH:
      flashOk ? checkIcon(64, 16) : crossIcon(64, 16);
      wrapped(40, a, F_SMALL, 12, 2);
      break;
    case WAIT:
      header(a);
      centered(36, n2 ? tr(T_HOLD_STILL) : tr(T_PLACE_BADGE), F_BOLD);
      if (!n2) {
        oled.drawFrame(14, 50, 100, 6);
        oled.drawBox(14, 50, 100 * n1 / (JOB_TIMEOUT_MS / 1000), 6);
      }
      break;
    case NOREADER:
      crossIcon(64, 18);
      centered(42, tr(T_NO_READER), F_SMALL);
      centered(56, tr(T_CHECK_WIRING), F_SMALL);
      break;
    case CARD:
      header(tr(T_BADGE_DETECTED));
      centered(32, a, F_BOLD);
      centered(47, b, F_SMALL);
      centered(61, c, F_SMALL);
      break;
    case SETUP:
      header(tr(T_WIFI_SETUP));
      oled.setFont(F_SMALL);
      oled.drawUTF8(0, 28, (String(tr(T_NETWORK)) + a).c_str());
      oled.drawUTF8(0, 42, (String(tr(T_PASSWORD)) + c).c_str());
      oled.drawUTF8(0, 58, ("http://" + b).c_str());
      break;
    case IDLE:
      if (saver) {
        oled.setFont(F_SMALL);
        oled.drawUTF8(shift * 8, 30 + shift * 5, (String(tr(T_IDLE_1)) + " " + tr(T_IDLE_2)).c_str());
        break;
      }
      header(a);
      wifiBars(115, 10, rssi);
      centered(36, tr(T_IDLE_1), F_BOLD);
      centered(50, tr(T_IDLE_2), F_BOLD);
      centered(63, b, F_SMALL);
      break;
  }
  oled.sendBuffer();
}
