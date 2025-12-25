
# Alle Steckpläne der drei verschiedenen Boards

## Steckplan E-Ink Display

| **Display Pin** | **Funktion** | **ESP32 Pin** |
| --- | --- | --- |
| VCC | Stromversorgung | 3.3V |
| GND | Masse | GND |
| DIN (MOSI) | Daten (SPI) | GPIO 23 |
| CLK (SCK) | Takt (SPI) | GPIO 18 |
| CS  | Chip Select | GPIO 5 |
| DC  | Data/Command | GPIO 17 |
| RST | Reset | GPIO 16 |
| BUSY | Busy Signal | GPIO 4 |

## Steckplan Mikrofon 

| **Mikrofon Pin** | **Funktion** | **ESP32 Pin** |
| --- | --- | --- |
| VDD | Stromversorgung | 3.3V |
| GND | Masse | GND |
| SD (DOUT) | Serial Data Out | GPIO 11 |
| WS (LRCLK) | Word Select / Left-Right Clock | GPIO 12 |
| SCK (BCLK) | Serial Clock / Bit Clock | GPIO 10 |
| L/R | Kanal-Auswahl (Left/Right) | GND (für Left) |

## Steckplan Lichtschranke

| **Sensor Pin** | **Funktion** | **ESP32 Pin** |
| --- | --- | --- |
| **VL53L0X Sensor 1 (Eingang)** |     |     |
| VCC | Stromversorgung | 3.3V |
| GND | Masse | GND |
| SDA | Daten (I2C) | GPIO 7 (IO7) |
| SCL | Takt (I2C) | GPIO 6 (IO6) |
| XSHUT | Shutdown/Reset | GPIO 2 |
| **VL6180X Sensor (Mitte - Validierung)** |     |     |
| VCC | Stromversorgung | 3.3V |
| GND | Masse | GND |
| SDA | Daten (I2C) | GPIO 7 (IO7) |
| SCL | Takt (I2C) | GPIO 6 (IO6) |
| XSHUT | Shutdown/Reset | GPIO 11 |
| **VL53L0X Sensor 2 (Ausgang)** |     |     |
| VCC | Stromversorgung | 3.3V |
| GND | Masse | GND |
| SDA | Daten (I2C) | GPIO 7 (IO7) |
| SCL | Takt (I2C) | GPIO 6 (IO6) |
| XSHUT | Shutdown/Reset | GPIO 3 |

