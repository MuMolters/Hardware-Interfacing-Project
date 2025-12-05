#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"

#define DHTPIN   13
#define DHTTYPE  DHT11

#define RELAISPIN1 7
#define RELAISPIN2 27

#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

float g_temp = NAN;
float g_hum  = NAN;

TaskHandle_t TaskDHTHandle = NULL;
TaskHandle_t TaskLCDHandle = NULL;
TaskHandle_t TaskRelaisLampHandle = NULL;
TaskHandle_t TaskRelaisFanHandle = NULL;

void TaskDHT(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (!isnan(h) && !isnan(t)) {
      g_hum  = h;
      g_temp = t;
    }

    vTaskDelay(2500 / portTICK_PERIOD_MS);  // ~2,5 s
  }
}

void TaskLCD(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    lcd.clear();
    if (!isnan(g_temp) && !isnan(g_hum)) {
      lcd.setCursor(0, 0);
      lcd.print("T:");
      lcd.print(g_temp, 1);
      lcd.print((char)223);
      lcd.print("C ");

      lcd.setCursor(9, 0);
      lcd.print("H:");
      lcd.print(g_hum, 1);
      lcd.print("%");

      if(g_hum < 50) {
        lcd.setCursor(0, 1);
        lcd.print("Te laag. Sproei!");
      } else if (g_hum > 90) {
        lcd.setCursor(0, 1);
        lcd.print("Te hoog. Open");
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Alles goed");
      }
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Wachten op data");
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // LCD elke seconde verversen
  }
}

void TaskRelaisLamp(void *pvParameters) {
  (void) pvParameters;

  const float TEMP_ON  = 21.0;  // onder 24°C: AAN
  const float TEMP_OFF = 24.0;  // boven 29.5°C: UIT

  bool relayState = false;
  digitalWrite(RELAISPIN1, HIGH); 

  for (;;) {
    if (!isnan(g_temp)) {
      if (g_temp < TEMP_ON) {
        relayState = true;
      } else if (g_temp > TEMP_ON && g_temp < TEMP_OFF) {
        relayState = true;
      } else if (g_temp > TEMP_OFF) {
        relayState = false;
      }

      // voor active LOW-relay:
      digitalWrite(RELAISPIN1, relayState ? LOW : HIGH);

      Serial.print("Temp=");
      Serial.print(g_temp);
      Serial.print("  Relay=");
      Serial.print(relayState ? "ON" : "OFF");
      Serial.print(" ");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void TaskRelaisFan(void *pvParameters){
  (void) pvParameters;

  const float HUM_ON = 85.0;
  const float HUM_OFF = 60.0;

  bool relayState = false;

  digitalWrite(RELAISPIN2, HIGH);

  for(;;){
    if(!isnan(g_hum)){
      if(g_hum > HUM_ON){
        relayState = true;
      } else if (g_hum < HUM_OFF){
        relayState = false;
      } else if (g_hum > HUM_OFF && g_hum < HUM_ON){
        relayState = true;
      }

      digitalWrite(RELAISPIN2, relayState ? LOW : HIGH);

      Serial.print("Hum=");
      Serial.print(g_hum);
      Serial.print("  Relay=");
      Serial.println(relayState ? "ON" : "OFF");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000);

  dht.begin();
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FreeRTOS start");
  delay(2000);

  pinMode(RELAISPIN1, OUTPUT);
  digitalWrite(RELAISPIN1, LOW);

  pinMode(RELAISPIN2, OUTPUT);
  digitalWrite(RELAISPIN2, LOW);

  xTaskCreate(
    TaskDHT,          
    "TaskDHT",        
    4096,             
    NULL,             
    2,                
    &TaskDHTHandle    
  );

  xTaskCreate(
    TaskLCD,
    "TaskLCD",
    4096,
    NULL,
    1,                
    &TaskLCDHandle
  );

    xTaskCreate(
    TaskRelaisLamp,
    "TaskRelaisLamp",
    2048,        
    NULL,
    1,
    &TaskRelaisLampHandle
  );

    xTaskCreate(
    TaskRelaisFan,
    "TaskRelaisFan",
    2048,
    NULL,
    1,
    &TaskRelaisFanHandle
    );
}

void loop() {
  // leeg laten of alleen heel lichte zaken
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}

