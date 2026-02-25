#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ---------- WIFI ----------
const char* ssid     = "sweetliberty";
const char* password = "forTheC0mpany89";

// ---------- HOME ASSISTANT ----------
const char* HA_HOST  = "http://192.168.86.71:8123";  // change if needed
const char* HA_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiI0Y2FmOGVjNmYyZWI0ZGJjYmRjY2U1M2U1NTYyMjViNCIsImlhdCI6MTc3MTk5MDU0MSwiZXhwIjoyMDg3MzUwNTQxfQ.ASl4BQBRnaRJp8DjMd1Nhksl55wtcECNUA-dwnqZj9I";   
// ---------- TOUCH ----------
#define TOUCH_CS D4

int TS_MINX = 690;
int TS_MAXX = 3760;
int TS_MINY = 396;
int TS_MAXY = 3591;

bool SWAP_XY  = false;
bool INVERT_X = true;
bool INVERT_Y = true;   // IMPORTANT

// ---------- OBJECTS ----------
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS);

// ---------- UI ----------
void drawUI() {
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0,   0, 240, 160, TFT_GREEN);
  tft.fillRect(240, 0, 240, 160, TFT_BLUE);
  tft.fillRect(0, 160, 240, 160, TFT_ORANGE);
  tft.fillRect(240,160,240,160,TFT_RED);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Office",  60,  60, 4);
  tft.drawString("Bedroom", 270, 60, 4);
  tft.drawString("Living",  60,  220,4);
  tft.drawString("ALL OFF", 270, 220,4);
}

void statusLine(const char* msg) {
  tft.fillRect(0, 0, 480, 24, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(msg, 6, 4, 2);
}

// ---------- TOUCH READ ----------
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

  // Invert first
  if (INVERT_X) nx = (W - 1) - nx;
  if (INVERT_Y) ny = (H - 1) - ny;

  // Then swap if needed
  if (SWAP_XY) { int tmp = nx; nx = ny; ny = tmp; }

  x = nx;
  y = ny;

  return true;
}

// ---------- HA POST ----------
bool haPost(const char* path, const String& json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HA] WiFi not connected");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(HA_HOST) + path;

  Serial.println("---- HA POST ----");
  Serial.print("[HA] URL: "); Serial.println(url);
  Serial.print("[HA] Body: "); Serial.println(json);

  if (!http.begin(client, url)) {
    Serial.println("[HA] http.begin() failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int code = http.POST(json);
  String resp = http.getString();

  Serial.print("[HA] HTTP code: "); Serial.println(code);
  Serial.print("[HA] Response: "); Serial.println(resp);

  http.end();

  return (code >= 200 && code < 300);
}

// ---------- ACTIONS ----------
void toggleLight(const char* entity) {
  Serial.print("[BTN] Toggle: ");
  Serial.println(entity);

  statusLine("Sending...");
  String payload = String("{\"entity_id\":\"") + entity + "\"}";
  bool ok = haPost("/api/services/light/toggle", payload);
  statusLine(ok ? "OK" : "FAILED");
}

void allLightsOff() {
  Serial.println("[BTN] All lights off");

  statusLine("Turning off...");
  // Reliable payload, avoids 400
  bool ok = haPost("/api/services/light/turn_off", "{\"entity_id\":\"all\"}");
  statusLine(ok ? "OK" : "FAILED");
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] Serial OK @115200");

  tft.init();
  tft.setRotation(1);
  drawUI();
  statusLine("Booting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  statusLine("WiFi connecting...");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
    statusLine("WiFi OK");
  } else {
    statusLine("WiFi FAILED");
  }

  ts.begin();
  statusLine("Ready");
}

// ---------- LOOP ----------
void loop() {
  static uint32_t lastPressMs = 0;

  int x, y;
  if (getTouchXY(x, y)) {
    // debounce
    uint32_t now = millis();
    if (now - lastPressMs < 350) return;
    lastPressMs = now;

    int midX = tft.width() / 2;   // 240
    int midY = tft.height() / 2;  // 160

    const char* quad =
      (x < midX && y < midY) ? "TL" :
      (x >= midX && y < midY) ? "TR" :
      (x < midX && y >= midY) ? "BL" : "BR";

    Serial.printf("[TOUCH] x=%d y=%d -> %s\n", x, y, quad);

    // Map quadrants to actions
    if (x < midX && y < midY) {
      toggleLight("light.office_lights");
    } else if (x >= midX && y < midY) {
      toggleLight("light.bedroom_lights");
    } else if (x < midX && y >= midY) {
      toggleLight("light.living_room");
    } else {
      allLightsOff();
    }
  }
}