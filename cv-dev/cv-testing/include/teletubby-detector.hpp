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
    static constexpr int kDefaultInputSize = 640;

    bool load(const std::string& onnxPath, int inputSize = kDefaultInputSize);
    bool loaded() const { return loaded_; }
    int inputSize() const { return inputSize_; }

    std::vector<Detection> detect(const cv::Mat& frame, float confThreshold = 0.4f);

private:
    cv::Mat preprocess(const cv::Mat& frame, float& scale, int& padX, int& padY) const;
    std::vector<Detection> postprocess(const cv::Mat& output, float confThreshold,
                                       float scale, int padX, int padY,
                                       const cv::Size& frameSize) const;

    cv::dnn::Net net_;
    bool loaded_ = false;
    int inputSize_ = kDefaultInputSize;
};
