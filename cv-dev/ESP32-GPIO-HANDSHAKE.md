# ESP32 ↔ Raspberry Pi GPIO Handshake (firmware contract)

**Audience:** ESP32 firmware developers  
**Pi side (deployed):** `mars-cv --dual` on **marspi**, model `teletubby-yolov8n-320.onnx`  
**Transport:** 3.3 V digital GPIO only — no UART / I2C / SPI for this path  

Copy this file into the firmware repo. **Pi BCM pin numbers are authoritative.** Map them to whatever ESP32 GPIO pins you wire.

---

## Goal

When the robot wants vision:

1. ESP32 tells the Pi to start dual-camera YOLO search.
2. Each time a camera confirms a teletubby, the Pi pulses that camera’s DETECT line (cam0 → GPIO3, cam1 → GPIO4).
3. The Pi keeps searching until **two** such handshakes have been sent (same camera twice, or one each — whichever confirms first).
4. After the **second** DETECT pulse, the Pi stops inference and exits. ESP advances robot state from each pulse.

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
| Pi restart / re-arm | **~3 s** after **second** detect | `mars-cv` exits; systemd restarts; then idle waiting for START |
| Warmup before counting | ~20 frames on Pi | Do not expect DETECT in the first ~1–2 s of search |
| Detects per mission | **2** | Two DETECT pulses, then Pi shuts off |
| Same-camera separation | **N empty frames** (N = detect window, default 8) | After a pulse, that camera must see the target leave frame before it can pulse again |
| Other camera | stays armed | Opposite FOV — a find on cam0 does not block cam1 |
| Search duration | until 2nd detect or stop | ESP should timeout if needed for robot safety |

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
  |  first find (example: cam0)                 |
  |  <---------------- Pi_GPIO3 HIGH 100 ms --- |  pulse DETECT_CAM0  (#1/2)
  |  <---------------- Pi_GPIO3 LOW ----------- |
  |                                             |  keep searching; that camera awaits frame clear
  |                                             |  (other camera stays armed)
  |                                             |
  |  second find (example: cam1)                |
  |  <---------------- Pi_GPIO4 HIGH 100 ms --- |  pulse DETECT_CAM1  (#2/2)
  |  <---------------- Pi_GPIO4 LOW ----------- |
  |                                             |  stop both cams, process exits
  |                                             |
  |  wait ≥ ~3–5 s (or until ready)             |  systemd restart → idle again
  |  Pi_GPIO4 = OUTPUT LOW                      |  ready for next rising edge
  |  ==== re-armed ====                         |
```

Exactly **two** DETECT pulses are sent per mission. Each pulse uses the GPIO for the camera that confirmed (GPIO3 = cam0, GPIO4 = cam1). Both pulses can be from the same camera if that camera confirms twice.

---

## ESP32 required behavior

### Idle / re-armed

1. Drive the Pi-GPIO4 wire **LOW** (push-pull output).
2. Configure Pi-GPIO3 wire as **input** (prefer pulldown or rely on Pi idle-LOW).
3. Do **not** raise START until the Pi is expected to be idle (after boot, or ≥ ~3–5 s after the **second** DETECT / mission end).

### Start search

1. Drive Pi-GPIO4 **HIGH** → creates rising edge.
2. Within **1–5 ms**, switch that ESP pin to **input / high-Z** (release the bus).
3. Immediately begin watching for **two** pulses:
   - Pi-GPIO3 HIGH pulse → **cam0** detection
   - Pi-GPIO4 HIGH pulse → **cam1** detection  
     (same physical pin you just released; now read as input)

### On DETECT

1. Treat a sustained HIGH of roughly **50–150 ms** as a valid pulse (debounce short spikes).
2. Record which camera fired; advance robot state for that find (1st or 2nd).
3. Stay in WAIT_DETECT until the **second** pulse (or timeout). Do not re-arm START after the first pulse.
4. After the second pulse (mission complete): wait for Pi restart (~3–5 s), then drive Pi-GPIO4 **LOW** again before the next START.

### Must not

- Leave Pi-GPIO4 driven after START (blocks cam1 DETECT).
- Issue another START while the Pi is still searching (including between 1st and 2nd detect) or during the ~3 s restart window.
- Drive 5 V into the Pi.
- Assume UART ACKs — there are none.

---

## Suggested firmware state machine

```text
states:
  IDLE
  START_PULSE
  WAIT_DETECT   // expect 2 pulses
  COOLDOWN

IDLE:
  pin4 = OUTPUT LOW
  pin3 = INPUT
  detectCount = 0
  on mission_request → START_PULSE

START_PULSE:
  pin4 = OUTPUT HIGH
  delay 2 ms
  pin4 = INPUT   // release for DETECT_CAM1
  → WAIT_DETECT

WAIT_DETECT:
  if pin3 high for ≥ 50 ms:
    record cam0; detectCount++
    if detectCount >= 2 → COOLDOWN else stay WAIT_DETECT
  if pin4 high for ≥ 50 ms:
    record cam1; detectCount++
    if detectCount >= 2 → COOLDOWN else stay WAIT_DETECT
  if timeout (robot policy) → COOLDOWN

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
static constexpr int PI_COOLDOWN_MS = 3500;
static constexpr int REQUIRED_DETECTS = 2;

enum class Phase { Idle, WaitDetect, Cooldown };

Phase phase = Phase::Idle;
int detectCount = 0;
int lastCam = -1; // 0 or 1 for most recent pulse

void armIdle() {
  pinMode(PIN_PI4, OUTPUT);
  digitalWrite(PIN_PI4, LOW);
  pinMode(PIN_PI3, INPUT); // or INPUT_PULLDOWN if available
  detectCount = 0;
  lastCam = -1;
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
  detectCount = 0;
  phase = Phase::WaitDetect;
}

// Call from loop() or a timer
void pollDetect() {
  if (phase != Phase::WaitDetect) return;

  static unsigned long high3Start = 0, high4Start = 0;
  const unsigned long now = millis();

  auto accept = [&](int cam) {
    lastCam = cam;
    ++detectCount;
    // handle robot event for this cam / ordinal
    high3Start = high4Start = 0;
    if (detectCount >= REQUIRED_DETECTS) {
      phase = Phase::Cooldown;
    }
  };

  if (digitalRead(PIN_PI3) == HIGH) {
    if (high3Start == 0) high3Start = now;
    else if (now - high3Start >= DETECT_MIN_MS) accept(0);
  } else {
    high3Start = 0;
  }

  if (phase == Phase::WaitDetect && digitalRead(PIN_PI4) == HIGH) {
    if (high4Start == 0) high4Start = now;
    else if (now - high4Start >= DETECT_MIN_MS) accept(1);
  } else if (digitalRead(PIN_PI4) == LOW) {
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
| 1st DETECT_CAM0 / CAM1 pulse | First teletubby confirmed on that camera; **search continues** |
| 2nd DETECT_CAM0 / CAM1 pulse | Second teletubby confirmed; **search stops** |
| No 2nd DETECT before ESP timeout | Robot policy (abort / retry after cooldown); Pi may still be searching until STOP externally |

There is no “search failed” GPIO from the Pi. Timeout is entirely on the ESP / robot side.

---

## Bring-up checklist (firmware)

- [ ] Common GND connected
- [ ] Both lines measured 3.3 V max
- [ ] Idle: Pi GPIO4 wire sits LOW under ESP drive
- [ ] START: scope/logic analyzer shows clean LOW→HIGH, then ESP releases within a few ms
- [ ] After START, Pi journal shows `START received` / dual cams opening
- [ ] First detect: GPIO3 or GPIO4 pulse; Pi keeps running (`#1/2`)
- [ ] Second detect: another pulse on the confirming cam’s GPIO; Pi exits (`#2/2`)
- [ ] After 2nd detect, wait ≥ 3.5 s, re-drive GPIO4 LOW, fire START again successfully

Pi log watch (on marspi):

```bash
journalctl -u mars-cv -f
```

Expected idle line:

```text
ESP handshake idle: waiting for START on GPIO4 (cam0 DETECT GPIO3, cam1 DETECT GPIO4)
```

Expected on detects:

```text
[cam0] TELETUBBY DETECTED #1/2 (confidence ..., GPIO3 pulsed 100 ms)
[cam1] TELETUBBY DETECTED #2/2 (confidence ..., GPIO4 pulsed 100 ms)
[cam1] Required detects reached (2) — shutting down
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
| Detects before stop | **2** (each pulsed on the confirming camera’s GPIO) |
| Pi service restart | **RestartSec=3** |
| Model / mode | dual cam, `teletubby-yolov8n-320.onnx` |
