# ⚡ TRIPWIRE // ESP32 CYBERPUNK RECON PLATFORM

<p align="center">
  <img src="images/banner.png" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP32-S3-red?style=for-the-badge">
  <img src="https://img.shields.io/badge/BLE-SCANNER-blue?style=for-the-badge">
  <img src="https://img.shields.io/badge/WIFI-RECON-green?style=for-the-badge">
  <img src="https://img.shields.io/badge/CYBERPUNK-UI-purple?style=for-the-badge">
  <img src="https://img.shields.io/badge/STATUS-IN%20DEVELOPMENT-orange?style=for-the-badge">
</p>

---

# 🛰 WHAT IS TRIPWIRE?

**TRIPWIRE** is a handheld cyberpunk-inspired reconnaissance platform built using an ESP32, round TFT display and SD card logging system.

Designed as a futuristic portable recon tool, TRIPWIRE scans for nearby BLE and WiFi devices, visualises them on a live radar interface, logs detections to SD storage and provides real-time proximity feedback using geiger-style audio.

Inspired by:

* piglet
* halehound
* digital tails

---

# ⚡ CURRENT FEATURES

## 📡 BLE Device Scanning

* Real-time Bluetooth scanning
* Live device plotting
* RSSI signal tracking
* Known device monitoring

## 📶 WiFi Recon Mode

* Nearby AP discovery
* Signal strength visualisation

## 🎯 Radar Interface

* Animated sweep scanner
* Dynamic target plotting
* Signal-strength colouring
* Cyberpunk UI styling

## 🔊 Geiger Audio Feedback

* Faster beeps as targets get closer
* Proximity-based alert system

## 💾 SD Card Logging

* Device history
* Threat tracking
* Persistent storage
* Boot assets support

## 🖼 Custom Boot Animation

* Full-screen RGB565 splash screen
* Instant rendering
* No SD parsing lag

---

# 🛠 HARDWARE USED

| Component             | Purpose           |
| --------------------- | ----------------- |
| ESP32-S3 / XIAO ESP32 | Main controller   |
| Round TFT Display     | Radar UI          |
| SD Card Module        | Logging + assets  |
| Passive Buzzer        | Geiger audio      |
| LiPo Battery          | Portable power    |
| Antennas              | Extended scanning |

---

# 📂 PROJECT STRUCTURE

```bash id="apfhm5"
TRIPWIRE/
│
├── images/
├── icons/
├── themes/
├── boot/
│
├── known_devices.txt
├── threats.txt
├── logs.txt
│
├── boot.h
└── tripwire_x_round_display_full.ino
```

---

# ⚙ INSTALLATION

## 1️⃣ Clone Repo

```bash id="0ofiv4"
git clone https://github.com/Notorious-Squirrel/TRIPWIRE.git
```

---

## 2️⃣ Install Arduino Libraries

Required libraries:

* TFT_eSPI
* ESP32 BLE Arduino
* SPI
* SD

---

## 3️⃣ Configure TFT_eSPI

Configure your display pins inside:

```bash id="urjlwm"
User_Setup.h
```

---

## 4️⃣ Upload Boot Image

Convert your boot image into:

* RGB565
* uint16_t array
* PROGMEM enabled

Save as:

```bash id="0sg8g6"
boot.h
```

---

## 5️⃣ Upload Firmware

Flash using Arduino IDE or PlatformIO.

---

# 🖥 UI PREVIEW

## Boot Screen

```text id="jbjlwm"
[ CUSTOM CYBERPUNK BOOT LOGO ]
```

## Radar Mode

```text id="1b3b8v"
LIVE TARGETS
KNOWN DEVICES
SIGNAL SWEEP
PROXIMITY ALERTS
```

---

# 🔥 WHY THIS PROJECT EXISTS

Modern wireless environments are saturated with invisible devices.

TRIPWIRE was built to:

* visualise nearby wireless activity
* create a cinematic cyberpunk recon platform
* explore embedded recon tooling
* learn ESP32 low-level scanning
* build a real-world portable scanner system

---

# ⚠ DISCLAIMER

This project is intended for:

* educational purposes
* defensive research
* authorised testing
* hardware experimentation

Do not use this project unlawfully or against networks/devices you do not own or have permission to analyse.

---

# 📸 MEDIA

<p align="center">
  <img src="images/radar.jpg" width="45%">
  <img src="images/boot.jpg" width="45%">
</p>

---

# ⭐ SUPPORT THE PROJECT

If you enjoy this project:

⭐ Star the repository
📺 Subscribe on YouTube
🛠 Fork and build your own
🔥 Share your modifications

---

# 👾 CREATED BY

## NOTORIOUS SQUIRREL

> “Turning invisible signals into visible intelligence.”

🌃 Cyberpunk Hardware
📡 Wireless Recon
🛠 Embedded Systems
⚡ ESP32 Development

---
