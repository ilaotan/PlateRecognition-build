// SPDX-License-Identifier: MIT
// NCNN-based plate recognition implementation.

#include "platerecognition_ncnn.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace plate::ncnn_backend {

// Match the PlateRecognition.cpp vocabulary exactly so the converted
// .param/.bin yields the same text/color output.
const char* const PlateRecognizer::kPlateCharset[] = {
    "#", "京", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑", "苏",
    "浙", "皖", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤", "桂", "琼", "川",
    "贵", "云", "藏", "陕", "甘", "青", "宁", "新", "学", "警", "港", "澳",
    "挂", "使", "领", "民", "航", "危", "0",  "1",  "2",  "3",  "4",  "5",
    "6",  "7",  "8",  "9",  "A",  "B",  "C",  "D",  "E",  "F",  "G",  "H",
    "J",  "K",  "L",  "M",  "N",  "P",  "Q",  "R",  "S",  "T",  "U",  "V",
    "W",  "X",  "Y",  "Z",  "险", "品"};

const char* const PlateRecognizer::kColorCharset[] = {
    "黑色", "蓝色", "绿色", "白色", "黄色"};

HZFLAG PlateRecognizer::Init(const std::string& param_path, int num_threads) {
  if (initialized_) {
    Release();
  }
  std::string bin_path = param_path;
  const std::string ext = ".param";
  if (bin_path.size() >= ext.size() &&
      bin_path.compare(bin_path.size() - ext.size(), ext.size(), ext) == 0) {
    bin_path.replace(bin_path.size() - ext.size(), ext.size(), ".bin");
  } else {
    std::cerr << "[PlateRecognizer] param file must end in .param: "
              << param_path << std::endl;
    return HZ_WITHOUTMODEL;
  }

  num_threads_ = num_threads;

  net_.opt.use_vulkan_compute = false;
  net_.opt.use_int8_inference = false;
  net_.opt.use_fp16_storage = false;
  net_.opt.use_fp16_arithmetic = false;
  net_.opt.num_threads = num_threads_;

  if (net_.load_param(param_path.c_str()) != 0) {
    std::cerr << "[PlateRecognizer] load_param failed: " << param_path
              << std::endl;
    return HZ_WITHOUTMODEL;
  }
  if (net_.load_model(bin_path.c_str()) != 0) {
    std::cerr << "[PlateRecognizer] load_model failed: " << bin_path
              << std::endl;
    return HZ_WITHOUTMODEL;
  }

  initialized_ = true;
  std::cout << "[PlateRecognizer] Initialized: " << param_path
            << " threads=" << num_threads_ << std::endl;
  return HZ_SUCCESS;
}

void PlateRecognizer::Release() {
  if (initialized_) {
    net_.clear();
    initialized_ = false;
  }
}

cv::Mat PlateRecognizer::CenterCrop(const cv::Mat& img) const {
  // PlateRecognition original preprocessing does a centered square crop
  // by min(width, height) on both sides, then resizes to (input_h_, input_w_).
  int w = img.cols;
  int h = img.rows;
  int m = std::min(w, h);
  int x = (w - m) / 2;
  int y = (h - m) / 2;
  cv::Mat cropped = img(cv::Rect(x, y, m, m)).clone();
  cv::Mat resized;
  cv::resize(cropped, resized, cv::Size(input_w_, input_h_));
  return resized;
}

HZFLAG PlateRecognizer::Run(const cv::Mat& plate_bgr,
                            std::string& plate_str,
                            std::string& plate_color) {
  plate_str.clear();
  plate_color.clear();
  if (!initialized_) {
    return HZ_INITFAILED;
  }
  if (plate_bgr.empty()) {
    return HZ_IMGEMPTY;
  }

  cv::Mat pre;
  cv::resize(plate_bgr, pre, cv::Size(input_w_, input_h_));

  ncnn::Mat in = ncnn::Mat::from_pixels(pre.data, ncnn::Mat::PIXEL_BGR,
                                         input_w_, input_h_);
  const float mean_vals[3] = {0.588f * 255.f, 0.588f * 255.f, 0.588f * 255.f};
  const float norm_vals[3] = {1 / (0.193f * 255.f), 1 / (0.193f * 255.f),
                              1 / (0.193f * 255.f)};
  in.substract_mean_normalize(mean_vals, norm_vals);

  ncnn::Extractor ex = net_.create_extractor();
  ex.set_num_threads(num_threads_);
  ex.input("images", in);

  // Two softmax heads. The original graph names them as
  // "output" (text) and "color" (color). The ONNX->NCNN tool
  // preserves node names; the workflow renames them via a small
  // .param edit if necessary.
  ncnn::Mat text_logits;
  ncnn::Mat color_logits;
  if (ex.extract("output_1", text_logits) != 0) {
    std::cerr << "[PlateRecognizer] extract text logits failed" << std::endl;
    return HZ_ERROR;
  }
  if (ex.extract("output_2", color_logits) != 0) {
    // Some checkpoints only output plate text; treat as unknown color.
    plate_color = "未知";
  } else {
    int best = 0;
    float best_val = -1e9f;
    for (int i = 0; i < color_logits.w; ++i) {
      float v = color_logits.row(0)[i];
      if (v > best_val) {
        best_val = v;
        best = i;
      }
    }
    if (best >= 0 && best < kColorCharsetSize) {
      plate_color = kColorCharset[best];
    } else {
      plate_color = "未知";
    }
  }

  // Text head is CTC-like: shape (T, num_classes). We do greedy argmax
  // per time step, then collapse repeats and skip blank (id=0).
  if (text_logits.w != kPlateCharsetSize) {
    std::cerr << "[PlateRecognizer] unexpected text head width "
              << text_logits.w << " (expected " << kPlateCharsetSize << ")"
              << std::endl;
    return HZ_ERROR;
  }
  const int T = text_logits.h;
  if (T <= 0) {
    return HZ_SUCCESS;
  }
  int prev = -1;
  std::string out;
  out.reserve(T);
  for (int t = 0; t < T; ++t) {
    const float* row = text_logits.row(t);
    int best = 0;
    float best_val = -1e9f;
    for (int i = 0; i < text_logits.w; ++i) {
      float v = row[i];
      if (v > best_val) {
        best_val = v;
        best = i;
      }
    }
    if (best != 0 && best != prev) {
      if (best >= 0 && best < kPlateCharsetSize) {
        out += kPlateCharset[best];
      }
    }
    prev = best;
  }
  plate_str = out;
  return HZ_SUCCESS;
}

}  // namespace plate::ncnn_backend
