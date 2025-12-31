/*
  To upload through terminal you can use: curl -F "image=@firmware.bin" esp8266-webupdate.local/update
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Arduino.h>
#include <AceRoutine.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

using namespace ace_routine;

#ifndef STASSID
#define STASSID **********
#define STAPSK *********
#endif

#include <Adafruit_NeoPixel.h>

// Which pin on the Arduino is connected to the NeoPixels?
// On a Trinket or Gemma we suggest changing this to 1:
#define LED_PIN 15

// How many NeoPixels are attached to the Arduino?
#define LED_COUNT 84

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ400);


//const char* host = "esp8266-webupdate";
const char* ssid = STASSID;
const char* password = STAPSK;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

// New Year 2026 Unix Epoch Time (UTC)
// Jan 01 2026 00:00:00 UTC = 1767225600
const long targetEpoch = 1767200400 + 7 * 3600;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void handleRoot() {
  // Send HTTP 200 (OK) with HTML content
  //String message = "value: " + strip.getBrightness();
  httpServer.send(200, "text/html", "<h1>Hello from ESP8266!</h1><p><a href='/brightness?val=100'>Brightness </a></p><p><a href='/update'>Update</a></p>");
}

void handleBrightness() {
  // Check if "val" exists in the URL: /brightness?val=123

  if (httpServer.hasArg("val")) {
    String value = httpServer.arg("val");
    int br = value.toInt();
    if (br >= 0 && br <= 255) {
      strip.setBrightness(value.toInt());
    } else {
      httpServer.send(400, "text/html", "<h1>Hello from ESP8266!</h1><p>use /brightness?val=100, where value should be in 0..255</p>");
    }
  }
  // Send HTTP 200 (OK) with HTML content
  httpServer.send(200, "text/html", "<h1>Hello from ESP8266!</h1><p><a href='/brightness'>Set brightness</a></p>");
}

long secondsRemaining = 600;
long minutesRemaining = secondsRemaining / 60;
int startAnimationIndex = 0;
volatile bool rainbow = true;

class NTPCoroutine : public Coroutine {
public:
  NTPCoroutine() = default;

  int runCoroutine() override {
    COROUTINE_LOOP() {
      COROUTINE_DELAY(1000);
      timeClient.update();
      unsigned long currentEpoch = timeClient.getEpochTime();
      if (currentEpoch <= targetEpoch) {
        secondsRemaining = targetEpoch - currentEpoch;
        minutesRemaining = secondsRemaining / 60;
      } else {
        rainbow = false;
      }
      startAnimationIndex++;
      startAnimationIndex %= LED_COUNT;
    }
  }

private:
};

class HttpServerCoroutine : public Coroutine {
public:
  HttpServerCoroutine() = default;

  int runCoroutine() override {
    COROUTINE_LOOP() {
      COROUTINE_DELAY(109);
      httpServer.handleClient();
    }
  }

private:
};


class StripCoroutine : public Coroutine {
public:
  StripCoroutine() {
  }

  void drawParticles() {

    if (minutesRemaining > 60) return;

    if (minutesRemaining > 0) {
      for (int i = startAnimationIndex; i < startAnimationIndex + minutesRemaining; ++i) {
        strip.setPixelColor(i % LED_COUNT, strip.Color(0, 0, 0));
      }
    } else if (secondsRemaining > 0) {
      for (int i = startAnimationIndex; i < startAnimationIndex + secondsRemaining; ++i) {
        strip.setPixelColor(i % LED_COUNT, strip.Color(0, 0, 0));
      }
    }
  }

  int runCoroutine() override {
    COROUTINE_LOOP() {

      if (rainbow) {
        // Hue of first pixel runs 5 complete loops through the color wheel.
        // Color wheel has a range of 65536 but it's OK if we roll over, so
        // just count from 0 to 5*65536. Adding 256 to firstPixelHue each time
        // means we'll make 5*65536/256 = 1280 passes through this loop:
        for (firstPixelHue = 0; firstPixelHue < 5 * 65536; firstPixelHue += 256) {
          if (!rainbow) break;
          // strip.rainbow() can take a single argument (first pixel hue) or
          // optionally a few extras: number of rainbow repetitions (default 1),
          // saturation and value (brightness) (both 0-255, similar to the
          // ColorHSV() function, default 255), and a true/false flag for whether
          // to apply gamma correction to provide 'truer' colors (default true).
          strip.rainbow(firstPixelHue);
          // Above line is equivalent to:
          // strip.rainbow(firstPixelHue, 1, 255, 255, true);

          drawParticles();
          strip.show();  // Update strip with new contents

          COROUTINE_DELAY(30);
        }
      } else {
        while (showCount--) {
          strip.fill(strip.Color(255, 0, 0), 0, LED_COUNT);
          strip.show();  // Update strip with new contents
          COROUTINE_DELAY(500);
          strip.fill(strip.Color(0, 255, 0), 0, LED_COUNT);
          strip.show();  // Update strip with new contents
          COROUTINE_DELAY(500);
          strip.fill(strip.Color(0, 0, 255), 0, LED_COUNT);
          strip.show();  // Update strip with new contents
          COROUTINE_DELAY(500);

          for (int i = 0; i < 84; ++i) {
            auto getColor = [](int i) {
              switch (i % 3) {
                case 0:
                  return strip.Color(255, 255, 255);
                case 1:
                  return strip.Color(0, 0, 255);
                case 2:
                  return strip.Color(255, 0, 0);
              }
              return strip.Color(0, 0, 0);
            };

            strip.setPixelColor(i, getColor(i));
          }
          strip.show();  // Update strip with new contents
          COROUTINE_DELAY(3000);
        }
        rainbow = true;
      }
    }
  }

private:
  long firstPixelHue;
  int showCount = 15;
};

HttpServerCoroutine hsc;
StripCoroutine sc;
NTPCoroutine ntpc;

void setup(void) {

  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting Sketch...");

  strip.begin();             // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();              // Turn OFF all pixels ASAP
  strip.setBrightness(250);  // Set BRIGHTNESS to about 1/5 (max = 255)

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    Serial.println("WiFi failed, retrying.");
  }

  //MDNS.begin(host);
  httpServer.on("/", handleRoot);                            // Root path
  httpServer.on("/brightness", HTTP_GET, handleBrightness);  // Root path

  httpUpdater.setup(&httpServer);
  httpServer.begin();
  timeClient.begin();


  //MDNS.addService("http", "tcp", 80);
  //Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", WiFi.localIP());
  Serial.println(WiFi.localIP());  // Печать IP-адреса

  // Auto-register all coroutines into the scheduler.
  CoroutineScheduler::setup();
}


void loop(void) {
  CoroutineScheduler::loop();
}
