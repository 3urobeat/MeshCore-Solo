#pragma once
// Favourites dial slots (NodePrefs::favourite_contacts / favourite_kinds).
// Slot index 0..FAVOURITES_COUNT-1; a slot holds either a contact / room
// (pubkey prefix) or a channel (index in byte 0), per favourite_kinds[].

namespace favslots {

inline uint8_t kind(const NodePrefs* p, int slot) {
  if (!p || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return NodePrefs::FAV_KIND_CONTACT;
  return p->favourite_kinds[slot];
}

inline int findContact(const NodePrefs* p, const uint8_t* pub_key) {
  if (!p || !pub_key) return -1;
  for (int i = 0; i < NodePrefs::FAVOURITES_COUNT; i++) {
    if (p->favourite_kinds[i] != NodePrefs::FAV_KIND_CONTACT) continue;
    if (memcmp(p->favourite_contacts[i], pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
      // All-zero prefix is "empty" — never matches a real key.
      for (uint8_t b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++)
        if (p->favourite_contacts[i][b]) return i;
    }
  }
  return -1;
}

inline int findChannel(const NodePrefs* p, uint8_t ch_idx) {
  if (!p) return -1;
  for (int i = 0; i < NodePrefs::FAVOURITES_COUNT; i++)
    if (p->favourite_kinds[i] == NodePrefs::FAV_KIND_CHANNEL && p->favourite_contacts[i][0] == ch_idx) return i;
  return -1;
}

inline bool isEmpty(const NodePrefs* p, int slot) {
  if (!p || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return true;
  // A channel slot is never empty: channel 0's prefix is all zeroes.
  if (p->favourite_kinds[slot] == NodePrefs::FAV_KIND_CHANNEL) return false;
  for (uint8_t b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++)
    if (p->favourite_contacts[slot][b]) return false;
  return true;
}

inline void setContact(NodePrefs* p, int slot, const uint8_t* pub_key) {
  if (!p || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT || !pub_key) return;
  memcpy(p->favourite_contacts[slot], pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
  p->favourite_kinds[slot] = NodePrefs::FAV_KIND_CONTACT;
}

inline void setChannel(NodePrefs* p, int slot, uint8_t ch_idx) {
  if (!p || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return;
  memset(p->favourite_contacts[slot], 0, NodePrefs::FAVOURITE_PREFIX_LEN);
  p->favourite_contacts[slot][0] = ch_idx;
  p->favourite_kinds[slot] = NodePrefs::FAV_KIND_CHANNEL;
}

inline void clear(NodePrefs* p, int slot) {
  if (!p || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return;
  memset(p->favourite_contacts[slot], 0, NodePrefs::FAVOURITE_PREFIX_LEN);
  p->favourite_kinds[slot] = NodePrefs::FAV_KIND_CONTACT;
}

// Pin into `slot`, moving it there if it already sits in another one.
inline void pinContact(NodePrefs* p, int slot, const uint8_t* pub_key) {
  int existing = findContact(p, pub_key);
  if (existing >= 0 && existing != slot) clear(p, existing);
  setContact(p, slot, pub_key);
}
inline void pinChannel(NodePrefs* p, int slot, uint8_t ch_idx) {
  int existing = findChannel(p, ch_idx);
  if (existing >= 0 && existing != slot) clear(p, existing);
  setChannel(p, slot, ch_idx);
}

}  // namespace favslots
