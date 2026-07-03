#include "teletubby-detector.hpp"

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr float kNmsThreshold = 0.45f;

cv::Mat letterbox(const cv::Mat& frame, int size, float& scale, int& padX, int& padY)
{
    scale = std::min(static_cast<float>(size) / frame.cols,
                     static_cast<float>(size) / frame.rows);
    const int newWidth = static_cast<int>(std::round(frame.cols * scale));
    const int newHeight = static_cast<int>(std::round(frame.rows * scale));
    padX = (size - newWidth) / 2;
    padY = (size - newHeight) / 2;

    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(newWidth, newHeight));

    cv::Mat padded(size, size, frame.type(), cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(padX, padY, newWidth, newHeight)));
    return padded;
}

cv::Rect toFrameRect(float centerX, float centerY, float width, float height,
                     float scale, int padX, int padY, const cv::Size& frameSize)
{
    const float x = (centerX - width / 2.0f - padX) / scale;
    const float y = (centerY - height / 2.0f - padY) / scale;
    const float w = width / scale;
    const float h = height / scale;

    cv::Rect rect(static_cast<int>(std::round(x)),
                  static_cast<int>(std::round(y)),
                  static_cast<int>(std::round(w)),
                  static_cast<int>(std::round(h)));
    rect &= cv::Rect(0, 0, frameSize.width, frameSize.height);
    return rect;
}
} // namespace

bool TeletubbyDetector::load(const std::string& onnxPath)
{
    try {
        net_ = cv::dnn::readNetFromONNX(onnxPath);
    } catch (const cv::Exception& ex) {
        std::cerr << "Failed to load ONNX model: " << ex.what() << std::endl;
        loaded_ = false;
        return false;
    }

    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    loaded_ = true;
    return true;
}

cv::Mat TeletubbyDetector::preprocess(const cv::Mat& frame, float& scale, int& padX, int& padY) const
{
    return letterbox(frame, kInputSize, scale, padX, padY);
}

std::vector<Detection> TeletubbyDetector::postprocess(const cv::Mat& output, float confThreshold,
                                                      float scale, int padX, int padY,
                                                      const cv::Size& frameSize) const
{
    cv::Mat detections = output;
    if (detections.dims == 3 && detections.size[0] == 1) {
        const int dim1 = detections.size[1];
        const int dim2 = detections.size[2];
        detections = detections.reshape(1, dim1);
        if (dim1 < dim2) {
            cv::transpose(detections, detections);
        }
    } else if (detections.dims == 2 && detections.rows < detections.cols) {
        cv::transpose(detections, detections);
    }

    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    const int numClasses = detections.cols - 4;
    for (int i = 0; i < detections.rows; ++i) {
        const float* row = detections.ptr<float>(i);
        const float centerX = row[0];
        const float centerY = row[1];
        const float width = row[2];
        const float height = row[3];

        float bestScore = 0.0f;
        int bestClass = -1;
        for (int c = 0; c < numClasses; ++c) {
            const float score = row[4 + c];
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }

        if (bestClass < 0 || bestScore < confThreshold) {
            continue;
        }

        cv::Rect box = toFrameRect(centerX, centerY, width, height, scale, padX, padY, frameSize);
        if (box.width <= 0 || box.height <= 0) {
            continue;
        }

        classIds.push_back(bestClass);
        confidences.push_back(bestScore);
        boxes.push_back(box);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, kNmsThreshold, keep);

    std::vector<Detection> results;
    results.reserve(keep.size());
    for (int index : keep) {
        results.push_back({boxes[index], confidences[index]});
    }
    return results;
}

std::vector<Detection> TeletubbyDetector::detect(const cv::Mat& frame, float confThreshold)
{
    if (!loaded_ || frame.empty()) {
        return {};
    }

    float scale = 1.0f;
    int padX = 0;
    int padY = 0;
    const cv::Mat input = preprocess(frame, scale, padX, padY);

    cv::Mat blob = cv::dnn::blobFromImage(input, 1.0 / 255.0,
                                          cv::Size(kInputSize, kInputSize),
                                          cv::Scalar(), true, false);
    net_.setInput(blob);
    std::vector<cv::Mat> outputs;
    net_.forward(outputs, net_.getUnconnectedOutLayersNames());
    if (outputs.empty()) {
        return {};
    }

    return postprocess(outputs[0], confThreshold, scale, padX, padY, frame.size());
}
