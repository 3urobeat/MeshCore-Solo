#pragma once
// Base map layers (Map tools > Map): the tile servers the map can show, each
// in a folder of its own on the card -- its downloaded areas, its job, its
// attribution -- and its live tiles in a folder of their own under LIVE_ROOT
// (LiveCache.h; one size limit for them all). Topo is the original layout:
// /sdcard/maps and the top of /sdcard/maps-live, with <root>/source.txt still
// able to name another server (TileDownloader.h).
//
// Tiles keep the .png name whatever the server sends: the Esri layers are
// JPEG (all but Topo), told apart by their first bytes when decoded (TileProvider.h).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

namespace mapview {

struct MapLayer {
  const char* name;   // in Map tools
  const char* root;   // downloaded areas
  const char* live;   // live tiles (LIVE_ROOT or a folder in it)
  const char* url;    // {z} {x} {y}
  const char* attr;
  int8_t      zmax;   // the server's last zoom level
};

static const MapLayer LAYERS[] = {
  { "Topo", "/sdcard/maps", "/sdcard/maps-live",
    "https://a.tile.opentopomap.org/{z}/{x}/{y}.png",
    "\xC2\xA9 OpenStreetMap contributors, SRTM | \xC2\xA9 OpenTopoMap (CC-BY-SA)", 17 },
  // Monochrome: Esri's grey canvases (CARTO's Positron / Dark Matter now want
  // an API key). Nothing past z16: the server answers "Map data not yet available".
  { "Light", "/sdcard/maps-light", "/sdcard/maps-live/light",
    "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Light_Gray_Base/MapServer/tile/{z}/{y}/{x}",
    "\xC2\xA9 Esri, HERE, Garmin, \xC2\xA9 OpenStreetMap contributors", 16 },
  { "Dark", "/sdcard/maps-dark", "/sdcard/maps-live/dark",
    "https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}",
    "\xC2\xA9 Esri, HERE, Garmin, \xC2\xA9 OpenStreetMap contributors", 16 },
  { "Satellite", "/sdcard/maps-sat", "/sdcard/maps-live/sat",
    "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
    "Imagery \xC2\xA9 Esri, Maxar, Earthstar Geographics", 19 },
  { "Esri Topo", "/sdcard/maps-esritopo", "/sdcard/maps-live/esritopo",
    "https://server.arcgisonline.com/ArcGIS/rest/services/World_Topo_Map/MapServer/tile/{z}/{y}/{x}",
    "\xC2\xA9 Esri, HERE, Garmin, \xC2\xA9 OpenStreetMap contributors", 19 },
};
static const int LAYER_COUNT = sizeof(LAYERS) / sizeof(LAYERS[0]);
static const char* const LAYER_OPTS = "Topo\nLight\nDark\nSatellite\nEsri Topo";   // as LAYERS
static uint8_t s_layer = 0;   // the one shown (lvport::mapLayer())

static const MapLayer& layer(int i = -1) { return LAYERS[i < 0 || i >= LAYER_COUNT ? s_layer : i]; }

}  // namespace mapview
