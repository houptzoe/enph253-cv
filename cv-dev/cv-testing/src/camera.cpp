#include "camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>

#if defined(__linux__) && !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
#if defined(__linux__) && !defined(_WIN32)
constexpr int kBackend = cv::CAP_V4L2;
#else
constexpr int kBackend = cv::CAP_ANY;
#endif

constexpr int kWarmupFrames = 5;
constexpr int kMaxReadAttempts = 30;
constexpr int kRpicamOpenTimeoutMs = 8000;
constexpr int kMaxV4l2Devices = 16;
// IMX708 full FOV is 16:9. 640x480 (4:3) forces a cropped mode and loses side FOV.
// 2304x1296 is 2x2-binned full-sensor (same FOV as 4608x2592); scale output to 1280x720.
constexpr int kVideoWidth = 1280;
constexpr int kVideoHeight = 720;
constexpr int kVideoFps = 30;
constexpr char kRpicamSensorMode[] = "2304:1296";

std::string devicePathForIndex(int index)
{
    return "/dev/video" + std::to_string(index);
}

bool extractMjpegFrame(std::vector<unsigned char>& buffer, cv::Mat& frame)
{
    if (buffer.size() < 4) {
        return false;
    }

    size_t start = std::string::npos;
    for (size_t i = 0; i + 1 < buffer.size(); ++i) {
        if (buffer[i] == 0xFF && buffer[i + 1] == 0xD8) {
            start = i;
            break;
        }
    }
    if (start == std::string::npos) {
        return false;
    }

    size_t end = std::string::npos;
    for (size_t i = start + 2; i + 1 < buffer.size(); ++i) {
        if (buffer[i] == 0xFF && buffer[i + 1] == 0xD9) {
            end = i + 2;
            break;
        }
    }
    if (end == std::string::npos) {
        return false;
    }

    const cv::Mat encoded(1, static_cast<int>(end - start), CV_8UC1, buffer.data() + start);
    frame = cv::imdecode(encoded, cv::IMREAD_COLOR);
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(end));
    return !frame.empty();
}
} // namespace

Camera::Camera(int deviceIndex) : deviceIndex_(deviceIndex) {}

Camera::~Camera()
{
    closeRpicamVid();
}

bool Camera::tryOpen()
{
#if defined(MARS_CV_RPI)
    lastError_.clear();
    const bool rpicamInstalled = openRpicamVid();
    if (rpicamInstalled) {
        return true;
    }
    if (rpicamVidAttempted_) {
        if (lastError_.empty()) {
            lastError_ =
                "Camera " + std::to_string(deviceIndex_) +
                " unavailable via rpicam-vid (another process may be using it). "
                "Run: sudo fuser -v /dev/video*  then  sudo killall rpicam-vid mars-cv";
        }
        return false;
    }
    if (openIndex(deviceIndex_)) {
        return true;
    }
    for (int index = 0; index < kMaxV4l2Devices; ++index) {
        if (index == deviceIndex_) {
            continue;
        }
        if (openIndex(index)) {
            return true;
        }
    }
    if (lastError_.empty()) {
        lastError_ = "No working camera found via rpicam-vid or V4L2";
    }
    return false;
#else
    devicePath_.clear();
    lastError_.clear();
    return open();
#endif
}

bool Camera::open()
{
    return openIndex(deviceIndex_);
}

void Camera::configureVideoCapture()
{
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, kVideoWidth);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, kVideoHeight);
    cap_.set(cv::CAP_PROP_FPS, kVideoFps);
#if defined(MARS_CV_RPI)
    cap_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
#endif
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
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
    configureVideoCapture();

    for (int i = 0; i < kWarmupFrames; ++i) {
        if (!cap_.grab()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    cv::Mat test;
    if (!captureFrame(test) || test.empty()) {
        cap_.release();
        return false;
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

void Camera::closeRpicamVid()
{
#if defined(__linux__) && !defined(_WIN32)
    // Stop the writer before closing the pipe so rpicam-vid does not SIGPIPE/abort.
    if (rpicamVidPid_ > 0) {
        kill(rpicamVidPid_, SIGTERM);
        int status = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline) {
            const pid_t waited = waitpid(rpicamVidPid_, &status, WNOHANG);
            if (waited == rpicamVidPid_ || (waited < 0 && errno == ECHILD)) {
                rpicamVidPid_ = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (rpicamVidPid_ > 0) {
            kill(rpicamVidPid_, SIGKILL);
            waitpid(rpicamVidPid_, &status, 0);
            rpicamVidPid_ = -1;
        }
    }

    if (rpicamVidPipe_ != nullptr) {
        fclose(rpicamVidPipe_);
        rpicamVidPipe_ = nullptr;
    }
#endif
    useRpicamVid_ = false;
    mjpegBuffer_.clear();
}

bool Camera::openRpicamVid()
{
#if defined(__linux__) && !defined(_WIN32)
    closeRpicamVid();
    rpicamVidAttempted_ = false;

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        // Child: rpicam-vid writes MJPEG to stdout.
        setsid();
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        const std::string cameraArg = std::to_string(deviceIndex_);
        const std::string widthArg = std::to_string(kVideoWidth);
        const std::string heightArg = std::to_string(kVideoHeight);
        const std::string fpsArg = std::to_string(kVideoFps);
        execlp("rpicam-vid",
               "rpicam-vid",
               "-t", "0",
               "--camera", cameraArg.c_str(),
               "--mode", kRpicamSensorMode,
               "--width", widthArg.c_str(),
               "--height", heightArg.c_str(),
               "--codec", "mjpeg",
               "--inline",
               "-o", "-",
               "-n",
               "--framerate", fpsArg.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }

    close(pipefd[1]);
    rpicamVidPid_ = pid;
    rpicamVidPipe_ = fdopen(pipefd[0], "r");
    if (rpicamVidPipe_ == nullptr) {
        close(pipefd[0]);
        kill(rpicamVidPid_, SIGTERM);
        waitpid(rpicamVidPid_, nullptr, 0);
        rpicamVidPid_ = -1;
        return false;
    }
    // Avoid blocking forever in fread; poll() gates reads.
    const int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }
    rpicamVidAttempted_ = true;

    cv::Mat test;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kRpicamOpenTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (captureViaRpicamVid(test, 250) && !test.empty()) {
            useRpicamVid_ = true;
            devicePath_ = "rpicam-vid:" + std::to_string(deviceIndex_);
            return true;
        }
        int status = 0;
        const pid_t waited = waitpid(rpicamVidPid_, &status, WNOHANG);
        if (waited == rpicamVidPid_) {
            lastError_ =
                "Camera " + std::to_string(deviceIndex_) +
                " in use by another process (or invalid index). "
                "Run: sudo fuser -v /dev/video*  then  sudo killall rpicam-vid mars-cv";
            rpicamVidPid_ = -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    closeRpicamVid();
#endif
    return false;
}

bool Camera::captureViaRpicamVid(cv::Mat& frame, int timeoutMs)
{
#if defined(__linux__) && !defined(_WIN32)
    if (rpicamVidPipe_ == nullptr) {
        return false;
    }

    constexpr size_t kChunkSize = 8192;
    std::array<char, kChunkSize> chunk{};
    const auto deadline = timeoutMs >= 0
                              ? std::optional<std::chrono::steady_clock::time_point>(
                                    std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(timeoutMs))
                              : std::nullopt;

    while (!deadline.has_value() || std::chrono::steady_clock::now() < deadline.value()) {
        if (extractMjpegFrame(mjpegBuffer_, frame)) {
            return true;
        }

        pollfd ready{};
        ready.fd = fileno(rpicamVidPipe_);
        ready.events = POLLIN;
        const int waitMs = deadline.has_value() ? 200 : -1;
        const int pollResult = poll(&ready, 1, waitMs);
        if (pollResult <= 0) {
            return false;
        }

        const size_t bytesRead = fread(chunk.data(), 1, chunk.size(), rpicamVidPipe_);
        if (bytesRead == 0) {
            if (feof(rpicamVidPipe_)) {
                return false;
            }
            // O_NONBLOCK: poll said readable but fread may still return EAGAIN.
            clearerr(rpicamVidPipe_);
            continue;
        }

        mjpegBuffer_.insert(mjpegBuffer_.end(), chunk.begin(), chunk.begin() + bytesRead);
        if (mjpegBuffer_.size() > 4 * 1024 * 1024) {
            mjpegBuffer_.clear();
            return false;
        }
    }

    return extractMjpegFrame(mjpegBuffer_, frame);
#else
    (void)frame;
    (void)timeoutMs;
    return false;
#endif
}

bool Camera::captureFrame(cv::Mat& frame)
{
    if (useRpicamVid_) {
        return captureViaRpicamVid(frame);
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
