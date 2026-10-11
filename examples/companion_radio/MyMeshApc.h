#pragma once
// Adaptive power control, and the relay tracking behind the UI's "relayed" mark.
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

// Adaptive Power Control. tx_power_dbm is the user-set ceiling; APC drives the
// radio's *actual* TX power within [APC_MIN_DBM, ceiling] to hold the link margin
// near a target. The link-quality signal is the SNR of a returning confirmation —
// either a direct-message ACK or our own channel/flood packet heard rebroadcast by
// a repeater (both are the *reverse* link, a good proxy on a roughly symmetric RF
// neighbourhood). No protocol change required.
//
// The margin is measured *above this SF's demodulation floor* (not an absolute
// SNR), so the same target works across SF7–SF12. A single SNR sample is noisy, so
// we smooth it with an EWMA and step proportionally to the error (capped), with a
// deadband to avoid hunting. A lost confirmation is an ambiguous signal for power,
// so we step up gradually and only jump to the ceiling after a short streak.
#define APC_MIN_DBM          -9     // lower bound (SX126x PA floor)
#define APC_TARGET_MARGIN_DB  6.0f  // desired SNR margin above the SF demod floor
#define APC_DEADBAND_DB       2.0f  // hold while the smoothed margin is within ±this
#define APC_EWMA_ALPHA        0.4f  // smoothing weight for each SNR sample
#define APC_MAX_STEP_DB       2     // cap on a single power adjustment (stability)
#define APC_FAIL_STEP_DB      4     // power bump on one lost confirmation
#define APC_FAIL_CEIL_HITS    2     // consecutive losses → jump straight to the ceiling
// How long to wait for a repeater to rebroadcast our channel/flood packet before
// treating it as un-heard (flood retransmit delays are randomised over a few s).
#define APC_FLOOD_ECHO_WINDOW_MS  6000

void MyMesh::applyApc() {
  _apc_cur_dbm = _prefs.tx_power_dbm;        // start at the ceiling
  _apc_margin_ewma = APC_TARGET_MARGIN_DB;   // assume on-target until samples say otherwise
  _apc_fail_count = 0;
  _apc_flood_pending = false;
  radio_driver.setTxPower(_apc_cur_dbm);
}

// Feed one reverse-link SNR sample (ACK or heard flood echo) into the controller.
void MyMesh::apcSampleSnr(float snr) {
  _apc_fail_count = 0;                        // a confirmation clears the failure streak

  float margin = snr - radio_driver.snrFloorForSF(_prefs.sf);
  _apc_margin_ewma += APC_EWMA_ALPHA * (margin - _apc_margin_ewma);   // smooth

  float err = _apc_margin_ewma - APC_TARGET_MARGIN_DB;   // +ve = more margin than needed
  if (fabsf(err) <= APC_DEADBAND_DB) return;             // inside deadband → hold

  int step = (int)lroundf(err * 0.5f);                   // proportional (~half the error)
  if (step >  APC_MAX_STEP_DB) step =  APC_MAX_STEP_DB;
  if (step < -APC_MAX_STEP_DB) step = -APC_MAX_STEP_DB;
  if (step == 0) return;

  int8_t target = _apc_cur_dbm - step;                   // surplus margin → lower power
  if (target < APC_MIN_DBM) target = APC_MIN_DBM;
  if (target > _prefs.tx_power_dbm) target = _prefs.tx_power_dbm;
  if (target == _apc_cur_dbm) return;

  int8_t delta = target - _apc_cur_dbm;
  _apc_cur_dbm = target;
  radio_driver.setTxPower(_apc_cur_dbm);
  _apc_margin_ewma += (float)delta;   // anticipate the margin shift (≈1 dB TX → 1 dB SNR)
}

// A send got no confirmation (missed ACK, or a flood no repeater echoed). Ramp up.
void MyMesh::apcOnFailure() {
  if (_apc_cur_dbm >= _prefs.tx_power_dbm) return;       // already at the ceiling

  int8_t target;
  if (++_apc_fail_count >= APC_FAIL_CEIL_HITS) {
    target = _prefs.tx_power_dbm;            // repeated losses → restore full reliability
  } else {
    target = _apc_cur_dbm + APC_FAIL_STEP_DB;
    if (target > _prefs.tx_power_dbm) target = _prefs.tx_power_dbm;
  }
  _apc_cur_dbm = target;
  _apc_margin_ewma = APC_TARGET_MARGIN_DB;   // reset so the next sample doesn't trim straight back
  radio_driver.setTxPower(_apc_cur_dbm);
}

// Remember a channel/flood packet we just originated so a heard rebroadcast can be
// matched as positive feedback (and its absence as a failure). The hash excludes
// the mutable path, so it matches across repeater rebroadcasts.
void MyMesh::apcTrackFloodSend(const mesh::Packet* pkt) {
  pkt->calculatePacketHash(_apc_flood_hash);
  _apc_flood_len = pkt->payload_len;
  _apc_flood_deadline = futureMillis(APC_FLOOD_ECHO_WINDOW_MS);
  _apc_flood_pending = true;
}

// Arm the UI "relayed into mesh" tracker for a channel send (independent of APC).
// A repeater rebroadcast heard within the window = relayed; no echo = simply not
// shown as relayed (NOT a failure — direct/0-hop neighbours never echo).
void MyMesh::trackRelaySend(const mesh::Packet* pkt) {
  RelaySlot& s = _relay[_relay_head];
  if (!s.pending) _relay_active++;   // overwriting an empty slot adds one pending
  pkt->calculatePacketHash(s.hash);
  s.len = pkt->payload_len;
  s.deadline = futureMillis(APC_FLOOD_ECHO_WINDOW_MS);
  _relay_seq = (_relay_seq == 0xFFFFFFFFu) ? 1 : _relay_seq + 1;   // never 0 (0 = "no relay")
  s.seq = _relay_seq;
  s.pending = true;
  _last_relay_seq = _relay_seq;
  _relay_head = (_relay_head + 1) % RELAY_RING;
}
