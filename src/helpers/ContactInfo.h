#pragma once

#include <Arduino.h>
#include <Mesh.h>

#define OUT_PATH_UNKNOWN   0xFF

// ContactInfo::flags bit 4: the route in out_path was set by hand (the device's
// Route editor) and is kept: a path heard from the contact, or a message that
// goes unanswered, doesn't replace it. Bit 0 is the favourite star, bits 1-3 the
// telemetry permissions. Reset path (or a new route saved) clears / sets it.
#define CONTACT_FLAG_ROUTE_PINNED  0x10

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;   // on of ADV_TYPE_*
  uint8_t flags;
  uint8_t out_path_len;
  mutable bool shared_secret_valid; // flag to indicate if shared_secret has been calculated
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;   // by THEIR clock
  uint32_t lastmod;  // by OUR clock
  int32_t gps_lat, gps_lon;    // 6 dec places
  uint32_t sync_since;

  const uint8_t* getSharedSecret(const mesh::LocalIdentity& self_id) const {
    if (!shared_secret_valid) {
      self_id.calcSharedSecret(shared_secret, id.pub_key);
      shared_secret_valid = true;
    }
    return shared_secret;
  }

  bool isFav() const { return flags & 0x01; }
  bool isTelemBaseAllowed() const { return flags & 0x02; }
  bool isTelemLocAllowed() const { return flags & 0x04; }
  bool isTelemEnvAllowed() const { return flags & 0x08; }
  bool isRemoteCLIAllowed() const { return flags & 0x10; }

private:
  mutable uint8_t shared_secret[PUB_KEY_SIZE];
};
