#pragma once
#include <Arduino.h>
#include <Wire.h>

// Minimal PN532 driver over I2C. The caller initializes the bus (shared with the display).
// With the IRQ pin wired, waiting for a badge generates no bus traffic at all.
class PN532 {
public:
  bool begin(TwoWire &w, int irq);
  uint32_t firmware();                       // 0 when silent, else IC|Ver|Rev|Support

  // Asynchronous listening: startListen(), then poll ready() until a badge answers.
  bool startListen();
  bool ready();
  uint8_t listenResult(uint8_t *uid, uint8_t &sak, uint16_t &atqa);  // UID length, 0 on failure
  void abort();                              // cancels the pending command

  uint8_t detect(uint8_t *uid, uint8_t &sak, uint16_t &atqa);        // blocking, ~60 ms max
  bool field(bool on);

  // MIFARE Classic
  bool mfAuth(uint8_t block, const uint8_t key[6], const uint8_t *uid, uint8_t uidLen);
  bool mfWrite(uint8_t block, const uint8_t data[16]);
  // Reads 16 bytes (one Classic block, or 4 Ultralight/NTAG pages)
  bool read16(uint8_t block, uint8_t out[16]);
  // Ultralight / NTAG
  bool ulWrite(uint8_t page, const uint8_t data[4]);

private:
  TwoWire *_w = nullptr;
  int _irq = -1;
  uint32_t _lastStatus = 0;
  bool writeFrame(const uint8_t *d, uint8_t n);
  bool statusReady();
  bool waitReady(uint16_t timeoutMs);
  bool readAck();
  int  readResponse(uint8_t cmd, uint8_t *out, uint8_t maxOut);
  int  command(const uint8_t *d, uint8_t n, uint8_t *out, uint8_t maxOut, uint16_t timeoutMs);
  int  exchange(const uint8_t *d, uint8_t n, uint8_t *out, uint8_t maxOut);
  static uint8_t parseTarget(const uint8_t *r, int n, uint8_t *uid, uint8_t &sak, uint16_t &atqa);
};
