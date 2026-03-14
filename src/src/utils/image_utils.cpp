#include "image_utils.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>

#ifdef USE_NEON
#include <arm_neon.h>
#endif

namespace ImageUtils {

// ============================================================
// Resize
// ============================================================

void resizeBilinear(const uint8_t* src, int srcW, int srcH,
                    uint8_t* dst, int dstW, int dstH) {
    float xRatio = (dstW > 1) ? (float)(srcW - 1) / (dstW - 1) : 0.0f;
    float yRatio = (dstH > 1) ? (float)(srcH - 1) / (dstH - 1) : 0.0f;

    for (int dy = 0; dy < dstH; ++dy) {
        float fy = dy * yRatio;
        int sy = (int)fy;
        float yFrac = fy - sy;
        int sy1 = std::min(sy + 1, srcH - 1);

        for (int dx = 0; dx < dstW; ++dx) {
            float fx = dx * xRatio;
            int sx = (int)fx;
            float xFrac = fx - sx;
            int sx1 = std::min(sx + 1, srcW - 1);

            float p00 = src[sy  * srcW + sx];
            float p10 = src[sy  * srcW + sx1];
            float p01 = src[sy1 * srcW + sx];
            float p11 = src[sy1 * srcW + sx1];

            float top = p00 + (p10 - p00) * xFrac;
            float bot = p01 + (p11 - p01) * xFrac;
            float val = top + (bot - top) * yFrac;

            dst[dy * dstW + dx] = (uint8_t)std::max(0.0f, std::min(255.0f, val + 0.5f));
        }
    }
}

void resizeBilinearF(const float* src, int srcW, int srcH,
                     float* dst, int dstW, int dstH) {
    float xRatio = (float)(srcW - 1) / std::max(1, dstW - 1);
    float yRatio = (float)(srcH - 1) / std::max(1, dstH - 1);

    for (int dy = 0; dy < dstH; ++dy) {
        float fy = dy * yRatio;
        int sy = (int)fy;
        float yFrac = fy - sy;
        int sy1 = std::min(sy + 1, srcH - 1);

        for (int dx = 0; dx < dstW; ++dx) {
            float fx = dx * xRatio;
            int sx = (int)fx;
            float xFrac = fx - sx;
            int sx1 = std::min(sx + 1, srcW - 1);

            float p00 = src[sy  * srcW + sx];
            float p10 = src[sy  * srcW + sx1];
            float p01 = src[sy1 * srcW + sx];
            float p11 = src[sy1 * srcW + sx1];

            float top = p00 + (p10 - p00) * xFrac;
            float bot = p01 + (p11 - p01) * xFrac;
            dst[dy * dstW + dx] = top + (bot - top) * yFrac;
        }
    }
}

// ============================================================
// Color conversion
// ============================================================

void yuyvToGray(const uint8_t* yuyv, uint8_t* gray, int width, int height) {
    int totalPixels = width * height;
    for (int i = 0; i < totalPixels; ++i) {
        gray[i] = yuyv[i * 2]; // Y component is at even indices
    }
}

void nv12ToGray(const uint8_t* nv12, uint8_t* gray, int width, int height) {
    memcpy(gray, nv12, width * height); // Y plane is first
}

void nv21ToGray(const uint8_t* nv21, uint8_t* gray, int width, int height) {
    memcpy(gray, nv21, width * height);
}

void grayToRgb(const uint8_t* gray, uint8_t* rgb, int width, int height) {
    int n = width * height;
    for (int i = 0; i < n; ++i) {
        rgb[i * 3 + 0] = gray[i];
        rgb[i * 3 + 1] = gray[i];
        rgb[i * 3 + 2] = gray[i];
    }
}

void grayToFloat(const uint8_t* gray, float* out, int width, int height) {
    int n = width * height;
#ifdef USE_NEON
    grayToFloatNeon(gray, out, width, height);
    return;
#endif
    for (int i = 0; i < n; ++i) {
        out[i] = gray[i] / 255.0f;
    }
}

void grayToFloatNorm(const uint8_t* gray, float* out, int width, int height,
                     float mean, float std_val) {
    int n = width * height;
    float invStd = 1.0f / std_val;
    for (int i = 0; i < n; ++i) {
        out[i] = (gray[i] / 255.0f - mean) * invStd;
    }
}

void depthToGray(const float* depth, uint8_t* gray, int width, int height,
                 float minDepth, float maxDepth) {
    float range = maxDepth - minDepth;
    if (range < 1e-6f) range = 1.0f;
    float invRange = 255.0f / range;

    int n = width * height;
    for (int i = 0; i < n; ++i) {
        float d = depth[i];
        d = std::max(minDepth, std::min(maxDepth, d));
        // Closer = brighter for visibility
        gray[i] = (uint8_t)(255.0f - (d - minDepth) * invRange);
    }
}

// ============================================================
// Filtering
// ============================================================

void gaussianBlur3x3(const uint8_t* src, uint8_t* dst, int width, int height) {
    // Kernel: [1 2 1; 2 4 2; 1 2 1] / 16
#ifdef USE_NEON
    gaussianBlur3x3Neon(src, dst, width, height);
    return;
#endif
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int sum =
                src[(y-1)*width + (x-1)] * 1 + src[(y-1)*width + x] * 2 + src[(y-1)*width + (x+1)] * 1 +
                src[y*width + (x-1)]     * 2 + src[y*width + x]     * 4 + src[y*width + (x+1)]     * 2 +
                src[(y+1)*width + (x-1)] * 1 + src[(y+1)*width + x] * 2 + src[(y+1)*width + (x+1)] * 1;
            dst[y * width + x] = (uint8_t)(sum / 16);
        }
    }
    // Handle borders: copy edge pixels
    memcpy(dst, src, width);
    memcpy(dst + (height-1)*width, src + (height-1)*width, width);
    for (int y = 0; y < height; ++y) {
        dst[y*width] = src[y*width];
        dst[y*width + width-1] = src[y*width + width-1];
    }
}

void gaussianBlur5x5(const uint8_t* src, uint8_t* dst, int width, int height) {
    // Kernel: [1 4 6 4 1; 4 16 24 16 4; 6 24 36 24 6; 4 16 24 16 4; 1 4 6 4 1] / 256
    static const int k[5][5] = {
        {1,  4,  6,  4, 1},
        {4, 16, 24, 16, 4},
        {6, 24, 36, 24, 6},
        {4, 16, 24, 16, 4},
        {1,  4,  6,  4, 1}
    };

    for (int y = 2; y < height - 2; ++y) {
        for (int x = 2; x < width - 2; ++x) {
            int sum = 0;
            for (int ky = -2; ky <= 2; ++ky) {
                for (int kx = -2; kx <= 2; ++kx) {
                    sum += src[(y+ky)*width + (x+kx)] * k[ky+2][kx+2];
                }
            }
            dst[y*width + x] = (uint8_t)(sum / 256);
        }
    }

    // Copy border
    for (int y = 0; y < 2; ++y) memcpy(dst + y*width, src + y*width, width);
    for (int y = height-2; y < height; ++y) memcpy(dst + y*width, src + y*width, width);
    for (int y = 0; y < height; ++y) {
        dst[y*width] = src[y*width];
        dst[y*width+1] = src[y*width+1];
        dst[y*width+width-1] = src[y*width+width-1];
        dst[y*width+width-2] = src[y*width+width-2];
    }
}

void medianFilter3x3(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            uint8_t window[9];
            int idx = 0;
            for (int ky = -1; ky <= 1; ++ky)
                for (int kx = -1; kx <= 1; ++kx)
                    window[idx++] = src[(y+ky)*width + (x+kx)];

            // Partial sort to find median (5th element)
            std::nth_element(window, window + 4, window + 9);
            dst[y*width + x] = window[4];
        }
    }
    // Borders
    memcpy(dst, src, width);
    memcpy(dst + (height-1)*width, src + (height-1)*width, width);
    for (int y = 0; y < height; ++y) {
        dst[y*width] = src[y*width];
        dst[y*width+width-1] = src[y*width+width-1];
    }
}

void bilateralFilter(const uint8_t* src, uint8_t* dst, int width, int height,
                     int radius, float sigmaSpace, float sigmaColor) {
    float invSigmaSpace2 = -0.5f / (sigmaSpace * sigmaSpace);
    float invSigmaColor2 = -0.5f / (sigmaColor * sigmaColor);

    for (int y = radius; y < height - radius; ++y) {
        for (int x = radius; x < width - radius; ++x) {
            float center = src[y * width + x];
            float sumW = 0, sumVal = 0;

            for (int ky = -radius; ky <= radius; ++ky) {
                for (int kx = -radius; kx <= radius; ++kx) {
                    float neighbor = src[(y+ky)*width + (x+kx)];
                    float spatialDist2 = (float)(kx*kx + ky*ky);
                    float colorDist2 = (center - neighbor) * (center - neighbor);

                    float w = std::exp(spatialDist2 * invSigmaSpace2 +
                                       colorDist2 * invSigmaColor2);
                    sumW += w;
                    sumVal += w * neighbor;
                }
            }

            dst[y*width + x] = (uint8_t)std::max(0.0f, std::min(255.0f, sumVal / sumW));
        }
    }

    // Copy border rows
    for (int y = 0; y < radius; ++y) memcpy(dst + y*width, src + y*width, width);
    for (int y = height-radius; y < height; ++y) memcpy(dst + y*width, src + y*width, width);

    // Copy border columns (left and right)
    for (int y = radius; y < height - radius; ++y) {
        for (int x = 0; x < radius; ++x) {
            dst[y*width + x] = src[y*width + x];
            dst[y*width + width-1-x] = src[y*width + width-1-x];
        }
    }
}

void sobelX(const uint8_t* src, int16_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int gx =
                -src[(y-1)*width+(x-1)] + src[(y-1)*width+(x+1)] +
                -2*src[y*width+(x-1)]   + 2*src[y*width+(x+1)] +
                -src[(y+1)*width+(x-1)] + src[(y+1)*width+(x+1)];
            dst[y*width + x] = (int16_t)gx;
        }
    }
}

void sobelY(const uint8_t* src, int16_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int gy =
                -src[(y-1)*width+(x-1)] - 2*src[(y-1)*width+x] - src[(y-1)*width+(x+1)] +
                 src[(y+1)*width+(x-1)] + 2*src[(y+1)*width+x] + src[(y+1)*width+(x+1)];
            dst[y*width + x] = (int16_t)gy;
        }
    }
}

void sobelMagnitude(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int gx =
                -src[(y-1)*width+(x-1)] + src[(y-1)*width+(x+1)] +
                -2*src[y*width+(x-1)]   + 2*src[y*width+(x+1)] +
                -src[(y+1)*width+(x-1)] + src[(y+1)*width+(x+1)];
            int gy =
                -src[(y-1)*width+(x-1)] - 2*src[(y-1)*width+x] - src[(y-1)*width+(x+1)] +
                 src[(y+1)*width+(x-1)] + 2*src[(y+1)*width+x] + src[(y+1)*width+(x+1)];

            float mag = std::sqrt((float)(gx*gx + gy*gy));
            dst[y*width + x] = (uint8_t)std::min(255.0f, mag);
        }
    }
}

// ============================================================
// Histogram
// ============================================================

void computeHistogram(const uint8_t* img, int width, int height, int* histogram) {
    memset(histogram, 0, 256 * sizeof(int));
    int n = width * height;
    for (int i = 0; i < n; ++i) {
        histogram[img[i]]++;
    }
}

void equalizeHistogram(const uint8_t* src, uint8_t* dst, int width, int height) {
    int hist[256];
    computeHistogram(src, width, height, hist);

    int n = width * height;
    int cdf[256];
    cdf[0] = hist[0];
    for (int i = 1; i < 256; ++i) {
        cdf[i] = cdf[i-1] + hist[i];
    }

    // Find cdf_min
    int cdfMin = 0;
    for (int i = 0; i < 256; ++i) {
        if (cdf[i] > 0) { cdfMin = cdf[i]; break; }
    }

    uint8_t lut[256];
    float scale = 255.0f / std::max(1, n - cdfMin);
    for (int i = 0; i < 256; ++i) {
        lut[i] = (uint8_t)std::max(0.0f,
                    std::min(255.0f, (float)(cdf[i] - cdfMin) * scale));
    }

    for (int i = 0; i < n; ++i) {
        dst[i] = lut[src[i]];
    }
}

void clahe(const uint8_t* src, uint8_t* dst, int width, int height,
           int tileW, int tileH, float clipLimit) {
    int numTilesX = (width + tileW - 1) / tileW;
    int numTilesY = (height + tileH - 1) / tileH;

    // For simplicity, process each tile independently
    std::vector<uint8_t> luts(numTilesX * numTilesY * 256);

    for (int ty = 0; ty < numTilesY; ++ty) {
        for (int tx = 0; tx < numTilesX; ++tx) {
            int x0 = tx * tileW;
            int y0 = ty * tileH;
            int x1 = std::min(x0 + tileW, width);
            int y1 = std::min(y0 + tileH, height);
            int tilePixels = (x1 - x0) * (y1 - y0);

            // Compute tile histogram
            int hist[256] = {};
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    hist[src[y*width + x]]++;

            // Clip histogram
            int clipVal = (int)(clipLimit * tilePixels / 256);
            int excess = 0;
            for (int i = 0; i < 256; ++i) {
                if (hist[i] > clipVal) {
                    excess += hist[i] - clipVal;
                    hist[i] = clipVal;
                }
            }
            int redistrib = excess / 256;
            for (int i = 0; i < 256; ++i) {
                hist[i] += redistrib;
            }

            // CDF
            int cdf[256];
            cdf[0] = hist[0];
            for (int i = 1; i < 256; ++i) cdf[i] = cdf[i-1] + hist[i];

            int cdfMin = 0;
            for (int i = 0; i < 256; ++i) {
                if (cdf[i] > 0) { cdfMin = cdf[i]; break; }
            }

            uint8_t* lut = &luts[(ty * numTilesX + tx) * 256];
            float scale = 255.0f / std::max(1, tilePixels - cdfMin);
            for (int i = 0; i < 256; ++i) {
                lut[i] = (uint8_t)std::max(0.0f,
                    std::min(255.0f, (float)(cdf[i] - cdfMin) * scale));
            }
        }
    }

    // Apply with bilinear interpolation between tiles
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int tx = x / tileW;
            int ty_idx = y / tileH;
            tx = std::min(tx, numTilesX - 1);
            ty_idx = std::min(ty_idx, numTilesY - 1);

            uint8_t* lut = &luts[(ty_idx * numTilesX + tx) * 256];
            dst[y*width + x] = lut[src[y*width + x]];
        }
    }
}

void computeStats(const uint8_t* img, int width, int height,
                  float& mean, float& stddev) {
    int n = width * height;
    if (n == 0) { mean = 0; stddev = 0; return; }

    long sum = 0;
    for (int i = 0; i < n; ++i) sum += img[i];
    mean = (float)sum / n;

    float sumSq = 0;
    for (int i = 0; i < n; ++i) {
        float diff = img[i] - mean;
        sumSq += diff * diff;
    }
    stddev = std::sqrt(sumSq / n);
}

// ============================================================
// Geometric
// ============================================================

void cropRegion(const uint8_t* src, int srcW, int srcH,
                uint8_t* dst, int x, int y, int cropW, int cropH) {
    for (int row = 0; row < cropH; ++row) {
        int srcY = y + row;
        if (srcY < 0 || srcY >= srcH) {
            memset(dst + row * cropW, 0, cropW);
            continue;
        }
        int srcX = std::max(0, x);
        int copyW = std::min(cropW, srcW - srcX);
        if (copyW > 0) {
            memcpy(dst + row * cropW, src + srcY * srcW + srcX, copyW);
        }
    }
}

void flipHorizontal(uint8_t* img, int width, int height) {
    for (int y = 0; y < height; ++y) {
        uint8_t* row = img + y * width;
        for (int x = 0; x < width / 2; ++x) {
            std::swap(row[x], row[width - 1 - x]);
        }
    }
}

void flipVertical(uint8_t* img, int width, int height) {
    std::vector<uint8_t> tmpRow(width);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = img + y * width;
        uint8_t* bot = img + (height - 1 - y) * width;
        memcpy(tmpRow.data(), top, width);
        memcpy(top, bot, width);
        memcpy(bot, tmpRow.data(), width);
    }
}

void rotate90CW(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            dst[x * height + (height - 1 - y)] = src[y * width + x];
        }
    }
}

// ============================================================
// Thresholding
// ============================================================

void threshold(const uint8_t* src, uint8_t* dst, int width, int height,
               uint8_t thresh, uint8_t maxVal) {
    int n = width * height;
    for (int i = 0; i < n; ++i) {
        dst[i] = (src[i] > thresh) ? maxVal : 0;
    }
}

uint8_t otsuThreshold(const uint8_t* img, int width, int height) {
    int hist[256];
    computeHistogram(img, width, height, hist);

    int total = width * height;
    float sum = 0;
    for (int i = 0; i < 256; ++i) sum += i * hist[i];

    float sumB = 0;
    int wB = 0;
    float maxVariance = 0;
    uint8_t bestThresh = 0;

    for (int t = 0; t < 256; ++t) {
        wB += hist[t];
        if (wB == 0) continue;
        int wF = total - wB;
        if (wF == 0) break;

        sumB += t * hist[t];
        float mB = sumB / wB;
        float mF = (sum - sumB) / wF;
        float variance = (float)wB * wF * (mB - mF) * (mB - mF);

        if (variance > maxVariance) {
            maxVariance = variance;
            bestThresh = (uint8_t)t;
        }
    }

    return bestThresh;
}

void adaptiveThreshold(const uint8_t* src, uint8_t* dst,
                       int width, int height, int blockSize, int C) {
    // Build integral image with padding (single pass, no redundant computation)
    std::vector<uint32_t> intImg((width + 1) * (height + 1), 0);
    for (int y = 1; y <= height; ++y) {
        for (int x = 1; x <= width; ++x) {
            intImg[y*(width+1)+x] = src[(y-1)*width+(x-1)]
                + intImg[(y-1)*(width+1)+x]
                + intImg[y*(width+1)+(x-1)]
                - intImg[(y-1)*(width+1)+(x-1)];
        }
    }

    int half = blockSize / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int x1 = std::max(0, x - half);
            int y1 = std::max(0, y - half);
            int x2 = std::min(width - 1, x + half);
            int y2 = std::min(height - 1, y + half);

            int count = (x2 - x1 + 1) * (y2 - y1 + 1);
            uint32_t sum = intImg[(y2+1)*(width+1)+(x2+1)]
                         - intImg[y1*(width+1)+(x2+1)]
                         - intImg[(y2+1)*(width+1)+x1]
                         + intImg[y1*(width+1)+x1];

            float mean = (float)sum / count;
            dst[y*width + x] = (src[y*width + x] > mean - C) ? 255 : 0;
        }
    }
}

// ============================================================
// Integral Image
// ============================================================

void computeIntegralImage(const uint8_t* src, uint32_t* integral,
                          int width, int height) {
    // First row
    uint32_t rowSum = 0;
    for (int x = 0; x < width; ++x) {
        rowSum += src[x];
        integral[x] = rowSum;
    }
    // Remaining rows
    for (int y = 1; y < height; ++y) {
        rowSum = 0;
        for (int x = 0; x < width; ++x) {
            rowSum += src[y * width + x];
            integral[y * width + x] = integral[(y-1) * width + x] + rowSum;
        }
    }
}

uint32_t integralRectSum(const uint32_t* integral, int imgW,
                         int x1, int y1, int x2, int y2) {
    uint32_t sum = integral[y2 * imgW + x2];
    if (x1 > 0) sum -= integral[y2 * imgW + (x1 - 1)];
    if (y1 > 0) sum -= integral[(y1 - 1) * imgW + x2];
    if (x1 > 0 && y1 > 0) sum += integral[(y1 - 1) * imgW + (x1 - 1)];
    return sum;
}

// ============================================================
// Morphology
// ============================================================

void erode3x3(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            uint8_t minVal = 255;
            for (int ky = -1; ky <= 1; ++ky)
                for (int kx = -1; kx <= 1; ++kx)
                    minVal = std::min(minVal, src[(y+ky)*width + (x+kx)]);
            dst[y*width + x] = minVal;
        }
    }
}

void dilate3x3(const uint8_t* src, uint8_t* dst, int width, int height) {
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            uint8_t maxVal = 0;
            for (int ky = -1; ky <= 1; ++ky)
                for (int kx = -1; kx <= 1; ++kx)
                    maxVal = std::max(maxVal, src[(y+ky)*width + (x+kx)]);
            dst[y*width + x] = maxVal;
        }
    }
}

// ============================================================
// NEON optimized
// ============================================================

#ifdef USE_NEON

void resizeBilinearNeon(const uint8_t* src, int srcW, int srcH,
                        uint8_t* dst, int dstW, int dstH) {
    // For non-aligned or small sizes, fall back
    if (dstW < 16) {
        resizeBilinear(src, srcW, srcH, dst, dstW, dstH);
        return;
    }

    float xRatio = (float)(srcW - 1) / (dstW - 1);
    float yRatio = (float)(srcH - 1) / (dstH - 1);

    for (int dy = 0; dy < dstH; ++dy) {
        float fy = dy * yRatio;
        int sy = (int)fy;
        float yFrac = fy - sy;
        int sy1 = std::min(sy + 1, srcH - 1);

        const uint8_t* row0 = src + sy * srcW;
        const uint8_t* row1 = src + sy1 * srcW;

        int dx = 0;
        // Process 8 pixels at a time
        for (; dx + 7 < dstW; dx += 8) {
            float fxArr[8];
            for (int k = 0; k < 8; ++k) fxArr[k] = (dx + k) * xRatio;

            uint8_t result[8];
            for (int k = 0; k < 8; ++k) {
                int sx = (int)fxArr[k];
                float xFrac = fxArr[k] - sx;
                int sx1 = std::min(sx + 1, srcW - 1);

                float top = row0[sx] + (row0[sx1] - row0[sx]) * xFrac;
                float bot = row1[sx] + (row1[sx1] - row1[sx]) * xFrac;
                float val = top + (bot - top) * yFrac;
                result[k] = (uint8_t)std::max(0.0f, std::min(255.0f, val + 0.5f));
            }

            uint8x8_t vr = vld1_u8(result);
            vst1_u8(dst + dy * dstW + dx, vr);
        }
        // Remainder
        for (; dx < dstW; ++dx) {
            float fx = dx * xRatio;
            int sx = (int)fx;
            float xFrac = fx - sx;
            int sx1 = std::min(sx + 1, srcW - 1);
            float top = row0[sx] + (row0[sx1] - row0[sx]) * xFrac;
            float bot = row1[sx] + (row1[sx1] - row1[sx]) * xFrac;
            dst[dy*dstW + dx] = (uint8_t)(top + (bot - top) * yFrac + 0.5f);
        }
    }
}

void gaussianBlur3x3Neon(const uint8_t* src, uint8_t* dst,
                          int width, int height) {
    // Process row by row using NEON, 16 pixels at a time
    for (int y = 1; y < height - 1; ++y) {
        const uint8_t* row0 = src + (y - 1) * width;
        const uint8_t* row1 = src + y * width;
        const uint8_t* row2 = src + (y + 1) * width;

        int x = 1;
        for (; x + 15 < width - 1; x += 16) {
            // Load rows with shifts for convolution
            uint8x16_t r0l = vld1q_u8(row0 + x - 1);
            uint8x16_t r0c = vld1q_u8(row0 + x);
            uint8x16_t r0r = vld1q_u8(row0 + x + 1);
            uint8x16_t r1l = vld1q_u8(row1 + x - 1);
            uint8x16_t r1c = vld1q_u8(row1 + x);
            uint8x16_t r1r = vld1q_u8(row1 + x + 1);
            uint8x16_t r2l = vld1q_u8(row2 + x - 1);
            uint8x16_t r2c = vld1q_u8(row2 + x);
            uint8x16_t r2r = vld1q_u8(row2 + x + 1);

            // Widen to 16-bit for arithmetic
            uint16x8_t sum_lo = vaddl_u8(vget_low_u8(r0l), vget_low_u8(r0r));
            sum_lo = vaddw_u8(sum_lo, vget_low_u8(r2l));
            sum_lo = vaddw_u8(sum_lo, vget_low_u8(r2r));
            uint16x8_t mid_lo = vaddl_u8(vget_low_u8(r0c), vget_low_u8(r2c));
            mid_lo = vaddw_u8(mid_lo, vget_low_u8(r1l));
            mid_lo = vaddw_u8(mid_lo, vget_low_u8(r1r));
            mid_lo = vshlq_n_u16(mid_lo, 1);  // *2
            uint16x8_t center_lo = vshll_n_u8(vget_low_u8(r1c), 2); // *4
            sum_lo = vaddq_u16(sum_lo, mid_lo);
            sum_lo = vaddq_u16(sum_lo, center_lo);
            uint8x8_t res_lo = vshrn_n_u16(sum_lo, 4); // /16

            uint16x8_t sum_hi = vaddl_u8(vget_high_u8(r0l), vget_high_u8(r0r));
            sum_hi = vaddw_u8(sum_hi, vget_high_u8(r2l));
            sum_hi = vaddw_u8(sum_hi, vget_high_u8(r2r));
            uint16x8_t mid_hi = vaddl_u8(vget_high_u8(r0c), vget_high_u8(r2c));
            mid_hi = vaddw_u8(mid_hi, vget_high_u8(r1l));
            mid_hi = vaddw_u8(mid_hi, vget_high_u8(r1r));
            mid_hi = vshlq_n_u16(mid_hi, 1);
            uint16x8_t center_hi = vshll_n_u8(vget_high_u8(r1c), 2);
            sum_hi = vaddq_u16(sum_hi, mid_hi);
            sum_hi = vaddq_u16(sum_hi, center_hi);
            uint8x8_t res_hi = vshrn_n_u16(sum_hi, 4);

            uint8x16_t result = vcombine_u8(res_lo, res_hi);
            vst1q_u8(dst + y * width + x, result);
        }

        // Scalar remainder
        for (; x < width - 1; ++x) {
            int sum =
                row0[x-1]*1 + row0[x]*2 + row0[x+1]*1 +
                row1[x-1]*2 + row1[x]*4 + row1[x+1]*2 +
                row2[x-1]*1 + row2[x]*2 + row2[x+1]*1;
            dst[y*width + x] = (uint8_t)(sum / 16);
        }
    }

    // Copy borders
    memcpy(dst, src, width);
    memcpy(dst + (height-1)*width, src + (height-1)*width, width);
    for (int y = 0; y < height; ++y) {
        dst[y*width] = src[y*width];
        dst[y*width+width-1] = src[y*width+width-1];
    }
}

void grayToFloatNeon(const uint8_t* gray, float* out, int width, int height) {
    int n = width * height;
    float32x4_t scale = vdupq_n_f32(1.0f / 255.0f);
    int i = 0;

    for (; i + 15 < n; i += 16) {
        uint8x16_t v8 = vld1q_u8(gray + i);

        // Low 8
        uint16x8_t v16_lo = vmovl_u8(vget_low_u8(v8));
        uint32x4_t v32_0 = vmovl_u16(vget_low_u16(v16_lo));
        uint32x4_t v32_1 = vmovl_u16(vget_high_u16(v16_lo));
        float32x4_t f0 = vmulq_f32(vcvtq_f32_u32(v32_0), scale);
        float32x4_t f1 = vmulq_f32(vcvtq_f32_u32(v32_1), scale);
        vst1q_f32(out + i, f0);
        vst1q_f32(out + i + 4, f1);

        // High 8
        uint16x8_t v16_hi = vmovl_u8(vget_high_u8(v8));
        uint32x4_t v32_2 = vmovl_u16(vget_low_u16(v16_hi));
        uint32x4_t v32_3 = vmovl_u16(vget_high_u16(v16_hi));
        float32x4_t f2 = vmulq_f32(vcvtq_f32_u32(v32_2), scale);
        float32x4_t f3 = vmulq_f32(vcvtq_f32_u32(v32_3), scale);
        vst1q_f32(out + i + 8, f2);
        vst1q_f32(out + i + 12, f3);
    }

    for (; i < n; ++i) {
        out[i] = gray[i] / 255.0f;
    }
}

#endif // USE_NEON

} // namespace ImageUtils