#pragma once

#include <atomic>
#include <string>

// Pi <-> ESP32 dual-camera digital handshake:
//   START       in  = BCM GPIO4 (rising edge begins dual inference)
//   DETECT cam0 out = BCM GPIO3 (active-high pulse when cam0 yields)
//   DETECT cam1 out = BCM GPIO4 (active-high pulse when cam1 yields)
//
// GPIO4 is multiplexed: idle as START input; after the rising edge the Pi
// releases the input claim and reclaims GPIO4 as cam1 DETECT output.
// Firmware must stop driving GPIO4 after START so the Pi can pulse it.
class EspHandshake {
public:
    static constexpr unsigned kStartGpio = 4;
    static constexpr unsigned kDetectCam0Gpio = 3;
    static constexpr unsigned kDetectCam1Gpio = 4;
    static constexpr int kDefaultPulseMs = 100;

    EspHandshake();
    ~EspHandshake();

    EspHandshake(const EspHandshake&) = delete;
    EspHandshake& operator=(const EspHandshake&) = delete;

    // Open gpiochip: GPIO4 rising-edge input + GPIO3 output low.
    // Returns false on non-Pi builds or if lines cannot be claimed.
    bool open();
    void close();
    bool isOpen() const { return open_; }
    bool detectArmed() const { return detectArmed_; }

    // Block until a rising edge on GPIO4 (START).
    // If stopFlag becomes true between 1s polls, returns false.
    bool waitForStart(const std::atomic<bool>* stopFlag = nullptr);

    // After START: release GPIO4 input and request GPIO4 as output low.
    // Call once before inference; required before pulseDetect(1).
    bool armDetectOutputs();

    // Drive the winning camera's DETECT line high for pulseMs, then low.
    // cameraIndex 0 -> GPIO3, cameraIndex 1 -> GPIO4.
    bool pulseDetect(int cameraIndex, int pulseMs = kDefaultPulseMs);

    const std::string& lastError() const { return lastError_; }

private:
#if defined(MARS_CV_RPI)
    struct gpiod_chip* chip_ = nullptr;
    struct gpiod_line_request* startRequest_ = nullptr;
    struct gpiod_line_request* detectCam0Request_ = nullptr;
    struct gpiod_line_request* detectCam1Request_ = nullptr;
#endif
    bool open_ = false;
    bool detectArmed_ = false;
    std::string lastError_;
};
