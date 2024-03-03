// Налаштування
const char* apSsid = "AlarmMap";
const char* apPass = ""; // Пароль для точки доступу (від 8 симолів)
int wifiConnectTime = 20; // Час протягом якого пристрій буде намагатися підключитися до WiFi (в секундах)
int period = 10000; // Інтервал оновлення карти
// =================================

// Змінні для зберігання веб налаштувань
int stripPin;
int brightness;
bool greenStates;
// =================================

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClient.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif

#include <WiFiManager.h>
WiFiManager wm;

#include <EEPROM.h>
#define PIN_ADDRESS 0
#define BRIGHTNESS_ADDRESS PIN_ADDRESS + sizeof(brightness)
#define GREEN_STATES_ADDRESS BRIGHTNESS_ADDRESS + sizeof(greenStates)
#define EEPROM_SIZE PIN_ADDRESS + BRIGHTNESS_ADDRESS + GREEN_STATES_ADDRESS

#include <Adafruit_NeoPixel.h>
#define LED_COUNT 25
Adafruit_NeoPixel strip(LED_COUNT, 255, NEO_GRB + NEO_KHZ800);
static unsigned long times[LED_COUNT];
static int ledColor[LED_COUNT];
static int ledColorBlue[] = {2,3,4,5,6,7,20,8,9,19,10,11,25};
static int ledColorYellow[] = {0,1,24,23,22,21,16,17,18,14,15,13,12};

#include <ArduinoJson.h>
String baseURL = "https://ubilling.net.ua/aerialalerts/";
static String states[] = {"Закарпатська область", "Івано-Франківська область", "Тернопільська область", "Львівська область", "Волинська область", "Рівненська область", "Житомирська область", "Київська область", "Чернігівська область", "Сумська область", "Харківська область", "Луганська область", "Донецька область", "Запорізька область", "Херсонська область", "АР Крим", "Одеська область", "Миколаївська область", "Дніпропетровська область", "Полтавська область", "Черкаська область", "Кіровоградська область", "Вінницька область", "Хмельницька область", "Чернівецька область"};
unsigned long lastTime;

void initEEPROM() {
  Serial.println("Ініціалізація EEPROM...");

  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(PIN_ADDRESS, stripPin);
  EEPROM.get(BRIGHTNESS_ADDRESS, brightness);
  EEPROM.get(GREEN_STATES_ADDRESS, greenStates);

  EEPROM.commit();

  if (stripPin == -1) {
    stripPin = 13;
    brightness = 100;
    greenStates = true;
  }
}

void initStrip() {
  Serial.println("Ініціалізація стрічки...");

  strip.setPin(stripPin); // Встановлення піну

  strip.begin();
  
  strip.setBrightness(brightness * 2.55); // Встановлення яскравості

  // Відображення прапору
  int count = sizeof(ledColorYellow)/ sizeof(int);
  for (int i = 0; i < count; i++) {
    strip.setPixelColor(ledColorBlue[i], strip.Color(0, 87, 183));
    strip.setPixelColor(ledColorYellow[i], strip.Color(255, 215, 0));
    strip.show();
    delay(60);
  }
}

void initWiFi() {
  Serial.print("Ініціалізація WiFi... ");

  WiFi.mode(WIFI_STA);

  wm.setDebugOutput(false);

  wm.addParameter(new WiFiManagerParameter("<h1 style=\"text-align: center;\">Налаштування</h1>"));
  wm.addParameter(new WiFiManagerParameter("pin", "Пін стрічки", String(stripPin).c_str(), 3,"type=\"number\""));
  wm.addParameter(new WiFiManagerParameter("brightness", "Яскравість", String(brightness).c_str(), 3,"type=\"number\" min=\"1\" max \"100\""));
  wm.addParameter(new WiFiManagerParameter("greenstates", "Зелені області без тривог", String(greenStates).c_str(), 1, "type=\"number\" min=\"0\" max=\"1\""));
  
  wm.setSaveParamsCallback([]() {
    EEPROM.begin(EEPROM_SIZE);
    if (stripPin != wm.server->arg("pin").toInt()) EEPROM.put(PIN_ADDRESS, wm.server->arg("pin").toInt());
    if (brightness != wm.server->arg("brightness").toInt()) EEPROM.put(BRIGHTNESS_ADDRESS, wm.server->arg("brightness").toInt());
    if (greenStates != (wm.server->arg("greenstates") == "1")) EEPROM.put(GREEN_STATES_ADDRESS, wm.server->arg("greenstates") == "1");
    EEPROM.commit();

    ESP.restart();
  });

  const char* menu[] = {"wifi","param","sep","info","restart"}; 
  wm.setMenu(menu, 5);

  wm.setConnectTimeout(wifiConnectTime);

  if (!wm.getWiFiIsSaved()) Serial.println("Під'єднайтеся до точки доступу " + String(apSsid) + (*apPass != '\0' ? " з паролем " + String(apPass) : "") + " щоб налаштувати WiFi! ");

  bool res = wm.autoConnect(apSsid,apPass);

  if(!res) {
    Serial.println("Не вдалося підключитися!");
    ESP.restart();
  } else {
    Serial.print("Підключено! IP: ");
    Serial.println(WiFi.localIP());

    wm.setHttpPort(80);
    wm.startWebPortal();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.print("\n\n\n");

  initEEPROM();
  initStrip(); 
  initWiFi();
}

void loop() {
  wm.process(); // Обробка WiFi Manager

  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastTime > period || lastTime == 0) {
      Serial.println("Перевірка тривог...");

			HTTPClient http;

      #if defined(ESP8266)
        WiFiClient client;
        http.begin(client, baseURL);
      #elif defined(ESP32)
        http.begin(baseURL);
      #endif
        
			int httpResponseCode = http.GET();

      String response;
			if (httpResponseCode == 200) {
        response = http.getString(); 
      } else {
        Serial.print("Помилка: ");
        Serial.println(httpResponseCode);
        return;
      }
        
      http.end();

      DynamicJsonDocument doc(4000);
      DeserializationError error = deserializeJson(doc, response);
      
      bool enabledStates[LED_COUNT];
      for (int i = 0; i < LED_COUNT; i++) {
        enabledStates[i] = doc["states"][states[i]]["alertnow"].as<bool>() ? 1 : 0;
        if (states[i] == "АР Крим") enabledStates[i] = 1;
      }

      alarmMode(enabledStates); // Відораження тривог на стрічці

      lastTime = millis();
    }
  } else {
    strip.clear();
    strip.show();
    delay(10000);
    ESP.restart();
  }
}

void alarmMode(bool enabledStates[LED_COUNT]) {
  unsigned long hv = 180000; // Інтервал зміннення нової, помаранчевої тривоги на червону - 3 хвилини

  for (int i = 0; i < LED_COUNT; i++) {
    bool alarmEnable = enabledStates[i];

    if (alarmEnable && times[i] == 0) {
      times[i] = millis();
      ledColor[i] = 2;
    }
    else if (alarmEnable && times[i] + hv > millis() && ledColor[i] != 1) {
      ledColor[i] = 2;
    }
    else if (alarmEnable) {
      ledColor[i] = 1;
      times[i] = millis();
    }

    if (!alarmEnable && times[i] + hv > millis() && times[i] != 0) {
      ledColor[i] = 3;
    } 
    else if (!alarmEnable) {
      ledColor[i] = 0;
      times[i] = 0;
    }    
  }
  
  strip.clear();
  for (int i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
    switch (ledColor[i]) {
      // Червоний колір - відображається якщо тривога триває довше 3 хвилин
      case 1: strip.setPixelColor(i, strip.Color(255,0,0)); break;
      // Помаранчевий колір - відображається протягом 3 хвилин після початку тривоги
      case 2: strip.setPixelColor(i, strip.Color(255,55,0)); break;
      // В залежності від налаштувань після трьох хвилин після закінчення тривоги лед вимикається або і далі світить зеленим кольром
      case 0: if (!greenStates) {strip.setPixelColor(i, strip.Color(0, 0, 0)); break;}
      // Зелений колір - відображається протягом 3 хвилин після закінчення тривоги
      case 3: strip.setPixelColor(i, strip.Color(0,255,0)); break;
    }
  }
  strip.show();
}
