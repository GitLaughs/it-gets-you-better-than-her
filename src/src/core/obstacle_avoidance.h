#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <cmath>

enum class ZoneType {
    SAFE = 0,
    WARNING,
    DANGER,
    CRITICAL
};

struct Sector {
    int index;
    float angleDeg;        // center angle in degrees
    float minDistance;      // closest obstacle
    float avgDistance;      // average distance
    ZoneType zone;
    int pixelCount;
};

struct NavigationCommand {
    float linearSpeed;     // m/s, positive = forward
    float angularSpeed;    // rad/s, positive = left
    bool emergencyStop;
    ZoneType worstZone;
    float clearanceAngle;  // best direction to go (degrees)
    float clearanceDist;   // distance in best direction
    std::string description;
};

class ObstacleAvoidance {
public:
    ObstacleAvoidance();

    void init(float criticalDist, float dangerDist, float warningDist,
              float safeDist, float maxLinearSpeed, float maxAngularSpeed,
              int sectorCount, float fovH);

    // Build obstacle map from depth data
    void updateDepthMap(const std::vector<float>& depthMap,
                        int width, int height);

    // Get navigation command based on current obstacle map
    NavigationCommand computeNavigation();

    // Get all sectors
    std::vector<Sector> getSectors() const;

    // Get closest obstacle info
    float getClosestDistance() const;
    float getClosestAngle() const;

    // Check if path in given direction is clear
    bool isPathClear(float angleDeg, float requiredClearance = 1.0f) const;

    // Check if emergency stop needed
    bool isEmergencyStop() const;

    void reset();

private:
    ZoneType classifyDistance(float dist) const;
    float findBestDirection() const;

    float criticalDist_ = 0.3f;
    float dangerDist_ = 0.8f;
    float warningDist_ = 1.5f;
    float safeDist_ = 3.0f;
    float maxLinearSpeed_ = 0.5f;
    float maxAngularSpeed_ = 1.0f;
    int sectorCount_ = 72;
    float fovH_ = 49.7f;

    std::vector<Sector> sectors_;
    mutable std::mutex mu_;

    float closestDist_ = 999.0f;
    float closestAngle_ = 0.0f;
    bool emergency_ = false;
};