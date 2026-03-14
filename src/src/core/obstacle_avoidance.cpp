#include "obstacle_avoidance.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char* MOD = "NavOA";

ObstacleAvoidance::ObstacleAvoidance() {}

void ObstacleAvoidance::init(float criticalDist, float dangerDist,
                              float warningDist, float safeDist,
                              float maxLinearSpeed, float maxAngularSpeed,
                              int sectorCount, float fovH) {
    criticalDist_ = criticalDist;
    dangerDist_ = dangerDist;
    warningDist_ = warningDist;
    safeDist_ = safeDist;
    maxLinearSpeed_ = maxLinearSpeed;
    maxAngularSpeed_ = maxAngularSpeed;
    sectorCount_ = sectorCount;
    fovH_ = fovH;

    sectors_.resize(sectorCount);
    for (int i = 0; i < sectorCount; ++i) {
        sectors_[i].index = i;
        sectors_[i].angleDeg = -fovH / 2.0f + (fovH * i) / sectorCount;
        sectors_[i].minDistance = safeDist * 2.0f;
        sectors_[i].avgDistance = safeDist * 2.0f;
        sectors_[i].zone = ZoneType::SAFE;
        sectors_[i].pixelCount = 0;
    }

    LOG_I(MOD, "Obstacle avoidance init: sectors=%d fovH=%.1f crit=%.1f danger=%.1f warn=%.1f safe=%.1f",
          sectorCount, fovH, criticalDist, dangerDist, warningDist, safeDist);
}

ZoneType ObstacleAvoidance::classifyDistance(float dist) const {
    if (dist <= criticalDist_) return ZoneType::CRITICAL;
    if (dist <= dangerDist_)   return ZoneType::DANGER;
    if (dist <= warningDist_)  return ZoneType::WARNING;
    return ZoneType::SAFE;
}

void ObstacleAvoidance::updateDepthMap(const std::vector<float>& depthMap,
                                        int width, int height) {
    std::lock_guard<std::mutex> lk(mu_);

    // Reset sectors
    for (auto& s : sectors_) {
        s.minDistance = safeDist_ * 2.0f;
        s.avgDistance = 0;
        s.pixelCount = 0;
    }

    // Use central vertical strip for obstacle detection
    // Only use middle 60% of image height (avoid sky/ground noise)
    int yStart = height * 2 / 10;
    int yEnd = height * 8 / 10;

    float anglePerPixel = fovH_ / width;
    float halfFov = fovH_ / 2.0f;

    closestDist_ = 999.0f;
    closestAngle_ = 0.0f;
    emergency_ = false;

    // Map each column to a sector
    for (int x = 0; x < width; x += 2) { // stride 2 for speed
        float angleDeg = -halfFov + (float)x / width * fovH_;

        // Find sector index
        int sectorIdx = (int)((angleDeg + halfFov) / fovH_ * sectorCount_);
        sectorIdx = std::max(0, std::min(sectorCount_ - 1, sectorIdx));

        // Find minimum depth in this column
        float colMin = safeDist_ * 2.0f;
        float colSum = 0;
        int colCount = 0;

        for (int y = yStart; y < yEnd; y += 4) { // stride 4
            int idx = y * width + x;
            if (idx < (int)depthMap.size()) {
                float d = depthMap[idx];
                if (d > 0.01f) { // valid depth
                    if (d < colMin) colMin = d;
                    colSum += d;
                    colCount++;
                }
            }
        }

        if (colCount > 0) {
            if (colMin < sectors_[sectorIdx].minDistance) {
                sectors_[sectorIdx].minDistance = colMin;
            }
            sectors_[sectorIdx].avgDistance += colSum;
            sectors_[sectorIdx].pixelCount += colCount;

            if (colMin < closestDist_) {
                closestDist_ = colMin;
                closestAngle_ = angleDeg;
            }
        }
    }

    // Finalize sectors
    for (auto& s : sectors_) {
        if (s.pixelCount > 0) {
            s.avgDistance /= s.pixelCount;
        } else {
            s.avgDistance = safeDist_ * 2.0f;
        }
        s.zone = classifyDistance(s.minDistance);

        if (s.zone == ZoneType::CRITICAL) {
            emergency_ = true;
        }
    }
}

NavigationCommand ObstacleAvoidance::computeNavigation() {
    std::lock_guard<std::mutex> lk(mu_);

    NavigationCommand nav;
    nav.emergencyStop = false;
    nav.linearSpeed = maxLinearSpeed_;
    nav.angularSpeed = 0;
    nav.worstZone = ZoneType::SAFE;
    nav.description = "clear";

    // Find worst zone across all sectors
    for (auto& s : sectors_) {
        if ((int)s.zone > (int)nav.worstZone) {
            nav.worstZone = s.zone;
        }
    }

    // Emergency stop
    if (emergency_ || nav.worstZone == ZoneType::CRITICAL) {
        nav.emergencyStop = true;
        nav.linearSpeed = 0;
        nav.angularSpeed = 0;
        nav.description = "EMERGENCY_STOP";
        LOG_W(MOD, "Emergency stop! Closest: %.2fm at %.1f°",
              closestDist_, closestAngle_);
        return nav;
    }

    // Find best direction
    float bestAngle = findBestDirection();
    nav.clearanceAngle = bestAngle;

    // Find clearance distance at best direction
    int bestSector = (int)((bestAngle + fovH_/2.0f) / fovH_ * sectorCount_);
    bestSector = std::max(0, std::min(sectorCount_ - 1, bestSector));
    nav.clearanceDist = sectors_[bestSector].minDistance;

    // Compute navigation speeds based on zone
    switch (nav.worstZone) {
        case ZoneType::SAFE:
            nav.linearSpeed = maxLinearSpeed_;
            nav.description = "safe_forward";
            break;

        case ZoneType::WARNING: {
            // Slow down and steer toward best direction
            float distFactor = closestDist_ / warningDist_;
            nav.linearSpeed = maxLinearSpeed_ * std::max(0.3f, distFactor);

            // Steer away from closest obstacle
            float steerAngle = -closestAngle_ * (M_PI / 180.0f);
            nav.angularSpeed = steerAngle * 0.5f;
            nav.angularSpeed = std::max(-maxAngularSpeed_,
                std::min(maxAngularSpeed_, nav.angularSpeed));
            nav.description = "warning_steer";
            break;
        }

        case ZoneType::DANGER: {
            // Very slow, strong steering
            float distFactor = closestDist_ / dangerDist_;
            nav.linearSpeed = maxLinearSpeed_ * std::max(0.1f, distFactor * 0.3f);

            // Strong steer toward best direction
            float steerAngle = bestAngle * (M_PI / 180.0f);
            nav.angularSpeed = steerAngle * 1.5f;
            nav.angularSpeed = std::max(-maxAngularSpeed_,
                std::min(maxAngularSpeed_, nav.angularSpeed));
            nav.description = "danger_avoid";

            LOG_I(MOD, "Danger zone: closest=%.2fm steer=%.1f°",
                  closestDist_, bestAngle);
            break;
        }

        default:
            break;
    }

    return nav;
}

float ObstacleAvoidance::findBestDirection() const {
    // Find sector with maximum minimum distance (safest direction)
    float bestDist = 0;
    float bestAngle = 0;

    // Use a sliding window of up to 3 sectors for smoother results.
    // Handle boundary sectors (i==0 and i==sectorCount_-1) with a
    // reduced window so they are not skipped entirely.
    for (int i = 0; i < sectorCount_; ++i) {
        float avgMin;
        if (sectorCount_ < 3) {
            avgMin = sectors_[i].minDistance;
        } else if (i == 0) {
            avgMin = (sectors_[0].minDistance + sectors_[1].minDistance) / 2.0f;
        } else if (i == sectorCount_ - 1) {
            avgMin = (sectors_[i-1].minDistance + sectors_[i].minDistance) / 2.0f;
        } else {
            avgMin = (sectors_[i-1].minDistance +
                      sectors_[i].minDistance +
                      sectors_[i+1].minDistance) / 3.0f;
        }
        if (avgMin > bestDist) {
            bestDist = avgMin;
            bestAngle = sectors_[i].angleDeg;
        }
    }

    return bestAngle;
}

std::vector<Sector> ObstacleAvoidance::getSectors() const {
    std::lock_guard<std::mutex> lk(mu_);
    return sectors_;
}

float ObstacleAvoidance::getClosestDistance() const {
    std::lock_guard<std::mutex> lk(mu_);
    return closestDist_;
}

float ObstacleAvoidance::getClosestAngle() const {
    std::lock_guard<std::mutex> lk(mu_);
    return closestAngle_;
}

bool ObstacleAvoidance::isPathClear(float angleDeg, float requiredClearance) const {
    std::lock_guard<std::mutex> lk(mu_);

    float halfFov = fovH_ / 2.0f;
    if (angleDeg < -halfFov || angleDeg > halfFov) return true; // outside FOV

    int sectorIdx = (int)((angleDeg + halfFov) / fovH_ * sectorCount_);
    sectorIdx = std::max(0, std::min(sectorCount_ - 1, sectorIdx));

    // Check 3-sector window
    for (int d = -1; d <= 1; ++d) {
        int idx = sectorIdx + d;
        if (idx >= 0 && idx < sectorCount_) {
            if (sectors_[idx].minDistance < requiredClearance) return false;
        }
    }
    return true;
}

bool ObstacleAvoidance::isEmergencyStop() const {
    std::lock_guard<std::mutex> lk(mu_);
    return emergency_;
}

void ObstacleAvoidance::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& s : sectors_) {
        s.minDistance = safeDist_ * 2.0f;
        s.avgDistance = safeDist_ * 2.0f;
        s.zone = ZoneType::SAFE;
        s.pixelCount = 0;
    }
    closestDist_ = 999.0f;
    closestAngle_ = 0.0f;
    emergency_ = false;
    LOG_I(MOD, "Obstacle avoidance reset");
}