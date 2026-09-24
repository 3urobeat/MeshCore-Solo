#pragma once
// UI Core: hardware-independent UI state and logic shared by every frontend
// (ui-new today, ui-lvgl later). See docs/development/ui-core.md.
//
// For now the Core is header-only and compiled as part of the frontend's single
// translation unit (ui-new/UITask.cpp includes this file), so no platformio.ini
// needs a new source filter or include path. It relies on the same globals the
// frontend does (the_mesh, rtc_clock).

#include "MessageHistory.h"
#include "DmUnreadTable.h"

class UiCore {
public:
  // ── Models ────────────────────────────────────────────────────────────────
  MessageHistory history;   // channel + DM rings, delivery state, channel unread
  DmUnreadTable  dm_unread; // per-contact DM unread counters

  // ── DM unread (clamped to what the DM ring still holds) ──────────────────
  void    noteIncomingDM(const uint8_t* pub_key) { dm_unread.onIncoming(pub_key); }
  void    afterDMInsert()                         { dm_unread.afterInsert(history); }
  int     dmUnreadTotal() const                   { return dm_unread.total(history); }
  uint8_t dmUnread(const uint8_t* pub_key) const  { return dm_unread.get(history, pub_key); }
  bool    dmUnreadOverflow(const uint8_t* pub_key) const { return dm_unread.overflow(pub_key); }
  bool    anyDMUnreadOverflow() const             { return dm_unread.anyOverflow(); }
  void    clearDMUnread(const uint8_t* pub_key)   { dm_unread.clear(pub_key); }
  void    clearAllDMUnread()                      { dm_unread.clearAll(); }
  void    reconcileDMUnread()                     { dm_unread.reconcile(history); }

  // All messages synced to the app: nothing is unread any more (room unread
  // still lives in the frontend).
  void clearAllUnread() {
    dm_unread.clearAll();
    history.clearAllChannelUnread();
  }
};
