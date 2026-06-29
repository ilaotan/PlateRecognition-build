package com.hyperai.hyperlpr3.bean;

import com.hyperai.hyperlpr3.settings.TypeDefine;

public class HyperLPRParameter {

    private String modelPath;

    // NCNN uses CPU threads (no Vulkan flag exposed). 4 strikes a good
    // balance on modern octa-core phones; tune via setThreads().
    private int threads = 4;

    // Retained for API compatibility; the NCNN backend ignores it.
    private boolean useHalf = false;

    // yolov7 plate detector thresholds. Lowered slightly to favour recall
    // on small / distant plates.
    private float boxConfThreshold = 0.3f;
    private float nmsThreshold = 0.3f;

    // NCNN plate recognition has no separate confidence head by default
    // (we always pick the argmax per time step). Threshold retained for
    // downstream filtering if needed.
    private float recConfidenceThreshold = 0.5f;

    // Backwards compatible; the NCNN backend only ships a single (640)
    // detector at the moment, so DETECT_LEVEL_LOW == DETECT_LEVEL_HIGH.
    private int detLevel = TypeDefine.DETECT_LEVEL_LOW;

    // Maximum plates returned per image.
    private int maxNum = 5;

    public HyperLPRParameter() {
    }

    public int getMaxNum() {
        return maxNum;
    }

    public HyperLPRParameter setMaxNum(int maxNum) {
        this.maxNum = maxNum;
        return this;
    }

    public String getModelPath() {
        return modelPath;
    }

    public HyperLPRParameter setModelPath(String modelPath) {
        this.modelPath = modelPath;
        return this;
    }

    public int getThreads() {
        return threads;
    }

    public HyperLPRParameter setThreads(int threads) {
        this.threads = threads;
        return this;
    }

    public boolean isUseHalf() {
        return useHalf;
    }

    public HyperLPRParameter setUseHalf(boolean useHalf) {
        this.useHalf = useHalf;
        return this;
    }

    public float getBoxConfThreshold() {
        return boxConfThreshold;
    }

    public HyperLPRParameter setBoxConfThreshold(float boxConfThreshold) {
        this.boxConfThreshold = boxConfThreshold;
        return this;
    }

    public float getNmsThreshold() {
        return nmsThreshold;
    }

    public HyperLPRParameter setNmsThreshold(float nmsThreshold) {
        this.nmsThreshold = nmsThreshold;
        return this;
    }

    public float getRecConfidenceThreshold() {
        return recConfidenceThreshold;
    }

    public HyperLPRParameter setRecConfidenceThreshold(float recConfidenceThreshold) {
        this.recConfidenceThreshold = recConfidenceThreshold;
        return this;
    }

    public int getDetLevel() {
        return detLevel;
    }

    public HyperLPRParameter setDetLevel(int detLevel) {
        this.detLevel = detLevel;
        return this;
    }


}
