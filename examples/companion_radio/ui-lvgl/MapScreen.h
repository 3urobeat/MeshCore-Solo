#pragma once
// Map screen: offline Web-Mercator tiles from the card (map/TileProvider.h,
// cached by map/TileCache.h) under own position and the nodes Nearby knows a
// position for. Drag to pan, +/- to zoom, the crosshair re-centres and
// follows the GPS again; tap a marker for its node detail.
//
// Tiles decode one per loop pass (a PNG takes tens of ms), so panning stays
// responsive and the radio keeps being serviced while a view fills in.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after the Nearby section.

#include <math.h>
#include "map/TileProvider.h"
#include "map/TileCache.h"

namespace mapview {

static RasterTileProvider s_raster("/sdcard/maps");
static TileProvider*      s_provider = &s_raster;   // the one place to swap in a vector renderer
static TileCache          s_cache;

static const int MIN_Z = 3, MAX_Z = 18;
static const int GRID_COLS = 3, GRID_ROWS = 2;   // covers 320x218 at any offset

// Fallback centre with no GPS fix and no saved position: Poland.
static const double DEFAULT_LAT = 52.0, DEFAULT_LON = 19.4;
static const int    DEFAULT_Z = 6;

static double lonToTileX(double lon, int z) { return (lon + 180.0) / 360.0 * (double)(1 << z); }
static double latToTileY(double lat, int z) {
  double r = lat * M_PI / 180.0;
  return (1.0 - asinh(tan(r)) / M_PI) / 2.0 * (double)(1 << z);
}

// Markers: one per positioned Nearby row, remembered so layout can reposition them.
struct Mark { int32_t lat_e6, lon_e6; int row; lv_obj_t* obj; };
static Mark s_marks[NearbyModel::MAX_NEARBY];
static int  s_mark_count = 0;
static int  s_drag = 0;   // px moved in the current press: a drag isn't a tap
static bool s_available = false;   // provider has data; checked when the map opens, not per frame

}  // namespace mapview

static void onMapPress(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) { mapview::s_drag = 0; return; }
  lv_point_t v;
  lv_indev_get_vect(lv_indev_active(), &v);
  if (v.x == 0 && v.y == 0) return;
  mapview::s_drag += abs(v.x) + abs(v.y);
  s_ui->mapPan(v.x, v.y);
}
static void onMapZoomIn(lv_event_t* e)  { (void)e; s_ui->mapZoom(+1); }
static void onMapZoomOut(lv_event_t* e) { (void)e; s_ui->mapZoom(-1); }
static void onMapCenter(lv_event_t* e)  { (void)e; s_ui->mapCenterOnMe(); }
static void onMapMarker(lv_event_t* e) {
  if (mapview::s_drag > 8) return;   // the press was a pan that ended on a marker
  s_ui->mapOpenMarker((int)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t* mapButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 40, 40);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, text, THEME_FONT_TITLE, theme::TEXT));
  return b;
}

static lv_obj_t* mapPill(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_set_style_bg_color(l, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(l, 5, 0);
  lv_obj_set_style_pad_ver(l, 1, 0);
  lv_obj_set_style_radius(l, 4, 0);
  return l;
}

void UITask::showMap() {
  _screen = SCR_MAP;
  mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
  if (_map_z == 0) {   // first open: own position, else the node's own advert position, else Poland
    int32_t lat, lon;
    double la = mapview::DEFAULT_LAT, lo = mapview::DEFAULT_LON;
    int z = mapview::DEFAULT_Z;
    if (_core->course.currentLocation(lat, lon)) { la = lat / 1e6; lo = lon / 1e6; z = 15; }
    else if (_sensors && (_sensors->node_lat != 0 || _sensors->node_lon != 0)) {   // saved / app-set position
      la = _sensors->node_lat; lo = _sensors->node_lon; z = 13;
    }
    _map_z = z;
    _map_cx = mapview::lonToTileX(lo, z);
    _map_cy = mapview::latToTileY(la, z);
  }
  buildMap();
}

void UITask::buildMap() {
  lv_obj_t* body = newScreen(NULL, false);
  lv_obj_set_layout(body, LV_LAYOUT_NONE);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(body, lv_color_hex(0x1A1A1E), 0);   // unloaded / missing tiles
  lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(body, onMapPress, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(body, onMapPress, LV_EVENT_PRESSING, NULL);
  _map_area = body;

  for (int i = 0; i < mapview::GRID_COLS * mapview::GRID_ROWS; i++) {
    _map_tiles[i] = lv_image_create(body);
    lv_obj_add_flag(_map_tiles[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Marker layer: same size as the map, lets presses through to it.
  _map_marks = lv_obj_create(body);
  lv_obj_remove_style_all(_map_marks);
  lv_obj_set_size(_map_marks, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(_map_marks, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(_map_marks, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(_map_marks, LV_OBJ_FLAG_EVENT_BUBBLE);

  _map_me = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(_map_me);
  lv_obj_set_size(_map_me, 16, 16);
  lv_obj_set_style_radius(_map_me, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(_map_me, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_bg_opa(_map_me, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_map_me, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_width(_map_me, 3, 0);
  lv_obj_remove_flag(_map_me, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(_map_me, LV_OBJ_FLAG_HIDDEN);

  // Controls
  lv_obj_t* back = mapButton(body, LV_SYMBOL_LEFT, onBack);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_align(mapButton(body, LV_SYMBOL_PLUS, onMapZoomIn), LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_align(mapButton(body, LV_SYMBOL_MINUS, onMapZoomOut), LV_ALIGN_TOP_RIGHT, -6, 52);
  lv_obj_align(mapButton(body, LV_SYMBOL_GPS, onMapCenter), LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  _map_zoom_lbl = mapPill(body, "");
  lv_obj_align(_map_zoom_lbl, LV_ALIGN_TOP_LEFT, 52, 16);
  lv_obj_t* attr = mapPill(body, mapview::s_provider->attribution());   // required by the data licences
  lv_label_set_long_mode(attr, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(attr, 250, 0);
  lv_obj_set_width(attr, LV_SIZE_CONTENT);
  lv_obj_align(attr, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  _map_hint = mapPill(body, "");
  lv_label_set_long_mode(_map_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_map_hint, 200);
  lv_obj_set_style_text_align(_map_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(_map_hint);
  lv_obj_add_flag(_map_hint, LV_OBJ_FLAG_HIDDEN);

  lv_obj_update_layout(body);
  mapview::s_mark_count = 0;   // the previous markers went with the previous screen
  rebuildMapMarkers();
  layoutMap();
}

// Places tiles and markers for the current centre / zoom; queues tiles that
// aren't decoded yet for mapLoop().
void UITask::layoutMap() {
  if (!_map_area) return;
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area);
  double left = _map_cx * mapview::TILE_PX - w / 2.0;   // world px of the view's top-left
  double top  = _map_cy * mapview::TILE_PX - h / 2.0;
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << _map_z;
  bool have_provider = mapview::s_available;
  int shown = 0, missing = 0;
  _map_pending = false;

  for (int j = 0; j < mapview::GRID_ROWS; j++) {
    for (int i = 0; i < mapview::GRID_COLS; i++) {
      lv_obj_t* img = _map_tiles[j * mapview::GRID_COLS + i];
      int tx = tx0 + i, ty = ty0 + j;
      int px = (int)lround(tx * (double)mapview::TILE_PX - left);
      int py = (int)lround(ty * (double)mapview::TILE_PX - top);
      bool on_screen = px < w && py < h && px + mapview::TILE_PX > 0 && py + mapview::TILE_PX > 0;
      mapview::TileCache::Slot* s = nullptr;
      if (have_provider && on_screen && ty >= 0 && ty < n) {
        s = mapview::s_cache.find(_map_z, ((tx % n) + n) % n, ty);
        if (!s) _map_pending = true;
      }
      if (s && s->present) {
        if (lv_image_get_src(img) != &s->dsc) lv_image_set_src(img, &s->dsc);
        lv_obj_set_pos(img, px, py);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
        shown++;
      } else {
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        if (s) missing++;
      }
    }
  }

  // Own position
  int32_t lat, lon;
  if (_core->course.currentLocation(lat, lon)) {
    double x = mapview::lonToTileX(lon / 1e6, _map_z) * mapview::TILE_PX - left;
    double y = mapview::latToTileY(lat / 1e6, _map_z) * mapview::TILE_PX - top;
    lv_obj_set_pos(_map_me, (int)x - 8, (int)y - 8);
    lv_obj_remove_flag(_map_me, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(_map_me, LV_OBJ_FLAG_HIDDEN);
  }
  for (int k = 0; k < mapview::s_mark_count; k++) {
    const mapview::Mark& m = mapview::s_marks[k];
    double x = mapview::lonToTileX(m.lon_e6 / 1e6, _map_z) * mapview::TILE_PX - left;
    double y = mapview::latToTileY(m.lat_e6 / 1e6, _map_z) * mapview::TILE_PX - top;
    lv_obj_set_pos(m.obj, (int)x - 6, (int)y - 6);   // the dot's centre on the spot; name to its right
  }

  lv_label_set_text_fmt(_map_zoom_lbl, "z%d", _map_z);
  const char* hint = !have_provider ? "No map on the SD card.\nPut tiles in /maps (tools/maps)."
                   : (!_map_pending && shown == 0 && missing > 0) ? "No tiles here at this zoom." : nullptr;
  if (hint) { lv_label_set_text(_map_hint, hint); lv_obj_remove_flag(_map_hint, LV_OBJ_FLAG_HIDDEN); }
  else lv_obj_add_flag(_map_hint, LV_OBJ_FLAG_HIDDEN);
}

// One tile decode per call, nearest to the centre first.
void UITask::mapLoop() {
  if ((int32_t)(millis() - _next_map_marks_ms) >= 0) {
    _next_map_marks_ms = millis() + 3000;
    if (_map_follow) {
      int32_t lat, lon;
      if (_core->course.currentLocation(lat, lon)) {
        _map_cx = mapview::lonToTileX(lon / 1e6, _map_z);
        _map_cy = mapview::latToTileY(lat / 1e6, _map_z);
      }
    }
    rebuildMapMarkers();
    layoutMap();
  }
  if (!_map_pending || !_map_area) return;
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area);
  double left = _map_cx * mapview::TILE_PX - w / 2.0, top = _map_cy * mapview::TILE_PX - h / 2.0;
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << _map_z;
  int best_x = 0, best_y = 0;
  double best_d = 1e18;
  for (int j = 0; j < mapview::GRID_ROWS; j++) {
    for (int i = 0; i < mapview::GRID_COLS; i++) {
      int tx = tx0 + i, ty = ty0 + j;
      double px = tx * (double)mapview::TILE_PX - left, py = ty * (double)mapview::TILE_PX - top;
      if (px >= w || py >= h || px + mapview::TILE_PX <= 0 || py + mapview::TILE_PX <= 0) continue;
      if (ty < 0 || ty >= n) continue;
      int wx = ((tx % n) + n) % n;
      if (mapview::s_cache.find(_map_z, wx, ty)) continue;
      double d = fabs(px + 128 - w / 2.0) + fabs(py + 128 - h / 2.0);
      if (d < best_d) { best_d = d; best_x = wx; best_y = ty; }
    }
  }
  if (best_d < 1e18) mapview::s_cache.load(*mapview::s_provider, _map_z, best_x, best_y);
  layoutMap();
}

void UITask::rebuildMapMarkers() {
  if (!_map_marks) return;
  for (int k = 0; k < mapview::s_mark_count; k++) lv_obj_delete(mapview::s_marks[k].obj);
  mapview::s_mark_count = 0;
  _nearby->refreshStored();
  for (int i = 0; i < _nearby->count() && mapview::s_mark_count < NearbyModel::MAX_NEARBY; i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    if (e.lat_e6 == 0 && e.lon_e6 == 0) continue;
    uint32_t col = e.is_live ? theme::OK : e.type == ADV_TYPE_CHAT ? theme::TEXT : theme::TEXT_MUTED;

    // One clickable object per marker: a dot plus the name beside it.
    lv_obj_t* m = lv_obj_create(_map_marks);
    lv_obj_remove_style_all(m);
    lv_obj_set_size(m, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(m, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m, 3, 0);
    lv_obj_add_flag(m, LV_OBJ_FLAG_EVENT_BUBBLE);   // a drag starting here still pans
    lv_obj_add_event_cb(m, onMapMarker, LV_EVENT_CLICKED, (void*)(uintptr_t)mapview::s_mark_count);
    lv_obj_t* dot = lv_obj_create(m);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* name = mapPill(m, e.name[0] ? e.name : "?");
    lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);

    mapview::Mark& mk = mapview::s_marks[mapview::s_mark_count++];
    mk.lat_e6 = e.lat_e6; mk.lon_e6 = e.lon_e6; mk.row = i; mk.obj = m;
  }
}

void UITask::mapPan(int dx, int dy) {
  _map_cx -= dx / (double)mapview::TILE_PX;
  _map_cy -= dy / (double)mapview::TILE_PX;
  double n = (double)(1 << _map_z);
  if (_map_cy < 0) _map_cy = 0;
  if (_map_cy > n) _map_cy = n;
  _map_follow = false;
  layoutMap();
}

void UITask::mapZoom(int delta) {
  int z = _map_z + delta;
  if (z < mapview::MIN_Z || z > mapview::MAX_Z) return;
  double f = delta > 0 ? 2.0 : 0.5;
  _map_cx *= f; _map_cy *= f;
  _map_z = z;
  layoutMap();
}

void UITask::mapCenterOnMe() {
  int32_t lat, lon;
  if (!_core->course.currentLocation(lat, lon)) { showToast("No GPS fix"); return; }
  _map_follow = true;
  _map_cx = mapview::lonToTileX(lon / 1e6, _map_z);
  _map_cy = mapview::latToTileY(lat / 1e6, _map_z);
  layoutMap();
}

void UITask::mapOpenMarker(int idx) {
  if (idx < 0 || idx >= mapview::s_mark_count) return;
  openNode(mapview::s_marks[idx].row);
  _node_from_map = true;
}

