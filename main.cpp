#include <Arduino.h>
#include <math.h>

// LovyanGFX v1の日本語フォントを有効にする
#define LGFX_USE_V1_FONT_JP

#define LGFX_AUTODETECT
#include <LovyanGFX.hpp>

#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// 年、月、日から月齢を計算する関数 (Jean Meeus's algorithm)
double calculateMoonAge(int year, int month, int day) {
    // 年、月、日からユリウス日を計算
    if (month < 3) {
        year--;
        month += 12;
    }
    int a = year / 100;
    int b = a / 4;
    int c = 2 - a + b;
    int e = 365.25 * (year + 4716);
    int f = 30.6001 * (month + 1);
    double jd = c + day + e + f - 1524.5;

    // 2000年1月6日の新月(JD 2451550.1)からの日数を計算
    double days_since_new_moon = jd - 2451550.1;

    // 朔望月で割る
    double new_moons = days_since_new_moon / 29.53058867;

    // 小数部分を取り出し、朔望月を掛ける
    double moon_age = (new_moons - floor(new_moons)) * 29.53058867;

    return moon_age;
}


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "ntp.nict.jp", 9 * 3600, 60000); // 9時間オフセット (JST), 更新間隔60秒

const char* ssid     = "*************";
const char* password = "*************";

// --- OpenWeatherMap設定 ---
const char* apiKey = "***************************";
const char* city = "********"; // 都市名で指定  例）Tokyo,jp

// --- タッチスクリーンと省電力設定 ---
const int touchPin = 33;            // タッチ検出に使用するGPIOピン
const int motionPin = 14;           // 人感センサに使用するGPIOピン
const int touchThreshold = 30;      // タッチ検出の閾値 (値が小さいほど敏感)
unsigned long lastInteractionTime = 0; // 最後の操作（タッチまたは人感）時刻
unsigned long lastMotionCheckTime = 0; // 最後に人感センサをチェックした時刻
const unsigned long displayTimeout = 5 * 60 * 1000; // 5分 (ms)
bool isDisplayOff = false;          // ディスプレイが消灯中かどうかのフラグ

// --- スターフィールド設定 ---
const int DISPLAY_TOTAL_WIDTH = 512;  // 全ディスプレイ合計の幅
const int DISPLAY_TOTAL_HEIGHT = 64; // 全ディスプレイ合計の高さ
const int NUM_STARS = 100;           // 表示する星の数

struct Star {
  float x, y, z; // 3D座標
  float pz;      // 前のフレームのz座標 (トレイル用)
};

Star stars[NUM_STARS]; // 星の配列

unsigned long lastGOLUpdate = 0; // マンデルブロから流用 (名前は後で修正するかも)
unsigned long UpdatestarAdr = 0; // スターフィールドの更新間隔 (ms)
const unsigned long golUpdateInterval = 50; // スターフィールドの更新間隔 (ms)

// 左側ディスプレイ(0x3C)用の設定
class LGFX_Left : public lgfx::LGFX_Device
{
  lgfx::Panel_SSD1306 _panel_instance;
  lgfx::Bus_I2C       _bus_instance;

public:
  LGFX_Left(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 0;              // 使用するI2Cポート (0 or 1)
      cfg.freq_write = 400000;       // 送信クロック
      cfg.freq_read  = 400000;       // 受信クロック
      cfg.pin_scl = 22;              // SCLピン
      cfg.pin_sda = 21;              // SDAピン
      cfg.i2c_addr = 0x3C;           // I2Cデバイスアドレス
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.panel_width = 128;
      cfg.panel_height = 64;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 2; // ここに 2 を指定するとデフォルトで180度回転します
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

// 右側ディスプレイ(0x3D)用の設定
class LGFX_Right : public lgfx::LGFX_Device
{
  lgfx::Panel_SSD1306 _panel_instance;
  lgfx::Bus_I2C       _bus_instance;

public:
  LGFX_Right(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 0;              // 使用するI2Cポート (0 or 1)
      cfg.freq_write = 400000;       // 送信クロック
      cfg.freq_read  = 400000;       // 受信クロック
      cfg.pin_scl = 22;              // SCLピン
      cfg.pin_sda = 21;              // SDAピン
      cfg.i2c_addr = 0x3D;           // I2Cデバイスアドレス
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.panel_width = 128;
      cfg.panel_height = 64;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 2; // ここに 2 を指定するとデフォルトで180度回転します
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

// 2つ目のI2Cバスの左側ディスプレイ(0x3C)用の設定
class LGFX_Left_Bus1 : public lgfx::LGFX_Device
{
  lgfx::Panel_SSD1306 _panel_instance;
  lgfx::Bus_I2C       _bus_instance;

public:
  LGFX_Left_Bus1(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 1;              // 使用するI2Cポート (0 or 1)
      cfg.freq_write = 400000;       // 送信クロック
      cfg.freq_read  = 400000;       // 受信クロック
      cfg.pin_scl = 16;              // SCLピン
      cfg.pin_sda = 17;              // SDAピン
      cfg.i2c_addr = 0x3C;           // I2Cデバイスアドレス
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.panel_width = 128;
      cfg.panel_height = 64;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 2; // ここに 2 を指定するとデフォルトで180度回転します
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

// 2つ目のI2Cバスの右側ディスプレイ(0x3D)用の設定
class LGFX_Right_Bus1 : public lgfx::LGFX_Device
{
  lgfx::Panel_SSD1306 _panel_instance;
  lgfx::Bus_I2C       _bus_instance;

public:
  LGFX_Right_Bus1(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 1;              // 使用するI2Cポート (0 or 1)
      cfg.freq_write = 400000;       // 送信クロック
      cfg.freq_read  = 400000;       // 受信クロック
      cfg.pin_scl = 16;              // SCLピン
      cfg.pin_sda = 17;              // SDAピン
      cfg.i2c_addr = 0x3D;           // I2Cデバイスアドレス
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.panel_width = 128;
      cfg.panel_height = 64;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 2; // ここに 2 を指定するとデフォルトで180度回転します
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

static LGFX_Left lgfx_left;
static LGFX_Right lgfx_right;
static LGFX_Left_Bus1 lgfx_left_bus1;
static LGFX_Right_Bus1 lgfx_right_bus1;

// --- 天気情報取得関数 ---
void updateWeather() {
    Serial.println("Updating weather...");
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + String(city) + "&appid=" + String(apiKey) + "&units=metric&lang=ja";
    http.begin(url);
    Serial.println("HTTP begin (without CA).");

    int httpCode = http.GET();
    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Payload received.");
            
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.c_str());
                // LovyanGFXでのエラー表示
                lgfx_left_bus1.clear();
                lgfx_left_bus1.setCursor(0, 0);
                lgfx_left_bus1.setFont(&fonts::lgfxJapanMinchoP_16);
                lgfx_left_bus1.println("Jsonパースエラー");
                lgfx_right_bus1.clear();
                lgfx_right_bus1.setCursor(0, 0);
                lgfx_right_bus1.setFont(&fonts::lgfxJapanMinchoP_16);
                lgfx_right_bus1.println("Jsonパースエラー");
                http.end();
                return;
            }
            
            Serial.println("JSON parsed.");

            // 天気概況の取得
            String today_weather_desc = doc["list"][0]["weather"][0]["description"].as<String>();
            String tomorrow_weather_desc = doc["list"][8]["weather"][0]["description"].as<String>();

            // 今日の最高・最低気温（現時刻から24時間以内）の計算
            float today_temp_max = -100.0f; // 初期値は非常に小さい値
            float today_temp_min = 100.0f;  // 初期値は非常に大きい値
            for (int i = 0; i < 8; ++i) {
                if (doc["list"][i]) {
                    float max_t = doc["list"][i]["main"]["temp_max"].as<float>();
                    float min_t = doc["list"][i]["main"]["temp_min"].as<float>();
                    if (max_t > today_temp_max) today_temp_max = max_t;
                    if (min_t < today_temp_min) today_temp_min = min_t;
                }
            }

            // 明日の最高・最低気温（24時間後から48時間後まで）の計算
            float tomorrow_temp_max = -100.0f;
            float tomorrow_temp_min = 100.0f;
            for (int i = 8; i < 16; ++i) {
                if (doc["list"][i]) {
                    float max_t = doc["list"][i]["main"]["temp_max"].as<float>();
                    float min_t = doc["list"][i]["main"]["temp_min"].as<float>();
                    if (max_t > tomorrow_temp_max) tomorrow_temp_max = max_t;
                    if (min_t < tomorrow_temp_min) tomorrow_temp_min = min_t;
                }
            }
            
            Serial.println("Weather data extracted.");
            Serial.print("Today: "); Serial.print(today_weather_desc); Serial.print(" Max:"); Serial.print(today_temp_max); Serial.print(" Min:"); Serial.println(today_temp_min);
            Serial.print("Tomorrow: "); Serial.print(tomorrow_weather_desc); Serial.print(" Max:"); Serial.print(tomorrow_temp_max); Serial.print(" Min:"); Serial.println(tomorrow_temp_min);


            // 3枚目のOLED (lgfx_left_bus1) に今日の天気を表示
            int margin = -2; // 行間のマージン調整
            lgfx_left_bus1.clear();
            lgfx_left_bus1.setCursor(0, 0);
            lgfx_left_bus1.setFont(&fonts::lgfxJapanGothicP_20);
            lgfx_left_bus1.print("今日:");
            lgfx_left_bus1.println(today_weather_desc);
            char temp_str_today[30];
            int h1 = lgfx_left_bus1.fontHeight() + margin;
            sprintf(temp_str_today, "Max:%.0f°C", today_temp_max);
            lgfx_left_bus1.setFont(&fonts::efontJA_24);
            lgfx_left_bus1.setCursor(0, h1);
            lgfx_left_bus1.println(temp_str_today);
            sprintf(temp_str_today, "Min:%.0f°C", today_temp_min);
            int h2 = lgfx_left_bus1.fontHeight() + margin;
            lgfx_left_bus1.setCursor(0, h1 + h2);
            lgfx_left_bus1.println(temp_str_today);

            // 4枚目のOLED (lgfx_right_bus1) に明日の天気を表示
            lgfx_right_bus1.clear();
            lgfx_right_bus1.setCursor(0, 0);
            lgfx_right_bus1.setFont(&fonts::lgfxJapanGothicP_20);
            lgfx_right_bus1.print("明日:");
            lgfx_right_bus1.println(tomorrow_weather_desc);
            char temp_str_tomorrow[30];
            lgfx_right_bus1.setFont(&fonts::efontJA_24);
            sprintf(temp_str_tomorrow, "Max:%.0f°C", tomorrow_temp_max);
            lgfx_right_bus1.setCursor(0, h1);
            lgfx_right_bus1.println(temp_str_tomorrow);
            sprintf(temp_str_tomorrow, "Min:%.0f°C", tomorrow_temp_min);
            lgfx_right_bus1.setCursor(0, h1 + h2);
            lgfx_right_bus1.println(temp_str_tomorrow);
            
            Serial.println("OLEDs updated.");
        } else {
            // LovyanGFXでのエラー表示
            lgfx_left_bus1.clear();
            lgfx_left_bus1.setCursor(0, 0);
            lgfx_left_bus1.setFont(&fonts::lgfxJapanMinchoP_16);
            char http_err_str[20];
            sprintf(http_err_str, "HTTPエラー: %d", httpCode);
            lgfx_left_bus1.println(http_err_str);
            lgfx_right_bus1.clear();
            lgfx_right_bus1.setCursor(0, 0);
            lgfx_right_bus1.setFont(&fonts::lgfxJapanMinchoP_16);
            lgfx_right_bus1.println(http_err_str);
        }
    } else {
        Serial.print("HTTP GET failed, error: ");
        Serial.println(http.errorToString(httpCode).c_str());

        // LovyanGFXでのエラー表示
        lgfx_left_bus1.clear();
        lgfx_left_bus1.setCursor(0, 0);
        lgfx_left_bus1.setFont(&fonts::lgfxJapanMinchoP_16);
        lgfx_left_bus1.println("HTTP GET失敗");
        lgfx_right_bus1.clear();
        lgfx_right_bus1.setCursor(0, 0);
        lgfx_right_bus1.setFont(&fonts::lgfxJapanMinchoP_16);
        lgfx_right_bus1.println("HTTP GET失敗");
    }

    http.end();
    Serial.println("updateWeather() finished.");
}

// スターフィールドの星を初期化
void initStars() {
  for (int i = 0; i < NUM_STARS; i++) {
    stars[i].x = random(-DISPLAY_TOTAL_WIDTH / 2, DISPLAY_TOTAL_WIDTH / 2); // 画面中央を(0,0)として
    stars[i].y = random(-DISPLAY_TOTAL_HEIGHT / 2, DISPLAY_TOTAL_HEIGHT / 2); // 画面中央を(0,0)として
    stars[i].z = random(1, DISPLAY_TOTAL_WIDTH / 2); // Z軸の奥行き (遠いほど数値が大きい)
    stars[i].pz = stars[i].z; // 前フレームのZ座標も初期化
  }
}

// スターフィールドを描画
void drawStarfield() {
  // lgfx_left.clear(); // 全面クリアを削除
  // lgfx_right.clear();
  // lgfx_left_bus1.clear();
  // lgfx_right_bus1.clear();

  for (int i = 0; i < NUM_STARS; i++) {
    // 前の星の位置を黒で消す (トレイル)
    float psx = (stars[i].x / stars[i].pz) * (DISPLAY_TOTAL_WIDTH / 2) + (DISPLAY_TOTAL_WIDTH / 2);
    float psy = (stars[i].y / stars[i].pz) * (DISPLAY_TOTAL_HEIGHT / 2) + (DISPLAY_TOTAL_HEIGHT / 2);

    int prev_display_index_x = (int)psx / 128;
    int prev_local_x = (int)psx % 128;
    int prev_local_y = (int)psy;

    int prev_star_size = map((int)stars[i].pz, 1, DISPLAY_TOTAL_WIDTH / 2, 2, 0);
    if (prev_star_size == 0) prev_star_size = 1;

    // 前の星を消去
    switch (prev_display_index_x) {
      case 0: lgfx_left.fillRect(prev_local_x, prev_local_y, prev_star_size, prev_star_size, TFT_BLACK); break;
      case 1: lgfx_right.fillRect(prev_local_x, prev_local_y, prev_star_size, prev_star_size, TFT_BLACK); break;
      case 2: lgfx_left_bus1.fillRect(prev_local_x, prev_local_y, prev_star_size, prev_star_size, TFT_BLACK); break;
      case 3: lgfx_right_bus1.fillRect(prev_local_x, prev_local_y, prev_star_size, prev_star_size, TFT_BLACK); break;
    }


    // スクリーン座標に変換 (新しい位置)
    float sx = (stars[i].x / stars[i].z) * (DISPLAY_TOTAL_WIDTH / 2) + (DISPLAY_TOTAL_WIDTH / 2);
    float sy = (stars[i].y / stars[i].z) * (DISPLAY_TOTAL_HEIGHT / 2) + (DISPLAY_TOTAL_HEIGHT / 2);

    // 画面外に出たら星をリセット
    if (sx < 0 || sx > DISPLAY_TOTAL_WIDTH || sy < 0 || sy > DISPLAY_TOTAL_HEIGHT || stars[i].z < 0.1) {
      stars[i].x = random(-DISPLAY_TOTAL_WIDTH / 2, DISPLAY_TOTAL_WIDTH / 2);
      stars[i].y = random(-DISPLAY_TOTAL_HEIGHT / 2, DISPLAY_TOTAL_HEIGHT / 2);
      stars[i].z = random(1, DISPLAY_TOTAL_WIDTH / 2);
      stars[i].pz = stars[i].z;
    } else {
      // 描画 (新しい星)
      int display_index_x = (int)sx / 128; // 0, 1, 2, 3
      int local_x = (int)sx % 128;
      int local_y = (int)sy;

      // 奥行きに応じて星の大きさを変える
      int star_size = map((int)stars[i].z, 1, DISPLAY_TOTAL_WIDTH / 2, 2, 0);
      if (star_size == 0) star_size = 1; // 最小1ピクセル
      uint16_t color = TFT_WHITE; // 白
      
      switch (display_index_x) {
        case 0: lgfx_left.fillRect(local_x, local_y, star_size, star_size, color); break;
        case 1: lgfx_right.fillRect(local_x, local_y, star_size, star_size, color); break;
        case 2: lgfx_left_bus1.fillRect(local_x, local_y, star_size, star_size, color); break;
        case 3: lgfx_right_bus1.fillRect(local_x, local_y, star_size, star_size, color); break;
      }

      // 星を動かす (z値を減らす)
      stars[i].pz = stars[i].z; // 現在のzを前のzに保存
      stars[i].z -= 0.5; // 速度
    }
  }
}


/**
 * 月を描画する関数
 * @param x, y : 中心座標
 * @param r    : 半径 (今回は16)
 * @param age  : 月齢 (0.0 〜 29.5)
 */
void drawMoon(int x, int y, int r, float age) {
  // 1. 全体を「影」の色で塗る (黒または暗いグレー)
  lgfx_left.fillCircle(x, y, r, TFT_BLACK); 
  // 輪郭だけ描いておくと新月の時も位置がわかります
  lgfx_left.drawCircle(x, y, r, TFT_DARKGRAY); 

  // 月齢を 0.0〜1.0 の比率に変換 (0:新月, 0.5:満月, 1.0:新月)
  float phase = age / 29.53;
  if (phase > 1.0) phase = 0.0;

  // 左右どちらが光るか判定 (半分より前なら右が光る)
  bool waxing = (phase < 0.5); 
  
  // 光の幅を計算 (-r 〜 +r)
  // cosを使って満ち欠けの「厚み」をシミュレート
  float cp = cos(phase * 2.0 * PI);
  int w = abs(r * cp);

  if (waxing) {
    // 【満ちていくとき】右半分を半円で塗り、左半分を楕円の影or光で調整
    lgfx_left.fillArc(x, y, 0, r, 270, 90, TFT_WHITE); // 右半円
    if (phase < 0.25) {
      // 三日月：左側の楕円で「影」を上書き
      lgfx_left.fillEllipse(x, y, w, r, TFT_BLACK);
    } else {
      // 半月〜満月：左側の楕円で「光」を追加
      lgfx_left.fillEllipse(x, y, w, r, TFT_WHITE);
    }
  } else {
    // 【欠けていくとき】左半分を半円で塗り、右半分を楕円で調整
    lgfx_left.fillArc(x, y, 0, r, 90, 270, TFT_WHITE); // 左半円
    if (phase < 0.75) {
      // 満月〜下弦：右側の楕円で「光」を追加
      lgfx_left.fillEllipse(x, y, w, r, TFT_WHITE);
    } else {
      // 二十六夜〜新月：右側の楕円で「影」を上書き
      lgfx_left.fillEllipse(x, y, w, r, TFT_BLACK);
    }
  }
}
// 1枚目のOLED（日付と曜日・月齢）を表示する関数
void displayDateOfWeek() {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = localtime(&epochTime);

    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%Y/%m/%d", ptm);

    char dayOfWeekStr[10];
    switch (ptm->tm_wday) {
      case 0: strcpy(dayOfWeekStr, "(日)"); break;
      case 1: strcpy(dayOfWeekStr, "(月)"); break;
      case 2: strcpy(dayOfWeekStr, "(火)"); break;
      case 3: strcpy(dayOfWeekStr, "(水)"); break;
      case 4: strcpy(dayOfWeekStr, "(木)"); break;
      case 5: strcpy(dayOfWeekStr, "(金)"); break;
      case 6: strcpy(dayOfWeekStr, "(土)"); break;
      default: strcpy(dayOfWeekStr, ""); break;
    }

    // 月齢を計算
    double moonAge = calculateMoonAge(ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);

    lgfx_left.clear();
    lgfx_left.setCursor(0, 0);
    lgfx_left.setFont(&fonts::Font4);
    lgfx_left.println(dateStr);
    lgfx_left.setFont(&fonts::lgfxJapanGothicP_24);
    lgfx_left.println(dayOfWeekStr);
    
    // 月齢画像をOLED右側に描画
    drawMoon(110, 40, 16, moonAge);
    char moonAgeStr[10];
    lgfx_left.setCursor(70, 40);
    lgfx_left.setFont(&fonts::efontJA_24);
    lgfx_left.setTextSize(1);
    sprintf(moonAgeStr, "%d", (int)(moonAge + 0.5));
    lgfx_left.println(moonAgeStr);
    lgfx_left.setTextSize(1.0);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("OLED Test");

  // 左側ディスプレイの初期化
  if (!lgfx_left.init()) {
    Serial.println("Left display initialization failed");
    return;
  }

  // 右側ディスプレイの初期化
  if (!lgfx_right.init()) {
    Serial.println("Right display initialization failed");
    return;
  }

  // 2つ目のI2Cバスの左側ディスプレイの初期化
  if (!lgfx_left_bus1.init()) {
    Serial.println("Left display (Bus 1) initialization failed");
    return;
  }

  // 2つ目のI2Cバスの右側ディスプレイの初期化
  if (!lgfx_right_bus1.init()) {
    Serial.println("Right display (Bus 1) initialization failed");
    return;
  }
  
  pinMode(motionPin, INPUT); // 人感センサのピンを入力に設定

  lgfx_left.setBrightness(32); // 明るさ設定(max=255)
  lgfx_right.setBrightness(32); // 明るさ設定(max=255)
  lgfx_left_bus1.setBrightness(32); // 明るさ設定(max=255)
  lgfx_right_bus1.setBrightness(32); // 明るさ設定(max=255)
  
  // WiFi接続
  lgfx_left.clear();
  lgfx_left.setCursor(0, 0);
  lgfx_left.setFont(&fonts::Font2); // 小さめのフォントを選択
  lgfx_left.println("Connecting to WiFi");
  lgfx_left.println(""); // 2行目にドットを表示するために改行

  Serial.print("Connecting to WiFi ");
  WiFi.begin(ssid, password);
  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lgfx_left.print("."); // ドットを追加
    dotCount++;
    if (dotCount > 10) { // 長くなったらリセットして再度表示
      lgfx_left.setCursor(0, lgfx_left.fontHeight() + 2); // 2行目
      lgfx_left.print("          "); // ドットを消す
      lgfx_left.setCursor(0, lgfx_left.fontHeight() + 2); // 2行目
      dotCount = 0;
    }
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  lgfx_left.clear(); // 接続完了後、一度クリアして次の表示に備える
  lgfx_left.setCursor(0, 0);
  lgfx_left.setFont(&fonts::Font2); // フォントをリセット
  lgfx_left.println("WiFi connected.");
  lgfx_left.print("IP: ");
  lgfx_left.println(WiFi.localIP());
  delay(2000); // しばらく表示
  
  // NTPクライアントの初期化
  timeClient.begin();
  timeClient.update();
  Serial.println("NTP client initialized and time updated.");

  updateWeather();
  displayDateOfWeek(); 
  
  randomSeed(analogRead(0)); // 乱数シードを初期化
  initStars(); // スターフィールドを初期化

  lastInteractionTime = millis(); // 最後の操作時刻を初期化
  lastMotionCheckTime = millis(); // 人感センサの最終チェック時刻を初期化
}

void loop() {
  // --- ループ全体で共有するstatic変数 ---
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long lastWeatherUpdate = 0;
  static int last_day = -1;
  static int last_minute = -1;
  static int last_moon_age = -1;
  static int last_triangle_x_center = -1;
  static int time_display_bottom_y = 30;

  // --- タッチ入力の検出と人感センサのチェック ---
  int touchValue = touchRead(touchPin);
  bool interactionDetected = false;

  // タッチ検出
  if (touchValue < touchThreshold) {
    interactionDetected = true;
  }

  // 人感センサ検出 (1秒おき)
  if (millis() - lastMotionCheckTime > 1000) {
    lastMotionCheckTime = millis();
    if (digitalRead(motionPin) == HIGH) {
      interactionDetected = true;
      Serial.println("Motion detected!");
    }
  }

  if (interactionDetected) {
    lastInteractionTime = millis(); // 最後の操作時刻を更新

    if (isDisplayOff) {
      isDisplayOff = false;
      Serial.println("Waking up from screensaver.");
      
      // ディスプレイを復帰させ、明るさを設定
      lgfx_left.wakeup();
      lgfx_right.wakeup();
      lgfx_left_bus1.wakeup();
      lgfx_right_bus1.wakeup();

      lgfx_left.init(); // 時刻表示の再初期化
      lgfx_right.init(); // 時刻表示の再初期化
      lgfx_left_bus1.init(); // 時刻表示の再初期化
      lgfx_right_bus1.init(); // 時刻表示の再初期化

      lgfx_left.setBrightness(32);
      lgfx_right.setBrightness(32);
      lgfx_left_bus1.setBrightness(32);
      lgfx_right_bus1.setBrightness(32);
      
      // 全てのディスプレイをクリア
      lgfx_left.clear();
      lgfx_right.clear();
      lgfx_left_bus1.clear();
      lgfx_right_bus1.clear();
      
      // 通常画面を再描画
      displayDateOfWeek();
      updateWeather();
      
      // 時刻表示を強制的に再描画させる
      last_minute = -1;
      
      return; // 復帰処理を完了し、次のループサイクルから通常表示を再開
    }
  }

  // --- モードに応じた処理 ---
  if (!isDisplayOff) {
    // === 通常表示モード ===
    
    // タイムアウトをチェックし、スターフィールドモードに移行
    if (millis() - lastInteractionTime > displayTimeout) {
      Serial.println("Timeout. Entering Starfield.");
      isDisplayOff = true;
      initStars(); // スターフィールドを初期化
      
      // 画面を一旦すべてクリア
      lgfx_left.clear();
      lgfx_right.clear();
      lgfx_left_bus1.clear();
      lgfx_right_bus1.clear();
      
      lastGOLUpdate = millis(); // スターフィールドの初回更新タイミングを設定
      UpdatestarAdr = millis(); // スターフィールドの開始行リセットタイミングを設定
      return; // 次のループサイクルからスターフィールドを開始
    }

    // --- 以下、従来の表示更新処理 ---
    timeClient.update();

    if (millis() - lastDisplayUpdate > 1000) { // 1秒ごとに更新
      lastDisplayUpdate = millis();

      time_t epochTime = timeClient.getEpochTime();
      struct tm *ptm = localtime(&epochTime);

      // 月齢を計算 (四捨五入して整数にする)
      double currentMoonAge = calculateMoonAge(ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
      int currentMoonAgeInt = (int)(currentMoonAge + 0.5);

      // 日付の更新、または月齢（整数値）の更新
      if (last_day == -1 || ptm->tm_mday != last_day || currentMoonAgeInt != last_moon_age) {
          displayDateOfWeek();
          last_day = ptm->tm_mday;
          last_moon_age = currentMoonAgeInt;
      }
      
      // 時刻の更新
      char timeStr[20];
      strftime(timeStr, sizeof(timeStr), "%H:%M", ptm);
      int current_minute = ptm->tm_min;
      int indicator_offset = 5;
      int triangle_height = 10;
      int triangle_base_width = 5;


      if (current_minute != last_minute) {
          last_minute = current_minute;
          //lgfx_right.clear();
          lgfx_right.setCursor(0, 0);
          lgfx_right.setTextColor(TFT_WHITE, TFT_BLACK); // 白文字、背景は黒
          lgfx_right.setFont(&fonts::Font7);
          lgfx_right.setTextSize(0.9);
          lgfx_right.println(timeStr);
          time_display_bottom_y = lgfx_right.getCursorY();

          int indicator_offset = 5;
          int indicator_start_y = time_display_bottom_y + indicator_offset;
          int line_y = indicator_start_y - 2;
          lgfx_right.drawFastHLine(4, line_y, lgfx_right.width() - 8, TFT_WHITE);
          int effective_width = lgfx_right.width() - 8;
          int start_x = 4;
          int vline_y_start = line_y + 1;
          int vline_height = 4;
          lgfx_right.drawFastVLine(start_x + effective_width / 4, vline_y_start, vline_height, TFT_WHITE);
          lgfx_right.drawFastVLine(start_x + effective_width / 2, vline_y_start, vline_height, TFT_WHITE);
          lgfx_right.drawFastVLine(start_x + effective_width * 3 / 4, vline_y_start, vline_height, TFT_WHITE);
          //int triangle_base_width = 5;
          //int triangle_height = 10;
          lgfx_right.fillTriangle(last_triangle_x_center, indicator_start_y, last_triangle_x_center - triangle_base_width / 2, indicator_start_y + triangle_height, last_triangle_x_center + triangle_base_width / 2, indicator_start_y + triangle_height, TFT_BLACK);
      } else {
          if (last_triangle_x_center != -1) {
              //int indicator_offset = 5;
              //int triangle_height = 10;
              //int triangle_base_width = 5;
              int indicator_start_y = time_display_bottom_y + indicator_offset;
              lgfx_right.fillTriangle(last_triangle_x_center, indicator_start_y, last_triangle_x_center - triangle_base_width / 2, indicator_start_y + triangle_height, last_triangle_x_center + triangle_base_width / 2, indicator_start_y + triangle_height, TFT_BLACK);
          }
      }

      int sec = ptm->tm_sec;
      int display_width = lgfx_right.width();
      //int indicator_offset = 5;
      //int triangle_height = 10;
      //int triangle_base_width = 5;
      int indicator_start_y = time_display_bottom_y + indicator_offset;
      float x_center_float = (float)sec / 59.0f * (display_width - 8);
      int x_center = round(x_center_float + 4);
      lgfx_right.fillTriangle(x_center, indicator_start_y, x_center - triangle_base_width / 2, indicator_start_y + triangle_height, x_center + triangle_base_width / 2, indicator_start_y + triangle_height, TFT_WHITE);
      last_triangle_x_center = x_center;
    }

    // 天気予報の更新 (30分ごと)
    if (millis() - lastWeatherUpdate > 1800000) {
      lastWeatherUpdate = millis();
      updateWeather();
    }
    
  } else {
    // === スターフィールドモード ===
    if (millis() - lastGOLUpdate > golUpdateInterval) {
      lastGOLUpdate = millis();
      drawStarfield(); // スターフィールドを描画
      if(millis() - UpdatestarAdr >10000 ) { // 10秒ごとに開始行をリセットして描画の乱れを防止
        UpdatestarAdr = millis();
        lgfx_right.startWrite();       // 通信開始を宣言
        lgfx_right.writeCommand(0x40); // 開始行を0にリセット
        lgfx_right.endWrite();         // 通信終了を宣言
        lgfx_left.startWrite();       // 通信開始を宣言
        lgfx_left.writeCommand(0x40); // 開始行を0にリセット
        lgfx_left.endWrite();         // 通信終了を宣言
        lgfx_left_bus1.startWrite();       // 通信開始を宣言
        lgfx_left_bus1.writeCommand(0x40); // 開始行を0にリセット
        lgfx_left_bus1.endWrite();         // 通信終了を宣言
        lgfx_right_bus1.startWrite();       // 通信開始を宣言
        lgfx_right_bus1.writeCommand(0x40); // 開始行を0にリセット
        lgfx_right_bus1.endWrite();         // 通信終了を宣言
      }
    }
  }
}
