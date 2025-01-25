#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/gpio.h>

// use an ESP-32 to control a smart outlet configured within homebridge.
// I used a single pole lightswitch because I liked the idea of embedding it
// in a wall, but it was very impractical. you may want to simplify by
// hooking up a standard momentary press button instead.

// wifi
const char *WIFI_SSID = "SSID_GOES_HERE";
const char *WIFI_PASSWORD = "PASSWORD_GOES_HERE";

// homebridge config
const char *HOMEBRIDGE_HOST = ""; // local network address of your homebridge server
const int HOMEBRIDGE_PORT = 8581; // homebridge default is 8581 unless you configured a custom port
const char *HOMEBRIDGE_USERNAME = "";
const char *HOMEBRIDGE_PASSWORD = "";
const char *TARGET_DEVICE_NAME = ""; // name to search for in accessories list, e.g. "Bedroom Floor Lamp"

// hardware config
const bool ENABLE_DEEP_SLEEP = true; // set false to disable deep sleep for debugging
const gpio_num_t SWITCH_PIN = GPIO_NUM_33; // GPIO33 is a real time clock pin

// app state
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR char savedDeviceId[128];
RTC_DATA_ATTR char savedAuthToken[512];

String authToken = "";
String deviceId = "";

bool getHomeBridgeToken(bool usePort = false) {
  HTTPClient http;
  String url = "http://" + String(HOMEBRIDGE_HOST);
  if (usePort) {
    url += ":" + String(HOMEBRIDGE_PORT);
  }
  url += "/api/auth/login";
  Serial.printf("Trying auth request to: %s\n", url.c_str());

  StaticJsonDocument<200> doc;
  doc["username"] = HOMEBRIDGE_USERNAME;
  doc["password"] = HOMEBRIDGE_PASSWORD;
  String jsonString;
  serializeJson(doc, jsonString);
  Serial.printf("Auth request payload: %s\n", jsonString.c_str());

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(jsonString);
  String payload = http.getString();
  Serial.printf("Auth request response code: %d\n", httpCode);
  Serial.printf("Auth request response body: %s\n", payload.c_str());

  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)
  {
    StaticJsonDocument<1024> response;
    DeserializationError error = deserializeJson(response, payload);
    if (error)
    {
      Serial.printf("JSON parsing failed: %s\n", error.c_str());
      http.end();
      return false;
    }
    authToken = response["access_token"].as<String>();
    Serial.printf("Token received successfully: %s\n", authToken.c_str());
    http.end();
    return true;
  }
  http.end();
  return false;
}

bool findDeviceId(bool usePort = false)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String url = "http://" + String(HOMEBRIDGE_HOST);
    if (usePort) {
      url += ":" + String(HOMEBRIDGE_PORT);
    }
    url += "/api/accessories";
    Serial.printf("Trying accessories list from: %s\n", url.c_str());

    http.begin(url);
    http.addHeader("Authorization", "Bearer " + authToken);
    int httpCode = http.GET();
    String payload = http.getString();
    Serial.printf("Accessories request response code: %d\n", httpCode);
    Serial.printf("Accessories request response body: %s\n", payload.c_str());

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)
    {
      DynamicJsonDocument doc(32768);
      DeserializationError error = deserializeJson(doc, payload);
      if (error)
      {
        Serial.printf("JSON parsing failed: %s\n", error.c_str());
        http.end();
        return false;
      }

      JsonArray accessories = doc.as<JsonArray>();
      Serial.printf("Searching through %d accessories...\n", accessories.size());
      for (JsonVariant accessory : accessories)
      {
        String name = accessory["serviceName"].as<String>();
        String id = accessory["uniqueId"].as<String>();
        Serial.printf("Checking accessory: %s (ID: %s)\n", name.c_str(), id.c_str());
        if (name == TARGET_DEVICE_NAME)
        {
          deviceId = id;
          deviceId.toCharArray(savedDeviceId, deviceId.length() + 1);
          Serial.printf("Found matching device ID: %s\n", deviceId.c_str());
          http.end();
          return true;
        }
      }
    }
    http.end();
  }
  return false;
}

bool sendHomeBridgeCommand(bool turnOn)
{
  if (WiFi.status() == WL_CONNECTED && deviceId.length() > 0)
  {
    HTTPClient http;
    String url = "http://" + String(HOMEBRIDGE_HOST);
    if (deviceId.indexOf(":" + String(HOMEBRIDGE_PORT)) == -1)
    {
      url += ":" + + String(HOMEBRIDGE_PORT);
    }
    url += "/api/accessories/" + deviceId;
    Serial.printf("Sending state change request to: %s\n", url.c_str());

    StaticJsonDocument<200> doc;
    doc["characteristicType"] = "On";
    doc["value"] = turnOn;
    String jsonString;
    serializeJson(doc, jsonString);
    Serial.printf("State change request payload: %s\n", jsonString.c_str());

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + authToken);
    int httpCode = http.PUT(jsonString);
    String payload = http.getString();
    Serial.printf("State change response code: %d\n", httpCode);
    Serial.printf("State change response body: %s\n", payload.c_str());

    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)
    {
      Serial.println("State change successful");
      http.end();
      return true;
    }

    if (httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN)
    {
      Serial.println("Token expired, refreshing...");
      http.end();
      if (getHomeBridgeToken(true))
      {
        Serial.println("Token refresh successful, retrying state change");
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + authToken);
        httpCode = http.PUT(jsonString);
        payload = http.getString();
        Serial.printf("State change retry response code: %d\n", httpCode);
        Serial.printf("State change retry response body: %s\n", payload.c_str());
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED)
        {
          Serial.println("State change successful after token refresh");
          http.end();
          return true;
        }
      }
    }
    Serial.println("State change failed");
    http.end();
  }
  return false;
}

void performInitialSetup()
{
  Serial.printf("Connecting to WiFi network: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected to WiFi. IP: %s\n", WiFi.localIP().toString().c_str());

  Serial.println("Getting HomeBridge auth token...");
  if (!getHomeBridgeToken(false) && !getHomeBridgeToken(true))
  {
    Serial.println("Failed to get auth token after all attempts!");
    return;
  }
  authToken.toCharArray(savedAuthToken, authToken.length() + 1);

  if (strlen(savedDeviceId) == 0)
  {
    Serial.printf("No saved device ID found. Searching for device: %s\n", TARGET_DEVICE_NAME);
    if (!findDeviceId(false) && !findDeviceId(true))
    {
      Serial.println("Failed to find device ID after all attempts!");
      return;
    }
  }
  else
  {
    deviceId = String(savedDeviceId);
    Serial.printf("Using saved device ID: %s\n", deviceId.c_str());
  }
}

void prepareSleep()
{
  if (!ENABLE_DEEP_SLEEP)
  {
    Serial.println("Deep sleep disabled - continuing normal operation");
    return;
  }

  // read current switch state and set wakeup to trigger on the opposite level
  bool currentState = gpio_get_level(SWITCH_PIN);
  Serial.printf("Current switch state is %s, setting wake-up trigger for %s\n",
                currentState ? "HIGH" : "LOW",
                currentState ? "LOW" : "HIGH");

  esp_sleep_enable_ext0_wakeup(SWITCH_PIN, !currentState);

  gpio_set_direction(SWITCH_PIN, GPIO_MODE_INPUT);
  gpio_pullup_dis(SWITCH_PIN);
  gpio_pulldown_en(SWITCH_PIN);

  Serial.println("Entering deep sleep...");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nBoot count: %d\n", ++bootCount);

  gpio_set_direction(SWITCH_PIN, GPIO_MODE_INPUT);
  gpio_pullup_dis(SWITCH_PIN);
  gpio_pulldown_en(SWITCH_PIN);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || !ENABLE_DEEP_SLEEP)
  {
    if (strlen(savedDeviceId) == 0 || strlen(savedAuthToken) == 0)
    {
      Serial.println("Missing cached credentials, performing full setup");
      performInitialSetup();
    }
    else
    {
      deviceId = String(savedDeviceId);
      authToken = String(savedAuthToken);

      Serial.printf("Connecting to WiFi network: %s\n", WIFI_SSID);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(500);
        Serial.print(".");
      }
      Serial.println("\nWiFi Connected");

      bool switchState = gpio_get_level(SWITCH_PIN);
      Serial.printf("Switch state is: %s\n", switchState ? "ON" : "OFF");
      sendHomeBridgeCommand(switchState);
    }
  }
  else
  {
    Serial.println("First boot or reset detected - performing full setup");
    performInitialSetup();
  }

  delay(100);
  prepareSleep();
}

void loop()
{
  if (!ENABLE_DEEP_SLEEP)
  {
    static bool lastSwitchState = gpio_get_level(SWITCH_PIN);
    bool currentSwitchState = gpio_get_level(SWITCH_PIN);

    if (currentSwitchState != lastSwitchState)
    {
      Serial.printf("Switch state changed to: %s\n", currentSwitchState ? "ON" : "OFF");
      sendHomeBridgeCommand(currentSwitchState);
      lastSwitchState = currentSwitchState;
    }

    delay(100); // prevent excessive checking
  }
}
