#pragma once
// Packet flow: what the radio hears, as lines on the map -- a picture of the
// mesh at work, nothing more. Every packet heard (MyMesh::logRx) that can be
// placed becomes a flow: the node it came from, the repeaters it went
// through, this device; a packet sent is a pulse here. The frontends draw the
// recent flows over their map (ui-lvgl animates them, ui-new draws the lines).
//
// Placing a packet:
//   - its path (a flood's repeaters, in order) -- each hash is matched to a
//     repeater contact with a position; with several matches, the one nearest
//     the point before it;
//   - its origin, when the packet says: an advert carries the sender's key
//     and position, a direct message / request / response / path the
//     sender's one-byte hash (matched like a hop, to any contact);
//   - its end: this device (the GPS, else the position set in Privacy).
// Points without a position are skipped, so a flow may be shorter than its
// path or none at all (a group message from a node in direct range).
// A direct packet heard on its way elsewhere carries the hops still ahead:
// a pulse at the next one.
//
// Off (NodePrefs::flow_show 0) nothing is resolved: a contact scan per hop is
// cheap, but not free on every packet.
//
// Header-only Core, single TU (see UiCore.h).

class PacketFlow {
public:
  static const int MAX_FLOWS = 12;
  static const int MAX_PTS = 10;   // origin, up to 8 repeaters, this device

  enum Kind : uint8_t { K_ADVERT, K_MSG, K_GROUP, K_OTHER, K_SENT, K_AHEAD };

  struct Flow {
    uint32_t t_ms;        // when it was heard
    uint8_t  kind;
    uint8_t  n;           // points (1 for a pulse)
    bool     origin;      // point 0 is the sender (else the first repeater placed)
    int32_t  lat[MAX_PTS], lon[MAX_PTS];
  };

  void begin(NodePrefs* prefs, CourseEngine* course, SensorManager* sensors) {
    _prefs = prefs; _course = course; _sensors = sensors;
  }

  bool enabled() const { return _prefs && _prefs->flow_show; }

  // Changes with every flow added: a view repaints when it moves.
  uint32_t seq() const { return _seq; }
  int count() const { return _n; }
  // i = 0 is the newest.
  const Flow& at(int i) const { return _flows[(_head + MAX_FLOWS - 1 - i) % MAX_FLOWS]; }
  // Anything younger than `life_ms`?
  bool active(uint32_t life_ms) const { return _n > 0 && millis() - at(0).t_ms < life_ms; }

  void onHeard(const mesh::Packet* pkt) {
    if (!enabled() || !pkt) return;
    Flow f;
    f.t_ms = millis();
    f.n = 0;
    f.origin = false;
    const uint8_t type = pkt->getPayloadType();
    f.kind = type == PAYLOAD_TYPE_ADVERT ? K_ADVERT
           : type == PAYLOAD_TYPE_TXT_MSG ? K_MSG
           : type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA ? K_GROUP : K_OTHER;
    const uint8_t hs = pkt->getPathHashSize(), hc = pkt->getPathHashCount();

    if (pkt->isRouteDirect()) {   // on its way elsewhere: a pulse at the next hop
      int32_t lat, lon;
      if (hc == 0 || !placeHash(pkt->path, hs, false, 0, 0, false, lat, lon)) return;
      f.kind = K_AHEAD;
      add(f, lat, lon);
      push(f);
      return;
    }

    // Back from this device: each hash nearest the point after it, which
    // it was heard by -- the last repeater is the one heard here.
    int32_t lat[MAX_PTS], lon[MAX_PTS];
    int k = MAX_PTS;
    int32_t ola, olo;
    const bool own = ownPosition(ola, olo);
    if (own) { k--; lat[k] = ola; lon[k] = olo; }
    for (int i = hc - 1; i >= 0 && k > 1; i--) {
      int32_t a, b;
      const bool near = k < MAX_PTS;
      if (placeHash(&pkt->path[i * hs], hs, true, near ? lat[k] : 0, near ? lon[k] : 0, near, a, b)) {
        k--; lat[k] = a; lon[k] = b;
      }
    }
    int32_t a, b;
    if (k > 0 && origin(pkt, k < MAX_PTS, k < MAX_PTS ? lat[k] : 0, k < MAX_PTS ? lon[k] : 0, a, b)) {
      k--; lat[k] = a; lon[k] = b;
      f.origin = true;
    }
    for (int i = k; i < MAX_PTS; i++) add(f, lat[i], lon[i]);
    if (f.n >= 2) push(f);
  }

  void onSent(const mesh::Packet* pkt) {
    if (!enabled() || !pkt) return;
    int32_t lat, lon;
    if (!ownPosition(lat, lon)) return;
    Flow f;
    f.t_ms = millis();
    f.kind = K_SENT;
    f.n = 0;
    f.origin = true;
    add(f, lat, lon);
    push(f);
  }

  void clear() { _n = 0; _head = 0; _seq++; }

private:
  static void add(Flow& f, int32_t lat, int32_t lon) {
    if (f.n > 0 && f.lat[f.n - 1] == lat && f.lon[f.n - 1] == lon) return;   // the same spot twice
    f.lat[f.n] = lat; f.lon[f.n] = lon; f.n++;
  }

  void push(const Flow& f) {
    _flows[_head] = f;
    _head = (_head + 1) % MAX_FLOWS;
    if (_n < MAX_FLOWS) _n++;
    _seq++;
  }

  bool ownPosition(int32_t& lat, int32_t& lon) const {
    if (_course && _course->currentLocation(lat, lon)) return true;
    if (!_sensors) return false;
    lat = (int32_t)lround(_sensors->node_lat * 1e6);
    lon = (int32_t)lround(_sensors->node_lon * 1e6);
    return lat != 0 || lon != 0;
  }

  // Where the packet came from, when it says (a one-byte hash: the contact
  // nearest the point after it, when there is one).
  static bool origin(const mesh::Packet* pkt, bool near, int32_t nlat, int32_t nlon, int32_t& lat, int32_t& lon) {
    const uint8_t type = pkt->getPayloadType();
    if (type == PAYLOAD_TYPE_ADVERT) {
      const int app = PUB_KEY_SIZE + 4 + SIGNATURE_SIZE;   // key, timestamp, signature, then the app data
      if (pkt->payload_len > app) {
        AdvertDataParser p(&pkt->payload[app], pkt->payload_len - app);
        if (p.isValid() && p.hasLatLon() && (p.getIntLat() || p.getIntLon())) {
          lat = p.getIntLat(); lon = p.getIntLon();
          return true;
        }
      }
      ContactInfo* c = the_mesh.lookupContactByPubKey(pkt->payload, PUB_KEY_SIZE);
      if (c && (c->gps_lat || c->gps_lon)) { lat = c->gps_lat; lon = c->gps_lon; return true; }
      return false;
    }
    if ((type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE ||
         type == PAYLOAD_TYPE_PATH) && pkt->payload_len >= 2)
      return placeHash(&pkt->payload[1], 1, false, nlat, nlon, near, lat, lon);   // [dest hash, src hash, ...]
    return false;
  }

  // A contact with a position whose key starts with `hash` (`size` bytes):
  // repeaters only when `repeaters`; with several, the nearest to (nlat, nlon)
  // when `near`, else the first.
  static bool placeHash(const uint8_t* hash, uint8_t size, bool repeaters, int32_t nlat, int32_t nlon, bool near,
                        int32_t& lat, int32_t& lon) {
    bool found = false;
    float best = 0;
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (repeaters && c.type != ADV_TYPE_REPEATER) continue;
      if ((!c.gps_lat && !c.gps_lon) || memcmp(c.id.pub_key, hash, size) != 0) continue;
      float d = near ? geo::haversineKm(nlat, nlon, c.gps_lat, c.gps_lon) : 0;
      if (!found || d < best) { best = d; lat = c.gps_lat; lon = c.gps_lon; found = true; }
      if (!near) break;
    }
    return found;
  }

  NodePrefs*     _prefs = nullptr;
  CourseEngine*  _course = nullptr;
  SensorManager* _sensors = nullptr;
  Flow     _flows[MAX_FLOWS];
  int      _head = 0, _n = 0;
  uint32_t _seq = 0;
};
