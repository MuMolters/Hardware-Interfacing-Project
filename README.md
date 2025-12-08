# Terrarium Klimaatregeling (ESP32 + FreeRTOS)

Dit project is een geautomatiseerd klimaatbeheersingssysteem voor een terrarium met twee slangen.  
Een ESP32 leest een DHT11-sensor uit en schakelt via relais een verwarmingslamp en ventilator, terwijl een 16x2 LCD via I2C de waarden toont en een drukknop het meetinterval instelt.[file:18]

## Onderdelen

| Component           | Rol                                      | Samenwerking |
|--------------------|-------------------------------------------|-------------|
| Adafruit Feather V2 (ESP32) | Hoofd computing unit waar alle FreeRTOS-taken op draaien | Stuurt sensoren, relais en LCD aan |
| LCD 16x2 (I2C)     | Visuele weergave van data                | Toont temperatuur, luchtvochtigheid en menu |
| DHT11              | Meten van luchtvochtigheid en temperatuur | Levert meetdata voor lamp- en ventilatorlogica |
| Relais (2×)        | Schakelen van 230V-lamp en ventilator    | Worden op basis van drempelwaarden aangestuurd |

Onder de tabel kun je de afbeelding met het schakelschema laten staan.

---

## Benodigde software

- **Arduino IDE** (1.8.x of 2.x) of PlatformIO
- **ESP32 board support** voor Arduino
- De volgende libraries:
  1. `DHT sensor library` by Adafruit  
  2. `Adafruit Unified Sensor` (dependency van DHT)[web:25]  
  3. `LiquidCrystal_I2C` by Frank de Brabander

---

## ESP32 board installeren (Arduino IDE)

1. Open **File → Preferences**.  
2. Voeg deze URL toe bij **Additional Boards Manager URLs**:  
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`[web:25]  
3. Ga naar **Tools → Board → Boards Manager…**.  
4. Zoek op **ESP32** en installeer **esp32 by Espressif Systems**.  
5. Kies daarna bij **Tools → Board**: `Adafruit Feather ESP32 V2` (of een vergelijkbare ESP32-boarddefinitie).[file:18]

---

## Libraries installeren

### Via Library Manager

1. Ga naar **Sketch → Include Library → Manage Libraries…**.  
2. Zoek en installeer:
   - `DHT sensor library` (Adafruit)
   - `Adafruit Unified Sensor`
   - `LiquidCrystal I2C` (Frank de Brabander)[web:25]

Hieronder is een visuele weergave van het schakelschema en hoe alles aan te sluiten:
![Image](Images/Curcuit-io.png)
