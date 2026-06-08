#include "camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace {
#if defined(__linux__) && !defined(_WIN32)
constexpr int kBackend = cv::CAP_V4L2;
#else
constexpr int kBackend = cv::CAP_ANY;
#endif

constexpr int kWarmupFrames = 15;
constexpr int kMaxReadAttempts = 30;
constexpr char kRpicamCapturePath[] = "/tmp/mars-cv-capture.jpg";

std::string devicePathForIndex(int index)
{
    return "/dev/video" + std::to_string(index);
}
} // namespace

Camera::Camera(int deviceIndex) : deviceIndex_(deviceIndex) {}

bool Camera::tryOpen()
{
#if defined(MARS_CV_RPI)
    cv::Mat test;
    if (captureViaRpicam(test)) {
        useRpicam_ = true;
        devicePath_ = "rpicam-still";
        return true;
    }
    return false;
#else
    devicePath_.clear();
    return open();
#endif
}

bool Camera::open()
{
    return openIndex(deviceIndex_);
}

bool Camera::openIndex(int index)
{
    cap_.release();
    cap_.open(index, kBackend);
    if (!cap_.isOpened()) {
        return false;
    }

    deviceIndex_ = index;
    devicePath_ = devicePathForIndex(index);
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);

    for (int i = 0; i < kWarmupFrames; ++i) {
        cap_.grab();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return true;
}

bool Camera::probeIndex(int index)
{
    cv::VideoCapture probe;
    probe.open(index, kBackend);
    if (!probe.isOpened()) {
        return false;
    }

    probe.set(cv::CAP_PROP_BUFFERSIZE, 1);

    for (int i = 0; i < 8; ++i) {
        probe.grab();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    cv::Mat test;
    for (int i = 0; i < 15; ++i) {
        probe >> test;
        if (!test.empty()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}

bool Camera::captureViaRpicam(cv::Mat& frame)
{
    const std::string cmd = std::string("rpicam-still -o ") + kRpicamCapturePath +
                            " --width 640 --height 480 -t 1000 -n >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        return false;
    }

    frame = cv::imread(kRpicamCapturePath);
    return !frame.empty();
}

bool Camera::captureFrame(cv::Mat& frame)
{
    if (useRpicam_) {
        return captureViaRpicam(frame);
    }

    if (!cap_.isOpened()) {
        return false;
    }

    for (int i = 0; i < kMaxReadAttempts; ++i) {
        cap_ >> frame;
        if (!frame.empty()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}
