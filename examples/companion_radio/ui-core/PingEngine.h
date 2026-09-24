#pragma once
// Ping engine: one on-device ping in flight at a time (MyMesh::sendPing), its
// result (RTT + SNR both ways) kept after the in-flight slot is released so the
// view can keep showing it. The reply arrives through MyMesh's ping callback,
// which is a plain function pointer -- routed here via a single static instance.
//
// The ping timeout is still decided by the view (NearbyScreen's
// PING_TIMEOUT_MS), which calls clear() when it gives up.

class PingEngine {
public:
  enum StartResult : uint8_t { STARTED = 0, BUSY, UNSUPPORTED, SEND_FAILED };

  void begin(NodePrefs* prefs) { _prefs = prefs; s_instance = this; }

  StartResult start(const uint8_t* pub_key) {
    if (_active || !pub_key) return BUSY;
    if (_prefs && _prefs->path_hash_mode > 1) return UNSUPPORTED;   // no 3-byte path hash support

    _active = true;
    _tag = 0;
    _snr_out_x4 = 0;
    _snr_back_x4 = 0;
    _rtt_ms = 0;

    // Always install the callback before sending so the response cannot race it.
    the_mesh.setPingCallback(onPingResult, NULL);
    _tag = the_mesh.sendPing(pub_key, _prefs ? _prefs->path_hash_mode + 1 : 1);
    if (_tag == 0) {
      clear();
      return SEND_FAILED;
    }
    return STARTED;
  }

  // Release the in-flight slot (result values are kept).
  void clear() {
    if (_tag != 0) the_mesh.clearPingResult(_tag);
    _active = false;
    _tag = 0;
  }

  bool isActive() const { return _active; }
  void getResult(int16_t& snr_out_x4, int16_t& snr_back_x4, uint32_t& rtt_ms) const {
    snr_out_x4 = _snr_out_x4;
    snr_back_x4 = _snr_back_x4;
    rtt_ms = _rtt_ms;
  }

private:
  void handleResult(uint32_t tag, int16_t snr_out_x4, int16_t snr_back_x4, uint32_t rtt_ms) {
    if (_active && _tag == tag) {
      _snr_out_x4 = snr_out_x4;
      _snr_back_x4 = snr_back_x4;
      _rtt_ms = rtt_ms;
      // Release the in-flight slot immediately; the view keeps the result values.
      clear();
    }
  }

  static void onPingResult(uint32_t tag, int16_t snr_out_x4, int16_t snr_back_x4, uint32_t rtt_ms) {
    if (s_instance) s_instance->handleResult(tag, snr_out_x4, snr_back_x4, rtt_ms);
  }

  static PingEngine* s_instance;

  NodePrefs* _prefs = nullptr;
  bool     _active = false;
  uint32_t _tag = 0;
  int16_t  _snr_out_x4 = 0;
  int16_t  _snr_back_x4 = 0;
  uint32_t _rtt_ms = 0;
};

// Header-only Core, single TU (see UiCore.h) -- the one definition lives here.
PingEngine* PingEngine::s_instance = nullptr;
