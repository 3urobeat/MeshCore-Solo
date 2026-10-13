#pragma once
// Path editor: the route to one node as a list of repeaters, traced and saved.
// Shared by both frontends (ui-new's Path screen, ui-lvgl's path editor and its
// "pick on the map" mode); the view only draws `hop()` rows and calls the
// actions below.
//
//   begin(contact)    -- loads the contact's stored route (out_path), if any
//   addRepeater(key)  -- appends a repeater (a contact of type repeater)
//   remove / move     -- edit the list
//   trace()           -- a TRACE out and back over the list (A B dest B A):
//                        a reply needs the last retransmission to be heard,
//                        so a far node is traced on its way there and back
//   poll()            -- advance a running trace (call every UI pass)
//   save()            -- the list becomes the contact's out_path
//
// A hop keeps up to 4 bytes of the repeater's key, so the route can be traced
// with 1, 2 or 4 byte hashes and stored with 1, 2 or 3; a hop loaded from a
// stored path whose repeater isn't a contact keeps only the bytes it had.
//
// Header-only Core, single TU (see UiCore.h).

class PathEditor {
public:
  static const int MAX_HOPS = 7;   // repeaters; out and back is 2n+1 <= TRACE_SNR_MAX entries
  static const int NAME_LEN = 24;

  enum State : uint8_t { IDLE, RUNNING, DONE, TIMEOUT, FAILED };

  struct Hop {
    uint8_t key[4];
    uint8_t known;            // bytes of key that are real (1..4)
    char    name[NAME_LEN];
  };

  void begin(NodePrefs* prefs, PingEngine* ping) { _prefs = prefs; _ping = ping; }

  // Opens the editor for a node: its key and name, and its stored route.
  bool open(const uint8_t* pub_key) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (!c) return false;
    memcpy(_target_key, c->id.pub_key, PUB_KEY_SIZE);
    snprintf(_target_name, sizeof(_target_name), "%s", c->name);
    _n = 0;
    _state = IDLE;
    _stored = c->out_path_len != OUT_PATH_UNKNOWN;
    _pinned = _stored && (c->flags & CONTACT_FLAG_ROUTE_PINNED) != 0;
    if (_stored) {
      uint8_t size = (c->out_path_len >> 6) + 1;
      uint8_t cnt = c->out_path_len & 63;
      for (uint8_t i = 0; i < cnt && _n < MAX_HOPS; i++) loadHash(&c->out_path[i * size], size);
    }
    return true;
  }

  const uint8_t* targetKey() const { return _target_key; }
  const char* targetName() const { return _target_name; }
  bool hasStoredPath() const { return _stored; }
  // A kept route isn't replaced by a path heard from the node or dropped after a
  // failed send (CONTACT_FLAG_ROUTE_PINNED); written with save(), or at once
  // when the route is already stored.
  bool pinned() const { return _pinned; }
  void setPinned(bool on) {
    _pinned = on;
    if (_stored) the_mesh.setContactPinned(_target_key, on);
  }

  // ── the list ────────────────────────────────────────────────────────────────
  int count() const { return _n; }
  const Hop& hop(int i) const { return _hops[i]; }
  bool full() const { return _n >= MAX_HOPS; }

  // Appends a repeater by its key; false if it isn't a repeater, the list is
  // full or it is the one just before (a hop twice in a row can't be heard).
  bool addRepeater(const uint8_t* pub_key) {
    if (_n >= MAX_HOPS) return false;
    ContactInfo* c = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (!c || c->type != ADV_TYPE_REPEATER) return false;
    if (_n > 0 && _hops[_n - 1].known >= 1 && memcmp(_hops[_n - 1].key, c->id.pub_key, _hops[_n - 1].known) == 0) return false;
    Hop& h = _hops[_n++];
    memcpy(h.key, c->id.pub_key, 4);
    h.known = 4;
    snprintf(h.name, sizeof(h.name), "%s", c->name);
    invalidate();
    return true;
  }

  void remove(int i) {
    if (i < 0 || i >= _n) return;
    for (int k = i; k + 1 < _n; k++) _hops[k] = _hops[k + 1];
    _n--;
    invalidate();
  }

  void removeLast() { remove(_n - 1); }

  // Moves hop i one place (dir -1 up, +1 down).
  void move(int i, int dir) {
    int j = i + dir;
    if (i < 0 || i >= _n || j < 0 || j >= _n) return;
    Hop t = _hops[i];
    _hops[i] = _hops[j];
    _hops[j] = t;
    invalidate();
  }

  void clear() {
    _n = 0;
    invalidate();
  }

  // Is this repeater (by key) already in the list? (map picking marks it)
  int indexOf(const uint8_t* pub_key) const {
    for (int i = 0; i < _n; i++)
      if (memcmp(_hops[i].key, pub_key, _hops[i].known) == 0) return i;
    return -1;
  }

  // ── trace ───────────────────────────────────────────────────────────────────
  State state() const { return _state; }
  uint32_t timeoutMs() const { return _ping ? _ping->timeoutMs() : 3000; }

  // The hash width a trace uses: the node's path hash mode, narrowed to what
  // every hop has bytes for. 0 if a hop has nothing usable.
  uint8_t traceWidth() const {
    uint8_t want = _prefs ? (_prefs->path_hash_mode >= 2 ? 4 : _prefs->path_hash_mode + 1) : 1;
    uint8_t known = 4;
    for (int i = 0; i < _n; i++) if (_hops[i].known < known) known = _hops[i].known;
    while (want > known) want = want == 4 ? 2 : 1;
    return known >= 1 ? want : 0;
  }

  // Path entries of a round trip: the repeaters, the node, the repeaters back.
  int traceEntries() const { return 2 * _n + 1; }

  bool trace() {
    if (!_ping || _state == RUNNING) return false;
    uint8_t w = traceWidth();
    if (w == 0) { _state = FAILED; return false; }
    uint8_t path[TRACE_SNR_MAX * 4];
    int e = 0;
    auto put = [&](const uint8_t* key) { memcpy(&path[e * w], key, w); e++; };
    for (int i = 0; i < _n; i++) put(_hops[i].key);
    put(_target_key);
    for (int i = _n - 1; i >= 0; i--) put(_hops[i].key);
    if (_ping->startTrace(path, (uint8_t)e, w) != PingEngine::STARTED) { _state = FAILED; return false; }
    _state = RUNNING;
    _started_ms = millis();
    _entries = e;
    return true;
  }

  // Call every UI pass while a trace may be running.
  void poll() {
    if (_state != RUNNING || !_ping) return;
    if (!_ping->isActive()) {   // the reply came (the engine released its slot)
      _state = _ping->hopCount() > 0 ? DONE : TIMEOUT;
    } else if (millis() - _started_ms > _ping->timeoutMs()) {
      _ping->clear();
      _state = TIMEOUT;
    }
  }

  void cancelTrace() {
    if (_state == RUNNING && _ping) _ping->clear();
    if (_state == RUNNING) _state = IDLE;
  }

  // Result of the last trace: SNR x4 at path entry i (0..traceEntries()-1),
  // false where it wasn't heard. Entry n is the node, entries after it the
  // way back; snrBackX4() is us hearing the last retransmission.
  bool hopSnrX4(int entry, int16_t& snr_x4) const {
    if (_state != DONE || !_ping || entry < 0 || entry >= _ping->hopCount()) return false;
    snr_x4 = _ping->hopSnrX4(entry);
    return true;
  }
  bool snrBackX4(int16_t& snr_x4) const {
    if (_state != DONE || !_ping) return false;
    int16_t o, b; uint32_t r;
    _ping->getResult(o, b, r);
    snr_x4 = b;
    return true;
  }
  uint32_t rttMs() const {
    int16_t o, b; uint32_t r = 0;
    if (_ping) _ping->getResult(o, b, r);
    return r;
  }

  // The weakest SNR of the way there (repeaters and the node) in x4, for a
  // one-line verdict; false without a result.
  bool worstOutX4(int16_t& v) const {
    bool any = false;
    for (int i = 0; i <= _n; i++) {
      int16_t s;
      if (!hopSnrX4(i, s)) continue;
      if (!any || s < v) v = s;
      any = true;
    }
    return any;
  }

  // ── save ────────────────────────────────────────────────────────────────────
  // The list as the contact's stored route. False with an empty list (that is
  // Reset path / flood) or when the hops share no usable width.
  bool canSave() const { return _n > 0 && saveWidth() > 0; }
  bool save() {
    uint8_t w = saveWidth();
    if (_n == 0 || w == 0) return false;
    uint8_t path[MAX_HOPS * 3];
    for (int i = 0; i < _n; i++) memcpy(&path[i * w], _hops[i].key, w);
    if (!the_mesh.setContactPath(_target_key, path, w, (uint8_t)_n, _pinned)) return false;
    _stored = true;
    return true;
  }

  // Text of path entry i of a round trip for a result row: "A", "A >", etc. is
  // the view's job; this gives the name of the entry (a repeater, the node,
  // a repeater on the way back).
  const char* entryName(int i) const {
    if (i < _n) return _hops[i].name;
    if (i == _n) return _target_name;
    int back = 2 * _n - i;   // n+1 -> n-1 ... 2n -> 0
    return back >= 0 && back < _n ? _hops[back].name : "";
  }

private:
  uint8_t saveWidth() const {   // 1..3 bytes of a stored path, the hash mode narrowed to what every hop has
    uint8_t want = _prefs ? _prefs->path_hash_mode + 1 : 1;
    if (want > 3) want = 3;
    uint8_t known = 4;
    for (int i = 0; i < _n; i++) if (_hops[i].known < known) known = _hops[i].known;
    if (known > 3) known = 3;
    return want < known ? want : known;
  }

  void invalidate() {
    if (_state == RUNNING && _ping) _ping->clear();
    _state = IDLE;
  }

  // A hash of a stored path: the contact it belongs to by key prefix, else the bytes.
  void loadHash(const uint8_t* hash, uint8_t size) {
    Hop& h = _hops[_n++];
    memset(h.key, 0, sizeof(h.key));
    h.known = size > 4 ? 4 : size;
    memcpy(h.key, hash, h.known);
    h.name[0] = '\0';
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (c.type == ADV_TYPE_REPEATER && c.id.isHashMatch(hash, size)) {
        memcpy(h.key, c.id.pub_key, 4);
        h.known = 4;
        snprintf(h.name, sizeof(h.name), "%s", c.name);
        return;
      }
    }
    char hex[9] = {0};
    for (uint8_t k = 0; k < h.known; k++) snprintf(hex + k * 2, 3, "%02X", h.key[k]);
    snprintf(h.name, sizeof(h.name), "?%s", hex);
  }

  NodePrefs*  _prefs = nullptr;
  PingEngine* _ping = nullptr;
  uint8_t  _target_key[PUB_KEY_SIZE];
  char     _target_name[32];
  Hop      _hops[MAX_HOPS];
  int      _n = 0;
  State    _state = IDLE;
  bool     _stored = false;
  bool     _pinned = false;
  int      _entries = 0;
  uint32_t _started_ms = 0;
};
