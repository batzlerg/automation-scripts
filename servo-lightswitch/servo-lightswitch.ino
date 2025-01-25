#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// use an ESP-32 to set up a tiny web server attached to a servo,
// and use that servo to physically switch a single pole home lightswitch
// on and off. you can then configure this as a light switch in homebridge using
// https://github.com/Supereg/homebridge-http-switch
// is it practical? no. but if you're renting a home wired in the stone age
// before they ran neutral wires to all junction boxes, and you can't install
// a real smart switch without frying yourself trying to pull a neutral
// from the ceiling, this may have to do.

const char *WIFI_SSID = "SSID_GOES_HERE";
const char *WIFI_PASSWORD = "PASSWORD_GOES_HERE";

// on Macbook you can do `networksetup -getinfo Wi-Fi` to get these values:
const IPAddress staticIP(192, 168, 0, 194); // first three from gateway, last one you choose
const IPAddress gateway(192, 168, 0, 1); // router IP
const IPAddress subnet(255, 255, 255, 0); // subnet mask
const IPAddress dns(192, 168, 0, 1); // router IP

WebServer server(80);

// hardware
Servo switchServo;
const int servoPin = 4;
const int bootButtonPin = 9;

// state
bool switchState = false;
const int onPosition = 75;
const int offPosition = 0;

void logMessage(const String &message) {
  Serial.println(String(millis()) + "ms: " + message);
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true); // disconnect and clear previous configs
  delay(100);

  // set static IP
  if (!WiFi.config(staticIP, gateway, subnet, dns)) {
    logMessage("Static IP Configuration Failed!");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  logMessage("Connecting to WiFi...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    logMessage("Connected to WiFi");
    logMessage("IP: " + WiFi.localIP().toString());
    logMessage("Gateway: " + WiFi.gatewayIP().toString());
    logMessage("Subnet: " + WiFi.subnetMask().toString());
    logMessage("DNS: " + WiFi.dnsIP().toString());
  }
  else
  {
    logMessage("WiFi connection FAILED!");
  }
}

void handlePing() {
  logMessage("ROOT endpoint hit");
  server.enableCORS(true);
  server.send(200, "text/plain", "Pong");
}

void handleOn() {
  logMessage("ON endpoint hit");
  server.enableCORS(true);
  switchState = true;
  switchServo.write(onPosition);
  server.send(200, "text/plain", "Switch turned ON");
}

void handleOff() {
  logMessage("OFF endpoint hit");
  server.enableCORS(true);
  switchState = false;
  switchServo.write(offPosition);
  server.send(200, "text/plain", "Switch turned OFF");
}

void handleStatus() {
  logMessage("STATUS endpoint hit");
  server.enableCORS(true);
  server.send(200, "text/plain", switchState ? "1" : "0");
}

void handleNotFound() {
  logMessage("404: " + server.uri());
  server.enableCORS(true);
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  logMessage("Starting up...");

  // initialize all the things
  switchServo.attach(servoPin);
  switchServo.write(offPosition);
  logMessage("Servo initialized to OFF position");

  pinMode(bootButtonPin, INPUT_PULLUP);
  logMessage("Boot button pin initialized");

  setupWiFi();

  // endpoints
  server.on("/", HTTP_GET, handlePing);
  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();
  logMessage("HTTP server started");
}

void handleButton() {
  static int lastButtonState = HIGH;
  int buttonState = digitalRead(bootButtonPin);

  if (buttonState != lastButtonState) {
    if (buttonState == LOW)
    {
      logMessage("Button press detected");
      switchState = !switchState;

      if (switchState)
      {
        switchServo.write(onPosition);
        logMessage("Moving servo to ON position");
      }
      else
      {
        switchServo.write(offPosition);
        logMessage("Moving servo to OFF position");
      }
    }
    lastButtonState = buttonState;
  }
}

void loop() {
  static unsigned long lastCheck = 0;

  // run handlers
  server.handleClient();
  handleButton();

  // keepalive wifi
  if (millis() - lastCheck > 10000) { // 10 seconds
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
      logMessage("WiFi disconnected! Reconnecting...");
      setupWiFi();
    }
  }
}
