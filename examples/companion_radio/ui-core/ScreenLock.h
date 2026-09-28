#pragma once
// The screen lock's PIN, for every frontend: stored in NodePrefs as a salted
// SHA-256 (lock_screen_password / _salt), never as the PIN itself. It locks
// the screen only -- the radio and the app link keep working.

#include <string.h>
#include <Arduino.h>
#include <Utils.h>
#include "../NodePrefs.h"

namespace screenlock {

static const uint8_t TRIES = 5;           // misses before a pause
static const uint32_t PAUSE_MS = 30000;

// Any byte set: a digest can start with a zero byte.
static bool isSet(const NodePrefs& p) {
  for (uint8_t b : p.lock_screen_password) if (b) return true;
  return false;
}

static void clear(NodePrefs& p) {
  memset(p.lock_screen_password, 0, sizeof(p.lock_screen_password));
  memset(p.lock_screen_password_salt, 0, sizeof(p.lock_screen_password_salt));
}

// A new PIN under a fresh salt; "" clears it. The caller saves the prefs.
static void set(NodePrefs& p, const char* pin, mesh::RNG* rng) {
  if (!pin || !pin[0]) { clear(p); return; }
  if (rng) {
    rng->random(p.lock_screen_password_salt, sizeof(p.lock_screen_password_salt));
  } else {
    uint32_t t = micros() ^ (millis() << 16);
    memcpy(p.lock_screen_password_salt, &t, sizeof(t));
  }
  mesh::Utils::sha256(p.lock_screen_password, sizeof(p.lock_screen_password),
                      p.lock_screen_password_salt, sizeof(p.lock_screen_password_salt),
                      (const uint8_t*)pin, (int)strlen(pin));
}

static bool check(const NodePrefs& p, const char* pin) {
  if (!pin) return false;
  uint8_t digest[NodePrefs::LOCK_HASH_LEN];
  mesh::Utils::sha256(digest, sizeof(digest),
                      p.lock_screen_password_salt, sizeof(p.lock_screen_password_salt),
                      (const uint8_t*)pin, (int)strlen(pin));
  return memcmp(digest, p.lock_screen_password, sizeof(digest)) == 0;
}

// Wrong tries: TRIES misses in a row, then PAUSE_MS before the next one.
struct Attempts {
  uint8_t fails = 0;
  uint32_t block_until = 0;   // millis(); 0 = not blocked

  bool blocked() {
    if (block_until && (int32_t)(block_until - millis()) > 0) return true;
    block_until = 0;
    return false;
  }
  uint32_t secondsLeft() const {
    int32_t left = (int32_t)(block_until - millis());
    return block_until && left > 0 ? (left + 999) / 1000 : 0;
  }
  void ok() { fails = 0; block_until = 0; }
  // A miss; true when it starts the pause.
  bool miss() {
    if (++fails < TRIES) return false;
    fails = 0;
    block_until = (millis() + PAUSE_MS) | 1;
    return true;
  }
  uint8_t left() const { return TRIES - fails; }
};

}  // namespace screenlock
