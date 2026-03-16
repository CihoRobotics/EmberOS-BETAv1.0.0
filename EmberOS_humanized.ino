// ============================================================
//  EmberOS v1.0.0 BETA — ESP32 Firmware
//  A web-based GPIO control interface with a built-in "OS"
//  served over Wi-Fi. Think of it as a tiny desktop running
//  on a microcontroller, accessible from any browser.
// ============================================================


// ── Dependencies ─────────────────────────────────────────────
// Standard ESP32 networking + a bunch of sensor/peripheral libs
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>
#include <map>
#include <DHT.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


// ── Wi-Fi / Hotspot Config ────────────────────────────────────
// The ESP32 runs as an access point — connect to this from
// your phone or laptop to reach the EmberOS web interface.
const char* ssid = "EmberOS";
const char* password = "12345678";

WebServer server(80);


// ── GPIO State Tracking ───────────────────────────────────────
// We keep runtime maps of what every pin is currently doing
// so the system can validate commands before executing them.

// pin number → mode string (e.g. "OUTPUT", "DHT11", "SERVO")
std::map<int, String> activePins;

// pins that have been promoted to buzzer mode
std::map<int, bool> buzzerPins;

// DHT11 sensor objects, one per pin
std::map<int, DHT*> dhtPins;

// LDR sensors — just a flag, analogRead does the work
std::map<int, bool> ldrPins;

// Ultrasonic sensors need two pins each (TRIG + ECHO),
// keyed by a user-chosen sensor name like "ultrasonic1"
std::map<String, int> ultrasonicTrig;
std::map<String, int> ultrasonicEcho;

// IR receiver objects, one per pin
std::map<int, IRrecv*> irPins;

// Remember the last IR code we saw on each pin,
// so the UI can poll and get the most recent result
std::map<int, String> irLastCode;

// Servo objects, one per pin
std::map<int, Servo*> servoPins;


// ── I2C / LCD State ───────────────────────────────────────────
// I2C needs an SDA and SCL pin assigned before LCD can init.
// We store -1 as "not yet assigned".
int i2cSDAPin = -1;
int i2cSCLPin = -1;

// Default I2C address for most cheap 16x2 LCD backpacks
uint8_t lcdAddress = 0x27;
bool lcdReady = false;
LiquidCrystal_I2C* lcd = nullptr;


// ── Script Executor State ─────────────────────────────────────
// The "code/" terminal command lets users write simple scripts
// (comma-separated blocks like "GPIO 5 HIGH, delay 500, GPIO 5 LOW").
// These run non-blocking inside loop() using a time-based state machine.
String scriptBlocks[50];
int scriptBlockCount = 0;
bool scriptRunning = false;
bool scriptLoop = false;
int scriptIndex = 0;
unsigned long scriptNextTime = 0;


// ── Frontend HTML/CSS/JS ──────────────────────────────────────
// The entire EmberOS web UI lives in this one big PROGMEM string.
// It's served on GET / and runs entirely in the browser —
// no external resources, no CDN, just pure vanilla HTML.
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>EmberOS-BETA v1.0.0</title>
<style>
:root { --main-color: #ff5555; --bg-color: #5a0000; --np-bg: #000; --np-text: #fff; }
html, body { margin: 0; width: 100%; height: 100%; overflow: hidden; background: linear-gradient(135deg, #000000 0%, #000000 65%, var(--bg-color) 100%); font-family: 'Segoe UI', sans-serif; cursor: none; transition: background 0.5s ease; }
#topbar-wrapper { position: fixed; top: 8px; width: 100%; display: flex; justify-content: center; z-index: 1000; pointer-events: none; }
#topbar { width: 480px; height: 40px; border-radius: 20px; background: rgba(15,15,15,0.9); backdrop-filter: blur(15px); box-shadow: 0 4px 25px rgba(0,0,0,0.8), 0 0 15px var(--bg-color); color: #ccc; font-size: 13px; display: flex; align-items: center; justify-content: center; position: relative; border: 1px solid rgba(255,255,255,0.12); }
#date { position: absolute; left: 20px; color: #888; font-weight: bold; }
#clock { font-weight: bold; color: var(--main-color); text-shadow: 0 0 8px var(--main-color); }
#link-emoji { position: absolute; right: 20px; font-size: 18px; color: var(--main-color); pointer-events: auto; cursor: none; transition: 0.3s; }
#link-emoji:hover { transform: scale(1.4); filter: drop-shadow(0 0 8px var(--main-color)); }
#dock-wrapper { position: fixed; top: 60px; left: 0; width: 95px; height: calc(100% - 60px); display: flex; justify-content: center; align-items: center; z-index: 900; }
#dock { width: 75px; padding: 25px 0; border-radius: 28px; background: rgba(10,10,10,0.8); backdrop-filter: blur(12px); display: flex; flex-direction: column; align-items: center; gap: 22px; border: 1px solid rgba(255,255,255,0.05); }
.app-box { width: 54px; height: 54px; border-radius: 15px; background: #000; display: flex; align-items: center; justify-content: center; cursor: none; position: relative; overflow: hidden; transition: 0.4s; border: 1px solid #1a1a1a; }
.app-box::before { content: ""; position: absolute; top: 0; left: 0; width: 100%; height: 66%; background: linear-gradient(to bottom, var(--main-color) 0%, transparent 100%); opacity: 0.3; transition: 0.3s; }
.app-box svg, .app-box .icon-text { z-index: 2; width: 30px; height: 30px; fill: var(--main-color); color: var(--main-color); filter: drop-shadow(0 0 5px var(--main-color)); }
.app-box:hover { transform: scale(1.1); border-color: var(--main-color); }
#terminal, #info-app-panel, #themes-app-panel, #menu-panel, #notepad-app, #settings-app, #wifi-app, #hotspot-app, #io-app, #time-app { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); background: rgba(5, 5, 5, 0.98); border: 1px solid var(--bg-color); border-radius: 22px; display: none; z-index: 500; color: #fff; box-shadow: 0 0 70px #000; }
.win-x { position: absolute; top: 18px; right: 18px; width: 28px; height: 28px; background: rgba(255,0,0,0.1); border-radius: 50%; color: var(--main-color); display: flex; align-items: center; justify-content: center; font-size: 14px; cursor: none; transition: 0.2s; }
.win-x:hover { background: var(--main-color); color: #000; }
.win-back { position: absolute; top: 18px; right: 54px; width: 28px; height: 28px; background: rgba(255,255,255,0.05); border-radius: 50%; color: #fff; display: flex; align-items: center; justify-content: center; font-size: 14px; cursor: none; transition: 0.2s; }
.win-back:hover { background: #fff; color: #000; transform: scale(1.1); }
#notepad-app { width: 600px; height: 420px; background: var(--np-bg); color: var(--np-text); overflow: hidden; }
#np-editor { width: 100%; height: 100%; padding: 60px 25px 25px; background: transparent; color: inherit; border: none; outline: none; font-size: 16px; resize: none; box-sizing: border-box; }
.np-toolbar { position: absolute; top: 18px; right: 60px; display: flex; gap: 15px; z-index: 10; }
.np-btn { font-size: 18px; cursor: none; transition: 0.3s; opacity: 0.7; }
.np-btn:hover { opacity: 1; transform: scale(1.3); filter: drop-shadow(0 0 5px var(--main-color)); }
#np-file-list { position: absolute; top: 55px; left: 0; width: 100%; height: calc(100% - 55px); background: rgba(5,5,5,0.98); display: none; padding: 20px; box-sizing: border-box; overflow-y: auto; z-index: 20; color: #fff; }
.np-file-item { display: flex; justify-content: space-between; align-items: center; padding: 12px; border-bottom: 1px solid #222; transition: 0.2s; }
.np-file-item:hover { background: rgba(255,255,255,0.05); }
.np-del-btn:hover { transform: scale(1.4); filter: drop-shadow(0 0 5px #f00); }
#settings-app { width: 500px; padding: 60px 20px 30px; min-height: 400px; }
.st-container { display: flex; flex-direction: column; gap: 12px; }
.st-item { width: 100%; height: 55px; background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; display: flex; align-items: center; padding: 0 20px; box-sizing: border-box; cursor: none; transition: 0.3s; font-weight: 500; color: #eee; }
.st-item:hover { background: rgba(255,255,255,0.07); border-color: var(--main-color); transform: translateX(10px); box-shadow: -5px 0 15px var(--main-color); }
#wifi-app { width: 450px; padding: 60px 20px 30px; height: 450px; }
.wifi-info-text { color: #888; font-size: 12px; text-align: center; margin-bottom: 15px; }
.wifi-list { display: flex; flex-direction: column; gap: 10px; overflow-y: auto; max-height: 320px; padding-right: 5px; }
.wifi-item { display: flex; justify-content: space-between; align-items: center; padding: 15px; background: rgba(255,255,255,0.03); border: 1px solid #222; border-radius: 10px; cursor: none; transition: 0.3s; }
.wifi-item:hover { border-color: var(--main-color); transform: scale(1.02); }
.wifi-name { font-weight: bold; }
.wifi-dbm { font-size: 12px; font-weight: bold; }
#wifi-details-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 300px; background: #151515; border: 1px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 4000; box-shadow: 0 0 50px #000; }
.wifi-detail-line { margin: 10px 0; font-size: 14px; color: #fff; }
.storage-header { font-size: 14px; color: #aaa; text-align: center; margin-bottom: 20px; font-weight: bold; }
.storage-bar-container { width: 100%; height: 35px; background: #111; border: 1px solid #333; position: relative; margin-bottom: 35px; border-radius: 4px; overflow: visible; }
.storage-fill { height: 100%; background: var(--main-color); width: 0%; transition: 1s ease-in-out; box-shadow: 0 0 15px var(--main-color); }
.storage-label { position: absolute; font-size: 11px; color: #666; top: 40px; }
.label-used { left: 0; }
.label-total { right: 0; }
.label-percent { left: 50%; transform: translateX(-50%); color: var(--main-color); font-weight: bold; }
.storage-list { max-height: 200px; overflow-y: auto; display: flex; flex-direction: column; gap: 8px; margin-top: 10px; padding-right: 5px; }
.storage-row { width: 100%; height: 40px; background: rgba(255,255,255,0.02); border-radius: 8px; display: flex; align-items: center; justify-content: space-between; padding: 0 15px; box-sizing: border-box; border: 1px solid #1a1a1a; }
.storage-row span:first-child { color: #888; font-size: 13px; }
.storage-val-box { background: #000; border: 1px solid var(--main-color); color: var(--main-color); padding: 3px 10px; border-radius: 4px; font-size: 11px; font-weight: bold; min-width: 60px; text-align: center; }
#np-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 280px; background: #111; border: 1px solid var(--main-color); border-radius: 15px; padding: 20px; display: none; z-index: 3000; text-align: center; box-shadow: 0 0 50px #000; }
#pop-text { color: var(--main-color); font-weight: bold; }
.pop-btns { display: flex; justify-content: center; gap: 15px; margin-top: 20px; }
.pop-btn { padding: 8px 20px; border-radius: 5px; cursor: none; font-weight: bold; transition: 0.3s; border: none; }
.btn-yes { background: var(--main-color); color: #000; }
.btn-yes:hover { transform: scale(1.1); box-shadow: 0 0 15px var(--main-color); }
.btn-no { background: #333; color: #fff; }
.btn-no:hover { background: #444; transform: scale(1.1); box-shadow: 0 0 10px rgba(255,255,255,0.2); }
#themes-app-panel { width: 450px; padding: 60px 30px 40px; }
.themes-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 20px; margin-bottom: 30px; justify-items: center; }
.theme-box { width: 70px; height: 70px; border-radius: 15px; border: 2px solid #222; position: relative; cursor: none; transition: 0.3s cubic-bezier(0.4, 0, 0.2, 1); }
.theme-box:hover { transform: scale(1.15); border-color: #fff; box-shadow: 0 0 20px rgba(255,255,255,0.2); }
.theme-box.active::after { content: "✔️"; position: absolute; top: -10px; right: -10px; font-size: 10px; background: #fff; border-radius: 50%; width: 22px; height: 22px; display: flex; align-items: center; justify-content: center; color: #000; font-weight: bold; }
.np-selector { display: flex; justify-content: center; gap: 30px; border-top: 1px solid #222; padding-top: 25px; }
.np-box { width: 80px; height: 35px; border-radius: 8px; border: 2px solid #333; cursor: none; transition: 0.3s; position: relative; }
.np-box:hover { transform: scale(1.1); border-color: var(--main-color); }
.np-box.active::after { content: "✔️"; position: absolute; top: -12px; right: -12px; font-size: 8px; background: #0f0; border-radius: 50%; width: 18px; height: 18px; display: flex; align-items: center; justify-content: center; color: #000; }
#info-app-panel { width: 380px; padding: 60px 25px 70px; }
.info-header { font-size: 10px; color: #666; margin-bottom: 15px; text-align: center; font-weight: bold; border-bottom: 1px solid #222; padding-bottom: 8px; }
.info-v-item { border-bottom: 1px solid #111; padding: 8px 0; }
.info-v-label { color: #555; font-size: 10px; text-transform: uppercase; }
.info-v-data { color: var(--main-color); font-weight: bold; font-size: 15px; margin-top: 2px; display: block; }
.plus-minus-btn { position: absolute; bottom: 20px; right: 25px; font-size: 28px; cursor: none; color: var(--main-color); transition: 0.3s; opacity: 0.7; }
.plus-minus-btn:hover { opacity: 1; transform: scale(1.3); filter: drop-shadow(0 0 8px var(--main-color)); }
#menu-panel { width: 320px; padding: 50px; justify-content: center; gap: 40px; border: 2px solid var(--main-color); }
.menu-box { width: 90px; height: 90px; }
.menu-box span { font-size: 40px; }
#terminal { top: 60px; left: 110px; right: 30px; bottom: 30px; transform: none; padding: 55px 25px 25px; color: var(--main-color); font-family: monospace; white-space: pre-wrap; overflow-y: auto; }
#cursor { position: fixed; width: 12px; height: 12px; border-radius: 50%; background: #fff; box-shadow: 0 0 10px #fff; pointer-events: none; z-index: 9999; }
.hs-input-text { background: #111; border: 1px solid #333; color: var(--main-color); padding: 12px; border-radius: 8px; margin-top: 5px; font-size: 14px; font-weight: bold; width: 250px; display: block; }
#hotspot-app { width: 450px; padding: 60px 30px 40px; }
.hs-gap { margin-top: 25px; }
#espnow-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 250px; background: #151515; border: 1px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 5000; text-align: center; box-shadow: 0 0 50px #000; }
.espnow-btn { margin-top: 20px; padding: 10px 25px; background: #222; border: 1px solid var(--main-color); border-radius: 8px; color: #fff; cursor: none; font-weight: bold; transition: 0.3s; }
.espnow-btn:hover { background: var(--main-color); color: #000; }
#io-app { width: 500px; padding: 60px 30px 40px; }
.io-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 15px; margin-top: 20px; }
.io-box { padding: 15px; background: #111; border: 1px solid #333; border-radius: 10px; cursor: none; text-align: center; transition: 0.3s; }
.io-box:hover { border-color: var(--main-color); }
.io-status { font-weight: bold; margin-top: 5px; font-size: 12px; }
#time-app { width: 400px; padding: 60px 30px 40px; text-align: center; }
select { width: 100%; padding: 12px; background: #111; border: 1px solid #333; color: var(--main-color); border-radius: 8px; cursor: none; font-weight: bold; }
.save-btn { margin-top: 25px; padding: 12px 30px; background: var(--main-color); color: #000; border: none; border-radius: 8px; cursor: none; font-weight: bold; }
/* Read Popup Styles */
#read-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 200px; background: #050505; border: 2px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 6000; text-align: center; box-shadow: 0 0 50px #000; }
#read-title { color: #888; font-size: 12px; margin-bottom: 10px; text-transform: uppercase; }
#read-value { font-size: 48px; font-weight: bold; color: var(--main-color); text-shadow: 0 0 15px var(--main-color); }
/* DHT11 Popup Styles */
#dht-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 260px; background: #050505; border: 2px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 6000; text-align: center; box-shadow: 0 0 50px #000; }
#dht-title { color: #888; font-size: 12px; margin-bottom: 18px; text-transform: uppercase; }
.dht-row { display: flex; justify-content: space-around; align-items: flex-end; margin-bottom: 10px; }
.dht-block { display: flex; flex-direction: column; align-items: center; }
.dht-big { font-size: 48px; font-weight: bold; color: var(--main-color); text-shadow: 0 0 15px var(--main-color); line-height: 1; }
.dht-unit { font-size: 18px; font-weight: bold; color: var(--main-color); margin-left: 2px; }
.dht-small { font-size: 12px; color: #555; margin-top: 4px; }
.dht-fahr { font-size: 13px; color: #555; margin-top: 6px; }
/* LDR Popup Styles */
#ldr-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 220px; background: #050505; border: 2px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 6000; text-align: center; box-shadow: 0 0 50px #000; }
#ldr-title { color: #888; font-size: 12px; margin-bottom: 18px; text-transform: uppercase; }
#ldr-value { font-size: 48px; font-weight: bold; color: var(--main-color); text-shadow: 0 0 15px var(--main-color); line-height: 1; }
#ldr-unit { font-size: 18px; font-weight: bold; color: var(--main-color); }
.ldr-small { font-size: 12px; color: #555; margin-top: 8px; }
/* Ultrasonic Popup Styles */
#ultrasonic-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 240px; background: #050505; border: 2px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 6000; text-align: center; box-shadow: 0 0 50px #000; }
#ultrasonic-title { color: #888; font-size: 12px; margin-bottom: 18px; text-transform: uppercase; }
#ultrasonic-value { font-size: 48px; font-weight: bold; color: var(--main-color); text-shadow: 0 0 15px var(--main-color); line-height: 1; }
#ultrasonic-unit { font-size: 18px; font-weight: bold; color: var(--main-color); }
.ultrasonic-inch { font-size: 13px; color: #555; margin-top: 6px; }
.ultrasonic-small { font-size: 12px; color: #555; margin-top: 4px; }
/* IR Popup Styles */
#ir-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 280px; background: #050505; border: 2px solid var(--main-color); border-radius: 15px; padding: 25px; display: none; z-index: 6000; text-align: center; box-shadow: 0 0 50px #000; }
#ir-title { color: #888; font-size: 12px; margin-bottom: 18px; text-transform: uppercase; }
#ir-value { font-size: 32px; font-weight: bold; color: var(--main-color); text-shadow: 0 0 15px var(--main-color); line-height: 1; word-break: break-all; }
.ir-small { font-size: 12px; color: #555; margin-top: 8px; }
/* Servo Popup Styles */
#servo-popup { position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 340px; background: #050505; border: 2px solid var(--main-color); border-radius: 15px; padding: 35px 30px 30px; display: none; z-index: 6000; text-align: center; box-shadow: 0 0 50px #000; }
#servo-title { color: #888; font-size: 12px; margin-bottom: 28px; text-transform: uppercase; }
.servo-slider-wrap { position: relative; width: 100%; height: 36px; display: flex; align-items: center; margin-bottom: 18px; }
#servo-slider { -webkit-appearance: none; appearance: none; width: 100%; height: 10px; border-radius: 5px; background: #222; outline: none; cursor: none; border: 1px solid #333; }
#servo-slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 22px; height: 36px; border-radius: 5px; background: var(--main-color); cursor: none; box-shadow: 0 0 10px var(--main-color); border: 2px solid #000; transition: box-shadow 0.2s; }
#servo-slider::-webkit-slider-thumb:hover { box-shadow: 0 0 20px var(--main-color); }
#servo-slider::-moz-range-thumb { width: 22px; height: 36px; border-radius: 5px; background: var(--main-color); cursor: none; box-shadow: 0 0 10px var(--main-color); border: 2px solid #000; }
#servo-degree { font-size: 42px; font-weight: bold; color: var(--main-color); text-shadow: 0 0 15px var(--main-color); line-height: 1; }
.servo-deg-label { font-size: 13px; color: #555; margin-top: 6px; }
</style>
</head>
<body onload="initSystem()">

<!-- ── Top Bar: clock + date + connectivity shortcut ── -->
<div id="topbar-wrapper"><div id="topbar">
    <div id="date">--.--.----</div>
    <div id="clock">00:00:00</div>
    <div id="link-emoji" onclick="toggleConnectivity()">🔗</div>
</div></div>

<!-- ── Left Dock: app launcher icons ── -->
<div id="dock-wrapper"><div id="dock">
    <div class="app-box" onclick="openTerminal()"><svg viewBox="0 0 24 24"><path d="M5 8l4 4-4 4M11 16h6" stroke="currentColor" stroke-width="2" fill="none" stroke-linecap="round" stroke-linejoin="round"/></svg></div>
    <div class="app-box" onclick="openNotepad()"><svg viewBox="0 0 24 24"><path d="M6 2h9l3 3v17H6z" fill="none" stroke="currentColor" stroke-width="2"/><path d="M9 9h6M9 13h6M9 17h4" stroke="currentColor" stroke-width="2"/></svg></div>
    <div class="app-box" onclick="openSettings()"><svg viewBox="0 0 24 24"><path d="M12 8a4 4 0 100 8 4 4 0 000-8z" fill="none" stroke="currentColor" stroke-width="2"/><path d="M2 12h3M19 12h3M12 2v3M12 19v3M4.9 4.9l2.1 2.1M16.9 16.9l2.1 2.1M4.9 19.1l2.1-2.1M16.9 7.1l2.1-2.1" stroke="currentColor" stroke-width="2"/></svg></div>
    <div class="app-box" onclick="openMenu()"><svg viewBox="0 0 24 24"><circle cx="6" cy="12" r="2" fill="currentColor"/><circle cx="12" cy="12" r="2" fill="currentColor"/><circle cx="18" cy="12" r="2" fill="currentColor"/></svg></div>
</div></div>

<!-- ── Terminal Window ── -->
<div id="terminal"><div class="win-x" onclick="killTerminal(event)">×</div><div id="term-content"></div></div>

<!-- ── Notepad App ── -->
<div id="notepad-app">
    <div class="np-toolbar">
        <div class="np-btn" onclick="npNew()">➕</div>
        <div class="np-btn" onclick="npToggleList()">📁</div>
    </div>
    <div class="win-x" onclick="hideApp('notepad-app')">×</div>
    <textarea id="np-editor" oninput="npAutoSave()" placeholder="Write Something..."></textarea>
    <div id="np-file-list"></div>
</div>

<!-- ── Settings App (has sub-views: About / Connectivity) ── -->
<div id="settings-app">
    <div id="st-main-view">
        <div class="win-x" onclick="hideApp('settings-app')">×</div>
        <div style="margin-bottom:20px; font-weight:bold; color:var(--main-color); letter-spacing:1px; text-align:center;">SYSTEM_SETTINGS</div>
        <div class="st-container">
            <div class="st-item" onclick="showAbout()">About</div>
            <div class="st-item" onclick="showConnectivity()">Connectivity</div>
            <div class="st-item" onclick="openIO()">Config I/O</div>
            <div class="st-item" onclick="openTime()">Time & Date</div>
        </div>
    </div>
    <div id="st-about-view" style="display:none;">
        <div class="win-x" onclick="hideApp('settings-app')">×</div>
        <div class="win-back" onclick="backToSettings()">←</div>
        <div class="storage-header">Ember OS v1.0.0-BETA version</div>
        <div class="storage-bar-container">
            <div id="st-fill" class="storage-fill"></div>
            <div id="st-used-txt" class="storage-label label-used">0 KB</div>
            <div id="st-percent-txt" class="storage-label label-percent">0%</div>
            <div id="st-total-txt" class="storage-label label-total">4 MB</div>
        </div>
        <div class="storage-list" id="st-list"></div>
    </div>
    <div id="st-connectivity-view" style="display:none;">
        <div class="win-x" onclick="hideApp('settings-app')">×</div>
        <div class="win-back" onclick="backToSettings()">←</div>
        <div style="margin-bottom:20px; font-weight:bold; color:var(--main-color); letter-spacing:1px; text-align:center;">CONNECTIVITY</div>
        <div class="st-container">
            <div class="st-item" onclick="openWifiApp()">WI-FI</div>
            <div class="st-item" onclick="openHotspotApp()">HOTSPOT</div>
            <div class="st-item" onclick="showEspNow()">ESP-NOW</div>
        </div>
    </div>
</div>

<!-- ── Config I/O App ── -->
<div id="io-app">
    <div class="win-x" onclick="hideApp('io-app')">×</div>
    <div style="margin-bottom:20px; font-weight:bold; color:var(--main-color); text-align:center;">CONFIG I/O</div>
    <div class="io-grid" id="io-grid"></div>
</div>

<!-- ── Time & Date App ── -->
<div id="time-app">
    <div class="win-x" onclick="hideApp('time-app')">×</div>
    <div style="margin-bottom:20px; font-weight:bold; color:var(--main-color); text-align:center;">TIME & DATE</div>
    <div style="text-align:left; color:#888; font-size:12px; margin-bottom:5px;">Select Country:</div>
    <select id="country-select">
        <option value="Europe/Istanbul">Turkey</option>
        <option value="Europe/London">UK</option>
        <option value="America/New_York">USA</option>
        <option value="Europe/Berlin">Germany</option>
        <option value="Asia/Tokyo">Japan</option>
    </select>
    <button class="save-btn" onclick="saveTime()">SAVE</button>
</div>

<!-- ── Wi-Fi Scanner App ── -->
<div id="wifi-app">
    <div class="win-x" onclick="hideApp('wifi-app')">×</div>
    <div class="win-back" onclick="backToWifiSettings()">←</div>
    <div style="margin-bottom:10px; font-weight:bold; color:var(--main-color); text-align:center;">WI-FI SCAN</div>
    <div class="wifi-info-text">Just 2.4 Ghz devices available!</div>
    <div style="font-size:12px; margin-bottom:5px;">Discovered WI-FI Devices:</div>
    <div class="wifi-list" id="wifi-list"></div>
</div>

<!-- ── Hotspot Config App ── -->
<div id="hotspot-app">
    <div class="win-x" onclick="hideApp('hotspot-app')">×</div>
    <div class="win-back" onclick="backToConnectivity()">←</div>
    <div style="margin-bottom:15px; font-weight:bold; color:var(--main-color); text-align:center;">HOTSPOT CONFIG</div>
    <div class="hs-gap">SSID HOTSPOT: <span class="hs-input-text" id="hs-ssid"></span></div>
    <div class="hs-gap">PASS HOTSPOT: <span class="hs-input-text" id="hs-pass"></span></div>
</div>

<!-- ── Popups & Overlays ── -->

<!-- ESP-NOW isn't supported yet in this build -->
<div id="espnow-popup">
    <div style="color:#fff; margin-bottom:15px;">OS version not Compatible</div>
    <div class="espnow-btn" onclick="hideEspNow()">OK</div>
</div>

<!-- Wi-Fi network details popup -->
<div id="wifi-details-popup">
    <div class="win-x" onclick="closeWifiPopup()">×</div>
    <div id="wifi-popup-content"></div>
</div>

<!-- Notepad file delete confirmation -->
<div id="np-popup">
    <div id="pop-text">Are you sure you want to delete this file?</div>
    <div class="pop-btns">
        <button class="pop-btn btn-yes" id="pop-confirm">Yes</button>
        <button class="pop-btn btn-no" onclick="document.getElementById('np-popup').style.display='none'">Cancel</button>
    </div>
</div>

<!-- GPIO digital read display — polls every 100ms -->
<div id="read-popup">
    <div class="win-x" onclick="closeReadPopup()">×</div>
    <div id="read-title">READ GPIO X</div>
    <div id="read-value">0</div>
</div>

<!-- DHT11 temperature + humidity display -->
<div id="dht-popup">
    <div class="win-x" onclick="closeDhtPopup()">×</div>
    <div id="dht-title">READING GPIO X DHT11</div>
    <div class="dht-row">
        <div class="dht-block">
            <div style="display:flex;align-items:flex-start;">
                <span id="dht-temp" class="dht-big">--</span><span class="dht-unit">°C</span>
            </div>
            <div id="dht-fahr" class="dht-fahr">-- °F</div>
            <div class="dht-small">Temperature</div>
        </div>
        <div class="dht-block">
            <div style="display:flex;align-items:flex-start;">
                <span id="dht-hum" class="dht-big">--</span><span class="dht-unit">%</span>
            </div>
            <div class="dht-small">Humidity</div>
        </div>
    </div>
</div>

<!-- LDR light level display (0–100%) -->
<div id="ldr-popup">
    <div class="win-x" onclick="closeLdrPopup()">×</div>
    <div id="ldr-title">READING GPIO X LDR SENSOR:</div>
    <div style="display:flex;align-items:flex-start;justify-content:center;">
        <span id="ldr-value" class="dht-big">--</span><span id="ldr-unit" class="dht-unit">%</span>
    </div>
    <div class="ldr-small">Light Level</div>
</div>

<!-- Ultrasonic distance display — shows cm and inches -->
<div id="ultrasonic-popup">
    <div class="win-x" onclick="closeUltrasonicPopup()">×</div>
    <div id="ultrasonic-title">READING ULTRASONIC</div>
    <div style="display:flex;align-items:flex-start;justify-content:center;">
        <span id="ultrasonic-value" class="dht-big">--</span><span id="ultrasonic-unit" class="dht-unit">cm</span>
    </div>
    <div id="ultrasonic-inch" class="ultrasonic-inch">-- inch</div>
    <div class="ultrasonic-small">Distance</div>
</div>

<!-- IR receiver display — shows last decoded hex code -->
<div id="ir-popup">
    <div class="win-x" onclick="closeIrPopup()">×</div>
    <div id="ir-title">READING GPIO X IR SENSOR:</div>
    <div id="ir-value">--</div>
    <div class="ir-small">IR Code</div>
</div>

<!-- Servo control popup — drag the slider to set angle -->
<div id="servo-popup">
    <div class="win-x" onclick="closeServoPopup()">×</div>
    <div id="servo-title">Controlling Servo Motor.</div>
    <div class="servo-slider-wrap">
        <input type="range" id="servo-slider" min="0" max="180" value="90" oninput="onServoSlide(this.value)">
    </div>
    <div id="servo-degree">90</div>
    <div class="servo-deg-label">degrees</div>
</div>

<!-- ── Menu Panel (Themes + Info) ── -->
<div id="menu-panel">
  <div class="app-box menu-box" onclick="openThemes()"><span>🎨</span></div>
  <div class="app-box menu-box" onclick="openInfo()"><span style="font-size:35px;font-weight:bold;color:var(--main-color)">i</span></div>
</div>

<!-- ── System Info Panel ── -->
<div id="info-app-panel">
  <div class="win-x" onclick="hideApp('info-app-panel')">×</div>
  <div class="info-header">OS_BUILD: "Ember_OS 1.0.0 BETA - Open Source Version"</div>
  <div id="v-main-info"></div>
  <div id="extra-info-v" style="display:none; flex-direction:column;"></div>
  <div class="plus-minus-btn" id="v-toggle" onclick="toggleVInfo()">➕</div>
</div>

<!-- ── Theme Manager ── -->
<div id="themes-app-panel">
  <div class="win-x" onclick="hideApp('themes-app-panel')">×</div>
  <div style="margin-bottom:25px; font-weight:bold; color:var(--main-color); text-align:center; letter-spacing:2px;">THEME_MANAGER</div>
  <div class="themes-grid">
    <div class="theme-box" style="background:#ff5555;" onclick="setMainTheme('#ff5555','#5a0000',0)"></div>
    <div class="theme-box" style="background:#55ff55;" onclick="setMainTheme('#55ff55','#004400',1)"></div>
    <div class="theme-box" style="background:#5555ff;" onclick="setMainTheme('#5555ff','#00005a',2)"></div>
    <div class="theme-box" style="background:#ffffff;" onclick="setMainTheme('#ffffff','#333333',3)"></div>
    <div class="theme-box" style="background:#a020f0;" onclick="setMainTheme('#a020f0','#300040',4)"></div>
    <div class="theme-box" style="background:#ffaa00;" onclick="setMainTheme('#ffaa00','#552200',5)"></div>
    <div class="theme-box" style="background:#ffff55;" onclick="setMainTheme('#ffff55','#444400',6)"></div>
    <div class="theme-box" style="background:#222222;" onclick="setMainTheme('#555555','#111111',7)"></div>
  </div>
  <div class="np-selector">
    <div id="np-white" class="np-box" style="background:#fff;" onclick="setNPTheme('white')"></div>
    <div id="np-black" class="np-box active" style="background:#000;" onclick="setNPTheme('black')"></div>
  </div>
</div>

<!-- Custom cursor dot (the OS hides the real cursor) -->
<div id="cursor"></div>

<script>

// ── JS: Globals ───────────────────────────────────────────────

const PROMPT = "EmberOS> ";
let termLines = [PROMPT];
let curFile = null;

// I/O feature toggles — persisted in localStorage so they
// survive page refreshes. Gating things like OUTPUT/INPUT
// commands behind these keeps accidental pin changes safe-ish.
let ioStates = JSON.parse(localStorage.getItem('io_states') || '{"Input":false, "Output":false, "i2c":false, "SPI":false, "PWM":false, "ADC":false}');

let timeZone = localStorage.getItem('ember_tz') || 'Europe/Istanbul';

// Interval handles for sensor polling — we keep these so we
// can stop them when a popup is closed or closeAll() fires.
let readInterval = null;
let dhtInterval = null;
let ldrInterval = null;
let ultrasonicInterval = null;
let irInterval = null;

// Tracks which pin the servo popup is currently controlling
let servoActivePin = null;


// ── JS: Boot ─────────────────────────────────────────────────

// Called on page load — restores theme, starts the clock,
// fetches hotspot config, and renders the I/O toggle grid.
function initSystem() {
  const m = localStorage.getItem('ember_main');
  const b = localStorage.getItem('ember_bg');
  const i = localStorage.getItem('ember_idx');
  const n = localStorage.getItem('ember_np_mode') || 'black';
  if(m && b) { setMainTheme(m, b, parseInt(i), false); }
  else { setMainTheme('#ff5555', '#5a0000', 0, false); }
  setNPTheme(n);
  updateTime(); setInterval(updateTime, 1000);
  fetch('/getconfig').then(r=>r.json()).then(d=>{
    document.getElementById('hs-ssid').innerText = d.ssid;
    document.getElementById('hs-pass').innerText = d.pass;
  });
  renderIO();
}


// ── JS: Connectivity shortcut (the 🔗 button in topbar) ──────

function toggleConnectivity() {
    const s = document.getElementById("settings-app");
    if(s.style.display === "block") { hideApp('settings-app'); }
    else { closeAll(); s.style.display = "block"; showConnectivity(); }
}


// ── JS: Theme System ─────────────────────────────────────────

// Apply a main color + background combo and optionally save it.
// The idx param just tracks which swatch gets the checkmark.
function setMainTheme(main, bg, idx, save = true) {
  document.documentElement.style.setProperty('--main-color', main);
  document.documentElement.style.setProperty('--bg-color', bg);
  document.querySelectorAll('.theme-box').forEach((b,i)=> b.classList.toggle('active', i===idx));
  if(save) {
    localStorage.setItem('ember_main', main);
    localStorage.setItem('ember_bg', bg);
    localStorage.setItem('ember_idx', idx);
  }
}

// Switch the notepad between light and dark mode
function setNPTheme(mode) {
  const isW = mode === 'white';
  document.documentElement.style.setProperty('--np-bg', isW ? '#fff' : '#000');
  document.documentElement.style.setProperty('--np-text', isW ? '#000' : '#fff');
  document.getElementById('np-white').classList.toggle('active', isW);
  document.getElementById('np-black').classList.toggle('active', !isW);
  localStorage.setItem('ember_np_mode', mode);
}


// ── JS: Utilities ─────────────────────────────────────────────

// Human-readable byte sizes — used in the About storage view
function formatBytes(bytes) {
    if (bytes === 0) return '0 Byte';
    const k = 1024;
    const sizes = ['Byte', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}


// ── JS: Settings — About / Storage View ──────────────────────

// Calculates how much "flash" is being used by combining a
// hardcoded OS size estimate with actual localStorage usage.
// Not perfectly accurate but gives a useful visual indicator.
function showAbout() {
    document.getElementById('st-main-view').style.display = 'none';
    const aboutView = document.getElementById('st-about-view');
    aboutView.style.display = 'block';
    const list = document.getElementById('st-list');
    list.innerHTML = "";
    const osSize = 450 * 1024;
    let sysSavesSize = 0;
    ['ember_main', 'ember_bg', 'ember_idx', 'ember_np_mode'].forEach(key => {
        sysSavesSize += (localStorage.getItem(key) || "").length;
    });
    let notesStore = JSON.parse(localStorage.getItem('ember_files') || '{}');
    let totalNotesSize = 0;
    renderStorageRow("Ember OS", osSize);
    renderStorageRow("System Saves", sysSavesSize);
    list.innerHTML += `<div style="height:10px;"></div>`;
    Object.keys(notesStore).forEach(id => {
        let size = notesStore[id].content.length + notesStore[id].title.length;
        totalNotesSize += size;
        renderStorageRow(notesStore[id].title, size);
    });
    const totalFlash = 4 * 1024 * 1024;
    const usedTotal = osSize + sysSavesSize + totalNotesSize;
    const percent = ((usedTotal / totalFlash) * 100).toFixed(2);
    document.getElementById('st-fill').style.width = percent + "%";
    document.getElementById('st-used-txt').innerText = formatBytes(usedTotal);
    document.getElementById('st-total-txt').innerText = formatBytes(totalFlash);
    document.getElementById('st-percent-txt').innerText = percent + "%";
}

// Renders a single labeled row in the storage breakdown list
function renderStorageRow(label, bytes) {
    const list = document.getElementById('st-list');
    const row = document.createElement('div');
    row.className = "storage-row";
    row.innerHTML = `<span>${label}</span><div class="storage-val-box">${formatBytes(bytes)}</div>`;
    list.appendChild(row);
}


// ── JS: Settings — Navigation ─────────────────────────────────

function showConnectivity() {
    document.getElementById('st-main-view').style.display = 'none';
    const connectivityView = document.getElementById('st-connectivity-view');
    connectivityView.style.display = 'block';
}

function backToSettings() {
    document.getElementById('st-about-view').style.display = 'none';
    document.getElementById('st-connectivity-view').style.display = 'none';
    document.getElementById('st-main-view').style.display = 'block';
}

function backToConnectivity() {
    hideApp('hotspot-app');
    document.getElementById('st-connectivity-view').style.display = 'block';
}

function backToWifiSettings() {
    hideApp('wifi-app');
    document.getElementById('settings-app').style.display = 'block';
}


// ── JS: Config I/O Toggles ────────────────────────────────────

// Opens the I/O panel (closes settings first)
function openIO() {
    hideApp('settings-app');
    document.getElementById('io-app').style.display = 'block';
}

// Rebuilds the toggle grid from the current ioStates object.
// Clicking a box flips its state and re-renders.
function renderIO() {
    const grid = document.getElementById('io-grid');
    grid.innerHTML = "";
    Object.keys(ioStates).forEach(key => {
        let box = document.createElement('div');
        box.className = "io-box";
        box.onclick = () => { ioStates[key] = !ioStates[key]; localStorage.setItem('io_states', JSON.stringify(ioStates)); renderIO(); };
        box.innerHTML = `<div>${key}</div><div class="io-status" style="color:${ioStates[key]?'#55ff55':'#ff5555'}">${ioStates[key]?'ON':'OFF'}</div>`;
        grid.appendChild(box);
    });
}


// ── JS: Time & Date ───────────────────────────────────────────

function openTime() {
    hideApp('settings-app');
    document.getElementById('time-app').style.display = 'block';
    document.getElementById('country-select').value = timeZone;
}

function saveTime() {
    timeZone = document.getElementById('country-select').value;
    localStorage.setItem('ember_tz', timeZone);
    hideApp('time-app');
    document.getElementById('settings-app').style.display = 'block';
}

// Runs every second — formats time and date using the
// currently selected timezone
function updateTime(){
  const n = new Date();
  const options = { timeZone: timeZone, hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' };
  const dateOptions = { timeZone: timeZone, day: '2-digit', month: '2-digit', year: 'numeric' };
  document.getElementById("clock").innerText = n.toLocaleTimeString("tr-TR", options);
  document.getElementById("date").innerText = n.toLocaleDateString("tr-TR", dateOptions);
}


// ── JS: Wi-Fi Scanner ─────────────────────────────────────────

function openWifiApp() {
    hideApp('settings-app');
    const wifiApp = document.getElementById('wifi-app');
    wifiApp.style.display = 'block';
    scanWifi();
}

// Asks the ESP32 to scan nearby networks and renders them
// sorted by signal strength (strongest first)
function scanWifi() {
    const list = document.getElementById('wifi-list');
    list.innerHTML = "Scanning...";
    fetch('/wifiscan').then(r=>r.json()).then(data => {
        list.innerHTML = "";
        data.sort((a,b) => b.rssi - a.rssi).forEach(w => {
            let color = w.rssi > -60 ? '#55ff55' : (w.rssi > -80 ? '#ffff55' : '#ff5555');
            let item = document.createElement('div');
            item.className = "wifi-item";
            item.onclick = () => showWifiPopup(w);
            item.innerHTML = `<span class="wifi-name">${w.ssid}</span>
                              <span class="wifi-dbm" style="color:${color}">${w.rssi} dBm</span>`;
            list.appendChild(item);
        });
    });
}

// Shows SSID, BSSID, channel, etc. when you tap a network
function showWifiPopup(w) {
    const p = document.getElementById('wifi-details-popup');
    const c = document.getElementById('wifi-popup-content');
    c.innerHTML = `
        <div class="wifi-detail-line"><b>SSID:</b> ${w.ssid}</div>
        <div class="wifi-detail-line"><b>BSSID:</b> ${w.bssid}</div>
        <div class="wifi-detail-line"><b>RSSI:</b> ${w.rssi} dBm</div>
        <div class="wifi-detail-line"><b>Channel:</b> ${w.channel}</div>
        <div class="wifi-detail-line"><b>Enc:</b> ${w.enc}</div>
    `;
    p.style.display = 'block';
}

function closeWifiPopup() {
    document.getElementById('wifi-details-popup').style.display = 'none';
}


// ── JS: Hotspot & ESP-NOW ─────────────────────────────────────

function openHotspotApp() {
    hideApp('settings-app');
    document.getElementById('hotspot-app').style.display = 'block';
}

// ESP-NOW isn't implemented yet — just show a "not compatible" notice
function showEspNow() {
    document.getElementById('espnow-popup').style.display = 'block';
}

function hideEspNow() {
    document.getElementById('espnow-popup').style.display = 'none';
}


// ── JS: Notepad ───────────────────────────────────────────────

// Auto-saves the current file to localStorage on every keystroke.
// Creates a new file ID if one doesn't exist yet.
function npAutoSave() {
    const txt = document.getElementById('np-editor').value;
    if(!curFile) curFile = "file_" + Date.now();
    let store = JSON.parse(localStorage.getItem('ember_files') || '{}');
    let title = txt.substring(0,20).trim() || "unknown_" + Object.keys(store).length;
    store[curFile] = { title: title, content: txt };
    localStorage.setItem('ember_files', JSON.stringify(store));
}

// Start a fresh note (doesn't delete anything)
function npNew() {
    document.getElementById('np-editor').value = "";
    curFile = "file_" + Date.now();
    document.getElementById('np-file-list').style.display = "none";
}

// Toggle the saved files list panel
function npToggleList() {
    const list = document.getElementById('np-file-list');
    if(list.style.display === "block") { list.style.display = "none"; return; }
    list.innerHTML = "";
    let store = JSON.parse(localStorage.getItem('ember_files') || '{}');
    Object.keys(store).forEach(id => {
        let item = document.createElement('div');
        item.className = "np-file-item";
        item.innerHTML = `<span onclick="npLoad('${id}')" style="flex:1">${store[id].title}</span>
                          <span class="np-del-btn" onclick="npConfirmDel('${id}')">🗑️</span>`;
        list.appendChild(item);
    });
    list.style.display = "block";
}

// Load a saved file into the editor
function npLoad(id) {
    let store = JSON.parse(localStorage.getItem('ember_files') || '{}');
    curFile = id;
    document.getElementById('np-editor').value = store[id].content;
    document.getElementById('np-file-list').style.display = "none";
}

// Ask for confirmation before deleting — nobody likes losing notes
function npConfirmDel(id) {
    const pop = document.getElementById('np-popup');
    pop.style.display = "block";
    document.getElementById('pop-confirm').onclick = () => {
        let store = JSON.parse(localStorage.getItem('ember_files') || '{}');
        delete store[id];
        localStorage.setItem('ember_files', JSON.stringify(store));
        pop.style.display = "none";
        npToggleList(); npToggleList();
    };
}


// ── JS: Window / App Management ──────────────────────────────

// Each "app" is just a div that gets shown/hidden.
// These helpers keep the open/close logic tidy.

function openNotepad() {
    const n = document.getElementById("notepad-app");
    if(n.style.display === "block") { n.style.display = "none"; }
    else { closeAll(); n.style.display = "block"; }
}

function openSettings() {
    const s = document.getElementById("settings-app");
    if(s.style.display === "block") { s.style.display = "none"; }
    else { closeAll(); s.style.display = "block"; backToSettings(); }
}

// Close everything and stop all active sensor polling
function closeAll(){ document.querySelectorAll('#terminal, #info-app-panel, #themes-app-panel, #menu-panel, #notepad-app, #settings-app, #wifi-app, #hotspot-app, #espnow-popup, #io-app, #time-app, #read-popup, #dht-popup, #ldr-popup, #ultrasonic-popup, #ir-popup, #servo-popup').forEach(w=>w.style.display="none"); if(readInterval) clearInterval(readInterval); if(dhtInterval) clearInterval(dhtInterval); if(ldrInterval) clearInterval(ldrInterval); if(ultrasonicInterval) clearInterval(ultrasonicInterval); if(irInterval) clearInterval(irInterval); servoActivePin = null; }

function hideApp(id){ document.getElementById(id).style.display = "none"; }

// Clears terminal history and closes it
function killTerminal(e){ e.stopPropagation(); termLines = [PROMPT]; hideApp('terminal'); }

function openMenu(){
  const m = document.getElementById("menu-panel");
  if(m.style.display === "flex") { m.style.display = "none"; }
  else { closeAll(); m.style.display = "flex"; }
}

function openThemes(){
  const t = document.getElementById("themes-app-panel");
  if(t.style.display === "block") { t.style.display = "none"; }
  else { closeAll(); t.style.display = "block"; }
}

// Fetches chip info (model, MAC, heap, CPU freq) from the ESP32
function openInfo(){
  const i = document.getElementById("info-app-panel");
  if(i.style.display === "block") { i.style.display = "none"; return; }
  closeAll(); i.style.display = "block";
  fetch('/sysinfo').then(r=>r.json()).then(d=>{
    document.getElementById("v-main-info").innerHTML = `
      <div class="info-v-item"><span class="info-v-label">CHIP_MODEL</span><span class="info-v-data">${d.chip}</span></div>
      <div class="info-v-item"><span class="info-v-label">MAC_ADDRESS</span><span class="info-v-data">${d.mac}</span></div>
      <div class="info-v-item"><span class="info-v-label">FREE_HEAP</span><span class="info-v-data">${d.heap} KB</span></div>
    `;
    document.getElementById("extra-info-v").innerHTML = `
      <div class="info-v-item"><span class="info-v-label">CPU_FREQ</span><span class="info-v-data">${d.cpu} MHz</span></div>
      <div class="info-v-item"><span class="info-v-label">CORES</span><span class="info-v-data">${d.cores}</span></div>
      <div class="info-v-item"><span class="info-v-label">SDK_VER</span><span class="info-v-data">${d.sdk}</span></div>
    `;
  });
}

// Expand / collapse the extra chip details in the info panel
function toggleVInfo(){
  const ex = document.getElementById("extra-info-v");
  const btn = document.getElementById("v-toggle");
  const open = ex.style.display === "flex";
  ex.style.display = open ? "none" : "flex";
  btn.innerText = open ? "➕" : "➖";
}

// Opens the terminal — positioned differently from other apps
// (fills the main area instead of centering as a modal)
function openTerminal(){
  const t = document.getElementById("terminal");
  if(t.style.display === "block") { t.style.display = "none"; }
  else { closeAll(); t.style.display = "block"; renderTerm(); }
}

// Redraws the terminal content and scrolls to the bottom
function renderTerm(){
  const t = document.getElementById("term-content");
  t.textContent = termLines.join('\n') + "█";
  const termDiv = document.getElementById("terminal");
  termDiv.scrollTop = termDiv.scrollHeight;
}


// ── JS: Custom Cursor ────────────────────────────────────────

// The OS hides the real cursor and draws its own dot instead
document.addEventListener("mousemove", e => {
  const c = document.getElementById("cursor");
  c.style.left = e.clientX + "px"; c.style.top = e.clientY + "px";
});


// ── JS: Sensor Popup Helpers ─────────────────────────────────

// Each sensor type gets a matching open/close pair.
// The "start" functions kick off a polling interval;
// the "close" functions cancel it.

function closeReadPopup() {
    document.getElementById('read-popup').style.display = 'none';
    if(readInterval) clearInterval(readInterval);
}

// Poll digital pin state every 100ms
function startRead(pin) {
    document.getElementById('read-title').innerText = "READ GPIO " + pin;
    document.getElementById('read-popup').style.display = 'block';
    if(readInterval) clearInterval(readInterval);
    readInterval = setInterval(() => {
        fetch(`/gpioread?pin=${pin}`).then(r=>r.text()).then(val => {
            document.getElementById('read-value').innerText = val;
        });
    }, 100);
}

function closeDhtPopup() {
    document.getElementById('dht-popup').style.display = 'none';
    if(dhtInterval) clearInterval(dhtInterval);
}

// Poll DHT11 once per second — faster than that and the sensor
// doesn't have time to take a new reading anyway
function startDht(pin) {
    document.getElementById('dht-title').innerText = "READING GPIO " + pin + " DHT11";
    document.getElementById('dht-popup').style.display = 'block';
    if(dhtInterval) clearInterval(dhtInterval);
    dhtInterval = setInterval(() => {
        fetch(`/dhtread?pin=${pin}`).then(r=>r.json()).then(d => {
            if(d.error) {
                document.getElementById('dht-temp').innerText = "--";
                document.getElementById('dht-hum').innerText = "--";
                document.getElementById('dht-fahr').innerText = "-- °F";
            } else {
                const fahr = ((d.temp * 9/5) + 32).toFixed(1);
                document.getElementById('dht-temp').innerText = d.temp.toFixed(1);
                document.getElementById('dht-hum').innerText = d.hum.toFixed(1);
                document.getElementById('dht-fahr').innerText = fahr + " °F";
            }
        });
    }, 1000);
}

function closeLdrPopup() {
    document.getElementById('ldr-popup').style.display = 'none';
    if(ldrInterval) clearInterval(ldrInterval);
}

// Poll LDR light level once per second
function startLdr(pin) {
    document.getElementById('ldr-title').innerText = "READING GPIO " + pin + " LDR SENSOR:";
    document.getElementById('ldr-popup').style.display = 'block';
    if(ldrInterval) clearInterval(ldrInterval);
    ldrInterval = setInterval(() => {
        fetch(`/ldrread?pin=${pin}`).then(r=>r.json()).then(d => {
            if(d.error) {
                document.getElementById('ldr-value').innerText = "--";
            } else {
                document.getElementById('ldr-value').innerText = d.percent;
            }
        });
    }, 1000);
}

function closeUltrasonicPopup() {
    document.getElementById('ultrasonic-popup').style.display = 'none';
    if(ultrasonicInterval) clearInterval(ultrasonicInterval);
}

// Poll ultrasonic distance twice per second — shows both cm and inches
function startUltrasonic(sensorName) {
    document.getElementById('ultrasonic-title').innerText = "READING " + sensorName.toUpperCase();
    document.getElementById('ultrasonic-popup').style.display = 'block';
    if(ultrasonicInterval) clearInterval(ultrasonicInterval);
    ultrasonicInterval = setInterval(() => {
        fetch(`/ultrasonicread?sensor=${sensorName}`).then(r=>r.json()).then(d => {
            if(d.error) {
                document.getElementById('ultrasonic-value').innerText = "--";
                document.getElementById('ultrasonic-inch').innerText = "-- inch";
            } else {
                const inch = (d.cm / 2.54).toFixed(1);
                document.getElementById('ultrasonic-value').innerText = d.cm.toFixed(1);
                document.getElementById('ultrasonic-inch').innerText = inch + " inch";
            }
        });
    }, 500);
}

function closeIrPopup() {
    document.getElementById('ir-popup').style.display = 'none';
    if(irInterval) clearInterval(irInterval);
}

// Poll IR receiver 5x/sec — the ESP32 caches the last code
// so we don't miss signals that arrive between polls
function startIr(pin) {
    document.getElementById('ir-title').innerText = "READING GPIO " + pin + " IR SENSOR:";
    document.getElementById('ir-value').innerText = "--";
    document.getElementById('ir-popup').style.display = 'block';
    if(irInterval) clearInterval(irInterval);
    irInterval = setInterval(() => {
        fetch(`/irread?pin=${pin}`).then(r=>r.json()).then(d => {
            if(!d.error && d.code) {
                document.getElementById('ir-value').innerText = d.code;
            }
        });
    }, 200);
}

function closeServoPopup() {
    document.getElementById('servo-popup').style.display = 'none';
    servoActivePin = null;
}

// Opens the servo slider and moves to 90° as a starting position
function startServoVisual(pin) {
    servoActivePin = pin;
    document.getElementById('servo-slider').value = 90;
    document.getElementById('servo-degree').innerText = "90";
    document.getElementById('servo-popup').style.display = 'block';
    fetch(`/servorun?pin=${pin}&degree=90`);
}

// Called on every slider move — sends the new angle to the ESP32
function onServoSlide(val) {
    document.getElementById('servo-degree').innerText = val;
    if(servoActivePin !== null) {
        fetch(`/servorun?pin=${servoActivePin}&degree=${val}`);
    }
}


// ── JS: Terminal Command Processor ───────────────────────────
//
// This is the heart of EmberOS interaction. Commands follow
// a structured syntax like:
//   set/ GPIO 5 output
//   run/ GPIO 5 HIGH
//   read/ GPIO 4 DHT11
//   code/ GPIO 5 HIGH, delay 500, GPIO 5 LOW, loop
//
// The "code/" prefix triggers the script executor —
// a comma-separated sequence that runs on the ESP32.

async function processCommand(cmd) {
    const raw = cmd.replace(PROMPT, "").trim();

    // ── code/ Script Submission ──
    // Validates the script locally before sending it to the ESP32.
    // Catches syntax issues like double commas, unknown commands,
    // or invalid note names before wasting a round trip.
    if(raw.startsWith("code/")) {
        const script = raw.substring(5).trim();
        if(!script) return "Code Error: Not Finished.";
        // Comma validation: commas at wrong places
        if(script.includes(",,") || script.startsWith(",") || script.endsWith(",")) return "Error Code: Comma.";
        const parts = script.split(',');
        let valid = true;
        // Block validation: check each block is a known command
        const validNotes = ["C","D","E","F","G","A","H"];
        for(let i = 0; i < parts.length; i++) {
            const block = parts[i].trim();
            if(block === "") { valid = false; break; }
            const words = block.split(/\s+/);
            // loop block
            if(words[0].toLowerCase() === "loop") { if(words.length !== 1) { valid = false; break; } continue; }
            // delay block
            if(words[0].toLowerCase() === "delay") { if(words.length !== 2 || isNaN(words[1])) { valid = false; break; } continue; }
            // GPIO blocks
            if(words[0].toUpperCase() === "GPIO") {
                if(words.length < 3 || isNaN(words[1])) { valid = false; break; }
                const cmd3 = words[2].toUpperCase();
                if(cmd3 === "HIGH" && words.length === 3) continue;
                if(cmd3 === "LOW" && words.length === 3) continue;
                if(cmd3 === "BUZZER" && words.length === 4 && words[3].toUpperCase() === "STOP") continue;
                if(cmd3 === "BUZZER" && words.length === 5 && words[3].toLowerCase() === "tone" && validNotes.includes(words[4].toUpperCase())) continue;
                valid = false; break;
            }
            valid = false; break;
        }
        if(!valid) return "Error Code: Code Invalid.";
        // Send script to ESP32 for execution
        const r = await fetch(`/execute?script=${encodeURIComponent(script)}`);
        const resp = await r.text();
        if(resp.startsWith("Error")) return resp;
        return "Code Succesful!";
    }

    const parts = raw.split(/\s+/);
    const action = parts[0] ? parts[0].toLowerCase() : "";

    // ── /clear — wipe terminal history ──
    if (action === "/clear") {
        termLines = [];
        return "";
    }

    // ── /show GPIO — list all active pins ──
    if (action === "/show" && parts[1] && parts[1].toUpperCase() === "GPIO") {
        const r = await fetch('/gpiolist');
        return await r.text();
    }

    // ── read/ GPIO X — digital read with live popup ──
    if (action === "read/" && parts.length === 3) {
        const pinType = parts[1].toLowerCase();
        const pinNum = parts[2];
        if (pinType === "gpio" && !isNaN(pinNum)) {
            startRead(pinNum);
            return `Reading GPIO ${pinNum}.`;
        }
    }

    // ── read/ GPIO X DHT11 — temperature + humidity ──
    // read/ GPIO X DHT11
    if (action === "read/" && parts.length === 4 && parts[1].toUpperCase() === "GPIO" && parts[3].toUpperCase() === "DHT11") {
        const pinNum = parts[2];
        if (!isNaN(pinNum)) {
            const r = await fetch(`/dhtcheck?pin=${pinNum}`);
            const resp = await r.text();
            if (resp === "NOT_INPUT") return "GPIO is not INPUT!";
            if (resp === "NOT_DHT") return "Command not Found.";
            startDht(pinNum);
            return `Reading GPIO ${pinNum} as DHT11 sensor.`;
        }
    }

    // ── read/ GPIO X LDR — light level ──
    // read/ GPIO X LDR
    if (action === "read/" && parts.length === 4 && parts[1].toUpperCase() === "GPIO" && parts[3].toUpperCase() === "LDR") {
        const pinNum = parts[2];
        if (!isNaN(pinNum)) {
            const r = await fetch(`/ldrcheck?pin=${pinNum}`);
            const resp = await r.text();
            if (resp === "NOT_INPUT") return "GPIO " + pinNum + " is not INPUT!";
            if (resp === "NOT_LDR") return "Command not Found.";
            startLdr(pinNum);
            return `Reading GPIO ${pinNum} as LDR sensor.`;
        }
    }

    // ── read/ GPIO ultrasonicN — distance sensor ──
    // read/ GPIO X ultrasonicN
    if (action === "read/" && parts.length === 3 && parts[1].toUpperCase() === "GPIO") {
        // check if third part is a ultrasonic sensor name
        if (parts[2].toLowerCase().startsWith("ultrasonic")) {
            const sensorName = parts[2].toLowerCase();
            const r = await fetch(`/ultrasoniccheck?sensor=${sensorName}`);
            const resp = await r.text();
            if (resp === "NOT_READY") return "Command not Found.";
            startUltrasonic(sensorName);
            return `Reading ${sensorName}.`;
        }
    }

    // ── read/ GPIO X IR — infrared receiver ──
    // read/ GPIO X IR
    if (action === "read/" && parts.length === 4 && parts[1].toUpperCase() === "GPIO" && parts[3].toUpperCase() === "IR") {
        const pinNum = parts[2];
        if (!isNaN(pinNum)) {
            const r = await fetch(`/ircheck?pin=${pinNum}`);
            const resp = await r.text();
            if (resp === "NOT_INPUT") return "GPIO is not INPUT!";
            if (resp === "NOT_IR") return "Command not Found.";
            startIr(pinNum);
            return `Reading GPIO ${pinNum} as IR sensor.`;
        }
    }

    // ── set/ i2c address — change LCD I2C address ──
    // set/ i2c adress 0xAA
    if (action === "set/" && parts.length === 4 && parts[1].toLowerCase() === "i2c" && parts[2].toLowerCase() === "adress") {
        const r = await fetch(`/i2cadress?addr=${parts[3]}`);
        return await r.text();
    }

    // ── set/ GPIO X i2c SDA/SCL — assign I2C pins ──
    // set/ GPIO X i2c SDA/SCL/SCK
    if (action === "set/" && parts.length === 5 && parts[1].toUpperCase() === "GPIO" && parts[3].toLowerCase() === "i2c") {
        const pinNum = parts[2];
        const role = parts[4].toUpperCase();
        if (!isNaN(pinNum) && (role === "SDA" || role === "SCL" || role === "SCK")) {
            const r = await fetch(`/gpio?pin=${pinNum}&mode=i2c&role=${role}`);
            return await r.text();
        }
    }

    // ── set/ GPIO i2c LCD — initialize LCD over I2C ──
    // set/ GPIO i2c LCD
    if (action === "set/" && parts.length === 4 && parts[1].toUpperCase() === "GPIO" && parts[2].toLowerCase() === "i2c" && parts[3].toUpperCase() === "LCD") {
        const r = await fetch(`/gpio?pin=0&mode=i2clcd`);
        return await r.text();
    }

    // ── write/ LCD "TEXT" — print text on the LCD ──
    // Write/ LCD "TEXT"
    if (action === "write/" && parts[1] && parts[1].toUpperCase() === "LCD") {
        const rawCmd = raw.substring(raw.indexOf('"'));
        if (!rawCmd.startsWith('"') || !rawCmd.endsWith('"') || rawCmd.length < 2) return "Command Not Found.";
        const text = rawCmd.slice(1, -1);
        const r = await fetch(`/lcdwrite?text=${encodeURIComponent(text)}`);
        return await r.text();
    }

    // ── draw/ LCD smiley — display a custom char ──
    // Draw/ LCD smiley
    if (action === "draw/" && parts.length === 3 && parts[1].toUpperCase() === "LCD" && parts[2].toLowerCase() === "smiley") {
        const r = await fetch(`/lcddraw?shape=smiley`);
        return await r.text();
    }

    // ── erase/ LCD — clear the display ──
    // Erase/ LCD
    if (action === "erase/" && parts.length === 2 && parts[1].toUpperCase() === "LCD") {
        const r = await fetch(`/lcderase`);
        return await r.text();
    }

    // ── set/ GPIO X <mode> — configure a pin ──
    if (action === "set/" && parts.length === 4) {
        const pinType = parts[1].toLowerCase();
        const pinNum = parts[2];
        const mode = parts[3].toLowerCase();
        
        if (pinType === "gpio" && !isNaN(pinNum)) {
            if (mode === "output" && ioStates["Output"]) {
                await fetch(`/gpio?pin=${pinNum}&mode=out`);
                return `Set GPIO ${pinNum} to OUTPUT.`;
            } else if (mode === "input" && ioStates["Input"]) {
                await fetch(`/gpio?pin=${pinNum}&mode=in`);
                return `Set GPIO ${pinNum} to INPUT.`;
            } else if (mode === "buzzer") {
                const r = await fetch(`/gpio?pin=${pinNum}&mode=buzzer`);
                return await r.text();
            } else if (mode === "dht11") {
                const r = await fetch(`/gpio?pin=${pinNum}&mode=dht11`);
                return await r.text();
            } else if (mode === "ldr") {
                const r = await fetch(`/gpio?pin=${pinNum}&mode=ldr`);
                return await r.text();
            } else if (mode === "ir") {
                const r = await fetch(`/gpio?pin=${pinNum}&mode=ir`);
                return await r.text();
            } else if (mode === "servo") {
                const r = await fetch(`/gpio?pin=${pinNum}&mode=servo`);
                return await r.text();
            }
        }
    }
    // ── set/ GPIO X ultrasonicN TRIG/ECHO — assign ultrasonic pins ──
    // set/ GPIO X ultrasonicN TRIG/ECHO
    else if (action === "set/" && parts.length === 5 && parts[1].toUpperCase() === "GPIO") {
        const pinNum = parts[2];
        const sensorName = parts[3].toLowerCase();
        const role = parts[4].toUpperCase();
        if (!isNaN(pinNum) && sensorName.startsWith("ultrasonic") && (role === "TRIG" || role === "ECHO")) {
            const r = await fetch(`/gpio?pin=${pinNum}&mode=ultrasonic&sensor=${sensorName}&role=${role}`);
            return await r.text();
        }
    }
    // ── unset/ GPIO X — release a pin back to default ──
    else if (action === "unset/" && parts.length === 3) {
        const pinType = parts[1].toLowerCase();
        const pinNum = parts[2];
        if (pinType === "gpio" && !isNaN(pinNum)) {
            await fetch(`/gpio?pin=${pinNum}&mode=unset`);
            return `Set GPIO ${pinNum} to default.`;
        }
    }
    // ── run/ GPIO X <state> — control an output pin ──
    else if (action === "run/" && parts.length >= 4) {
        const pinType = parts[1].toLowerCase();
        const pinNum = parts[2];
        const state = parts[3].toUpperCase();
        
        if (pinType === "gpio" && !isNaN(pinNum)) {
            // run/ GPIO X servo DDD or STOP or visual
            if (state === "SERVO" && parts.length === 5) {
                const servoArg = parts[4].toUpperCase();
                if (servoArg === "STOP") {
                    const r = await fetch(`/servorun?pin=${pinNum}&degree=stop`);
                    return await r.text();
                } else if (servoArg === "VISUAL") {
                    const r = await fetch(`/servocheck?pin=${pinNum}`);
                    const resp = await r.text();
                    if (resp === "NOT_OUTPUT") return "GPIO is not OUTPUT!";
                    if (resp === "NOT_SERVO") return "Command not Found.";
                    startServoVisual(pinNum);
                    return `Control Servo Motor on GPIO ${pinNum}.`;
                } else if (!isNaN(servoArg)) {
                    const deg = parseInt(servoArg);
                    if (deg < 0 || deg > 180) return "Command not Found.";
                    const r = await fetch(`/servorun?pin=${pinNum}&degree=${deg}`);
                    return await r.text();
                }
            }

            if(state === "BUZZER" && parts.length >= 6 && parts[4].toLowerCase() === "tone") {
                const note = parts[5].toUpperCase();
                const r = await fetch(`/gpriorun?pin=${pinNum}&state=TONE&val=${note}`);
                return await r.text();
            }
            if(state === "BUZZER" && parts[4].toUpperCase() === "STOP") {
                const r = await fetch(`/gpriorun?pin=${pinNum}&state=LOW`);
                return await r.text();
            }
            
            const r = await fetch(`/gpriorun?pin=${pinNum}&state=${state}`);
            return await r.text();
        }
    }
    
    return "Command not found.";
}


// ── JS: Keyboard Handler ──────────────────────────────────────
// Only fires when the terminal is open.
// Enter → process the command; Backspace → delete last char;
// anything else → append to the current line.
document.addEventListener("keydown", async e => {
  if(document.getElementById("terminal").style.display !== "block") return;
  if(e.key === "Enter"){ 
    let currentCmd = termLines[termLines.length-1];
    let response = await processCommand(currentCmd);
    
    if (response === "") {
    } else {
        termLines.push(response);
    }
    
    termLines.push(PROMPT); 
    renderTerm(); 
  }
  else if(e.key === "Backspace"){
    let last = termLines[termLines.length-1];
    if(last.length > PROMPT.length){ termLines[termLines.length-1] = last.slice(0, -1); renderTerm(); }
  } else if(e.key.length === 1){ termLines[termLines.length-1] += e.key; renderTerm(); }
});
</script>
</body>
</html>
)rawliteral";


// ── C++: System Info Endpoint (/sysinfo) ─────────────────────
// Returns chip model, MAC, free heap, CPU speed, core count,
// and SDK version as JSON. Used by the Info panel in the UI.
void handleInfo() {
  String json = "{";
  json += "\"chip\":\"ESP32\",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  json += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"cpu\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"cores\":" + String(ESP.getChipCores()) + ",";
  json += "\"sdk\":\"" + String(ESP.getSdkVersion()) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}


// ── C++: Wi-Fi Scanner Endpoint (/wifiscan) ──────────────────
// Scans for nearby 2.4GHz networks and returns them as a JSON
// array with SSID, RSSI, BSSID, channel, and encryption type.
void handleWifiScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; ++i) {
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"bssid\":\"" + WiFi.BSSIDstr(i) + "\",";
    json += "\"channel\":" + String(WiFi.channel(i)) + ",";
    json += "\"enc\":\"" + String(WiFi.encryptionType(i)) + "\"}";
    if (i < n - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
  WiFi.scanDelete();
}


// ── C++: Hotspot Config Endpoint (/getconfig) ────────────────
// Just echoes back the hardcoded SSID + password so the
// Hotspot Config screen can display them.
void handleGetConfig() {
  String json = "{";
  json += "\"ssid\":\"" + String(ssid) + "\",";
  json += "\"pass\":\"" + String(password) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}


// ── C++: LCD Initialization Helper ───────────────────────────
// Tears down any existing LCD object and creates a fresh one.
// Called both on first setup and when the I2C address changes.
void initLCD() {
  if(lcd != nullptr) {
    delete lcd;
    lcd = nullptr;
  }
  lcd = new LiquidCrystal_I2C(lcdAddress, 16, 2);
  lcd->init();
  lcd->backlight();
  lcdReady = true;
}


// ── C++: GPIO Config Endpoint (/gpio) ────────────────────────
// The main pin management handler. Accepts a pin number and a
// mode string, then configures the pin accordingly. Handles:
//   out, in, buzzer, dht11, ldr, ir, servo,
//   i2c (SDA/SCL), i2clcd, ultrasonic (TRIG/ECHO), unset
void handleGPIO() {
  if (server.hasArg("pin") && server.hasArg("mode")) {
    int pin = server.arg("pin").toInt();
    String mode = server.arg("mode");
    if (mode == "out") {
      pinMode(pin, OUTPUT);
      activePins[pin] = "OUTPUT";
      buzzerPins[pin] = false;
    } else if (mode == "in") {
      pinMode(pin, INPUT);
      activePins[pin] = "INPUT";
      buzzerPins[pin] = false;
    } else if (mode == "buzzer") {
      if(activePins[pin] == "OUTPUT") {
        buzzerPins[pin] = true;
        server.send(200, "text/plain", "GPIO " + String(pin) + " set as Buzzer.");
      } else {
        server.send(200, "text/plain", "GPIO is not OUTPUT.");
      }
      return;
    } else if (mode == "dht11") {
      // DHT11 requires INPUT first
      if(activePins[pin] == "INPUT") {
        if(dhtPins.count(pin) && dhtPins[pin] != nullptr) {
          delete dhtPins[pin];
        }
        dhtPins[pin] = new DHT(pin, DHT11);
        dhtPins[pin]->begin();
        activePins[pin] = "DHT11";
        server.send(200, "text/plain", "Set GPIO " + String(pin) + " as DHT11 sensor.");
      } else {
        server.send(200, "text/plain", "GPIO is not INPUT!");
      }
      return;
    } else if (mode == "ldr") {
      // LDR requires INPUT first
      if(activePins[pin] == "INPUT") {
        ldrPins[pin] = true;
        activePins[pin] = "LDR";
        server.send(200, "text/plain", "Set GPIO " + String(pin) + " as LDR sensor.");
      } else {
        server.send(200, "text/plain", "GPIO is not INPUT!");
      }
      return;
    } else if (mode == "ir") {
      // IR requires INPUT first
      if(activePins[pin] == "INPUT") {
        if(irPins.count(pin) && irPins[pin] != nullptr) {
          irPins[pin]->disableIRIn();
          delete irPins[pin];
        }
        irPins[pin] = new IRrecv(pin);
        irPins[pin]->enableIRIn();
        irLastCode[pin] = "";
        activePins[pin] = "IR";
        server.send(200, "text/plain", "Set GPIO " + String(pin) + " as IR sensor.");
      } else {
        server.send(200, "text/plain", "GPIO is not INPUT!");
      }
      return;
    } else if (mode == "servo") {
      // Servo requires OUTPUT first
      if(activePins[pin] == "OUTPUT") {
        if(servoPins.count(pin) && servoPins[pin] != nullptr) {
          servoPins[pin]->detach();
          delete servoPins[pin];
        }
        servoPins[pin] = new Servo();
        servoPins[pin]->attach(pin);
        activePins[pin] = "SERVO";
        server.send(200, "text/plain", "Set GPIO " + String(pin) + " as Servo motor.");
      } else {
        server.send(200, "text/plain", "GPIO is not OUTPUT!");
      }
      return;
    } else if (mode == "i2c") {
      // I2C pin assignment: SDA or SCL/SCK
      if(server.hasArg("role")) {
        String role = server.arg("role");
        if(activePins[pin] == "OUTPUT") {
          if(role == "SDA") {
            i2cSDAPin = pin;
            activePins[pin] = "I2C_SDA";
            server.send(200, "text/plain", "Set GPIO " + String(pin) + " as i2c SDA.");
          } else if(role == "SCL" || role == "SCK") {
            i2cSCLPin = pin;
            activePins[pin] = "I2C_SCL";
            server.send(200, "text/plain", "Set GPIO " + String(pin) + " as i2c " + role + ".");
          } else {
            server.send(200, "text/plain", "Command not Found.");
          }
        } else {
          server.send(200, "text/plain", "GPIO is not OUTPUT!");
        }
      } else {
        server.send(200, "text/plain", "Command not Found.");
      }
      return;
    } else if (mode == "i2clcd") {
      // I2C LCD requires both SDA and SCL to be assigned first
      if(i2cSDAPin == -1 || i2cSCLPin == -1) {
        server.send(200, "text/plain", "GPIO is not OUTPUT!");
        return;
      }
      Wire.begin(i2cSDAPin, i2cSCLPin);
      initLCD();
      server.send(200, "text/plain", "Set GPIO i2c as LCD Display.");
      return;
    } else if (mode == "ultrasonic") {
      // Ultrasonic: TRIG needs OUTPUT, ECHO needs INPUT
      if(server.hasArg("sensor") && server.hasArg("role")) {
        String sensorName = server.arg("sensor");
        String role = server.arg("role");
        if(role == "TRIG") {
          if(activePins[pin] == "OUTPUT") {
            ultrasonicTrig[sensorName] = pin;
            activePins[pin] = "TRIG_" + sensorName;
            server.send(200, "text/plain", "Set GPIO " + String(pin) + " as " + sensorName + " TRIG.");
          } else {
            server.send(200, "text/plain", "GPIO is not OUTPUT!");
          }
        } else if(role == "ECHO") {
          if(activePins[pin] == "INPUT") {
            ultrasonicEcho[sensorName] = pin;
            activePins[pin] = "ECHO_" + sensorName;
            server.send(200, "text/plain", "Set GPIO " + String(pin) + " as " + sensorName + " ECHO.");
          } else {
            server.send(200, "text/plain", "GPIO is not INPUT!");
          }
        } else {
          server.send(200, "text/plain", "Command not Found.");
        }
      } else {
        server.send(200, "text/plain", "Command not Found.");
      }
      return;
    } else if (mode == "unset") {
      pinMode(pin, INPUT); 
      activePins.erase(pin);
      buzzerPins.erase(pin);
      ldrPins.erase(pin);
      // Clean up DHT11
      if(dhtPins.count(pin) && dhtPins[pin] != nullptr) {
        delete dhtPins[pin];
        dhtPins.erase(pin);
      }
      // Clean up IR
      if(irPins.count(pin) && irPins[pin] != nullptr) {
        irPins[pin]->disableIRIn();
        delete irPins[pin];
        irPins.erase(pin);
        irLastCode.erase(pin);
      }
      // Clean up Servo
      if(servoPins.count(pin) && servoPins[pin] != nullptr) {
        servoPins[pin]->detach();
        delete servoPins[pin];
        servoPins.erase(pin);
      }
      // Clean up I2C pins
      if(i2cSDAPin == pin) i2cSDAPin = -1;
      if(i2cSCLPin == pin) i2cSCLPin = -1;
      // Clean up Ultrasonic
      for(auto it = ultrasonicTrig.begin(); it != ultrasonicTrig.end(); ) {
        if(it->second == pin) { it = ultrasonicTrig.erase(it); } else { ++it; }
      }
      for(auto it = ultrasonicEcho.begin(); it != ultrasonicEcho.end(); ) {
        if(it->second == pin) { it = ultrasonicEcho.erase(it); } else { ++it; }
      }
      ledcDetach(pin);
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}


// ── C++: GPIO List Endpoint (/gpiolist) ──────────────────────
// Returns a plain-text list of all pins that are currently
// configured, along with their mode. Used by "/show GPIO".
void handleGPIOList() {
  if (activePins.empty()) {
    server.send(200, "text/plain", "No GPIO pins set.");
    return;
  }
  String list = "Active GPIO Pins:\n";
  for (auto const& [pin, mode] : activePins) {
    list += "GPIO " + String(pin) + " -> " + mode + (buzzerPins[pin] ? " (BUZZER)" : "") + "\n";
  }
  server.send(200, "text/plain", list);
}


// ── C++: Buzzer Notes Map ─────────────────────────────────────
// Maps musical note names to frequencies (Hz).
// "H" is used here for the 7th note (B in English notation).
std::map<String, int> notes = {{"C", 262}, {"D", 294}, {"E", 330}, {"F", 349}, {"G", 392}, {"A", 440}, {"H", 494}};


// ── C++: GPIO Run Endpoint (/gpriorun) ───────────────────────
// Controls the state of an output pin: HIGH, LOW, or a buzzer
// tone. Rejects requests on input-only pins.
void handleGPIORun() {
  if (server.hasArg("pin") && server.hasArg("state")) {
    int pin = server.arg("pin").toInt();
    String state = server.arg("state");
    
    if (activePins.find(pin) == activePins.end()) {
      server.send(200, "text/plain", "GPIO not found.");
      return;
    }
    
    if (activePins[pin] == "INPUT") {
      server.send(200, "text/plain", "GPIO is not OUTPUT.");
      return;
    }
    
    if (state == "TONE" && server.hasArg("val")) {
      if(!buzzerPins[pin]) { server.send(200, "text/plain", "Pin is not a buzzer."); return; }
      String n = server.arg("val");
      if(notes.count(n)) {
        ledcAttach(pin, 2000, 10);
        ledcWriteTone(pin, notes[n]);
        server.send(200, "text/plain", "Playing note " + n);
      } else {
        server.send(200, "text/plain", "Invalid note.");
      }
    } else if (state == "HIGH") {
      ledcDetach(pin);
      digitalWrite(pin, HIGH);
      server.send(200, "text/plain", "GPIO " + String(pin) + " is HIGH");
    } else if (state == "LOW") {
      ledcDetach(pin);
      digitalWrite(pin, LOW);
      server.send(200, "text/plain", "GPIO " + String(pin) + " is LOW");
    } else {
      server.send(200, "text/plain", "Command not found.");
    }
  }
}


// ── C++: Digital Read Endpoint (/gpioread) ───────────────────
// Returns 0 or 1 for the current state of a pin.
// The UI polls this at 100ms for the live read popup.
void handleGPIORead() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    server.send(200, "text/plain", String(digitalRead(pin)));
  }
}


// ── C++: DHT11 Endpoints (/dhtread, /dhtcheck) ───────────────

// Returns temperature (°C) and humidity (%) as JSON.
// Sends {error:true} if the sensor isn't responding.
void handleDHTRead() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(dhtPins.count(pin) && dhtPins[pin] != nullptr) {
      float temp = dhtPins[pin]->readTemperature();
      float hum = dhtPins[pin]->readHumidity();
      if(isnan(temp) || isnan(hum)) {
        server.send(200, "application/json", "{\"error\":true}");
      } else {
        String json = "{\"temp\":" + String(temp) + ",\"hum\":" + String(hum) + "}";
        server.send(200, "application/json", json);
      }
    } else {
      server.send(200, "application/json", "{\"error\":true}");
    }
  }
}

// Pre-flight check — confirms the pin is INPUT and set to DHT11
// before the UI starts polling dhtread
void handleDHTCheck() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(activePins.find(pin) == activePins.end() || activePins[pin] == "OUTPUT") {
      server.send(200, "text/plain", "NOT_INPUT");
      return;
    }
    if(activePins[pin] != "DHT11") {
      server.send(200, "text/plain", "NOT_DHT");
      return;
    }
    server.send(200, "text/plain", "OK");
  }
}


// ── C++: LDR Endpoints (/ldrread, /ldrcheck) ─────────────────

// Reads ADC value (0–4095) and converts to a 0–100% light level
void handleLDRRead() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(ldrPins.count(pin) && ldrPins[pin]) {
      int raw = analogRead(pin);
      // ESP32 ADC 12-bit: 0-4095, map to 0-100%
      int percent = map(raw, 0, 4095, 0, 100);
      String json = "{\"percent\":" + String(percent) + "}";
      server.send(200, "application/json", json);
    } else {
      server.send(200, "application/json", "{\"error\":true}");
    }
  }
}

// Pre-flight check for LDR — pin must be INPUT and set to LDR mode
void handleLDRCheck() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(activePins.find(pin) == activePins.end() || activePins[pin] == "OUTPUT") {
      server.send(200, "text/plain", "NOT_INPUT");
      return;
    }
    if(activePins[pin] != "LDR") {
      server.send(200, "text/plain", "NOT_LDR");
      return;
    }
    server.send(200, "text/plain", "OK");
  }
}


// ── C++: Ultrasonic Endpoints (/ultrasonicread, /ultrasoniccheck) ──

// Fires a 10µs TRIG pulse and measures the ECHO duration.
// Converts to cm using the standard speed-of-sound formula.
// Returns {error:true} if no echo within 30ms (out of range).
void handleUltrasonicRead() {
  if (server.hasArg("sensor")) {
    String sensorName = server.arg("sensor");
    if(ultrasonicTrig.count(sensorName) && ultrasonicEcho.count(sensorName)) {
      int trigPin = ultrasonicTrig[sensorName];
      int echoPin = ultrasonicEcho[sensorName];
      // Send pulse
      digitalWrite(trigPin, LOW);
      delayMicroseconds(2);
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);
      // Read echo
      long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout
      float cm = duration * 0.034 / 2.0;
      if(duration == 0 || cm > 400) {
        server.send(200, "application/json", "{\"error\":true}");
      } else {
        String json = "{\"cm\":" + String(cm) + "}";
        server.send(200, "application/json", json);
      }
    } else {
      server.send(200, "application/json", "{\"error\":true}");
    }
  }
}

// Confirms both TRIG and ECHO pins are registered for this sensor
void handleUltrasonicCheck() {
  if (server.hasArg("sensor")) {
    String sensorName = server.arg("sensor");
    if(ultrasonicTrig.count(sensorName) && ultrasonicEcho.count(sensorName)) {
      server.send(200, "text/plain", "OK");
    } else {
      server.send(200, "text/plain", "NOT_READY");
    }
  }
}


// ── C++: IR Receiver Endpoints (/irread, /ircheck) ───────────

// Decodes a received IR signal and returns its hex code.
// If no new signal came in, returns the last known code instead
// so the UI doesn't flash back to "--" between presses.
void handleIRRead() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(irPins.count(pin) && irPins[pin] != nullptr) {
      decode_results results;
      if(irPins[pin]->decode(&results)) {
        String code = "0x" + String(results.value, HEX);
        code.toUpperCase();
        irLastCode[pin] = code;
        irPins[pin]->resume();
        server.send(200, "application/json", "{\"code\":\"" + code + "\"}");
      } else {
        // No new code, send last stored code
        if(irLastCode[pin].length() > 0) {
          server.send(200, "application/json", "{\"code\":\"" + irLastCode[pin] + "\"}");
        } else {
          server.send(200, "application/json", "{\"error\":true}");
        }
      }
    } else {
      server.send(200, "application/json", "{\"error\":true}");
    }
  }
}

// Pre-flight check — pin must be INPUT and set to IR mode
void handleIRCheck() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(activePins.find(pin) == activePins.end() || activePins[pin] == "OUTPUT") {
      server.send(200, "text/plain", "NOT_INPUT");
      return;
    }
    if(activePins[pin] != "IR") {
      server.send(200, "text/plain", "NOT_IR");
      return;
    }
    server.send(200, "text/plain", "OK");
  }
}


// ── C++: Servo Endpoints (/servorun, /servocheck) ────────────

// Moves the servo to a given angle (0–180°), or detaches it
// if "stop" is passed. Re-attaches automatically if needed.
void handleServoRun() {
  if (server.hasArg("pin") && server.hasArg("degree")) {
    int pin = server.arg("pin").toInt();
    String degStr = server.arg("degree");
    if(activePins.find(pin) == activePins.end() || activePins[pin] != "SERVO") {
      server.send(200, "text/plain", "Command not Found.");
      return;
    }
    if(degStr == "stop") {
      servoPins[pin]->detach();
      server.send(200, "text/plain", "The Servo on GPIO " + String(pin) + " has been Stopped.");
      return;
    }
    int deg = degStr.toInt();
    if(deg < 0 || deg > 180) { server.send(200, "text/plain", "Command not Found."); return; }
    if(!servoPins[pin]->attached()) { servoPins[pin]->attach(pin); }
    servoPins[pin]->write(deg);
    server.send(200, "text/plain", "The Servo on GPIO " + String(pin) + " will turn to " + String(deg) + " degrees.");
  }
}

// Pre-flight check — pin must be OUTPUT and set to SERVO mode
void handleServoCheck() {
  if (server.hasArg("pin")) {
    int pin = server.arg("pin").toInt();
    if(activePins.find(pin) == activePins.end() || activePins[pin] == "INPUT") {
      server.send(200, "text/plain", "NOT_OUTPUT");
      return;
    }
    if(activePins[pin] != "SERVO") {
      server.send(200, "text/plain", "NOT_SERVO");
      return;
    }
    server.send(200, "text/plain", "OK");
  }
}


// ── C++: I2C Address Endpoint (/i2cadress) ───────────────────
// Updates the LCD I2C address at runtime. If the LCD is already
// initialized, it gets re-inited with the new address immediately.
void handleI2CAdress() {
  if (server.hasArg("addr")) {
    String addrStr = server.arg("addr");
    uint8_t addr = (uint8_t)strtol(addrStr.c_str(), NULL, 16);
    lcdAddress = addr;
    // If LCD is already initialized, reinitialize with new address
    if(lcdReady) {
      initLCD();
    }
    server.send(200, "text/plain", "Set the i2c Adress to: " + addrStr + ".");
  }
}


// ── C++: LCD Endpoints (/lcdwrite, /lcddraw, /lcderase) ──────

// Prints text on the LCD. Strings over 16 chars wrap to line 2.
void handleLCDWrite() {
  if(!lcdReady || lcd == nullptr) {
    server.send(200, "text/plain", "Command Not Found.");
    return;
  }
  if (server.hasArg("text")) {
    String text = server.arg("text");
    lcd->clear();
    // If text is longer than 16 chars, split across two lines
    if(text.length() <= 16) {
      lcd->setCursor(0, 0);
      lcd->print(text);
    } else {
      lcd->setCursor(0, 0);
      lcd->print(text.substring(0, 16));
      lcd->setCursor(0, 1);
      lcd->print(text.substring(16, 32));
    }
    server.send(200, "text/plain", "Wrote the text: \"" + text + "\" on the LCD Display.");
  }
}

// Draws a custom character on the LCD.
// Currently only "smiley" is supported — two side-by-side faces
// centered on the top row.
void handleLCDDraw() {
  if(!lcdReady || lcd == nullptr) {
    server.send(200, "text/plain", "Command Not Found.");
    return;
  }
  if (server.hasArg("shape")) {
    String shape = server.arg("shape");
    if(shape == "smiley") {
      // Create custom smiley character
      byte smile[8] = {
        0b00000,
        0b01010,
        0b01010,
        0b00000,
        0b10001,
        0b01110,
        0b00000,
        0b00000
      };
      lcd->createChar(0, smile);
      lcd->clear();
      // Place smiley in the center of the 16x2 display (col 7-8, row 0)
      lcd->setCursor(7, 0);
      lcd->write(byte(0));
      lcd->setCursor(8, 0);
      lcd->write(byte(0));
      server.send(200, "text/plain", "Drawn a smiley on the LCD Display.");
    } else {
      server.send(200, "text/plain", "Command Not Found.");
    }
  }
}

// Clears all content from the LCD display
void handleLCDErase() {
  if(!lcdReady || lcd == nullptr) {
    server.send(200, "text/plain", "Command Not Found.");
    return;
  }
  lcd->clear();
  server.send(200, "text/plain", "LCD Display is Erased.");
}


// ── C++: Script Executor Endpoint (/execute) ─────────────────
// Receives a comma-separated script from the browser, validates
// that all referenced GPIO pins are configured, then kicks off
// non-blocking execution via the runScript() state machine.
void handleExecute() {
  if (server.hasArg("script")) {
    String script = server.arg("script");
    Serial.println("Executing Script: " + script);

    // Split script blocks by comma
    scriptBlockCount = 0;
    scriptLoop = false;
    int start = 0;
    for (int i = 0; i <= script.length(); i++) {
      if (i == script.length() || script[i] == ',') {
        String block = script.substring(start, i);
        block.trim();
        if (block.length() > 0 && scriptBlockCount < 50) {
          scriptBlocks[scriptBlockCount++] = block;
        }
        start = i + 1;
      }
    }

    // Check if last block is "loop"
    if (scriptBlockCount > 0 && scriptBlocks[scriptBlockCount - 1].equalsIgnoreCase("loop")) {
      scriptLoop = true;
      scriptBlockCount--; // remove loop block from list
    }

    // Check all GPIO blocks: are the pins set?
    for (int i = 0; i < scriptBlockCount; i++) {
      String block = scriptBlocks[i];
      block.trim();
      if (block.substring(0, 4).equalsIgnoreCase("GPIO")) {
        int spaceIdx = block.indexOf(' ', 5);
        String pinStr = (spaceIdx > 0) ? block.substring(5, spaceIdx) : block.substring(5);
        pinStr.trim();
        int pin = pinStr.toInt();
        if (activePins.find(pin) == activePins.end()) {
          server.send(200, "text/plain", "Error Code: GPIO " + String(pin) + " is Default.");
          return;
        }
      }
    }

    // Start script execution
    scriptIndex = 0;
    scriptRunning = true;
    scriptNextTime = millis();

    server.send(200, "text/plain", "OK");
  }
}


// ── C++: Script Runner (called every loop()) ─────────────────
// Processes one script block per call. Uses millis() instead of
// delay() so the web server stays responsive between blocks.
// When a "delay N" block is hit, scriptNextTime is set into the
// future and nothing runs until that time arrives.
void runScript() {
  if (!scriptRunning) return;
  if (millis() < scriptNextTime) return;

  if (scriptIndex >= scriptBlockCount) {
    if (scriptLoop) {
      scriptIndex = 0; // loop: restart from beginning
    } else {
      scriptRunning = false; // no loop: stop
      return;
    }
  }

  String block = scriptBlocks[scriptIndex];
  block.trim();
  scriptIndex++;

  // delay block
  if (block.substring(0, 5).equalsIgnoreCase("delay")) {
    int spaceIdx = block.indexOf(' ');
    if (spaceIdx > 0) {
      unsigned long ms = block.substring(spaceIdx + 1).toInt();
      scriptNextTime = millis() + ms;
    }
    return;
  }

  // GPIO block
  if (block.substring(0, 4).equalsIgnoreCase("GPIO")) {
    // GPIO X HIGH
    // GPIO X LOW
    // GPIO X BUZZER STOP
    // GPIO X BUZZER TONE Y
    int firstSpace = block.indexOf(' ');
    int secondSpace = block.indexOf(' ', firstSpace + 1);
    int thirdSpace = block.indexOf(' ', secondSpace + 1);
    int fourthSpace = block.indexOf(' ', thirdSpace + 1);

    String pinStr = block.substring(firstSpace + 1, secondSpace);
    pinStr.trim();
    int pin = pinStr.toInt();

    String cmd3 = block.substring(secondSpace + 1, (thirdSpace > 0) ? thirdSpace : block.length());
    cmd3.trim();
    cmd3.toUpperCase();

    if (cmd3 == "HIGH") {
      ledcDetach(pin);
      digitalWrite(pin, HIGH);
    } else if (cmd3 == "LOW") {
      ledcDetach(pin);
      digitalWrite(pin, LOW);
    } else if (cmd3 == "BUZZER") {
      String cmd4 = (thirdSpace > 0) ? block.substring(thirdSpace + 1, (fourthSpace > 0) ? fourthSpace : block.length()) : "";
      cmd4.trim();
      cmd4.toUpperCase();
      if (cmd4 == "STOP") {
        ledcDetach(pin);
        digitalWrite(pin, LOW);
      } else if (cmd4 == "TONE" && fourthSpace > 0) {
        String note = block.substring(fourthSpace + 1);
        note.trim();
        note.toUpperCase();
        if (notes.count(note)) {
          ledcAttach(pin, 2000, 10);
          ledcWriteTone(pin, notes[note]);
        }
      }
    }
  }

  // Move to next block immediately (no delay)
  scriptNextTime = millis();
}


// ── C++: Setup ───────────────────────────────────────────────
// Starts serial, brings up the access point, registers all
// HTTP routes, and starts the web server.
void setup() {
  Serial.begin(115200);
  WiFi.softAP(ssid, password);
  server.on("/", [](){ server.send_P(200, "text/html", PAGE); });
  server.on("/sysinfo", handleInfo);
  server.on("/wifiscan", handleWifiScan);
  server.on("/getconfig", handleGetConfig);
  server.on("/gpio", handleGPIO);
  server.on("/gpiolist", handleGPIOList);
  server.on("/gpriorun", handleGPIORun);
  server.on("/gpioread", handleGPIORead);
  server.on("/dhtread", handleDHTRead); // DHT11 read endpoint
  server.on("/dhtcheck", handleDHTCheck); // DHT11 check endpoint
  server.on("/ldrread", handleLDRRead); // LDR read endpoint
  server.on("/ldrcheck", handleLDRCheck); // LDR check endpoint
  server.on("/ultrasonicread", handleUltrasonicRead); // Ultrasonic read endpoint
  server.on("/ultrasoniccheck", handleUltrasonicCheck); // Ultrasonic check endpoint
  server.on("/irread", handleIRRead); // IR read endpoint
  server.on("/ircheck", handleIRCheck); // IR check endpoint
  server.on("/servorun", handleServoRun); // Servo run endpoint
  server.on("/servocheck", handleServoCheck); // Servo check endpoint
  server.on("/execute", handleExecute); // Script executor endpoint
  server.on("/i2cadress", handleI2CAdress); // I2C address set endpoint
  server.on("/lcdwrite", handleLCDWrite); // LCD write endpoint
  server.on("/lcddraw", handleLCDDraw); // LCD draw endpoint
  server.on("/lcderase", handleLCDErase); // LCD erase endpoint
  server.begin();
}


// ── C++: Main Loop ───────────────────────────────────────────
// Keeps the web server alive and ticks the script executor.
// Everything else is interrupt/event driven.
void loop() {
  server.handleClient();
  runScript(); // script executor runs every loop
}
