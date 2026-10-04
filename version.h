#ifndef VERSION_H
#define VERSION_H

/*
  Firmware version.

  FIRMWARE_VERSION_MINOR is incremented automatically by `make compile`
  (see the bump-minor target in the Makefile), so every recompile
  produces a distinct version. Commit version.h alongside the code so
  the version history is visible in git.

  Bump MAJOR by hand when a change makes existing EEPROM calibration
  records invalid in a way that a minor bump would not catch.
*/

#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 15
#define FIRMWARE_VERSION_PATCH 0

#define PACK_VERSION(major, minor, patch) \
  (((uint32_t)(major) << 16) | ((uint32_t)(minor) << 8) | (uint32_t)(patch))

const uint32_t FIRMWARE_VERSION =
  PACK_VERSION(FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR,
               FIRMWARE_VERSION_PATCH);

#endif
