// SPDX-License-Identifier: MIT
// Implementation of the NCNN-based plate algorithm glue layer.

#include "plate_algorithm_ncnn.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace plate::ncnn_backend {

namespace {

inline float GetNorm2(float x, float y) {
  return std::sqrt(x * x + y * y);
}

}  // namespace

PlateAlgorithm::~PlateAlgorithm() { Release(); }

HZFLAG PlateAlgorithm::Initialize(const NcnnConfig& cfg) {
  Release();
  cfg_ = cfg;

  if (cfg_.enable_detect) {
    detector_ = std::make_unique<Yolov7PlateDetector>();
    std::string detect_param = cfg_.detect_model_dir + "/yolov7plate.param";
    HZFLAG rc = detector_->Init(detect_param, cfg_.detect_conf_threshold,
                                 cfg_.detect_nms_threshold, cfg_.num_threads);
    if (rc != HZ_SUCCESS) {
      detector_.reset();
      return rc;
    }
  }

  if (cfg_.enable_recognize) {
      recognizer_ = std::make_unique<PlateRecognizer>();
      std::string recog_param = cfg_.recog_model_dir + "/plate_recognition_color.param";
      HZFLAG rc = recognizer_->Init(recog_param, cfg_.num_threads);
    if (rc != HZ_SUCCESS) {
      recognizer_.reset();
      return rc;
    }
  }

  initialized_ = true;
  std::cout << "[PlateAlgorithm] NCNN pipeline ready." << std::endl;
  return HZ_SUCCESS;
}

void PlateAlgorithm::Release() {
  if (detector_) {
    detector_->Release();
    detector_.reset();
  }
  if (recognizer_) {
    recognizer_->Release();
    recognizer_.reset();
  }
  initialized_ = false;
}

cv::Mat PlateAlgorithm::PerspectiveCrop(const cv::Mat& src,
                                        const float key_points[8]) const {
  cv::Point2f order_rect[4];
  for (int k = 0; k < 4; ++k) {
    order_rect[k] = cv::Point2f(key_points[2 * k], key_points[2 * k + 1]);
  }
  cv::Point2f w1 = order_rect[0] - order_rect[1];
  cv::Point2f w2 = order_rect[2] - order_rect[3];
  float width1 = GetNorm2(w1.x, w1.y);
  float width2 = GetNorm2(w2.x, w2.y);
  float maxWidth = std::max(width1, width2);

  cv::Point2f h1 = order_rect[0] - order_rect[3];
  cv::Point2f h2 = order_rect[1] - order_rect[2];
  float height1 = GetNorm2(h1.x, h1.y);
  float height2 = GetNorm2(h2.x, h2.y);
  float maxHeight = std::max(height1, height2);

  std::vector<cv::Point2f> pts_ori(4);
  std::vector<cv::Point2f> pts_std = {
      cv::Point2f(0, 0),
      cv::Point2f(maxWidth, 0),
      cv::Point2f(maxWidth, maxHeight),
      cv::Point2f(0, maxHeight),
  };
  for (int k = 0; k < 4; ++k) {
    pts_ori[k] = order_rect[k];
  }
  cv::Mat M = cv::getPerspectiveTransform(pts_ori, pts_std);
  cv::Mat dst;
  cv::warpPerspective(src, dst, M, cv::Size(maxWidth, maxHeight));
  return dst;
}

cv::Mat PlateAlgorithm::SplitMergeDoubleLayer(const cv::Mat& img) const {
  // Match the original implementation: upper 5/12 rows, lower 2/3..end
  // are stacked horizontally with constant 114 padding.
  cv::Rect upper_rect(0, 0, img.cols, static_cast<int>(5.0 / 12.0 * img.rows));
  cv::Rect lower_rect(0, static_cast<int>(1.0 / 3.0 * img.rows), img.cols,
                      img.rows - static_cast<int>(1.0 / 3.0 * img.rows));
  cv::Mat upper = img(upper_rect);
  cv::Mat lower = img(lower_rect);
  cv::resize(upper, upper, cv::Size(lower.cols, lower.rows));
  cv::Mat out(lower.rows, lower.cols + upper.cols, CV_8UC3,
              cv::Scalar(114, 114, 114));
  upper.copyTo(out(cv::Rect(0, 0, upper.cols, upper.rows)));
  lower.copyTo(out(cv::Rect(upper.cols, 0, lower.cols, lower.rows)));
  return out;
}

HZFLAG PlateAlgorithm::Detect(const cv::Mat& bgr,
                               std::vector<PlateDet>& plates) {
  plates.clear();
  if (!initialized_) {
    return HZ_INITFAILED;
  }
  if (bgr.empty()) {
    return HZ_IMGEMPTY;
  }
  if (!detector_ || !recognizer_) {
    return HZ_INITFAILED;
  }

  std::vector<Det> dets;
  HZFLAG rc = detector_->Detect(bgr, dets);
  if (rc != HZ_SUCCESS) {
    return rc;
  }

  for (const auto& d : dets) {
    PlateDet pd;
    std::memset(&pd, 0, sizeof(PlateDet));
    pd.bbox = d.bbox;
    pd.confidence = d.confidence;
    pd.label = d.label;
    for (int k = 0; k < 8; ++k) {
      pd.key_points[k] = (k < static_cast<int>(d.key_points.size()))
                             ? d.key_points[k]
                             : 0.f;
    }
    pd.plate_license = new char[32];
    pd.plate_color = new char[16];
    std::memset(pd.plate_license, 0, 32);
    std::memset(pd.plate_color, 0, 16);

    cv::Mat roi = PerspectiveCrop(bgr, pd.key_points);
    if (d.label == 1) {
      roi = SplitMergeDoubleLayer(roi);
    }

    std::string plate_str;
    std::string plate_color;
    if (recognizer_->Run(roi, plate_str, plate_color) == HZ_SUCCESS) {
      std::strncpy(pd.plate_license, plate_str.c_str(), 31);
      std::strncpy(pd.plate_color, plate_color.c_str(), 15);
    }
    plates.push_back(pd);
  }
  return HZ_SUCCESS;
}

// -------- C entry points (mirror the legacy ABI) --------

void* Initialize(NcnnConfig* cfg) {
  if (!cfg) return nullptr;
  auto* algo = new PlateAlgorithm();
  if (algo->Initialize(*cfg) != HZ_SUCCESS) {
    delete algo;
    return nullptr;
  }
  return algo;
}

int PlateRecognition_yolov7(void* handle, Plate_ImageData* img, PlateDet* out) {
  if (!handle || !img || !img->image) return -1;
  auto* algo = static_cast<PlateAlgorithm*>(handle);
  cv::Mat mat(img->height, img->width, CV_8UC3, img->image);
  std::vector<PlateDet> plates;
  HZFLAG rc = algo->Detect(mat, plates);
  if (rc != HZ_SUCCESS) return rc;
  for (size_t i = 0; i < plates.size(); ++i) {
    out[i].bbox = plates[i].bbox;
    out[i].label = plates[i].label;
    out[i].confidence = plates[i].confidence;
    for (int k = 0; k < 8; ++k) out[i].key_points[k] = plates[i].key_points[k];
    std::strncpy(out[i].plate_license, plates[i].plate_license, 31);
    std::strncpy(out[i].plate_color, plates[i].plate_color, 15);
  }
  // Cleanup local PlateDet allocations.
  for (auto& p : plates) {
    delete[] p.plate_license;
    delete[] p.plate_color;
  }
  return static_cast<int>(plates.size());
}

int Release(void* handle) {
  if (!handle) return -1;
  auto* algo = static_cast<PlateAlgorithm*>(handle);
  algo->Release();
  delete algo;
  return 0;
}

}  // namespace plate::ncnn_backend
