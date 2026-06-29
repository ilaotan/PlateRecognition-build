// SPDX-License-Identifier: MIT
// NCNN-based plate recognition network. Reads a `.param`/`.bin` pair
// derived from `plate_recognition_color.onnx` and decodes two softmax
// heads (plate text + plate color) using the legacy PlateRecognition
// vocabulary (78 classes for plate text, 5 for color).

#ifndef PLATE_RECOGNITION_NCNN_H
#define PLATE_RECOGNITION_NCNN_H

#include <memory>
#include <string>
#include <vector>

#include <ncnn/net.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "DataTypes_Plate.h"

namespace plate::ncnn_backend {

class PlateRecognizer {
 public:
  PlateRecognizer() = default;
  ~PlateRecognizer() = default;

  HZFLAG Init(const std::string& param_path,
              int num_threads = 4);

  // Run plate recognition on a single cropped + warped plate image.
  // The image is resized to (input_h_, input_w_) with optional center
  // crop (matching the original PlateRecognition preprocessing).
  HZFLAG Run(const cv::Mat& plate_bgr,
             std::string& plate_str,
             std::string& plate_color);

  void Release();

  // Vocabulary (78 plate characters, matches the original model).
  static constexpr int kPlateCharsetSize = 78;
  static constexpr int kColorCharsetSize = 5;

 private:
  cv::Mat CenterCrop(const cv::Mat& img) const;
  static const char* const kPlateCharset[];
  static const char* const kColorCharset[];

  ncnn::Net net_;
  bool initialized_ = false;
  int input_w_ = 168;
  int input_h_ = 48;
  int num_threads_ = 4;
};

}  // namespace plate::ncnn_backend

#endif  // PLATE_RECOGNITION_NCNN_H
