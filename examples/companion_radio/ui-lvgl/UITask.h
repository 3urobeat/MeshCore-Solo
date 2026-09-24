#pragma once
// ui-lvgl: the rich (colour + touch) frontend of the UI Core. Wio Tracker L2
// first. All application state lives in the Core (../ui-core); this class owns
// only LVGL screens, display power and input. See docs/development/ui-core.md.

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <lvgl.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../ui-core/UiCoreHost.h"

class UiCore;
class NearbyModel;
struct UiEvent;

class UITask : public UITaskBase, public UiCoreHost {
public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : UITaskBase(board, serial) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void loop() override;
  void notify(UIEventType t = UIEventType::none) override { (void)t; }   // no speaker driver yet
  void shutdown(bool restart = false) override;
  MyMesh::Listener* meshListener() override;

  // UiCoreHost
  bool isViewingChannel(uint8_t channel_idx) override;
  bool isViewingDM(const uint8_t* pub_key) override;
  void onViewedHistoryGrew(bool channel) override { (void)channel; _thread_dirty = true; }

  // ── Navigation (called from LVGL event callbacks) ────────────────────────
  void showHome();
  void showChats();
  void showContacts();
  void showSettings();
  void showNearby();
  void setNearbyFilter(uint8_t f);
  void toggleNearbySort();
  void startNearbyScan();
  void closeScanPopup();
  void openScanNode(int row);
  void openNode(int row);
  void nodeAction(uint8_t action);
  void setKeyboardAlphabets(int main_idx, int alt_sel);
  void openChannel(uint8_t channel_idx);
  void openDM(const uint8_t* pub_key);
  void back();
  void sendFromCompose();
  void setKeyboardVisible(bool show);
  void showToast(const char* text, uint32_t ms = 2500);

  // Kept for main.cpp's sim hooks (ui-new API).
  void openContactDM(const ContactInfo& ci) { openDM(ci.id.pub_key); }
  void openAdminFor(const ContactInfo& ci, bool from_picker) { (void)ci; (void)from_picker; }

private:
  enum Screen : uint8_t { SCR_HOME, SCR_CHATS, SCR_CONTACTS, SCR_THREAD, SCR_SETTINGS, SCR_NEARBY, SCR_NODE };

  void buildStatusBar();
  void refreshStatusBar();
  lv_obj_t* newScreen(const char* title, bool with_back);
  void buildHome();
  void refreshHome();
  void buildChats();
  void buildContacts();
  void buildSettings();
  void buildNearby();
  void refreshNearbyList();
  uint32_t nearbySignature() const;
  void showScanPopup();
  void refreshScanPopup();
  void buildNode();
  void refreshNode();
  void buildThread();
  void refreshThread();
  uint32_t threadSignature() const;
  void drainCoreEvents();
  void onMessageArrived(const UiEvent& ev);
  void wake();
  void sleep();
  uint32_t autoOffMillis() const;

  DisplayDriver* _display = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs*     _prefs = nullptr;
  UiCore*        _core = nullptr;

  Screen   _screen = SCR_HOME;
  bool     _asleep = false;
  uint32_t _next_status_ms = 0;
  uint32_t _next_thread_check_ms = 0;

  // Open conversation (SCR_THREAD)
  bool     _thread_is_channel = false;
  uint8_t  _thread_channel = 0;
  uint8_t  _thread_key[PUB_KEY_SIZE] = {0};
  bool     _thread_dirty = false;
  uint32_t _thread_sig = 0;

  // Widgets
  lv_obj_t* _status_time = nullptr;
  lv_obj_t* _status_icons = nullptr;
  lv_obj_t* _toast = nullptr;
  lv_timer_t* _toast_timer = nullptr;
  lv_obj_t* _home_clock = nullptr;
  lv_obj_t* _home_date = nullptr;
  lv_obj_t* _home_unread = nullptr;
  lv_obj_t* _header = nullptr;    // current screen's title bar (nullptr on Home)
  lv_obj_t* _body = nullptr;
  lv_obj_t* _thread_list = nullptr;
  lv_obj_t* _compose_ta = nullptr;
  lv_obj_t* _keyboard = nullptr;

  // Nearby (SCR_NEARBY) / node detail (SCR_NODE); list data in NearbyModel
  NearbyModel* _nearby = nullptr;
  NearbyModel* _scan = nullptr;    // NODE_DISCOVER results: nodes in range now (popup over the list)
  bool      _scanning = false;
  bool      _node_from_scan = false;
  uint32_t  _scan_sig = 0;
  lv_obj_t* _scan_overlay = nullptr;
  lv_obj_t* _scan_list = nullptr;
  lv_obj_t* _scan_status = nullptr;
  uint32_t  _scan_until_ms = 0;
  uint32_t  _nearby_sig = 0;
  uint32_t  _next_nearby_ms = 0;
  bool      _pinging = false;
  uint32_t  _ping_started_ms = 0;
  uint32_t  _delete_armed_ms = 0;
  lv_obj_t* _nearby_list = nullptr;
  lv_obj_t* _nearby_status = nullptr;
  lv_obj_t* _nearby_sort_lbl = nullptr;
  lv_obj_t* _nearby_chips = nullptr;
  lv_obj_t* _node_info = nullptr;
  lv_obj_t* _node_ping = nullptr;
  lv_obj_t* _node_delete_lbl = nullptr;
};
