// PN532 ESP32-C3 Terminal - a passive NFC badge reader/writer driven over a local API.
// https://github.com/timiliris/pn532-esp32c3-terminal  (MIT license)

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"
#include "state.h"
#include "i18n.h"
#include "nfc.h"
#include "ui.h"
#include "api.h"

// Each device gets its own setup-network password, shown on its display: no shared default.
static String randomPassword() {
  static const char *ALPHA = "abcdefghjkmnpqrstuvwxyz23456789";  // no look-alike characters
  String p;
  for (int i = 0; i < 10; i++) p += ALPHA[esp_random() % strlen(ALPHA)];
  return p;
}

static void loadConfig() {
  Preferences p;
  p.begin("cfg", false);
  S.name = p.getString("name", S.name);
  S.hostname = p.getString("hostname", S.hostname);
  S.lang = p.getString("lang", S.lang);
  S.contrast = p.getUChar("contrast", S.contrast);
  S.apPassword = p.getString("appass", "");
  if (S.apPassword.length() < 8) {
    S.apPassword = randomPassword();
    p.putString("appass", S.apPassword);
  }
  p.end();
  setLang(S.lang);
}

static void startWifi() {
  Preferences p;
  p.begin("wifi", true);
  String ssid = p.getString("ssid", ""), pass = p.getString("pass", "");
  p.end();

  WiFi.setHostname(S.hostname.c_str());
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // many C3 Super Mini boards drop the link at full power
    uiBoot(tr(T_WIFI) + ssid);
    Serial.printf("Connecting to %s\n", ssid.c_str());
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(100);
  }

  Lock l;
  if (WiFi.status() == WL_CONNECTED) {
    S.ip = WiFi.localIP().toString();
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    Serial.printf("Connected: http://%s  (http://%s.local)\n", S.ip.c_str(), S.hostname.c_str());
  } else {
    // Setup access point, named after the end of the MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    S.apMode = true;
    S.apName = AP_PREFIX + hexStr(mac + 4, 2);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(S.apName.c_str(), S.apPassword.c_str());
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    S.ip = WiFi.softAPIP().toString();
    Serial.printf("Setup network %s, password %s: http://%s\n", S.apName.c_str(), S.apPassword.c_str(), S.ip.c_str());
  }
}

static void startMdns() {
  MDNS.begin(S.hostname.c_str());
  MDNS.addService("http", "tcp", 80);
  // Dedicated service so apps can find terminals without knowing their address
  MDNS.addService(MDNS_SERVICE, "tcp", 80);
  MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "fw", FW_VERSION);
  MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "api", "/api/v1");
  Lock l;
  MDNS.addServiceTxt(MDNS_SERVICE, "tcp", "name", S.name);
}

// BOOT button held for RESET_HOLD_MS: factory reset (Wi-Fi, paired apps, settings).
static void checkResetButton() {
  bool held = digitalRead(PIN_BOOT_BUTTON) == LOW;
  uint32_t since;
  {
    Lock l;
    if (!held) { S.resetHeldSince = 0; return; }
    if (!S.resetHeldSince) S.resetHeldSince = millis() | 1;
    since = S.resetHeldSince;
  }
  if (millis() - since >= RESET_HOLD_MS) factoryReset();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n== PN532 ESP32-C3 Terminal v" FW_VERSION " ==");
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  loadConfig();

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ);
  uiBegin();
  uiBoot(tr(T_STARTING));
  nfcBegin();
  startWifi();
  startMdns();
  apiBegin();
  { Lock l; S.lastActivity = millis(); }
}

void loop() {
  nfcTick();
  uiTick();
  apiTick();
  checkResetButton();

  // Keep the IP up to date after a Wi-Fi reconnection
  static uint32_t lastNet = 0;
  if (millis() - lastNet > 5000) {
    lastNet = millis();
    Lock l;
    if (!S.apMode) S.ip = WiFi.isConnected() ? WiFi.localIP().toString() : tr(T_WIFI_LOST);
  }
  delay(2);
}
