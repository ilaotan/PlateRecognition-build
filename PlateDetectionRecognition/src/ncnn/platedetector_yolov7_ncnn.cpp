// SPDX-License-Identifier: MIT
// NCNN-based YOLOv7 plate detector implementation.

#include "platedetector_yolov7_ncnn.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

namespace plate::ncnn_backend {

namespace {

inline float Sigmoid(float x) {
  return 1.0f / (1.0f + std::exp(-x));
}

inline float Clip(float x, float lo, float hi) {
  return std::min(hi, std::max(lo, x));
}

float IoU(const Det& a, const Det& b) {
  float xx1 = std::max(a.bbox.xmin, b.bbox.xmin);
  float yy1 = std::max(a.bbox.ymin, b.bbox.ymin);
  float xx2 = std::min(a.bbox.xmax, b.bbox.xmax);
  float yy2 = std::min(a.bbox.ymax, b.bbox.ymax);
  float w = std::max(0.f, xx2 - xx1);
  float h = std::max(0.f, yy2 - yy1);
  float inter = w * h;
  float area_a =
      std::max(0.f, a.bbox.xmax - a.bbox.xmin) *
      std::max(0.f, a.bbox.ymax - a.bbox.ymin);
  float area_b =
      std::max(0.f, b.bbox.xmax - b.bbox.xmin) *
      std::max(0.f, b.bbox.ymax - b.bbox.ymin);
  float uni = area_a + area_b - inter + 1e-6f;
  return inter / uni;
}

}  // namespace

HZFLAG Yolov7PlateDetector::Init(const std::string& param_path,
                                 float conf_threshold,
                                 float nms_threshold,
                                 int num_threads) {
  if (initialized_) {
    Release();
  }

  // Resolve the .bin path next to the .param.
  std::string bin_path = param_path;
  const std::string ext = ".param";
  if (bin_path.size() >= ext.size() &&
      bin_path.compare(bin_path.size() - ext.size(), ext.size(), ext) == 0) {
    bin_path.replace(bin_path.size() - ext.size(), ext.size(), ".bin");
  } else {
    std::cerr << "[Yolov7PlateDetector] param file must end in .param: "
              << param_path << std::endl;
    return HZ_WITHOUTMODEL;
  }

  conf_threshold_ = conf_threshold;
  nms_threshold_ = nms_threshold;
  num_threads_ = num_threads;

  net_.opt.use_vulkan_compute = false;
  net_.opt.use_int8_inference = false;
  net_.opt.use_fp16_storage = false;
  net_.opt.use_fp16_arithmetic = false;
  net_.opt.num_threads = num_threads_;

  int ret = net_.load_param(param_path.c_str());
  if (ret != 0) {
    std::cerr << "[Yolov7PlateDetector] load_param failed: " << param_path
              << " ret=" << ret << std::endl;
    return HZ_WITHOUTMODEL;
  }
  ret = net_.load_model(bin_path.c_str());
  if (ret != 0) {
    std::cerr << "[Yolov7PlateDetector] load_model failed: " << bin_path
              << " ret=" << ret << std::endl;
    return HZ_WITHOUTMODEL;
  }

  initialized_ = true;
  std::cout << "[Yolov7PlateDetector] Initialized: " << param_path
            << " threads=" << num_threads_
            << " conf=" << conf_threshold_
            << " nms=" << nms_threshold_ << std::endl;
  return HZ_SUCCESS;
}

void Yolov7PlateDetector::Release() {
  if (initialized_) {
    net_.clear();
    initialized_ = false;
  }
}

void Yolov7PlateDetector::LetterBox(const cv::Mat& src,
                                     cv::Mat& dst,
                                     float& scale,
                                     int& pad_w,
                                     int& pad_h) const {
  int w = src.cols;
  int h = src.rows;
  scale = std::min(static_cast<float>(input_w_) / w,
                   static_cast<float>(input_h_) / h);
  int new_w = static_cast<int>(std::round(w * scale));
  int new_h = static_cast<int>(std::round(h * scale));
  pad_w = (input_w_ - new_w) / 2;
  pad_h = (input_h_ - new_h) / 2;

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(new_w, new_h));

  // YOLOv7 uses 114/255 gray padding (OpenCV BGR, fill with 114).
  cv::copyMakeBorder(resized, dst, pad_h, input_h_ - new_h - pad_h, pad_w,
                     input_w_ - new_w - pad_w, cv::BORDER_CONSTANT,
                     cv::Scalar(114, 114, 114));
}

HZFLAG Yolov7PlateDetector::Detect(const cv::Mat& bgr,
                                   std::vector<Det>& dets) {
  dets.clear();
  if (!initialized_) {
    return HZ_INITFAILED;
  }
  if (bgr.empty()) {
    return HZ_IMGEMPTY;
  }

  // Preprocess: letterbox + normalize to [0,1] in NCHW float.
  float scale = 1.f;
  int pad_w = 0;
  int pad_h = 0;
  cv::Mat letter;
  LetterBox(bgr, letter, scale, pad_w, pad_h);

  ncnn::Mat in = ncnn::Mat::from_pixels(letter.data, ncnn::Mat::PIXEL_BGR2RGB,
                                         input_w_, input_h_);
  const float mean_vals[3] = {0.f, 0.f, 0.f};
  const float norm_vals[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
  in.substract_mean_normalize(mean_vals, norm_vals);

  // YOLOv7 export with hardcoded input blob name "images" and output "output".
  ncnn::Extractor ex = net_.create_extractor();
  ex.set_num_threads(num_threads_);
  ex.input("images", in);

  ncnn::Mat out;
  ex.extract("output", out);

  const int expected = 5 + num_classes_ + 3 * num_keypoints_;
  int num_anchors;
  int num_box_element;
  bool transposed = false;
  if (out.h == expected) {
    num_anchors = out.w;
    num_box_element = out.h;
  } else if (out.w == expected) {
    num_anchors = out.h;
    num_box_element = out.w;
    transposed = true;
  } else {
    std::cerr << "[Yolov7PlateDetector] Unexpected output dims w=" << out.w
              << " h=" << out.h
              << " expected_box_element=" << expected << std::endl;
    return HZ_ERROR;
  }

  std::vector<Det> proposals;
  proposals.reserve(64);
  for (int i = 0; i < num_anchors; ++i) {
    const float* row = transposed ? out.row(i) : out.row(0) + i * num_box_element;
    const float obj = row[4];
    if (obj < conf_threshold_) {
      continue;
    }
    int best_class = -1;
    float best_score = 0.f;
    for (int c = 0; c < num_classes_; ++c) {
      float s = row[5 + c];
      if (s > best_score) {
        best_score = s;
        best_class = c;
      }
    }
    float score = obj * best_score;
    if (score < conf_threshold_ || best_class < 0) {
      continue;
    }
    // cx,cy,w,h are in the letterboxed coordinate space.
    float cx = row[0];
    float cy = row[1];
    float w = row[2];
    float h = row[3];

    // Inverse letterbox: subtract padding, divide by scale.
    cx = (cx - pad_w) / scale;
    cy = (cy - pad_h) / scale;
    w /= scale;
    h /= scale;

    Det d;
    d.bbox.xmin = cx - w * 0.5f;
    d.bbox.ymin = cy - h * 0.5f;
    d.bbox.xmax = cx + w * 0.5f;
    d.bbox.ymax = cy + h * 0.5f;
    d.confidence = score;
    d.label = best_class;  // 0=single, 1=double
    d.id = best_class;
    d.key_points.clear();
    d.key_points.reserve(num_keypoints_ * 2);
    for (int k = 0; k < num_keypoints_; ++k) {
      float kx = (row[5 + num_classes_ + 3 * k] - pad_w) / scale;
      float ky = (row[5 + num_classes_ + 3 * k + 1] - pad_h) / scale;
      d.key_points.push_back(kx);
      d.key_points.push_back(ky);
    }
    proposals.push_back(d);
  }

  NMS(proposals, nms_threshold_);
  dets.swap(proposals);
  return HZ_SUCCESS;
}

void Yolov7PlateDetector::GenerateProposals(
    const std::vector<cv::Mat>& /*outs*/,
    std::vector<Det>& /*dets*/,
    float /*scale*/,
    int /*pad_w*/,
    int /*pad_h*/) const {
  // Reserved for future per-stride decoding if the ONNX exporter produces
  // a multi-output graph. The current `ex.extract("output")` path handles
  // the standard single-output case inline in Detect().
}

void Yolov7PlateDetector::NMS(std::vector<Det>& dets,
                              float iou_threshold) const {
  if (dets.empty()) return;
  std::sort(dets.begin(), dets.end(), [](const Det& a, const Det& b) {
    return a.confidence > b.confidence;
  });
  std::vector<bool> removed(dets.size(), false);
  std::vector<Det> keep;
  keep.reserve(dets.size());
  for (size_t i = 0; i < dets.size(); ++i) {
    if (removed[i]) continue;
    keep.push_back(dets[i]);
    for (size_t j = i + 1; j < dets.size(); ++j) {
      if (removed[j]) continue;
      if (IoU(dets[i], dets[j]) > iou_threshold) {
        removed[j] = true;
      }
    }
  }
  dets.swap(keep);
}

}  // namespace plate::ncnn_backend
