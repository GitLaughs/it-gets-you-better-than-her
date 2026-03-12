#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace ImageUtils {

// ============================================================
// Resize (bilinear interpolation)
// ============================================================
void resizeBilinear(const uint8_t* src, int srcW, int srcH,
                    uint8_t* dst, int dstW, int dstH);

// Resize float image
void resizeBilinearF(const float* src, int srcW, int srcH,
                     float* dst, int dstW, int dstH);

// ============================================================
// Color conversion
// ============================================================

// YUYV (V4L2 common) -> Grayscale
void yuyvToGray(const uint8_t* yuyv, uint8_t* gray, int width, int height);

// NV12 -> Grayscale (just copy Y plane)
void nv12ToGray(const uint8_t* nv12, uint8_t* gray, int width, int height);

// NV21 -> Grayscale
void nv21ToGray(const uint8_t* nv21, uint8_t* gray, int width, int height);

// Grayscale -> 3-channel (for NN input that expects RGB)
void grayToRgb(const uint8_t* gray, uint8_t* rgb, int width, int height);

// Grayscale -> float normalized [0, 1]
void grayToFloat(const uint8_t* gray, float* out, int width, int height);

// Grayscale -> float normalized with mean/std
void grayToFloatNorm(const uint8_t* gray, float* out, int width, int height,
                     float mean, float std);

// Float depth map -> uint8 visualization
void depthToGray(const float* depth, uint8_t* gray, int width, int height,
                 float minDepth, float maxDepth);

// ============================================================
// Filtering
// ============================================================

// 3x3 Gaussian blur
void gaussianBlur3x3(const uint8_t* src, uint8_t* dst, int width, int height);

// 5x5 Gaussian blur
void gaussianBlur5x5(const uint8_t* src, uint8_t* dst, int width, int height);

// 3x3 Median filter
void medianFilter3x3(const uint8_t* src, uint8_t* dst, int width, int height);

// Bilateral filter (simplified)
void bilateralFilter(const uint8_t* src, uint8_t* dst, int width, int height,
                     int radius, float sigmaSpace, float sigmaColor);

// Sobel edge detection
void sobelX(const uint8_t* src, int16_t* dst, int width, int height);
void sobelY(const uint8_t* src, int16_t* dst, int width, int height);
void sobelMagnitude(const uint8_t* src, uint8_t* dst, int width, int height);

// ============================================================
// Histogram
// ============================================================

// Compute histogram (256 bins)
void computeHistogram(const uint8_t* img, int width, int height,
                      int* histogram);

// Histogram equalization
void equalizeHistogram(const uint8_t* src, uint8_t* dst,
                       int width, int height);

// CLAHE (Contrast Limited Adaptive Histogram Equalization)
void clahe(const uint8_t* src, uint8_t* dst, int width, int height,
           int tileW, int tileH, float clipLimit);

// Compute mean and standard deviation
void computeStats(const uint8_t* img, int width, int height,
                  float& mean, float& stddev);

// ============================================================
// Geometric transforms
// ============================================================

// Crop region
void cropRegion(const uint8_t* src, int srcW, int srcH,
                uint8_t* dst, int x, int y, int cropW, int cropH);

// Flip horizontal
void flipHorizontal(uint8_t* img, int width, int height);

// Flip vertical
void flipVertical(uint8_t* img, int width, int height);

// Rotate 90 degrees clockwise
void rotate90CW(const uint8_t* src, uint8_t* dst, int width, int height);

// ============================================================
// Thresholding
// ============================================================

// Binary threshold
void threshold(const uint8_t* src, uint8_t* dst, int width, int height,
               uint8_t thresh, uint8_t maxVal = 255);

// Otsu's automatic threshold
uint8_t otsuThreshold(const uint8_t* img, int width, int height);

// Adaptive threshold (block mean)
void adaptiveThreshold(const uint8_t* src, uint8_t* dst,
                       int width, int height, int blockSize, int C);

// ============================================================
// Integral image
// ============================================================
void computeIntegralImage(const uint8_t* src, uint32_t* integral,
                          int width, int height);

// Query sum in rectangle using integral image
uint32_t integralRectSum(const uint32_t* integral, int imgW,
                         int x1, int y1, int x2, int y2);

// ============================================================
// Morphology
// ============================================================

// Erode (3x3 square kernel)
void erode3x3(const uint8_t* src, uint8_t* dst, int width, int height);

// Dilate (3x3 square kernel)
void dilate3x3(const uint8_t* src, uint8_t* dst, int width, int height);

// ============================================================
// NEON-optimized variants (ARM only)
// ============================================================
#ifdef USE_NEON
void resizeBilinearNeon(const uint8_t* src, int srcW, int srcH,
                        uint8_t* dst, int dstW, int dstH);
void gaussianBlur3x3Neon(const uint8_t* src, uint8_t* dst,
                          int width, int height);
void grayToFloatNeon(const uint8_t* gray, float* out, int width, int height);
#endif

} // namespace ImageUtils