#pragma once

#include <opencv2/core.hpp>

#if defined(MARS_CV_MJPEG_STREAM)

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class MjpegStreamServer {
public:
    explicit MjpegStreamServer(int port);
    ~MjpegStreamServer();

    MjpegStreamServer(const MjpegStreamServer&) = delete;
    MjpegStreamServer& operator=(const MjpegStreamServer&) = delete;

    void publish(const cv::Mat& bgrFrame);
    int port() const { return port_; }

private:
    void acceptLoop();
    void serveClient(int clientFd);

    int port_;
    int serverFd_ = -1;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::mutex frameMutex_;
    std::vector<unsigned char> jpeg_;
    int frameVersion_ = 0;
};

#else

class MjpegStreamServer {
public:
    explicit MjpegStreamServer(int) {}
    void publish(const cv::Mat&) {}
    int port() const { return 0; }
};

#endif
