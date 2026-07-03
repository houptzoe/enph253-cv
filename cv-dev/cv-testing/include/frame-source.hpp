#pragma once

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <memory>
#include <string>

class FrameSource {
public:
    static std::unique_ptr<FrameSource> demo();
    static std::unique_ptr<FrameSource> fromCamera(int deviceIndex);
    static std::unique_ptr<FrameSource> fromImage(const std::string& path);
    static std::unique_ptr<FrameSource> fromVideo(const std::string& path, bool loopVideo);

    ~FrameSource();

    bool next(cv::Mat& frame);
    const std::string& description() const { return description_; }

private:
    enum class Kind { Demo, Camera, Image, Video };

    FrameSource() = default;

    Kind kind_ = Kind::Demo;
    std::string description_;
    std::string imagePath_;
    std::string videoPath_;
    cv::Mat cachedImage_;
    cv::VideoCapture videoCapture_;
    bool loopVideo_ = false;
    std::unique_ptr<class Camera> camera_;
};
