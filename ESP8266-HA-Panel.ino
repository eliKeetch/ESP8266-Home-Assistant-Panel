#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "icons.h"

// ---------- WIFI ----------
const char* ssid     = "sweetliberty";
const char* password = "forTheC0mpany89";

// ---------- HOME ASSISTANT ----------
const char* HA_HOST  = "http://192.168.86.71:8123";  // change if needed
const char* HA_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI0Y2FmOGVjNmYyZWI0ZGJjYmRjY2U1M2U1NTYyMjViNCIsImlhdCI6MTc3MTk5MDU0MSwiZXhwIjoyMDg3MzUwNTQxfQ.ASl4BQBRnaRJp8DjMd1Nhksl55wtcECNUA-dwnqZj9I";   


// Best if this is a real HA light group/helper
const char* ALL_OFF_ENTITY = "light.all_lights";

// ---------- TOUCH ----------
#define TOUCH_CS   21
#define TOUCH_IRQ  25

int TS_MINX = 690;
int TS_MAXX = 3760;
int TS_MINY = 396;
int TS_MAXY = 3591;

bool SWAP_XY  = false;
bool INVERT_X = true;
bool INVERT_Y = true;

// ---------- ICON SETTINGS ----------
#define ICON_W 40
#define ICON_H 40
static const bool ICON_MSB_FIRST = true;
static const bool ICON_INVERT    = true;
// ---------- CUSTOM COLORS ----------
#define COLOR_LIVING  0x0BA8
#define COLOR_BEDROOM 0x2C1B
#define COLOR_ALLOFF  0xBA49
#define COLOR_OFFICE  0xCC84

// ---------- STATE REFRESH ----------
static const uint32_t STATE_REFRESH_MS = 5000;

// ---------- OBJECTS ----------
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// ---------- BUTTON MODEL ----------
enum ActionType : uint8_t {
  ACT_LIGHT_ENTITY,
  ACT_ALL_OFF
};

struct ButtonDef {
  int x, y, w, h;
  uint16_t color;
  const char* label;
  const unsigned char* icon;
  ActionType action;
  const char* entity_id;
  bool isOn;
};

ButtonDef BTN_OFFICE;
ButtonDef BTN_BEDROOM;
ButtonDef BTN_LIVING;
ButtonDef BTN_ALLOFF;

// ---------- HELPERS ----------
static inline uint16_t darken565(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >>  5) & 0x3F;
  uint8_t b = (c >>  0) & 0x1F;
  r = (r * 3) / 4;
  g = (g * 3) / 4;
  b = (b * 3) / 4;
  return (r << 11) | (g << 5) | b;
}

static inline bool inButton(const ButtonDef& b, int x, int y) {
  return (x >= b.x && x < (b.x + b.w) && y >= b.y && y < (b.y + b.h));
}

void drawMonoBitmapTransparent(int x, int y,
                               const unsigned char* bits,
                               int w, int h,
                               uint16_t fg,
                               bool msbFirst,
                               bool invert) {
  int bytesPerRow = (w + 7) / 8;

  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      int byteIndex = yy * bytesPerRow + (xx / 8);
      uint8_t b = pgm_read_byte(bits + byteIndex);

      uint8_t mask = msbFirst ? (0x80 >> (xx & 7)) : (0x01 << (xx & 7));
      bool on = (b & mask) != 0;
      if (invert) on = !on;

      if (on) {
        tft.drawPixel(x + xx, y + yy, fg);
      }
    }
    if ((yy & 7) == 0) delay(0);
  }
}

void statusLine(const String& msg) {
  tft.fillRect(0, 0, tft.width(), 28, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(msg, 8, 7, 2);
}

void buildButtons() {
  int screenW = tft.width();
  int screenH = tft.height();
  int statusH = 28;
  int gap = 12;

  int btnW = (screenW - (gap * 3)) / 2;
  int btnH = (screenH - statusH - (gap * 3)) / 2;

  int x1 = gap;
  int x2 = gap * 2 + btnW;
  int y1 = statusH + gap;
  int y2 = statusH + gap * 2 + btnH;

 BTN_OFFICE  = { x1, y1, btnW, btnH, COLOR_OFFICE,  "Office",  epd_bitmap_desktop_classic, ACT_LIGHT_ENTITY, "light.office_lights",  false };
BTN_BEDROOM = { x2, y1, btnW, btnH, COLOR_BEDROOM, "Bedroom", epd_bitmap_bed_king,        ACT_LIGHT_ENTITY, "light.bedroom_lights", false };
BTN_LIVING  = { x1, y2, btnW, btnH, COLOR_LIVING,  "Living",  epd_bitmap_sofa_single,     ACT_LIGHT_ENTITY, "light.living_room",    false };
BTN_ALLOFF  = { x2, y2, btnW, btnH, COLOR_ALLOFF,  "All Off", epd_bitmap_lightbulb_off,   ACT_ALL_OFF,      nullptr,                false };
  
}

void drawButton(const ButtonDef& b, bool pressed) {
  uint16_t fill = pressed ? darken565(b.color) : b.color;

  tft.fillRoundRect(b.x, b.y, b.w, b.h, 18, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 18, b.isOn ? TFT_WHITE : TFT_DARKGREY);

  // state dot
  uint16_t dotColor = b.isOn ? TFT_WHITE : TFT_BLACK;
  tft.fillCircle(b.x + b.w - 18, b.y + 18, 7, dotColor);
  tft.drawCircle(b.x + b.w - 18, b.y + 18, 7, TFT_WHITE);

  int cx = b.x + b.w / 2;
  int iconX = cx - (ICON_W / 2);
  int iconY = b.y + 18;

  uint16_t iconCol = b.isOn ? TFT_WHITE : TFT_LIGHTGREY;
  drawMonoBitmapTransparent(iconX, iconY, b.icon, ICON_W, ICON_H, iconCol, ICON_MSB_FIRST, ICON_INVERT);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fill);
  tft.drawString(b.label, cx, b.y + b.h - 34, 4);

  tft.setTextColor(TFT_WHITE, fill);
  tft.drawString(b.isOn ? "ON" : "OFF", cx, b.y + b.h - 14, 2);
}

void drawUI() {
  buildButtons();
  tft.fillScreen(TFT_BLACK);
  statusLine("Ready");
  drawButton(BTN_OFFICE,  false);
  drawButton(BTN_BEDROOM, false);
  drawButton(BTN_LIVING,  false);
  drawButton(BTN_ALLOFF,  false);
}

void redrawAllButtons() {
  drawButton(BTN_OFFICE,  false);
  drawButton(BTN_BEDROOM, false);
  drawButton(BTN_LIVING,  false);
  drawButton(BTN_ALLOFF,  false);
}

void pressFlash(const ButtonDef& b) {
  drawButton(b, true);
  delay(80);
  drawButton(b, false);
}

// ---------- TOUCH ----------
bool getTouchXY(int &x, int &y) {
  if (!ts.touched()) return false;

  TS_Point p = ts.getPoint();
  int rx = p.x;
  int ry = p.y;

  int w = tft.width();
  int h = tft.height();

  int nx = map(rx, TS_MINX, TS_MAXX, 0, w - 1);
  int ny = map(ry, TS_MINY, TS_MAXY, 0, h - 1);

  nx = constrain(nx, 0, w - 1);
  ny = constrain(ny, 0, h - 1);

  if (INVERT_X) nx = (w - 1) - nx;
  if (INVERT_Y) ny = (h - 1) - ny;
  if (SWAP_XY) {
    int tmp = nx;
    nx = ny;
    ny = tmp;
  }

  x = nx;
  y = ny;
  return true;
}

// ---------- HOME ASSISTANT ----------
bool haPost(const char* path, const String& json, String* outResp = nullptr) {
  if (WiFi.status() != WL_CONNECTED) {
    
    return false;
  }

  HTTPClient http;
  String url = String(HA_HOST) + path;

  
  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = http.POST(json);
  String resp = http.getString();
  http.end();

  

  if (outResp) *outResp = resp;
  return (code >= 200 && code < 300);
}

bool haGet(const char* path, String& responseOut) {
  if (WiFi.status() != WL_CONNECTED) {
    
    return false;
  }

  HTTPClient http;
  String url = String(HA_HOST) + path;

  
  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = http.GET();
  responseOut = http.getString();
  http.end();

  
  return (code >= 200 && code < 300);
}

bool parseEntityIsOn(const String& json) {
  int idx = json.indexOf("\"state\":\"");
  if (idx < 0) return false;
  idx += 9;
  int endIdx = json.indexOf('"', idx);
  if (endIdx < 0) return false;
  String state = json.substring(idx, endIdx);
  return state == "on";
}

bool haReadEntityState(const char* entity_id, bool& isOn) {
  String path = String("/api/states/") + entity_id;
  String resp;
  bool ok = haGet(path.c_str(), resp);
  if (!ok) return false;

  isOn = parseEntityIsOn(resp);
  return true;
}

bool haTurnOn(const char* entity_id) {
  String payload = String("{\"entity_id\":\"") + entity_id + "\"}";
  return haPost("/api/services/light/turn_on", payload);
}

bool haTurnOff(const char* entity_id) {
  String payload = String("{\"entity_id\":\"") + entity_id + "\"}";
  return haPost("/api/services/light/turn_off", payload);
}

bool haAllOff() {
  String payload = String("{\"entity_id\":\"") + ALL_OFF_ENTITY + "\"}";
  return haPost("/api/services/light/turn_off", payload);
}

void refreshStatesFromHA(bool redraw = true) {
  bool state = false;

  if (haReadEntityState(BTN_OFFICE.entity_id, state)) {
    if (BTN_OFFICE.isOn != state) {
      BTN_OFFICE.isOn = state;
      if (redraw) drawButton(BTN_OFFICE, false);
    }
  }

  if (haReadEntityState(BTN_BEDROOM.entity_id, state)) {
    if (BTN_BEDROOM.isOn != state) {
      BTN_BEDROOM.isOn = state;
      if (redraw) drawButton(BTN_BEDROOM, false);
    }
  }

  if (haReadEntityState(BTN_LIVING.entity_id, state)) {
    if (BTN_LIVING.isOn != state) {
      BTN_LIVING.isOn = state;
      if (redraw) drawButton(BTN_LIVING, false);
    }
  }
}
void refreshStatesAfterAction() {
  // Give HA a brief moment to apply the action
  delay(200);

  // Try a couple times in case state propagation is slightly delayed
  for (int i = 0; i < 3; i++) {
    refreshStatesFromHA(true);
    delay(150);
  }
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  

  tft.init();
  tft.setRotation(1);

 
  tft.fillScreen(TFT_BLACK);
  statusLine("Booting...");

  SPI.begin(18, 19, 23, -1);

  ts.begin();
  ts.setRotation(1);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  statusLine("WiFi connecting...");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    statusLine(String("WiFi OK ") + WiFi.localIP().toString());
   
  } else {
    statusLine("WiFi FAILED");
    
  }

  delay(250);
  drawUI();

  if (WiFi.status() == WL_CONNECTED) {
    statusLine("Syncing states...");
    refreshStatesFromHA(true);
    statusLine("Ready");
  }
}

void loop() {
  static uint32_t lastPressMs = 0;
  static uint32_t lastRefreshMs = 0;

  // periodic sync from HA
 if (WiFi.status() == WL_CONNECTED && millis() - lastRefreshMs >= STATE_REFRESH_MS) {
  lastRefreshMs = millis();
  refreshStatesFromHA(true);
}

  int x, y;
  if (!getTouchXY(x, y)) return;

  uint32_t now = millis();
  if (now - lastPressMs < 300) return;
  lastPressMs = now;

  

  ButtonDef* hit = nullptr;
  if (inButton(BTN_OFFICE, x, y)) hit = &BTN_OFFICE;
  else if (inButton(BTN_BEDROOM, x, y)) hit = &BTN_BEDROOM;
  else if (inButton(BTN_LIVING, x, y)) hit = &BTN_LIVING;
  else if (inButton(BTN_ALLOFF, x, y)) hit = &BTN_ALLOFF;
  else return;

  pressFlash(*hit);
  statusLine("Sending...");

  bool ok = false;

  if (hit->action == ACT_LIGHT_ENTITY && hit->entity_id) {
  ok = hit->isOn ? haTurnOff(hit->entity_id) : haTurnOn(hit->entity_id);
  if (ok) {
    refreshStatesAfterAction();
  }
}
else if (hit->action == ACT_ALL_OFF) {
  ok = haAllOff();
  if (ok) {
    refreshStatesAfterAction();
  }
}



  statusLine(ok ? "OK" : "FAILED");
  delay(50);
}