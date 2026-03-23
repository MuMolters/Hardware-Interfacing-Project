#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include "DHT.h"

// Pin definities
#define DHTPIN 13
#define DHTTYPE DHT11
#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
#define LED_PIN 12
#define SERVO_PIN 5

#define RELAIS_DAYLIGHT 7
#define RELAIS_HEATLAMP 32

#define BUTTON_PIN 14

// RGB LED pins
#define RGB_PIN_R 15
#define RGB_PIN_G 33
#define RGB_PIN_B 27

// Globale klok
uint8_t clockH = 12;
uint8_t clockM = 0;
unsigned long clockLastIncrement = 0;

// Menu en knop status
bool inClockSet = false;
bool setMode = 0;  // 0: uren, 1: minuten
unsigned long clockTimeout = 0;

// Forceer refresh na terugkeer
bool forceClockRefresh = false;

// Meetinterval
unsigned long measureIntervalMs = 1000;
unsigned long lastMeasureTime = 0;

// Sensor objecten
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

Servo sprinklerServo;

// Globale sensor waarden
float g_temp = NAN;
float g_hum = NAN;

// FreeRTOS handles
TaskHandle_t TaskClockHandle = NULL;
TaskHandle_t TaskDHTHandle = NULL;
TaskHandle_t TaskLCDHandle = NULL;
TaskHandle_t TaskRGBHandle = NULL;
TaskHandle_t TaskRelaisDaylightHandle = NULL;
TaskHandle_t TaskRelaisHeatlampHandle = NULL;
TaskHandle_t TaskButtonHandle = NULL;

// ======================
// Klok taak: incrementeer tijd elke minuut
// ======================
void TaskClock(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    unsigned long now = millis();
    if (now - clockLastIncrement >= 60000UL) {  // 60 seconden
      clockLastIncrement = now;
      if (!inClockSet) {  // Update alleen buiten set mode
        clockM++;
        if (clockM >= 60) {
          clockM = 0;
          clockH = (clockH + 1) % 24;
        }
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Check elke seconde
  }
}

// ======================
// Is dagtijd?
// ======================
bool isDaytime() {
  return (clockH >= 8 && clockH < 20);
}

// ======================
// RGB status op basis van temp en hum
// ======================
void showClimateStatus(float temp, float hum) {
  const float TEMP_MIN = 10.0;
  const float TEMP_MAX = 40.0;
  const float HUM_MIN = 40.0;
  const float HUM_MAX = 80.0;

  float t = isnan(temp) ? TEMP_MIN : temp;
  float h = isnan(hum) ? HUM_MIN : hum;

  bool tempBad = (t < TEMP_MIN || t > TEMP_MAX);
  bool humBad = (h < HUM_MIN || h > HUM_MAX);

  if (tempBad || humBad) {
    // Rood/oranje fout
    analogWrite(RGB_PIN_R, 255);
    analogWrite(RGB_PIN_G, 0);
    analogWrite(RGB_PIN_B, 40);
    return;
  }

  bool day = isDaytime();
  float tTargetLow = day ? 30.0 : 16.0;
  float tTargetHigh = day ? 34.0 : 20.0;

  if (t < tTargetLow || t > tTargetHigh) {
    if (t < tTargetLow) {
      // Blauw koud
      analogWrite(RGB_PIN_R, 60);
      analogWrite(RGB_PIN_G, 0);
      analogWrite(RGB_PIN_B, 220);
    } else {
      // Rood warm
      analogWrite(RGB_PIN_R, 220);
      analogWrite(RGB_PIN_G, 40);
      analogWrite(RGB_PIN_B, 0);
    }
    return;
  }

  if (h < 50.0 || h > 75.0) {
    // Cyaan vocht probleem
    analogWrite(RGB_PIN_R, 0);
    analogWrite(RGB_PIN_G, 200);
    analogWrite(RGB_PIN_B, 200);
    return;
  }

  // Groen goed
  analogWrite(RGB_PIN_R, 0);
  analogWrite(RGB_PIN_G, 220);
  analogWrite(RGB_PIN_B, 120);
}

// ======================
// DHT sensor taak
// ======================
void TaskDHT(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    digitalWrite(LED_PIN, HIGH);
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    digitalWrite(LED_PIN, LOW);

    if (!isnan(h) && !isnan(t)) {
      g_hum = h;
      g_temp = t;
    }
    lastMeasureTime = millis();

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ======================
// LCD taak - trager refresh in set mode
// ======================
void TaskLCD(void *pvParameters) {
  (void) pvParameters;
  unsigned long lastSetUpdate = 0;
  for (;;) {
    unsigned long now = millis();

    if (!inClockSet) {
      // Normale modus: 2s refresh of force
      if (now - clockLastIncrement >= 2000 || forceClockRefresh) {
        lcd.clear();
        if (!isnan(g_temp) && !isnan(g_hum)) {
          // Regel 1: T en H
          lcd.setCursor(0, 0);
          lcd.print("T:");
          lcd.print(g_temp, 1);
          lcd.print((char)223);
          lcd.print("C ");
          lcd.setCursor(9, 0);
          lcd.print("H:");
          lcd.print(g_hum, 1);
          lcd.print("%");

          // Regel 2: tijd + G/F
          lcd.setCursor(0, 1);
          if (clockH < 10) lcd.print('0');
          lcd.print(clockH);
          lcd.print(':');
          if (clockM < 10) lcd.print('0');
          lcd.print(clockM);

          lcd.setCursor(9, 1);
          if (g_temp < 16.0 || g_temp > 25.0 || g_hum < 50.0 || g_hum > 75.0) {
            lcd.print(" F ");
          } else {
            lcd.print(" G ");
          }
        } else {
          lcd.setCursor(0, 0);
          lcd.print("Wachten op data");
          lcd.setCursor(0, 1);
          lcd.print("     ");
        }
        forceClockRefresh = false;
      }
    } else {
      // Set mode: update elke 500ms i.p.v. elke 50ms
      if (now - lastSetUpdate >= 500) {
        lastSetUpdate = now;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("SET UREN/MIN");

        lcd.setCursor(0, 1);
        if (setMode == 0) {
          lcd.print("Uren: ");
        } else {
          lcd.print("Min:  ");
        }
        if (clockH < 10) lcd.print('0');
        lcd.print(clockH);
        lcd.print(':');
        if (clockM < 10) lcd.print('0');
        lcd.print(clockM);
      }
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Langzamer loop
  }
}

// ======================
// RGB update taak
// ======================
void TaskRGB(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    showClimateStatus(g_temp, g_hum);
    vTaskDelay(2000 / portTICK_PERIOD_MS);  // Update elke 2s
  }
}

// ======================
// Sproeier servo taak
// ======================
void TaskSprinkler(void *pvParameters) {
  (void) pvParameters;
  bool lastSprayState = false;
  for (;;) {
    if (!isnan(g_hum)) {
      if (g_hum < 50.0 && !lastSprayState) {
        Serial.println("Sproeier AAN: hum <50%");
        sprinklerServo.write(90);  // Spray aan
        lastSprayState = true;
      }
      else if (g_hum > 70.0 && lastSprayState) {
        Serial.println("Sproeier UIT: hum >70%");
        sprinklerServo.write(0);   // Uit
        lastSprayState = false;
      }
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS);  // Check elke 3s
  }
}

// ======================
// Daglicht relais
// ======================
void TaskRelaisDaylight(void *pvParameters) {
  (void) pvParameters;
  digitalWrite(RELAIS_DAYLIGHT, HIGH);  // Start uit
  for (;;) {
    digitalWrite(RELAIS_DAYLIGHT, isDaytime() ? LOW : HIGH);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// ======================
// Warmtelamp relais
// ======================
void TaskRelaisHeatlamp(void *pvParameters) {
  (void) pvParameters;
  bool lastRelayState = false;
  bool lastDay = false;

  digitalWrite(RELAIS_HEATLAMP, HIGH);  // Start uit

  for (;;) {
    if (!isnan(g_temp)) {
      bool day = isDaytime();
      float TEMP_ON = day ? 30.0 : 17.0;
      float TEMP_OFF = day ? 34.0 : 19.0;

      bool relayState = (g_temp < TEMP_ON);

      digitalWrite(RELAIS_HEATLAMP, relayState ? LOW : HIGH);

      if (relayState != lastRelayState || day != lastDay) {
        Serial.printf("Uur=%02d Mode=%s Temp=%.1f HeatLamp=%s\n",
                      clockH, day ? "DAG" : "NACHT", g_temp, relayState ? "ON" : "OFF");
      }

      lastRelayState = relayState;
      lastDay = day;
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ======================
// Knop taak
// ======================
void TaskButton(void *pvParameters) {
  (void) pvParameters;
  bool lastState = HIGH;
  unsigned long lastDebounce = 0;
  const unsigned long debounceTime = 30;

  for (;;) {
    bool curr = digitalRead(BUTTON_PIN);

    // Flank detectie LOW->HIGH (loslaten)
    if (curr == HIGH && lastState == LOW) {
      unsigned long dt = millis() - lastDebounce;
      if (dt >= debounceTime) {
        if (!inClockSet) {
          // Start set mode uren
          inClockSet = true;
          setMode = 0;
          clockTimeout = millis();
          forceClockRefresh = true;
        } else {
          // Increment
          if (setMode == 0) {
            clockH = (clockH + 1) % 24;
          } else {
            clockM = (clockM + 1) % 60;
          }
          clockTimeout = millis();
          forceClockRefresh = true;
        }
      }
    }

    if (curr == LOW) {
      lastDebounce = millis();
    }
    lastState = curr;

    // Timeout logica
    if (inClockSet && (millis() - clockTimeout > 5000)) {
      if (setMode == 0) {
        setMode = 1;
      } else {
        inClockSet = false;
        forceClockRefresh = true;
      }
      clockTimeout = millis();
    }

    vTaskDelay(30 / portTICK_PERIOD_MS);
  }
}

// ======================
// Setup
// ======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Wire speed lager voor LCD stabiliteit [web:22]
  Wire.setClock(100000);  // 100kHz i.p.v. default 400kHz

  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.noDisplay();  // Uit
  delay(10);
  for(int i = 0; i < 3; i++) {
    lcd.display();
    delay(500);
    lcd.noDisplay();
    delay(500);
  }
  lcd.display();  // Definitief aan
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FreeRTOS start");
  delay(2000);

  // Pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(RGB_PIN_R, OUTPUT);
  pinMode(RGB_PIN_G, OUTPUT);
  pinMode(RGB_PIN_B, OUTPUT);
  analogWrite(RGB_PIN_R, 0);
  analogWrite(RGB_PIN_G, 0);
  analogWrite(RGB_PIN_B, 0);  // Nu aan met TaskRGB [web:21]

  // Relais
  pinMode(RELAIS_DAYLIGHT, OUTPUT);
  digitalWrite(RELAIS_DAYLIGHT, HIGH);
  pinMode(RELAIS_HEATLAMP, OUTPUT);
  digitalWrite(RELAIS_HEATLAMP, HIGH);

  // Knop
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(SERVO_PIN, OUTPUT);
  sprinklerServo.setPeriodHertz(50);
  sprinklerServo.attach(SERVO_PIN, 500, 2400);  // SG90 params
  sprinklerServo.write(0);  // Start uit positie

  // Taken starten
  xTaskCreate(TaskClock, "TaskClock", 2048, NULL, 2, &TaskClockHandle);
  xTaskCreate(TaskDHT, "TaskDHT", 4096, NULL, 2, &TaskDHTHandle);
  xTaskCreate(TaskLCD, "TaskLCD", 4096, NULL, 1, &TaskLCDHandle);
  xTaskCreate(TaskRGB, "TaskRGB", 2048, NULL, 1, &TaskRGBHandle);
  xTaskCreate(TaskRelaisDaylight, "TaskRelaisDaylight", 2048, NULL, 1, &TaskRelaisDaylightHandle);
  xTaskCreate(TaskRelaisHeatlamp, "TaskRelaisHeatlamp", 2048, NULL, 1, &TaskRelaisHeatlampHandle);
  xTaskCreate(TaskButton, "TaskButton", 2048, NULL, 1, &TaskButtonHandle);
  xTaskCreate(TaskSprinkler, "TaskSprinkler", 2048, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
