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
  void botSetGPS(bool on) override;

  // ── Navigation (called from LVGL event callbacks) ────────────────────────
  void showHome();
  void setHomePage(int page);
  void showChats();
  void showContacts();
  void showSettings();
  void showSchemaSettings(int page);
  void pruneContacts();
  void setBrightnessPct(uint8_t pct, bool save);
  void applyDisplayPrefs() override;
  void setSchemaValue(int idx, int v);
  void showNearby();
  void showMap();                  // in its current mode
  void openMap(bool nav);          // Nodes map (false) or Navigation map (true)
  void mapLongPress(int x, int y);
  void mapZoom(int delta);
  void mapCenterOnMe();
  void mapPan(int dx, int dy);
  void mapOpenMarker(int idx);
  void mapDownloadPopup();
  void mapDownloadClose();
  void mapDownloadStart();
  void mapDownloadStop();
  void mapDownloadResume();
  void mapDownloadDiscard();
  void mapDownloadZmax(int delta);
  // Navigation map (NavMap.h)
  void navTargetsPopup();
  void navClosePopup();
  void navPick(int code);
  void navWaypointMenu(int idx);
  void navWaypointAction(uint8_t act);
  void navRenameDone(bool ok);
  void navMarkHere();
  void navAveragingCancel();
  void navToNode(const uint8_t* key, int32_t lat, int32_t lon, const char* name);
  void navToolsPopup();
  void navToolAction(uint8_t act);
  void navSetShareTarget(int sel);
  void navWaypointsPopup();
  void navCoordsPopup();
  void navCoordsDone(bool ok);
  void shareToMessage(const char* text);
  void messageLocationAction(int idx, bool save);
  void showWifi(bool from_map);
  void wifiScan();
  void wifiPick(int idx);
  void wifiSave();
  void wifiEdit(lv_obj_t* ta);
  void wifiKeyboardHide();
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
  // Clock tools (ClockScreen.h)
  void showClock();
  void clockTab(int tab);
  void clockAction(uint8_t act);
  void setAlarm(int which, int v);
  void dismissRing();
  // Settings > Radio (RadioScreen.h)
  void showRadio();
  void radioSet(int which, int v);
  void radioFreqPopup();
  void radioFreqDone(bool ok);
  // Channels (ChannelScreen.h)
  void channelMenu(int idx);
  void channelSet(uint8_t which, int v);
  void channelAction(uint8_t act);
  void showChannelEdit(int idx);
  void channelEditType(int type);
  void channelEditHex(bool hex);
  void channelEditField(lv_obj_t* ta);
  void channelEditKbHide();
  void channelEditSave();
  void setGps(bool on);
  bool ensureGps();   // true with a fix; else turns GPS on / says it's waiting

  // Kept for main.cpp's sim hooks (ui-new API).
  void openContactDM(const ContactInfo& ci) { openDM(ci.id.pub_key); }
  void openAdminFor(const ContactInfo& ci, bool from_picker) { (void)ci; (void)from_picker; }

private:
  enum Screen : uint8_t { SCR_HOME, SCR_CHATS, SCR_CONTACTS, SCR_THREAD, SCR_SETTINGS, SCR_NEARBY, SCR_NODE, SCR_MAP, SCR_WIFI, SCR_SETTINGS_NAV, SCR_CLOCK, SCR_RADIO, SCR_CHANNEL_EDIT };

  void buildStatusBar();
  void refreshStatusBar();
  lv_obj_t* newScreen(const char* title, bool with_back);
  void buildHome();
  void refreshHome();
  void homeSwipePoll();
  void buildChats();
  void buildContacts();
  void buildSettings();
  void buildSchemaSettings();
  void buildNearby();
  void refreshNearbyList();
  uint32_t nearbySignature() const;
  void showScanPopup();
  void refreshScanPopup();
  void buildMap();
  void layoutMap();
  void mapLoop();
  void rebuildMapMarkers();
  void rebuildNodeMarkers();
  void addMapMark(uint8_t kind, int idx, int32_t lat_e6, int32_t lon_e6, uint32_t col, const char* text);
  void mapPointToLatLon(int x, int y, int32_t& lat_e6, int32_t& lon_e6) const;
  double mapCenterBias() const;
  void buildNavLayers();
  void buildNavControls(lv_obj_t* body);
  void rebuildNavMarkers();
  void layoutNav();
  void refreshNavBar();
  void navFrameTarget();
  void navSetTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name);
  lv_obj_t* navPopupPanel(const char* title, bool full);
  void navRenamePopup(int idx);
  void navAddWaypoint(int32_t lat, int32_t lon);
  void navDropAt(int x, int y);
  void navSpotPopup(int32_t lat, int32_t lon);
  void refreshNavTools();
  void navPollAveraging();
  void navPollTrackBack();
  bool navCurrentTarget(int32_t& lat, int32_t& lon, char* name, int n, bool& person, bool& set);
  bool exportTrailGpx(char* name_out, size_t n);
  void mapDownloadTick();
  void refreshDownloadPopup();
  void buildWifi();
  void pollWifiScan();
  void buildNode();
  void refreshNode();
  void buildThread();
  void refreshThread();
  uint32_t threadSignature() const;
  void buildClock();
  void buildRadio();
  void radioCloseFreq();
  void buildChannelEdit();
  bool radioPopupOpen() const;
  void refreshClock();
  void showRing(const char* text);
  void hideRing();
  void drainCoreEvents();
  void onMessageArrived(const UiEvent& ev);
  void wake();
  void sleep();
  uint32_t autoOffMillis() const;
  void checkLowBattery();

  DisplayDriver* _display = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs*     _prefs = nullptr;
  UiCore*        _core = nullptr;

  Screen   _screen = SCR_HOME;
  bool     _asleep = false;
  uint32_t _next_status_ms = 0;
  uint32_t _next_thread_check_ms = 0;
  uint32_t _next_trackback_ms = 0;
  uint32_t _next_clock_ms = 0;
  uint8_t  _settings_page = 0;   // settings::Page shown in SCR_SETTINGS_NAV
  uint32_t _prune_armed_ms = 0;
  uint16_t _batt_mv = 0;         // smoothed, for the low-battery shutdown
  uint32_t _next_batt_ms = 0;
  lv_obj_t* _prune_lbl = nullptr;

  // Open conversation (SCR_THREAD)
  bool     _thread_is_channel = false;
  uint8_t  _thread_channel = 0;
  uint8_t  _thread_key[PUB_KEY_SIZE] = {0};
  bool     _thread_dirty = false;
  uint32_t _thread_sig = 0;

  // Widgets
  lv_obj_t* _status_time = nullptr;
  lv_obj_t* _status_icons = nullptr;
  lv_obj_t* _status_gps = nullptr;
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
  bool      _node_from_map = false;

  // Map (SCR_MAP): view centre in fractional tile coords at zoom _map_z
  double    _map_cx = 0, _map_cy = 0;
  int       _map_z = 0;             // 0 = not initialised yet
  bool      _map_follow = true;     // keep centred on own position until panned
  bool      _map_pending = false;   // visible tiles still to decode
  uint32_t  _next_map_marks_ms = 0;
  lv_obj_t* _map_area = nullptr;
  lv_obj_t* _map_tiles[6] = {nullptr};
  lv_obj_t* _map_marks = nullptr;
  lv_obj_t* _map_me = nullptr;
  lv_obj_t* _map_zoom_lbl = nullptr;
  lv_obj_t* _map_hint = nullptr;
  lv_obj_t* _map_dl_pill = nullptr;     // download progress over the map
  lv_obj_t* _dl_overlay = nullptr;      // download popup
  lv_obj_t* _dl_info = nullptr;
  lv_obj_t* _dl_zoom_lbl = nullptr;
  lv_obj_t* _dl_start_lbl = nullptr;
  lv_obj_t* _dl_job_row = nullptr;      // "Unfinished ... Resume" in the popup
  lv_obj_t* _dl_job_lbl = nullptr;
  int       _dl_zmax = 0;
  uint8_t   _dl_last_state = 0;

  // Navigation map (SCR_MAP with _map_nav)
  bool      _map_nav = false;
  uint32_t  _next_nav_bar_ms = 0;
  lv_obj_t* _nav_bar = nullptr;       // target bar along the bottom
  lv_obj_t* _nav_title = nullptr;
  lv_obj_t* _nav_info = nullptr;
  lv_obj_t* _nav_clear = nullptr;
  lv_obj_t* _nav_overlay = nullptr;   // target list / waypoint menu / rename
  lv_obj_t* _nav_ta = nullptr;
  lv_obj_t* _nav_kb = nullptr;
  lv_obj_t* _nav_del_lbl = nullptr;
  uint32_t  _nav_del_armed_ms = 0;
  int       _nav_wp = -1;             // waypoint the menu / rename is about
  lv_obj_t* _nav_rec = nullptr;       // "REC 1.2 km  LIVE 58m" pill
  lv_obj_t* _nav_avg_pill = nullptr;  // GPS averaging progress (tap: cancel)
  lv_obj_t* _nav_trail_lbl = nullptr; // tools panel
  lv_obj_t* _nav_trail_btn = nullptr;
  lv_obj_t* _nav_reset_lbl = nullptr;
  lv_obj_t* _nav_share_lbl = nullptr;
  lv_obj_t* _nav_share_btn = nullptr;
  lv_obj_t* _nav_tb_btn = nullptr;
  uint32_t  _nav_reset_armed_ms = 0;
  int32_t   _nav_spot_lat = 0, _nav_spot_lon = 0;   // the spot a long-press picked
  char      _share_text[96] = "";     // waiting for a conversation to be picked (shareToMessage)

  // WiFi settings (SCR_WIFI)
  bool      _wifi_from_map = false;
  bool      _wifi_scanning = false;
  lv_obj_t* _wifi_ssid = nullptr;
  lv_obj_t* _wifi_pass = nullptr;
  lv_obj_t* _wifi_kb = nullptr;
  lv_obj_t* _wifi_list = nullptr;
  lv_obj_t* _wifi_status = nullptr;
};
