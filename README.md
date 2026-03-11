# 🔥 EmberOS — A Full Browser-Based OS on a $1 ESP32

> **A professional UI Operating System that runs entirely on a $1 ESP32 chip — local, no server, no internet needed.**

![EmberOS](https://img.shields.io/badge/Platform-ESP32-red?style=for-the-badge)
![Version](https://img.shields.io/badge/Version-1.0.0--BETA-orange?style=for-the-badge)
![License](https://img.shields.io/badge/License-Custom--Protected-darkred?style=for-the-badge)
![Arduino](https://img.shields.io/badge/IDE-Arduino-blue?style=for-the-badge)

---

## 🌟 What is EmberOS?

**EmberOS** is a fully functional browser-based Operating System served directly from an ESP32 microcontroller via its own WiFi hotspot. Connect to it from any phone, tablet, or computer browser — no internet, no cloud, no external server. Just a $1 chip.

Built over **2 months** and developed by **[CihoRobotics](https://github.com/CihoRobotics)**.

---

## ✨ Features

### 🖥️ OS Interface
- Windows-like UI with dock, topbar, clock & date
- Glassmorphism design with animated custom cursor
- 8 color themes + notepad light/dark mode
- Fully responsive — works on mobile, tablet, desktop

### 📱 Built-in Apps
- **Terminal** — full command-line interface
- **Notepad** — with file save/load/delete system
- **Settings** — about, connectivity, I/O config, time & date
- **Theme Manager** — 8 themes, live switching
- **Info App** — chip model, MAC, heap, CPU, SDK
- **WiFi Scanner** — scan nearby 2.4GHz networks with signal strength
- **Hotspot Config** — view SSID and password
- **Config I/O** — toggle Input/Output/I2C/SPI/PWM/ADC

### ⚡ Terminal Commands

#### GPIO Control
```
set/ GPIO X output          → Set pin as OUTPUT
set/ GPIO X input           → Set pin as INPUT
set/ GPIO X buzzer          → Set OUTPUT pin as Buzzer
set/ GPIO X dht11           → Set INPUT pin as DHT11 sensor
set/ GPIO X ldr             → Set INPUT pin as LDR sensor
set/ GPIO X ir              → Set INPUT pin as IR receiver
set/ GPIO X servo           → Set OUTPUT pin as Servo motor
set/ GPIO X ultrasonicN TRIG   → Set OUTPUT pin as Ultrasonic TRIG
set/ GPIO X ultrasonicN ECHO   → Set INPUT pin as Ultrasonic ECHO
set/ GPIO X i2c SDA         → Set OUTPUT pin as I2C SDA
set/ GPIO X i2c SCL         → Set OUTPUT pin as I2C SCL
set/ GPIO i2c LCD           → Initialize I2C LCD Display
set/ i2c adress 0xAA        → Set custom I2C address (default: 0x27)
unset/ GPIO X               → Reset pin to default
```

#### GPIO Run
```
run/ GPIO X HIGH            → Set pin HIGH
run/ GPIO X LOW             → Set pin LOW
run/ GPIO X BUZZER TONE C   → Play note (C D E F G A H)
run/ GPIO X BUZZER STOP     → Stop buzzer
run/ GPIO X servo 090       → Turn servo to degree (0-180)
run/ GPIO X servo STOP      → Stop servo
run/ GPIO X servo visual    → Open visual servo slider
```

#### GPIO Read
```
read/ GPIO X                → Digital read (100ms refresh popup)
read/ GPIO X DHT11          → Temperature & humidity popup (1s refresh)
read/ GPIO X LDR            → Light level % popup (1s refresh)
read/ GPIO X ultrasonicN    → Distance cm + inch popup (0.5s refresh)
read/ GPIO X IR             → IR code popup (200ms refresh)
```

#### LCD Display
```
Write/ LCD "YOUR TEXT"      → Write text on LCD
Draw/ LCD smiley            → Draw a smiley on LCD
Erase/ LCD                  → Clear the LCD display
```

#### Scripting Engine
```
code/ GPIO X HIGH, delay 1000, GPIO X LOW, delay 1000, loop
```
Supported blocks: `GPIO X HIGH`, `GPIO X LOW`, `GPIO X BUZZER TONE Y`, `GPIO X BUZZER STOP`, `delay TTTT`, `loop`

#### Utility
```
/show GPIO                  → List all active GPIO pins
/clear                      → Clear terminal
```

---

## 🛠️ Hardware Requirements

| Component | Details |
|-----------|---------|
| ESP32 | WROOM, C3 SuperMini, or WROOM S3 (~$1) |
| DHT11 | Temperature & Humidity sensor (optional) |
| LDR | Light sensor (optional) |
| HC-SR04 | Ultrasonic distance sensor (optional) |
| IR Receiver | VS1838B or similar (optional) |
| Servo Motor | SG90 or similar (optional) |
| LCD 16x2 | With I2C backpack module (optional) |
| Buzzer | Passive buzzer (optional) |

---

## 📚 Required Libraries

Install these from **Arduino Library Manager**:

| Library | Author |
|---------|--------|
| DHT sensor library | Adafruit |
| IRremoteESP8266 | David Conran et al. |
| ESP32Servo | Kevin Harrington |
| LiquidCrystal I2C | Frank de Brabander |

---

## 🚀 Installation

1. Install **Arduino IDE**
2. Add ESP32 board support:
   - Go to `File → Preferences`
   - Add to Board Manager URLs: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Go to `Tools → Board → Board Manager` → Search `esp32` → Install
3. Install all required libraries from **Library Manager**
4. Open `EmberOS.ino` in Arduino IDE
5. Select your ESP32 board under `Tools → Board`
6. Upload the code
7. Connect to WiFi: **`EmberOS`** / Password: **`12345678`**
8. Open your browser and go to **`192.168.4.1`**

---

## 📡 How It Works

EmberOS turns your ESP32 into a WiFi hotspot. Any device that connects to it can open the OS in their browser at `192.168.4.1`. The entire UI — HTML, CSS, JavaScript — is stored inside the ESP32's flash memory and served on demand. No internet. No cloud. Just a $1 chip.

---

## 📁 Project Structure

```
EmberOS/
├── EmberOS.ino       ← Main Arduino sketch (full OS)
├── README.md         ← This file
└── LICENSE           ← Custom license
```

---

## ⚖️ License

**Custom Protected License — © CihoRobotics**

You may download and use this project for **personal and educational purposes only**.

You may **NOT**:
- Sell, resell, or commercially distribute this project or any part of it
- Remix, modify, or build upon this project and present it as your own work
- Redistribute this project under a different name or identity
- Claim authorship of this project in any form

Don't forget! This project was built over **2 months and 50-100 hours** of original work. Please respect the effort behind it.

For permissions beyond personal use, contact: **github.com/CihoRobotics**

---

## 👨‍💻 Author

**CihoRobotics**
- GitHub: [@CihoRobotics](https://github.com/CihoRobotics)

---

## ⭐ Support

If you like this project, please **star the repo** and **share it**! It helps more people discover EmberOS and supports future development. 🔥
Also, I had needed so much time for this project. Please suport me/subcribe and check out my videos on my youtube channel too: https://www.youtube.com/@CihoRobotics

---

## 🛑 Stop!

Don't forget that this project is the first DEMO and BETA version of the real Ember OS. There can be unexpected bugs, and we will try to repair them. 😎

---

*EmberOS — The world's cheapest computer web UI based OS. Built with passion. 🔥*
