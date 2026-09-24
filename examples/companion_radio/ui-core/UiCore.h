#pragma once
// UI Core: hardware-independent UI state and logic shared by every frontend
// (ui-new today, ui-lvgl later). See docs/development/ui-core.md.
//
// The Core is MyMesh's Listener: it files incoming/outgoing messages into the
// history, keeps the unread counters, runs the engines, and tells the frontend
// what happened through `events` (drained from the frontend's loop()). The
// frontend implements UiCoreHost for the few things the Core still asks of it.
//
// For now the Core is header-only and compiled as part of the frontend's single
// translation unit (ui-new/UITask.cpp includes this file), so no platformio.ini
// needs a new source filter or include path. It relies on the same globals the
// frontend does (the_mesh, rtc_clock).

#include "MessageHistory.h"
#include "DmUnreadTable.h"
#include "UiEvents.h"
#include "UiCoreHost.h"
#include "ClockEngine.h"
#include "PingEngine.h"
#include "CourseEngine.h"
#include "LiveShareEngine.h"
#include "LocatorEngine.h"
#include "TrailEngine.h"

class UiCore : public MyMesh::Listener {
public:
  void begin(NodePrefs* prefs, SensorManager* sensors, UiCoreHost* host) {
    _host = host;
    clock.begin(prefs, &events);
    ping.begin(prefs);
    course.begin(sensors);
    live_share.begin(prefs, &course, &events);
    locator.begin(prefs, &course, &live_share, &events);
    trail.begin(prefs, &course);
  }

  // Driven from the frontend's loop(), before it drains `events`.
  void loop() {
    clock.loop();
    course.loop();
    live_share.loop();
    locator.loop();
    trail.loop();
  }

  UiEventQueue events;      // Core → frontend; drained by the frontend's loop()

  // ── Engines ───────────────────────────────────────────────────────────────
  ClockEngine clock;        // alarm / countdown / ring
  PingEngine  ping;         // single in-flight ping + last result
  CourseEngine course;      // GPS position + course-over-ground ring
  LiveShareEngine live_share; // [LOC] auto-share session + peers' shared positions
  LocatorEngine locator;    // active target, geofence crossings, proximity beeper
  TrailEngine  trail;       // GPS trail store, sampling, auto-pause, shutdown save

  // ── Models ────────────────────────────────────────────────────────────────
  MessageHistory history;   // channel + DM rings, delivery state, channel unread
  DmUnreadTable  dm_unread; // per-contact DM unread counters

  // ── Unread ────────────────────────────────────────────────────────────────
  // Messages waiting in the companion-app offline queue (0 = the app synced).
  int     msgCount() const                        { return _queue_len; }
  int     roomUnread() const                      { return _room_unread; }
  void    clearRoomUnread()                       { _room_unread = 0; }
  // DM unread, clamped to what the DM ring still holds.
  int     dmUnreadTotal() const                   { return dm_unread.total(history); }
  uint8_t dmUnread(const uint8_t* pub_key) const  { return dm_unread.get(history, pub_key); }
  bool    dmUnreadOverflow(const uint8_t* pub_key) const { return dm_unread.overflow(pub_key); }
  bool    anyDMUnreadOverflow() const             { return dm_unread.anyOverflow(); }
  void    clearDMUnread(const uint8_t* pub_key)   { dm_unread.clear(pub_key); }
  void    clearAllDMUnread()                      { dm_unread.clearAll(); }
  void    reconcileDMUnread()                     { dm_unread.reconcile(history); }

  // ════ MyMesh::Listener ════════════════════════════════════════════════════

  void onQueueSizeChanged(int msgcount) override {
    _queue_len = msgcount;
    if (msgcount == 0) {   // the app drained the queue: nothing is unread any more
      _room_unread = 0;
      dm_unread.clearAll();
      history.clearAllChannelUnread();
    }
  }

  void onACKRecv(uint32_t ack_crc) override { history.markDmDelivered(ack_crc); }
  void onAdvertHeard(bool was_flood) override { events.push(UiEventType::AdvertHeard, nullptr, was_flood); }
  bool requestShutdown(bool restart) override { _host->shutdown(restart); return true; }

  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp,
                     const char* text) override {
    onMessageRecvEx(pkt, from, txt_type, sender_timestamp, nullptr, 0, text);
  }
  void onMessageRecvEx(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp,
                       const uint8_t* extra, int extra_len, const char* text) override {
    // we only want to show text messages on display, not cli data
    if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;
    if (from.type == ADV_TYPE_ROOM && _room_unread < _queue_len) _room_unread++;
    if (from.type == ADV_TYPE_CHAT) dm_unread.onIncoming(from.id.pub_key);   // before the ring insert below
    UiEvent& ev = pushMessageArrived(from.type == ADV_TYPE_ROOM ? UIEventType::roomMessage : UIEventType::contactMessage,
                                     from.name, -1);
    if (from.type == ADV_TYPE_CHAT) { memcpy(ev.key, from.id.pub_key, 4); ev.flag = true; }
    // Add to the on-device conversation history. Room servers (ADV_TYPE_ROOM) are
    // viewed through the same history list as chat contacts (keyed by the server's
    // pubkey), so their posts must be stored too — otherwise an incoming room
    // message fires the notification and reaches the app via the offline queue but
    // never shows when the room is opened directly on the device.
    if (from.type == ADV_TYPE_CHAT) {
      addDMMsg(from.id.pub_key, false, text, sender_timestamp, 0, 0, 0, pkt->path, (uint8_t)pkt->path_len);
    } else if (from.type == ADV_TYPE_ROOM) {
      // A room carries many guests, so prefix the post with its author so the UI
      // can attribute each line. The signed message's `extra` holds the sender's
      // pubkey prefix; resolve it to a contact name, falling back to a short hex.
      char labeled[MAX_TEXT_LEN + 40];  // room text + "Sender: " (history store truncates)
      if (extra && extra_len >= 4) {
        ContactInfo* sc = the_mesh.lookupContactByPubKey(extra, extra_len);
        if (sc && sc->name[0])
          snprintf(labeled, sizeof(labeled), "%s: %s", sc->name, text);
        else
          snprintf(labeled, sizeof(labeled), "%02X%02X: %s", extra[0], extra[1], text);
      } else {
        snprintf(labeled, sizeof(labeled), "%s", text);
      }
      addDMMsg(from.id.pub_key, false, labeled, sender_timestamp, 0, 0, 0, pkt->path, (uint8_t)pkt->path_len);
    }
  }

  // Upstream-shaped entry point without the channel slot: MyMesh itself always
  // calls the Ex variant below, so this only serves a caller that doesn't know
  // the slot -- notify, but there's no history ring to file it under.
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override {
    pushMessageArrived(UIEventType::channelMessage, channel_details.name, -1);
  }
  void onChannelMessageRecvEx(mesh::Packet *pkt, uint8_t channel_idx, ChannelDetails& channel_details,
                              uint32_t timestamp, const char* text) override {
    addChannelMsg(channel_idx, text, timestamp, pkt->path, (uint8_t)pkt->path_len);
    pushMessageArrived(UIEventType::channelMessage, channel_details.name, channel_idx);
  }

  // Also the entry point for our own sends mirrored from the app / the bot
  // (own_message) and for on-device composes. Returns the ring position.
  int addChannelMsg(uint8_t channel_idx, const char* text, uint32_t timestamp = 0,
                    const uint8_t* path = nullptr, uint8_t path_len = 0,
                    bool own_message = false) override {
    bool viewing = _host->isViewingChannel(channel_idx);
    int pos = history.addChannelMsg(channel_idx, text, viewing, timestamp, path, path_len, own_message);
    if (viewing && pos >= 0) _host->onViewedHistoryGrew(true);
    return pos;
  }
  void armChannelRelay(int pos, uint32_t seq) override { history.armChannelRelay(pos, seq); }
  void onChannelRelayed(uint32_t seq, const uint8_t* repeater_hash = nullptr, uint8_t hash_size = 0) override {
    history.markChannelRelayed(seq, repeater_hash, hash_size);
  }

  void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text, uint32_t sender_timestamp = 0,
                uint32_t ack_tag = 0, uint32_t ack_deadline_ms = 0, uint8_t resends = 0,
                const uint8_t* path = nullptr, uint8_t path_len = 0) override {
    bool viewing = _host->isViewingDM(pub_key);
    history.addDMMsg(pub_key, outgoing, text, sender_timestamp, ack_tag, ack_deadline_ms, resends, path, path_len);
    if (viewing) _host->onViewedHistoryGrew(false);
    dm_unread.afterInsert(history);
  }

  void onSharedLocation(const uint8_t* pub_key, const char* name, int32_t lat_1e6, int32_t lon_1e6,
                        uint32_t ts, bool verified) override {
    live_share.onSharedLocation(pub_key, name, lat_1e6, lon_1e6, ts, verified);
  }

  // Not yet extracted -- the frontend still owns these.
  void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) override {
    _host->onRoomLoginResult(pub_key, success, permissions);
  }
  void onAdminReply(const uint8_t* pub_key, const char* text) override { _host->onAdminReply(pub_key, text); }
  void onContactRemoved(const uint8_t* pub_key) override { _host->onContactRemoved(pub_key); }
  void onChannelRemoved(uint8_t channel_idx) override { _host->onChannelRemoved(channel_idx); }
  void botSetGPS(bool on) override { _host->botSetGPS(on); }
  void botBuzz(int seconds) override { _host->botBuzz(seconds); }
  bool botSetGPIO(int idx, bool on) override { return _host->botSetGPIO(idx, on); }
  bool botGetGPIO(int idx, bool& is_output, bool& value) override { return _host->botGetGPIO(idx, is_output, value); }
  bool botGetGPIOAnalog(int idx, int& millivolts) override { return _host->botGetGPIOAnalog(idx, millivolts); }

private:
  UiEvent& pushMessageArrived(UIEventType kind, const char* name, int channel_idx) {
    UiEvent& ev = events.push(UiEventType::MessageArrived, name);
    ev.kind = kind;
    ev.idx  = (int16_t)channel_idx;
    return ev;
  }

  UiCoreHost* _host = nullptr;
  int _queue_len = 0;     // last onQueueSizeChanged()
  int _room_unread = 0;
};
