#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <time.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>

/* ===================== DISPLAY ===================== */
#define CS_PIN 5
#define DC_PIN 17
#define RES_PIN 16
#define BUSY_PIN 4

GxEPD2_3C<GxEPD2_213_Z98c, GxEPD2_213_Z98c::HEIGHT> display(
  GxEPD2_213_Z98c(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN));

/* ===================== WIFI ===================== */
const char* ssid = "SSID";
const char* password = "PASSWORD";

/* ===================== TIME ===================== */
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 3600       // CET
#define DAYLIGHT_OFFSET_SEC 3600  // CEST

/* ===================== STORAGE ===================== */
Preferences prefs;

/* ===================== ASSETS ===================== */
struct Asset {
  const char* label;
  const char* symbol;
  float shares;
  bool isGold;
  float history[5];  // last 5 closing prices
};

Asset assets[] = {
  { "VGWD", "vgwd.de", 2100, false, { 0, 0, 0, 0, 0 } },
  { "VWCE", "vwce.de", 700, false, { 0, 0, 0, 0, 0 } },
  { "IUSU", "iusu.de", 450, false, { 0, 0, 0, 0, 0 } },
  { "SXRH", "sxrh.de", 10000, false, { 0, 0, 0, 0, 0 } },
  { "EUNX", "eunx.de", 300, false, { 0, 0, 0, 0, 0 } },
  { "Gold", "4gld.de", 5, true, { 0, 0, 0, 0, 0 } }
};

const int ASSET_COUNT = sizeof(assets) / sizeof(assets[0]);

/* ===================== HELPERS ===================== */

String formatThousands(int value) {
  String s = String(value);
  String out = "";
  int count = 0;
  for (int i = s.length() - 1; i >= 0; i--) {
    out = s[i] + out;
    if (++count % 3 == 0 && i != 0) out = "," + out;
  }
  return out;
}

void drawSparkline(int x, int y, float* v) {
  float minV = v[0], maxV = v[0];
  for (int i = 1; i < 5; i++) {
    if (v[i] < minV) minV = v[i];
    if (v[i] > maxV) maxV = v[i];
  }
  if (maxV - minV < 0.01) return;

  int w = 20, h = 10;
  for (int i = 0; i < 4; i++) {
    int x1 = x + (i * w) / 4;
    int x2 = x + ((i + 1) * w) / 4;
    int y1 = y - ((v[i] - minV) / (maxV - minV)) * h;
    int y2 = y - ((v[i + 1] - minV) / (maxV - minV)) * h;
    display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
  }
}

/* ===================== TIME LOGIC ===================== */

bool isTradingDay() {
  struct tm t;
  getLocalTime(&t);
  return (t.tm_wday != 0 && t.tm_wday != 6);
}

uint64_t secondsUntilNextClose() {
  time_t now;
  time(&now);

  struct tm t;
  localtime_r(&now, &t);
  t.tm_hour = 18;
  t.tm_min = 0;
  t.tm_sec = 0;

  if (mktime(&t) <= now) t.tm_mday++;

  while (true) {
    mktime(&t);
    if (t.tm_wday != 0 && t.tm_wday != 6) break;
    t.tm_mday++;
  }
  return difftime(mktime(&t), now);
}

/* ===================== NETWORK ===================== */

float fetchPrice(const char* symbol, bool gold) {
  String url = String("https://stooq.com/q/l/?s=") + symbol + "&f=sd2t2ohlcv&h&e=csv";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  https.begin(client, url);
  https.addHeader("User-Agent", "Mozilla/5.0");

  float price = 0;
  if (https.GET() == 200) {
    String p = https.getString();
    int nl = p.indexOf('\n');
    String line = p.substring(nl + 1);
    int c = 0, pos = 0;
    while (c++ < 6) pos = line.indexOf(',', pos) + 1;
    int end = line.indexOf(',', pos);
    price = line.substring(pos, end).toFloat();
    if (gold) price *= 31.1035;
  }
  https.end();
  return price;
}

/* ===================== DISPLAY ===================== */

void showPortfolio() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    int y0 = 16;
    int lh = 16;
    int priceX = 50;
    int sparkX = 128;
    int totalX = 178;

    int sum = 0;

    for (int i = 0; i < ASSET_COUNT; i++) {
      int y = y0 + i * lh;
      float last = assets[i].history[4];
      float prev = assets[i].history[3];
      bool down = (prev > 0 && last < prev);

      display.setCursor(0, y);
      display.setTextColor(GxEPD_BLACK);
      display.print(String(assets[i].label) + " ");

      char buf[10];
      snprintf(buf, sizeof(buf), "%7.2f", last);
      display.setCursor(priceX, y);
      display.setTextColor(down ? GxEPD_RED : GxEPD_BLACK);
      display.print(buf);

      drawSparkline(sparkX + 2, y, assets[i].history);

      int total = round(last * assets[i].shares);
      sum += total;
      String ts = formatThousands(total);
      int16_t tbx, tby;
      uint16_t tbw, tbh;
      display.getTextBounds(ts, 0, 0, &tbx, &tby, &tbw, &tbh);
      display.setCursor(totalX + (70 - tbw), y);
      display.setTextColor(down ? GxEPD_RED : GxEPD_BLACK);
      display.print(ts);
    }

    String sumStr = formatThousands(sum);
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(sumStr, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(totalX + (70 - tbw), y0 + ASSET_COUNT * lh + 2);
    display.setTextColor(GxEPD_BLACK);
    display.print(sumStr);

  } while (display.nextPage());
}

/* ===================== SETUP ===================== */

void setup() {
  btStop();
  display.init(115200, true, 50, false);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();
  display.setRotation(1);

  prefs.begin("prices", false);
  for (int i = 0; i < ASSET_COUNT; i++)
    for (int j = 0; j < 5; j++)
      assets[i].history[j] = prefs.getFloat(
        (String(assets[i].label) + j).c_str(), 0);

  WiFi.begin(ssid, password);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    if (millis() - start > 15000) break;
  }

  if (WiFi.status() != WL_CONNECTED) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setCursor(10, 40);
      display.print("WiFi FAILED");
    } while (display.nextPage());
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm t;
  while (!getLocalTime(&t)) delay(200);

  if (isTradingDay()) {
    for (int i = 0; i < ASSET_COUNT; i++) {
      float p = fetchPrice(assets[i].symbol, assets[i].isGold);
      for (int j = 0; j < 4; j++) assets[i].history[j] = assets[i].history[j + 1];
      assets[i].history[4] = p;
    }
    for (int i = 0; i < ASSET_COUNT; i++)
      for (int j = 0; j < 5; j++)
        prefs.putFloat((String(assets[i].label) + j).c_str(), assets[i].history[j]);
  }

  prefs.end();
  showPortfolio();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  display.hibernate();

  esp_sleep_enable_timer_wakeup(secondsUntilNextClose() * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
