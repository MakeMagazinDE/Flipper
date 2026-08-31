// ============================================================
// THE BLUE MERMAID — Portable Pinball Game  v4
// Lolin D32 + DFPlayer Mini
// Main strip: 26 WS2812B  |  Side strips: 35+35 WS2812B
// Total: 96 LEDs
// ============================================================
//
// Libraries (Arduino Library Manager):
//   - FastLED
//   - DFRobotDFPlayerMini
//
// Board: "LOLIN D32" or "ESP32 Dev Module"
//
// SD card (FAT32, files in root):
//   001.mp3  — Seashell chime
//   002.mp3  — Wheel tick slow
//   003.mp3  — Wheel tick medium
//   004.mp3  — Wheel tick fast
//   005.mp3  — Top gate whoosh/splash
//   006.mp3  — Combo fanfare (3+ hits)
//   007.mp3  — Jackpot fanfare (5+ hits)
//
// ============================================================
// WIRING
// ============================================================
//
// POWER — USB power bank:
//   Power bank USB-A 5V  → LED strips VCC (all three)
//   Power bank USB-A GND → LED strips GND (all three)
//   Power bank           → Lolin micro-USB (powers board)
//   Lolin 3V3            → Laser VCC, Light sensor VCC
//
//   Do NOT route LED power through the board!
//   Common GND between power bank, board, and all modules.
//
// MAIN STRIP (26 LEDs, one chain, clockwise from top-right):
//   GPIO 22 → strip DI  (330Ω resistor recommended)
//   Segment order:
//     #1  Upper right corner    3 LEDs  (idx  0– 2)
//     #2  Under right wheel     2 LEDs  (idx  3– 4)
//     #3  Lower middle singles  7 LEDs  (idx  5–11)
//     #4  Bottom right corner   3 LEDs  (idx 12–14)
//     #5  Bottom left corner    3 LEDs  (idx 15–17)
//     #6  Seashell              3 LEDs  (idx 18–20)
//     #7  Under left wheel      2 LEDs  (idx 21–22)
//     #8  Upper left corner     3 LEDs  (idx 23–25)
//
// LEFT SIDE STRIP (35 LEDs, bottom to top):
//   GPIO 4 → strip DI  (330Ω resistor recommended)
//
// RIGHT SIDE STRIP (35 LEDs, bottom to top):
//   GPIO 5 → strip DI  (330Ω resistor recommended)
//
// DFPlayer Mini:
//   GPIO 16 (TX2) → DFPlayer RX  (via 1kΩ resistor!)
//   GPIO 17 (RX2) → DFPlayer TX
//   DFPlayer VCC  → 5V (from power bank)
//   DFPlayer GND  → GND
//   SPK1/SPK2     → 8Ω speaker(s)
//
// SEASHELL SWITCH:
//   GPIO 25 → taster → GND  (INPUT_PULLUP)
//
// WHEEL REED SENSORS (5 magnets, 72° apart):
//   GPIO 26 → left reed sensor → GND
//   GPIO 27 → right reed sensor → GND
//   Mounting: reed long axis tangential, 2–4mm gap.
//
// LASER LICHTSCHRANKE (top gate):
//   GPIO 32 → Light sensor module DO
//   GPIO 33 → 1kΩ → BC337 pin 2 (Base)
//
//   BC337 (TO-92, flat side facing you):
//     Pin 1 (left)   = COLLECTOR → Laser GND wire
//     Pin 2 (center) = BASE     → 1kΩ → GPIO 33
//     Pin 3 (right)  = EMITTER  → GND
//     Laser VCC → 3V3
//
// RESET BUTTON (existing on board):
//   GPIO 2 → taster → GND  (INPUT_PULLUP)
//
// ============================================================

#include <FastLED.h>
#include <DFRobotDFPlayerMini.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

Preferences prefs;

// ----- Pin definitions -----
#define PIN_MAIN_LED      22
#define PIN_SIDE_L_LED     4
#define PIN_SIDE_R_LED     5
#define PIN_RESET_BTN      2
#define PIN_SEASHELL      25
#define PIN_WHEEL_L       26
#define PIN_WHEEL_R       27
#define PIN_GATE_SENSOR   32
#define PIN_LASER         33
#define DFPLAYER_TX       16
#define DFPLAYER_RX       17
#define PIN_BRIGHT_POT    34    // Potentiometer for brightness (ADC1)
#define PIN_GAME_TOGGLE   13    // Locking switch: ON=playing, OFF=attract
#define PIN_SOUND_TOGGLE  14    // Locking switch: each flip plays next sound

#define SND_EXTRA_START    8    // First extra sound track number on SD card
#define SND_EXTRA_COUNT   10    // Number of extra sound files (008.mp3 - 017.mp3)

// ----- LED counts -----
#define NUM_MAIN          26
#define NUM_SIDE          35    // Per side strip

// ----- Main strip segment indices (clockwise from top-right) -----
#define CORNER_UR_START    0    // #1 Upper right corner
#define CORNER_UR_END      2    //     3 LEDs

#define WHEEL_R_START      3    // #2 Under right wheel
#define WHEEL_R_END        4    //     2 LEDs

#define HOLES_START        5    // #3 Lower middle singles
#define HOLES_END         11    //     7 LEDs

#define CORNER_BR_START   12    // #4 Bottom right corner
#define CORNER_BR_END     14    //     3 LEDs

#define CORNER_BL_START   15    // #5 Bottom left corner
#define CORNER_BL_END     17    //     3 LEDs

#define SHELL_START       18    // #6 Seashell
#define SHELL_END         20    //     3 LEDs

#define WHEEL_L_START     21    // #7 Under left wheel
#define WHEEL_L_END       22    //     2 LEDs

#define CORNER_UL_START   23    // #8 Upper left corner
#define CORNER_UL_END     25    //     3 LEDs

// ----- Sound tracks -----
#define SND_SHELL_CHIME    1
#define SND_WHEEL_SLOW     2
#define SND_WHEEL_MED      3
#define SND_WHEEL_FAST     4
#define SND_TOP_GATE       5
#define SND_COMBO_FANFARE  6
#define SND_JACKPOT        7

// ----- Timing -----
#define COMBO_WINDOW_MS      2500
#define REED_DEBOUNCE_MS       10
#define GATE_DEBOUNCE_MS        5
#define SHELL_DEBOUNCE_MS     200
#define RESET_DEBOUNCE_MS     500
#define WHEEL_TIMEOUT_MS      800

// Wheel speed: ms between reed pulses (5 magnets, 72° apart)
#define WHEEL_FAST_THRESH     150
#define WHEEL_MED_THRESH      300

// Effect durations
#define EFFECT_SHELL_MS       800
#define EFFECT_GATE_MS       1000
#define EFFECT_WHEEL_MS       500
#define EFFECT_COMBO_MS      1500

// Laser blink in attract mode
#define LASER_BLINK_ON_MS    2000
#define LASER_BLINK_OFF_MS    400

// ============================================================
// LED arrays
// ============================================================

CRGB mainLeds[NUM_MAIN];
CRGB sideLeds_L[NUM_SIDE];
CRGB sideLeds_R[NUM_SIDE];

DFRobotDFPlayerMini dfPlayer;
bool dfPlayerReady = false;

// ----- Game state -----
enum GameState { STATE_ATTRACT, STATE_PLAYING, STATE_DEBUG };
GameState gameState = STATE_ATTRACT;
GameState preDebugState = STATE_ATTRACT;
uint16_t score = 0;
unsigned long debugPrintMs = 0;

// ----- Switch state -----
struct SwitchState {
  uint8_t pin;
  bool lastState;
  unsigned long lastChangeMs;
  unsigned long debounceMs;
};

SwitchState sw_shell = { PIN_SEASHELL, HIGH, 0, SHELL_DEBOUNCE_MS };
SwitchState sw_reedL = { PIN_WHEEL_L,  HIGH, 0, REED_DEBOUNCE_MS };
SwitchState sw_reedR = { PIN_WHEEL_R,  HIGH, 0, REED_DEBOUNCE_MS };
SwitchState sw_gate  = { PIN_GATE_SENSOR, HIGH, 0, GATE_DEBOUNCE_MS };
SwitchState sw_reset = { PIN_RESET_BTN,   HIGH, 0, RESET_DEBOUNCE_MS };

// ----- Gate auto-calibration -----
bool gateBeamIntactState = HIGH;

// ----- Physical controls -----
bool lastGameToggle = HIGH;
bool lastSoundToggle = HIGH;
bool gameToggleActive = false;
uint8_t extraSoundIndex = 0;
unsigned long lastPotReadMs = 0;
unsigned long lastGameToggleMs = 0;
unsigned long lastSoundToggleMs = 0;

// ----- Wheel tracking -----
struct WheelTracker {
  unsigned long lastPulseMs;
  unsigned long pulseInterval;
  uint8_t pulseCount;
  uint8_t speedTier;        // 0=stopped, 1=slow, 2=med, 3=fast
  uint8_t lastSoundPlayed;
};

WheelTracker wheelL = { 0, 9999, 0, 0, 0 };
WheelTracker wheelR = { 0, 9999, 0, 0, 0 };

// ----- Combo -----
uint8_t comboCount = 0;
unsigned long lastHitMs = 0;

// ----- Active effects (main strip) -----
struct Effect {
  uint8_t type;   // 0=none 1=shell 2=wheelL 3=wheelR 4=gate 5=combo
  unsigned long startMs;
  unsigned long durationMs;
  uint8_t intensity;
};
#define MAX_EFFECTS 5
Effect effects[MAX_EFFECTS];

// ----- Side strip swoosh state -----
struct Swoosh {
  bool active;
  unsigned long startMs;
  uint8_t hue;
  uint8_t direction;    // 0=top-to-bottom, 1=bottom-to-top
  uint8_t intensity;
  uint16_t durationMs;
  uint8_t sides;        // 0=both, 1=left only, 2=right only
};
#define MAX_SWOOSHES 3
Swoosh swooshes[MAX_SWOOSHES];

// ----- Animation phase -----
uint16_t idlePhase = 0;
bool laserOn = false;
unsigned long laserToggleMs = 0;

// ----- Tunable parameters (adjustable via web, saveable to NVS) -----
uint16_t tuneGateDebounce = GATE_DEBOUNCE_MS;
uint16_t tuneReedDebounce = REED_DEBOUNCE_MS;
uint16_t tuneShellDebounce = SHELL_DEBOUNCE_MS;
uint16_t tuneWheelFast = WHEEL_FAST_THRESH;
uint16_t tuneWheelMed = WHEEL_MED_THRESH;
uint16_t tuneComboWindow = COMBO_WINDOW_MS;
uint8_t  tuneBrightness = 180;
uint16_t tuneLoopDelay = 10;

// ----- Color tuning (HSV hues, 0-255) -----
uint8_t tuneHueSlow   = 145;  // Blue
uint8_t tuneHueMed    = 24;   // Orange
uint8_t tuneHueFast   = 120;  // Cyan
uint8_t tuneHueShell  = 25;   // Warm gold
uint8_t tuneHueGate   = 130;  // Cyan-blue
uint8_t tuneHueIdle   = 140;  // Deep blue
uint8_t tuneHueAttract = 140; // Attract base hue
uint8_t tuneVolume    = 25;   // DFPlayer volume (0-30)

// ----- NVS save/load -----
void saveSettings() {
  prefs.begin("bm", false);
  prefs.putUShort("gateDb", tuneGateDebounce);
  prefs.putUShort("reedDb", tuneReedDebounce);
  prefs.putUShort("shellDb", tuneShellDebounce);
  prefs.putUShort("wFast", tuneWheelFast);
  prefs.putUShort("wMed", tuneWheelMed);
  prefs.putUShort("combo", tuneComboWindow);
  prefs.putUChar("bright", tuneBrightness);
  prefs.putUShort("loopMs", tuneLoopDelay);
  prefs.putUChar("hueSlow", tuneHueSlow);
  prefs.putUChar("hueMed", tuneHueMed);
  prefs.putUChar("hueFast", tuneHueFast);
  prefs.putUChar("hueShell", tuneHueShell);
  prefs.putUChar("hueGate", tuneHueGate);
  prefs.putUChar("hueIdle", tuneHueIdle);
  prefs.putUChar("hueAttr", tuneHueAttract);
  prefs.putUChar("vol", tuneVolume);
  prefs.putUChar("saved", 1);
  prefs.end();
  Serial.println("Settings SAVED to NVS");
}

void loadSettings() {
  prefs.begin("bm", true);
  if (prefs.getUChar("saved", 0) == 1) {
    tuneGateDebounce = prefs.getUShort("gateDb", GATE_DEBOUNCE_MS);
    tuneReedDebounce = prefs.getUShort("reedDb", REED_DEBOUNCE_MS);
    tuneShellDebounce = prefs.getUShort("shellDb", SHELL_DEBOUNCE_MS);
    tuneWheelFast = prefs.getUShort("wFast", WHEEL_FAST_THRESH);
    tuneWheelMed = prefs.getUShort("wMed", WHEEL_MED_THRESH);
    tuneComboWindow = prefs.getUShort("combo", COMBO_WINDOW_MS);
    tuneBrightness = prefs.getUChar("bright", 180);
    tuneLoopDelay = prefs.getUShort("loopMs", 10);
    tuneHueSlow = prefs.getUChar("hueSlow", 145);
    tuneHueMed = prefs.getUChar("hueMed", 24);
    tuneHueFast = prefs.getUChar("hueFast", 120);
    tuneHueShell = prefs.getUChar("hueShell", 25);
    tuneHueGate = prefs.getUChar("hueGate", 130);
    tuneHueIdle = prefs.getUChar("hueIdle", 140);
    tuneHueAttract = prefs.getUChar("hueAttr", 140);
    tuneVolume = prefs.getUChar("vol", 25);
    Serial.println("Settings LOADED from NVS");
  } else {
    Serial.println("No saved settings, using defaults");
  }
  prefs.end();

  // Apply loaded values
  FastLED.setBrightness(tuneBrightness);
  sw_gate.debounceMs = tuneGateDebounce;
  sw_reedL.debounceMs = tuneReedDebounce;
  sw_reedR.debounceMs = tuneReedDebounce;
  sw_shell.debounceMs = tuneShellDebounce;
}

// ----- Gate interrupt (catches fast ball crossings) -----
volatile bool gateISRfired = false;
volatile unsigned long gateISRtime = 0;

void IRAM_ATTR gateISR() {
  gateISRfired = true;
  gateISRtime = millis();
}

// ============================================================
// WiFi Captive Portal (debug via phone)
// ============================================================

WebServer webServer(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

unsigned long webHitShell = 0;
unsigned long webHitWheelL = 0;
unsigned long webHitWheelR = 0;
unsigned long webHitGate = 0;
bool webLaserOverride = false;
bool webLaserState = false;

void setupWiFi() {
  WiFi.softAP("BlueMermaid");
  delay(100);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Captive portal detection endpoints
  webServer.on("/generate_204", []() {
    webServer.sendHeader("Location", "http://192.168.4.1/");
    webServer.send(302, "text/plain", "");
  });
  webServer.on("/hotspot-detect.html", []() {
    webServer.sendHeader("Location", "http://192.168.4.1/");
    webServer.send(302, "text/plain", "");
  });
  webServer.on("/connecttest.txt", []() {
    webServer.sendHeader("Location", "http://192.168.4.1/");
    webServer.send(302, "text/plain", "");
  });
  webServer.on("/redirect", []() {
    webServer.sendHeader("Location", "http://192.168.4.1/");
    webServer.send(302, "text/plain", "");
  });
  webServer.on("/success.txt", []() {
    webServer.send(200, "text/plain", "success");
  });

  webServer.on("/", handleWebRoot);
  webServer.on("/data", handleWebData);
  webServer.on("/set", handleWebSet);
  webServer.on("/laser/on",  []() { webLaserOverride=true; webLaserState=true;  setLaser(true);  webServer.send(200, "text/plain", "on"); });
  webServer.on("/laser/off", []() { webLaserOverride=true; webLaserState=false; setLaser(false); webServer.send(200, "text/plain", "off"); });
  webServer.on("/laser/auto", []() { webLaserOverride=false; webServer.send(200, "text/plain", "auto"); });
  webServer.on("/save", []() { saveSettings(); webServer.send(200, "text/plain", "saved"); });
  webServer.on("/reset", []() {
    webHitShell=0; webHitWheelL=0; webHitWheelR=0; webHitGate=0;
    webServer.send(200, "text/plain", "ok");
  });
  webServer.onNotFound([]() {
    webServer.sendHeader("Location", "http://192.168.4.1/");
    webServer.send(302, "text/plain", "");
  });
  webServer.begin();
  Serial.print("WiFi AP: BlueMermaid @ ");
  Serial.println(WiFi.softAPIP());
}

void handleWebRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Blue Mermaid</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, Arial, sans-serif;
  background: #0a1628; color: #e0e8f0;
  padding: 12px; max-width: 420px; margin: 0 auto;
}
h1 { color: #4fc3f7; font-size: 18px; text-align: center; margin-bottom: 10px; }
.card {
  background: #162040; border-radius: 10px;
  padding: 12px; margin-bottom: 10px;
  border: 1px solid #2a3a5c;
}
.row { display: flex; justify-content: space-between; align-items: center;
       padding: 8px 0; border-bottom: 1px solid #1a2a4a; }
.row:last-child { border-bottom: none; }
.name { font-weight: bold; font-size: 14px; }
.sub { color: #888; font-size: 11px; }
.dot { width: 20px; height: 20px; border-radius: 50%; display: inline-block; }
.off { background: #333; }
.on-g { background: #0f0; box-shadow: 0 0 8px #0f0; }
.on-b { background: #06f; box-shadow: 0 0 8px #06f; }
.on-c { background: #0cc; box-shadow: 0 0 8px #0cc; }
.on-r { background: #f33; box-shadow: 0 0 8px #f33; }
.on-o { background: #f80; box-shadow: 0 0 8px #f80; }
.val { font-size: 20px; font-weight: bold; min-width: 40px; text-align: right; }
.score { font-size: 28px; color: #4fc3f7; text-align: center; font-weight: bold; }
.state { text-align: center; color: #8ab; font-size: 13px; margin: 4px 0; }
.btn { display: inline-block; padding: 8px 14px; border-radius: 6px;
       border: none; font-size: 13px; font-weight: bold; cursor: pointer; margin: 3px; }
.btn-on { background: #f80; color: #fff; }
.btn-off { background: #444; color: #ccc; }
.btn-rst { background: #2a4a6a; color: #8bb; }
.buttons { text-align: center; margin-top: 8px; }
.ts { text-align: center; color: #4a6; font-size: 11px; margin-top: 6px; }
.srow { display: flex; align-items: center; gap: 6px; padding: 5px 0;
        font-size: 12px; border-bottom: 1px solid #1a2a4a; flex-wrap: wrap; }
.srow:last-child { border-bottom: none; }
.srow span:first-child { flex: 1; }
.srow input[type=range] { width: 100%; margin-top: 2px; }
.hprev { display: inline-block; min-width: 32px; text-align: center;
         border-radius: 4px; padding: 1px 4px; }
</style>
</head>
<body>
<h1>&#x1F42C; Blue Mermaid</h1>
<div class="card">
  <div class="score" id="score">0</div>
  <div class="state" id="state">ATTRACT</div>
  <div class="state" id="combo"></div>
</div>
<div class="card">
  <div class="row">
    <div><span class="name">Muschel</span><br><span class="sub">GPIO25</span></div>
    <div class="val" id="c_sh">0</div>
    <div class="dot off" id="d_sh"></div>
  </div>
  <div class="row">
    <div><span class="name">Rad L</span><br><span class="sub" id="wl_int">GPIO26</span></div>
    <div class="val" id="c_wl">0</div>
    <div class="dot off" id="d_wl"></div>
  </div>
  <div class="row">
    <div><span class="name">Rad R</span><br><span class="sub" id="wr_int">GPIO27</span></div>
    <div class="val" id="c_wr">0</div>
    <div class="dot off" id="d_wr"></div>
  </div>
  <div class="row">
    <div><span class="name">Gate</span><br><span class="sub" id="g_cal">GPIO32</span></div>
    <div class="val" id="c_ga">0</div>
    <div class="dot off" id="d_ga"></div>
  </div>
  <div class="row">
    <div><span class="name">Laser</span><br><span class="sub">GPIO33</span></div>
    <div></div>
    <div class="dot off" id="d_la"></div>
  </div>
</div>
<div class="card buttons">
  <button class="btn btn-on" onclick="fetch('/laser/on')">Laser AN</button>
  <button class="btn btn-off" onclick="fetch('/laser/off')">Laser AUS</button>
  <button class="btn btn-rst" onclick="fetch('/laser/auto')">Laser Auto</button>
  <button class="btn btn-rst" onclick="fetch('/reset')">Reset</button>
</div>
<div class="card">
  <div class="name" style="margin-bottom:8px">Timing</div>
  <div class="srow"><span>Gate Debounce</span><span id="v_gd">5</span>ms
    <input type="range" min="0" max="50" value="5" id="s_gd" oninput="sv('gateDb',this.value,'v_gd')"></div>
  <div class="srow"><span>Reed Debounce</span><span id="v_rd">10</span>ms
    <input type="range" min="1" max="50" value="10" id="s_rd" oninput="sv('reedDb',this.value,'v_rd')"></div>
  <div class="srow"><span>Shell Debounce</span><span id="v_sd">200</span>ms
    <input type="range" min="50" max="500" value="200" id="s_sd" oninput="sv('shellDb',this.value,'v_sd')"></div>
  <div class="srow"><span>Wheel Fast</span><span id="v_wf">150</span>ms
    <input type="range" min="50" max="400" value="150" id="s_wf" oninput="sv('wheelFast',this.value,'v_wf')"></div>
  <div class="srow"><span>Wheel Medium</span><span id="v_wm">300</span>ms
    <input type="range" min="100" max="600" value="300" id="s_wm" oninput="sv('wheelMed',this.value,'v_wm')"></div>
  <div class="srow"><span>Combo Window</span><span id="v_cw">2500</span>ms
    <input type="range" min="500" max="10000" step="100" value="2500" id="s_cw" oninput="sv('combo',this.value,'v_cw')"></div>
  <div class="srow"><span>LED Brightness</span><span id="v_br">180</span>
    <input type="range" min="10" max="255" value="180" id="s_br" oninput="sv('bright',this.value,'v_br')"></div>
  <div class="srow"><span>Loop Delay</span><span id="v_ld">10</span>ms
    <input type="range" min="1" max="30" value="10" id="s_ld" oninput="sv('loopMs',this.value,'v_ld')"></div>
</div>
<div class="card">
  <div class="name" style="margin-bottom:8px">Colors <span style="font-weight:normal;font-size:11px;color:#888">(HSV hue 0-255)</span></div>
  <div class="srow"><span>Idle/Base</span><span id="v_hi" class="hprev">140</span>
    <input type="range" min="0" max="255" value="140" oninput="sv('hueIdle',this.value,'v_hi')"></div>
  <div class="srow"><span>Attract</span><span id="v_ha" class="hprev">140</span>
    <input type="range" min="0" max="255" value="140" oninput="sv('hueAttr',this.value,'v_ha')"></div>
  <div class="srow"><span>Wheel Slow</span><span id="v_hs" class="hprev">145</span>
    <input type="range" min="0" max="255" value="145" oninput="sv('hueSlow',this.value,'v_hs')"></div>
  <div class="srow"><span>Wheel Medium</span><span id="v_hm" class="hprev">24</span>
    <input type="range" min="0" max="255" value="24" oninput="sv('hueMed',this.value,'v_hm')"></div>
  <div class="srow"><span>Wheel Fast</span><span id="v_hf" class="hprev">120</span>
    <input type="range" min="0" max="255" value="120" oninput="sv('hueFast',this.value,'v_hf')"></div>
  <div class="srow"><span>Seashell</span><span id="v_hsh" class="hprev">25</span>
    <input type="range" min="0" max="255" value="25" oninput="sv('hueShell',this.value,'v_hsh')"></div>
  <div class="srow"><span>Gate Flash</span><span id="v_hg" class="hprev">130</span>
    <input type="range" min="0" max="255" value="130" oninput="sv('hueGate',this.value,'v_hg')"></div>
  <div class="srow"><span>Volume</span><span id="v_vol">25</span>
    <input type="range" min="0" max="30" value="25" oninput="sv('vol',this.value,'v_vol')"></div>
</div>
<div class="card buttons">
  <button class="btn btn-on" onclick="fetch('/save').then(()=>document.getElementById('ts').textContent='SAVED!')">SAVE</button>
  <button class="btn btn-rst" onclick="fetch('/reset')">Reset Counters</button>
</div>
<div class="ts" id="ts">...</div>
<script>
function sv(k,v,id){
  var el=document.getElementById(id);
  el.textContent=v;
  if(el.classList.contains('hprev')){
    var h=v*360/256;
    el.style.background='hsl('+h+',80%,45%)';
    el.style.color='#fff';
  }
  fetch('/set?'+k+'='+v);
}
function initHue(){
  document.querySelectorAll('.hprev').forEach(function(el){
    var h=parseInt(el.textContent)*360/256;
    el.style.background='hsl('+h+',80%,45%)';
    el.style.color='#fff';
  });
}
function u(){
  fetch('/data').then(r=>r.json()).then(d=>{
    document.getElementById('score').textContent=d.score;
    document.getElementById('state').textContent=d.state;
    document.getElementById('combo').textContent=d.combo>1?'Combo x'+d.combo:'';
    document.getElementById('d_sh').className='dot '+(d.shell?'on-g':'off');
    document.getElementById('d_wl').className='dot '+(d.wheelL?'on-b':'off');
    document.getElementById('d_wr').className='dot '+(d.wheelR?'on-c':'off');
    document.getElementById('d_ga').className='dot '+(d.gate?'on-r':'off');
    document.getElementById('d_la').className='dot '+(d.laser?'on-o':'off');
    document.getElementById('c_sh').textContent=d.cntShell;
    document.getElementById('c_wl').textContent=d.cntWheelL;
    document.getElementById('c_wr').textContent=d.cntWheelR;
    document.getElementById('c_ga').textContent=d.cntGate;
    if(d.intWheelL>0) document.getElementById('wl_int').textContent='GPIO26 · '+d.intWheelL+'ms';
    if(d.intWheelR>0) document.getElementById('wr_int').textContent='GPIO27 · '+d.intWheelR+'ms';
    document.getElementById('g_cal').textContent='GPIO32 · cal='+d.gateCal;
    document.getElementById('ts').textContent=new Date().toLocaleTimeString();
  }).catch(()=>{document.getElementById('ts').textContent='lost...';});
}
setInterval(u,200); u(); initHue();
</script>
</body>
</html>
)rawliteral";
  webServer.send(200, "text/html", html);
}

void handleWebData() {
  bool shell  = (digitalRead(PIN_SEASHELL) == LOW);
  bool wL     = (digitalRead(PIN_WHEEL_L) == LOW);
  bool wR     = (digitalRead(PIN_WHEEL_R) == LOW);
  bool gate   = (digitalRead(PIN_GATE_SENSOR) != gateBeamIntactState);
  bool laser  = digitalRead(PIN_LASER);

  const char* stateStr = "ATTRACT";
  if (gameState == STATE_PLAYING) stateStr = "PLAYING";
  else if (gameState == STATE_DEBUG) stateStr = "DEBUG";

  char json[400];
  snprintf(json, sizeof(json),
    "{\"score\":%d,\"state\":\"%s\",\"combo\":%d,"
    "\"shell\":%d,\"wheelL\":%d,\"wheelR\":%d,\"gate\":%d,\"laser\":%d,"
    "\"cntShell\":%lu,\"cntWheelL\":%lu,\"cntWheelR\":%lu,\"cntGate\":%lu,"
    "\"intWheelL\":%lu,\"intWheelR\":%lu,\"gateCal\":%d}",
    score, stateStr, comboCount,
    shell, wL, wR, gate, laser,
    webHitShell, webHitWheelL, webHitWheelR, webHitGate,
    wheelL.pulseInterval, wheelR.pulseInterval, gateBeamIntactState);

  webServer.send(200, "application/json", json);
}

void handleWebSet() {
  if (webServer.hasArg("gateDb"))    tuneGateDebounce = webServer.arg("gateDb").toInt();
  if (webServer.hasArg("reedDb"))    tuneReedDebounce = webServer.arg("reedDb").toInt();
  if (webServer.hasArg("shellDb"))   tuneShellDebounce = webServer.arg("shellDb").toInt();
  if (webServer.hasArg("wheelFast")) tuneWheelFast = webServer.arg("wheelFast").toInt();
  if (webServer.hasArg("wheelMed"))  tuneWheelMed = webServer.arg("wheelMed").toInt();
  if (webServer.hasArg("combo"))     tuneComboWindow = webServer.arg("combo").toInt();
  if (webServer.hasArg("bright")) {
    tuneBrightness = webServer.arg("bright").toInt();
    FastLED.setBrightness(tuneBrightness);
  }
  if (webServer.hasArg("loopMs"))    tuneLoopDelay = webServer.arg("loopMs").toInt();
  if (webServer.hasArg("hueSlow"))   tuneHueSlow = webServer.arg("hueSlow").toInt();
  if (webServer.hasArg("hueMed"))    tuneHueMed = webServer.arg("hueMed").toInt();
  if (webServer.hasArg("hueFast"))   tuneHueFast = webServer.arg("hueFast").toInt();
  if (webServer.hasArg("hueShell"))  tuneHueShell = webServer.arg("hueShell").toInt();
  if (webServer.hasArg("hueGate"))   tuneHueGate = webServer.arg("hueGate").toInt();
  if (webServer.hasArg("hueIdle"))   tuneHueIdle = webServer.arg("hueIdle").toInt();
  if (webServer.hasArg("hueAttr"))   tuneHueAttract = webServer.arg("hueAttr").toInt();
  if (webServer.hasArg("vol")) {
    tuneVolume = webServer.arg("vol").toInt();
    if (dfPlayerReady) dfPlayer.volume(tuneVolume);
  }

  sw_gate.debounceMs = tuneGateDebounce;
  sw_reedL.debounceMs = tuneReedDebounce;
  sw_reedR.debounceMs = tuneReedDebounce;
  sw_shell.debounceMs = tuneShellDebounce;

  webServer.send(200, "text/plain", "ok");
}

// ============================================================
// Setup
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);

  // Three separate LED strips
  FastLED.addLeds<WS2812B, PIN_MAIN_LED,   GRB>(mainLeds,   NUM_MAIN);
  FastLED.addLeds<WS2812B, PIN_SIDE_L_LED, GRB>(sideLeds_L, NUM_SIDE);
  FastLED.addLeds<WS2812B, PIN_SIDE_R_LED, GRB>(sideLeds_R, NUM_SIDE);
  FastLED.setBrightness(180);
  fill_solid(mainLeds,   NUM_MAIN, CRGB::Black);
  fill_solid(sideLeds_L, NUM_SIDE, CRGB::Black);
  fill_solid(sideLeds_R, NUM_SIDE, CRGB::Black);
  FastLED.show();

  // Pins
  pinMode(PIN_SEASHELL,    INPUT_PULLUP);
  pinMode(PIN_WHEEL_L,     INPUT_PULLUP);
  pinMode(PIN_WHEEL_R,     INPUT_PULLUP);
  pinMode(PIN_GATE_SENSOR, INPUT);
  pinMode(PIN_RESET_BTN,   INPUT_PULLUP);
  pinMode(PIN_LASER,       OUTPUT);
  pinMode(PIN_BRIGHT_POT,  INPUT);         // ADC, no pull-up needed (pot is voltage divider)
  pinMode(PIN_GAME_TOGGLE, INPUT_PULLUP);  // Locking switch to GND
  pinMode(PIN_SOUND_TOGGLE, INPUT_PULLUP); // Locking switch to GND

  // Read initial toggle states
  lastGameToggle = digitalRead(PIN_GAME_TOGGLE);
  lastSoundToggle = digitalRead(PIN_SOUND_TOGGLE);
  gameToggleActive = (lastGameToggle == LOW);

  // Laser on for gate calibration
  setLaser(true);
  delay(100);
  gateBeamIntactState = digitalRead(PIN_GATE_SENSOR);
  sw_gate.lastState = gateBeamIntactState;
  Serial.printf("Gate calibrated: intact = %s\n",
                gateBeamIntactState ? "HIGH" : "LOW");

  // Gate interrupt — fires on ANY change so we never miss a fast ball
  attachInterrupt(digitalPinToInterrupt(PIN_GATE_SENSOR), gateISR, CHANGE);

  // DFPlayer — begin() can block up to 3s while reading SD card
  delay(1000);
  if (!dfPlayer.begin(Serial2, /*isACK=*/false, /*doReset=*/false)) {
    Serial.println("DFPlayer FAILED");
    for (int i = 0; i < 3; i++) {
      fill_solid(mainLeds, NUM_MAIN, CRGB::Red);
      FastLED.show(); delay(200);
      fill_solid(mainLeds, NUM_MAIN, CRGB::Black);
      FastLED.show(); delay(200);
    }
  } else {
    dfPlayerReady = true;
    Serial.println("DFPlayer OK");
  }
  // Load saved settings from NVS (before applying volume)
  loadSettings();
  if (dfPlayerReady) dfPlayer.volume(tuneVolume);

  // Clear effects and swooshes
  for (int i = 0; i < MAX_EFFECTS; i++) effects[i].type = 0;
  for (int i = 0; i < MAX_SWOOSHES; i++) swooshes[i].active = false;

  gameState = STATE_ATTRACT;
  setupWiFi();
  Serial.println("=== THE BLUE MERMAID v4 ===");
}

// ============================================================
// Main loop
// ============================================================

void loop() {
  unsigned long now = millis();

  dnsServer.processNextRequest();
  webServer.handleClient();

  // --- Potentiometer brightness (read every 100ms, smoothed) ---
  if ((now - lastPotReadMs) > 100) {
    lastPotReadMs = now;
    int raw = analogRead(PIN_BRIGHT_POT);
    uint8_t potBright = map(raw, 0, 4095, 10, 255);
    // Smooth: only update if change is significant (avoids ADC noise flicker)
    if (abs((int)potBright - (int)tuneBrightness) > 5) {
      tuneBrightness = potBright;
      FastLED.setBrightness(tuneBrightness);
    }
  }

  // --- Game toggle switch (locking switch, 50ms debounce) ---
  bool curGame = digitalRead(PIN_GAME_TOGGLE);
  if (curGame != lastGameToggle && (now - lastGameToggleMs) > 50) {
    lastGameToggle = curGame;
    lastGameToggleMs = now;
    gameToggleActive = (curGame == LOW);
    if (gameToggleActive && gameState == STATE_ATTRACT) {
      score = 0;
      comboCount = 0;
      lastHitMs = 0;
      wheelL = { 0, 9999, 0, 0, 0 };
      wheelR = { 0, 9999, 0, 0, 0 };
      for (int i = 0; i < MAX_EFFECTS; i++) effects[i].type = 0;
      for (int i = 0; i < MAX_SWOOSHES; i++) swooshes[i].active = false;
      gameState = STATE_PLAYING;
      setLaser(true);
    } else if (!gameToggleActive && gameState == STATE_PLAYING) {
      gameState = STATE_ATTRACT;
      setLaser(false);
    }
  }

  // --- Sound toggle switch (locking switch, 50ms debounce) ---
  bool curSound = digitalRead(PIN_SOUND_TOGGLE);
  if (curSound != lastSoundToggle && (now - lastSoundToggleMs) > 50) {
    lastSoundToggle = curSound;
    lastSoundToggleMs = now;
    if (dfPlayerReady) dfPlayer.volume(tuneVolume);
    playSound(SND_EXTRA_START + extraSoundIndex);
    extraSoundIndex = (extraSoundIndex + 1) % SND_EXTRA_COUNT;
  }

  checkResetButton(now);
  updateLaser(now);

  if (gameState == STATE_DEBUG) {
    renderDebug(now);
    FastLED.show();
    delay(10);
    return;
  }

  if (gameState == STATE_PLAYING) {
    checkSeashell(now);
    checkReed(now, sw_reedL, wheelL, true);
    checkReed(now, sw_reedR, wheelR, false);
    checkTopGate(now);
    updateWheelTimeout(now, wheelL);
    updateWheelTimeout(now, wheelR);

    if (comboCount > 0 && (now - lastHitMs) > tuneComboWindow) {
      comboCount = 0;
    }
  }

  renderMainStrip(now);
  renderSideStrips(now);
  FastLED.show();
  delay(tuneLoopDelay);
}

// ============================================================
// Game control
// ============================================================

void checkResetButton(unsigned long now) {
  if (debouncePress(sw_reset, now)) {

    if (gameState == STATE_DEBUG) {
      // Exit debug → go to attract
      gameState = STATE_ATTRACT;
      setLaser(false);
      Serial.println(">>> DEBUG OFF — ATTRACT MODE <<<");
      return;
    }

    if (gameState == STATE_ATTRACT) {
      // Short press in attract: check if held for 2s → debug
      // For now: first press = start game
      score = 0;
      comboCount = 0;
      lastHitMs = 0;
      wheelL = { 0, 9999, 0, 0, 0 };
      wheelR = { 0, 9999, 0, 0, 0 };
      for (int i = 0; i < MAX_EFFECTS; i++) effects[i].type = 0;
      for (int i = 0; i < MAX_SWOOSHES; i++) swooshes[i].active = false;

      gameState = STATE_PLAYING;
      webLaserOverride = false;
      setLaser(true);
      gateISRfired = false;
      Serial.println(">>> GAME START <<<");

      // Startup flash
      fill_solid(mainLeds,   NUM_MAIN, CRGB(0, 60, 90));
      fill_solid(sideLeds_L, NUM_SIDE, CRGB(0, 40, 80));
      fill_solid(sideLeds_R, NUM_SIDE, CRGB(0, 40, 80));
      FastLED.show();
      delay(400);

    } else if (gameState == STATE_PLAYING) {
      // Press during game = reset score
      score = 0;
      comboCount = 0;
      lastHitMs = 0;
      Serial.println(">>> SCORE RESET <<<");
    }
  }

  // Long press detection: hold button for 2 seconds → toggle debug
  static unsigned long btnHeldSince = 0;
  static bool btnWasHeld = false;
  bool btnDown = (digitalRead(PIN_RESET_BTN) == LOW);

  if (btnDown && btnHeldSince == 0) {
    btnHeldSince = now;
    btnWasHeld = false;
  } else if (btnDown && !btnWasHeld && (now - btnHeldSince) > 2000) {
    btnWasHeld = true;
    if (gameState != STATE_DEBUG) {
      preDebugState = gameState;
      gameState = STATE_DEBUG;
      setLaser(true);
      Serial.println(">>> DEBUG MODE ON <<<");
      Serial.println("    Inputs shown on LEDs + Serial.");
      Serial.println("    Press button to exit.");
    }
  } else if (!btnDown) {
    btnHeldSince = 0;
  }
}

// ============================================================
// Laser
// ============================================================

void setLaser(bool on) {
  laserOn = on;
  digitalWrite(PIN_LASER, on ? HIGH : LOW);
  gateISRfired = false;  // Ignore interrupt caused by laser toggle
}

void updateLaser(unsigned long now) {
  if (webLaserOverride) {
    if (laserOn != webLaserState) setLaser(webLaserState);
    return;
  }
  if (gameState == STATE_PLAYING) {
    if (!laserOn) setLaser(true);
    return;
  }
  // Attract: blink
  if (laserOn && (now - laserToggleMs) > LASER_BLINK_ON_MS) {
    setLaser(false);
    laserToggleMs = now;
  } else if (!laserOn && (now - laserToggleMs) > LASER_BLINK_OFF_MS) {
    setLaser(true);
    laserToggleMs = now;
  }
}

// ============================================================
// Switch reading
// ============================================================

bool debouncePress(SwitchState &sw, unsigned long now) {
  bool cur = digitalRead(sw.pin);
  if (cur != sw.lastState && (now - sw.lastChangeMs) > sw.debounceMs) {
    sw.lastChangeMs = now;
    bool wasHigh = sw.lastState;
    sw.lastState = cur;
    return (wasHigh && !cur);  // Falling edge
  }
  return false;
}

bool debounceGate(SwitchState &sw, unsigned long now) {
  bool cur = digitalRead(sw.pin);
  if (cur != sw.lastState && (now - sw.lastChangeMs) > sw.debounceMs) {
    sw.lastChangeMs = now;
    bool wasIntact = (sw.lastState == gateBeamIntactState);
    sw.lastState = cur;
    return (wasIntact && cur != gateBeamIntactState);
  }
  return false;
}

// ============================================================
// Hit registration & combo
// ============================================================

void registerHit(unsigned long now, uint16_t points) {
  score += points;
  if ((now - lastHitMs) < tuneComboWindow) {
    comboCount++;
  } else {
    comboCount = 1;
  }
  lastHitMs = now;

  Serial.printf("Score: %d  Combo: x%d\n", score, comboCount);

  if (comboCount >= 5) {
    triggerEffect(5, EFFECT_COMBO_MS, 255, now);
    triggerSwoosh(1, 160, 255, 1200, 0, now);  // Bottom-to-top rainbow
    playSound(SND_JACKPOT);
    Serial.println(">>> JACKPOT <<<");
  } else if (comboCount >= 3) {
    triggerEffect(5, EFFECT_COMBO_MS, 200, now);
    triggerSwoosh(0, 128, 200, 800, 0, now);   // Top-to-bottom
    playSound(SND_COMBO_FANFARE);
    Serial.println(">> COMBO <<");
  }
}

// ============================================================
// Input handlers
// ============================================================

void checkSeashell(unsigned long now) {
  if (debouncePress(sw_shell, now)) {
    Serial.println("Seashell!");
    triggerEffect(1, EFFECT_SHELL_MS, 255, now);
    playSound(SND_SHELL_CHIME);
    registerHit(now, 50);
    webHitShell++;
  }
}

void checkReed(unsigned long now, SwitchState &sw,
               WheelTracker &wt, bool isLeft) {
  if (debouncePress(sw, now)) {
    if (isLeft) webHitWheelL++; else webHitWheelR++;
    if (wt.lastPulseMs > 0) {
      wt.pulseInterval = now - wt.lastPulseMs;
    }
    wt.lastPulseMs = now;
    wt.pulseCount++;

    // Speed tier
    uint8_t newTier;
    if (wt.pulseInterval < tuneWheelFast)      newTier = 3;
    else if (wt.pulseInterval < tuneWheelMed)  newTier = 2;
    else                                       newTier = 1;
    wt.speedTier = newTier;

    // Sound on tier change
    uint8_t snd;
    if (newTier == 3)      snd = SND_WHEEL_FAST;
    else if (newTier == 2) snd = SND_WHEEL_MED;
    else                   snd = SND_WHEEL_SLOW;

    bool tierChanged = (snd != wt.lastSoundPlayed);

    if (tierChanged) {
      playSound(snd);
      wt.lastSoundPlayed = snd;
    }

    // Main strip wheel effect
    uint8_t et = isLeft ? 2 : 3;
    triggerEffect(et, EFFECT_WHEEL_MS,
                  min((int)(100 + newTier * 50), 255), now);

    // Side strip swoosh on medium/fast spin
    if (newTier >= 2) {
      uint8_t hue = 120 + newTier * 15;
      uint8_t bright = 100 + newTier * 40;
      uint8_t side = isLeft ? 1 : 2;
      triggerSwoosh(0, hue, bright, 600 - newTier * 100, side, now);
    }

    // Score on first pulse or tier change
    if (wt.pulseCount == 1 || tierChanged) {
      registerHit(now, 10 * newTier);
    }

    Serial.printf("Wheel %c: %lums tier=%d\n",
                  isLeft ? 'L' : 'R', wt.pulseInterval, newTier);
  }
}

void updateWheelTimeout(unsigned long now, WheelTracker &wt) {
  if (wt.speedTier > 0 && (now - wt.lastPulseMs) > WHEEL_TIMEOUT_MS) {
    wt.speedTier = 0;
    wt.pulseCount = 0;
    wt.lastSoundPlayed = 0;
  }
}

void checkTopGate(unsigned long now) {
  // Interrupt-based: ball crosses beam so fast that digitalRead()
  // already shows "intact" by the time we check. Trust the interrupt.
  // CHANGE fires twice per crossing (break + restore), so debounce
  // at minimum 100ms to prevent double-counting one ball pass.
  if (gateISRfired) {
    gateISRfired = false;
    unsigned long minGap = max((uint16_t)100, tuneGateDebounce);

    if ((now - sw_gate.lastChangeMs) > minGap) {
      sw_gate.lastChangeMs = now;
      Serial.println("TOP GATE!");
      triggerEffect(4, EFFECT_GATE_MS, 255, now);
      playSound(SND_TOP_GATE);
      registerHit(now, 100);
      webHitGate++;
      triggerSwoosh(0, 130, 255, 500, 0, now);
    }
  }
}

// ============================================================
// Sound
// ============================================================

unsigned long lastSoundMs = 0;

void playSound(uint8_t track) {
  if (!dfPlayerReady) return;
  unsigned long now = millis();
  if ((now - lastSoundMs) < 50) return;
  lastSoundMs = now;
  dfPlayer.play(track);
}

// ============================================================
// Effect management (main strip)
// ============================================================

void triggerEffect(uint8_t type, unsigned long dur,
                   uint8_t intensity, unsigned long now) {
  int slot = -1;
  unsigned long oldest = now;
  int oldSlot = 0;

  for (int i = 0; i < MAX_EFFECTS; i++) {
    if (effects[i].type == 0) { slot = i; break; }
    if (effects[i].startMs < oldest) {
      oldest = effects[i].startMs;
      oldSlot = i;
    }
  }
  if (slot == -1) slot = oldSlot;

  effects[slot] = { type, now, dur, intensity };
}

// ============================================================
// Swoosh management (side strips)
// ============================================================

void triggerSwoosh(uint8_t dir, uint8_t hue, uint8_t intensity,
                   uint16_t durationMs, uint8_t sides,
                   unsigned long now) {
  int slot = -1;
  for (int i = 0; i < MAX_SWOOSHES; i++) {
    if (!swooshes[i].active) { slot = i; break; }
  }
  if (slot == -1) slot = 0;

  swooshes[slot] = { true, now, hue, dir, intensity, durationMs, sides };
}

// ============================================================
// MAIN STRIP rendering
// ============================================================

void renderMainStrip(unsigned long now) {
  if (gameState == STATE_ATTRACT) {
    renderMainAttract(now);
  } else {
    renderMainIdle(now);
  }

  // Overlay effects
  for (int i = 0; i < MAX_EFFECTS; i++) {
    if (effects[i].type == 0) continue;
    unsigned long elapsed = now - effects[i].startMs;
    if (elapsed > effects[i].durationMs) {
      effects[i].type = 0;
      continue;
    }
    float prog = (float)elapsed / effects[i].durationMs;
    uint8_t fade = 255 - (uint8_t)(prog * 255);

    switch (effects[i].type) {
      case 1: fxShell(fade, prog); break;
      case 2: fxWheel(true, fade, now); break;
      case 3: fxWheel(false, fade, now); break;
      case 4: fxGateFlash(fade, prog, now); break;
      case 5: fxCombo(fade, prog, now); break;
    }
  }
}

// ----- Attract: vivid to draw players -----
void renderMainAttract(unsigned long now) {
  idlePhase++;
  for (int i = 0; i < NUM_MAIN; i++) {
    uint8_t wave = sin8(idlePhase + i * 14);
    uint8_t hue = tuneHueAttract + sin8(idlePhase / 3 + i * 9) / 8;
    mainLeds[i] = CHSV(hue, 230, map(wave, 0, 255, 10, 100));
  }
  for (int i = SHELL_START; i <= SHELL_END; i++) {
    uint8_t p = sin8(idlePhase / 2);
    mainLeds[i] = CHSV(tuneHueShell, 200, map(p, 0, 255, 20, 120));
  }
}

// ----- Wheel strobe: intense blink when spinning -----
void renderWheelStrobe(unsigned long now, uint8_t startIdx, uint8_t endIdx,
                       WheelTracker &wt) {
  if (wt.speedTier == 0) {
    // Stopped: gentle idle pulse
    for (int i = startIdx; i <= endIdx; i++) {
      uint8_t p = sin8(now / 40 + i * 30);
      mainLeds[i] = CHSV(tuneHueIdle - 5, 210, map(p, 0, 255, 8, 35));
    }
    return;
  }

  // Spinning: strobe! Faster tier = faster blink
  // Tier 1: 80ms cycle, tier 2: 50ms, tier 3: 30ms
  uint8_t strobeMs = (wt.speedTier == 1) ? 80 : (wt.speedTier == 2) ? 50 : 30;
  bool strobeOn = ((now / strobeMs) % 2) == 0;
  uint8_t hue = sideHueForTier(wt.speedTier);

  for (int i = startIdx; i <= endIdx; i++) {
    if (strobeOn) {
      mainLeds[i] = CHSV(hue, 220, 255);  // Full blast
    } else {
      mainLeds[i] = CHSV(hue, 180, 30);   // Dim base, never fully dark
    }
  }
}

// ----- Gameplay idle: visible base glow + effects on top -----
void renderMainIdle(unsigned long now) {
  idlePhase++;

  // Corners: brighter base glow with wave
  uint8_t cornerSegs[] = {
    CORNER_UR_START, CORNER_UR_END,
    CORNER_BR_START, CORNER_BR_END,
    CORNER_BL_START, CORNER_BL_END,
    CORNER_UL_START, CORNER_UL_END
  };
  for (int s = 0; s < 8; s += 2) {
    for (int i = cornerSegs[s]; i <= cornerSegs[s + 1]; i++) {
      uint8_t wave = sin8(idlePhase / 2 + i * 18);
      mainLeds[i] = CHSV(tuneHueIdle + (i * 3) % 20, 180,
                          map(wave, 0, 255, 30, 90));
    }
  }

  // Wheel underglow: strobe when spinning, gentle pulse when stopped
  renderWheelStrobe(now, WHEEL_R_START, WHEEL_R_END, wheelR);
  renderWheelStrobe(now, WHEEL_L_START, WHEEL_L_END, wheelL);

  // Holes: brighter base with sparkle
  for (int i = HOLES_START; i <= HOLES_END; i++) {
    uint8_t twinkle = sin8(idlePhase / 5 + i * 53);
    uint8_t hue = tuneHueIdle - 10 + (i * 13) % 40;
    uint8_t bright;
    if (random8() > 248) {
      bright = random8(100, 200);
      hue = tuneHueIdle + random8(30);
    } else {
      bright = map(twinkle, 0, 255, 25, 70);
    }
    mainLeds[i] = CHSV(hue, 160, bright);
  }

  // Seashell: warmer and brighter
  for (int i = SHELL_START; i <= SHELL_END; i++) {
    uint8_t p = sin8(idlePhase / 3 + 80);
    mainLeds[i] = CHSV(tuneHueShell, 160, map(p, 0, 255, 30, 80));
  }
}

// ----- Seashell hit: golden-pink pearl burst -----
void fxShell(uint8_t fade, float prog) {
  for (int i = SHELL_START; i <= SHELL_END; i++) {
    mainLeds[i] = CHSV(tuneHueShell + (uint8_t)(prog * 20), 180, fade);
  }
  if (fade > 100) {
    uint8_t ripple = fade / 3;
    for (int i = CORNER_BL_START; i <= CORNER_BL_END; i++)
      mainLeds[i] += CHSV(tuneHueShell + 10, 150, ripple);
    for (int i = CORNER_BR_START; i <= CORNER_BR_END; i++)
      mainLeds[i] += CHSV(tuneHueShell + 10, 150, ripple);
  }
}

// ----- Wheel hit: glow on wheel + adjacent corner -----
void fxWheel(bool isLeft, uint8_t fade, unsigned long now) {
  uint8_t wStart, wEnd, cStart, cEnd;
  WheelTracker &wt = isLeft ? wheelL : wheelR;

  if (isLeft) {
    wStart = WHEEL_L_START; wEnd = WHEEL_L_END;
    cStart = CORNER_UL_START; cEnd = CORNER_UL_END;
  } else {
    wStart = WHEEL_R_START; wEnd = WHEEL_R_END;
    cStart = CORNER_UR_START; cEnd = CORNER_UR_END;
  }

  // Wheel underglow: strobe during effect
  uint8_t wheelHue = sideHueForTier(wt.speedTier);
  uint8_t strobeMs = (wt.speedTier <= 1) ? 80 : (wt.speedTier == 2) ? 50 : 30;
  bool strobeOn = ((now / strobeMs) % 2) == 0;
  for (int i = wStart; i <= wEnd; i++) {
    mainLeds[i] = strobeOn ? CHSV(wheelHue, 220, fade) : CHSV(0, 0, 0);
  }

  // Adjacent corner lights up too
  if (fade > 80) {
    for (int i = cStart; i <= cEnd; i++) {
      mainLeds[i] = CHSV(wheelHue, 200, fade / 2);
    }
  }
}

// ----- Gate: dramatic flash on everything -----
void fxGateFlash(uint8_t fade, float prog, unsigned long now) {
  for (int i = CORNER_UR_START; i <= CORNER_UR_END; i++)
    mainLeds[i] = CHSV(tuneHueGate, 200, fade);
  for (int i = CORNER_UL_START; i <= CORNER_UL_END; i++)
    mainLeds[i] = CHSV(tuneHueGate, 200, fade);
  for (int i = CORNER_BR_START; i <= CORNER_BR_END; i++)
    mainLeds[i] = CHSV(tuneHueGate, 200, fade / 2);
  for (int i = CORNER_BL_START; i <= CORNER_BL_END; i++)
    mainLeds[i] = CHSV(tuneHueGate, 200, fade / 2);

  for (int i = WHEEL_L_START; i <= WHEEL_L_END; i++)
    mainLeds[i] = CHSV(tuneHueGate - 10, 220, fade);
  for (int i = WHEEL_R_START; i <= WHEEL_R_END; i++)
    mainLeds[i] = CHSV(tuneHueGate - 10, 220, fade);

  if (fade > 120) {
    for (int i = HOLES_START; i <= HOLES_END; i++) {
      mainLeds[i] = CHSV(tuneHueGate + 10, 200, fade / 2);
    }
  }
}

// ----- Combo: sparkle burst across everything -----
void fxCombo(uint8_t fade, float prog, unsigned long now) {
  for (int i = 0; i < NUM_MAIN; i++) {
    if (random8() > 190) {
      mainLeds[i] = CHSV((now / 5 + i * 17) % 256, 180, fade);
    } else {
      uint8_t p = sin8(now / 3 + i * 25);
      mainLeds[i] = blend(mainLeds[i],
                          CHSV(128, 220, scale8(p, fade / 2)), 128);
    }
  }
}

// ============================================================
// SIDE STRIPS rendering
// ============================================================
//
// sideLeds_L[0] = left bottom,  sideLeds_L[34] = left top
// sideLeds_R[0] = right bottom, sideLeds_R[34] = right top

void renderSideStrips(unsigned long now) {
  if (gameState == STATE_ATTRACT) {
    renderSideAttract(now);
  } else {
    renderSideIdle(now);
  }

  // Overlay swooshes
  for (int i = 0; i < MAX_SWOOSHES; i++) {
    if (!swooshes[i].active) continue;
    unsigned long elapsed = now - swooshes[i].startMs;
    if (elapsed > swooshes[i].durationMs) {
      swooshes[i].active = false;
      continue;
    }
    renderSwoosh(swooshes[i], elapsed, now);
  }
}

// ----- Side idle: reacts to wheel speed -----
// Tier 0 (stopped): dim blue shimmer
// Tier 1 (slow):    brighter blue pulse
// Tier 2 (medium):  orange pulse
// Tier 3 (fast):    cyan rapid pulse (as before)

uint8_t sideHueForTier(uint8_t tier) {
  switch (tier) {
    case 2:  return tuneHueMed;
    case 3:  return tuneHueFast;
    default: return tuneHueSlow;
  }
}

void renderSideIdle(unsigned long now) {
  uint8_t divL = (wheelL.speedTier == 0) ? 30 : (wheelL.speedTier == 1) ? 20 : (wheelL.speedTier == 2) ? 10 : 5;
  uint8_t divR = (wheelR.speedTier == 0) ? 30 : (wheelR.speedTier == 1) ? 20 : (wheelR.speedTier == 2) ? 10 : 5;
  uint8_t hueL = sideHueForTier(wheelL.speedTier);
  uint8_t hueR = sideHueForTier(wheelR.speedTier);

  for (int i = 0; i < NUM_SIDE; i++) {
    // Bottom LEDs (idx 0) are brightest, fades toward top (idx 34)
    // Gradient: bottom = full white-ish glow, top = subtle color
    float pos = (float)i / (NUM_SIDE - 1);  // 0.0=bottom, 1.0=top
    uint8_t baseMin = (uint8_t)(40 * (1.0 - pos * 0.7));   // 40 at bottom, 12 at top
    uint8_t baseMax = (uint8_t)(120 * (1.0 - pos * 0.5));   // 120 at bottom, 60 at top
    uint8_t sat = 120 + (uint8_t)(pos * 110);  // Less saturated (whiter) at bottom

    // When spinning, boost everything
    if (wheelL.speedTier > 0) {
      baseMin += wheelL.speedTier * 10;
      baseMax += wheelL.speedTier * 30;
    }
    if (baseMax > 255) baseMax = 255;

    uint8_t breathL = sin8(now / divL + i * 7);
    uint8_t breathR = sin8(now / divR + i * 7);

    sideLeds_L[i] = CHSV(hueL, sat, map(breathL, 0, 255, baseMin, baseMax));

    // Right side uses its own wheel tier
    uint8_t baseMinR = (uint8_t)(40 * (1.0 - pos * 0.7));
    uint8_t baseMaxR = (uint8_t)(120 * (1.0 - pos * 0.5));
    if (wheelR.speedTier > 0) {
      baseMinR += wheelR.speedTier * 10;
      baseMaxR += wheelR.speedTier * 30;
    }
    if (baseMaxR > 255) baseMaxR = 255;

    sideLeds_R[i] = CHSV(hueR, sat, map(breathR, 0, 255, baseMinR, baseMaxR));
  }
}

// ----- Side attract: more visible -----
void renderSideAttract(unsigned long now) {
  for (int i = 0; i < NUM_SIDE; i++) {
    uint8_t wave = sin8(now / 20 + i * 10);
    uint8_t hue = 135 + sin8(now / 50 + i * 5) / 10;
    CRGB c = CHSV(hue, 220, map(wave, 0, 255, 5, 70));
    sideLeds_L[i] = c;
    sideLeds_R[i] = c;
  }
}

// ----- Swoosh: light races along the strip -----
void renderSwoosh(Swoosh &sw, unsigned long elapsed, unsigned long now) {
  float progress = (float)elapsed / sw.durationMs;

  // Head position along the 35-LED strip
  float headPos = progress * (NUM_SIDE + 6);  // +6 for tail clearance
  uint8_t tailLen = 8;

  for (int j = 0; j < NUM_SIDE; j++) {
    int pos;
    if (sw.direction == 0) {
      // Top to bottom: index 34 is top, 0 is bottom
      pos = (NUM_SIDE - 1) - j;
    } else {
      // Bottom to top
      pos = j;
    }

    float dist = headPos - pos;
    if (dist < 0 || dist > tailLen) continue;

    float tailFade = 1.0 - (dist / tailLen);
    uint8_t bright = (uint8_t)(sw.intensity * tailFade * tailFade);
    uint8_t hue = sw.hue + (uint8_t)(dist * 3);

    if (sw.sides == 0 || sw.sides == 1) {
      sideLeds_L[j] = blend(sideLeds_L[j], CHSV(hue, 220, bright), 200);
    }
    if (sw.sides == 0 || sw.sides == 2) {
      sideLeds_R[j] = blend(sideLeds_R[j], CHSV(hue, 220, bright), 200);
    }
  }
}

// ============================================================
// DEBUG MODE
// ============================================================
//
// Main strip LED layout in debug mode:
//
//   LEDs  0– 3 : SEASHELL  (GPIO 25)  — GREEN  when active
//   LEDs  5– 8 : WHEEL L   (GPIO 26)  — BLUE   when active
//   LEDs  9–12 : WHEEL R   (GPIO 27)  — CYAN   when active
//   LEDs 13–16 : GATE      (GPIO 32)  — RED    when active
//   LED  4     : separator (dim white)
//   LEDs 17–20 : LASER     (GPIO 33)  — ORANGE when laser on
//   LEDs 21–25 : pulse counter (lights up with each hit)
//
//   Side strips: dim blue base, flash WHITE on any input
//
// Serial prints raw pin states every 200ms.

void renderDebug(unsigned long now) {
  // Read raw pin states — NO debounce, direct reads
  bool rawShell  = digitalRead(PIN_SEASHELL);
  bool rawReedL  = digitalRead(PIN_WHEEL_L);
  bool rawReedR  = digitalRead(PIN_WHEEL_R);
  bool rawGate   = digitalRead(PIN_GATE_SENSOR);

  // Detect if gate beam is broken (compared to calibrated state)
  bool gateTriggered = (rawGate != gateBeamIntactState);

  // Active = LOW for pullup switches, special logic for gate
  bool shellActive = (rawShell == LOW);
  bool reedLActive = (rawReedL == LOW);
  bool reedRActive = (rawReedR == LOW);
  bool anyActive   = shellActive || reedLActive || reedRActive || gateTriggered;

  // --- Serial output every 200ms ---
  if ((now - debugPrintMs) > 200) {
    debugPrintMs = now;
    Serial.printf("DEBUG | Shell:%d(%s) WheelL:%d(%s) WheelR:%d(%s) Gate:%d(%s/cal:%d) Laser:%s\n",
      rawShell,  shellActive  ? "ON" : "--",
      rawReedL,  reedLActive  ? "ON" : "--",
      rawReedR,  reedRActive  ? "ON" : "--",
      rawGate,   gateTriggered ? "ON" : "--", gateBeamIntactState,
      laserOn ? "ON" : "OFF");
  }

  // --- Main strip: clear to dim base ---
  fill_solid(mainLeds, NUM_MAIN, CRGB(5, 5, 5));

  // Separator
  mainLeds[4] = CRGB(20, 20, 20);

  // Seashell indicator (LEDs 0-3)
  {
    CRGB c = shellActive ? CRGB(0, 255, 0) : CRGB(0, 15, 0);
    for (int i = 0; i <= 3; i++) mainLeds[i] = c;
  }

  // Wheel L indicator (LEDs 5-8)
  {
    CRGB c = reedLActive ? CRGB(0, 0, 255) : CRGB(0, 0, 15);
    for (int i = 5; i <= 8; i++) mainLeds[i] = c;
  }

  // Wheel R indicator (LEDs 9-12)
  {
    CRGB c = reedRActive ? CRGB(0, 255, 255) : CRGB(0, 15, 15);
    for (int i = 9; i <= 12; i++) mainLeds[i] = c;
  }

  // Gate indicator (LEDs 13-16)
  {
    CRGB c = gateTriggered ? CRGB(255, 0, 0) : CRGB(15, 0, 0);
    for (int i = 13; i <= 16; i++) mainLeds[i] = c;
  }

  // Laser status (LEDs 17-20)
  {
    CRGB c = laserOn ? CRGB(255, 120, 0) : CRGB(15, 7, 0);
    for (int i = 17; i <= 20; i++) mainLeds[i] = c;
  }

  // Pulse counter: lights up one more LED with each detected edge
  // (simple visual counter that wraps around)
  {
    static uint8_t pulseCount = 0;
    static bool prevAny = false;
    if (anyActive && !prevAny) {
      pulseCount = (pulseCount + 1) % 6;  // 0-5, wraps
    }
    prevAny = anyActive;
    for (int i = 0; i < 5; i++) {
      mainLeds[21 + i] = (i < pulseCount) ? CRGB(100, 0, 100) : CRGB(5, 0, 5);
    }
  }

  // --- Side strips: dim blue, flash white on any input ---
  for (int i = 0; i < NUM_SIDE; i++) {
    CRGB base = CRGB(0, 0, 10);
    sideLeds_L[i] = anyActive ? CRGB(80, 80, 80) : base;
    sideLeds_R[i] = anyActive ? CRGB(80, 80, 80) : base;
  }
}
