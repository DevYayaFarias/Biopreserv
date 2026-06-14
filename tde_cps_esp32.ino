#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==========================================
// 1. MAPEAMENTO E HARDWARE
// ==========================================
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ135_PIN 34
#define RELAY_PIN 5
#define SERVO_PIN 13
#define BTN_EMERGENCIA 0

// FÍSICA REAL COMPROVADA: Active High
#define RELAY_LIGA HIGH
#define RELAY_DESLIGA LOW

// FÍSICA REAL COMPROVADA: Servo
#define SERVO_ABERTO 90
#define SERVO_FECHADO 0

DHT dht(DHTPIN, DHTTYPE);
Servo windowServo;
Preferences preferences;
AsyncWebServer server(80);

// ==========================================
// 2. VARIÁVEIS DE ESTADO E LOGS (FreeRTOS)
// ==========================================
SemaphoreHandle_t xDataMutex = NULL;
SemaphoreHandle_t xLogMutex = NULL;

float t = 0.0;    
float h = 0.0;    
int gas = 0;     
float limUmidade = 70.0, limTemp = 25.0; 
int limAr = 1500; 
int freqLeitura = 2; 

bool ventoinhaLigada = false;
bool janelaAberta = false;
bool modoManual = false; 
bool primeiraLeitura = false; 
unsigned long ultimaLeitura = 0;

int core1Load = 0;
unsigned long timeDHT = 0;
unsigned long timeMQ = 0;
unsigned long timeLogic = 0;
unsigned long timeFS = 0;
unsigned long timeNet = 0;
unsigned long lastTimeNet = 0; // Armazena tempo de rede para cálculo de CPU

volatile bool emergencyStop = false;
bool alertaUmidade = false;
bool alertaTemperatura = false;
bool alertaAr = false;

String sta_ssid = "";
String sta_pass = "";

volatile bool savePending = false;
float pendH = 0.0; float pendT = 0.0; int pendA = 0;

volatile bool saveAdvPending = false;
int pendFreq = 2;

volatile bool rebootPending = false;
unsigned long rebootTimer = 0;

#define MAX_LOGS 15
String sysLogs[MAX_LOGS];
int logIndex = 0;
int logCount = 0;

TaskHandle_t TaskSystemHandle = NULL;
TaskHandle_t TaskPersistHandle = NULL;

// ==============================================================================
// 3. PROTÓTIPOS DE FUNÇÕES
// ==============================================================================
void IRAM_ATTR isrEmergency();
void addSystemLog(String level, String msg);
String getTaskStateStr(int s);
void vTaskSystem(void *pvParameters);
void vTaskPersist(void *pvParameters);

// ==============================================================================
// 4. INTERRUPÇÕES E LOGS
// ==============================================================================
void IRAM_ATTR isrEmergency() {
  emergencyStop = !emergencyStop;
}

void addSystemLog(String level, String msg) {
  if (xLogMutex != NULL) {
    if (xSemaphoreTake(xLogMutex, portMAX_DELAY)) {
      unsigned long secs = millis() / 1000;
      char tStamp[12];
      sprintf(tStamp, "%02lu:%02lu:%02lu", (secs / 3600), ((secs % 3600) / 60), (secs % 60));
      
      String newLog = "{\"time\":\"" + String(tStamp) + "\", \"level\":\"" + level + "\", \"msg\":\"" + msg + "\"}";
      sysLogs[logIndex] = newLog;
      logIndex = (logIndex + 1) % MAX_LOGS;
      if(logCount < MAX_LOGS) logCount++;
      xSemaphoreGive(xLogMutex);
    }
  }
}

String getTaskStateStr(int s) {
  switch(s) {
    case 0: return "Running";   // eRunning
    case 1: return "Ready";     // eReady
    case 2: return "Blocked";   // eBlocked
    case 3: return "Suspended"; // eSuspended
    case 4: return "Deleted";   // eDeleted
    default: return "Unknown";
  }
}

// ==============================================================================
// 5. FRONT-END: CSS
// ==============================================================================
const char BIOPRESERV_CSS[] PROGMEM = R"rawliteral(
:root {
    --bg-body: #11151c; --bg-header: #1a212b; --bg-panel: #1a212c;
    --bg-card: #202834; --bg-box: #202733; --bg-input: #0f131a; --bg-terminal: #0a0e14;
    --color-cyan: #00e5ff; --color-yellow: #ffcc00; --color-red: #ff3b3b;
    --color-green: #00ff88; --color-purple: #9d4edd; --color-blue-chart: #1a5b75;
    --color-text-main: #8a99ae; --color-text-light: #e2e8f0; --color-text-dark: #000000;
    --color-text-muted: #64748b;
    --border-subtle: #2d3748;
    --border-radius-sm: 6px; --border-radius-md: 12px; --border-radius-lg: 16px;
}

* { margin: 0; padding: 0; box-sizing: border-box; }

body { background-color: var(--bg-body); color: var(--color-text-main); font-family: 'Inter', sans-serif; -webkit-font-smoothing: antialiased; min-height: 100vh; display: flex; flex-direction: column; }
.font-mono { font-family: 'JetBrains Mono', monospace; }
.font-bold { font-weight: 700; }
.text-cyan { color: var(--color-cyan); }
.text-yellow { color: var(--color-yellow); }
.text-red { color: var(--color-red); }
.text-white { color: #fff; }
.text-purple { color: #b775ff; }
.text-green { color: var(--color-green); }
.mb-0 { margin-bottom: 4px !important; }
.mb-1 { margin-bottom: 8px; }
.mb-3 { margin-bottom: 16px; }
.mb-4 { margin-bottom: 24px; }
.mt-4 { margin-top: 16px; }
.mr-2 { margin-right: 8px; }
.w-100 { width: 100%; }
.hidden { display: none !important; }

.top-header { background-color: var(--bg-header); border-bottom: 2px solid var(--color-cyan); padding: 24px 0; text-align: center; position: relative; box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2); }
.header-content { max-width: 1040px; margin: 0 auto; position: relative; display: flex; justify-content: center; align-items: center; flex-direction: column; }
.logo-title { color: var(--color-cyan); font-size: 32px; font-weight: 700; letter-spacing: 1px; margin-bottom: 4px; }
.subtitle { font-size: 13px; color: var(--color-text-main); }
.highlight { color: var(--color-cyan); }
.btn-link { cursor: pointer; text-decoration: underline; text-underline-offset: 2px; color: var(--color-cyan); }

.header-icon { position: absolute; right: 20px; top: 50%; transform: translateY(-50%); background-color: #176b78; width: 40px; height: 40px; border-radius: 50%; display: flex; align-items: center; justify-content: center; cursor: pointer; transition: all 0.3s ease; border: 2px solid var(--color-cyan); box-shadow: 0 0 10px rgba(0, 229, 255, 0.3); color: var(--color-cyan); }
.header-icon:hover { background-color: #1d8b9c; transform: translateY(-50%) scale(1.05); }
#toggle-icon-svg { transition: transform 0.4s ease; }

.dashboard-container { max-width: 1040px; width: 100%; margin: 30px auto; padding: 0 20px; flex-grow: 1; display: flex; flex-direction: column; gap: 24px; }

.metrics-header-wrapper { width: 100%; max-width: 800px; margin: 0 auto; display: flex; flex-direction: column; }
.metrics-top-bar { display: flex; justify-content: flex-end; min-height: 28px; margin-bottom: 8px; }
.metrics-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 20px; width: 100%; }
.metric-card { background-color: var(--bg-card); border-radius: var(--border-radius-md); padding: 16px 20px; box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.05), 0 4px 10px rgba(0,0,0,0.2); display: flex; flex-direction: column; align-items: flex-start; border: 1px solid var(--border-subtle); }
.card-label { font-size: 10px; font-weight: 600; color: var(--color-text-main); text-transform: uppercase; margin-bottom: 8px; letter-spacing: 0.5px; }
.card-value { font-size: 28px; font-weight: 700; color: var(--color-cyan); line-height: 1; margin-bottom: 4px; }
.card-unit { font-size: 11px; color: var(--color-text-main); margin-bottom: 12px; }
.badge { font-size: 9px; font-weight: 700; padding: 3px 8px; border-radius: 12px; text-transform: uppercase; letter-spacing: 0.5px; }
.badge-normal { color: var(--color-cyan); border: 1px solid var(--color-cyan); background-color: rgba(0, 229, 255, 0.05); }
.badge-warning { color: var(--color-yellow); border: 1px solid var(--color-yellow); background-color: rgba(255, 204, 0, 0.05); }
.badge-danger { color: var(--color-red); border: 1px solid var(--color-red); background-color: rgba(255, 59, 59, 0.05); }
.wifi-status { font-size: 11px; background: var(--bg-card); padding: 4px 12px; border-radius: 20px; border: 1px solid var(--border-subtle); display: inline-flex; align-items: center; }
.wifi-text { color: var(--color-text-main); }
.highlight-green { color: var(--color-green); font-weight: 600; margin-left: 4px; }

.panel { background-color: var(--bg-panel); border-radius: var(--border-radius-lg); padding: 24px; box-shadow: 0 4px 15px rgba(0,0,0,0.15); border: 1px solid #232c3b; }
.panel-title { font-size: 14px; font-weight: 600; color: var(--color-text-light); margin-bottom: 16px; text-transform: uppercase; letter-spacing: 0.5px; }
.panel-subtitle { font-size: 11px; color: var(--color-text-muted); margin-bottom: 20px; display: block; }

#simple-view, #advanced-view { display: flex; flex-direction: column; gap: 24px; }
.alerts-container { display: flex; flex-direction: column; gap: 12px; }
.alert-box { display: flex; align-items: center; padding: 14px 16px; border-radius: var(--border-radius-sm); background-color: var(--bg-body); gap: 12px; }
.alert-yellow { border: 1px solid rgba(255, 204, 0, 0.4); }
.alert-red { border: 1px solid rgba(255, 59, 59, 0.4); }
.dot { width: 10px; height: 10px; border-radius: 50%; flex-shrink: 0; }
.dot-yellow { background-color: var(--color-yellow); box-shadow: 0 0 8px rgba(255, 204, 0, 0.6); }
.dot-red { background-color: var(--color-red); box-shadow: 0 0 8px rgba(255, 59, 59, 0.6); }
.alert-text { font-size: 13px; font-weight: 500; color: var(--color-text-light); }

.config-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 20px; }
.config-box { border: 1px solid var(--border-subtle); border-radius: var(--border-radius-md); padding: 20px; display: flex; flex-direction: column; background: var(--bg-box); }
.box-title { font-size: 13px; font-weight: 600; color: var(--color-text-light); margin-bottom: 16px; }
.box-subtitle { font-size: 11px; color: var(--color-text-main); margin-top: -12px; margin-bottom: 16px; display: block; }
.input-group { margin-bottom: 12px; }
.input-group label { display: block; font-size: 9px; font-weight: 600; color: var(--color-text-main); margin-bottom: 4px; }
.input-group input { width: 100%; background-color: var(--bg-input); border: 1px solid var(--border-subtle); border-radius: var(--border-radius-sm); color: var(--color-text-light); padding: 8px; font-size: 12px; font-family: 'Inter', sans-serif; outline: none; }
.input-group input:focus { border-color: var(--color-cyan); }
.btn-save { margin-top: auto; background-color: var(--color-cyan); color: var(--color-text-dark); border: none; border-radius: var(--border-radius-sm); padding: 10px; font-size: 11px; font-weight: 700; cursor: pointer; text-transform: uppercase; }
.toggle-group { display: flex; flex-direction: column; gap: 10px; }
.btn-toggle { background-color: transparent; border: 1px solid var(--border-subtle); color: var(--color-text-main); border-radius: var(--border-radius-sm); padding: 10px; font-size: 12px; font-weight: 600; cursor: pointer; text-align: center; transition: 0.2s; }
.btn-toggle.active { border-color: var(--color-cyan); color: var(--color-cyan); background-color: rgba(0, 229, 255, 0.08); }
.uptime-display { font-size: 26px; font-weight: 700; color: var(--color-cyan); margin-top: 4px; }

.adv-panel { padding: 20px 24px; }
.adv-header { display: flex; justify-content: space-between; align-items: flex-start; margin-bottom: 16px; }
.chart-legend { display: flex; gap: 12px; font-size: 10px; color: var(--color-text-main); }
.legend-item { display: flex; align-items: center; gap: 4px; }
.dot-legend { width: 6px; height: 6px; border-radius: 50%; display: inline-block; }
.dot-cyan { background-color: var(--color-cyan); box-shadow: 0 0 4px var(--color-cyan); }
.dot-red-chart { background-color: var(--color-red); box-shadow: 0 0 4px var(--color-red); }
.chart-container { display: flex; height: 160px; margin-top: 10px; position: relative; }
.y-axis { display: flex; flex-direction: column-reverse; justify-content: space-between; font-size: 10px; color: var(--color-text-muted); padding-right: 12px; border-right: 1px solid var(--border-subtle); width: 25px; text-align: right; }
.chart-area { flex-grow: 1; position: relative; padding-left: 10px; display: flex; flex-direction: column; }
.chart-svg { width: 100%; height: 100%; display: block; overflow: visible; }
.line-red { fill: none; stroke: var(--color-red); stroke-width: 2; }
.line-cyan { fill: none; stroke: var(--color-cyan); stroke-width: 2; }
.x-axis { display: flex; justify-content: space-between; font-size: 10px; color: var(--color-text-muted); margin-top: 8px; border-top: 1px solid var(--border-subtle); padding-top: 4px; }

.progress-item { margin-bottom: 14px; }
.progress-labels { display: flex; justify-content: space-between; font-size: 11px; margin-bottom: 6px; }
.prog-name { color: var(--color-text-main); }
.prog-val { font-weight: 600; font-size: 10px; }
.progress-track { width: 100%; height: 6px; background-color: rgba(255,255,255,0.05); border-radius: 4px; overflow: hidden; }
.progress-fill { height: 100%; border-radius: 4px; transition: width 0.3s; }
.fill-cyan { background-color: var(--color-cyan); box-shadow: 0 0 8px rgba(0, 229, 255, 0.4); }
.fill-gray { background-color: #64748b; }
.fill-purple { background-color: var(--color-purple); }

.ticks-info { font-family: 'JetBrains Mono', monospace; font-size: 12px; color: var(--color-cyan); font-weight: 700; }
.rtos-table { width: 100%; border-collapse: collapse; font-size: 11px; text-align: left; }
.rtos-table th { color: var(--color-text-muted); font-weight: 600; padding: 10px 8px; border-bottom: 1px solid var(--border-subtle); text-transform: uppercase; font-size: 10px; }
.rtos-table td { padding: 12px 8px; border-bottom: 1px solid rgba(255,255,255,0.02); color: var(--color-text-light); }
.rtos-table tr:last-child td { border-bottom: none; }
.rtos-badge { padding: 3px 8px; border-radius: 4px; font-size: 9px; font-weight: 600; text-transform: uppercase; }
.badge-running { background: rgba(0, 255, 136, 0.1); color: var(--color-green); border: 1px solid rgba(0, 255, 136, 0.3); }
.badge-blocked { background: rgba(100, 116, 139, 0.1); color: #94a3b8; border: 1px solid rgba(100, 116, 139, 0.3); }
.badge-ready { background: rgba(0, 229, 255, 0.1); color: var(--color-cyan); border: 1px solid rgba(0, 229, 255, 0.3); }

.profiler-list { display: flex; flex-direction: column; gap: 12px; }
.profiler-item .prof-header { display: flex; justify-content: space-between; font-size: 11px; margin-bottom: 6px; color: var(--color-text-main); }
.prof-dur { font-size: 10px; }
.alert-inline { display: flex; align-items: center; gap: 8px; font-size: 10px; color: var(--color-text-muted); }
.track-dark { background-color: #0d1117; }

.terminal-controls { display: flex; align-items: center; gap: 16px; font-size: 11px; flex-wrap: wrap; }
.terminal-search { background: var(--bg-input); border: 1px solid var(--border-subtle); color: var(--color-text-light); padding: 6px 10px; border-radius: 4px; font-size: 11px; outline: none; }
.cb-container { display: flex; align-items: center; gap: 6px; cursor: pointer; color: var(--color-text-main); }
.cb-container input { display: none; }
.cb-mark { width: 12px; height: 12px; border-radius: 2px; display: inline-block; position: relative; }
.cb-info { background: var(--color-cyan); }
.cb-warn { background: var(--color-yellow); }
.cb-err { background: var(--color-red); }
.btn-export { background: transparent; border: 1px solid var(--border-subtle); color: var(--color-text-light); padding: 6px 12px; border-radius: 4px; font-size: 11px; cursor: pointer; display: flex; align-items: center; gap: 6px; }
.btn-flash { border-color: var(--color-purple); color: var(--color-purple); margin-left: 8px; }
.terminal-window { background: var(--bg-terminal); border: 1px solid #141a23; border-radius: 8px; padding: 16px; height: 220px; overflow-y: auto; font-family: 'JetBrains Mono', monospace; font-size: 11px; line-height: 1.6; margin-top: 10px; }
.log-time { color: var(--color-text-muted); margin-right: 8px; }
.log-info { color: var(--color-cyan); font-weight: 700; width: 45px; display: inline-block; }
.log-warn { color: var(--color-yellow); font-weight: 700; width: 45px; display: inline-block; }
.log-error { color: var(--color-red); font-weight: 700; width: 45px; display: inline-block; }
.log-msg { color: #d1d5db; }

.adv-bottom-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-top: 20px; }
.slider-group { margin-bottom: 20px; }
.slider-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; font-size: 12px; color: var(--color-text-light); }
.slider-val { font-size: 11px; }
.slider-help { font-size: 9px; color: var(--color-text-muted); margin-top: 6px; line-height: 1.3; }
input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; }
input[type=range]:focus { outline: none; }
input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 4px; cursor: pointer; background: rgba(255,255,255,0.1); border-radius: 2px; }
input[type=range]::-webkit-slider-thumb { height: 14px; width: 14px; border-radius: 50%; background: #fff; cursor: pointer; -webkit-appearance: none; margin-top: -5px; box-shadow: 0 0 5px rgba(0,0,0,0.5); }
.slider-cyan::-webkit-slider-thumb { background: var(--color-cyan); box-shadow: 0 0 8px var(--color-cyan); }
.slider-yellow::-webkit-slider-thumb { background: var(--color-yellow); box-shadow: 0 0 8px var(--color-yellow); }
.slider-red::-webkit-slider-thumb { background: var(--color-red); box-shadow: 0 0 8px var(--color-red); }

.manual-control-mt { margin-top: 24px; border-top: 1px solid var(--border-subtle); padding-top: 16px; }
.label-sm { font-size: 9px; font-weight: 600; color: var(--color-text-muted); display: block; margin-bottom: 12px; text-transform: uppercase; }
.gpios-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
.btn-gpio { background: transparent; border: 1px solid var(--border-subtle); color: var(--color-text-main); font-size: 10px; padding: 10px; border-radius: 6px; cursor: pointer; font-weight: 600; transition: 0.2s; text-align: center; }
.btn-gpio.active { border-color: var(--color-cyan); color: var(--color-cyan); background-color: rgba(0, 229, 255, 0.08); }

.input-group-adv { margin-bottom: 16px; }
.input-group-adv label { display: block; font-size: 11px; color: var(--color-text-main); margin-bottom: 6px; }
.input-group-adv input { width: 100%; background: var(--bg-input); border: 1px solid var(--border-subtle); padding: 10px; border-radius: 6px; color: var(--color-text-light); font-size: 12px; outline: none; font-family: 'Inter', sans-serif; }
.alert-box-adv { border: 1px solid rgba(255, 204, 0, 0.3); background: rgba(255, 204, 0, 0.05); padding: 12px; border-radius: 6px; display: flex; align-items: flex-start; }
.alert-text-adv { font-size: 10px; color: var(--color-text-main); line-height: 1.4; }
.btn-reconnect { background: var(--color-cyan); color: #000; border: none; padding: 12px; border-radius: 6px; font-size: 11px; font-weight: 700; cursor: pointer; text-transform: uppercase; box-shadow: 0 0 10px rgba(0, 229, 255, 0.2); }

.mcu-info-card { display: flex; align-items: center; gap: 12px; background: rgba(0,0,0,0.2); border: 1px solid var(--border-subtle); border-radius: 8px; padding: 12px 16px; margin-top: 16px; width: max-content; }
.mcu-icon { width: 20px; height: 20px; color: var(--color-text-muted); }
.mcu-details { display: flex; flex-direction: column; }
.mcu-title { font-size: 12px; font-weight: 600; color: var(--color-text-light); }
.mcu-sub { font-size: 10px; color: var(--color-text-muted); }

.bottom-footer { text-align: center; padding: 20px 20px 30px; margin-top: auto; }
.footer-divider { border: none; border-top: 1px solid #2a3342; max-width: 600px; margin: 0 auto 20px; }
.footer-text { font-size: 12px; color: var(--color-text-main); }
.footer-text strong { color: var(--color-text-light); }
.sub-text { font-size: 11px; margin-top: 4px; }
.modal-overlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background-color: rgba(6, 10, 18, 0.85); display: none; align-items: center; justify-content: center; z-index: 1000; backdrop-filter: blur(2px); }
.modal-overlay.active { display: flex; }
.modal-box { background-color: var(--bg-panel); border: 1px solid var(--color-cyan); border-radius: 40px; padding: 40px 60px; width: 90%; max-width: 800px; box-shadow: 0 0 50px rgba(0, 229, 255, 0.1); position: relative; max-height: 90vh; overflow-y: auto;}
.modal-title { color: var(--color-cyan); font-size: 32px; font-weight: 400; text-align: center; margin-bottom: 24px; letter-spacing: 0.5px; }
.modal-description { color: var(--color-text-light); font-size: 13px; line-height: 1.6; text-align: justify; margin-bottom: 40px; }
.modal-details { display: flex; flex-direction: column; gap: 24px; }
.detail-row { display: grid; grid-template-columns: 240px 1fr; align-items: start; }
.detail-label { color: var(--color-text-muted); font-size: 13px; }
.detail-value { color: var(--color-text-light); font-size: 13px; font-weight: 500; }
.detail-value.font-bold { font-weight: 700; }
.detail-value ul { list-style-type: disc; padding-left: 18px; margin: 0; }
.detail-value li { margin-bottom: 6px; }

@media (max-width: 768px) {
    .adv-bottom-grid { grid-template-columns: 1fr; }
    .metrics-grid { grid-template-columns: 1fr 1fr; }
}
)rawliteral";

// ==============================================================================
// 6. FRONT-END: HTML
// ==============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Biopreserv Dashboard</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;700&display=swap" rel="stylesheet">
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <header class="top-header">
        <div class="header-content">
            <h1 class="logo-title">BIOPRESERV</h1>
            <p class="subtitle">Sistema Preditivo de Detecção de Mofo | <span class="btn-link" id="open-modal">Sobre</span> | <span class="btn-link" id="open-log">Changelog</span></p>
            <div class="header-icon" id="toggle-view-btn" title="Alternar Visão">
                <svg id="toggle-icon-svg" width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
                    <line x1="5" y1="12" x2="19" y2="12"></line>
                    <polyline points="12 5 19 12 12 19"></polyline>
                </svg>
            </div>
        </div>
    </header>

    <main class="dashboard-container">
        
        <div class="metrics-header-wrapper">
            <div class="metrics-top-bar">
                <div class="wifi-status hidden" id="wifi-badge">
                    <span class="wifi-text">Modo Wi-Fi: <span class="highlight-green">AP Local</span></span>
                </div>
            </div>
            
            <section class="metrics-grid">
                <div class="metric-card">
                    <div class="card-label">TEMPERATURA</div>
                    <div class="card-value" id="val-temp">00.0</div>
                    <div class="card-unit">°C</div>
                    <div class="badge badge-normal" id="badge-temp">AGUARDANDO</div>
                </div>
                <div class="metric-card">
                    <div class="card-label">UMIDADE</div>
                    <div class="card-value" id="val-umid">00</div>
                    <div class="card-unit">%</div>
                    <div class="badge badge-normal" id="badge-umid">AGUARDANDO</div>
                </div>
                <div class="metric-card">
                    <div class="card-label">QUALIDADE DO AR</div>
                    <div class="card-value" id="val-ar">000</div>
                    <div class="card-unit">ppm</div>
                    <div class="badge badge-normal" id="badge-ar">AGUARDANDO</div>
                </div>
                <div class="metric-card">
                    <div class="card-label">STATUS GERAL</div>
                    <div class="card-value" id="status-geral" style="font-size: 18px;">INICIALIZANDO</div>
                    <div class="card-unit">Última:</div>
                    <div class="badge badge-normal" id="badge-geral">BOOT</div>
                </div>
            </section>
        </div>

        <div id="simple-view">
            <section class="panel hidden" id="dynamic-alerts-section">
                <h2 class="panel-title">ALERTAS E EVENTOS</h2>
                <div class="alerts-container" id="alerts-container">
                </div>
            </section>

            <section class="panel">
                <div class="adv-header">
                    <div>
                        <h2 class="panel-title mb-0">GRÁFICO DE SENSORES TEMPORAL</h2>
                        <p class="panel-subtitle">Acompanhamento temporal em tempo real</p>
                    </div>
                    <div class="chart-legend">
                        <span class="legend-item"><span class="dot-legend dot-cyan"></span> Umidade (%)</span>
                        <span class="legend-item"><span class="dot-legend dot-red-chart"></span> Temperatura (°C)</span>
                    </div>
                </div>
                <div class="chart-container">
                    <div class="y-axis">
                        <span>0</span><span>20</span><span>40</span><span>60</span><span>80</span><span>100</span>
                    </div>
                    <div class="chart-area" style="padding-left: 0;">
                        <svg viewBox="0 0 1000 150" preserveAspectRatio="none" class="chart-svg">
                            <polyline id="poly-t" class="line-red" points=""/>
                            <polyline id="poly-h" class="line-cyan" points=""/>
                        </svg>
                        <div class="x-axis">
                            <span>1</span><span>2</span><span>3</span><span>4</span><span>5</span><span>6</span><span>7</span><span>8</span><span>9</span><span>10</span><span>11</span><span>12</span><span>13</span><span>14</span><span>15</span><span>16</span><span>17</span><span>18</span><span>19</span><span>20</span>
                        </div>
                    </div>
                </div>
            </section>

            <section class="panel">
                <h2 class="panel-title">Configurações do Sistema</h2>
                <div class="config-grid">
                    <div class="config-box">
                        <h3 class="box-title">Limiares críticos</h3>
                        <div class="input-group">
                            <label>UMIDADE MÁXIMA (%)</label>
                            <input type="text" id="limite-umidade" value="">
                        </div>
                        <div class="input-group">
                            <label>TEMPERATURA MÁXIMA (°C)</label>
                            <input type="text" id="limite-temp" value="">
                        </div>
                        <div class="input-group">
                            <label>QUALIDADE DO AR MÁXIMA</label>
                            <input type="text" id="limite-ar" value="">
                        </div>
                        <button class="btn-save" id="btn-salvar">SALVAR CONFIGURAÇÕES</button>
                    </div>

                    <div class="config-box">
                        <h3 class="box-title">Controle de Atuadores</h3>
                        <p class="box-subtitle">Sobrescrever Automação</p>
                        <div class="toggle-group">
                            <button class="btn-toggle" id="btn-ventoinha" data-device="VENTOINHA">VENTOINHA OFF</button>
                            <button class="btn-toggle" id="btn-servo" data-device="JANELA">JANELA: FECHADA</button>
                        </div>
                    </div>

                    <div class="config-box">
                        <h3 class="box-title">Informações</h3>
                        <p class="box-subtitle">Tempo de funcionamento</p>
                        <div class="uptime-display" id="val-uptime">0m</div>
                    </div>
                </div>
            </section>
        </div>

        <div id="advanced-view" class="hidden">
            <section class="panel adv-panel">
                <div class="adv-header">
                    <div>
                        <h2 class="panel-title mb-0">GRÁFICO DE SENSORES TEMPORAL</h2>
                        <p class="panel-subtitle">Monitoramento contínuo em aba técnica</p>
                    </div>
                    <div class="chart-legend">
                        <span class="legend-item"><span class="dot-legend dot-cyan"></span> Umidade (%)</span>
                        <span class="legend-item"><span class="dot-legend dot-red-chart"></span> Temperatura (°C)</span>
                    </div>
                </div>
                <div class="chart-container">
                    <div class="y-axis">
                        <span>0</span><span>20</span><span>40</span><span>60</span><span>80</span><span>100</span>
                    </div>
                    <div class="chart-area" style="padding-left: 0;">
                        <svg viewBox="0 0 1000 150" preserveAspectRatio="none" class="chart-svg">
                            <polyline id="poly-t-adv" class="line-red" points=""/>
                            <polyline id="poly-h-adv" class="line-cyan" points=""/>
                        </svg>
                        <div class="x-axis">
                            <span>1</span><span>2</span><span>3</span><span>4</span><span>5</span><span>6</span><span>7</span><span>8</span><span>9</span><span>10</span><span>11</span><span>12</span><span>13</span><span>14</span><span>15</span><span>16</span><span>17</span><span>18</span><span>19</span><span>20</span>
                        </div>
                    </div>
                </div>
            </section>

            <section class="panel adv-panel">
                <h2 class="panel-title">ESTADO FÍSICO DA MCU</h2>
                <p class="panel-subtitle mb-4">Métricas de performance e alocação de memória</p>
                
                <div class="progress-item">
                    <div class="progress-labels">
                        <span class="prog-name">Núcleo 0 (Processamento Wi-Fi/Rede)</span>
                        <span class="prog-val text-cyan" id="core0-text">--%</span>
                    </div>
                    <div class="progress-track"><div class="progress-fill fill-cyan" id="core0-bar" style="width: 0%"></div></div>
                </div>

                <div class="progress-item">
                    <div class="progress-labels">
                        <span class="prog-name">Núcleo 1 (Loop Sensoriamento/Atuação)</span>
                        <span class="prog-val text-cyan" id="core1-text">--%</span>
                    </div>
                    <div class="progress-track"><div class="progress-fill fill-cyan" id="core1-bar" style="width: 0%"></div></div>
                </div>

                <div class="progress-item">
                    <div class="progress-labels">
                        <span class="prog-name">Interna: SRAM Livre (Heap RAM)</span>
                        <span class="prog-val text-white" id="heap-text">Calculando...</span>
                    </div>
                    <div class="progress-track"><div class="progress-fill fill-gray" id="heap-bar" style="width: 0%"></div></div>
                </div>
                
                <div class="progress-item">
                    <div class="progress-labels">
                        <span class="prog-name">Armazenamento Flash (LittleFS/History)</span>
                        <span class="prog-val text-purple" id="fs-text">Calculando...</span>
                    </div>
                    <div class="progress-track"><div class="progress-fill fill-purple" id="fs-bar" style="width: 0%"></div></div>
                </div>

                <div class="mcu-info-card">
                    <svg class="mcu-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2" ry="2"></rect><rect x="9" y="9" width="6" height="6"></rect><line x1="9" y1="1" x2="9" y2="4"></line><line x1="15" y1="1" x2="15" y2="4"></line><line x1="9" y1="20" x2="9" y2="23"></line><line x1="15" y1="20" x2="15" y2="23"></line><line x1="20" y1="9" x2="23" y2="9"></line><line x1="20" y1="14" x2="23" y2="14"></line><line x1="1" y1="9" x2="4" y2="9"></line><line x1="1" y1="14" x2="4" y2="14"></line></svg>
                    <div class="mcu-details">
                        <span class="mcu-title">ESP_WROOM_32</span>
                        <span class="mcu-sub">Frequência: 240MHz (WIFI_PS_MIN_MODEM)</span>
                    </div>
                </div>
            </section>

            <section class="panel adv-panel">
                <div class="adv-header">
                    <div>
                        <h2 class="panel-title mb-0">AGENDADOR FREERTOS (ACTIVE SCHEDULERS)</h2>
                        <p class="panel-subtitle">Dados de interface restritos ao Core do ESP-IDF</p>
                    </div>
                    <div class="ticks-info" id="rtos-ticks">Ticks: 0</div>
                </div>
                
                <table class="rtos-table">
                    <thead>
                        <tr>
                            <th>TASK NAME</th>
                            <th>CORE</th>
                            <th>STATE</th>
                            <th>PRIORITY</th>
                            <th>WATERMARK STACK</th>
                            <th>CPU UTILIZATION</th>
                        </tr>
                    </thead>
                    <tbody id="rtos-tbody">
                    </tbody>
                </table>
            </section>

            <section class="panel adv-panel" id="profiler-section">
                <h2 class="panel-title mb-1">TIME PROFILER DE FUNÇÕES (LATÊNCIA UNITÁRIA DE SOFTWARE)</h2>
                <div class="profiler-list" id="profiler-list">
                </div>
                <div class="alert-inline mt-4">
                    <i class="fa-solid fa-triangle-exclamation text-yellow"></i>
                    <span>A resolução de latência utiliza o timer de alta resolução `micros()` iterativo.</span>
                </div>
            </section>

            <section class="panel adv-panel">
                <div class="adv-header">
                    <h2 class="panel-title mb-0">MONITOR SERIAL DE DEPURAÇÃO</h2>
                    <div class="terminal-controls">
                        <input type="text" id="term-search" class="terminal-search" placeholder="Filtrar por texto...">
                        <label class="cb-container"><input type="checkbox" id="cb-info" checked><span class="cb-mark cb-info"></span> INFO</label>
                        <label class="cb-container"><input type="checkbox" id="cb-warn" checked><span class="cb-mark cb-warn"></span> WARN</label>
                        <label class="cb-container"><input type="checkbox" id="cb-err" checked><span class="cb-mark cb-err"></span> ERROR</label>
                        <button class="btn-export" id="btn-export"><i class="fa-solid fa-download"></i> Exportar CSV</button>
                        <button class="btn-export btn-flash" id="btn-flash"><i class="fa-solid fa-microchip"></i> Baixar Flash</button>
                    </div>
                </div>
                <p class="panel-subtitle mb-3">Logs capturados no buffer circular da RAM em tempo de execução</p>
                
                <div class="terminal-window">
                    <pre><code id="terminal-output"></code></pre>
                </div>
            </section>

            <div class="adv-bottom-grid">
                <section class="panel adv-panel" style="margin-bottom:0;">
                    <h2 class="panel-title">AJUSTES FÍSICOS DOS SENSORES</h2>
                    
                    <div class="slider-group">
                        <div class="slider-header">
                            <label>Frequência de Leitura dos Sensores</label>
                            <span class="slider-val text-cyan font-bold" id="val-slider-freq">2 segundos</span>
                        </div>
                        <input type="range" class="slider-cyan" id="slider-freq" min="1" max="60" value="2">
                        <p class="slider-help">Define a taxa de polling do microcontrolador nos barramentos I2C e OneWire.</p>
                    </div>

                    <div class="slider-group">
                        <div class="slider-header">
                            <label>Limiar Máximo de Umidade (Alerta)</label>
                            <span class="slider-val text-yellow font-bold" id="val-slider-umid">70 % UR</span>
                        </div>
                        <input type="range" class="slider-yellow" id="slider-umid" min="0" max="100" value="70">
                        <p class="slider-help">Se a umidade relativa superar o valor definido, o sistema inicia cálculo de mitigação.</p>
                    </div>

                    <div class="slider-group">
                        <div class="slider-header">
                            <label>Limiar de CO2 (Risco Biológico)</label>
                            <span class="slider-val text-red font-bold" id="val-slider-ar">1500 PPM</span>
                        </div>
                        <input type="range" class="slider-red" id="slider-ar" min="0" max="4095" value="1500">
                        <p class="slider-help">Níveis críticos indicam ambientes propícios à proliferação de fungos.</p>
                    </div>

                    <div class="manual-control-mt">
                        <label class="label-sm">CONTROLE MANUAL DOS GPIOS</label>
                        <div class="gpios-grid">
                            <button class="btn-gpio" id="btn-adv-ventoinha">VENTOINHA OFF</button>
                            <button class="btn-gpio" id="btn-adv-servo">JANELA: FECHADA</button>
                        </div>
                    </div>
                </section>

                <section class="panel adv-panel" style="margin-bottom:0;">
                    <h2 class="panel-title">MAPEAMENTO DINÂMICO DE WI-FI</h2>
                    <p class="panel-subtitle mb-4">Altere as credenciais do roteador diretamente na RAM do microcontrolador sem perder os dados flash gravados.</p>
                    
                    <div class="input-group-adv">
                        <label>SSID da Rede (Acesso do Cliente)</label>
                        <input type="text" id="wifi-ssid" placeholder="Ex: RedeCasa_2G">
                    </div>
                    
                    <div class="input-group-adv">
                        <label>Senha do Ponto de Acesso</label>
                        <input type="password" id="wifi-pass" value="">
                    </div>

                    <div class="alert-box-adv alert-yellow mt-4 mb-4">
                        <i class="fa-solid fa-triangle-exclamation text-yellow mr-2"></i>
                        <span class="alert-text-adv">Ao enviar, o ESP32 tentará realizar a reconexão. Se a conexão falhar, o Portal de Captive Access Point temporário será reaberto automaticamente.</span>
                    </div>

                    <button class="btn-reconnect w-100" id="btn-reconnect">RECONECTAR MÓDULO DE RÁDIO</button>
                </section>
            </div>
        </div>

    </main>

    <footer class="bottom-footer">
        <hr class="footer-divider">
        <p class="footer-text"><strong>Biopreserv v1.1.0</strong> | Otimizado para ESP32 e FreeRTOS</p>
        <p class="footer-text sub-text">Monitoramento preditivo e detecção de mofo</p>
    </footer>

    <div class="modal-overlay" id="sobre-modal">
        <div class="modal-box">
            <h2 class="modal-title">Sobre o Projeto</h2>
            <p class="modal-description">O BIOPRESERV é uma solução fundamentada na Internet das Coisas (IoT), projetada para monitorar continuamente dados ambientais.</p>
            <div class="modal-details">
                <div class="detail-row">
                    <div class="detail-label">Instituição:</div>
                    <div class="detail-value font-bold">PUCPR - Pontifícia Universidade Católica do Paraná</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Equipe de desenvolvimento:</div>
                    <div class="detail-value">
                        <ul>
                            <li>Gabriela Estefania Uzcategui Perez</li>
                            <li>Heitor Roberto Goncalves</li>
                            <li>João Pedro Vicentini Couto</li>
                            <li>Rafaella da Silva Butture</li>
                            <li>Yasmin Victória Farias Leal</li>
                        </ul>
                    </div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">E-mail de Contato:</div>
                    <div class="detail-value">
                        <ul>
                            <li>gabrielauzcategui81@gmail.com</li>
                            <li>heitorgoncalves142@gmail.com</li>
                            <li>joaolivro870@gmail.com</li>
                            <li>rafaeladasilvabutture@gmail.com</li>
                            <li>minvicflg@gmail.com</li>
                        </ul>
                    </div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Repositório Técnico:</div>
                    <div class="detail-value">https://github.com/DevYayaFarias/Biopreserv</div>
                </div>
            </div>
        </div>
    </div>
    
    <div class="modal-overlay" id="log-modal">
        <div class="modal-box">
            <h2 class="modal-title">Changelog do Sistema</h2>
            <div class="modal-details">
                <div class="detail-row">
                    <div class="detail-label">v1.1.0 (TDE)</div>
                    <div class="detail-value">
                        <ul>
                            <li>Adoção de arquitetura Multithread (FreeRTOS).</li>
                            <li>Gerenciamento de Energia configurado (WiFi Sleep).</li>
                            <li>Persistência Histórica no LittleFS (24h).</li>
                            <li>Streaming Assíncrono para Buffer JSON.</li>
                            <li>Endpoint Zero-Copy para Download da Flash.</li>
                            <li>Lógica de relé validada na bancada (Active High).</li>
                        </ul>
                    </div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">v1.0.0</div>
                    <div class="detail-value">
                        <ul>
                            <li>Implementação base com interface Web Async.</li>
                            <li>Sensores DHT22/MQ135 operacionais.</li>
                        </ul>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script src="/script.js"></script>
</body>
</html>
)rawliteral";

// ==============================================================================
// 7. FRONT-END: JAVASCRIPT 
// ==============================================================================
const char BIOPRESERV_JS[] PROGMEM = R"rawliteral(
document.addEventListener('DOMContentLoaded', () => {
    const modalSobre = document.getElementById('sobre-modal');
    const btnOpenSobre = document.getElementById('open-modal');
    if(btnOpenSobre && modalSobre) {
        btnOpenSobre.addEventListener('click', () => { modalSobre.classList.add('active'); });
        modalSobre.addEventListener('click', (e) => { if (e.target === modalSobre) { modalSobre.classList.remove('active'); } });
    }

    const modalLog = document.getElementById('log-modal');
    const btnOpenLog = document.getElementById('open-log');
    if(btnOpenLog && modalLog) {
        btnOpenLog.addEventListener('click', () => { modalLog.classList.add('active'); });
        modalLog.addEventListener('click', (e) => { if (e.target === modalLog) { modalLog.classList.remove('active'); } });
    }

    const saveButton = document.getElementById('btn-salvar');
    if(saveButton) {
        saveButton.addEventListener('click', () => {
            const um = document.getElementById('limite-umidade').value;
            const tp = document.getElementById('limite-temp').value;
            const ar = document.getElementById('limite-ar').value;
            const url = `/salvar?umidade=${um}&temp=${tp}&ar=${ar}`;

            const originalText = saveButton.textContent;
            saveButton.textContent = "GRAVANDO...";
            
            fetch(url, { method: 'POST' }).then(response => {
                if(response.ok) {
                    saveButton.textContent = "SALVO!";
                    saveButton.style.backgroundColor = "#00ff88";
                    saveButton.style.color = "#000";
                    setTimeout(() => {
                        saveButton.textContent = originalText;
                        saveButton.style.backgroundColor = "";
                        saveButton.style.color = "";
                    }, 2000);
                }
            });
        });
    }

    const toggleViewBtn = document.getElementById('toggle-view-btn');
    const toggleIconSvg = document.getElementById('toggle-icon-svg');
    const simpleView = document.getElementById('simple-view');
    const advancedView = document.getElementById('advanced-view');
    let isAdvanced = false;

    if(toggleViewBtn) {
        toggleViewBtn.addEventListener('click', () => {
            isAdvanced = !isAdvanced;
            if(isAdvanced) {
                simpleView.classList.add('hidden');
                advancedView.classList.remove('hidden');
                toggleIconSvg.style.transform = 'rotate(180deg)';
            } else {
                advancedView.classList.add('hidden');
                simpleView.classList.remove('hidden');
                toggleIconSvg.style.transform = 'rotate(0deg)';
            }
        });
    }

    const sendToggle = (route, nodeIds, prefix) => {
        fetch(route, { method: 'POST' }).then(res => res.text()).then(state => {
            document.querySelectorAll(nodeIds).forEach(btn => {
                btn.innerText = prefix + state;
                if(state === "ON" || state === "ABERTA") btn.classList.add('active');
                else btn.classList.remove('active');
            });
        });
    };
    
    document.querySelectorAll('#btn-ventoinha, #btn-adv-ventoinha').forEach(b => b.addEventListener('click', () => sendToggle('/toggle_ventoinha', '#btn-ventoinha, #btn-adv-ventoinha', 'VENTOINHA: ')));
    document.querySelectorAll('#btn-servo, #btn-adv-servo').forEach(b => b.addEventListener('click', () => sendToggle('/toggle_servo', '#btn-servo, #btn-adv-servo', 'JANELA: ')));

    const sendAdvConfig = () => {
        const f = document.getElementById('slider-freq').value;
        const u = document.getElementById('slider-umid').value;
        const a = document.getElementById('slider-ar').value;
        fetch(`/api/adv_config?freq=${f}&umid=${u}&ar=${a}`, {method: 'POST'});
    };

    ['freq', 'umid', 'ar'].forEach(id => {
        const el = document.getElementById('slider-'+id);
        if(el) {
            el.addEventListener('input', (e) => { 
                let sufix = id==='freq'?' segundos':(id==='umid'?' % UR':' PPM');
                document.getElementById('val-slider-'+id).innerText = e.target.value + sufix; 
            });
            el.addEventListener('change', sendAdvConfig);
        }
    });

    const btnWifi = document.getElementById('btn-reconnect');
    if(btnWifi) {
        btnWifi.addEventListener('click', () => {
            const s = document.getElementById('wifi-ssid').value;
            const p = document.getElementById('wifi-pass').value;
            btnWifi.innerText = "REINICIANDO...";
            fetch(`/api/wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`, {method: 'POST'})
            .then(() => setTimeout(() => location.reload(), 3000));
        });
    }

    let currentLogs = [];
    const renderLogs = () => {
        const termOut = document.getElementById('terminal-output');
        if(!termOut) return;
        const search = document.getElementById('term-search').value.toLowerCase();
        const showInfo = document.getElementById('cb-info').checked;
        const showWarn = document.getElementById('cb-warn').checked;
        const showErr = document.getElementById('cb-err').checked;
        
        termOut.innerHTML = '';
        currentLogs.forEach(l => {
            if(!showInfo && l.level === 'INFO') return;
            if(!showWarn && l.level === 'WARN') return;
            if(!showErr && l.level === 'ERROR') return;
            if(search && !l.msg.toLowerCase().includes(search)) return;
            let lvlClass = l.level === 'INFO' ? 'log-info' : (l.level === 'WARN' ? 'log-warn' : 'log-error');
            termOut.innerHTML += `<span class="log-time">[${l.time}]</span> <span class="${lvlClass}">${l.level}</span> <span class="log-msg">${l.msg}</span>\n`;
        });
        termOut.parentElement.scrollTop = termOut.parentElement.scrollHeight;
    };

    document.getElementById('term-search').addEventListener('input', renderLogs);
    ['cb-info', 'cb-warn', 'cb-err'].forEach(id => {
        let el = document.getElementById(id);
        if (el) el.addEventListener('change', renderLogs);
    });

    const btnExport = document.getElementById('btn-export');
    if(btnExport) {
        btnExport.addEventListener('click', () => {
            if(currentLogs.length === 0) return;
            let csv = "Time,Level,Message\n";
            currentLogs.forEach(l => csv += `${l.time},${l.level},${l.msg}\n`);
            let blob = new Blob([csv], {type: "text/csv;charset=utf-8;"});
            let url = window.URL.createObjectURL(blob);
            let a = document.createElement("a");
            a.style.display = "none";
            a.href = url;
            a.download = "biopreserv_logs.csv";
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);
        });
    }

    // Botão nativo para Baixar a Flash adicionado via JS
    const btnFlash = document.getElementById('btn-flash');
    if(btnFlash) {
        btnFlash.addEventListener('click', () => {
            window.open('/baixar_flash', '_blank');
        });
    }

    const maxPontos = 20; let histT = []; let histH = []; let cfgLoaded = false;

    setInterval(() => {
        fetch('/api/dados')
        .then(res => res.json())
        .then(data => {
            if(!cfgLoaded) {
                if(document.getElementById('limite-umidade')) document.getElementById('limite-umidade').value = data.limH;
                if(document.getElementById('limite-temp')) document.getElementById('limite-temp').value = data.limT;
                if(document.getElementById('limite-ar')) document.getElementById('limite-ar').value = data.limA;
                
                if(document.getElementById('slider-freq')) {
                    document.getElementById('slider-freq').value = data.freq; 
                    document.getElementById('val-slider-freq').innerText = data.freq + " segundos";
                    document.getElementById('slider-umid').value = data.limH; 
                    document.getElementById('val-slider-umid').innerText = data.limH + " % UR";
                    document.getElementById('slider-ar').value = data.limA; 
                    document.getElementById('val-slider-ar').innerText = data.limA + " PPM";
                }
                
                if(data.sta_ssid && document.getElementById('wifi-ssid')) document.getElementById('wifi-ssid').value = data.sta_ssid;
                cfgLoaded = true;
            }

            const safeSet = (id, txt, cls=null) => { const e=document.getElementById(id); if(e){ e.innerText=txt; if(cls) e.className="badge "+cls; } };

            if(data.pronto === "false") safeSet('status-geral', "AQUECENDO");
            else safeSet('status-geral', data.situacao_geral);

            safeSet('val-temp', data.t); safeSet('badge-temp', data.status_t, data.classe_t);
            safeSet('val-umid', data.h); safeSet('badge-umid', data.status_h, data.classe_h);
            safeSet('val-ar', data.aq); safeSet('badge-ar', data.status_aq, data.classe_aq);
            safeSet('badge-geral', data.status_geral, data.classe_geral);
            safeSet('val-uptime', data.uptime);
            if(document.getElementById('rtos-ticks')) document.getElementById('rtos-ticks').innerText = "Ticks: " + data.ticks;

            // Alertas Dinâmicos
            const alertSection = document.getElementById('dynamic-alerts-section');
            const alertContainer = document.getElementById('alerts-container');
            if(alertSection && alertContainer) {
                alertContainer.innerHTML = '';
                if(data.emg) {
                    alertSection.classList.remove('hidden');
                    alertContainer.innerHTML += `<div class="alert-box alert-red"><span class="dot dot-red"></span><span class="alert-text">PARADA DE EMERGÊNCIA ATIVADA VIA HARDWARE (GPIO 0).</span></div>`;
                } else if(data.status_geral === "PERIGO") {
                    alertSection.classList.remove('hidden');
                    if(data.alerta_t) alertContainer.innerHTML += `<div class="alert-box alert-yellow"><span class="dot dot-yellow"></span><span class="alert-text">Temperatura alta</span></div>`;
                    if(data.alerta_h) alertContainer.innerHTML += `<div class="alert-box alert-yellow"><span class="dot dot-yellow"></span><span class="alert-text">Umidade crítica</span></div>`;
                    if(data.alerta_aq) alertContainer.innerHTML += `<div class="alert-box alert-red"><span class="dot dot-red"></span><span class="alert-text">Qualidade do ar comprometida!</span></div>`;
                } else {
                    alertSection.classList.add('hidden');
                }
            }

            document.querySelectorAll('#btn-ventoinha, #btn-adv-ventoinha').forEach(b => { b.innerText="VENTOINHA: "+data.motor; if(data.motor==="ON") b.classList.add('active'); else b.classList.remove('active'); });
            document.querySelectorAll('#btn-servo, #btn-adv-servo').forEach(b => { b.innerText="JANELA: "+data.servo; if(data.servo==="ABERTA") b.classList.add('active'); else b.classList.remove('active'); });

            const hb = document.getElementById('heap-bar'); if(hb) { hb.style.width = data.heap_perc + "%"; document.getElementById('heap-text').innerText = data.heap_perc + "% de " + data.heap_total + "KB"; }
            const fb = document.getElementById('fs-bar'); if(fb) { fb.style.width = data.fs_perc + "%"; document.getElementById('fs-text').innerText = data.fs_perc + "% de " + data.fs_total + "KB"; }

            const c0b = document.getElementById('core0-bar'); if(c0b) { c0b.style.width = data.core0 + "%"; document.getElementById('core0-text').innerText = data.core0 + "%"; }
            const c1b = document.getElementById('core1-bar'); if(c1b) { c1b.style.width = data.core1 + "%"; document.getElementById('core1-text').innerText = data.core1 + "%"; }

            const tb = document.getElementById('rtos-tbody');
            if(tb && data.tasks) {
                tb.innerHTML = '';
                data.tasks.forEach(t => {
                    let badgeClass = t.state === 'Running' ? 'badge-running' : (t.state === 'Ready' ? 'badge-ready' : 'badge-blocked');
                    tb.innerHTML += `<tr><td class="font-mono">${t.name}</td><td>Core ${t.core}</td><td><span class="rtos-badge ${badgeClass}">${t.state}</span></td><td>${t.prio}</td><td>${t.stack}</td><td class="text-cyan">${t.cpu}</td></tr>`;
                });
            }

            const pl = document.getElementById('profiler-list');
            if(pl && data.profiler) {
                pl.innerHTML = '';
                data.profiler.forEach(p => {
                    let w = Math.min(100, (p.dur / 250000.0) * 100); if(w<2) w=2; 
                    pl.innerHTML += `<div class="profiler-item"><div class="prof-header"><span class="font-mono">${p.name}</span><span class="prof-dur">Dur: <span class="text-white font-bold">${p.dur} us</span></span></div><div class="progress-track track-dark"><div class="progress-fill fill-cyan" style="width: ${w}%"></div></div></div>`;
                });
            }

            if(data.logs && JSON.stringify(currentLogs) !== JSON.stringify(data.logs)) {
                currentLogs = data.logs; renderLogs();
            }

            histT.push(parseFloat(data.t)); if(histT.length > maxPontos) histT.shift();
            histH.push(parseFloat(data.h)); if(histH.length > maxPontos) histH.shift();

            let wSvg = 1000;
            let ptsT = histT.map((v, i) => `${(i * wSvg) / (maxPontos - 1)},${150 - (v / 50.0) * 150}`).join(' ');
            let ptsH = histH.map((v, i) => `${(i * wSvg) / (maxPontos - 1)},${150 - (v / 100.0) * 150}`).join(' ');

            ['poly-t','poly-t-adv'].forEach(id => { let e=document.getElementById(id); if(e) e.setAttribute('points', ptsT); });
            ['poly-h','poly-h-adv'].forEach(id => { let e=document.getElementById(id); if(e) e.setAttribute('points', ptsH); });

        }).catch(err => {
            console.error("ERRO NO JSON DA API:", err);
        });
    }, 2000);
});
)rawliteral";

// ==============================================================================
// 8. SETUP DO SISTEMA E ROTAS
// ==============================================================================

void setup() {
  Serial.begin(115200);

  xDataMutex = xSemaphoreCreateMutex();
  xLogMutex = xSemaphoreCreateMutex();

  WiFi.setSleep(WIFI_PS_MIN_MODEM);

  pinMode(MQ135_PIN, INPUT);
  
  digitalWrite(RELAY_PIN, RELAY_DESLIGA); 
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_DESLIGA); 
  
  windowServo.attach(SERVO_PIN);
  windowServo.write(SERVO_FECHADO); 

  pinMode(BTN_EMERGENCIA, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_EMERGENCIA), isrEmergency, FALLING);
  
  dht.begin();
  if(!LittleFS.begin(true)){ Serial.println("SPIFFS Mount Failed"); }

  preferences.begin("biopreserv", false);
  limUmidade = preferences.getFloat("limH", 70.0);
  limTemp = preferences.getFloat("limT", 25.0);
  limAr = preferences.getInt("limA", 1500);
  freqLeitura = preferences.getInt("freq", 2);
  sta_ssid = preferences.getString("sta_ssid", "");
  sta_pass = preferences.getString("sta_pass", "");

  WiFi.softAP("BIOPRESERV", "12345678");

  addSystemLog("INFO", "Sistema Biopreserv Inicializado na MCU.");

  if(sta_ssid.length() > 0) {
      addSystemLog("INFO", "Tentando reconectar STA em: " + sta_ssid);
      WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
  }

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/css", BIOPRESERV_CSS); });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "application/javascript", BIOPRESERV_JS); });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", INDEX_HTML); });

  // === ENDPOINT DE EXTRAÇÃO DO LITTLEFS ===
  server.on("/baixar_flash", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/history.csv")) {
      request->send(LittleFS, "/history.csv", "text/csv", true);
    } else {
      request->send(404, "text/plain", "Arquivo history.csv nao encontrado na Flash.");
    }
  });

  // === MOTOR JSON BLINDADO (STREAMING ASSÍNCRONO) ===
  server.on("/api/dados", HTTP_GET, [](AsyncWebServerRequest *request){
    unsigned long startNet = micros();
    
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{");
    
    if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, pdMS_TO_TICKS(50))) {
        response->printf("\"pronto\":\"%s\",", primeiraLeitura ? "true" : "false");
        response->printf("\"emg\":%s,", emergencyStop ? "true" : "false");
        response->printf("\"t\":\"%.1f\",", t);
        response->printf("\"h\":\"%.0f\",", h);
        response->printf("\"aq\":\"%d\",", gas);
        
        bool riscoT = t >= limTemp; 
        bool riscoH = h >= limUmidade; 
        bool riscoAq = gas >= limAr; 
        bool riscoGeral = riscoT || riscoH || riscoAq;

        response->printf("\"status_t\":\"%s\",\"classe_t\":\"%s\",", riscoT ? "ALTA" : "NORMAL", riscoT ? "badge-warning" : "badge-normal");
        response->printf("\"status_h\":\"%s\",\"classe_h\":\"%s\",", riscoH ? "ALTA" : "NORMAL", riscoH ? "badge-warning" : "badge-normal");
        response->printf("\"status_aq\":\"%s\",\"classe_aq\":\"%s\",", riscoAq ? "POLUIDO" : "NORMAL", riscoAq ? "badge-danger" : "badge-normal");

        response->printf("\"alerta_t\":%s,", riscoT ? "true" : "false");
        response->printf("\"alerta_h\":%s,", riscoH ? "true" : "false");
        response->printf("\"alerta_aq\":%s,", riscoAq ? "true" : "false");

        response->printf("\"status_geral\":\"%s\",", riscoGeral ? "PERIGO" : "ESTÁVEL");
        response->printf("\"situacao_geral\":\"%s\",", riscoGeral ? "MOFO DETECTADO" : "SISTEMA SEGURO");
        response->printf("\"classe_geral\":\"%s\",", riscoGeral ? "badge-danger" : "badge-normal");
        
        response->printf("\"motor\":\"%s\",", ventoinhaLigada ? "ON" : "OFF");
        response->printf("\"servo\":\"%s\",", janelaAberta ? "ABERTA" : "FECHADA");
        
        xSemaphoreGive(xDataMutex);
    } else {
        response->print("\"pronto\":\"false\",\"emg\":false,\"t\":\"0.0\",\"h\":\"0\",\"aq\":\"0\",");
        response->print("\"status_t\":\"NORMAL\",\"classe_t\":\"badge-normal\",\"status_h\":\"NORMAL\",\"classe_h\":\"badge-normal\",\"status_aq\":\"NORMAL\",\"classe_aq\":\"badge-normal\",");
        response->print("\"alerta_t\":false,\"alerta_h\":false,\"alerta_aq\":false,");
        response->print("\"status_geral\":\"BLOQUEADO\",\"situacao_geral\":\"AGUARDANDO MUTEX\",\"classe_geral\":\"badge-normal\",\"motor\":\"OFF\",\"servo\":\"FECHADA\",");
    }
    
    int safeFreq = (freqLeitura > 0) ? freqLeitura : 1; 
    response->printf("\"limH\":\"%.0f\",\"limT\":\"%.0f\",\"limA\":\"%d\",\"freq\":\"%d\",\"sta_ssid\":\"%s\",", limUmidade, limTemp, limAr, safeFreq, sta_ssid.c_str());

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    int heapPerc = (totalHeap > 0) ? (100 - ((freeHeap * 100) / totalHeap)) : 0; 
    
    response->printf("\"heap_perc\":\"%d\",\"heap_total\":\"%lu\",", heapPerc, totalHeap / 1024);
    
    uint32_t fsUsed = LittleFS.usedBytes();
    uint32_t fsTotal = LittleFS.totalBytes();
    int fsPerc = (fsTotal > 0) ? ((fsUsed * 100) / fsTotal) : 0; 
    
    response->printf("\"fs_perc\":\"%d\",\"fs_total\":\"%lu\",", fsPerc, fsTotal / 1024);

    // CPU NET LOAD APPROXIMATION
    int netLoad = (lastTimeNet * 100) / 2000000; 
    if (netLoad == 0 && lastTimeNet > 0) netLoad = 1;

    response->printf("\"uptime\":\"%lum\",\"core0\":\"%d\",\"core1\":\"%d\",\"ticks\":\"%lu\",", millis() / 60000, netLoad, core1Load, (unsigned long)xTaskGetTickCount());

    response->print("\"tasks\": [");
    if(TaskSystemHandle != NULL) {
      response->printf("{\"name\":\"vTaskSystem()\", \"core\":\"1\", \"state\":\"Active\", \"prio\":\"%u\", \"stack\":\"%luB\", \"cpu\":\"%d%%\"}", 
          (unsigned int)uxTaskPriorityGet(TaskSystemHandle), 
          (unsigned long)uxTaskGetStackHighWaterMark(TaskSystemHandle), 
          core1Load);
      if(TaskPersistHandle != NULL) response->print(",");
    }
    if(TaskPersistHandle != NULL) {
      response->printf("{\"name\":\"vTaskPersist()\", \"core\":\"0\", \"state\":\"Active\", \"prio\":\"%u\", \"stack\":\"%luB\", \"cpu\":\"%d%%\"},", 
          (unsigned int)uxTaskPriorityGet(TaskPersistHandle), 
          (unsigned long)uxTaskGetStackHighWaterMark(TaskPersistHandle), 
          timeFS > 0 ? 1 : 0);
    }
    response->printf("{\"name\":\"AsyncWebServer\", \"core\":\"0\", \"state\":\"Active\", \"prio\":\"%u\", \"stack\":\"%luB\", \"cpu\":\"%d%%\"}", 
        (unsigned int)uxTaskPriorityGet(NULL), 
        (unsigned long)uxTaskGetStackHighWaterMark(NULL), netLoad);
    response->print("],");

    timeNet = micros() - startNet;
    lastTimeNet = timeNet; // Atualiza a variável global para a próxima amostragem
    
    response->print("\"profiler\": [");
    response->printf("{\"name\":\"1. dht.read() [I2C Poll]\", \"dur\":\"%lu\"},", timeDHT);
    response->printf("{\"name\":\"2. analogRead() [ADC Core]\", \"dur\":\"%lu\"},", timeMQ);
    response->printf("{\"name\":\"3. evaluateAutomatedLogic()\", \"dur\":\"%lu\"},", timeLogic);
    response->printf("{\"name\":\"4. LittleFS.printf() [Flash]\", \"dur\":\"%lu\"},", timeFS);
    response->printf("{\"name\":\"5. AsyncTCP Build [Network]\", \"dur\":\"%lu\"}", timeNet);
    response->print("]"); 

    if (xLogMutex != NULL && xSemaphoreTake(xLogMutex, pdMS_TO_TICKS(50))) {
      response->print(",\"logs\": ["); 
      int startIdx = (logCount == MAX_LOGS) ? logIndex : 0;
      for(int i = 0; i < logCount; i++) {
          int idx = (startIdx + i) % MAX_LOGS;
          response->print(sysLogs[idx]);
          if(i < logCount - 1) response->print(",");
      }
      response->print("]");
      xSemaphoreGive(xLogMutex);
    }
    
    response->print("}");
    request->send(response);
  });

  server.on("/salvar", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("umidade")) { pendH = request->getParam("umidade")->value().toFloat(); }
    if (request->hasParam("temp")) { pendT = request->getParam("temp")->value().toFloat(); }
    if (request->hasParam("ar")) { pendA = request->getParam("ar")->value().toInt(); }
    savePending = true; 
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/adv_config", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("freq")) { pendFreq = request->getParam("freq")->value().toInt(); }
    saveAdvPending = true;
    request->send(200, "text/plain", "OK");
  });

  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request){
    if(request->hasParam("ssid")) { preferences.putString("sta_ssid", request->getParam("ssid")->value()); }
    if(request->hasParam("pass")) { preferences.putString("sta_pass", request->getParam("pass")->value()); }
    addSystemLog("WARN", "Credenciais de Wi-Fi alteradas. O ESP32 ira reiniciar.");
    request->send(200, "text/plain", "OK");
    rebootPending = true;
    rebootTimer = millis();
  });

  server.on("/toggle_ventoinha", HTTP_POST, [](AsyncWebServerRequest *request){
    if(emergencyStop) { request->send(200, "text/plain", "OFF"); return; }
    if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
        modoManual = true; 
        ventoinhaLigada = !ventoinhaLigada;
        digitalWrite(RELAY_PIN, ventoinhaLigada ? RELAY_LIGA : RELAY_DESLIGA);
        xSemaphoreGive(xDataMutex);
    }
    addSystemLog("WARN", "Override Manual: VENTOINHA " + String(ventoinhaLigada ? "ON" : "OFF"));
    request->send(200, "text/plain", ventoinhaLigada ? "ON" : "OFF");
  });

  server.on("/toggle_servo", HTTP_POST, [](AsyncWebServerRequest *request){
    if(emergencyStop) { request->send(200, "text/plain", "FECHADA"); return; }
    if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
        modoManual = true; 
        janelaAberta = !janelaAberta;
        windowServo.write(janelaAberta ? SERVO_ABERTO : SERVO_FECHADO);
        xSemaphoreGive(xDataMutex);
    }
    addSystemLog("WARN", "Override Manual: JANELA " + String(janelaAberta ? "ABERTA" : "FECHADA"));
    request->send(200, "text/plain", janelaAberta ? "ABERTA" : "FECHADA");
  });

  server.begin();

  xTaskCreatePinnedToCore(vTaskSystem, "SystemTasks", 4096, NULL, 1, &TaskSystemHandle, 1);
  xTaskCreatePinnedToCore(vTaskPersist, "FS_Save", 4096, NULL, 1, &TaskPersistHandle, 0);
}

// ==============================================================================
// 9. LOOP MATEMÁTICO E TASKS RTOS
// ==============================================================================

void vTaskSystem(void *pvParameters) {
  for(;;) {
    unsigned long processStart = micros();
    int safeFreq = (freqLeitura >= 2) ? freqLeitura : 2; // Trava física contra Crash do DHT22

    if (millis() - ultimaLeitura > (safeFreq * 1000) || ultimaLeitura == 0) {
      ultimaLeitura = millis();
      
      unsigned long localStart = micros();
      float novaUmidade = dht.readHumidity();
      float novaTemperatura = dht.readTemperature();
      timeDHT = micros() - localStart;

      localStart = micros();
      int novoGas = analogRead(MQ135_PIN);
      timeMQ = micros() - localStart;

      if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
        gas = novoGas;
        primeiraLeitura = true; 

        static bool lastError = false;
        if (!isnan(novaUmidade) && !isnan(novaTemperatura)) {
          h = novaUmidade;
          t = novaTemperatura;
          if (lastError) {
            addSystemLog("INFO", "Barramento DHT22 restabelecido.");
            lastError = false;
          }
        } else {
          if (!lastError) {
            addSystemLog("ERROR", "Falha de comunicacao com o DHT22.");
            lastError = true;
          }
        }
        xSemaphoreGive(xDataMutex);
      }

      localStart = micros();
      static bool lastRisco = false;

      if (emergencyStop) {
         if(ventoinhaLigada || janelaAberta) {
             if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
                 ventoinhaLigada = false; janelaAberta = false;
                 digitalWrite(RELAY_PIN, RELAY_DESLIGA); 
                 windowServo.write(SERVO_FECHADO);
                 xSemaphoreGive(xDataMutex);
             }
             addSystemLog("ERROR", "HARDWARE ISR: Parada de Emergencia acionada (GPIO 0).");
         }
      } else if (!modoManual) {
        if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
            alertaUmidade = (h >= limUmidade) ? true : (h <= (limUmidade - 2.0) ? false : alertaUmidade);
            alertaTemperatura = (t >= limTemp) ? true : (t <= (limTemp - 1.0) ? false : alertaTemperatura);
            alertaAr = (gas >= limAr) ? true : (gas <= (limAr - 50) ? false : alertaAr);

            bool riscoGeral = alertaUmidade || alertaTemperatura || alertaAr;

            if (riscoGeral != lastRisco) {
              if (riscoGeral) addSystemLog("WARN", "Condicao propicia a fungos detectada. Mitigacao ON.");
              else addSystemLog("INFO", "Ambiente estabilizado e seguro. Mitigacao OFF.");
              lastRisco = riscoGeral;
            }

            if (riscoGeral && (!ventoinhaLigada || !janelaAberta)) {
              ventoinhaLigada = true;
              janelaAberta = true;
              digitalWrite(RELAY_PIN, RELAY_LIGA);   
              windowServo.write(SERVO_ABERTO);                 
            } 
            else if (!riscoGeral && (ventoinhaLigada || janelaAberta)) { 
              ventoinhaLigada = false;
              janelaAberta = false;
              digitalWrite(RELAY_PIN, RELAY_DESLIGA); 
              windowServo.write(SERVO_FECHADO);                   
            }
            xSemaphoreGive(xDataMutex);
        }
      }
      timeLogic = micros() - localStart;
      
      unsigned long processEnd = micros();
      unsigned long elapsed = processEnd - processStart;
      core1Load = 1 + ((elapsed * 100) / (safeFreq * 1000000));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void vTaskPersist(void *pvParameters) {
  for(;;) {
    unsigned long localS = micros();
    if(primeiraLeitura && !emergencyStop) {
       File file = LittleFS.open("/history.csv", FILE_APPEND);
       if(file) {
         if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
            file.printf("%lu,%.1f,%.0f,%d\n", millis()/1000, t, h, gas);
            xSemaphoreGive(xDataMutex);
         }
         file.close();
       }
    }
    timeFS = micros() - localS;
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

void loop() {
  if (rebootPending && (millis() - rebootTimer >= 1000)) {
    ESP.restart();
  }

  if (savePending) {
    if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
        limUmidade = pendH; 
        limTemp = pendT; 
        limAr = pendA;
        preferences.putFloat("limH", limUmidade);
        preferences.putFloat("limT", limTemp);
        preferences.putInt("limA", limAr);
        alertaUmidade = false; alertaTemperatura = false; alertaAr = false;
        modoManual = false; ultimaLeitura = 0; 
        xSemaphoreGive(xDataMutex);
    }
    addSystemLog("INFO", "Novos limites configurados.");
    savePending = false;
  }
  
  if (saveAdvPending) {
    if (xDataMutex != NULL && xSemaphoreTake(xDataMutex, portMAX_DELAY)) {
        freqLeitura = pendFreq;
        preferences.putInt("freq", freqLeitura);
        modoManual = false; ultimaLeitura = 0;
        xSemaphoreGive(xDataMutex);
    }
    addSystemLog("INFO", "Ajustes fisicos atualizados via painel.");
    saveAdvPending = false;
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));
}