#pragma once
// Per-contact DM unread counters. Kept apart from the DM ring in MessageHistory
// (which has no per-contact index), keyed by the same 4-byte pub_key prefix,
// and always read back clamped to what the ring still holds for that contact --
// the same self-healing shape as MessageHistory::chUnread() for channels.
//
// Header-only UI Core model (see docs/developer/ui-core.md); no drawing, no
// screen state.

class DmUnreadTable {
public:
  static const int SIZE = 16;

  DmUnreadTable() { clearAll(); }

  // An incoming DM from pub_key is about to be stored (called before the ring
  // insert). A sender that doesn't fit in the fixed table is simply not counted.
  void onIncoming(const uint8_t* pub_key) {
    int slot = -1, empty_slot = -1;
    for (int i = 0; i < SIZE; i++) {
      if (_t[i].count > 0 && memcmp(_t[i].prefix, pub_key, 4) == 0) { slot = i; break; }
      if (empty_slot < 0 && _t[i].count == 0) empty_slot = i;
    }
    if (slot >= 0) {
      if (_t[slot].count < 99) _t[slot].count++;
    } else if (empty_slot >= 0) {
      memcpy(_t[empty_slot].prefix, pub_key, 4);
      _t[empty_slot].count = 1;
      _t[empty_slot].overflow = false;   // fresh contact -- don't inherit a stale flag from whoever held this slot before
    }
    // Eviction/overflow is checked in afterInsert(), after the ring insert.
  }

  // The DM ring (unlike the channel ring) doesn't proactively decrement the
  // unread counters as it evicts old entries, so catch it here, right after
  // the insert: a raw count claiming more unread than the ring still holds
  // for that contact means one of their unread entries was just evicted. Any
  // contact can lose one -- not just this sender -- so check every slot.
  // Clamp back to the honest value and flag it (mirrors MessageHistory's
  // channel-side fix). Must run after the insert, not in onIncoming() (called
  // before it), or the new message itself reads as evicted.
  void afterInsert(const MessageHistory& h) {
    for (int i = 0; i < SIZE; i++) {
      if (_t[i].count == 0) continue;
      int held = h.dmHistCountForContact(_t[i].prefix);
      if (_t[i].count > held) {
        _t[i].count = (uint8_t)held;
        _t[i].overflow = held > 0;   // held == 0 frees the slot -- nothing left to flag
      }
    }
  }

  int total(const MessageHistory& h) const {
    int total = 0;
    for (int i = 0; i < SIZE; i++) {
      if (_t[i].count == 0) continue;
      int held = h.dmHistCountForContact(_t[i].prefix);
      total += (_t[i].count < held) ? _t[i].count : held;
    }
    return total;
  }

  uint8_t get(const MessageHistory& h, const uint8_t* pub_key) const {
    for (int i = 0; i < SIZE; i++) {
      if (_t[i].count > 0 && memcmp(_t[i].prefix, pub_key, 4) == 0) {
        int held = h.dmHistCountForContact(pub_key);
        return _t[i].count < held ? _t[i].count : (uint8_t)held;
      }
    }
    return 0;
  }

  // True once an unread entry for this contact was evicted off the DM ring
  // before ever being seen -- count is honest but understates the real total.
  bool overflow(const uint8_t* pub_key) const {
    for (int i = 0; i < SIZE; i++)
      if (_t[i].count > 0 && memcmp(_t[i].prefix, pub_key, 4) == 0)
        return _t[i].overflow;
    return false;
  }
  bool anyOverflow() const {
    for (int i = 0; i < SIZE; i++)
      if (_t[i].count > 0 && _t[i].overflow) return true;
    return false;
  }

  void clear(const uint8_t* pub_key) {
    for (int i = 0; i < SIZE; i++)
      if (_t[i].count > 0 && memcmp(_t[i].prefix, pub_key, 4) == 0)
        { _t[i].count = 0; _t[i].overflow = false; return; }
  }
  void clearAll() { memset(_t, 0, sizeof(_t)); }

  // Frees any slot whose ring occupancy has dropped to zero (evicted or
  // deduped-away messages) so a genuinely new sender isn't starved once the
  // fixed table fills with stale entries.
  void reconcile(const MessageHistory& h) {
    for (int i = 0; i < SIZE; i++) {
      if (_t[i].count == 0) continue;
      if (h.dmHistCountForContact(_t[i].prefix) == 0) {
        _t[i].count = 0;   // ring no longer holds anything for this sender -- free the slot
        _t[i].overflow = false;
      }
    }
  }

private:
  struct Entry { uint8_t prefix[4]; uint8_t count; bool overflow; };
  Entry _t[SIZE];
};
