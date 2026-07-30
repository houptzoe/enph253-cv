#include <atomic>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) && !defined(_WIN32)
#include <csignal>
#include <unistd.h>
#endif

#include "camera.hpp"
#include "frame-source.hpp"
#include "teletubby-detector.hpp"
#include "mjpeg-stream.hpp"

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

struct Options {
    bool useCamera = false;
    bool dual = false;
    bool loop = false;
    bool forceHeadless = false;
    bool noDisplay = false;
    int cameraDevice = 0;
    int streamPort = 0;
    // Sliding window: fire when hits in the last N frames reach hit-rate.
    int detectWindow = 8;
    float detectHitRate = 0.7f;
    // Skip first N frames before window counts (AE settle / boot false positives).
    int warmupFrames = 20;
    float confidence = 0.85f;
    int modelImgsz = 0; // 0 = infer from model path (320 in name -> 320, else 640)
    std::string imagePath;
    std::string videoPath;
    std::string modelPath;
};

std::mutex gLogMutex;
std::atomic<bool> gStopRequested{false};

#if defined(__linux__) && !defined(_WIN32)
void onStopSignal(int)
{
    gStopRequested.store(true);
    const char msg[] = "\nStopping (Ctrl+C)...\n";
    (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
    Camera::interruptAll();
}

void installStopHandlers()
{
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);
}
#else
void installStopHandlers() {}
#endif

bool hasDisplay()
{
#if defined(_WIN32)
    return true;
#else
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
#endif
}

void printBuildInfo()
{
    std::cout << "OpenCV " << CV_VERSION << std::endl;

#if defined(MARS_CV_RPI)
    std::cout << "Target: Raspberry Pi (aarch64)" << std::endl;
#elif defined(_WIN64)
    std::cout << "Target: Windows x64 (dev)" << std::endl;
#else
    std::cout << "Target: native Linux" << std::endl;
#endif
}

void printUsage()
{
    std::cout << "Usage: mars-cv [options]\n"
              << "  --camera              Capture from camera\n"
              << "  --device N            Camera index (default: 0; Picam 0/1 on Pi)\n"
              << "  --dual                Run cam0 + cam1 inference in parallel (SSH lab)\n"
              << "  --image PATH          Load a still image\n"
              << "  --video PATH          Load a video file\n"
              << "  --loop                Continuous search loop\n"
              << "  --model PATH          ONNX model for teletubby detection\n"
              << "  --confidence F        Detection threshold (default: 0.85)\n"
              << "  --imgsz N             YOLO letterbox size (default: 320 if model name has 320, else 640)\n"
              << "  --window N            Frames in hit-rate window (default: 8)\n"
              << "  --hit-rate F          Min detect fraction in window (default: 0.7)\n"
              << "  --warmup N            Skip first N frames before window counts (default: 20)\n"
              << "  --debounce N          Alias for --window (legacy)\n"
              << "  --headless            Save output.jpg instead of opening a window\n"
              << "  --no-display          Log only, no GUI or image output\n"
              << "  --stream-port N       MJPEG browser stream on port N (Linux/Pi; single cam)\n";
}

std::optional<Options> parseOptions(int argc, char* argv[])
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--camera") {
            options.useCamera = true;
        } else if (arg == "--dual") {
            options.dual = true;
            options.useCamera = true;
            options.loop = true;
        } else if (arg == "--loop") {
            options.loop = true;
        } else if (arg == "--headless") {
            options.forceHeadless = true;
        } else if (arg == "--no-display") {
            options.noDisplay = true;
        } else if (arg == "--device" && i + 1 < argc) {
            options.cameraDevice = std::stoi(argv[++i]);
            options.useCamera = true;
        } else if (arg == "--image" && i + 1 < argc) {
            options.imagePath = argv[++i];
        } else if (arg == "--video" && i + 1 < argc) {
            options.videoPath = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            options.modelPath = argv[++i];
        } else if (arg == "--confidence" && i + 1 < argc) {
            options.confidence = std::stof(argv[++i]);
        } else if (arg == "--imgsz" && i + 1 < argc) {
            options.modelImgsz = std::stoi(argv[++i]);
        } else if ((arg == "--window" || arg == "--debounce") && i + 1 < argc) {
            options.detectWindow = std::stoi(argv[++i]);
        } else if (arg == "--hit-rate" && i + 1 < argc) {
            options.detectHitRate = std::stof(argv[++i]);
        } else if (arg == "--warmup" && i + 1 < argc) {
            options.warmupFrames = std::stoi(argv[++i]);
        } else if (arg == "--stream-port" && i + 1 < argc) {
            options.streamPort = std::stoi(argv[++i]);
#if !defined(MARS_CV_MJPEG_STREAM)
            std::cerr << "--stream-port is only supported on Linux/Pi builds." << std::endl;
            return std::nullopt;
#endif
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return std::nullopt;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return std::nullopt;
        }
    }

    if (options.detectWindow < 1) {
        std::cerr << "--window must be >= 1" << std::endl;
        return std::nullopt;
    }
    if (options.detectHitRate <= 0.0f || options.detectHitRate > 1.0f) {
        std::cerr << "--hit-rate must be in (0, 1]" << std::endl;
        return std::nullopt;
    }
    if (options.warmupFrames < 0) {
        std::cerr << "--warmup must be >= 0" << std::endl;
        return std::nullopt;
    }
    if (options.modelImgsz != 0 && options.modelImgsz < 32) {
        std::cerr << "--imgsz must be >= 32" << std::endl;
        return std::nullopt;
    }

    if (options.dual) {
        if (options.modelPath.empty()) {
            std::cerr << "--dual requires --model PATH" << std::endl;
            return std::nullopt;
        }
        if (options.streamPort > 0) {
            std::cerr << "--stream-port is not supported with --dual" << std::endl;
            return std::nullopt;
        }
        if (!options.imagePath.empty() || !options.videoPath.empty()) {
            std::cerr << "--dual cannot be combined with --image or --video" << std::endl;
            return std::nullopt;
        }
        options.noDisplay = true;
    }

    return options;
}

std::unique_ptr<FrameSource> createFrameSource(const Options& options)
{
    if (!options.imagePath.empty()) {
        return FrameSource::fromImage(options.imagePath);
    }
    if (!options.videoPath.empty()) {
        return FrameSource::fromVideo(options.videoPath, options.loop);
    }
    if (options.useCamera) {
        return FrameSource::fromCamera(options.cameraDevice);
    }
    return FrameSource::demo();
}

void drawDetections(cv::Mat& frame, const std::vector<Detection>& detections)
{
    for (const Detection& detection : detections) {
        cv::rectangle(frame, detection.box, cv::Scalar(0, 255, 0), 2);
        const std::string label = "teletubby " + std::to_string(detection.confidence).substr(0, 4);
        cv::putText(frame, label,
                    cv::Point(detection.box.x, std::max(0, detection.box.y - 8)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
    }
}

float bestConfidence(const std::vector<Detection>& detections)
{
    float best = 0.0f;
    for (const Detection& detection : detections) {
        best = std::max(best, detection.confidence);
    }
    return best;
}

int requiredHits(int window, float hitRate)
{
    return std::max(1, static_cast<int>(std::ceil(static_cast<float>(window) * hitRate)));
}

int resolveModelImgsz(const Options& options)
{
    if (options.modelImgsz > 0) {
        return options.modelImgsz;
    }
    const std::string& path = options.modelPath;
    if (path.find("320") != std::string::npos) {
        return 320;
    }
    return TeletubbyDetector::kDefaultInputSize;
}

void logLine(const std::string& prefix, const std::string& message)
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    if (prefix.empty()) {
        std::cout << message << std::endl;
    } else {
        std::cout << "[" << prefix << "] " << message << std::endl;
    }
}

int runSearchLoop(const Options& options, FrameSource& source,
                  TeletubbyDetector* detector, bool showWindow,
                  MjpegStreamServer* stream, const std::string& logPrefix,
                  std::atomic<bool>* stopFlag, bool stopOnDetect)
{
    const std::string windowName = logPrefix.empty() ? "mars-cv" : ("mars-cv-" + logPrefix);
    const int hitsNeeded = requiredHits(options.detectWindow, options.detectHitRate);
    std::deque<bool> recent;
    int hitsInWindow = 0;
    int framesSeen = 0;
    bool warmupLogged = false;
    int framesSinceFpsLog = 0;
    auto fpsWindowStart = std::chrono::steady_clock::now();

    if (options.warmupFrames > 0) {
        logLine(logPrefix, "warmup " + std::to_string(options.warmupFrames) + " frames");
    }

    while ((stopFlag == nullptr || !stopFlag->load()) && !gStopRequested.load()) {
        cv::Mat frame;
        if (!source.next(frame)) {
            break;
        }

        std::vector<Detection> detections;
        if (detector != nullptr) {
            detections = detector->detect(frame, options.confidence);
        }

        ++framesSeen;
        ++framesSinceFpsLog;
        const auto now = std::chrono::steady_clock::now();
        const double elapsedSec =
            std::chrono::duration<double>(now - fpsWindowStart).count();
        if (elapsedSec >= 2.0) {
            const double fps = framesSinceFpsLog / elapsedSec;
            logLine(logPrefix, "inference " + std::to_string(fps).substr(0, 4) + " fps");
            framesSinceFpsLog = 0;
            fpsWindowStart = now;
        }

        const bool inWarmup = framesSeen <= options.warmupFrames;
        if (!inWarmup) {
            if (!warmupLogged && options.warmupFrames > 0) {
                logLine(logPrefix, "warmup done");
                warmupLogged = true;
            }

            const bool frameHasDetection = !detections.empty();
            recent.push_back(frameHasDetection);
            if (frameHasDetection) {
                ++hitsInWindow;
            }
            if (static_cast<int>(recent.size()) > options.detectWindow) {
                if (recent.front()) {
                    --hitsInWindow;
                }
                recent.pop_front();
            }

            // Sliding-window yield: hit-rate in last N frames, gaps allowed.
            if (stopOnDetect && detector != nullptr && hitsInWindow >= hitsNeeded) {
                logLine(logPrefix,
                        "TELETUBBY DETECTED (confidence " +
                            std::to_string(bestConfidence(detections)).substr(0, 4) + ")");
                if (stopFlag != nullptr) {
                    stopFlag->store(true);
                }
                gStopRequested.store(true);
                Camera::interruptAll();
                break;
            }
        }

        if (detector != nullptr) {
            drawDetections(frame, detections);
        }

        if (stream != nullptr) {
            stream->publish(frame);
        }

        if (showWindow) {
            cv::imshow(windowName, frame);
            const int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') {
                break;
            }
        } else if (!options.noDisplay) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (showWindow) {
        cv::destroyAllWindows();
    }
    return 0;
}

int runCameraWorker(Options options, int deviceIndex, const std::string& logPrefix,
                    std::atomic<bool>& stopFlag)
{
    options.cameraDevice = deviceIndex;
    options.useCamera = true;
    options.loop = true;
    options.noDisplay = true;
    options.streamPort = 0;

    std::unique_ptr<FrameSource> source;
    try {
        source = FrameSource::fromCamera(deviceIndex);
    } catch (const std::exception& ex) {
        logLine(logPrefix, std::string(ex.what()));
        stopFlag.store(true);
        return 1;
    }

    TeletubbyDetector detector;
    if (!detector.load(options.modelPath, resolveModelImgsz(options))) {
        logLine(logPrefix, "Failed to load model: " + options.modelPath);
        stopFlag.store(true);
        return 1;
    }
    logLine(logPrefix, "YOLO imgsz " + std::to_string(detector.inputSize()));

    logLine(logPrefix, "Opened " + source->description() + "; search loop running");
    const int rc = runSearchLoop(options, *source, &detector, false, nullptr, logPrefix,
                                 &stopFlag, true);
    logLine(logPrefix, "Stopped");
    return rc;
}

int runDualSearch(const Options& options)
{
    std::atomic<bool> stopFlag{false};
    int result0 = 1;
    int result1 = 1;

    std::thread cam0([&]() {
        result0 = runCameraWorker(options, 0, "cam0", stopFlag);
    });
    std::thread cam1([&]() {
        result1 = runCameraWorker(options, 1, "cam1", stopFlag);
    });

    cam0.join();
    cam1.join();
    if (gStopRequested.load()) {
        std::cout << "Stopped." << std::endl;
    }
    return (result0 != 0 || result1 != 0) ? 1 : 0;
}

int runSingleFrame(const Options& options, FrameSource& source,
                   TeletubbyDetector* detector, bool headless, bool showWindow)
{
    cv::Mat frame;
    if (!source.next(frame)) {
        std::cerr << "Failed to capture frame from " << source.description() << std::endl;
        return 1;
    }

    std::cout << "Captured from " << source.description()
              << " (" << frame.cols << "x" << frame.rows << ")" << std::endl;

    std::vector<Detection> detections;
    if (detector != nullptr) {
        detections = detector->detect(frame, options.confidence);
        if (!detections.empty()) {
            std::cout << "TELETUBBY DETECTED (confidence "
                      << bestConfidence(detections) << ")" << std::endl;
        } else {
            std::cout << "No teletubby detected." << std::endl;
        }
        drawDetections(frame, detections);
    }

    if (options.noDisplay) {
        return 0;
    }

    if (headless) {
        const std::string outputPath = "output.jpg";
        if (!cv::imwrite(outputPath, frame)) {
            std::cerr << "Failed to write " << outputPath << std::endl;
            return 1;
        }
        std::cout << "Headless mode: saved " << outputPath << std::endl;
        return 0;
    }

    if (showWindow) {
        const std::string windowName = "mars-cv";
        cv::imshow(windowName, frame);
        std::cout << "Press any key in the window to exit." << std::endl;
        cv::waitKey(0);
        cv::destroyAllWindows();
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    installStopHandlers();

    const std::optional<Options> options = parseOptions(argc, argv);
    if (!options.has_value()) {
        return 0;
    }

    printBuildInfo();

    // OpenCV DNN thread pool is process-wide.
    cv::setNumThreads(options->dual ? 2 : 4);

    if (options->dual) {
        std::cout << "Dual inference: cam0 + cam1 (Ctrl+C to stop)" << std::endl;
        std::cout << "Capture: 640x360@15, YOLO "
                  << resolveModelImgsz(*options)
                  << ", conf " << options->confidence
                  << ", window " << options->detectWindow
                  << " hit-rate " << options->detectHitRate
                  << ", warmup " << options->warmupFrames << std::endl;
        std::cout << "Loaded model: " << options->modelPath << std::endl;
        return runDualSearch(*options);
    }

    std::unique_ptr<FrameSource> source;
    try {
        source = createFrameSource(*options);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    std::unique_ptr<TeletubbyDetector> detector;
    if (!options->modelPath.empty()) {
        detector = std::make_unique<TeletubbyDetector>();
        if (!detector->load(options->modelPath, resolveModelImgsz(*options))) {
            return 1;
        }
        std::cout << "Loaded model: " << options->modelPath
                  << " (imgsz " << detector->inputSize() << ")" << std::endl;
    }

#if defined(MARS_CV_MJPEG_STREAM)
    std::unique_ptr<MjpegStreamServer> stream;
    if (options->streamPort > 0) {
        try {
            stream = std::make_unique<MjpegStreamServer>(options->streamPort);
            std::cout << "MJPEG stream: http://<pi-host>:" << stream->port() << "/" << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
            return 1;
        }
    }
#else
    if (options->streamPort > 0) {
        std::cerr << "--stream-port is only supported on Linux/Pi builds." << std::endl;
        return 1;
    }
#endif

    const bool showWindow = !options->noDisplay && !options->forceHeadless && hasDisplay()
                            && options->streamPort == 0;
    const bool headless = options->forceHeadless || !hasDisplay();

    if (options->loop) {
        if (showWindow) {
            std::cout << "Search loop running. Press q or Esc to exit." << std::endl;
        } else if (options->streamPort > 0) {
            std::cout << "Search loop running. Open the MJPEG URL in a browser." << std::endl;
        } else {
            std::cout << "Search loop running." << std::endl;
        }
        const std::string camTag =
            options->useCamera ? ("cam" + std::to_string(options->cameraDevice)) : "";
        return runSearchLoop(*options, *source, detector.get(), showWindow,
#if defined(MARS_CV_MJPEG_STREAM)
                             stream.get(),
#else
                             nullptr,
#endif
                             camTag, nullptr,
                             options->useCamera && detector != nullptr);
    }

    return runSingleFrame(*options, *source, detector.get(), headless, showWindow);
}
