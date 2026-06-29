// SPDX-License-Identifier: MIT
// JNI bridge for `com.hyperai.hyperlpr3.core.HyperLPRCore`.
//
// Maps the four native methods declared in
// hyperlpr3-android-sdk/hyperlpr3/src/main/java/com/hyperai/hyperlpr3/core/HyperLPRCore.java
// onto the NCNN-based plate algorithm:
//
//   native long  CreateRecognizerContext(HyperLPRParameter);
//   native int   ReleaseRecognizerContext(long);
//   native Plate[] PlateRecognitionFromBuffer(long, byte[], int, int, int, int);
//   native Plate[] PlateRecognitionFromImage (long, int[] , int, int, int, int);
//
// The Java class is `com.hyperai.hyperlpr3.core.HyperLPRCore`, so JNI
// symbol names follow the
//   Java_com_hyperai_hyperlpr3_core_HyperLPRCore_<method>
// convention.

#include <jni.h>

#include <android/log.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "plate_algorithm_ncnn.h"

#define LOG_TAG "HyperLPR-NCNN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define JNI_EXPORT extern "C" __attribute__((visibility("default")))

using plate::ncnn_backend::NcnnConfig;
using plate::ncnn_backend::PlateAlgorithm;

namespace {

// Read a Java string field via JNI without requiring iostream.
std::string JStrToStdString(JNIEnv* env, jstring js) {
  if (!js) return {};
  const char* c = env->GetStringUTFChars(js, nullptr);
  std::string s(c ? c : "");
  if (c) env->ReleaseStringUTFChars(js, c);
  return s;
}

// Read a Java enum/int field by name, returning a default if absent.
// This avoids hard-coding Java class layouts when the SDK evolves.
template <typename T>
T ReadIntFieldOrDefault(JNIEnv* env, jobject obj, const char* name, T def) {
  if (!obj) return def;
  jclass cls = env->GetObjectClass(obj);
  if (!cls) return def;
  jfieldID fid = env->GetFieldID(cls, name, "I");
  env->DeleteLocalRef(cls);
  if (!fid) {
    jclass cls2 = env->GetObjectClass(obj);
    jfieldID fidz = env->GetFieldID(cls2, name, "Z");
    env->DeleteLocalRef(cls2);
    if (!fidz) return def;
    return static_cast<T>(env->GetBooleanField(obj, fidz));
  }
  return static_cast<T>(env->GetIntField(obj, fid));
}

template <typename T>
T ReadFloatFieldOrDefault(JNIEnv* env, jobject obj, const char* name, T def) {
  if (!obj) return def;
  jclass cls = env->GetObjectClass(obj);
  if (!cls) return def;
  jfieldID fid = env->GetFieldID(cls, name, "F");
  env->DeleteLocalRef(cls);
  if (!fid) return def;
  return static_cast<T>(env->GetFloatField(obj, fid));
}

std::string ReadStringFieldOrEmpty(JNIEnv* env, jobject obj, const char* name) {
  if (!obj) return {};
  jclass cls = env->GetObjectClass(obj);
  if (!cls) return {};
  jfieldID fid = env->GetFieldID(cls, name, "Ljava/lang/String;");
  env->DeleteLocalRef(cls);
  if (!fid) return {};
  jstring js = static_cast<jstring>(env->GetObjectField(obj, fid));
  return JStrToStdString(env, js);
}

// Convert a BGR (or RGB/BGRA) Android byte buffer to a cv::Mat without
// copying. The buffer is not modified, but cv::Mat does not own it.
// Caller must keep the buffer alive for the duration of the cv::Mat.
enum StreamFormat {
  STREAM_BGR = 1,
  STREAM_RGB = 0,
  STREAM_RGBA = 2,
  STREAM_BGRA = 3,
  STREAM_NV12 = 4,
  STREAM_NV21 = 5,
};

cv::Mat WrapBuffer(JNIEnv* env, jbyteArray buf, jint height, jint width,
                   jint rotation, jint format) {
  jbyte* data = env->GetByteArrayElements(buf, nullptr);
  if (!data) return {};
  cv::Mat mat;
  switch (format) {
    case STREAM_BGR:
      mat = cv::Mat(height, width, CV_8UC3, data).clone();
      break;
    case STREAM_RGB:
      mat = cv::Mat(height, width, CV_8UC3, data).clone();
      cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
      break;
    case STREAM_BGRA:
      mat = cv::Mat(height, width, CV_8UC4, data).clone();
      cv::cvtColor(mat, mat, cv::COLOR_BGRA2BGR);
      break;
    case STREAM_RGBA:
      mat = cv::Mat(height, width, CV_8UC4, data).clone();
      cv::cvtColor(mat, mat, cv::COLOR_RGBA2BGR);
      break;
    case STREAM_NV12:
    case STREAM_NV21: {
      cv::Mat yuv(height * 3 / 2, width, CV_8UC1, data);
      cv::cvtColor(yuv, mat, format == STREAM_NV12 ? cv::COLOR_YUV2BGR_NV12
                                                   : cv::COLOR_YUV2BGR_NV21);
      break;
    }
    default:
      mat = cv::Mat(height, width, CV_8UC3, data).clone();
      break;
  }
  env->ReleaseByteArrayElements(buf, data, JNI_ABORT);

  // Apply rotation.
  if (rotation == 1) {  // 90
    cv::rotate(mat, mat, cv::ROTATE_90_CLOCKWISE);
  } else if (rotation == 2) {  // 180
    cv::rotate(mat, mat, cv::ROTATE_180);
  } else if (rotation == 3) {  // 270
    cv::rotate(mat, mat, cv::ROTATE_90_COUNTERCLOCKWISE);
  }
  return mat;
}

cv::Mat WrapIntBuffer(JNIEnv* env, jintArray buf, jint height, jint width,
                      jint rotation, jint format) {
  jint* data = env->GetIntArrayElements(buf, nullptr);
  if (!data) return {};
  cv::Mat mat;
  // Android Bitmap.getPixels returns ARGB_8888 ints.
  cv::Mat argb(height, width, CV_8UC4, data);
  if (format == STREAM_BGR) {
    cv::cvtColor(argb, mat, cv::COLOR_BGRA2BGR);
  } else if (format == STREAM_RGB) {
    cv::cvtColor(argb, mat, cv::COLOR_BGRA2RGB);
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
  } else {
    cv::cvtColor(argb, mat, cv::COLOR_BGRA2BGR);
  }
  mat = mat.clone();
  env->ReleaseIntArrayElements(buf, data, JNI_ABORT);

  if (rotation == 1) {
    cv::rotate(mat, mat, cv::ROTATE_90_CLOCKWISE);
  } else if (rotation == 2) {
    cv::rotate(mat, mat, cv::ROTATE_180);
  } else if (rotation == 3) {
    cv::rotate(mat, mat, cv::ROTATE_90_COUNTERCLOCKWISE);
  }
  return mat;
}

jobjectArray BuildPlateArray(JNIEnv* env, const std::vector<PlateDet>& plates) {
  jclass plate_cls = env->FindClass("com/hyperai/hyperlpr3/bean/Plate");
  if (!plate_cls) {
    LOGE("Plate class not found");
    return nullptr;
  }
  jmethodID ctor = env->GetMethodID(plate_cls, "<init>", "()V");
  jobjectArray arr = env->NewObjectArray(static_cast<jsize>(plates.size()),
                                          plate_cls, nullptr);
  jfieldID fx1 = env->GetFieldID(plate_cls, "x1", "F");
  jfieldID fy1 = env->GetFieldID(plate_cls, "y1", "F");
  jfieldID fx2 = env->GetFieldID(plate_cls, "x2", "F");
  jfieldID fy2 = env->GetFieldID(plate_cls, "y2", "F");
  jfieldID ftype = env->GetFieldID(plate_cls, "type", "I");
  jfieldID fconf = env->GetFieldID(plate_cls, "confidence", "F");
  jfieldID fcode = env->GetFieldID(plate_cls, "code", "Ljava/lang/String;");

  for (size_t i = 0; i < plates.size(); ++i) {
    jobject obj = env->NewObject(plate_cls, ctor);
    env->SetFloatField(obj, fx1, plates[i].bbox.xmin);
    env->SetFloatField(obj, fy1, plates[i].bbox.ymin);
    env->SetFloatField(obj, fx2, plates[i].bbox.xmax);
    env->SetFloatField(obj, fy2, plates[i].bbox.ymax);
    env->SetIntField(obj, ftype, plates[i].label);
    env->SetFloatField(obj, fconf, plates[i].confidence);
    jstring code = env->NewStringUTF(plates[i].plate_license[0]
                                         ? plates[i].plate_license
                                         : "");
    env->SetObjectField(obj, fcode, code);
    env->DeleteLocalRef(code);
    env->SetObjectArrayElement(arr, static_cast<jsize>(i), obj);
    env->DeleteLocalRef(obj);
  }
  env->DeleteLocalRef(plate_cls);
  return arr;
}

}  // namespace

// =================== JNI exports ===================

JNI_EXPORT jlong JNICALL
Java_com_hyperai_hyperlpr3_core_HyperLPRCore_CreateRecognizerContext(
    JNIEnv* env, jobject /*thiz*/, jobject param_obj) {
  NcnnConfig cfg;
  cfg.detect_model_dir = ReadStringFieldOrEmpty(env, param_obj, "modelPath");
  cfg.recog_model_dir = cfg.detect_model_dir;
  cfg.detect_conf_threshold = ReadFloatFieldOrDefault<float>(
      env, param_obj, "boxConfThreshold", 0.3f);
  cfg.detect_nms_threshold = ReadFloatFieldOrDefault<float>(
      env, param_obj, "nmsThreshold", 0.3f);
  cfg.num_threads = ReadIntFieldOrDefault<int>(env, param_obj, "threads", 4);
  cfg.enable_detect = true;
  cfg.enable_recognize = true;

  if (cfg.detect_model_dir.empty()) {
    LOGE("CreateRecognizerContext: modelPath is empty");
    return 0;
  }
  auto* algo = new PlateAlgorithm();
  if (algo->Initialize(cfg) != HZ_SUCCESS) {
    delete algo;
    return 0;
  }
  LOGI("CreateRecognizerContext ok: path=%s threads=%d", cfg.detect_model_dir.c_str(),
       cfg.num_threads);
  return reinterpret_cast<jlong>(algo);
}

JNI_EXPORT jint JNICALL
Java_com_hyperai_hyperlpr3_core_HyperLPRCore_ReleaseRecognizerContext(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  if (!handle) return -1;
  auto* algo = reinterpret_cast<PlateAlgorithm*>(handle);
  algo->Release();
  delete algo;
  return 0;
}

JNI_EXPORT jobjectArray JNICALL
Java_com_hyperai_hyperlpr3_core_HyperLPRCore_PlateRecognitionFromBuffer(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jbyteArray buf, jint height,
    jint width, jint rotation, jint format) {
  if (!handle) {
    return env->NewObjectArray(0,
                               env->FindClass("com/hyperai/hyperlpr3/bean/Plate"),
                               nullptr);
  }
  auto* algo = reinterpret_cast<PlateAlgorithm*>(handle);
  cv::Mat img = WrapBuffer(env, buf, height, width, rotation, format);
  std::vector<PlateDet> plates;
  if (img.empty() || algo->Detect(img, plates) != HZ_SUCCESS) {
    plates.clear();
  }
  return BuildPlateArray(env, plates);
}

JNI_EXPORT jobjectArray JNICALL
Java_com_hyperai_hyperlpr3_core_HyperLPRCore_PlateRecognitionFromImage(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jintArray buf, jint height,
    jint width, jint rotation, jint format) {
  if (!handle) {
    return env->NewObjectArray(0,
                               env->FindClass("com/hyperai/hyperlpr3/bean/Plate"),
                               nullptr);
  }
  auto* algo = reinterpret_cast<PlateAlgorithm*>(handle);
  cv::Mat img = WrapIntBuffer(env, buf, height, width, rotation, format);
  std::vector<PlateDet> plates;
  if (img.empty() || algo->Detect(img, plates) != HZ_SUCCESS) {
    plates.clear();
  }
  return BuildPlateArray(env, plates);
}
