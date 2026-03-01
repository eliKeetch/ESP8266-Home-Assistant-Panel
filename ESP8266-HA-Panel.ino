#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "icons.h"



// ---------- WIFI ----------
const char* ssid     = "sweetliberty";
const char* password = "forTheC0mpany89";

// ---------- HOME ASSISTANT ----------
const char* HA_HOST  = "http://192.168.86.71:8123";  // change if needed
const char* HA_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI0Y2FmOGVjNmYyZWI0ZGJjYmRjY2U1M2U1NTYyMjViNCIsImlhdCI6MTc3MTk5MDU0MSwiZXhwIjoyMDg3MzUwNTQxfQ.ASl4BQBRnaRJp8DjMd1Nhksl55wtcECNUA-dwnqZj9I";   


// ---------------- TOUCH ----------------
#define TOUCH_CS D4  // your touch CS pin (you had this earlier)

int TS_MINX = 690;
int TS_MAXX = 3760;
int TS_MINY = 396;
int TS_MAXY = 3591;

bool SWAP_XY  = false;
bool INVERT_X = true;
bool INVERT_Y = true;

// ---------- OBJECTS ----------
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS);

// ---------- SCREEN ----------
const int SCREEN_W = 480;
const int SCREEN_H = 320;
const int STATUS_H = 26;

const int GAP   = 12;
const int BTN_W = (SCREEN_W - (GAP * 3)) / 2;
const int BTN_H = (SCREEN_H - STATUS_H - (GAP * 3)) / 2;

const int X1 = GAP;
const int X2 = GAP * 2 + BTN_W;
const int Y1 = STATUS_H + GAP;
const int Y2 = STATUS_H + GAP * 2 + BTN_H;

struct ButtonDef {
  int x, y, w, h;
  uint16_t color;
  const char* label;
  const char* entity_id; // nullptr means special (All Off)
  bool isOn;
};

ButtonDef BTN_OFFICE  = { X1, Y1, BTN_W, BTN_H, TFT_BLUE,     "Office",  "light.office_lights",  false };
ButtonDef BTN_BEDROOM = { X2, Y1, BTN_W, BTN_H, TFT_RED,      "Bedroom", "light.bedroom_lights", false };
ButtonDef BTN_LIVING  = { X1, Y2, BTN_W, BTN_H, TFT_GREEN,    "Living",  "light.living_room",    false };
ButtonDef BTN_ALLOFF  = { X2, Y2, BTN_W, BTN_H, TFT_DARKGREY, "All Off", nullptr,                false };

uint16_t darken565(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >>  5) & 0x3F;
  uint8_t b = (c >>  0) & 0x1F;
  r = (r * 3) / 4; g = (g * 3) / 4; b = (b * 3) / 4;
  return (r<<11) | (g<<5) | b;
}

void statusLine(const String& msg) {
  tft.fillRect(0, 0, SCREEN_W, STATUS_H, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(msg, 6, 6, 2);
  yield();
}

bool inButton(const ButtonDef& b, int x, int y) {
  return (x >= b.x && x < (b.x + b.w) && y >= b.y && y < (b.y + b.h));
}

void drawButton(const ButtonDef& b, bool pressed) {
  uint16_t fill = pressed ? darken565(b.color) : b.color;

  // rounded fill
  tft.fillSmoothRoundRect(b.x, b.y, b.w, b.h, 18, fill);
  yield();

  // border indicates on/off
  uint16_t border = b.isOn ? TFT_WHITE : TFT_DARKGREY;
  tft.drawSmoothRoundRect(b.x, b.y, b.w, b.h, 18, border, fill);
  yield();

  // label centered, background SAME as fill (no black box)
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fill);
  tft.drawString(b.label, b.x + b.w/2, b.y + b.h - 28, 4);

  // tiny state
  tft.setTextColor(TFT_WHITE, fill);
  tft.drawString(b.isOn ? "ON" : "OFF", b.x + b.w/2, b.y + b.h - 10, 2);

  yield();
}

void pressFlash(const ButtonDef& b) {
  drawButton(b, true);
  delay(70);
  drawButton(b, false);
}

void drawUI() {
  tft.fillScreen(TFT_BLACK);
  statusLine("Ready");

  // draw in chunks with yields so no WDT
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

  if (SWAP_XY) { int tmp = nx; nx = ny; ny = tmp; }

  x = nx; y = ny;
  return true;
}

// ---------- HA ----------
bool haPost(const char* path, const String& json) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(3000);

  String url = String(HA_HOST) + path;
  if (!http.begin(client, url)) return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  yield();
  int code = http.POST(json);
  yield();

  http.end();
  return (code >= 200 && code < 300);
}

bool haToggleLight(const char* entity_id) {
  String payload = String("{\"entity_id\":\"") + entity_id + "\"}";
  return haPost("/api/services/light/toggle", payload);
}

bool haAllOff() {
  return haPost("/api/services/light/turn_off", "{\"entity_id\":\"all\"}");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  statusLine("Booting...");

  ts.begin();
  yield();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  statusLine("WiFi connecting...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(50);
    yield();
  }

  statusLine(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAILED");
  drawUI();
}

void loop() {
  static uint32_t lastPressMs = 0;

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
  if (hit->entity_id) {
    ok = haToggleLight(hit->entity_id);
    if (ok) hit->isOn = !hit->isOn; // optimistic
    drawButton(*hit, false);
  } else {
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
}