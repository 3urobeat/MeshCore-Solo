#pragma once
// Node discovery and ping (trace) from the UI.
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ;
  data[1] = (1 << ADV_TYPE_REPEATER) | (1 << ADV_TYPE_SENSOR) | (1 << ADV_TYPE_ROOM);
  getRNG()->random(&data[2], 4);
  memcpy(&_pending_node_discover_tag, &data[2], 4);
  _pending_node_discover_until = futureMillis(8000);
  _discover_count = 0;
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);
  auto pkt = createControlData(data, sizeof(data));
  if (pkt) sendZeroHop(pkt);
}

int MyMesh::getDiscoverResults(DiscoverResult dest[], int max_count) {
  int n = min(_discover_count, max_count);
  memcpy(dest, _discover_results, n * sizeof(DiscoverResult));
  return n;
}

// ── Ping/Trace functionality ─────────────────────────────────────────────────

uint32_t MyMesh::sendTrace(const uint8_t* path, uint8_t hops, uint8_t hash_width, uint32_t* est_timeout) {
  // A trace's hashes are 1 << flags bytes wide: 1, 2, 4 (no 3).
  if (hash_width != 1 && hash_width != 2 && hash_width != 4) return 0;
  if (hops == 0 || hops > TRACE_SNR_MAX || hops * hash_width > MAX_PATH_SIZE) return 0;

  // Generate random tag and auth code
  uint32_t tag, auth;
  getRNG()->random((uint8_t*)&tag, 4);
  getRNG()->random((uint8_t*)&auth, 4);

  // Find a free slot in ping results
  int slot = -1;
  for (int i = 0; i < PING_RESULT_MAX; i++) {
    if (!_ping_results[i].received && _ping_results[i].tag == 0) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return 0;
  }

  // Initialize ping result tracking
  PingResult& result = _ping_results[slot];
  memset(&result, 0, sizeof(result));
  result.tag = tag;
  result.auth_code = auth;
  result.sent_ms = millis();

  auto pkt = createTrace(tag, auth, hash_width == 4 ? 2 : hash_width - 1);   // flags = log2(hash_width)
  if (pkt) {
    sendDirect(pkt, path, hops * hash_width);
    if (est_timeout) {   // as CMD_SEND_TRACE_PATH answers the app: from the airtime, per hop
      uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
      *est_timeout = calcDirectTimeoutMillisFor(t, hops);
    }
    return tag;
  }

  // Failed to create packet
  memset(&result, 0, sizeof(result));
  return 0;
}

// Ping: a trace over just the destination's hash (one hop, it must be heard directly).
uint32_t MyMesh::sendPing(const uint8_t* dest_pubkey, uint8_t hash_width, uint32_t* est_timeout) {
  if (hash_width != 1 && hash_width != 2 && hash_width != 4) return 0;
  return sendTrace(dest_pubkey, 1, hash_width, est_timeout);
}

void MyMesh::setPingCallback(PingCallback cb, void* arg) {
  _ping_callback = cb;
  _ping_callback_arg = arg;
}

void MyMesh::clearPingResult(uint32_t tag) {
  if (tag == 0) return;
  for (int i = 0; i < PING_RESULT_MAX; i++) {
    if (_ping_results[i].tag == tag) {
      memset(&_ping_results[i], 0, sizeof(_ping_results[i]));
      return;
    }
  }
}

MyMesh::PingResult* MyMesh::getPingResult(uint32_t tag) {
  for (int i = 0; i < PING_RESULT_MAX; i++) {
    if (_ping_results[i].tag == tag) {
      return &_ping_results[i];
    }
  }
  return NULL;
}

// True when this (tag, responder) pair was already seen recently; records it
// otherwise. A discover response is often heard more than once — the zero-hop
// direct copy and a re-flooded copy relayed by another repeater carry
// different packet hashes, so the mesh duplicate filter passes both. Without
// this, the standalone scan appended the same node twice and an app-driven
// discover had both copies forwarded, so the app listed one repeater as two.
bool MyMesh::isDupDiscoverResp(uint32_t tag, const uint8_t* pub_key) {
  for (int i = 0; i < DISCOVER_SEEN_MAX; i++) {
    DiscoverSeen& e = _disc_seen[i];
    if (e.until != 0 && !millisHasNowPassed(e.until)
        && e.tag == tag && memcmp(e.pk, pub_key, sizeof(e.pk)) == 0) {
      return true;
    }
  }
  DiscoverSeen& s = _disc_seen[_disc_seen_head];
  _disc_seen_head = (_disc_seen_head + 1) % DISCOVER_SEEN_MAX;
  s.tag = tag;
  memcpy(s.pk, pub_key, sizeof(s.pk));
  s.until = futureMillis(10000);   // copies of one response arrive within seconds
  return false;
}
