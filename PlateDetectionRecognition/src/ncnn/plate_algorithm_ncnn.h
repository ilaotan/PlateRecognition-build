// SPDX-License-Identifier: MIT
// Top-level NCNN pipeline. Replicates the public surface of the legacy
// `void* Initialize(Config*)` / `PlateRecognition_yolov7()` / `Release()`
// entry points, so existing desktop callers can be ported to NCNN with
// minimal code change. The JNI bridge in jni_bridge.cpp also depends on
// these symbols.

#ifndef PLATE_ALGORITHM_NCNN_H
#define PLATE_ALGORITHM_NCNN_H

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

#include "DataTypes_Plate.h"
#include "platedetector_yolov7_ncnn.h"
#include "platerecognition_ncnn.h"

namespace plate::ncnn_backend {

// Runtime configuration. Mirrors the legacy `Config` struct but is
// easier to instantiate from JNI/Java.
struct NcnnConfig {
  std::string detect_model_dir;   // contains yolov7plate.param + .bin
  std::string recog_model_dir;    // contains plate_recognition.param + .bin
  float detect_conf_threshold = 0.3f;
  float detect_nms_threshold = 0.3f;
  int num_threads = 4;
  bool enable_detect = true;
  bool enable_recognize = true;
};

class PlateAlgorithm {
 public:
  PlateAlgorithm() = default;
  ~PlateAlgorithm();

  HZFLAG Initialize(const NcnnConfig& cfg);
  HZFLAG Detect(const cv::Mat& bgr, std::vector<PlateDet>& plates);
  void Release();

 private:
  cv::Mat PerspectiveCrop(const cv::Mat& src,
                          const float key_points[8]) const;
  cv::Mat SplitMergeDoubleLayer(const cv::Mat& img) const;

  NcnnConfig cfg_;
  std::unique_ptr<Yolov7PlateDetector> detector_;
  std::unique_ptr<PlateRecognizer> recognizer_;
  bool initialized_ = false;
};

// C entry points - identical signatures to the legacy C ABI.
void* Initialize(NcnnConfig* cfg);
int PlateRecognition_yolov7(void* handle, Plate_ImageData* img, PlateDet* out);
int Release(void* handle);

}  // namespace plate::ncnn_backend

#endif  // PLATE_ALGORITHM_NCNN_H
