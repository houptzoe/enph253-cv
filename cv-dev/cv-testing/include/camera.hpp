#pragma once

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <string>

class Camera {
public:
    explicit Camera(int deviceIndex = 0);

    bool tryOpen();
    bool captureFrame(cv::Mat& frame);

    int deviceIndex() const { return deviceIndex_; }
    const std::string& devicePath() const { return devicePath_; }

private:
    bool open();
    bool openIndex(int index);
    bool probeIndex(int index);
    bool captureViaRpicam(cv::Mat& frame);

    int deviceIndex_;
    std::string devicePath_;
    bool useRpicam_ = false;
    cv::VideoCapture cap_;
};
