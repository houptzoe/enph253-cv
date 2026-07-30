#pragma once

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <cstdio>
#include <string>
#include <vector>

#if defined(__linux__) && !defined(_WIN32)
#include <sys/types.h>
#endif

class Camera {
public:
    explicit Camera(int deviceIndex = 0);
    ~Camera();

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    bool tryOpen();
    bool captureFrame(cv::Mat& frame);

    int deviceIndex() const { return deviceIndex_; }
    const std::string& devicePath() const { return devicePath_; }
    const std::string& lastError() const { return lastError_; }

private:
    bool open();
    bool openIndex(int index);
    bool probeIndex(int index);
    void configureVideoCapture();
    bool openRpicamVid();
    void closeRpicamVid();
    bool captureViaRpicamVid(cv::Mat& frame, int timeoutMs = -1);

    int deviceIndex_;
    std::string devicePath_;
    std::string lastError_;
    bool useRpicamVid_ = false;
    bool rpicamVidAttempted_ = false;
    FILE* rpicamVidPipe_ = nullptr;
#if defined(__linux__) && !defined(_WIN32)
    pid_t rpicamVidPid_ = -1;
#endif
    std::vector<unsigned char> mjpegBuffer_;
    cv::VideoCapture cap_;
};
