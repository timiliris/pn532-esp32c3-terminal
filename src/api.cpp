#include "api.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <nvs_flash.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "state.h"
#include "i18n.h"
#include "nfc.h"
#include "web_app.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/api/v1/events");
static uint32_t rebootAt = 0;
static uint32_t resetAt = 0;    // factory reset requested through the API

// --- Tokens ----------------------------------------------------------------------
// Only the SHA-256 of each token is stored in flash.

struct Token { String id, name, hash; time_t created; };
static std::vector<Token> tokens;

static String sha256Hex(const String &s) {
  uint8_t out[32];
  mbedtls_sha256((const unsigned char *)s.c_str(), s.length(), out, 0);
  return hexStr(out, 32);
}

static String randomHex(size_t bytes) {
  uint8_t b[32];
  esp_fill_random(b, bytes);
  return hexStr(b, bytes);
}

static void loadTokens() {
  Preferences p;
  p.begin("auth", true);
  String raw = p.getString("tokens", "[]");
  p.end();
  JsonDocument d;
  if (deserializeJson(d, raw)) return;
  tokens.clear();
  for (JsonObject t : d.as<JsonArray>()) {
    tokens.push_back({t["id"] | "", t["name"] | "", t["h"] | "", t["c"] | (time_t)0});
  }
}

static void saveTokens() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (auto &t : tokens) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = t.id; o["name"] = t.name; o["h"] = t.hash; o["c"] = t.created;
  }
  String raw;
  serializeJson(d, raw);
  Preferences p;
  p.begin("auth", false);
  p.putString("tokens", raw);
  p.end();
}

static bool authorized(AsyncWebServerRequest *r) {
  String tok;
  if (r->hasHeader("Authorization")) {
    tok = r->header("Authorization");
    if (!tok.startsWith("Bearer ")) return false;
    tok = tok.substring(7);
  } else if (r->hasParam("token")) {
    tok = r->getParam("token")->value();  // WebSocket: browsers cannot set headers
  }
  tok.trim();
  if (tok.length() != 64) return false;
  String h = sha256Hex(tok);
  Lock l;
  for (auto &t : tokens) if (t.hash == h) return true;
  return false;
}

// --- Responses ---------------------------------------------------------------------

static void sendJson(AsyncWebServerRequest *r, int code, JsonDocument &d) {
  String out;
  serializeJson(d, out);
  r->send(code, "application/json", out);
}

static void sendError(AsyncWebServerRequest *r, int code, const char *err, const String &msg) {
  JsonDocument d;
  d["error"]["code"] = err;
  d["error"]["message"] = msg;
  sendJson(r, code, d);
}

static void onBody(AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
  if (total > 4096) { r->_tempObject = nullptr; return; }
  if (index == 0) r->_tempObject = calloc(total + 1, 1);
  if (r->_tempObject) memcpy((uint8_t *)r->_tempObject + index, data, len);
}

using Handler = std::function<void(AsyncWebServerRequest *, JsonDocument &)>;

static void route(const char *path, WebRequestMethodComposite method, bool auth, Handler h) {
  server.on(path, method, [auth, h](AsyncWebServerRequest *r) {
    if (auth && !authorized(r)) return sendError(r, 401, "unauthorized", "Missing or invalid token: pair the app first");
    if (r->contentLength() > 4096) return sendError(r, 413, "too_large", "Request body is limited to 4 KB");
    // A non-JSON body (form) is consumed by the server and never reaches onBody.
    if (r->contentLength() && !r->_tempObject) return sendError(r, 415, "bad_content_type", "Content-Type: application/json expected");
    JsonDocument body;
    if (r->_tempObject && deserializeJson(body, (const char *)r->_tempObject)) {
      return sendError(r, 400, "bad_json", "Invalid JSON body");
    }
    h(r, body);
  }, nullptr, onBody);
}

// Trailing id of a URL such as /api/v1/jobs/12
static String tail(AsyncWebServerRequest *r, const char *base) {
  String u = r->url();
  return u.length() > strlen(base) + 1 ? u.substring(strlen(base) + 1) : String();
}

// --- Badge jobs --------------------------------------------------------------------

static bool parseKey(JsonDocument &b, uint8_t key[6], AsyncWebServerRequest *r) {
  String k = b["key"] | "";
  if (!k.length()) return true;
  if (parseHex(k, key, 6) != 6) { sendError(r, 400, "bad_key", "key: 12 hexadecimal characters"); return false; }
  return true;
}

static void startJob(AsyncWebServerRequest *r, Job j) {
  JsonDocument d;
  {
    Lock l;
    if (!S.readerOk) return sendError(r, 503, "reader_offline", "The PN532 reader is not responding");
    if (Job *a = activeJob()) return sendError(r, 409, "busy", "Job " + String(a->id) + " is still in progress");
    j.id = S.nextJobId++;
    j.state = S_WAITING;
    j.created = millis();
    j.deadline = j.created + JOB_TIMEOUT_MS;
    S.jobs.push_back(j);
    if (S.jobs.size() > JOBS_KEPT) S.jobs.erase(S.jobs.begin());
    S.lastActivity = millis();
    jobToJson(d.to<JsonObject>(), j, false);
  }
  emitEvent("job.updated", d);
  sendJson(r, 202, d);
}

// --- Routes ------------------------------------------------------------------------

static void infoJson(JsonObject o, bool full) {
  Lock l;
  o["name"] = S.name;
  o["firmware"] = FW_VERSION;
  o["reader"]["ok"] = S.readerOk;
  o["reader"]["firmware"] = S.readerFw;
  o["display"]["ok"] = S.displayOk;
  if (!full) return;
  o["network"]["mode"] = S.apMode ? "ap" : "sta";
  o["network"]["ssid"] = S.apMode ? S.apName : WiFi.SSID();
  o["network"]["ip"] = S.ip;
  o["network"]["hostname"] = S.hostname;
  if (!S.apMode) o["network"]["rssi"] = WiFi.RSSI();
  o["uptime_s"] = millis() / 1000;
  o["heap_free"] = ESP.getFreeHeap();
  o["time"] = nowEpoch();
}

static void configJson(JsonObject o) {
  Lock l;
  o["name"] = S.name;
  o["hostname"] = S.hostname;
  o["lang"] = S.lang;
  o["contrast"] = S.contrast;
}

static bool validHostname(const String &h) {
  if (!h.length() || h.length() > 24 || h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (char c : h) if (!isdigit(c) && !(c >= 'a' && c <= 'z') && c != '-') return false;
  return true;
}

static void registerRoutes() {
  // Public: enough to recognize the terminal before pairing
  route("/api/v1/info", HTTP_GET, false, [](AsyncWebServerRequest *r, JsonDocument &) {
    JsonDocument d;
    bool paired = authorized(r);
    infoJson(d.to<JsonObject>(), paired);
    d["paired"] = paired;
    sendJson(r, 200, d);
  });

  // Pairing: the code only shows on the terminal's display, which proves physical access
  route("/api/v1/pair/start", HTTP_POST, false, [](AsyncWebServerRequest *r, JsonDocument &) {
    JsonDocument d;
    {
      Lock l;
      char code[7];
      snprintf(code, sizeof code, "%06u", (unsigned)(esp_random() % 1000000));
      S.pairCode = code;
      S.pairUntil = millis() + PAIR_TIMEOUT_MS;
      S.pairFails = 0;
      S.lastActivity = millis();
      Serial.printf("Pairing code: %s\n", code);  // a USB link is physical access too
    }
    d["expires_in_s"] = PAIR_TIMEOUT_MS / 1000;
    sendJson(r, 200, d);
  });

  route("/api/v1/pair/confirm", HTTP_POST, false, [](AsyncWebServerRequest *r, JsonDocument &b) {
    String code = b["code"] | "";
    String name = b["name"] | "App";
    name = name.substring(0, 32);
    String tok;
    Token t;
    {
      Lock l;
      if (!S.pairCode.length() || (int32_t)(millis() - S.pairUntil) > 0) {
        S.pairCode = "";
        return sendError(r, 410, "pairing_expired", "No pairing in progress: call pair/start again");
      }
      if (code != S.pairCode) {
        if (++S.pairFails >= 5) S.pairCode = "";
        return sendError(r, 403, "bad_code", "Wrong code");
      }
      S.pairCode = "";
      tok = randomHex(32);
      t = {randomHex(4), name, sha256Hex(tok), nowEpoch()};
      tokens.push_back(t);
      if (tokens.size() > MAX_TOKENS) tokens.erase(tokens.begin());
      saveTokens();
      S.flashOk = true;
      S.flashText = trf(T_PAIRED, name.c_str());
      S.flashUntil = millis() + 2500;
    }
    JsonDocument d;
    d["token"] = tok;
    d["id"] = t.id;
    d["name"] = t.name;
    sendJson(r, 201, d);
  });

  route("/api/v1/tokens", HTTP_GET | HTTP_DELETE, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    if (r->method() == HTTP_DELETE) {
      String id = tail(r, "/api/v1/tokens");
      Lock l;
      for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i].id == id) {
          tokens.erase(tokens.begin() + i);
          saveTokens();
          return r->send(204);
        }
      }
      return sendError(r, 404, "not_found", "Unknown token");
    }
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    Lock l;
    for (auto &t : tokens) {
      JsonObject o = a.add<JsonObject>();
      o["id"] = t.id; o["name"] = t.name; o["created"] = t.created;
    }
    sendJson(r, 200, d);
  });

  route("/api/v1/card", HTTP_GET, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    JsonDocument d;
    {
      Lock l;
      if (!S.present) return sendError(r, 404, "no_card", "No badge on the reader");
      cardToJson(d.to<JsonObject>(), S.card);
    }
    sendJson(r, 200, d);
  });

  route("/api/v1/read", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    Job j;
    j.kind = J_READ;
    if (!parseKey(b, j.key, r)) return;
    startJob(r, j);
  });

  route("/api/v1/write/ndef", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    Job j;
    j.kind = J_NDEF;
    String lang;
    { Lock l; lang = S.lang; }
    String err = buildNdef(b["records"].as<JsonArrayConst>(), j.data, lang);
    if (err.length()) return sendError(r, 400, "bad_records", err);
    startJob(r, j);
  });

  route("/api/v1/write/raw", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    Job j;
    j.kind = J_RAW;
    if (!b["block"].is<int>()) return sendError(r, 400, "bad_block", "block: block number (Classic) or page number (NTAG)");
    j.block = b["block"];
    if (!parseKey(b, j.key, r)) return;
    if (b["hex"].is<const char *>()) {
      uint8_t buf[16];
      int n = parseHex(b["hex"].as<String>(), buf, 16);
      if (n <= 0) return sendError(r, 400, "bad_data", "hex: 1 to 16 bytes in hexadecimal");
      j.data.assign(buf, buf + n);
    } else if (b["text"].is<const char *>()) {
      String t = b["text"];
      if (!t.length() || t.length() > 16) return sendError(r, 400, "bad_data", "text: 1 to 16 bytes");
      j.data.assign(t.c_str(), t.c_str() + t.length());
    } else {
      return sendError(r, 400, "bad_data", "Provide hex or text");
    }
    startJob(r, j);
  });

  route("/api/v1/erase", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    Job j;
    j.kind = J_ERASE;
    if (!parseKey(b, j.key, r)) return;
    startJob(r, j);
  });

  route("/api/v1/jobs", HTTP_GET | HTTP_DELETE, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    String id = tail(r, "/api/v1/jobs");
    JsonDocument d;
    if (!id.length()) {
      if (r->method() == HTTP_DELETE) return sendError(r, 405, "method_not_allowed", "Give a job id");
      JsonArray a = d.to<JsonArray>();
      Lock l;
      for (int i = S.jobs.size() - 1; i >= 0; i--) jobToJson(a.add<JsonObject>(), S.jobs[i], false);
      return sendJson(r, 200, d);
    }
    JsonDocument event;
    {
      Lock l;
      Job *j = findJob(id.toInt());
      if (!j) return sendError(r, 404, "not_found", "Unknown job");
      if (r->method() == HTTP_DELETE) {
        if (j->state != S_WAITING) return sendError(r, 409, "not_cancellable", "The job is no longer waiting for a badge");
        j->state = S_CANCELLED;
        j->code = "cancelled";
        j->message = tr(M_CANCELLED);
        jobToJson(event.to<JsonObject>(), *j, false);
      }
      jobToJson(d.to<JsonObject>(), *j, true);
    }
    if (!event.isNull()) emitEvent("job.updated", event);
    sendJson(r, 200, d);
  });

  route("/api/v1/history", HTTP_GET, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    Lock l;
    for (int i = S.history.size() - 1; i >= 0; i--) {
      JsonObject o = a.add<JsonObject>();
      o["uid"] = S.history[i].uid;
      o["type"] = S.history[i].type;
      o["seconds_ago"] = (millis() - S.history[i].uptime) / 1000;
      if (S.history[i].at) o["at"] = S.history[i].at;
    }
    sendJson(r, 200, d);
  });

  route("/api/v1/display", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    String text = b["text"] | "";
    int seconds = constrain((int)(b["seconds"] | 10), 0, 3600);
    {
      Lock l;
      S.msg = text.substring(0, 120);
      S.msgUntil = millis() + seconds * 1000;
      S.lastActivity = millis();
    }
    r->send(204);
  });

  route("/api/v1/config", HTTP_GET | HTTP_PUT, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    if (r->method() == HTTP_PUT) {
      // Validate everything first, so a bad field changes nothing
      String name = b["name"] | "", host = b["hostname"] | "", lang = b["lang"] | "";
      name.trim();
      if (b["name"].is<const char *>() && (!name.length() || name.length() > 24)) return sendError(r, 400, "bad_name", "name: 1 to 24 characters");
      if (b["hostname"].is<const char *>() && !validHostname(host)) return sendError(r, 400, "bad_hostname", "hostname: 1 to 24 of a-z, 0-9 and -");
      if (b["lang"].is<const char *>() && lang != "en" && lang != "fr") return sendError(r, 400, "bad_lang", "lang: \"en\" or \"fr\"");
      Lock l;
      Preferences p;
      p.begin("cfg", false);
      if (b["name"].is<const char *>()) { S.name = name; p.putString("name", name); }
      if (b["hostname"].is<const char *>()) { S.hostname = host; p.putString("hostname", host); }  // applied on reboot
      if (b["lang"].is<const char *>()) { S.lang = lang; p.putString("lang", lang); setLang(lang); }
      if (b["contrast"].is<int>()) { S.contrast = constrain(b["contrast"].as<int>(), 0, 255); p.putUChar("contrast", S.contrast); }
      p.end();
    }
    JsonDocument d;
    configJson(d.to<JsonObject>());
    sendJson(r, 200, d);
  });

  route("/api/v1/wifi", HTTP_PUT, true, [](AsyncWebServerRequest *r, JsonDocument &b) {
    String ssid = b["ssid"] | "";
    if (!ssid.length()) return sendError(r, 400, "bad_ssid", "ssid is required");
    Preferences p;
    p.begin("wifi", false);
    p.putString("ssid", ssid);
    p.putString("pass", b["password"] | "");
    p.end();
    rebootAt = millis() + 1000;
    JsonDocument d;
    d["rebooting"] = true;
    sendJson(r, 202, d);
  });

  route("/api/v1/wifi/scan", HTTP_GET, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    int n = WiFi.scanComplete();
    JsonDocument d;
    if (n < 0) {
      if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
      d["scanning"] = true;
      return sendJson(r, 202, d);
    }
    JsonArray a = d["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    sendJson(r, 200, d);
  });

  route("/api/v1/reboot", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    rebootAt = millis() + 500;
    r->send(202);
  });

  route("/api/v1/factory-reset", HTTP_POST, true, [](AsyncWebServerRequest *r, JsonDocument &) {
    resetAt = millis() + 1000;  // from the main loop, once the response has gone out
    r->send(202);
  });

  // Over-the-air update: multipart upload of firmware.bin (curl -F firmware=@firmware.bin)
  server.on("/api/v1/update", HTTP_POST,
    [](AsyncWebServerRequest *r) {
      if (!authorized(r)) return sendError(r, 401, "unauthorized", "Missing or invalid token: pair the app first");
      if (Update.hasError() || !Update.isFinished()) {
        Update.abort();
        { Lock l; S.updating = false; }
        return sendError(r, 400, "update_failed", String("Update failed: ") + Update.errorString());
      }
      JsonDocument d;
      d["rebooting"] = true;
      sendJson(r, 200, d);
      rebootAt = millis() + 1000;
    },
    [](AsyncWebServerRequest *r, const String &, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        if (!authorized(r)) return;
        { Lock l; S.updating = true; }
        Serial.println("Firmware update started");
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) return;
      }
      if (!Update.isRunning()) return;
      if (Update.write(data, len) != len) return;
      if (final && Update.end(true)) Serial.printf("Firmware update done (%u bytes)\n", (unsigned)(index + len));
    });

  // Web app: served by the terminal, a client of the API like any other
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html; charset=utf-8", (const uint8_t *)WEB_APP, strlen(WEB_APP));
  });

  server.onNotFound([](AsyncWebServerRequest *r) {
    if (r->method() == HTTP_OPTIONS) return r->send(204);  // CORS preflight
    if (r->url().startsWith("/api/")) return sendError(r, 404, "not_found", "Unknown route");
    r->redirect("/");
  });
}

// --- WebSocket ---------------------------------------------------------------------

static void onWs(AsyncWebSocket *, AsyncWebSocketClient *c, AwsEventType t, void *, uint8_t *, size_t) {
  if (t != WS_EVT_CONNECT) return;
  // Full state on connect: the client does not need to poll the API afterwards
  JsonDocument d;
  d["event"] = "hello";
  JsonObject data = d["data"].to<JsonObject>();
  infoJson(data["info"].to<JsonObject>(), true);
  {
    Lock l;
    if (S.present) cardToJson(data["card"].to<JsonObject>(), S.card);
    if (Job *j = activeJob()) jobToJson(data["job"].to<JsonObject>(), *j, false);
  }
  String out;
  serializeJson(d, out);
  c->text(out);
}

void emitEvent(const char *type, JsonDocument &data) {
  if (!ws.count()) return;
  JsonDocument d;
  d["event"] = type;
  d["data"] = data;
  d["t"] = nowEpoch();
  String out;
  serializeJson(d, out);
  ws.textAll(out);
}

void factoryReset() {
  Serial.println("Factory reset");
  { Lock l; S.resetHeldSince = 1; }
  delay(500);
  nvs_flash_erase();
  nvs_flash_init();
  ESP.restart();
}

void apiBegin() {
  loadTokens();
  ws.handleHandshake([](AsyncWebServerRequest *r) { return authorized(r); });
  ws.onEvent(onWs);
  server.addHandler(&ws);
  registerRoutes();
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.begin();
}

void apiTick() {
  static uint32_t lastClean = 0;
  if (millis() - lastClean > 2000) { lastClean = millis(); ws.cleanupClients(); }
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) ESP.restart();
  if (resetAt && (int32_t)(millis() - resetAt) > 0) factoryReset();
}
