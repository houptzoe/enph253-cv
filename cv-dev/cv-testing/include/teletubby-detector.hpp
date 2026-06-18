#pragma once

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include <string>
#include <vector>

struct Detection {
    cv::Rect box;
    float confidence = 0.0f;
};

class TeletubbyDetector {
public:
    static constexpr int kInputSize = 640;

    bool load(const std::string& onnxPath);
    bool loaded() const { return loaded_; }

    std::vector<Detection> detect(const cv::Mat& frame, float confThreshold = 0.5f);

private:
    cv::Mat preprocess(const cv::Mat& frame, float& scale, int& padX, int& padY) const;
    std::vector<Detection> postprocess(const cv::Mat& output, float confThreshold,
                                       float scale, int padX, int padY,
                                       const cv::Size& frameSize) const;

    cv::dnn::Net net_;
    bool loaded_ = false;
};
