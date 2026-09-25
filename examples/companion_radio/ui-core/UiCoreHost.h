#pragma once
// What the UI Core needs from the frontend that hosts it. The Core is MyMesh's
// Listener; everything it can decide on its own (history, unread, engines) it
// does, and reports to the frontend through UiEventQueue. This interface is the
// remainder -- calls that must stay synchronous with mesh processing, or whose
// logic still lives in the frontend until its extraction step
// (docs/development/ui-core.md). None of these may draw or block.

class UiCoreHost {
public:
  virtual ~UiCoreHost() {}

  // ── View state consulted while filing an incoming message ────────────────
  // "Viewing" = that conversation is open on screen, so the message isn't
  // counted unread. After a message is filed into a conversation being viewed,
  // onViewedHistoryGrew() lets the view keep its selection on the same entry
  // (ring entries are numbered newest-first).
  virtual bool isViewingChannel(uint8_t channel_idx) { (void)channel_idx; return false; }
  virtual bool isViewingDM(const uint8_t* pub_key) { (void)pub_key; return false; }
  virtual void onViewedHistoryGrew(bool channel) { (void)channel; }

  // ── Not yet extracted (screen sessions, device controls, prefs cleanup) ──
  virtual void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {}
  virtual void onAdminReply(const uint8_t* pub_key, const char* text) {}
  virtual void onContactRemoved(const uint8_t* pub_key) {}
  virtual void onChannelRemoved(uint8_t channel_idx) {}
  virtual void botSetGPS(bool on) {}
  virtual void botBuzz(int seconds) {}
  virtual bool botSetGPIO(int idx, bool on) { return false; }
  virtual bool botGetGPIO(int idx, bool& is_output, bool& value) { return false; }
  virtual bool botGetGPIOAnalog(int idx, int& millivolts) { return false; }

  // A display pref changed through the settings schema (brightness, ...):
  // push it to the panel.
  virtual void applyDisplayPrefs() {}

  // Controlled power-down / restart (flush state first). See
  // AbstractUITask::shutdown().
  virtual void shutdown(bool restart) = 0;
};
