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

// ---------- OBJECTS ----------
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// ---------- UI MODEL ----------
enum ActionType : uint8_t {
  ACT_LIGHT_TOGGLE,
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
  int SCREEN_W = tft.width();
  int SCREEN_H = tft.height();
  int STATUS_H = 28;
  int GAP = 12;

  int BTN_W = (SCREEN_W - (GAP * 3)) / 2;
  int BTN_H = (SCREEN_H - STATUS_H - (GAP * 3)) / 2;

  int X1 = GAP;
  int X2 = GAP * 2 + BTN_W;
  int Y1 = STATUS_H + GAP;
  int Y2 = STATUS_H + GAP * 2 + BTN_H;

  BTN_OFFICE  = { X1, Y1, BTN_W, BTN_H, TFT_BLUE,     "Office",  epd_bitmap_desktop_classic, ACT_LIGHT_TOGGLE, "light.office_lights",  false };
  BTN_BEDROOM = { X2, Y1, BTN_W, BTN_H, TFT_RED,      "Bedroom", epd_bitmap_bed_king,        ACT_LIGHT_TOGGLE, "light.bedroom_lights", false };
  BTN_LIVING  = { X1, Y2, BTN_W, BTN_H, TFT_GREEN,    "Living",  epd_bitmap_sofa_single,     ACT_LIGHT_TOGGLE, "light.living_room",    false };
  BTN_ALLOFF  = { X2, Y2, BTN_W, BTN_H, TFT_DARKGREY, "All Off", epd_bitmap_lightbulb_off,   ACT_ALL_OFF,      nullptr,                false };

  Serial.printf("Screen: %d x %d\n", SCREEN_W, SCREEN_H);
  Serial.printf("Button W/H: %d x %d\n", BTN_W, BTN_H);
}

void drawButton(const ButtonDef& b, bool pressed) {
  uint16_t fill = pressed ? darken565(b.color) : b.color;

  tft.fillRect(b.x, b.y, b.w, b.h, fill);
  tft.drawRect(b.x, b.y, b.w, b.h, b.isOn ? TFT_WHITE : TFT_DARKGREY);

  int cx = b.x + b.w / 2;
  int iconX = cx - (ICON_W / 2);
  int iconY = b.y + 18;

  uint16_t iconCol = b.isOn ? TFT_WHITE : TFT_LIGHTGREY;
  drawMonoBitmapTransparent(iconX, iconY, b.icon, ICON_W, ICON_H, iconCol, ICON_MSB_FIRST, ICON_INVERT);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fill);
  tft.drawString(b.label, cx, b.y + b.h - 34, 4);
  tft.drawString(b.isOn ? "ON" : "OFF", cx, b.y + b.h - 14, 2);
}

void pressFlash(const ButtonDef& b) {
  drawButton(b, true);
  delay(90);
  drawButton(b, false);
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

bool getTouchXY(int &x, int &y) {
  if (!ts.touched()) return false;

  TS_Point p = ts.getPoint();
  int rx = p.x;
  int ry = p.y;

  int W = tft.width();
  int H = tft.height();

  int nx = map(rx, TS_MINX, TS_MAXX, 0, W - 1);
  int ny = map(ry, TS_MINY, TS_MAXY, 0, H - 1);

  nx = constrain(nx, 0, W - 1);
  ny = constrain(ny, 0, H - 1);

  if (INVERT_X) nx = (W - 1) - nx;
  if (INVERT_Y) ny = (H - 1) - ny;
  if (SWAP_XY) {
    int tmp = nx;
    nx = ny;
    ny = tmp;
  }

  x = nx;
  y = ny;
  return true;
}

bool haPost(const char* path, const String& json, String* outResp = nullptr) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HA] WiFi not connected");
    return false;
  }

  HTTPClient http;
  String url = String(HA_HOST) + path;

  Serial.println("---- HA POST ----");
  Serial.print("[HA] URL: ");
  Serial.println(url);
  Serial.print("[HA] Body: ");
  Serial.println(json);

  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = http.POST(json);
  String resp = http.getString();
  http.end();

  Serial.print("[HA] HTTP code: ");
  Serial.println(code);
  Serial.print("[HA] Response: ");
  Serial.println(resp);

  if (outResp) *outResp = resp;
  return (code >= 200 && code < 300);
}

bool haToggleLight(const char* entity_id) {
  String payload = String("{\"entity_id\":\"") + entity_id + "\"}";
  return haPost("/api/services/light/toggle", payload);
}

bool haAllOff() {
  String payload = String("{\"entity_id\":\"") + ALL_OFF_ENTITY + "\"}";
  return haPost("/api/services/light/turn_off", payload);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[BOOT] ESP32 HA Panel");

  tft.init();
  tft.setRotation(1);

  Serial.print("tft.width() = ");
  Serial.println(tft.width());
  Serial.print("tft.height() = ");
  Serial.println(tft.height());

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
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    statusLine("WiFi FAILED");
    Serial.println("[WIFI] FAILED");
  }

  delay(250);
  drawUI();
}

void loop() {
  static uint32_t lastPressMs = 0;

  int x, y;
  if (!getTouchXY(x, y)) return;

  uint32_t now = millis();
  if (now - lastPressMs < 300) return;
  lastPressMs = now;

  Serial.printf("[TOUCH] x=%d y=%d\n", x, y);

  ButtonDef* hit = nullptr;
  if (inButton(BTN_OFFICE, x, y)) hit = &BTN_OFFICE;
  else if (inButton(BTN_BEDROOM, x, y)) hit = &BTN_BEDROOM;
  else if (inButton(BTN_LIVING, x, y)) hit = &BTN_LIVING;
  else if (inButton(BTN_ALLOFF, x, y)) hit = &BTN_ALLOFF;
  else return;

  pressFlash(*hit);
  statusLine("Sending...");

  bool ok = false;

  if (hit->action == ACT_LIGHT_TOGGLE && hit->entity_id) {
    ok = haToggleLight(hit->entity_id);
    if (ok) hit->isOn = !hit->isOn;
    drawButton(*hit, false);
  } else if (hit->action == ACT_ALL_OFF) {
    ok = haAllOff();
    if (ok) {
      BTN_OFFICE.isOn  = false;
      BTN_BEDROOM.isOn = false;
      BTN_LIVING.isOn  = false;

      drawButton(BTN_OFFICE, false);
      drawButton(BTN_BEDROOM, false);
      drawButton(BTN_LIVING, false);
      drawButton(BTN_ALLOFF, false);
    }
  }

  statusLine(ok ? "OK" : "FAILED");
  delay(50);
}