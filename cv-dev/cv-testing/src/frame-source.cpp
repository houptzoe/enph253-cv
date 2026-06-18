#include "frame-source.hpp"

#include "camera.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <stdexcept>
#include <utility>

FrameSource::~FrameSource() = default;

std::unique_ptr<FrameSource> FrameSource::demo()
{
    auto source = std::unique_ptr<FrameSource>(new FrameSource());
    source->kind_ = Kind::Demo;
    source->description_ = "demo placeholder";
    return source;
}

std::unique_ptr<FrameSource> FrameSource::fromCamera(int deviceIndex)
{
    auto source = std::unique_ptr<FrameSource>(new FrameSource());
    source->kind_ = Kind::Camera;
    source->camera_ = std::make_unique<Camera>(deviceIndex);
    if (!source->camera_->tryOpen()) {
        throw std::runtime_error("Failed to open camera device " + std::to_string(deviceIndex));
    }

    if (!source->camera_->devicePath().empty()) {
        source->description_ = source->camera_->devicePath();
    } else {
        source->description_ = "camera device " + std::to_string(source->camera_->deviceIndex());
    }
    return source;
}

std::unique_ptr<FrameSource> FrameSource::fromImage(const std::string& path)
{
    cv::Mat image = cv::imread(path);
    if (image.empty()) {
        throw std::runtime_error("Failed to read image: " + path);
    }

    auto source = std::unique_ptr<FrameSource>(new FrameSource());
    source->kind_ = Kind::Image;
    source->imagePath_ = path;
    source->cachedImage_ = std::move(image);
    source->description_ = path;
    return source;
}

std::unique_ptr<FrameSource> FrameSource::fromVideo(const std::string& path, bool loopVideo)
{
    cv::VideoCapture capture(path);
    if (!capture.isOpened()) {
        throw std::runtime_error("Failed to open video: " + path);
    }

    auto source = std::unique_ptr<FrameSource>(new FrameSource());
    source->kind_ = Kind::Video;
    source->videoPath_ = path;
    source->videoCapture_ = std::move(capture);
    source->loopVideo_ = loopVideo;
    source->description_ = path;
    return source;
}

bool FrameSource::next(cv::Mat& frame)
{
    switch (kind_) {
    case Kind::Demo: {
        frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(0, 128, 255));
        cv::putText(frame, "mars-cv", cv::Point(200, 250),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(255, 255, 255), 2);
        return true;
    }
    case Kind::Camera:
        return camera_ && camera_->captureFrame(frame);
    case Kind::Image:
        frame = cachedImage_.clone();
        return !frame.empty();
    case Kind::Video: {
        videoCapture_ >> frame;
        if (!frame.empty()) {
            return true;
        }
        if (!loopVideo_) {
            return false;
        }
        videoCapture_.set(cv::CAP_PROP_POS_FRAMES, 0);
        videoCapture_ >> frame;
        return !frame.empty();
    }
    }

    return false;
}
