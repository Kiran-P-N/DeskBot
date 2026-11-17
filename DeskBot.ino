/*
  REMAP 3.0 DESKBOT - ESP32-C3 MINI

  Hardware:
    OLED SSD1306: SDA GPIO8, SCL GPIO9 
    VL53L0X ToF:  SDA GPIO8, SCL GPIO9
    TTP223:       OUT GPIO10

  Behavior:
    - Starts asleep.
    - ToF presence wakes the bot and displays animated eyes.
    - No detected person for 5 seconds puts it back to sleep.
    - Touch while asleep wakes it but does not change the screen.
    - Touch while awake cycles Eyes -> Time -> Weather -> Eyes.
    - Eye moods and animations run only on the Eyes screen.
    - Time uses NTP with IST (UTC+05:30).
    - Weather uses OpenWeatherMap over HTTPS and retries on failure.

  Libraries required:
    Adafruit GFX Library
    Adafruit SSD1306
    Adafruit VL53L0X
    ArduinoJson (version 6 compatible)
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_VL53L0X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

// =====================================================================
// WIFI
// =====================================================================

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// =====================================================================
// OPENWEATHERMAP
// =====================================================================

const char* OWM_API_KEY = "YOUR_OWM_API_KEY";
const char* OWM_CITY = "Kochi,IN";
const char* OWM_UNITS = "metric";

// =====================================================================
// TIME
// =====================================================================

const long GMT_OFFSET_SEC = 5 * 3600 + 30 * 60;
const int DAYLIGHT_OFFSET_SEC = 0;
const char* NTP_SERVER = "pool.ntp.org";

// =====================================================================
// PINS
// =====================================================================

#define I2C_SDA     8
#define I2C_SCL     9
#define TOUCH_PIN   10

// =====================================================================
// OLED
// =====================================================================

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR  0x3C

// =====================================================================
// SLEEP
// =====================================================================

const unsigned long SLEEP_TIMEOUT = 5000;

// =====================================================================
// RANDOM MOOD
// =====================================================================

const unsigned long MIN_MOOD_TIME = 5000;
const unsigned long MAX_MOOD_TIME = 12000;

// =====================================================================
// OBJECTS
// =====================================================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

Adafruit_VL53L0X tof;

bool tofReady = false;

const unsigned long TOF_SAMPLE_INTERVAL = 100;
const uint16_t TOF_DETECTION_DISTANCE_MM = 1000;
const uint8_t TOF_CONFIRMATION_COUNT = 2;

unsigned long lastToFSample = 0;
unsigned long lastPresenceTime = 0;
uint8_t presenceCount = 0;

// =====================================================================
// SCREENS
// =====================================================================

enum Screen {
  SCREEN_EYES,
  SCREEN_TIME,
  SCREEN_WEATHER
};

Screen currentScreen = SCREEN_EYES;

// =====================================================================
// EMOTIONS
// =====================================================================

enum Emotion {
  EMOTION_HAPPY,
  EMOTION_SERIOUS,
  EMOTION_SAD,
  EMOTION_CURIOUS,
  EMOTION_SLEEPY,
  EMOTION_EXCITED
};

Emotion currentEmotion = EMOTION_HAPPY;

// =====================================================================
// SLEEP
// =====================================================================

bool isSleeping = true;

// =====================================================================
// TOUCH
// =====================================================================

bool lastTouchState = false;
unsigned long lastTouchTime = 0;

// =====================================================================
// EYE MOVEMENT
// =====================================================================

int eyeX = 0;
int eyeY = 0;
int targetEyeX = 0;
int targetEyeY = 0;

unsigned long lastLookChange = 0;
unsigned long lookChangeInterval = 2500;

// =====================================================================
// BLINK
// =====================================================================

bool isBlinking = false;

unsigned long blinkStart = 0;
unsigned long lastBlinkTime = 0;
unsigned long nextBlinkInterval = 3000;

// =====================================================================
// EMOTION ANIMATION
// =====================================================================

unsigned long lastMoodChange = 0;
unsigned long nextMoodChange = 8000;

// =====================================================================
// WEATHER
// =====================================================================

float weatherTemp = 0;
String weatherDesc = "";
String weatherIcon = "";

bool weatherOk = false;
String weatherLastError = "";

unsigned long lastWeatherFetch = 0;
unsigned long lastWeatherAttempt = 0;

const unsigned long WEATHER_INTERVAL =
  10UL * 60UL * 1000UL;

const unsigned long WEATHER_RETRY_INTERVAL =
  15UL * 1000UL;

// =====================================================================
// NTP
// =====================================================================

bool timeSynced = false;

// =====================================================================
// SETUP
// =====================================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("      REMAP 3.0 DESKBOT");
  Serial.println("==============================");

  // TOUCH

  pinMode(TOUCH_PIN, INPUT);

  // I2C

  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println("I2C started");

  // OLED

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED FAILED");

    while (true) {
      delay(1000);
    }
  }

  Serial.println("OLED OK - 0x3C");

  // TOF DISTANCE SENSOR

  tofReady = tof.begin(0x29, false, &Wire);

  if (tofReady) {
    Serial.println("VL53L0X OK - 0x29");
  }
  else {
    Serial.println("VL53L0X NOT FOUND - touch wake still works");
  }

  // BOOT

  showBootMessage("Starting...");
  delay(500);

  // WIFI

  connectWiFi();

  // NTP

  Serial.println("Starting NTP...");

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    NTP_SERVER
  );

  // WEATHER

  showBootMessage("Loading...");
  delay(500);

  fetchWeather();

  // INITIAL STATE

  isSleeping = true;
  currentEmotion = EMOTION_SLEEPY;
  currentScreen = SCREEN_EYES;

  lastPresenceTime = millis();
  lastMoodChange = millis();

  randomSeed(esp_random());

  nextMoodChange = random(
    MIN_MOOD_TIME,
    MAX_MOOD_TIME
  );

  Serial.println();
  Serial.println("DESKBOT READY");
  Serial.println();
}

// =====================================================================
// MAIN LOOP
// =====================================================================

void loop() {

  handleTouch();

  updatePresence();

  // Eye animation and random moods are only needed
  // when the bot is awake and showing the Eyes screen.

  if (!isSleeping && currentScreen == SCREEN_EYES) {
    updateEyeTargets();
    updateBlink();
    updateRandomMood();
  }

  updateTimeSync();

  // WEATHER
  // Failed requests retry every 15 seconds.
  // Successful weather refreshes every 10 minutes.

  if (WiFi.status() == WL_CONNECTED) {

    if (
      !weatherOk &&
      millis() - lastWeatherAttempt >= WEATHER_RETRY_INTERVAL
    ) {
      fetchWeather();
    }
    else if (
      weatherOk &&
      millis() - lastWeatherFetch >= WEATHER_INTERVAL
    ) {
      fetchWeather();
    }
  }

  render();

  delay(20);
}

// =====================================================================
// TOUCH
// =====================================================================

void handleTouch() {

  bool touched = digitalRead(TOUCH_PIN) == HIGH;

  // Rising edge.

  if (touched && !lastTouchState) {
    onTouch();
  }

  lastTouchState = touched;
}

// =====================================================================
// TOUCH ACTION
// =====================================================================

void onTouch() {

  Serial.println("TOUCH DETECTED");

  // If sleeping -> WAKE

  if (isSleeping) {

    wakeUp(EMOTION_HAPPY);

    // Stay on eyes screen when waking.

    currentScreen = SCREEN_EYES;

    return;
  }

  // Bot is awake. Touch counts as interaction.

  lastPresenceTime = millis();

  // SCREEN CYCLE

  if (currentScreen == SCREEN_EYES) {

    currentScreen = SCREEN_TIME;

    Serial.println("TOUCH -> TIME");
  }
  else if (currentScreen == SCREEN_TIME) {

    currentScreen = SCREEN_WEATHER;

    Serial.println("TOUCH -> WEATHER + PLACE");
  }
  else {

    currentScreen = SCREEN_EYES;

    currentEmotion = EMOTION_HAPPY;

    Serial.println("TOUCH -> EYES + HAPPY");
  }

  lastTouchTime = millis();
}

// =====================================================================
// WAKE UP
// =====================================================================

void wakeUp(Emotion wakeEmotion) {

  isSleeping = false;
  lastPresenceTime = millis();
  currentEmotion = wakeEmotion;

  Serial.println("BOT -> AWAKE");
}

// =====================================================================
// SLEEP
// =====================================================================

void goToSleep() {

  isSleeping = true;
  currentScreen = SCREEN_EYES;
  currentEmotion = EMOTION_SLEEPY;

  Serial.println("BOT -> SLEEP");
}

// =====================================================================
// TOF PRESENCE AND AUTOMATIC SLEEP/WAKE
// =====================================================================

void updatePresence() {

  if (!tofReady) {

    if (
      !isSleeping &&
      millis() - lastPresenceTime >= SLEEP_TIMEOUT
    ) {
      goToSleep();
    }

    return;
  }

  if (millis() - lastToFSample < TOF_SAMPLE_INTERVAL) {
    return;
  }

  lastToFSample = millis();

  VL53L0X_RangingMeasurementData_t measurement;

  tof.rangingTest(&measurement, false);

  bool detected = false;

  if (
    measurement.RangeStatus != 4 &&
    measurement.RangeMilliMeter > 0 &&
    measurement.RangeMilliMeter <= TOF_DETECTION_DISTANCE_MM
  ) {
    detected = true;
  }

  if (detected) {

    if (presenceCount < TOF_CONFIRMATION_COUNT) {
      presenceCount++;
    }
  }
  else {
    presenceCount = 0;
  }

  bool confirmedPresence =
    (presenceCount >= TOF_CONFIRMATION_COUNT);

  if (confirmedPresence) {

    lastPresenceTime = millis();

    if (isSleeping) {

      Serial.println("ToF: PERSON DETECTED - WAKE");

      wakeUp(EMOTION_HAPPY);

      currentScreen = SCREEN_EYES;
    }
  }
  else {

    if (
      !isSleeping &&
      millis() - lastPresenceTime >= SLEEP_TIMEOUT
    ) {

      Serial.println("ToF: NO PRESENCE FOR 5 SECONDS");

      goToSleep();
    }
  }
}

// =====================================================================
// RANDOM MOOD ENGINE
// =====================================================================

void updateRandomMood() {

  if (isSleeping || currentScreen != SCREEN_EYES) {
    return;
  }

  if (millis() - lastMoodChange > nextMoodChange) {

    int mood = random(0, 6);

    switch (mood) {

      case 0:
        currentEmotion = EMOTION_HAPPY;
        Serial.println("RANDOM MOOD -> HAPPY");
        break;

      case 1:
        currentEmotion = EMOTION_SERIOUS;
        Serial.println("RANDOM MOOD -> SERIOUS");
        break;

      case 2:
        currentEmotion = EMOTION_SAD;
        Serial.println("RANDOM MOOD -> SAD");
        break;

      case 3:
        currentEmotion = EMOTION_CURIOUS;
        Serial.println("RANDOM MOOD -> CURIOUS");
        break;

      case 4:
        currentEmotion = EMOTION_SLEEPY;
        Serial.println("RANDOM MOOD -> SLEEPY");
        break;

      case 5:
        currentEmotion = EMOTION_EXCITED;
        Serial.println("RANDOM MOOD -> EXCITED");
        break;
    }

    lastMoodChange = millis();

    nextMoodChange = random(
      MIN_MOOD_TIME,
      MAX_MOOD_TIME
    );
  }
}

// =====================================================================
// EYE TARGET MOVEMENT
// =====================================================================

void updateEyeTargets() {

  if (millis() - lastLookChange > lookChangeInterval) {

    targetEyeX = random(-10, 11);
    targetEyeY = random(-6, 7);

    lookChangeInterval = random(1500, 4000);

    lastLookChange = millis();
  }

  // Smooth eye movement.

  eyeX += (targetEyeX - eyeX) / 6;
  eyeY += (targetEyeY - eyeY) / 6;
}

// =====================================================================
// BLINK
// =====================================================================

void updateBlink() {

  if (
    !isBlinking &&
    millis() - lastBlinkTime > nextBlinkInterval
  ) {

    isBlinking = true;
    blinkStart = millis();
  }

  if (isBlinking && millis() - blinkStart > 180) {

    isBlinking = false;
    lastBlinkTime = millis();

    nextBlinkInterval = random(2000, 6000);
  }
}

// =====================================================================
// TIME SYNCHRONIZATION
// =====================================================================

void updateTimeSync() {

  if (timeSynced) {
    return;
  }

  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 10)) {

    timeSynced = true;

    Serial.println("NTP TIME SYNC OK");

    Serial.printf(
      "TIME: %02d:%02d:%02d\n",
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    );
  }
}

// =====================================================================
// WIFI
// =====================================================================

void connectWiFi() {

  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 15000
  ) {

    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WIFI CONNECTED");

    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    showBootMessage("WiFi OK");
    delay(500);
  }
  else {

    Serial.println("WIFI FAILED");
    Serial.println("Continuing offline...");

    showBootMessage("WiFi offline");
    delay(500);
  }
}

// =====================================================================
// WEATHER
// =====================================================================

void fetchWeather() {

  lastWeatherAttempt = millis();

  Serial.println();
  Serial.println("------------------------------");
  Serial.println("WEATHER FETCH");
  Serial.println("------------------------------");

  if (WiFi.status() != WL_CONNECTED) {

    weatherOk = false;
    weatherLastError = "WiFi offline";

    Serial.println("WEATHER: WiFi not connected");

    return;
  }

  Serial.print("WiFi IP: ");
  Serial.println(WiFi.localIP());

  if (String(OWM_API_KEY) == "YOUR_OWM_API_KEY") {

    weatherOk = false;
    weatherLastError = "API key missing";

    Serial.println("WEATHER: API key is still placeholder");

    return;
  }

  WiFiClientSecure client;

  // Prototype configuration: no CA certificate required.
  client.setInsecure();

  HTTPClient http;

  String url =
    "https://api.openweathermap.org/data/2.5/weather?id=1273874"
    "&units=" +
    String(OWM_UNITS) +
    "&appid=" +
    String(OWM_API_KEY);

  Serial.println(
    "WEATHER URL: https://api.openweathermap.org/data/2.5/weather?id=1273874&units=metric&appid=********"
  );

  if (!http.begin(client, url)) {

    weatherOk = false;
    weatherLastError = "HTTP begin failed";

    Serial.println("WEATHER: HTTP begin failed");

    return;
  }

  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  int code = http.GET();

  Serial.print("WEATHER HTTP CODE: ");
  Serial.println(code);

  if (code == HTTP_CODE_OK) {

    String payload = http.getString();

    Serial.print("WEATHER RESPONSE: ");
    Serial.println(payload);

    StaticJsonDocument<1536> doc;

    DeserializationError err =
      deserializeJson(doc, payload);

    if (
      !err &&
      doc["main"]["temp"].is<float>() &&
      doc["weather"][0]["main"].is<const char*>() &&
      doc["weather"][0]["icon"].is<const char*>()
    ) {

      weatherTemp =
        doc["main"]["temp"].as<float>();

      weatherDesc =
        String(doc["weather"][0]["main"].as<const char*>());

      weatherIcon =
        String(doc["weather"][0]["icon"].as<const char*>());

      weatherOk = true;
      weatherLastError = "";

      lastWeatherFetch = millis();

      Serial.println("WEATHER OK");

      Serial.print("Temperature: ");
      Serial.print(weatherTemp, 1);
      Serial.println(" C");

      Serial.print("Condition: ");
      Serial.println(weatherDesc);
    }
    else {

      weatherOk = false;
      weatherLastError = "JSON parse/field error";

      Serial.print("WEATHER JSON ERROR: ");
      Serial.println(err.c_str());
    }
  }
  else {

    weatherOk = false;
    weatherLastError = "HTTP " + String(code);

    String errorPayload = http.getString();

    Serial.print("WEATHER HTTP ERROR: ");
    Serial.println(code);

    if (errorPayload.length() > 0) {

      Serial.print("WEATHER ERROR RESPONSE: ");
      Serial.println(errorPayload);
    }
  }

  http.end();

  Serial.println("------------------------------");
}

// =====================================================================
// RENDER
// =====================================================================

void render() {

  display.clearDisplay();

  switch (currentScreen) {

    case SCREEN_EYES:
      drawEyesScreen();
      break;

    case SCREEN_TIME:
      drawTimeScreen();
      break;

    case SCREEN_WEATHER:
      drawWeatherScreen();
      break;
  }

  display.display();
}

// =====================================================================
// EYES SCREEN
// =====================================================================

void drawEyesScreen() {

  int leftCX = 38;
  int rightCX = 90;
  int cy = 32;

  if (isSleeping) {
    drawSleepingEyes();
    return;
  }

  switch (currentEmotion) {

    case EMOTION_HAPPY:
      drawHappyEyes(leftCX, rightCX, cy);
      break;

    case EMOTION_SERIOUS:
      drawSeriousEyes(leftCX, rightCX, cy);
      break;

    case EMOTION_SAD:
      drawSadEyes(leftCX, rightCX, cy);
      break;

    case EMOTION_CURIOUS:
      drawCuriousEyes(leftCX, rightCX, cy);
      break;

    case EMOTION_SLEEPY:
      drawSleepyEyes(leftCX, rightCX, cy);
      break;

    case EMOTION_EXCITED:
      drawExcitedEyes(leftCX, rightCX, cy);
      break;
  }
}

// =====================================================================
// SLEEPING
// =====================================================================

void drawSleepingEyes() {

  int leftCX = 38;
  int rightCX = 90;
  int cy = 32;

  display.drawFastHLine(
    leftCX - 14,
    cy,
    28,
    SSD1306_WHITE
  );

  display.drawFastHLine(
    rightCX - 14,
    cy,
    28,
    SSD1306_WHITE
  );

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(100, 4);
  display.print("z");

  display.setCursor(108, 0);
  display.print("Z");
}

// =====================================================================
// HAPPY
// =====================================================================

void drawHappyEyes(
  int leftCX,
  int rightCX,
  int cy
) {

  int h = isBlinking ? 3 : 25;

  display.fillRoundRect(
    leftCX + eyeX - 11,
    cy + eyeY - h / 2,
    22,
    h,
    7,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    rightCX + eyeX - 11,
    cy + eyeY - h / 2,
    22,
    h,
    7,
    SSD1306_WHITE
  );

  // Sparkles

  display.fillCircle(15, 12, 2, SSD1306_WHITE);
  display.fillCircle(113, 12, 2, SSD1306_WHITE);
}

// =====================================================================
// SERIOUS
// =====================================================================

void drawSeriousEyes(
  int leftCX,
  int rightCX,
  int cy
) {

  int h = isBlinking ? 3 : 25;

  display.fillRoundRect(
    leftCX + eyeX - 11,
    cy + eyeY - h / 2,
    22,
    h,
    3,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    rightCX + eyeX - 11,
    cy + eyeY - h / 2,
    22,
    h,
    3,
    SSD1306_WHITE
  );

  // Angry eyebrows.

  display.drawLine(
    leftCX - 15,
    cy - 18,
    leftCX + 8,
    cy - 12,
    SSD1306_WHITE
  );

  display.drawLine(
    rightCX - 8,
    cy - 12,
    rightCX + 15,
    cy - 18,
    SSD1306_WHITE
  );
}

// =====================================================================
// SAD
// =====================================================================

void drawSadEyes(
  int leftCX,
  int rightCX,
  int cy
) {

  int h = isBlinking ? 3 : 24;

  display.fillRoundRect(
    leftCX + eyeX - 11,
    cy + eyeY - h / 2,
    22,
    h,
    7,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    rightCX + eyeX - 11,
    cy + eyeY - h / 2,
    22,
    h,
    7,
    SSD1306_WHITE
  );

  // Sad eyebrows.

  display.drawLine(
    leftCX - 14,
    cy - 13,
    leftCX + 7,
    cy - 18,
    SSD1306_WHITE
  );

  display.drawLine(
    rightCX - 7,
    cy - 18,
    rightCX + 14,
    cy - 13,
    SSD1306_WHITE
  );

  // Tears.

  display.fillCircle(
    leftCX - 9,
    cy + 16,
    2,
    SSD1306_WHITE
  );

  display.fillCircle(
    rightCX + 9,
    cy + 16,
    2,
    SSD1306_WHITE
  );
}

// =====================================================================
// CURIOUS
// =====================================================================

void drawCuriousEyes(
  int leftCX,
  int rightCX,
  int cy
) {

  // Different eye heights.

  display.fillRoundRect(
    leftCX + eyeX - 10,
    cy + eyeY - 8,
    20,
    20,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    rightCX + eyeX - 12,
    cy + eyeY - 13,
    24,
    28,
    8,
    SSD1306_WHITE
  );

  // Curious eyebrow.

  display.drawLine(
    rightCX - 13,
    cy - 19,
    rightCX + 10,
    cy - 21,
    SSD1306_WHITE
  );
}

// =====================================================================
// SLEEPY
// =====================================================================

void drawSleepyEyes(
  int leftCX,
  int rightCX,
  int cy
) {

  display.fillRoundRect(
    leftCX - 11,
    cy - 5,
    22,
    12,
    5,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    rightCX - 11,
    cy - 5,
    22,
    12,
    5,
    SSD1306_WHITE
  );

  display.drawFastHLine(
    leftCX - 14,
    cy - 10,
    28,
    SSD1306_WHITE
  );

  display.drawFastHLine(
    rightCX - 14,
    cy - 10,
    28,
    SSD1306_WHITE
  );
}

// =====================================================================
// EXCITED
// =====================================================================

void drawExcitedEyes(
  int leftCX,
  int rightCX,
  int cy
) {

  int h = isBlinking ? 3 : 30;

  display.fillRoundRect(
    leftCX + eyeX - 12,
    cy + eyeY - h / 2,
    24,
    h,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    rightCX + eyeX - 12,
    cy + eyeY - h / 2,
    24,
    h,
    8,
    SSD1306_WHITE
  );

  // Excitement rays.

  display.drawLine(
    leftCX - 17,
    cy - 19,
    leftCX - 22,
    cy - 24,
    SSD1306_WHITE
  );

  display.drawLine(
    leftCX - 20,
    cy - 10,
    leftCX - 27,
    cy - 12,
    SSD1306_WHITE
  );

  display.drawLine(
    rightCX + 17,
    cy - 19,
    rightCX + 22,
    cy - 24,
    SSD1306_WHITE
  );

  display.drawLine(
    rightCX + 20,
    cy - 10,
    rightCX + 27,
    cy - 12,
    SSD1306_WHITE
  );
}

// =====================================================================
// TIME SCREEN
// =====================================================================

void drawTimeScreen() {

  struct tm timeinfo;

  display.setTextColor(SSD1306_WHITE);

  if (!timeSynced) {

    display.setTextSize(1);

    display.setCursor(25, 25);
    display.print("SYNCING...");

    display.setCursor(18, 40);
    display.print("Waiting for NTP");

    return;
  }

  if (!getLocalTime(&timeinfo, 10)) {

    timeSynced = false;

    return;
  }

  char timeStr[9];

  strftime(
    timeStr,
    sizeof(timeStr),
    "%H:%M:%S",
    &timeinfo
  );

  char dateStr[16];

  strftime(
    dateStr,
    sizeof(dateStr),
    "%a %d %b",
    &timeinfo
  );

  // Time.

  display.setTextSize(2);

  display.setCursor(6, 18);
  display.print(timeStr);

  // Date.

  display.setTextSize(1);

  display.setCursor(27, 43);
  display.print(dateStr);

  // WiFi.

  display.setCursor(2, 2);

  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi OK");
  }
  else {
    display.print("WiFi --");
  }
}

// =====================================================================
// WEATHER + PLACE SCREEN
// =====================================================================

void drawWeatherScreen() {

  display.setTextColor(SSD1306_WHITE);

  // PLACE

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("PLACE: KOCHI");

  // WEATHER UNAVAILABLE

  if (!weatherOk) {

    display.setCursor(0, 23);
    display.print("Weather unavailable");

    display.setCursor(0, 39);
    display.print("Retrying...");

    return;
  }

  // TEMPERATURE

  display.setTextSize(2);

  display.setCursor(5, 17);
  display.print(weatherTemp, 1);

  display.setTextSize(1);
  display.print(" C");

  // DESCRIPTION

  display.setCursor(5, 43);
  display.print(weatherDesc);

  // WEATHER ICON

  drawWeatherIcon(weatherIcon, 98, 18);
}

// =====================================================================
// WEATHER ICON
// =====================================================================

void drawWeatherIcon(
  String icon,
  int x,
  int y
) {

  // Clear sky.

  if (icon.startsWith("01")) {

    display.fillCircle(
      x + 12,
      y + 12,
      8,
      SSD1306_WHITE
    );
  }

  // Cloud.

  else if (
    icon.startsWith("02") ||
    icon.startsWith("03") ||
    icon.startsWith("04")
  ) {

    display.fillRoundRect(
      x,
      y + 8,
      26,
      12,
      6,
      SSD1306_WHITE
    );
  }

  // Rain.

  else if (
    icon.startsWith("09") ||
    icon.startsWith("10")
  ) {

    display.fillRoundRect(
      x,
      y,
      26,
      10,
      5,
      SSD1306_WHITE
    );

    display.drawFastVLine(
      x + 6,
      y + 12,
      8,
      SSD1306_WHITE
    );

    display.drawFastVLine(
      x + 14,
      y + 12,
      8,
      SSD1306_WHITE
    );

    display.drawFastVLine(
      x + 20,
      y + 12,
      8,
      SSD1306_WHITE
    );
  }

  // Thunder.

  else if (icon.startsWith("11")) {

    display.fillRoundRect(
      x,
      y,
      26,
      10,
      5,
      SSD1306_WHITE
    );

    display.fillTriangle(
      x + 12,
      y + 10,
      x + 6,
      y + 22,
      x + 14,
      y + 16,
      SSD1306_WHITE
    );
  }

  // Snow.

  else if (icon.startsWith("13")) {

    display.fillCircle(
      x + 12,
      y + 12,
      2,
      SSD1306_WHITE
    );

    display.fillCircle(
      x + 4,
      y + 18,
      2,
      SSD1306_WHITE
    );

    display.fillCircle(
      x + 20,
      y + 18,
      2,
      SSD1306_WHITE
    );
  }

  // Unknown.

  else {

    for (int i = 0; i < 4; i++) {

      display.drawFastHLine(
        x,
        y + i * 5,
        26,
        SSD1306_WHITE
      );
    }
  }
}

// =====================================================================
// BOOT MESSAGE
// =====================================================================

void showBootMessage(const char* msg) {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 28);
  display.print(msg);

  display.display();
}