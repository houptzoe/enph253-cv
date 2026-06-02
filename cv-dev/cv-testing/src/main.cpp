#include <cstdlib>
#include <iostream>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

static bool hasDisplay()
{
#if defined(_WIN32)
    return true;
#else
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
#endif
}

static void printBuildInfo()
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

int main(int argc, char* argv[])
{
    bool useCamera = false;
    bool forceHeadless = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--camera") {
            useCamera = true;
        } else if (arg == "--headless") {
            forceHeadless = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: mars-cv [--camera] [--headless]\n"
                      << "  --camera    Capture one frame from camera index 0\n"
                      << "  --headless  Save output.jpg instead of opening a window\n";
            return 0;
        }
    }

    printBuildInfo();

    cv::Mat frame;
    if (useCamera) {
        cv::VideoCapture cap(0);
        if (!cap.isOpened()) {
            std::cerr << "Failed to open camera 0. On Pi, enable the camera and check /dev/video0.\n";
            return 1;
        }

        cap >> frame;
        if (frame.empty()) {
            std::cerr << "Camera returned an empty frame.\n";
            return 1;
        }
    } else {
        frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(0, 128, 255));
        cv::putText(frame, "mars-cv", cv::Point(200, 250),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 2);
    }

    const bool headless = forceHeadless || !hasDisplay();
    if (headless) {
        const std::string outputPath = "output.jpg";
        if (!cv::imwrite(outputPath, frame)) {
            std::cerr << "Failed to write " << outputPath << std::endl;
            return 1;
        }
        std::cout << "Headless mode: saved " << outputPath << std::endl;
    } else {
        const std::string windowName = "mars-cv";
        cv::imshow(windowName, frame);
        std::cout << "Press any key in the window to exit." << std::endl;
        cv::waitKey(0);
        cv::destroyAllWindows();
    }

    return 0;
}
