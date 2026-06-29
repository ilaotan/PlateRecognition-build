// SPDX-License-Identifier: MIT
// NCNN-based YOLOv7 plate detector. CPU-only, Android-friendly.
// Mirrors the public interface of the legacy TensorRT-based
// `Detector_Yolov7Plate` so the algorithm glue layer can be reused with
// minimal changes.

#ifndef PLATE_DETECTOR_YOLOV7_NCNN_H
#define PLATE_DETECTOR_YOLOV7_NCNN_H

#include <memory>
#include <string>
#include <vector>

#include <ncnn/net.h>

#include <opencv2/core.hpp>

#include "DataTypes_Plate.h"

namespace plate::ncnn_backend {

class Yolov7PlateDetector {
 public:
  Yolov7PlateDetector() = default;
  ~Yolov7PlateDetector() = default;

  // Initialize from a `.param`/`.bin` NCNN model on disk.
  // `param_path` points to the `.param` file; the `.bin` is assumed to
  // share the same directory and stem.
  HZFLAG Init(const std::string& param_path,
              float conf_threshold,
              float nms_threshold,
              int num_threads = 4);

  HZFLAG Detect(const cv::Mat& bgr, std::vector<Det>& dets);

  void Release();

 private:
  // Letterbox + warp an input image to model input size.
  // Returns the warp matrix used for inverse mapping (d2i, 6 floats).
  void LetterBox(const cv::Mat& src,
                 cv::Mat& dst,
                 float& scale,
                 int& pad_w,
                 int& pad_h) const;

  // Decode raw yolov7 output into candidate detections.
  void GenerateProposals(const std::vector<cv::Mat>& outs,
                         std::vector<Det>& dets,
                         float scale,
                         int pad_w,
                         int pad_h) const;

  // Non-maximum suppression on a vector of Det (in-place, keeps highest).
  void NMS(std::vector<Det>& dets, float iou_threshold) const;

  ncnn::Net net_;
  bool initialized_ = false;
  int input_w_ = 640;
  int input_h_ = 640;
  int num_classes_ = 2;     // 0 single layer, 1 double layer
  int num_keypoints_ = 4;
  int num_threads_ = 4;
  float conf_threshold_ = 0.3f;
  float nms_threshold_ = 0.3f;
};

}  // namespace plate::ncnn_backend

#endif  // PLATE_DETECTOR_YOLOV7_NCNN_H
