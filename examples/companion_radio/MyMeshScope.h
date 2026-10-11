#pragma once
// The shared scope list: the primary / default scope, per-channel scopes and
// the repeat scopes they make.
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

void MyMesh::setPrimaryScope(const char* name) {
  // Keep the legacy fields in sync too -- inert for rebuildRepeatScopes()
  // once /scopes1 exists, but still what a pre-list save file round-trips,
  // and cheap to maintain.
  strncpy(_prefs.default_scope_name, name, sizeof(_prefs.default_scope_name) - 1);
  _prefs.default_scope_name[sizeof(_prefs.default_scope_name) - 1] = '\0';
  if (_prefs.default_scope_name[0] == '\0') {
    memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
  } else {
    char hashtag[1 + sizeof(_prefs.default_scope_name)];
    snprintf(hashtag, sizeof(hashtag), "#%s", _prefs.default_scope_name);
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, hashtag, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }

  if (_prefs.default_scope_name[0] == '\0') {
    _scope_list.default_idx = 0;   // "*"
  } else {
    // Find-or-create a matching named entry (e.g. the app driving this
    // remotely via CMD_SET_DEFAULT_FLOOD_SCOPE), then mark it default.
    uint8_t idx = 0;
    for (uint8_t i = 0; i < _scope_list.count; i++) {
      if (strcmp(_scope_list.entries[i].name, _prefs.default_scope_name) == 0) { idx = i + 1; break; }
    }
    if (idx == 0) idx = _scope_list.add(_prefs.default_scope_name);
    // add() returns 0 only when the list is full. Keep the previous default
    // rather than taking that as "*": silently dropping to unscoped would put
    // traffic on the air outside any region, which is a louder failure than
    // ignoring a request we had no room to honour.
    if (idx != 0) _scope_list.default_idx = idx;
  }
  if (_store) _store->saveScopeList(_scope_list);
  rebuildRepeatScopes();
}

void MyMesh::syncLegacyDefaultScope() {
  uint8_t idx = _scope_list.default_idx;
  if (idx == 0) {   // "*" -- no default scope, same as the field never being set
    memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));
    memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
  } else {
    const ScopeEntry& e = _scope_list.entries[idx - 1];
    StrHelper::strncpy(_prefs.default_scope_name, e.name, sizeof(_prefs.default_scope_name));
    memcpy(_prefs.default_scope_key, e.key, sizeof(_prefs.default_scope_key));
  }
}

uint8_t MyMesh::addScope(const char* name) {
  uint8_t idx = _scope_list.add(name);
  if (idx && _store) _store->saveScopeList(_scope_list);
  return idx;
}

void MyMesh::renameScope(uint8_t idx, const char* name) {
  if (idx < 1 || idx > _scope_list.count || !name || !name[0]) return;
  ScopeEntry& e = _scope_list.entries[idx - 1];
  StrHelper::strncpy(e.name, name, sizeof(e.name));
  ScopeList::deriveKey(e.name, e.key);
  if (_store) _store->saveScopeList(_scope_list);
  if (_scope_list.default_idx == idx) {   // renaming the default changes its key too
    syncLegacyDefaultScope();
    savePrefs();
  }
  rebuildRepeatScopes();   // this entry's key may be repeat_scopes[]'s default or an extra slot
}

void MyMesh::removeScope(uint8_t idx) {
  if (idx < 1 || idx > _scope_list.count) return;
  _scope_list.remove(idx);
  // repeat_extra_scope_mask bit (i) tracks list index (i+1) -- shift down the
  // same way ScopeList::remove() shifted entries[], dropping the removed bit.
  uint16_t old_mask = _prefs.repeat_extra_scope_mask, new_mask = 0;
  for (uint8_t i = 0; i < ScopeList::MAX_SCOPE_ENTRIES; i++) {   // one bit per possible list entry, not per MAX_REPEAT_SCOPES active slot
    if (!(old_mask & (1u << i))) continue;
    uint8_t list_idx = i + 1;
    if (list_idx < idx)      new_mask |= (1u << i);
    else if (list_idx > idx) new_mask |= (1u << (i - 1));
    // list_idx == idx: the removed one, dropped
  }
  _prefs.repeat_extra_scope_mask = new_mask;
  // Any channel pointed at the removed entry (or shifted ones) needs the same
  // fix-up ScopeList::remove() applied to default_idx.
  for (uint8_t i = 0; i < NodePrefs::MAX_SCOPED_CHANNELS; i++) {
    uint8_t ci = _prefs.ch_scope_idx[i];
    if (ci == NodePrefs::CH_SCOPE_DEFAULT) continue;   // follows the default, fixed up above
    if (ci == idx) _prefs.ch_scope_idx[i] = 0;
    else if (ci > idx) _prefs.ch_scope_idx[i] = ci - 1;
  }
  // Live Share's own scope (stored as list index + 1; 0 = follow the target).
  if (_prefs.loc_share_scope) {
    uint8_t li = _prefs.loc_share_scope - 1;
    if (li == idx)     _prefs.loc_share_scope = 0;
    else if (li > idx) _prefs.loc_share_scope--;
  }
  syncLegacyDefaultScope();   // ScopeList::remove() may have moved or cleared the default
  if (_store) _store->saveScopeList(_scope_list);
  // The fix-ups above live in NodePrefs, not in /scopes1, so both files have to
  // be written here. Saving only the list would leave the two out of step after
  // a reboot: the entries would be shifted down but every channel's saved index
  // (and the repeater's mask) would still point at the pre-delete positions, so
  // they'd silently resolve to the wrong scope -- the same class of bug the
  // stray-bits clamp in DataStore guards against, just from the other side.
  savePrefs();
  rebuildRepeatScopes();
}

void MyMesh::setDefaultScope(uint8_t idx) {
  _scope_list.default_idx = _scope_list.clamp(idx);
  syncLegacyDefaultScope();   // so CMD_GET_DEFAULT_FLOOD_SCOPE answers with what we now use
  if (_store) _store->saveScopeList(_scope_list);
  savePrefs();
  rebuildRepeatScopes();
}

void MyMesh::setChannelScope(uint8_t channel_idx, uint8_t idx) {
  if (channel_idx >= NodePrefs::MAX_SCOPED_CHANNELS) return;
  _prefs.ch_scope_idx[channel_idx] = idx == NodePrefs::CH_SCOPE_DEFAULT ? idx : _scope_list.clamp(idx);
}

void MyMesh::rebuildRepeatScopes() {
  repeat_scope_count = 0;

  TransportKey primary = _scope_list.key(_scope_list.default_idx);
  if (!primary.isNull()) repeat_scopes[repeat_scope_count++] = primary;

  for (uint8_t i = 0; i < _scope_list.count && repeat_scope_count < MAX_REPEAT_SCOPES; i++) {
    if (!(_prefs.repeat_extra_scope_mask & (1u << i))) continue;
    uint8_t list_idx = i + 1;
    TransportKey k = _scope_list.key(list_idx);
    if (!k.isNull()) repeat_scopes[repeat_scope_count++] = k;
  }
}
