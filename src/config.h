#pragma once

// Every value can be overridden from platformio.ini with -DNAME=value.

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// Wiring (ESP32-C3 Super Mini). The PN532 and the display share the I2C bus.
#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 2
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 1
#endif
#ifndef PIN_PN532_IRQ
#define PIN_PN532_IRQ 3     // -1 when not wired: the reader is then polled
#endif
#ifndef PIN_BOOT_BUTTON
#define PIN_BOOT_BUTTON 9   // held 5 s: factory reset
#endif
#ifndef I2C_FREQ
#define I2C_FREQ 400000
#endif
#ifndef OLED_ADDR
#define OLED_ADDR 0x3C
#endif

// Defaults, changeable at runtime through the API
#ifndef DEFAULT_NAME
#define DEFAULT_NAME "RFID Terminal"
#endif
#ifndef DEFAULT_HOSTNAME
#define DEFAULT_HOSTNAME "rfid"   // http://rfid.local
#endif
#ifndef DEFAULT_LANG
#define DEFAULT_LANG "en"         // "en" or "fr"
#endif
#ifndef TZ_INFO
#define TZ_INFO "UTC0"            // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

#define MDNS_SERVICE     "rfid-terminal"   // _rfid-terminal._tcp
#define AP_PREFIX        "RFID-"
#define JOB_TIMEOUT_MS   20000             // max wait for a badge
#define PAIR_TIMEOUT_MS  120000
#define RESET_HOLD_MS    5000
#define MAX_TOKENS       8
#define HISTORY_SIZE     20
#define JOBS_KEPT        10
