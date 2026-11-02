#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>

// ================= YOUR SETTINGS =================

// Enter your working hotspot credentials.
// Leave blank to use the direct BikeSpeed Wi-Fi network only.
const char* HOTSPOT_SSID = "Meatball-Sub";
const char* HOTSPOT_PASSWORD = "JwAwRw3*";

const char* BACKUP_SSID = "BikeSpeed";
const char* BACKUP_PASSWORD = "WheelSpeed29";
const char* HOSTNAME = "bikespeed";

// IMPORTANT: Rewire the gyro:
// SDA -> D4
// SCL -> D5
constexpr int SDA_PIN = D4;
constexpr int SCL_PIN = D5;

// Battery divider junction -> D2.
//
// Battery + -> 100k resistor -> D2 -> 100k resistor -> GND
// Add a 0.1 uF capacitor between D2 and GND.
//
// D2 is GPIO4 / ADC1 on the XIAO ESP32-C3.
constexpr int BATTERY_PIN = D2;

// Set false if the battery divider is not installed.
constexpr bool BATTERY_MONITOR_ENABLED = true;

// Two equal resistors divide the voltage by two.
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;

// Optional correction after checking against a multimeter.
// New correction = current correction * actual voltage / displayed voltage.
constexpr float BATTERY_CALIBRATION = 1.0f;

constexpr uint32_t BATTERY_SAMPLE_MS = 50;
constexpr int BATTERY_SAMPLES = 20;

// Estimated circumference of a 29-inch wheel.
// Replace with measured rollout in meters for better accuracy.
constexpr double CIRCUMFERENCE_M =
    29.0 * 0.0254 * 3.141592653589793;

constexpr double METERS_PER_MILE = 1609.344;

// Gyro sensitivity at +/-2000 degrees/second.
constexpr float COUNTS_PER_DPS = 16.4f;

// Very slow rotation below this threshold is ignored.
constexpr float ZERO_THRESHOLD_DPS = 2.0f;

constexpr uint32_t SAMPLE_MS = 10;
constexpr uint32_t SAVE_INTERVAL_MS = 300000;
constexpr int CALIBRATION_SAMPLES = 300;

// ================= STATE =================

WebServer server(80);
Preferences preferences;

uint8_t sensorAddress = 0x68;

bool sensorOK = false;
bool sampleOK = false;
bool calibrating = false;
bool nearLimit = false;

float bias[3] = {0, 0, 0};
double calibrationSum[3] = {0, 0, 0};
int calibrationCount = 0;

float filteredDps = 0;
uint32_t lastSample = 0;
uint32_t lastGoodSample = 0;

bool integrationValid = false;
double previousMps = 0;
uint32_t previousSpeedTime = 0;

double tripMeters = 0;
double lifetimeMeters = 0;
double savedLifetimeMeters = 0;

uint64_t rideMilliseconds = 0;
uint64_t movingMilliseconds = 0;

bool rideRunning = false;
uint32_t lastTimerUpdate = 0;

bool distanceIncomplete = false;
bool storageOK = false;
bool saveFailed = false;
uint32_t lastSaveAttempt = 0;

bool backupStarted = false;
bool previouslyConnected = false;
bool mdnsStarted = false;
bool retryPending = false;

uint32_t lastConnectAttempt = 0;
uint32_t retryStarted = 0;
uint32_t lastBackupAttempt = 0;
uint32_t lastMdnsAttempt = 0;

String advertisedIP;

// Battery state.
bool batteryValid = false;
float batteryVolts = 0;

uint32_t lastBatterySample = 0;
uint32_t batterySumMv = 0;
int batterySampleCount = 0;

// ================= DASHBOARD =================

const char DASHBOARD[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ride dashboard</title>

<style>
:root {
  color-scheme: dark;
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  background: #0b1220;
  color: #eef4ff;
  font-family: system-ui, sans-serif;
}

main {
  max-width: 560px;
  margin: auto;
  padding: 24px 18px 40px;
}

header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
  color: #b8c8df;
}

.battery {
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 13px;
  color: #9baec9;
}

.battery-icon {
  width: 30px;
  height: 15px;
  border: 2px solid currentColor;
  border-radius: 3px;
  padding: 2px;
  position: relative;
}

.battery-icon::after {
  content: "";
  position: absolute;
  right: -5px;
  top: 3px;
  width: 3px;
  height: 5px;
  background: currentColor;
  border-radius: 0 2px 2px 0;
}

#batteryFill {
  height: 100%;
  width: 0;
  background: currentColor;
  border-radius: 1px;
}

#status {
  text-align: center;
  color: #fbbf24;
  min-height: 24px;
  margin: 16px 0;
}

.card {
  background: #152238;
  border: 1px solid #26374f;
  border-radius: 20px;
  padding: 20px 12px;
  text-align: center;
}

.hero {
  margin-bottom: 12px;
}

#speed {
  font-size: clamp(76px, 23vw, 112px);
  font-weight: 750;
  line-height: 1.15;
}

.unit,
.label {
  color: #9baec9;
}

.unit {
  font-size: 22px;
}

.label {
  margin-top: 6px;
  font-size: 14px;
}

.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}

.value {
  font-size: 28px;
}

.number {
  font-variant-numeric: tabular-nums;
}

button {
  width: 100%;
  border: 0;
  border-radius: 12px;
  padding: 15px;
  margin-top: 12px;
  background: #38bdf8;
  color: #071422;
  font: 700 15px system-ui;
  cursor: pointer;
}

button.secondary {
  background: #263b58;
  color: #eef4ff;
}

button.danger {
  background: #542936;
  color: #ffdce3;
}

button:disabled {
  opacity: .45;
  cursor: default;
}

.note {
  color: #9baec9;
  font-size: 13px;
  line-height: 1.6;
  text-align: center;
}

#message {
  min-height: 24px;
  color: #7dd3fc;
  text-align: center;
}

#warning {
  color: #fbbf24;
}
</style>
</head>

<body>
<main>
  <header>
    <div id="battery" class="battery"
         title="Voltage-based estimate; charging and load affect it.">
      <div class="battery-icon" aria-hidden="true">
        <div id="batteryFill"></div>
      </div>

      <span id="batteryText" class="number" role="status">
        Battery: —
      </span>
    </div>

    <div id="clock" class="number"></div>
  </header>

  <div id="status">Connecting...</div>

  <div class="card hero">
    <div id="speed" class="number">—</div>
    <div class="unit">mph</div>
  </div>

  <div class="grid">
    <div class="card">
      <div id="kmh" class="value number">—</div>
      <div class="label">km/h</div>
    </div>

    <div class="card">
      <div id="rpm" class="value number">—</div>
      <div class="label">RPM</div>
    </div>

    <div class="card">
      <div id="trip" class="value number">—</div>
      <div class="label">Trip miles</div>
    </div>

    <div class="card">
      <div id="total" class="value number">—</div>
      <div class="label">Lifetime odometer · miles</div>
    </div>

    <div class="card">
      <div id="ride" class="value number">—</div>
      <div class="label">
        Ride time · <span id="timerState">stopped</span>
      </div>
    </div>

    <div class="card">
      <div id="moving" class="value number">—</div>
      <div class="label">Moving time</div>
    </div>
  </div>

  <div class="grid">
    <button id="start" disabled onclick="act('start')">
      Start Ride Timer
    </button>

    <button id="stop" class="secondary" disabled
            onclick="act('stop')">
      Stop Ride Timer
    </button>
  </div>

  <p class="note">
    Start resumes the ride timer. Stop pauses it.
    Mileage and moving time continue tracking wheel movement.
  </p>

  <p class="note">
    Battery percentage is a rough voltage estimate.
    USB charging affects the reading; charging status is not detected.
  </p>

  <p id="warning" class="note"></p>
  <p id="storage" class="note">Waiting for mileage status...</p>

  <button id="save" disabled onclick="act('save')">
    Save Mileage
  </button>

  <button id="calibrate" class="secondary" disabled
          onclick="act('calibrate')">
    Calibrate
  </button>

  <div class="grid">
    <button id="reset" class="secondary" disabled
            onclick="act('reset')">
      Reset Trip
    </button>

    <button id="resetodo" class="danger" disabled
            onclick="act('resetodo')">
      Reset Odometer
    </button>
  </div>

  <p id="message" role="status"></p>

  <p class="note">
    Keep the wheel still during calibration.
    Trip and timers reset when power is lost.
    Lifetime mileage saves every five minutes.
    Stop the wheel and press Save Mileage before switching off.
  </p>
</main>

<script>
const el = id => document.getElementById(id);

let lastContact = 0;
let busy = false;
let lastData = null;
let actionVersion = 0;

function updateClock() {
  el('clock').textContent = new Date().toLocaleTimeString([], {
    hour: 'numeric',
    minute: '2-digit',
    second: '2-digit'
  });
}

updateClock();
setInterval(updateClock, 1000);

function duration(seconds) {
  seconds = Math.floor(seconds);

  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const remainingSeconds = seconds % 60;

  return [hours, minutes, remainingSeconds]
    .map(value => String(value).padStart(2, '0'))
    .join(':');
}

function updateButtons() {
  const online =
    !!lastData && Date.now() - lastContact <= 3000;

  el('save').disabled =
    busy || !online || !lastData.storage;

  el('calibrate').disabled =
    busy || !online || !lastData.sensor || lastData.calibrating;

  el('reset').disabled =
    busy || !online;

  el('resetodo').disabled =
    busy || !online || !lastData.storage;

  el('start').disabled =
    busy || !online || lastData.rideRunning;

  el('stop').disabled =
    busy || !online || !lastData.rideRunning;
}

function clearBattery() {
  el('batteryText').textContent = 'Battery: —';
  el('batteryFill').style.width = '0%';
  el('battery').style.color = '#9baec9';
}

function renderBattery(d) {
  if (!d.batteryValid ||
      !Number.isFinite(d.batteryV) ||
      !Number.isFinite(d.batteryPct)) {
    clearBattery();
    return;
  }

  const percent = Math.max(0, Math.min(100, d.batteryPct));
  const low = d.batteryV <= 3.5;

  el('batteryText').textContent =
    '~' + percent + '% · ' +
    d.batteryV.toFixed(2) + ' V' +
    (low ? ' · LOW' : '');

  el('batteryFill').style.width = percent + '%';

  el('battery').style.color = low
    ? '#f87171'
    : percent <= 25
      ? '#fbbf24'
      : '#4ade80';
}

function clearSpeed(message) {
  for (const id of ['speed', 'kmh', 'rpm']) {
    el(id).textContent = '—';
  }

  el('status').textContent = message;
  el('status').style.color = '#fbbf24';
}

function offline() {
  lastData = null;

  clearBattery();
  clearSpeed('Connection lost — reconnecting...');

  for (const id of [
    'trip', 'total', 'ride', 'moving', 'timerState'
  ]) {
    el(id).textContent = '—';
  }

  el('storage').textContent = 'Mileage status unavailable';
  el('warning').textContent = '';

  updateButtons();
}

async function request(path, options = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 3000);

  try {
    const response = await fetch(path, {
      ...options,
      cache: 'no-store',
      signal: controller.signal
    });

    const text = await response.text();

    if (!response.ok) {
      throw new Error(text || 'Request failed');
    }

    return text;
  } finally {
    clearTimeout(timeout);
  }
}

function render(d) {
  lastData = d;
  lastContact = Date.now();

  renderBattery(d);

  if (d.ready) {
    el('speed').textContent = d.mph.toFixed(1);
    el('kmh').textContent = d.kmh.toFixed(1);
    el('rpm').textContent = d.rpm.toFixed(0);

    el('status').textContent = 'Live';
    el('status').style.color = '#4ade80';
  } else {
    clearSpeed(d.status);
  }

  el('trip').textContent = d.tripMi.toFixed(3);
  el('total').textContent = d.totalMi.toFixed(3);
  el('ride').textContent = duration(d.rideSec);
  el('moving').textContent = duration(d.movingSec);

  el('timerState').textContent =
    d.rideRunning ? 'running' : 'stopped';

  el('warning').textContent = d.incomplete
    ? 'Distance may be incomplete due to calibration, sensor errors, or measurement gaps.'
    : '';

  if (!d.storage) {
    el('storage').textContent =
      'Flash unavailable: lifetime mileage is RAM only.';
  } else if (d.saveFailed) {
    el('storage').textContent =
      'Last flash write failed. Retry Save Mileage before switching off.';
  } else if (d.unsavedMi > 0.000001) {
    el('storage').textContent =
      d.unsavedMi.toFixed(3) +
      ' miles pending save · autosaves every 5 minutes';
  } else {
    el('storage').textContent = 'Lifetime mileage saved';
  }

  updateButtons();
}

async function poll() {
  const version = actionVersion;

  try {
    if (!busy) {
      const text = await request('/api');

      if (!busy && version === actionVersion) {
        render(JSON.parse(text));
      }
    }
  } catch (error) {
    if (!busy && version === actionVersion) {
      offline();
    }
  } finally {
    setTimeout(poll, 250);
  }
}

async function act(action) {
  if (busy) return;

  const confirmations = {
    calibrate:
      'CALIBRATE SENSOR?\n\n' +
      'Stop the wheel and keep it completely still for 3 seconds.\n\n' +
      'Distance counting pauses during calibration.',

    reset:
      'RESET CURRENT TRIP?\n\n' +
      'This clears trip mileage, ride time, and moving time, ' +
      'and stops the ride timer.\n\n' +
      'Lifetime mileage will NOT change.\n' +
      'This cannot be undone.',

    resetodo:
      'PERMANENTLY RESET ODOMETER?\n\n' +
      'This erases ALL lifetime mileage, including the value ' +
      'saved in flash memory.\n\n' +
      'Trip mileage and timers will NOT change.\n' +
      'This cannot be undone, even by restarting.'
  };

  if (confirmations[action] &&
      !confirm(confirmations[action])) {
    return;
  }

  busy = true;
  actionVersion++;
  updateButtons();

  el('message').textContent = 'Working...';

  try {
    el('message').textContent = await request('/' + action, {
      method: 'POST'
    });

    render(JSON.parse(await request('/api')));
  } catch (error) {
    offline();

    el('message').textContent =
      'Could not confirm the action: ' + error.message +
      '. Check the displayed values before retrying.';
  } finally {
    busy = false;
    updateButtons();
  }
}

setInterval(() => {
  if (!busy && Date.now() - lastContact > 3000) {
    offline();
  }
}, 500);

poll();
</script>
</body>
</html>
)HTML";

// ================= SENSOR COMMUNICATION =================

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);
  Wire.write(value);

  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* data, uint8_t count) {
  Wire.beginTransmission(sensorAddress);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  size_t received = Wire.requestFrom(sensorAddress, count);

  if (received != count) {
    while (Wire.available()) {
      Wire.read();
    }

    return false;
  }

  for (uint8_t i = 0; i < count; i++) {
    data[i] = Wire.read();
  }

  return true;
}

bool readGyro(int16_t raw[3]) {
  uint8_t data[6];

  if (!readRegisters(0x43, data, 6)) {
    return false;
  }

  for (int i = 0; i < 3; i++) {
    raw[i] = (int16_t)(
      ((uint16_t)data[2 * i] << 8) |
      data[2 * i + 1]
    );
  }

  return true;
}

bool initializeSensor() {
  const uint8_t addresses[] = {0x68, 0x69, 0x70};
  bool found = false;

  for (uint8_t address : addresses) {
    sensorAddress = address;

    uint8_t identity = 0;

    if (!readRegisters(0x75, &identity, 1)) {
      continue;
    }

    Serial.printf(
      "Sensor address: 0x%02X, identity: 0x%02X\n",
      address,
      identity
    );

    // MPU-6500, MPU-9250, MPU-9255.
    if (identity == 0x70 ||
        identity == 0x71 ||
        identity == 0x73) {
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
    Serial.println("Sensor can't be found");
  }

  // Reset device.
  if (!writeRegister(0x6B, 0x80)) return false;
  delay(150);

  // Wake up and select clock.
  if (!writeRegister(0x6B, 0x01)) return false;

  // Enable sensor axes.
  if (!writeRegister(0x6C, 0x00)) return false;

  // Gyro low-pass filter.
  if (!writeRegister(0x1A, 0x03)) return false;

  // Gyro range +/-2000 degrees/second.
  if (!writeRegister(0x1B, 0x18)) return false;

  // 100 Hz sample rate.
  if (!writeRegister(0x19, 9)) return false;

  delay(500);
  return true;
}

// ================= MILEAGE AND TIMERS =================

void updateRideTimer() {
  uint32_t now = millis();

  if (rideRunning) {
    rideMilliseconds += (uint32_t)(now - lastTimerUpdate);
  }

  lastTimerUpdate = now;
}

void setRideRunning(bool running) {
  updateRideTimer();
  rideRunning = running;
}

bool saveMileage() {
  lastSaveAttempt = millis();

  if (!storageOK) {
    saveFailed = true;
    return false;
  }

  if (lifetimeMeters == savedLifetimeMeters && !saveFailed) {
    return true;
  }

  size_t written = preferences.putDouble(
    "totalM",
    lifetimeMeters
  );

  if (written != sizeof(double)) {
    saveFailed = true;
    return false;
  }

  savedLifetimeMeters = lifetimeMeters;
  saveFailed = false;

  return true;
}

void resetTrip() {
  tripMeters = 0;
  rideMilliseconds = 0;
  movingMilliseconds = 0;

  rideRunning = false;
  lastTimerUpdate = millis();

  integrationValid = false;

  distanceIncomplete =
    !sensorOK || !sampleOK || calibrating || nearLimit;
}

bool resetOdometer() {
  if (!storageOK) {
    return false;
  }

  // Save zero successfully BEFORE modifying RAM totals.
  size_t written = preferences.putDouble("totalM", 0.0);

  if (written != sizeof(double)) {
    saveFailed = true;
    return false;
  }

  lifetimeMeters = 0;
  savedLifetimeMeters = 0;

  saveFailed = false;
  lastSaveAttempt = millis();

  integrationValid = false;

  return true;
}

void integrateDistance(double mps, uint32_t now) {
  if (integrationValid) {
    uint32_t elapsedMs = now - previousSpeedTime;

    if (elapsedMs <= 100) {
      double averageMps = (previousMps + mps) * 0.5;
      double distance = averageMps * (elapsedMs / 1000.0);

      tripMeters += distance;
      lifetimeMeters += distance;

      if (averageMps > 0) {
        movingMilliseconds += elapsedMs;
      }
    } else {
      distanceIncomplete = true;
    }
  }

  previousMps = mps;
  previousSpeedTime = now;
  integrationValid = true;
}

// ================= CALIBRATION AND SPEED =================

void clearCalibrationSamples() {
  calibrationCount = 0;

  for (int i = 0; i < 3; i++) {
    calibrationSum[i] = 0;
  }
}

void beginCalibration() {
  clearCalibrationSamples();

  filteredDps = 0;
  nearLimit = false;
  sampleOK = false;
  calibrating = true;
  integrationValid = false;

  if (tripMeters > 0) {
    distanceIncomplete = true;
  }

  Serial.println(
    "Calibrating: keep wheel STILL for 3 seconds."
  );
}

void updateSensor() {
  if (!sensorOK) {
    return;
  }

  uint32_t now = millis();

  if (now - lastSample < SAMPLE_MS) {
    return;
  }

  lastSample = now;

  int16_t raw[3];

  if (!readGyro(raw)) {
    sampleOK = false;
    filteredDps = 0;
    integrationValid = false;
    distanceIncomplete = true;

    if (calibrating) {
      clearCalibrationSamples();
    }

    return;
  }

  sampleOK = true;
  lastGoodSample = now;
  nearLimit = false;

  for (int i = 0; i < 3; i++) {
    if (raw[i] > 32000 || raw[i] < -32000) {
      nearLimit = true;
    }
  }

  if (calibrating) {
    if (nearLimit) {
      clearCalibrationSamples();
      return;
    }

    for (int i = 0; i < 3; i++) {
      calibrationSum[i] += raw[i] / COUNTS_PER_DPS;
    }

    calibrationCount++;

    if (calibrationCount >= CALIBRATION_SAMPLES) {
      for (int i = 0; i < 3; i++) {
        bias[i] = calibrationSum[i] / CALIBRATION_SAMPLES;
      }

      calibrating = false;
      filteredDps = 0;
      integrationValid = false;

      Serial.println("Calibration complete.");
    }

    return;
  }

  if (nearLimit) {
    filteredDps = 0;
    integrationValid = false;
    distanceIncomplete = true;
    return;
  }

  float magnitudeSquared = 0;

  for (int i = 0; i < 3; i++) {
    float dps = raw[i] / COUNTS_PER_DPS - bias[i];
    magnitudeSquared += dps * dps;
  }

  float rotationDps = sqrtf(magnitudeSquared);

  if (rotationDps < ZERO_THRESHOLD_DPS) {
    rotationDps = 0;
  }

  double mps = (rotationDps / 360.0) * CIRCUMFERENCE_M;
  integrateDistance(mps, now);

  // Smoothing affects the displayed speed only.
  filteredDps += 0.2f * (rotationDps - filteredDps);

  if (rotationDps == 0 && filteredDps < 0.1f) {
    filteredDps = 0;
  }
}

// ================= BATTERY =================

// Generic single-cell LiPo voltage estimate.
// This is NOT a calibrated fuel gauge.
//
// The 0% endpoint is an early warning, not a protection cutoff.
// Actual remaining charge depends on the cell, load and temperature.
int batteryPercent(float volts) {
  const float voltage[] = {
    3.30f,
    3.50f,
    3.60f,
    3.70f,
    3.75f,
    3.80f,
    3.85f,
    3.90f,
    3.95f,
    4.00f,
    4.10f,
    4.20f
  };

  const int percent[] = {
    0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
  };

  const int count = sizeof(voltage) / sizeof(voltage[0]);

  if (volts <= voltage[0]) {
    return 0;
  }

  if (volts >= voltage[count - 1]) {
    return 100;
  }

  for (int i = 1; i < count; i++) {
    if (volts <= voltage[i]) {
      float fraction =
        (volts - voltage[i - 1]) /
        (voltage[i] - voltage[i - 1]);

      return (int)lroundf(
        percent[i - 1] +
        fraction * (percent[i] - percent[i - 1])
      );
    }
  }

  return 0;
}

void setupBattery() {
  if (!BATTERY_MONITOR_ENABLED) {
    return;
  }

  pinMode(BATTERY_PIN, INPUT);
  analogReadResolution(12);

  // The divider produces approximately 2.1 V at full charge.
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  lastBatterySample = millis();
}

void updateBattery() {
  if (!BATTERY_MONITOR_ENABLED) {
    return;
  }

  uint32_t now = millis();

  if (now - lastBatterySample < BATTERY_SAMPLE_MS) {
    return;
  }

  lastBatterySample = now;

  // Spread readings over time rather than blocking for a batch.
  batterySumMv += analogReadMilliVolts(BATTERY_PIN);
  batterySampleCount++;

  if (batterySampleCount < BATTERY_SAMPLES) {
    return;
  }

  float measured =
    (batterySumMv / (float)batterySampleCount) / 1000.0f;

  measured *= BATTERY_DIVIDER_RATIO * BATTERY_CALIBRATION;

  batterySumMv = 0;
  batterySampleCount = 0;

  // Invalid readings are not proof that the battery is absent.
  // With USB connected, the charger may produce a plausible
  // voltage even if no battery is attached.
  if (!isfinite(measured) ||
      measured < 2.5f ||
      measured > 4.35f) {
    batteryValid = false;
    batteryVolts = 0;
    return;
  }

  if (batteryValid) {
    batteryVolts += 0.25f * (measured - batteryVolts);
  } else {
    batteryVolts = measured;
  }

  batteryValid = true;
}

// ================= WEB API =================

void sendAPI() {
  updateRideTimer();

  bool fresh =
    sampleOK && (millis() - lastGoodSample < 500);

  bool ready =
    sensorOK && fresh && !calibrating && !nearLimit;

  const char* status = "Live";

  if (!sensorOK) {
    status = "Sensor unavailable: check wiring and restart";
  } else if (!fresh) {
    status = "Waiting for valid sensor data";
  } else if (calibrating) {
    status = "Calibrating: keep wheel still";
  } else if (nearLimit) {
    status = "Gyro limit reached: distance counting paused";
  }

  double rotationsPerSecond = filteredDps / 360.0;
  double mps = rotationsPerSecond * CIRCUMFERENCE_M;

  double unsavedMeters = lifetimeMeters - savedLifetimeMeters;

  if (unsavedMeters < 0) {
    unsavedMeters = 0;
  }

  String json;
  json.reserve(850);

  json = "{\"sensor\":";
  json += sensorOK ? "true" : "false";

  json += ",\"calibrating\":";
  json += calibrating ? "true" : "false";

  json += ",\"ready\":";
  json += ready ? "true" : "false";

  json += ",\"status\":\"";
  json += status;
  json += "\"";

  json += ",\"mph\":";
  json += ready
    ? String(mps * 2.236936, 3)
    : String("null");

  json += ",\"kmh\":";
  json += ready
    ? String(mps * 3.6, 3)
    : String("null");

  json += ",\"rpm\":";
  json += ready
    ? String(rotationsPerSecond * 60.0, 2)
    : String("null");

  json += ",\"tripMi\":";
  json += String(tripMeters / METERS_PER_MILE, 6);

  json += ",\"totalMi\":";
  json += String(lifetimeMeters / METERS_PER_MILE, 6);

  json += ",\"unsavedMi\":";
  json += String(unsavedMeters / METERS_PER_MILE, 8);

  json += ",\"rideSec\":";
  json += String((double)rideMilliseconds / 1000.0, 1);

  json += ",\"movingSec\":";
  json += String((double)movingMilliseconds / 1000.0, 1);

  json += ",\"rideRunning\":";
  json += rideRunning ? "true" : "false";

  json += ",\"incomplete\":";
  json += distanceIncomplete ? "true" : "false";

  json += ",\"storage\":";
  json += storageOK ? "true" : "false";

  json += ",\"saveFailed\":";
  json += saveFailed ? "true" : "false";

  json += ",\"batteryValid\":";
  json += batteryValid ? "true" : "false";

  json += ",\"batteryV\":";
  json += batteryValid
    ? String(batteryVolts, 3)
    : String("null");

  json += ",\"batteryPct\":";
  json += batteryValid
    ? String(batteryPercent(batteryVolts))
    : String("null");

  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", DASHBOARD);
  });

  server.on("/api", HTTP_GET, sendAPI);

  server.on("/start", HTTP_POST, []() {
    setRideRunning(true);

    server.send(
      200,
      "text/plain",
      "Ride timer running."
    );
  });

  server.on("/stop", HTTP_POST, []() {
    setRideRunning(false);

    server.send(
      200,
      "text/plain",
      "Ride timer stopped. Press Start to resume."
    );
  });

  server.on("/calibrate", HTTP_POST, []() {
    if (!sensorOK) {
      server.send(
        503,
        "text/plain",
        "Sensor unavailable."
      );
      return;
    }

    if (!calibrating) {
      beginCalibration();
    }

    server.send(
      202,
      "text/plain",
      "Calibration started. Keep the wheel still."
    );
  });

  server.on("/save", HTTP_POST, []() {
    bool saved = saveMileage();

    server.send(
      saved ? 200 : 500,
      "text/plain",
      saved
        ? "Lifetime mileage saved."
        : "Flash save failed. Current mileage remains in RAM."
    );
  });

  server.on("/reset", HTTP_POST, []() {
    resetTrip();

    server.send(
      200,
      "text/plain",
      "Trip and timers reset. Ride timer stopped. "
      "Lifetime mileage unchanged."
    );
  });

  server.on("/resetodo", HTTP_POST, []() {
    if (!storageOK) {
      server.send(
        503,
        "text/plain",
        "Odometer not reset: flash unavailable."
      );
      return;
    }

    if (!resetOdometer()) {
      server.send(
        500,
        "text/plain",
        "Odometer reset failed. Current mileage retained."
      );
      return;
    }

    server.send(
      200,
      "text/plain",
      "Lifetime odometer reset and saved. "
      "Trip and timers unchanged."
    );
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
}

// ================= WI-FI AND HOSTNAME =================

void startBackup() {
  lastBackupAttempt = millis();

  backupStarted = WiFi.softAP(
    BACKUP_SSID,
    BACKUP_PASSWORD
  );

  if (backupStarted) {
    Serial.print("Direct Wi-Fi: BikeSpeed, dashboard http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Direct Wi-Fi start failed; will retry.");
  }
}

void updateWiFi() {
  uint32_t now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;

  if (!backupStarted &&
      now - lastBackupAttempt >= 10000) {
    startBackup();
  }

  String currentIP = connected
    ? WiFi.localIP().toString()
    : String("");

  if (connected && !previouslyConnected) {
    Serial.print("Hotspot connected. Dashboard http://");
    Serial.println(WiFi.localIP());

    retryPending = false;
  }

  if (!connected && previouslyConnected) {
    Serial.println(
      "Hotspot lost. Direct Wi-Fi remains available."
    );

    lastConnectAttempt = now;
  }

  if (currentIP != advertisedIP) {
    if (mdnsStarted) {
      MDNS.end();
    }

    mdnsStarted = false;
    advertisedIP = currentIP;
    lastMdnsAttempt = now - 5000;
  }

  // Start mDNS on direct Wi-Fi as well as the hotspot connection.
  if (!mdnsStarted &&
      (connected || backupStarted) &&
      now - lastMdnsAttempt >= 5000) {

    lastMdnsAttempt = now;
    mdnsStarted = MDNS.begin(HOSTNAME);

    if (mdnsStarted) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS ready: http://bikespeed.local");
    } else {
      Serial.println(
        "mDNS start failed; retrying. IPv4 remains available."
      );
    }
  }

  // A single reconnect mechanism avoids overlapping attempts.
  if (HOTSPOT_SSID[0] && !connected) {
    if (retryPending) {
      if (now - retryStarted >= 500) {
        WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);

        lastConnectAttempt = now;
        retryPending = false;
      }
    } else if (now - lastConnectAttempt >= 30000) {
      WiFi.disconnect(false, false);

      retryStarted = now;
      retryPending = true;
    }
  }

  previouslyConnected = connected;
}

// ================= MAIN =================

void setup() {
  Serial.begin(115200);
  delay(300);

  // Preserve mileage saved by the previous firmware.
  storageOK = preferences.begin("bike-odo", false);

  if (storageOK) {
    lifetimeMeters = preferences.getDouble("totalM", 0.0);

    if (!isfinite(lifetimeMeters) || lifetimeMeters < 0) {
      lifetimeMeters = 0;
      saveFailed = true;
    }

    savedLifetimeMeters = lifetimeMeters;
  }

  setupBattery();

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(50);

  sensorOK = initializeSensor();

  if (sensorOK) {
    beginCalibration();
  } else {
    distanceIncomplete = true;
    Serial.println("Sensor initialization failed.");
  }

  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);

  // Favor responsive HTTP/mDNS. Uses more battery.
  WiFi.setSleep(false);

  startBackup();

  if (HOTSPOT_SSID[0]) {
    WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);
  }

  uint32_t now = millis();

  lastConnectAttempt = now;
  lastSaveAttempt = now;
  lastTimerUpdate = now;
  lastMdnsAttempt = now - 5000;

  setupWebServer();
  updateWiFi();

  Serial.println(
    "Dashboard started. Ride timer stopped; press Start."
  );

  Serial.printf(
    "Lifetime mileage: %.3f miles\n",
    lifetimeMeters / METERS_PER_MILE
  );
}

void loop() {
  updateRideTimer();
  updateSensor();
  updateBattery();
  updateWiFi();

  server.handleClient();

  if (millis() - lastSaveAttempt >= SAVE_INTERVAL_MS) {
    saveMileage();
  }

  delay(1);
}