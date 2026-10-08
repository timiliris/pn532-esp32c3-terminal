# PN532 ESP32-C3 Terminal

[![CI](https://github.com/timiliris/pn532-esp32c3-terminal/actions/workflows/ci.yml/badge.svg)](https://github.com/timiliris/pn532-esp32c3-terminal/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

*[Version française](README.fr.md)*

A passive NFC badge terminal you drive over your local network: an ESP32-C3 Super Mini,
a PN532 reader and a small OLED display. Apps create jobs (read, write, erase) through a
REST + WebSocket API; the terminal asks for a badge on its display and runs the job on
the first one presented. It ships with a built-in web app that uses the same API.

## Features

- **Read** NTAG213/215/216, MIFARE Ultralight (full dump + decoded NDEF) and MIFARE
  Classic 1K/4K/Mini (sectors readable with the key A you provide)
- **Write** NDEF messages (several URL / text records), raw blocks or pages, and **erase**
  badges back to their factory state
- **Live events** over WebSocket: badge present / removed, job progress
- **Pairing** with a 6-digit code shown on the display (proof of physical access);
  tokens are stored hashed
- **Built-in web app** (English / French), served by the terminal
- **Over-the-air updates**, Wi-Fi setup network with a per-device password, factory
  reset with the BOOT button
- **Discovery** through mDNS (`_rfid-terminal._tcp`)
- Robust I2C: one owner task for the bus, IRQ-driven badge detection, bus recovery and
  retries on weak RF links

**Safety by design:** block 0, sector trailers (keys and access bits) and Type 2 system
pages are never written, so a mistake cannot brick a badge. The firmware does not clone
UIDs or recover unknown keys.

## Hardware

| Part | Notes |
|------|-------|
| ESP32-C3 Super Mini | any ESP32-C3 board works with adjusted pins |
| PN532 module (red "V3" board) | set to **I2C**: switch 1 ON, switch 2 OFF |
| 0.96" OLED, SSD1306, I2C | 1.3" SH1106 displays: use the `c3-sh1106` build |

### Wiring

The PN532 and the display share the I2C bus.

| ESP32-C3 | PN532 (4-pin header) | OLED |
|----------|----------------------|------|
| 3V3      | VCC                  | VCC  |
| GND      | GND                  | GND  |
| GPIO 2   | SDA                  | SDA  |
| GPIO 1   | SCL                  | SCL  |
| GPIO 3   | IRQ (8-pin header)   |      |

- Power both modules from **3V3**: their I2C pull-ups then stay at 3.3 V, which the
  ESP32-C3 requires.
- The PN532 only reads its mode switches at power-on: after changing them, unplug the
  board.
- IRQ is optional (`-DPIN_PN532_IRQ=-1`), the reader is then polled.
- Pins and defaults live in [`src/config.h`](src/config.h) and can be overridden with
  `-D` flags in `platformio.ini`.

## Install

### From a release

Download `factory-c3.bin` from the [latest release](https://github.com/timiliris/pn532-esp32c3-terminal/releases)
and flash it at offset `0x0`, for instance with [ESP Web Flasher](https://espressif.github.io/esptool-js/)
or:

```bash
esptool.py --chip esp32c3 write_flash 0x0 factory-c3.bin
```

### From source

With [PlatformIO](https://platformio.org/):

```bash
pio run -e c3 -t upload && pio device monitor
```

## First start

1. The display shows a setup network `RFID-XXXX` and its password. Join it and open
   `http://192.168.4.1`.
2. Click **Show a code on the terminal** and type the code from the display: the browser
   is paired.
3. **Settings > Wi-Fi**: pick your network. The terminal reboots and is then reachable at
   `http://rfid.local` (the address is also on the display).

Factory reset: hold the BOOT button for 5 seconds (or `POST /api/v1/factory-reset`).

## API

Full specification: [`docs/openapi.yaml`](docs/openapi.yaml).

```bash
# pair (the code shows on the display)
curl -X POST http://rfid.local/api/v1/pair/start
curl -X POST http://rfid.local/api/v1/pair/confirm \
     -H 'Content-Type: application/json' -d '{"code":"123456","name":"my app"}'

# write a link on the next badge presented
curl -X POST http://rfid.local/api/v1/write/ndef \
     -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
     -d '{"records":[{"type":"url","value":"https://example.com"}]}'

# follow badges and jobs live
websocat "ws://rfid.local/api/v1/events?token=$TOKEN"
```

A job goes `waiting` → `running` → `done` / `error` / `timeout` / `cancelled`;
`Job.code` gives the outcome to code against, `Job.message` a human-readable text.

## Development

```bash
pio test -e native     # NDEF codec unit tests, on the host
pio run -e c3          # firmware
```

| File | Role |
|------|------|
| `src/pn532.*` | minimal PN532 I2C driver (async listening via IRQ) |
| `src/nfc.*`   | badge detection, identification and jobs |
| `src/ndef.*`  | NDEF encoder / decoder, no Arduino dependency |
| `src/api.*`   | REST + WebSocket API, pairing, OTA |
| `src/ui.*`    | OLED screens |
| `src/web_app.h` | built-in web app |
| `src/i18n.*`  | display and job texts (en / fr) |

Contributions are welcome, see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
