#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

// Pin-definities
#define DHTPIN   13          // DHT11-sensor is verbonden met pin 13
#define DHTTYPE  DHT11       // Type sensor: DHT11
#define RELAISPIN1 7          // Relais 1 voor lamp
#define RELAISPIN2 27         // Relais 2 voor ventilator
#define LCD_ADDR 0x27         // I2C-adres van het LCD-scherm
#define LCD_COLS 16           // Aantal kolommen van het LCD
#define LCD_ROWS 2            // Aantal rijen van het LCD
#define LED_PIN 4             // LED voor meetactiviteit
#define BUTTON_PIN 5          // Drukknop voor menu

// Instellingen voor meetinterval
unsigned long measureIntervalMs = 60000;     // standaard 1 minuut
unsigned long lastMeasureTime   = 0;         // tijdstip laatste meting

// Menu-instellingen
bool inMenu = false;                         // status: zit gebruiker in menu?
unsigned long menuStartTime = 0;             // tijdstip van laatste menu-activiteit
const unsigned long menuTimeout = 5000;      // menu verdwijnt na 5 seconden inactiviteit

// Sensor- en displayobjecten
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// Globale variabelen voor temperatuur en luchtvochtigheid
float g_temp = NAN;
float g_hum  = NAN;

// FreeRTOS taken
TaskHandle_t TaskDHTHandle = NULL;
TaskHandle_t TaskLCDHandle = NULL;
TaskHandle_t TaskRelaisLampHandle = NULL;
TaskHandle_t TaskRelaisFanHandle = NULL;
TaskHandle_t TaskButtonHandle = NULL;

// ======================
// Taak: Sensor uitlezen
// ======================
void TaskDHT(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    unsigned long now = millis();

    // Controleer of het tijd is voor een nieuwe meting
    if (now - lastMeasureTime >= measureIntervalMs) {
      lastMeasureTime = now;

      digitalWrite(LED_PIN, HIGH);      // LED aan tijdens meting
      float h = dht.readHumidity();     // Lees luchtvochtigheid
      float t = dht.readTemperature();  // Lees temperatuur
      digitalWrite(LED_PIN, LOW);       // LED weer uit

      // Sla meting alleen op als waarden geldig zijn
      if (!isnan(h) && !isnan(t)) {
        g_hum  = h;
        g_temp = t;
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS); // even wachten
  }
}

// ======================
// Taak: LCD-scherm aansturen
// ======================
void TaskLCD(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    lcd.clear(); // scherm telkens leegmaken

    // Toon ander scherm als gebruiker in menu zit
    if (inMenu) {
      lcd.setCursor(0, 0);
      lcd.print("Minuten meten:");
      lcd.setCursor(0, 1);
      lcd.print(measureIntervalMs / 60000); // toon aantal minuten
    } 
    // Anders toon temperatuur en vochtigheid
    else if (!isnan(g_temp) && !isnan(g_hum)) {
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(g_temp, 1);
      lcd.print((char)223); // graden-symbool
      lcd.print("C ");

      lcd.setCursor(9, 0);
      lcd.print("H:");
      lcd.print(g_hum, 1);
      lcd.print("%");

      // Tekst afhankelijk van vochtigheidsniveau
      if (g_hum < 50) {
        lcd.setCursor(0, 1);
        lcd.print("Te laag. Sproei!");
      } else if (g_hum > 90) {
        lcd.setCursor(0, 1);
        lcd.print("Te hoog. Open");
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Alles goed");
      }
    } 
    // Als er nog geen meetgegevens zijn
    else {
      lcd.setCursor(0, 0);
      lcd.print("Wachten op data");
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ======================
// Taak: Lamp (temperatuur)
// ======================
void TaskRelaisLamp(void *pvParameters) {
  (void) pvParameters;

  const float TEMP_ON  = 25.0;  // onder 25°C: lamp aan
  const float TEMP_OFF = 30.0;  // boven 30°C: lamp uit

  bool relayState = false;
  digitalWrite(RELAISPIN1, HIGH); // relais start in uit-stand (active LOW)

  for (;;) {
    if (!isnan(g_temp)) {
      // Beslissing afhankelijk van temperatuur
      if (g_temp < TEMP_ON) {
        relayState = true;
      } else if (g_temp > TEMP_OFF) {
        relayState = false;
      } else if (g_temp > TEMP_ON && g_temp < TEMP_OFF) {
        relayState = true;
      }

      // Active LOW logica
      digitalWrite(RELAISPIN1, relayState ? LOW : HIGH);

      // Seriële debug-output
      Serial.print("Temp=");
      Serial.print(g_temp);
      Serial.print("  Relay=");
      Serial.print(relayState ? "ON" : "OFF");
      Serial.print(" ");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ======================
// Taak: Ventilator (luchtvochtigheid)
// ======================
void TaskRelaisFan(void *pvParameters){
  (void) pvParameters;

  const float HUM_ON = 85.0;  // boven 85%: ventilator aan
  const float HUM_OFF = 60.0; // onder 60%: ventilator uit

  bool relayState = false;
  digitalWrite(RELAISPIN2, HIGH); // relais start uit

  for(;;){
    if(!isnan(g_hum)){
      // Logica met hysterese tussen HUM_OFF en HUM_ON
      if(g_hum > HUM_ON){
        relayState = true;
      } else if (g_hum < HUM_OFF){
        relayState = false;
      } else if (g_hum > HUM_OFF && g_hum < HUM_ON){
        relayState = true;
      }

      // Active LOW logica
      digitalWrite(RELAISPIN2, relayState ? LOW : HIGH);

      // Seriële debug-uitvoer
      Serial.print("Hum=");
      Serial.print(g_hum);
      Serial.print("  Relay=");
      Serial.println(relayState ? "ON" : "OFF");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ======================
// Taak: Knopbediening voor menu
// ======================
void TaskButton(void *pvParameters) {
  (void) pvParameters;
  bool lastState = HIGH;

  for (;;) {
    bool state = digitalRead(BUTTON_PIN);

    // Detecteer druk (van HIGH naar LOW)
    if (state == LOW && lastState == HIGH) {
      if (!inMenu) {
        // Menu ingaan
        inMenu = true;
        menuStartTime = millis();
      } else {
        // Menu actief: interval verhogen (1–10 minuten)
        unsigned long minutes = measureIntervalMs / 60000;
        minutes++;
        if (minutes > 10) minutes = 1;
        measureIntervalMs = minutes * 60000;
        menuStartTime = millis(); // reset timeout
      }
    }
    lastState = state;

    // Sluit menu na timeout
    if (inMenu && (millis() - menuStartTime > menuTimeout)) {
      inMenu = false;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);  // debounce vertraging
  }
}

// ======================
// Setup: eenmalige start
// ======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();       // start DHT11-sensor
  lcd.init();        // initialiseer LCD
  lcd.backlight();   // zet achtergrondlicht aan

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FreeRTOS start");
  delay(2000);

  // Pinconfiguratie
  pinMode(RELAISPIN1, OUTPUT);
  digitalWrite(RELAISPIN1, LOW);

  pinMode(RELAISPIN2, OUTPUT);
  digitalWrite(RELAISPIN2, LOW);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP); // interne pull-up actief

  // Taken aanmaken
  xTaskCreate(TaskDHT, "TaskDHT", 4096, NULL, 2, &TaskDHTHandle);
  xTaskCreate(TaskLCD, "TaskLCD", 4096, NULL, 1, &TaskLCDHandle);
  xTaskCreate(TaskRelaisLamp, "TaskRelaisLamp", 2048, NULL, 1, &TaskRelaisLampHandle);
  xTaskCreate(TaskRelaisFan, "TaskRelaisFan", 2048, NULL, 1, &TaskRelaisFanHandle);
  xTaskCreate(TaskButton, "TaskButton", 2048, NULL, 1, &TaskButtonHandle);
}

// ======================
// Lege loop (FreeRTOS gebruikt eigen scheduler)
// ======================
void loop() {
  // Hoofdlus wordt niet gebruikt, maar houdt kleine delay aan
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
