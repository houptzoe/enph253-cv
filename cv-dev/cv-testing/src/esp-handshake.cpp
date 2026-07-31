#include "esp-handshake.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#if defined(MARS_CV_RPI)
#include <gpiod.h>

#include <array>
#include <cerrno>
#include <cstring>
#endif

EspHandshake::EspHandshake() = default;

EspHandshake::~EspHandshake()
{
    close();
}

void EspHandshake::close()
{
#if defined(MARS_CV_RPI)
    if (startRequest_ != nullptr) {
        gpiod_line_request_release(startRequest_);
        startRequest_ = nullptr;
    }
    if (detectCam0Request_ != nullptr) {
        gpiod_line_request_release(detectCam0Request_);
        detectCam0Request_ = nullptr;
    }
    if (detectCam1Request_ != nullptr) {
        gpiod_line_request_release(detectCam1Request_);
        detectCam1Request_ = nullptr;
    }
    if (chip_ != nullptr) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
#endif
    open_ = false;
    detectArmed_ = false;
}

#if defined(MARS_CV_RPI)
namespace {

gpiod_line_request* requestInputRising(gpiod_chip* chip, unsigned offset, const char* consumer)
{
    gpiod_line_settings* settings = gpiod_line_settings_new();
    if (settings == nullptr) {
        return nullptr;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);

    gpiod_line_config* lineCfg = gpiod_line_config_new();
    if (lineCfg == nullptr) {
        gpiod_line_settings_free(settings);
        return nullptr;
    }
    if (gpiod_line_config_add_line_settings(lineCfg, &offset, 1, settings) != 0) {
        gpiod_line_config_free(lineCfg);
        gpiod_line_settings_free(settings);
        return nullptr;
    }
    gpiod_line_settings_free(settings);

    gpiod_request_config* reqCfg = gpiod_request_config_new();
    if (reqCfg == nullptr) {
        gpiod_line_config_free(lineCfg);
        return nullptr;
    }
    gpiod_request_config_set_consumer(reqCfg, consumer);

    gpiod_line_request* request = gpiod_chip_request_lines(chip, reqCfg, lineCfg);
    gpiod_request_config_free(reqCfg);
    gpiod_line_config_free(lineCfg);
    return request;
}

gpiod_line_request* requestOutputLow(gpiod_chip* chip, unsigned offset, const char* consumer)
{
    gpiod_line_settings* settings = gpiod_line_settings_new();
    if (settings == nullptr) {
        return nullptr;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    gpiod_line_config* lineCfg = gpiod_line_config_new();
    if (lineCfg == nullptr) {
        gpiod_line_settings_free(settings);
        return nullptr;
    }
    if (gpiod_line_config_add_line_settings(lineCfg, &offset, 1, settings) != 0) {
        gpiod_line_config_free(lineCfg);
        gpiod_line_settings_free(settings);
        return nullptr;
    }
    gpiod_line_settings_free(settings);

    gpiod_request_config* reqCfg = gpiod_request_config_new();
    if (reqCfg == nullptr) {
        gpiod_line_config_free(lineCfg);
        return nullptr;
    }
    gpiod_request_config_set_consumer(reqCfg, consumer);

    gpiod_line_request* request = gpiod_chip_request_lines(chip, reqCfg, lineCfg);
    gpiod_request_config_free(reqCfg);
    gpiod_line_config_free(lineCfg);
    return request;
}

} // namespace
#endif

bool EspHandshake::open()
{
    close();
    lastError_.clear();

#if defined(MARS_CV_RPI)
    // Pi 5 RP1 GPIO is typically gpiochip4; Pi 4 uses gpiochip0.
    static constexpr std::array<const char*, 3> kChipCandidates = {
        "/dev/gpiochip4",
        "/dev/gpiochip0",
        "/dev/gpiochip1",
    };

    for (const char* path : kChipCandidates) {
        gpiod_chip* chip = gpiod_chip_open(path);
        if (chip == nullptr) {
            continue;
        }

        gpiod_chip_info* info = gpiod_chip_get_info(chip);
        if (info == nullptr) {
            gpiod_chip_close(chip);
            continue;
        }
        const size_t numLines = gpiod_chip_info_get_num_lines(info);
        gpiod_chip_info_free(info);
        const unsigned maxNeeded =
            std::max(kStartGpio, std::max(kDetectCam0Gpio, kDetectCam1Gpio));
        if (numLines <= maxNeeded) {
            gpiod_chip_close(chip);
            continue;
        }

        gpiod_line_request* start = requestInputRising(chip, kStartGpio, "mars-cv-start");
        if (start == nullptr) {
            lastError_ = std::string("Failed to request START GPIO") +
                         std::to_string(kStartGpio) + " on " + path + ": " +
                         std::strerror(errno);
            gpiod_chip_close(chip);
            continue;
        }

        gpiod_line_request* detect0 =
            requestOutputLow(chip, kDetectCam0Gpio, "mars-cv-detect-cam0");
        if (detect0 == nullptr) {
            lastError_ = std::string("Failed to request DETECT cam0 GPIO") +
                         std::to_string(kDetectCam0Gpio) + " on " + path + ": " +
                         std::strerror(errno);
            gpiod_line_request_release(start);
            gpiod_chip_close(chip);
            continue;
        }

        chip_ = chip;
        startRequest_ = start;
        detectCam0Request_ = detect0;
        detectCam1Request_ = nullptr;
        open_ = true;
        detectArmed_ = false;
        lastError_.clear();
        return true;
    }

    if (lastError_.empty()) {
        lastError_ =
            "No usable gpiochip found for BCM GPIO3/GPIO4 "
            "(tried gpiochip4/0/1). Is another process holding the lines?";
    }
    return false;
#else
    lastError_ = "ESP handshake is only available on Raspberry Pi builds";
    return false;
#endif
}

bool EspHandshake::waitForStart(const std::atomic<bool>* stopFlag)
{
#if defined(MARS_CV_RPI)
    if (!open_ || startRequest_ == nullptr) {
        lastError_ = "ESP handshake not open";
        return false;
    }
    if (detectArmed_) {
        lastError_ = "START already consumed; reopen handshake to wait again";
        return false;
    }

    gpiod_edge_event_buffer* buffer = gpiod_edge_event_buffer_new(4);
    if (buffer == nullptr) {
        lastError_ = "Failed to allocate edge event buffer";
        return false;
    }

    while (true) {
        if (stopFlag != nullptr && stopFlag->load()) {
            lastError_ = "Interrupted while waiting for START";
            gpiod_edge_event_buffer_free(buffer);
            return false;
        }

        // 1 second timeout, then poll again (keeps process interruptible).
        const int ready = gpiod_line_request_wait_edge_events(startRequest_, 1000000000);
        if (ready < 0) {
            lastError_ = std::string("Failed waiting for START edge: ") + std::strerror(errno);
            gpiod_edge_event_buffer_free(buffer);
            return false;
        }
        if (ready == 0) {
            continue;
        }

        const int n = gpiod_line_request_read_edge_events(startRequest_, buffer, 4);
        if (n < 0) {
            lastError_ = std::string("Failed to read START events: ") + std::strerror(errno);
            gpiod_edge_event_buffer_free(buffer);
            return false;
        }

        for (int i = 0; i < n; ++i) {
            // libgpiod v2: buffer returns const*, getter still takes non-const*.
            gpiod_edge_event* event = const_cast<gpiod_edge_event*>(
                gpiod_edge_event_buffer_get_event(buffer, static_cast<unsigned>(i)));
            if (event != nullptr &&
                gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE) {
                gpiod_edge_event_buffer_free(buffer);
                return true;
            }
        }
    }
#else
    (void)stopFlag;
    lastError_ = "ESP handshake is only available on Raspberry Pi builds";
    return false;
#endif
}

bool EspHandshake::armDetectOutputs()
{
#if defined(MARS_CV_RPI)
    if (!open_ || chip_ == nullptr || detectCam0Request_ == nullptr) {
        lastError_ = "ESP handshake not open";
        return false;
    }
    if (detectArmed_) {
        return true;
    }

    // Brief gap so ESP can release GPIO4 after the START edge before we drive it.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    if (startRequest_ != nullptr) {
        gpiod_line_request_release(startRequest_);
        startRequest_ = nullptr;
    }

    gpiod_line_request* detect1 =
        requestOutputLow(chip_, kDetectCam1Gpio, "mars-cv-detect-cam1");
    if (detect1 == nullptr) {
        lastError_ = std::string("Failed to reclaim GPIO") + std::to_string(kDetectCam1Gpio) +
                     " as cam1 DETECT output: " + std::strerror(errno) +
                     " (ESP must release START after the rising edge)";
        return false;
    }

    detectCam1Request_ = detect1;
    detectArmed_ = true;
    lastError_.clear();
    return true;
#else
    lastError_ = "ESP handshake is only available on Raspberry Pi builds";
    return false;
#endif
}

bool EspHandshake::pulseDetect(int cameraIndex, int pulseMs)
{
#if defined(MARS_CV_RPI)
    if (!open_) {
        lastError_ = "ESP handshake not open";
        return false;
    }
    if (!detectArmed_) {
        lastError_ = "DETECT outputs not armed (call armDetectOutputs after START)";
        return false;
    }
    if (pulseMs < 1) {
        pulseMs = 1;
    }

    gpiod_line_request* request = nullptr;
    unsigned offset = 0;
    if (cameraIndex == 0) {
        request = detectCam0Request_;
        offset = kDetectCam0Gpio;
    } else if (cameraIndex == 1) {
        request = detectCam1Request_;
        offset = kDetectCam1Gpio;
    } else {
        lastError_ = "pulseDetect cameraIndex must be 0 or 1";
        return false;
    }

    if (request == nullptr) {
        lastError_ = "DETECT line request missing for camera " + std::to_string(cameraIndex);
        return false;
    }

    if (gpiod_line_request_set_value(request, offset, GPIOD_LINE_VALUE_ACTIVE) != 0) {
        lastError_ = std::string("Failed to assert DETECT GPIO") + std::to_string(offset) +
                     ": " + std::strerror(errno);
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(pulseMs));
    if (gpiod_line_request_set_value(request, offset, GPIOD_LINE_VALUE_INACTIVE) != 0) {
        lastError_ = std::string("Failed to deassert DETECT GPIO") + std::to_string(offset) +
                     ": " + std::strerror(errno);
        return false;
    }
    return true;
#else
    (void)cameraIndex;
    (void)pulseMs;
    lastError_ = "ESP handshake is only available on Raspberry Pi builds";
    return false;
#endif
}
