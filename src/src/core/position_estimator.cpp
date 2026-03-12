#include "position_estimator.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <numeric>

static const char* MOD = "PosEst";

PositionEstimator::PositionEstimator() {}
PositionEstimator::~PositionEstimator() {}

void PositionEstimator::init(float focalLengthPx, float baselineEstimate,
                              int maxFeatures, float matchThreshold,
                              int historySize) {
    focalLength_ = focalLengthPx;
    scaleEstimate_ = baselineEstimate;
    maxFeatures_ = maxFeatures;
    matchThreshold_ = matchThreshold;
    historySize_ = historySize;

    LOG_I(MOD, "PositionEstimator init: focal=%.1f scale=%.3f maxFeat=%d",
          focalLengthPx, baselineEstimate, maxFeatures);
}

void PositionEstimator::detectFeatures(const uint8_t* frame, int width,
                                        int height,
                                        std::vector<Feature>& features) {
    features.clear();
    features.reserve(maxFeatures_);

    // FAST-like corner detection on downsampled grid
    const int blockSize = 16;
    const int border = 4;
    const int threshold = 25;

    for (int by = border; by < height - border; by += blockSize) {
        for (int bx = border; bx < width - border; bx += blockSize) {

            // Find strongest corner in this block
            float bestResponse = 0;
            int bestX = bx, bestY = by;

            int yEnd = std::min(by + blockSize, height - border);
            int xEnd = std::min(bx + blockSize, width - border);

            for (int y = by; y < yEnd; y += 2) {
                for (int x = bx; x < xEnd; x += 2) {
                    int center = frame[y * width + x];

                    // Check 4 cardinal + 4 diagonal pixels at radius 3
                    int offsets[8][2] = {
                        {0,-3},{0,3},{-3,0},{3,0},
                        {-2,-2},{2,-2},{-2,2},{2,2}
                    };

                    int brighter = 0, darker = 0;
                    float responseSum = 0;

                    for (auto& off : offsets) {
                        int ny = y + off[1], nx = x + off[0];
                        if (ny < 0 || ny >= height || nx < 0 || nx >= width) continue;
                        int diff = (int)frame[ny * width + nx] - center;
                        if (diff > threshold) { brighter++; responseSum += diff; }
                        else if (diff < -threshold) { darker++; responseSum -= diff; }
                    }

                    // FAST criterion: at least 3 contiguous brighter or darker
                    if (brighter >= 3 || darker >= 3) {
                        float response = responseSum;
                        if (response > bestResponse) {
                            bestResponse = response;
                            bestX = x;
                            bestY = y;
                        }
                    }
                }
            }

            if (bestResponse > 0) {
                Feature f;
                f.x = (float)bestX;
                f.y = (float)bestY;
                f.response = bestResponse;
                f.id = (int)features.size();
                f.matched = false;
                features.push_back(f);
            }

            if ((int)features.size() >= maxFeatures_) break;
        }
        if ((int)features.size() >= maxFeatures_) break;
    }

    // Sort by response and keep top N
    if ((int)features.size() > maxFeatures_) {
        std::partial_sort(features.begin(), features.begin() + maxFeatures_,
                          features.end(),
                          [](const Feature& a, const Feature& b) {
                              return a.response > b.response;
                          });
        features.resize(maxFeatures_);
    }
}

void PositionEstimator::computeDescriptor(const uint8_t* frame, int width,
                                            int height, int px, int py,
                                            uint8_t* desc, int descSize) {
    // Simple BRIEF-like binary descriptor using pixel comparisons
    // Use fixed pattern pairs
    static const int pairs[][4] = {
        {-2,-2, 2, 2}, {-1,-3, 1, 3}, { 0,-2, 0, 2}, {-3, 0, 3, 0},
        {-1,-1, 1, 1}, { 2,-1,-2, 1}, {-2, 1, 2,-1}, { 1,-2,-1, 2},
        {-3,-1, 3, 1}, { 0,-3, 0, 3}, {-1, 0, 1, 0}, {-2,-3, 2, 3},
        { 3,-2,-3, 2}, { 1,-3,-1, 3}, {-3,-3, 3, 3}, { 2,-3,-2, 3},
    };

    memset(desc, 0, descSize);

    for (int i = 0; i < std::min(descSize * 8, 16); ++i) {
        int x1 = px + pairs[i][0], y1 = py + pairs[i][1];
        int x2 = px + pairs[i][2], y2 = py + pairs[i][3];

        x1 = std::max(0, std::min(width-1, x1));
        y1 = std::max(0, std::min(height-1, y1));
        x2 = std::max(0, std::min(width-1, x2));
        y2 = std::max(0, std::min(height-1, y2));

        if (frame[y1 * width + x1] > frame[y2 * width + x2]) {
            desc[i / 8] |= (1 << (i % 8));
        }
    }
}

bool PositionEstimator::trackPatch(const uint8_t* prevFrame,
                                    const uint8_t* currFrame,
                                    int width, int height,
                                    float prevX, float prevY,
                                    float& currX, float& currY) {
    // Simple template matching in a search window
    const int patchR = 4;    // 9x9 patch
    const int searchR = 16;  // search radius

    int px = (int)prevX, py = (int)prevY;

    if (px - patchR - searchR < 0 || px + patchR + searchR >= width ||
        py - patchR - searchR < 0 || py + patchR + searchR >= height) {
        return false;
    }

    float bestSAD = 1e9f;
    int bestDx = 0, bestDy = 0;

    for (int dy = -searchR; dy <= searchR; dy += 2) {
        for (int dx = -searchR; dx <= searchR; dx += 2) {
            float sad = 0;
            for (int ky = -patchR; ky <= patchR; ky += 2) {
                for (int kx = -patchR; kx <= patchR; kx += 2) {
                    int prevIdx = (py + ky) * width + (px + kx);
                    int currIdx = (py + ky + dy) * width + (px + kx + dx);
                    sad += std::abs((float)prevFrame[prevIdx] -
                                   (float)currFrame[currIdx]);
                }
            }
            if (sad < bestSAD) {
                bestSAD = sad;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }

    // Sub-pixel refinement at best position
    for (int dy = bestDy - 1; dy <= bestDy + 1; ++dy) {
        for (int dx = bestDx - 1; dx <= bestDx + 1; ++dx) {
            if (px + dx - patchR < 0 || px + dx + patchR >= width ||
                py + dy - patchR < 0 || py + dy + patchR >= height) continue;

            float sad = 0;
            for (int ky = -patchR; ky <= patchR; ++ky) {
                for (int kx = -patchR; kx <= patchR; ++kx) {
                    int prevIdx = (py + ky) * width + (px + kx);
                    int currIdx = (py + ky + dy) * width + (px + kx + dx);
                    sad += std::abs((float)prevFrame[prevIdx] -
                                   (float)currFrame[currIdx]);
                }
            }
            if (sad < bestSAD) {
                bestSAD = sad;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }

    // Reject poor matches
    float patchPixels = (2 * patchR + 1) * (2 * patchR + 1);
    float avgSAD = bestSAD / patchPixels;
    if (avgSAD > 30.0f) return false;

    currX = prevX + bestDx;
    currY = prevY + bestDy;
    return true;
}

int PositionEstimator::matchFeatures(const uint8_t* prevFrame,
                                      const uint8_t* currFrame,
                                      int width, int height,
                                      const std::vector<Feature>& prevFeats,
                                      std::vector<Feature>& currFeats,
                                      std::vector<std::pair<int,int>>& matches) {
    matches.clear();

    // KLT tracking: track each previous feature into current frame
    for (int i = 0; i < (int)prevFeats.size(); ++i) {
        float newX, newY;
        if (trackPatch(prevFrame, currFrame, width, height,
                       prevFeats[i].x, prevFeats[i].y, newX, newY)) {
            // Find or create matching feature in currFeats
            int bestJ = -1;
            float bestDist = 1e9f;

            for (int j = 0; j < (int)currFeats.size(); ++j) {
                if (currFeats[j].matched) continue;
                float dx = currFeats[j].x - newX;
                float dy = currFeats[j].y - newY;
                float dist = dx*dx + dy*dy;
                if (dist < bestDist && dist < 25.0f) { // within 5 pixels
                    bestDist = dist;
                    bestJ = j;
                }
            }

            if (bestJ >= 0) {
                matches.push_back({i, bestJ});
                currFeats[bestJ].matched = true;
            } else {
                // Add tracked point as new feature
                Feature f;
                f.x = newX;
                f.y = newY;
                f.response = prevFeats[i].response * 0.9f;
                f.id = (int)currFeats.size();
                f.matched = true;
                currFeats.push_back(f);
                matches.push_back({i, f.id});
            }
        }
    }

    return (int)matches.size();
}

bool PositionEstimator::estimateMotion(const std::vector<Feature>& prevFeats,
                                        const std::vector<Feature>& currFeats,
                                        const std::vector<std::pair<int,int>>& matches,
                                        float& dx, float& dy, float& dtheta) {
    if (matches.size() < 4) return false;

    // Compute average displacement (translation estimate)
    float sumDx = 0, sumDy = 0;
    int count = 0;

    for (auto& m : matches) {
        float fdx = currFeats[m.second].x - prevFeats[m.first].x;
        float fdy = currFeats[m.second].y - prevFeats[m.first].y;
        sumDx += fdx;
        sumDy += fdy;
        count++;
    }

    if (count == 0) return false;

    float avgDx = sumDx / count;
    float avgDy = sumDy / count;

    // Estimate rotation from flow field pattern
    // Rotation causes tangential flow around image center
    float cx = (prevFeats.empty() ? 640.0f : 0);
    float cy = (prevFeats.empty() ? 360.0f : 0);

    // Compute center of features
    for (auto& f : prevFeats) { cx += f.x; cy += f.y; }
    if (!prevFeats.empty()) { cx /= prevFeats.size(); cy /= prevFeats.size(); }

    float rotSum = 0;
    int rotCount = 0;

    for (auto& m : matches) {
        float rx = prevFeats[m.first].x - cx;
        float ry = prevFeats[m.first].y - cy;
        float r2 = rx*rx + ry*ry;
        if (r2 < 100) continue; // too close to center

        float fdx = currFeats[m.second].x - prevFeats[m.first].x - avgDx;
        float fdy = currFeats[m.second].y - prevFeats[m.first].y - avgDy;

        // Tangential component: (-ry, rx) direction
        float r = std::sqrt(r2);
        float tangential = (-ry * fdx + rx * fdy) / r2;
        rotSum += tangential;
        rotCount++;
    }

    dtheta = (rotCount > 3) ? rotSum / rotCount : 0;

    // Convert pixel displacement to metric using focal length and scale
    dx = -avgDx / focalLength_ * scaleEstimate_;
    dy = -avgDy / focalLength_ * scaleEstimate_;

    // Outlier rejection: remove matches with residual > 2x median
    std::vector<float> residuals;
    for (auto& m : matches) {
        float fdx = currFeats[m.second].x - prevFeats[m.first].x - avgDx;
        float fdy = currFeats[m.second].y - prevFeats[m.first].y - avgDy;
        residuals.push_back(fdx*fdx + fdy*fdy);
    }

    std::sort(residuals.begin(), residuals.end());
    float medianResidual = residuals[residuals.size()/2];

    int inliers = 0;
    for (float r : residuals) {
        if (r < medianResidual * 4.0f + 1.0f) inliers++;
    }

    confidence_ = (float)inliers / matches.size();
    return true;
}

bool PositionEstimator::update(const uint8_t* frame, int width, int height,
                                uint64_t timestampMs) {
    auto t0 = std::chrono::steady_clock::now();

    // Detect features in current frame
    std::vector<Feature> currFeatures;
    detectFeatures(frame, width, height, currFeatures);

    if (!hasPrev_ || prevFrame_.empty()) {
        // Store first frame
        prevFrame_.assign(frame, frame + width * height);
        prevFeatures_ = currFeatures;
        prevWidth_ = width;
        prevHeight_ = height;
        prevTimestamp_ = timestampMs;
        hasPrev_ = true;

        std::lock_guard<std::mutex> lk(mu_);
        currentPose_.timestampMs = timestampMs;
        trajectory_.push_back(currentPose_);

        auto t1 = std::chrono::steady_clock::now();
        lastProcMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();
        frameCount_++;
        return true;
    }

    // Match features
    std::vector<std::pair<int,int>> matches;
    matchedCount_ = matchFeatures(prevFrame_.data(), frame, width, height,
                                   prevFeatures_, currFeatures, matches);

    // Estimate motion
    float dx = 0, dy = 0, dtheta = 0;
    bool motionOk = estimateMotion(prevFeatures_, currFeatures, matches,
                                    dx, dy, dtheta);

    if (motionOk && confidence_ > 0.3f) {
        float dt = (timestampMs - prevTimestamp_) / 1000.0f;
        if (dt > 0 && dt < 1.0f) {
            std::lock_guard<std::mutex> lk(mu_);

            // Update pose in world frame
            float cosT = std::cos(currentPose_.theta);
            float sinT = std::sin(currentPose_.theta);

            currentPose_.x += cosT * dx - sinT * dy;
            currentPose_.y += sinT * dx + cosT * dy;
            currentPose_.theta += dtheta;

            // Normalize theta to [-pi, pi]
            while (currentPose_.theta > M_PI) currentPose_.theta -= 2 * M_PI;
            while (currentPose_.theta < -M_PI) currentPose_.theta += 2 * M_PI;

            currentPose_.timestampMs = timestampMs;

            // Update velocity
            velocityX_ = dx / dt;
            velocityY_ = dy / dt;
            omega_ = dtheta / dt;

            // Add to trajectory
            trajectory_.push_back(currentPose_);
            while ((int)trajectory_.size() > historySize_) {
                trajectory_.pop_front();
            }
        }
    }

    // Store current frame for next iteration
    prevFrame_.assign(frame, frame + width * height);
    prevFeatures_ = currFeatures;
    prevWidth_ = width;
    prevHeight_ = height;
    prevTimestamp_ = timestampMs;

    auto t1 = std::chrono::steady_clock::now();
    lastProcMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    frameCount_++;
    if (frameCount_ % 100 == 0) {
        LOG_D(MOD, "Pose: (%.3f, %.3f) θ=%.1f° features=%d matched=%d conf=%.2f",
              currentPose_.x, currentPose_.y,
              currentPose_.theta * 180.0f / M_PI,
              (int)currFeatures.size(), matchedCount_, confidence_);
    }

    return true;
}

Pose2D PositionEstimator::getCurrentPose() const {
    std::lock_guard<std::mutex> lk(mu_);
    return currentPose_;
}

void PositionEstimator::getVelocity(float& vx, float& vy, float& omega) const {
    std::lock_guard<std::mutex> lk(mu_);
    vx = velocityX_;
    vy = velocityY_;
    omega = omega_;
}

std::vector<Pose2D> PositionEstimator::getTrajectory() const {
    std::lock_guard<std::mutex> lk(mu_);
    return std::vector<Pose2D>(trajectory_.begin(), trajectory_.end());
}

void PositionEstimator::resetPose() {
    std::lock_guard<std::mutex> lk(mu_);
    currentPose_ = Pose2D();
    velocityX_ = velocityY_ = omega_ = 0;
    trajectory_.clear();
    hasPrev_ = false;
    prevFrame_.clear();
    prevFeatures_.clear();
    confidence_ = 0;
    LOG_I(MOD, "Pose reset to origin");
}

int PositionEstimator::getFeatureCount() const {
    return (int)prevFeatures_.size();
}

int PositionEstimator::getMatchedCount() const {
    return matchedCount_;
}

void PositionEstimator::updateScale(float knownDepth, float pixelDisplacement) {
    if (pixelDisplacement > 0.1f) {
        scaleEstimate_ = knownDepth / (pixelDisplacement / focalLength_);
        scaleEstimate_ = std::max(0.01f, std::min(100.0f, scaleEstimate_));
        LOG_D(MOD, "Scale updated: %.4f (depth=%.2f disp=%.1f)",
              scaleEstimate_, knownDepth, pixelDisplacement);
    }
}