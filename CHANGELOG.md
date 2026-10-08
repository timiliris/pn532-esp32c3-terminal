# Changelog

## 1.0.0

First public release.

- PN532 over I2C with IRQ-driven detection, shared bus with an SSD1306 / SH1106 OLED
- Read NTAG / Ultralight (with NDEF decoding) and MIFARE Classic; write NDEF and raw
  data; erase back to factory state; protected system areas
- REST + WebSocket API with display-code pairing and hashed tokens
- Built-in web app (English / French)
- OTA updates, per-device setup network password, factory reset (BOOT button or API)
- mDNS discovery (`_rfid-terminal._tcp`)
