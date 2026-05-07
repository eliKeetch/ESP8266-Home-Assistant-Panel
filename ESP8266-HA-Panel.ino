#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "icons.h"

// ---------- WIFI ----------
const char* ssid     = "sweetliberty";
const char* password = "forTheC0mpany89";

// ---------- HOME ASSISTANT ----------
const char* HA_HOST  = "http://192.168.86.71:8123";  // change if needed
const char* HA_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI0Y2FmOGVjNmYyZWI0ZGJjYmRjY2U1M2U1NTYyMjViNCIsImlhdCI6MTc3MTk5MDU0MSwiZXhwIjoyMDg3MzUwNTQxfQ.ASl4BQBRnaRJp8DjMd1Nhksl55wtcECNUA-dwnqZj9I";   

const char* ALL_OFF_ENTITY = "light.all_lights";

// ---------- BACKLIGHT / PHYSICAL BUTTON ----------
// Commented out for now so the panel only tests touch + Home Assistant behavior.
// Uncomment this whole section later when the touch/buttons are working reliably.

 #define BACKLIGHT_PIN 32
// #define POWER_BUTTON_PIN 25

 bool backlightOn = true;

// const uint32_t SCREEN_TIMEOUT_MS = 30000;
// uint32_t lastActivityMs = 0;

// ---------- TOUCH ----------
// If touches are hard to trigger, try 50.
// If it false-triggers, raise it back toward 200.
const uint16_t TOUCH_Z_MIN = 200;

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

// ---------- OBJECTS ----------
TFT_eSPI tft = TFT_eSPI();

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
  Serial.println("[STATUS] " + msg);
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
  BTN_ALLOFF  = { x2, y2, btnW, btnH, COLOR_ALLOFF,  "All Off", epd_bitmap_lightbulb_off,   ACT_ALL_OFF,      nullptr,                 false };
}

void drawButton(const ButtonDef& b, bool pressed) {
  uint16_t fill = pressed ? darken565(b.color) : b.color;

  tft.fillRoundRect(b.x, b.y, b.w, b.h, 18, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 18, b.isOn ? TFT_WHITE : TFT_DARKGREY);

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

void pressFlash(const ButtonDef& b) {
  drawButton(b, true);
  delay(80);
  drawButton(b, false);
}

// ---------- TOUCH ----------
bool getTouchXY(int &x, int &y) {
  // Do not pulse the backlight every loop pass.
  // This greatly reduces idle flicker.
  static uint32_t lastPollMs = 0;
  uint32_t now = millis();

  if (now - lastPollMs < 60) {
    return false;
  }

  lastPollMs = now;

  uint16_t rawX = 0;
  uint16_t rawY = 0;

  // Temporarily release/backlight-off while reading touch.
  // Your hardware needs this for touch to read correctly.
  digitalWrite(BACKLIGHT_PIN, HIGH);
  delayMicroseconds(300);

  uint16_t z = tft.getTouchRawZ();
  tft.getTouchRaw(&rawX, &rawY);

  // Turn backlight back on immediately.
  digitalWrite(BACKLIGHT_PIN, LOW);

  // Normal no-touch idle
  if (rawX == 8191 && rawY == 8191) return false;
  if ((rawX >= 8190 && rawY >= 8190) || z >= 4090) return false;

  // Your idle noise is around z=0..45.
  // Your real touches are around z=1399+.
  // So ignore low-z noise completely.
  if (z < 300) {
    return false;
  }

  uint16_t sx = 0;
  uint16_t sy = 0;

  // Release backlight again only for the mapped getTouch read.
  digitalWrite(BACKLIGHT_PIN, HIGH);
  delayMicroseconds(300);

  bool pressed = tft.getTouch(&sx, &sy, TOUCH_Z_MIN);

  digitalWrite(BACKLIGHT_PIN, LOW);

  if (!pressed) return false;

  x = sx;
  y = (tft.height() - 1) - sy;

  Serial.printf("[TOUCH] rawX=%u rawY=%u z=%u mapped=%d,%d\n",
                rawX, rawY, z, x, y);

  return true;
}

void waitForTouchRelease() {
  uint32_t start = millis();

  while (millis() - start < 1200) {
    uint16_t sx = 0;
    uint16_t sy = 0;

    if (!tft.getTouch(&sx, &sy, TOUCH_Z_MIN)) return;

    delay(10);
  }
}

// ---------- BACKLIGHT / PHYSICAL BUTTON FUNCTIONS ----------
// Commented out for now. Keep these here so you can re-enable them later.

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(BACKLIGHT_PIN, on ? LOW : HIGH);
  Serial.printf("[BACKLIGHT] %s\n", on ? "ON" : "OFF");
}

// void toggleBacklightManual() {
//   backlightOn = !backlightOn;
//   setBacklight(backlightOn);
//   statusLine(backlightOn ? "Screen On" : "Screen Off");
// }

// void handlePowerButton() {
//   static bool lastStableState = HIGH;
//   static bool lastReading = HIGH;
//   static uint32_t lastChangeMs = 0;
//
//   bool reading = digitalRead(POWER_BUTTON_PIN);
//
//   if (reading != lastReading) {
//     lastChangeMs = millis();
//     lastReading = reading;
//   }
//
//   if (millis() - lastChangeMs > 50) {
//     if (reading != lastStableState) {
//       lastStableState = reading;
//
//       if (lastStableState == LOW) {
//         Serial.println("[BUTTON] Power/backlight button pressed");
//         toggleBacklightManual();
//         lastActivityMs = millis();
//       }
//     }
//   }
// }

// void handleScreenTimeout() {
//   if (backlightOn && millis() - lastActivityMs > SCREEN_TIMEOUT_MS) {
//     setBacklight(false);
//     statusLine("Screen Off");
//   }
// }

// ---------- HOME ASSISTANT ----------
bool haPost(const char* path, const String& json, String* outResp = nullptr) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HA POST] WiFi not connected");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(HA_HOST) + path;

  Serial.println("[HA POST] " + url);
  Serial.println("[HA POST BODY] " + json);

  http.setTimeout(4000);

  if (!http.begin(client, url)) {
    Serial.println("[HA POST] begin() failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = http.POST(json);
  String resp = http.getString();

  Serial.printf("[HA POST] code=%d\n", code);
  Serial.println("[HA POST RESP] " + resp);

  http.end();

  if (outResp) *outResp = resp;

  return (code >= 200 && code < 300);
}

bool haGet(const char* path, String& responseOut) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HA GET] WiFi not connected");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(HA_HOST) + path;

  Serial.println("[HA GET] " + url);

  http.setTimeout(4000);

  if (!http.begin(client, url)) {
    Serial.println("[HA GET] begin() failed");
    return false;
  }

  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = http.GET();
  responseOut = http.getString();

  Serial.printf("[HA GET] code=%d\n", code);
  Serial.println("[HA GET RESP] " + responseOut);

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
  delay(200);
  refreshStatesFromHA(true);

  delay(150);
  refreshStatesFromHA(true);
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("===== BOOT =====");

  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(true);   // keep screen on

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  SPI.begin(18, 19, 23, -1);
  Serial.println("[SPI] Started");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  statusLine("WiFi connecting...");

  uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    statusLine(String("WiFi OK ") + WiFi.localIP().toString());
    Serial.println("[WIFI] Connected");
    Serial.println(WiFi.localIP());
  } else {
    statusLine("WiFi FAILED");
    Serial.println("[WIFI] Failed");
  }

  drawUI();

  if (WiFi.status() == WL_CONNECTED) {
    statusLine("Syncing states...");
    refreshStatesFromHA(true);
    statusLine("Ready");
  }

  Serial.println("===== SETUP DONE =====");
}

void loop() {
  static uint32_t lastPressMs = 0;

  // ---------- BACKLIGHT / PHYSICAL BUTTON LOOP ----------
  // Commented out for now.

  // handlePowerButton();
  // handleScreenTimeout();

  int x = 0;
  int y = 0;

  if (!getTouchXY(x, y)) {
    delay(10);
    return;
  }

  uint32_t now = millis();

  if (now - lastPressMs < 250) {
    Serial.println("[TOUCH] Debounced");
    waitForTouchRelease();
    return;
  }

  lastPressMs = now;

  // ---------- BACKLIGHT WAKE-ONLY LOGIC ----------
  // Commented out for now.
  // This is useful later if you want the first touch to wake the screen
  // without also pressing a Home Assistant button.

  // if (!backlightOn) {
  //   setBacklight(true);
  //   statusLine("Ready");
  //   Serial.println("[TOUCH] Wake only, no button action");
  //   waitForTouchRelease();
  //   delay(150);
  //   return;
  // }

  // lastActivityMs = now;

  Serial.printf("[TOUCH] Accepted x=%d y=%d\n", x, y);
  statusLine("Touch " + String(x) + "," + String(y));

  ButtonDef* hit = nullptr;

  if (inButton(BTN_OFFICE, x, y)) {
    hit = &BTN_OFFICE;
  } else if (inButton(BTN_BEDROOM, x, y)) {
    hit = &BTN_BEDROOM;
  } else if (inButton(BTN_LIVING, x, y)) {
    hit = &BTN_LIVING;
  } else if (inButton(BTN_ALLOFF, x, y)) {
    hit = &BTN_ALLOFF;
  } else {
    Serial.println("[TOUCH] No button hit");
    waitForTouchRelease();
    statusLine("Ready");
    return;
  }

  Serial.print("[TOUCH] Hit button: ");
  Serial.println(hit->label);

  pressFlash(*hit);
  statusLine("Sending...");

  bool ok = false;

  if (hit->action == ACT_LIGHT_ENTITY && hit->entity_id) {
    ok = hit->isOn ? haTurnOff(hit->entity_id) : haTurnOn(hit->entity_id);
  } else if (hit->action == ACT_ALL_OFF) {
    ok = haAllOff();
  }

  if (ok) {
    refreshStatesAfterAction();
  }

  statusLine(ok ? "OK" : "FAILED");

  Serial.printf("[ACTION] Result: %s\n", ok ? "OK" : "FAILED");

  waitForTouchRelease();
  delay(50);
}
