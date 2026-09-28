#pragma once

// The ESP32's internal filesystem, on the "spiffs" partition: SPIFFS, or with
// -D ESP32_LITTLEFS LittleFS -- far faster for the small files MeshCore keeps
// (a settings save: ~0.5 s on SPIFFS), and safe against power loss. The two
// formats don't mix: switching a board formats the partition on first boot.
#ifdef ESP32_LITTLEFS
  #include <LittleFS.h>
  #define ESP32_FS        LittleFS
  #define ESP32_FS_CLASS  fs::LittleFSFS
#else
  #include <SPIFFS.h>
  #define ESP32_FS        SPIFFS
  #define ESP32_FS_CLASS  fs::SPIFFSFS
#endif
