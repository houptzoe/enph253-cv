#include <cstdlib>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "frame-source.hpp"
#include "teletubby-detector.hpp"

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

struct Options {
    bool useCamera = false;
    bool loop = false;
    bool forceHeadless = false;
    bool noDisplay = false;
    int cameraDevice = 0;
    int debounceFrames = 3;
    float confidence = 0.5f;
    std::string imagePath;
    std::string videoPath;
    std::string modelPath;
};

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
              << "  --device N            Camera device index (default: 0)\n"
              << "  --image PATH          Load a still image\n"
              << "  --video PATH          Load a video file\n"
              << "  --loop                Continuous search loop\n"
              << "  --model PATH          ONNX model for teletubby detection\n"
              << "  --confidence F        Detection threshold (default: 0.5)\n"
              << "  --debounce N          Consecutive detections required (default: 3)\n"
              << "  --headless            Save output.jpg instead of opening a window\n"
              << "  --no-display          Log only, no GUI or image output\n";
}

std::optional<Options> parseOptions(int argc, char* argv[])
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--camera") {
            options.useCamera = true;
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
        } else if (arg == "--debounce" && i + 1 < argc) {
            options.debounceFrames = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return std::nullopt;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return std::nullopt;
        }
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

int runSearchLoop(const Options& options, FrameSource& source,
                  TeletubbyDetector* detector, bool showWindow)
{
    const std::string windowName = "mars-cv";
    int consecutiveDetections = 0;
    bool detectionLogged = false;

    while (true) {
        cv::Mat frame;
        if (!source.next(frame)) {
            break;
        }

        std::vector<Detection> detections;
        if (detector != nullptr) {
            detections = detector->detect(frame, options.confidence);
        }

        const bool frameHasDetection = !detections.empty();
        if (frameHasDetection) {
            ++consecutiveDetections;
        } else {
            consecutiveDetections = 0;
            detectionLogged = false;
        }

        if (consecutiveDetections >= options.debounceFrames && !detectionLogged) {
            std::cout << "TELETUBBY DETECTED (confidence "
                      << bestConfidence(detections) << ")" << std::endl;
            detectionLogged = true;
        }

        if (detector != nullptr) {
            drawDetections(frame, detections);
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
    const std::optional<Options> options = parseOptions(argc, argv);
    if (!options.has_value()) {
        return 0;
    }

    printBuildInfo();

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
        if (!detector->load(options->modelPath)) {
            return 1;
        }
        std::cout << "Loaded model: " << options->modelPath << std::endl;
    }

    const bool showWindow = !options->noDisplay && !options->forceHeadless && hasDisplay();
    const bool headless = options->forceHeadless || !hasDisplay();

    if (options->loop) {
        if (showWindow) {
            std::cout << "Search loop running. Press q or Esc to exit." << std::endl;
        } else {
            std::cout << "Search loop running." << std::endl;
        }
        return runSearchLoop(*options, *source, detector.get(), showWindow);
    }

    return runSingleFrame(*options, *source, detector.get(), headless, showWindow);
}
