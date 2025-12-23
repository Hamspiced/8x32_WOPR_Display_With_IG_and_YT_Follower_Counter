#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Preferences.h>

// ---------------- Display Config ----------------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   12                  // 3 × (8x32) panels → 12 MAX7219s total
const uint8_t PIN_CS = 5;                 // LOAD/CS (SCK=18, MOSI=23 via hardware SPI)

// ---------------- User-tunable (runtime-editable) ----------------
// (These are initialized with defaults; web UI can change and persist them.)
uint8_t  displayIntensity = 8;                // 0..15
uint16_t scrollSpeed      = 35;               // lower = slower
uint32_t fetchPeriodMs    = 3UL * 60UL * 60UL * 1000UL;  // 3 hours
uint32_t woprShowTimeMs   = 120UL * 1000UL;             // 2 minutes
uint16_t woprStepMs       = 120;              // frame delay for WOPR noise

// Credentials / API (runtime-editable too)
String WIFI_SSID   = "Your_SSID";
String WIFI_PASS   = "SSID_Password";
String YT_API_KEY  = "Your_Youtube_API_Key";
String YT_CHAN_ID  = "Your_Youtube_Channel_ID";
bool   ENABLE_IG   = true;  //If you arent using Instagram set this flag to False
String IG_TOKEN    = "YourInstagramToken";
String IG_USER_ID  = "YourInstagramID";

// if your token only works for an hour, use this site to extend the Access token you create for instagram: https://developers.facebook.com/tools/debug/accesstoken/?access_token="Your_Access_Token_Here"

// Optional static IP (set useStaticIP = true to apply)
bool useStaticIP = false;
IPAddress hardcodedIP(192, 168, 1, 10);
IPAddress hardcodedGW(192, 168, 1, 1);
IPAddress hardcodedSN(255, 255, 255, 0);
IPAddress hardcodedDNS1(1, 1, 1, 1);
IPAddress hardcodedDNS2(8, 8, 8, 8);

// Parola uses hardware SPI when you pass CS + device count
MD_Parola P(HARDWARE_TYPE, PIN_CS, MAX_DEVICES);

// We'll poke the matrix directly for WOPR; we’ll get the underlying MD_MAX72XX
MD_MAX72XX* mx = nullptr;

WiFiClientSecure secureClient;

// ---------------- Web + Storage ----------------
WebServer server(80);
Preferences prefs; // NVS keyspace "wopr"

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
               YT_CHAN_ID + "&key=" + YT_API_KEY;
  String body;
  if (!httpsGET(url, body)) return -1;

  DynamicJsonDocument doc(4096);
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
  String url = String("https://graph.instagram.com/v19.0/") + IG_USER_ID +
               "?fields=followers_count&access_token=" + IG_TOKEN;
  String body;
  if (!httpsGET(url, body)) return -1;

  DynamicJsonDocument doc(4096);
  auto err = deserializeJson(doc, body);
  if (err) return -1;

  if (!doc["followers_count"].isNull()) {
    return (long)doc["followers_count"].as<long>();
  }
  return -1;
}

// Scroll helper (used for system messages)
void scrollOnce(const String& msg, uint16_t speed) {
  P.displayClear();
  P.setIntensity(displayIntensity);
  P.displayText((char*)msg.c_str(), PA_CENTER, speed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!P.displayAnimate()) { server.handleClient(); yield(); }
  P.displayReset();
}

// Forward declaration so we can call it before it's defined later
void woprNoise(uint32_t durationMs, uint16_t stepMs, uint8_t density = 65, uint8_t updateColPct = 30);

// NEW: Scroll full message over existing static (no clear), then resume static
void scrollFullOverStatic(const String& msg, uint16_t spd, uint16_t postStaticMs = 2000) {
  // Keep whatever is already on the display (WOPR noise).
  P.setIntensity(displayIntensity);
  P.setTextAlignment(PA_LEFT);
  P.setCharSpacing(1);
  P.displayScroll((char*)msg.c_str(), PA_LEFT, PA_SCROLL_LEFT, spd);
  while (!P.displayAnimate()) { server.handleClient(); yield(); }
  P.displayReset();  // reset Parola state (doesn't wipe matrix contents)

  // Put more static back on top for a bit (optional aesthetic)
  if (postStaticMs > 0) woprNoise(postStaticMs, woprStepMs);
}

// WOPR-style noise (keeps web server responsive)
void woprNoise(uint32_t durationMs, uint16_t stepMs, uint8_t density, uint8_t updateColPct) {
  if (!mx) return;
  const uint16_t totalCols = MAX_DEVICES * 8;
  const uint8_t  totalRows = 8;

  uint32_t tStart = millis();
  while ((uint32_t)(millis() - tStart) < durationMs) {
    for (uint16_t col = 0; col < totalCols; col++) {
      if (random(100) < updateColPct) {
        for (uint8_t row = 0; row < totalRows; row++) {
          bool on = (random(100) < density);
          mx->setPoint(row, col, on);
        }
      }
    }

    // keep UI responsive
    uint32_t waited = 0;
    const uint16_t slice = 5;
    while (waited < stepMs) {
      server.handleClient();
      delay(slice);
      waited += slice;
    }
  }
}

// ---------------- Web UI (unchanged) ----------------
String htmlPage() {
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "0.0.0.0";
  String s;
  s.reserve(4000);
  s += "<!doctype html><html><head><meta charset='utf-8'/><meta name='viewport' content='width=device-width,initial-scale=1'/>"
       "<title>WOPR Display Config</title>"
       "<style>body{font-family:sans-serif;background:#0c141b;color:#eaf6fb;padding:20px}"
       "h1{font-size:20px}form{max-width:680px}label{display:block;margin-top:10px;font-weight:700}"
       "input{width:100%;padding:10px;border-radius:8px;border:1px solid #2a3b48;background:#0f1a22;color:#eaf6fb}"
       ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
       "button{margin-top:16px;padding:10px 14px;border-radius:10px;border:0;background:#00e4ff;color:#06222d;font-weight:800;cursor:pointer}"
       ".muted{opacity:.8;font-size:.9rem;margin-top:4px}"
       ".box{border:1px solid #244050;border-radius:12px;padding:14px;margin-top:16px}"
       "</style></head><body>";
  s += "<h1>WOPR Display — Config</h1>";
  s += "<div class='muted'>Device IP: " + ip + "</div>";
  s += "<form method='POST' action='/save'><div class='box'><h3>APIs</h3>";
  s += "<label>YouTube API Key</label><input name='yt_api' value='" + YT_API_KEY + "'/>";
  s += "<label>YouTube Channel ID</label><input name='yt_chan' value='" + YT_CHAN_ID + "'/>";
  s += "<label>Instagram Access Token</label><input name='ig_token' value='" + IG_TOKEN + "'/>";
  s += "<label>Instagram User ID</label><input name='ig_uid' value='" + IG_USER_ID + "'/></div>";

  s += "<div class='box'><h3>Display & Timing</h3><div class='row'>";
  s += "<div><label>Intensity (0-15)</label><input name='intensity' type='number' min='0' max='15' value='" + String(displayIntensity) + "'/></div>";
  s += "<div><label>Scroll Speed</label><input name='scroll' type='number' min='5' max='200' value='" + String(scrollSpeed) + "'/></div>";
  s += "</div><div class='row'>";
  s += "<div><label>Fetch Period (ms)</label><input name='fetch' type='number' min='60000' value='" + String(fetchPeriodMs) + "'/></div>";
  s += "<div><label>WOPR Show Time (ms)</label><input name='woprshow' type='number' min='1000' value='" + String(woprShowTimeMs) + "'/></div>";
  s += "</div><div class='row'>";
  s += "<div><label>WOPR Step (ms)</label><input name='woprstep' type='number' min='10' max='1000' value='" + String(woprStepMs) + "'/></div>";
  s += "<div><label>Enable Instagram (0/1)</label><input name='ig_en' type='number' min='0' max='1' value='" + String(ENABLE_IG ? 1 : 0) + "'/></div>";
  s += "</div></div>";

  s += "<button type='submit'>Save Settings</button></form>";
  s += "<form method='POST' action='/reboot'><button>Reboot</button></form>";
  s += "<div class='muted'>Changes apply immediately; some network/API changes may require a reboot.</div>";
  s += "</body></html>";
  return s;
}

// ---------------- Persistent Storage ----------------
void saveToPrefs() {
  prefs.putString("yt_api",  YT_API_KEY);
  prefs.putString("yt_chan", YT_CHAN_ID);
  prefs.putString("ig_tok",  IG_TOKEN);
  prefs.putString("ig_uid",  IG_USER_ID);
  prefs.putUChar("intens",   displayIntensity);
  prefs.putUShort("scroll",  scrollSpeed);
  prefs.putUInt("fetch",     fetchPeriodMs);
  prefs.putUInt("wshow",     woprShowTimeMs);
  prefs.putUShort("wstep",   woprStepMs);
  prefs.putBool("ig_en",     ENABLE_IG);
}

void loadFromPrefs() {
  YT_API_KEY  = prefs.getString("yt_api",  YT_API_KEY);
  YT_CHAN_ID  = prefs.getString("yt_chan", YT_CHAN_ID);
  IG_TOKEN    = prefs.getString("ig_tok",  IG_TOKEN);
  IG_USER_ID  = prefs.getString("ig_uid",  IG_USER_ID);
  displayIntensity = prefs.getUChar("intens", displayIntensity);
  scrollSpeed      = prefs.getUShort("scroll", scrollSpeed);
  fetchPeriodMs    = prefs.getUInt("fetch", fetchPeriodMs);
  woprShowTimeMs   = prefs.getUInt("wshow", woprShowTimeMs);
  woprStepMs       = prefs.getUShort("wstep", woprStepMs);
  ENABLE_IG        = prefs.getBool("ig_en", ENABLE_IG);
}

// ---------------- HTTP Handlers ----------------
void handleRoot() { server.send(200, "text/html", htmlPage()); }
void handleReboot() { server.send(200, "text/plain", "Rebooting..."); delay(300); ESP.restart(); }

void handleSave() {
  if (server.hasArg("yt_api"))   YT_API_KEY = server.arg("yt_api");
  if (server.hasArg("yt_chan"))  YT_CHAN_ID = server.arg("yt_chan");
  if (server.hasArg("ig_token")) IG_TOKEN   = server.arg("ig_token");
  if (server.hasArg("ig_uid"))   IG_USER_ID = server.arg("ig_uid");
  if (server.hasArg("intensity")) displayIntensity = constrain(server.arg("intensity").toInt(), 0, 15);
  if (server.hasArg("scroll"))    scrollSpeed      = constrain(server.arg("scroll").toInt(), 5, 200);
  if (server.hasArg("fetch"))     fetchPeriodMs    = (uint32_t) server.arg("fetch").toInt();
  if (server.hasArg("woprshow"))  woprShowTimeMs   = (uint32_t) server.arg("woprshow").toInt();
  if (server.hasArg("woprstep"))  woprStepMs       = (uint16_t) server.arg("woprstep").toInt();
  if (server.hasArg("ig_en"))     ENABLE_IG        = (server.arg("ig_en").toInt() != 0);
  saveToPrefs();
  if (mx) mx->control(MD_MAX72XX::INTENSITY, displayIntensity);
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Saved");
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(50);
  prefs.begin("wopr", false);
  loadFromPrefs();
  SPI.begin(18, 19, 23);
  P.begin();
  P.setZone(0, 0, MAX_DEVICES - 1);
  P.setFont(nullptr);
  P.displayClear();
  mx = P.getGraphicObject();
  if (mx) {
    mx->control(MD_MAX72XX::INTENSITY, displayIntensity);
    mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
    mx->clear();
  }

  // WiFi connection with optional static IP
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("wopr-display");
  bool usingDhcp = true;
  if (useStaticIP) {
    bool ok = WiFi.config(hardcodedIP, hardcodedGW, hardcodedSN, hardcodedDNS1, hardcodedDNS2);
    if (!ok) ok = WiFi.config(hardcodedIP, hardcodedGW, hardcodedSN);
    if (ok) usingDhcp = false;
  }
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
  scrollOnce(usingDhcp ? "WiFi DHCP..." : "WiFi Static...", scrollSpeed);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 20000UL) { delay(250); Serial.print("."); }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) scrollOnce("WiFi OK", scrollSpeed);
  else scrollOnce("WiFi FAIL", scrollSpeed);

  secureClient.setInsecure();
  randomSeed(esp_random());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.begin();
  Serial.println("[Web] UI active.");
}

// ---------------- Loop ----------------
void loop() {
  static uint32_t lastFetch = 0;
  static long ytSubs = -1;
  static long igFollowers = -1;
  server.handleClient();

  // Fetch periodically
  if (lastFetch == 0 || millis() - lastFetch >= fetchPeriodMs) {
    lastFetch = millis();
    scrollOnce("Retrieving follower data...", scrollSpeed);
    ytSubs = fetchYouTubeSubs();
    igFollowers = ENABLE_IG ? fetchInstagramFollowers() : -1;
  }

  // Cinematic static
  woprNoise(woprShowTimeMs, woprStepMs);

  // YouTube scroll
  String msgYT = "YouTube Followers: " + fmtCountCompact(ytSubs);
  Serial.println(msgYT);
  scrollFullOverStatic(msgYT, scrollSpeed, 2000);

  // Instagram scroll
  if (ENABLE_IG) {
    String msgIG = "Instagram Followers: " + fmtCountCompact(igFollowers);
    Serial.println(msgIG);
    scrollFullOverStatic(msgIG, scrollSpeed, 2000);
  }

  // Optional idle static
  // woprNoise(8000, woprStepMs);
}
