#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "config.h"

// State shared between the main loop (the only code touching the I2C bus) and the web
// handlers (network task). Every access goes through a Lock.
struct Lock {
  Lock();
  ~Lock();
};

enum CardKind : uint8_t { K_NONE, K_CLASSIC, K_T2, K_OTHER };

struct CardInfo {
  uint8_t uid[10];
  uint8_t uidLen = 0;
  uint8_t sak = 0;
  uint16_t atqa = 0;
  CardKind kind = K_NONE;
  int sectors = 0;   // MIFARE Classic
  int t2Pages = 0;   // Ultralight / NTAG: readable pages
  int t2Cap = 0;     // NDEF capacity in bytes, from the capability container
  String type;
};

enum JobKind : uint8_t { J_READ, J_NDEF, J_RAW, J_ERASE };
enum JobState : uint8_t { S_WAITING, S_RUNNING, S_DONE, S_ERROR, S_CANCELLED, S_TIMEOUT };

struct Job {
  uint32_t id = 0;
  JobKind kind = J_READ;
  JobState state = S_WAITING;
  uint32_t created = 0, deadline = 0;
  uint8_t key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  std::vector<uint8_t> data;   // NDEF: message TLV; raw: bytes to write
  int block = -1;
  String code;                 // machine-readable outcome, e.g. "auth_failed"
  String message;              // human-readable outcome, in the device language
  String uid;                  // badge the job ran on
  String result;               // JSON (read jobs)
  bool active() const { return state == S_WAITING || state == S_RUNNING; }
};

struct Scan {
  String uid, type;
  uint32_t uptime;
  time_t at;
};

struct Shared {
  bool readerOk = false;
  String readerFw;
  bool displayOk = false;

  bool present = false;
  CardInfo card;

  std::vector<Job> jobs;       // the last JOBS_KEPT
  uint32_t nextJobId = 1;
  std::vector<Scan> history;   // newest last

  String pairCode;
  uint32_t pairUntil = 0;
  uint8_t pairFails = 0;

  String msg;                  // message shown on request of an app
  uint32_t msgUntil = 0;

  bool flashOk = false;        // job outcome shown for a few seconds
  String flashText;
  uint32_t flashUntil = 0;

  uint32_t lastActivity = 0;   // for the screen saver
  uint32_t resetHeldSince = 0; // BOOT button held, 0 when released
  bool updating = false;

  // Settings (NVS)
  String name = DEFAULT_NAME;
  String hostname = DEFAULT_HOSTNAME;
  String lang = DEFAULT_LANG;
  uint8_t contrast = 160;
  String apPassword;

  bool apMode = false;
  String apName;
  String ip;
};

extern Shared S;

Job *activeJob();
Job *findJob(uint32_t id);

String hexStr(const uint8_t *b, size_t n, char sep = 0);
int parseHex(String s, uint8_t *out, size_t maxLen);
time_t nowEpoch();  // 0 until the clock is synchronized

const char *jobKindName(JobKind k);
const char *jobStateName(JobState s);
const char *cardKindName(CardKind k);
void cardToJson(JsonObject o, const CardInfo &c);
void jobToJson(JsonObject o, const Job &j, bool withResult);
