#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <ArduinoJson.h>

// ---------------- Display Config ----------------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   12                  // 3 × (8x32) panels → 12 MAX7219s total
const uint8_t PIN_CS = 5;                 // LOAD/CS (SCK=18, MOSI=23 via hardware SPI)

// ======== USER TUNABLES (top-level) ========
const uint8_t  DISPLAY_INTENSITY   = 8;            // 0..15
const uint16_t SCROLL_SPEED        = 35;           // smaller = slower scroll
const uint32_t FETCH_PERIOD_MS     = 3UL * 60UL * 60UL * 1000UL;  // 3 hours
const uint32_t WOPR_SHOW_TIME_MS   = 120UL * 1000UL;              // 2 minutes of static
const uint16_t WOPR_STEP_MS        = 120;          // frame delay for WOPR noise

// Parola uses hardware SPI when you pass CS + device count
MD_Parola P(HARDWARE_TYPE, PIN_CS, MAX_DEVICES);

// We'll poke the matrix directly for WOPR; we’ll get the underlying MD_MAX72XX
MD_MAX72XX* mx = nullptr;

// ---------------- WiFi --------------------------
const char* ssid     = "YourNetworkName";
const char* password = "YourNetworkPassword";

// ---------------- YouTube -----------------------
const char* YOUTUBE_API_KEY    = "YourYouTubeApiKey";
const char* YOUTUBE_CHANNEL_ID = "YourYouTubeChannelID";

// ---------------- Instagram (Graph API) ---------
const bool   ENABLE_IG       = true;  // set false to disable IG
const char*  IG_ACCESS_TOKEN = "YourInstagramAccessToken";
const char*  IG_USER_ID      = "YourInstagramUserID";   // numeric IG user id

WiFiClientSecure secureClient;

// ---------- Helpers ----------
String fmtCountCompact(long n) {
  if (n < 0) return "err";
  if (n < 1000) return String(n);
  if (n < 1000000) {
    float k = n / 1000.0;
    String s = String(k, 1);
    if (s.endsWith(".0")) s.remove(s.length() - 2);
    return s + "k";
  }
  float m = n / 1000000.0;
  String s = String(m, 1);
  if (s.endsWith(".0")) s.remove(s.length() - 2);
  return s + "M";
}

bool httpsGET(const String& url, String& payload) {
  HTTPClient http;
  http.begin(secureClient, url);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    payload = http.getString();
    http.end();
    return true;
  } else {
    payload = http.getString();  // may hold JSON error
    http.end();
    return false;
  }
}

long fetchYouTubeSubs() {
  String url = String("https://www.googleapis.com/youtube/v3/channels?part=statistics&id=") +
               YOUTUBE_CHANNEL_ID + "&key=" + YOUTUBE_API_KEY;
  String body;
  if (!httpsGET(url, body)) return -1;

  DynamicJsonDocument doc(2048);
  auto err = deserializeJson(doc, body);
  if (err) return -1;

  if (!doc["items"][0]["statistics"]["subscriberCount"].isNull()) {
    const char* subs = doc["items"][0]["statistics"]["subscriberCount"];
    return String(subs).toInt();
  }
  return -1;
}

long fetchInstagramFollowers() {
  if (!ENABLE_IG) return -1;
  String url = String("https://graph.facebook.com/v19.0/") + IG_USER_ID +
               "?fields=followers_count&access_token=" + IG_ACCESS_TOKEN;
  String body;
  if (!httpsGET(url, body)) return -1;

  DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, body);
  if (err) return -1;

  if (!doc["followers_count"].isNull()) {
    return (long)doc["followers_count"].as<long>();
  }
  return -1;
}

// Scroll a message once across the entire 8x(8*MAX_DEVICES) display
void scrollOnce(const String& msg, uint16_t speed = SCROLL_SPEED) {
  P.displayClear();
  P.setIntensity(DISPLAY_INTENSITY);   // use configurable intensity
  P.displayText(
    (char*)msg.c_str(),
    PA_CENTER,
    speed,
    0,
    PA_SCROLL_LEFT,
    PA_SCROLL_LEFT
  );
  while (!P.displayAnimate()) {
    yield();  // keeps WiFi stack happy
  }
  P.displayReset();
}

// WOPR/WarGames-y random noise for 'durationMs' milliseconds
void woprNoise(uint32_t durationMs, uint16_t stepMs = WOPR_STEP_MS, uint8_t density = 65, uint8_t updateColPct = 30) {
  if (!mx) return;
  const uint16_t totalCols = MAX_DEVICES * 8; // each device is 8 cols
  const uint8_t  totalRows = 8;

  uint32_t tStart = millis();
  while ((uint32_t)(millis() - tStart) < durationMs) {
    for (uint16_t col = 0; col < totalCols; col++) {
      if (random(100) < updateColPct) {     // ~30% columns updated per frame
        for (uint8_t row = 0; row < totalRows; row++) {
          bool on = (random(100) < density); // 65% chance a bit is on
          mx->setPoint(row, col, on);
        }
      }
    }
    delay(stepMs);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // Start SPI (Parola will init as needed; explicit begin is fine)
  SPI.begin(18, 19, 23);

  // Init display
  P.begin();
  P.setZone(0, 0, MAX_DEVICES - 1);
  P.setFont(nullptr);         // default 5x7
  P.displayClear();

  // Grab pointer to underlying MD_MAX72XX for WOPR effect
  mx = P.getGraphicObject();
  if (mx) {
    mx->control(MD_MAX72XX::INTENSITY, DISPLAY_INTENSITY);
    mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
    mx->clear();
  }

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  scrollOnce("Connecting WiFi...");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - t0) < 20000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK: "); Serial.println(WiFi.localIP());
    scrollOnce("WiFi OK");
  } else {
    Serial.println("WiFi FAIL");
    scrollOnce("WiFi FAIL");
  }

  // HTTPS: skip certificate validation (simple). Load real CA for production.
  secureClient.setInsecure();

  randomSeed(esp_random());   // better randomness for WOPR effect
}

void loop() {
  static uint32_t lastFetch = 0;
  static long ytSubs = -1;
  static long igFollowers = -1;

  // Fetch immediately on first loop, then only every 3 hours
  if (lastFetch == 0 || (uint32_t)(millis() - lastFetch) >= FETCH_PERIOD_MS) {
    lastFetch = millis();
    scrollOnce("Fetching...");
    ytSubs = fetchYouTubeSubs();
    igFollowers = ENABLE_IG ? fetchInstagramFollowers() : -1;
  }

  // Show WOPR static for WOPR_SHOW_TIME_MS (e.g., 2 minutes), then counts
  woprNoise(WOPR_SHOW_TIME_MS, WOPR_STEP_MS);

  // Show YouTube count
  {
    String msg = "YouTube Followers: " + fmtCountCompact(ytSubs);
    Serial.println(msg);
    scrollOnce(msg);
  }

  // Show Instagram count (if enabled)
  if (ENABLE_IG) {
    String msg = "Instagram Followers: " + fmtCountCompact(igFollowers);
    Serial.println(msg);
    scrollOnce(msg);
  }

  // (Optional) brief WOPR interlude between cycles; comment out if not desired
  // woprNoise(8000, WOPR_STEP_MS);
}