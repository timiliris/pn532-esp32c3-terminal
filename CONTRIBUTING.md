# Contributing

Thanks for your interest! Issues and pull requests are welcome.

- Run `pio test -e native` and `pio run -e c3` before opening a pull request; CI runs both.
- Keep the I2C bus owned by the main loop: web handlers must not talk to the PN532 or the
  display, they queue jobs and read the shared state under a `Lock`.
- User-facing text on the display or in job messages goes through `src/i18n.cpp`
  (English and French). API error messages stay in English.
- Write-protection of block 0, sector trailers and Type 2 system pages is intentional and
  will not be removed. Features aimed at cloning UIDs or recovering unknown keys are out
  of scope.
