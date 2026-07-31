# ESP32 ↔ Raspberry Pi GPIO Handshake (firmware contract)

**Audience:** ESP32 firmware developers  
**Pi side (deployed):** `mars-cv --dual` on **marspi**, model `teletubby-yolov8n-320.onnx`  
**Transport:** 3.3 V digital GPIO only — no UART / I2C / SPI for this path  

Copy this file into the firmware repo. **Pi BCM pin numbers are authoritative.** Map them to whatever ESP32 GPIO pins you wire.

---

## Goal

When the robot wants vision:

1. ESP32 tells the Pi to start dual-camera YOLO search.
2. If **cam0** finds a teletubby, the Pi pulses one line.
3. If **cam1** finds a teletubby, the Pi pulses the other line.
4. After either detection, the Pi stops inference immediately. ESP advances robot state from which line pulsed.

---

## Pin map (Pi BCM)

| Signal | Direction | Pi BCM | Pi header | Idle | Active meaning |
|--------|-----------|--------|-----------|------|----------------|
| **START** | ESP → Pi | **GPIO4** | physical **7** | LOW | Rising edge → start dual search |
| **DETECT_CAM0** | Pi → ESP | **GPIO3** | physical **5** | LOW | ~100 ms HIGH = teletubby on **cam0** |
| **DETECT_CAM1** | Pi → ESP | **GPIO4** | physical **7** | LOW | ~100 ms HIGH = teletubby on **cam1** |
| **GND** | common | any GND | e.g. physical **9** | — | Must share ground |

### Critical: GPIO4 is multiplexed

**START** and **DETECT_CAM1** share the same wire (Pi GPIO4).

| Phase | Who drives Pi GPIO4 | Role |
|-------|---------------------|------|
| Idle / re-arm | **ESP** (push-pull LOW) | Armed for next START rising edge |
| Mission start | **ESP** drives LOW→HIGH | Rising edge starts Pi |
| After START (~few ms) | **ESP releases** (input / high-Z) | Wire free for Pi |
| Searching | **Pi** (output, idle LOW) | Ready to pulse DETECT_CAM1 |
| cam1 detect | **Pi** pulses HIGH ~100 ms | ESP reads as DETECT_CAM1 |
| After mission | **ESP** drives LOW again | Re-arm before next START |

If the ESP keeps driving GPIO4 HIGH (or LOW push-pull) after START, the Pi cannot reclaim the line as DETECT_CAM1 and cam1 signaling will fail (bus fight / `armDetectOutputs` error on Pi).

**DETECT_CAM0 (GPIO3)** is Pi-driven only. ESP configures it as input for the whole mission.

---

## Electrical requirements

- **3.3 V logic only.** Pi GPIO is not 5 V tolerant.
- **Common GND** between ESP32 and Pi.
- Prefer short wires; add series resistors (~220–330 Ω) if you want protection against brief contention during the GPIO4 handoff.
- Optional: weak pulldown on the multiplexed wire so it idles LOW when neither side drives.
- On the Pi, I2C should be disabled if it claims GPIO3 (SCL). That is a Pi config issue, not firmware.

ESP pin choice is up to you. Example placeholders:

```text
#define PIN_TO_PI_GPIO4   /* your ESP GPIO wired to Pi BCM 4 (START + DETECT_CAM1) */
#define PIN_FROM_PI_GPIO3 /* your ESP GPIO wired to Pi BCM 3 (DETECT_CAM0) */
```

---

## Timing (Pi-side constants — treat as contract)

| Parameter | Value | Firmware implication |
|-----------|-------|----------------------|
| START trigger | Rising edge LOW→HIGH | Level after edge does not matter; edge does |
| ESP release window | **≤ ~20 ms** after START edge | Pi sleeps ~20 ms then claims GPIO4 as output |
| DETECT pulse width | **100 ms** (± software jitter) | Accept HIGH ≥ ~50 ms as valid; ignore glitches |
| DETECT polarity | Active HIGH | Idle is LOW |
| Pi restart / re-arm | **~3 s** after detect | `mars-cv` exits; systemd restarts; then idle waiting for START |
| Warmup before counting | ~20 frames on Pi | Do not expect DETECT in the first ~1–2 s of search |
| Search duration | unbounded until detect or stop | ESP should timeout if needed for robot safety |

CV thresholds (not required for GPIO code): conf 0.85, window 8, hit-rate 0.7, imgsz 320. Firmware only needs START / DETECT.

---

## Mission sequence (normative)

```text
ESP32                                         Pi (mars-cv service)
  |                                             |
  |  Pi_GPIO4 = LOW (driven)                    |  idle: wait rising edge on GPIO4
  |  Pi_GPIO3 = input                           |  GPIO3 held LOW as DETECT_CAM0 out
  |                                             |
  |  ==== start mission ====                    |
  |  Pi_GPIO4 = HIGH  ---------------------->   |  sees rising edge
  |  delay 1–5 ms                               |
  |  Pi_GPIO4 = INPUT / Hi-Z  -------------->   |  ~20 ms later: claim GPIO4 as DETECT_CAM1 out (LOW)
  |                                             |  open cam0 + cam1, run YOLO
  |                                             |  … searching …
  |                                             |
  |  case A: cam0 found                         |
  |  <---------------- Pi_GPIO3 HIGH 100 ms --- |  pulse DETECT_CAM0
  |  <---------------- Pi_GPIO3 LOW ----------- |
  |                                             |  stop both cams, process exits
  |                                             |
  |  case B: cam1 found                         |
  |  <---------------- Pi_GPIO4 HIGH 100 ms --- |  pulse DETECT_CAM1
  |  <---------------- Pi_GPIO4 LOW ----------- |
  |                                             |  stop both cams, process exits
  |                                             |
  |  wait ≥ ~3–5 s (or until ready)             |  systemd restart → idle again
  |  Pi_GPIO4 = OUTPUT LOW                      |  ready for next rising edge
  |  ==== re-armed ====                         |
```

Only **one** DETECT pulse is sent per mission (first camera to confirm wins). The other camera is stopped without a pulse.

---

## ESP32 required behavior

### Idle / re-armed

1. Drive the Pi-GPIO4 wire **LOW** (push-pull output).
2. Configure Pi-GPIO3 wire as **input** (prefer pulldown or rely on Pi idle-LOW).
3. Do **not** raise START until the Pi is expected to be idle (after boot, or ≥ ~3–5 s after last DETECT).

### Start search

1. Drive Pi-GPIO4 **HIGH** → creates rising edge.
2. Within **1–5 ms**, switch that ESP pin to **input / high-Z** (release the bus).
3. Immediately begin watching:
   - Pi-GPIO3 HIGH pulse → **cam0** detection
   - Pi-GPIO4 HIGH pulse → **cam1** detection  
     (same physical pin you just released; now read as input)

### On DETECT

1. Treat a sustained HIGH of roughly **50–150 ms** as a valid pulse (debounce short spikes).
2. Latch which camera won; advance robot state.
3. Optionally ignore further edges until re-arm.
4. Before the next mission: drive Pi-GPIO4 **LOW** again (output) and hold until the next START.

### Must not

- Leave Pi-GPIO4 driven after START (blocks cam1 DETECT).
- Issue another START while the Pi is still searching or during the ~3 s restart window (edge may be missed).
- Drive 5 V into the Pi.
- Assume UART ACKs — there are none.

---

## Suggested firmware state machine

```text
states:
  IDLE
  START_PULSE
  WAIT_DETECT
  FOUND_CAM0
  FOUND_CAM1
  COOLDOWN

IDLE:
  pin4 = OUTPUT LOW
  pin3 = INPUT
  on mission_request → START_PULSE

START_PULSE:
  pin4 = OUTPUT HIGH
  delay 2 ms
  pin4 = INPUT   // release for DETECT_CAM1
  → WAIT_DETECT

WAIT_DETECT:
  if pin3 high for ≥ 50 ms → FOUND_CAM0
  if pin4 high for ≥ 50 ms → FOUND_CAM1
  if timeout (robot policy) → COOLDOWN (no detect)

FOUND_CAM0 / FOUND_CAM1:
  record which camera
  → COOLDOWN

COOLDOWN:
  wait ≥ 3000 ms (Pi systemd RestartSec=3)
  pin4 = OUTPUT LOW
  → IDLE
```

---

## Reference sketch (Arduino / ESP-IDF style pseudocode)

Pin names are placeholders — replace with your board wiring.

```cpp
// Wire these to the Pi header:
//   PIN_PI4  <->  Pi BCM GPIO4  (physical pin 7)  START + DETECT_CAM1
//   PIN_PI3  <->  Pi BCM GPIO3  (physical pin 5)  DETECT_CAM0
//   GND      <->  Pi GND

static constexpr int PIN_PI4 = /* ... */;
static constexpr int PIN_PI3 = /* ... */;
static constexpr int DETECT_MIN_MS = 50;
static constexpr int DETECT_MAX_MS = 200;
static constexpr int PI_COOLDOWN_MS = 3500;

enum class Phase { Idle, WaitDetect, Cooldown };

Phase phase = Phase::Idle;
int foundCamera = -1; // 0, 1, or -1

void armIdle() {
  pinMode(PIN_PI4, OUTPUT);
  digitalWrite(PIN_PI4, LOW);
  pinMode(PIN_PI3, INPUT); // or INPUT_PULLDOWN if available
  foundCamera = -1;
  phase = Phase::Idle;
}

void startVisionSearch() {
  // Rising edge on Pi GPIO4
  pinMode(PIN_PI4, OUTPUT);
  digitalWrite(PIN_PI4, LOW);
  delayMicroseconds(100);
  digitalWrite(PIN_PI4, HIGH);
  delay(2);
  // Release so Pi can drive DETECT_CAM1 on the same wire
  pinMode(PIN_PI4, INPUT);
  phase = Phase::WaitDetect;
}

// Call from loop() or a timer
void pollDetect() {
  if (phase != Phase::WaitDetect) return;

  static unsigned long high3Start = 0, high4Start = 0;
  const unsigned long now = millis();

  if (digitalRead(PIN_PI3) == HIGH) {
    if (high3Start == 0) high3Start = now;
    else if (now - high3Start >= DETECT_MIN_MS) {
      foundCamera = 0; // cam0
      phase = Phase::Cooldown;
      high3Start = high4Start = 0;
      return;
    }
  } else {
    high3Start = 0;
  }

  if (digitalRead(PIN_PI4) == HIGH) {
    if (high4Start == 0) high4Start = now;
    else if (now - high4Start >= DETECT_MIN_MS) {
      foundCamera = 1; // cam1
      phase = Phase::Cooldown;
      high3Start = high4Start = 0;
      return;
    }
  } else {
    high4Start = 0;
  }
}

void finishCooldownIfReady(unsigned long cooldownStartedAt) {
  if (phase == Phase::Cooldown && millis() - cooldownStartedAt >= PI_COOLDOWN_MS) {
    armIdle(); // drives PIN_PI4 LOW for next rising edge
  }
}
```

Interrupt-on-rising + duration check is also fine; the pulse is ~100 ms so polling at 1–5 ms is enough.

---

## Semantics for the robot

| Event | Meaning |
|-------|---------|
| START edge accepted by Pi | Dual YOLO search running (cam0 + cam1) |
| DETECT_CAM0 pulse | Teletubby confirmed on **cam0**; search stopped |
| DETECT_CAM1 pulse | Teletubby confirmed on **cam1**; search stopped |
| No DETECT before ESP timeout | Robot policy (abort / retry after cooldown); Pi may still be searching until STOP externally |

There is no “search failed” GPIO from the Pi. Timeout is entirely on the ESP / robot side.

---

## Bring-up checklist (firmware)

- [ ] Common GND connected
- [ ] Both lines measured 3.3 V max
- [ ] Idle: Pi GPIO4 wire sits LOW under ESP drive
- [ ] START: scope/logic analyzer shows clean LOW→HIGH, then ESP releases within a few ms
- [ ] After START, Pi journal shows `START received` / dual cams opening
- [ ] Simulate or wait for detect: GPIO3 pulse → treat as cam0; GPIO4 pulse → cam1
- [ ] After detect, wait ≥ 3.5 s, re-drive GPIO4 LOW, fire START again successfully

Pi log watch (on marspi):

```bash
journalctl -u mars-cv -f
```

Expected idle line:

```text
ESP handshake idle: waiting for START on GPIO4 (cam0 DETECT GPIO3, cam1 DETECT GPIO4)
```

Expected on detect (example cam0):

```text
[cam0] TELETUBBY DETECTED (confidence ..., GPIO3 pulsed 100 ms)
```

---

## Out of scope

- Serial / UART protocol
- ACK from ESP back to Pi
- Pi staying in-process after detect (it **exits**; systemd restarts)
- Choosing which ESP GPIO numbers to use (board-specific)

---

## Revision

| Item | Value |
|------|--------|
| Pi START | BCM **GPIO4** |
| Pi DETECT cam0 | BCM **GPIO3** |
| Pi DETECT cam1 | BCM **GPIO4** (after START release) |
| DETECT pulse | **100 ms** active HIGH |
| Pi service restart | **RestartSec=3** |
| Model / mode | dual cam, `teletubby-yolov8n-320.onnx` |
