#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <deque>
#include <cmath>

struct Pose2D {
    float x = 0;       // meters
    float y = 0;       // meters
    float theta = 0;   // radians
    uint64_t timestampMs = 0;
};

struct Feature {
    float x, y;         // pixel coordinates
    float response;      // corner strength
    int id;              // tracking id
    bool matched;
};

class PositionEstimator {
public:
    PositionEstimator();
    ~PositionEstimator();

    void init(float focalLengthPx, float baselineEstimate,
              int maxFeatures, float matchThreshold, int historySize);

    // Update with new grayscale frame
    bool update(const uint8_t* frame, int width, int height, uint64_t timestampMs);

    // Get current estimated pose
    Pose2D getCurrentPose() const;

    // Get velocity estimate (m/s and rad/s)
    void getVelocity(float& vx, float& vy, float& omega) const;

    // Get trajectory history
    std::vector<Pose2D> getTrajectory() const;

    // Reset pose to origin
    void resetPose();

    // Get feature count
    int getFeatureCount() const;
    int getMatchedCount() const;

    // Scale correction from depth: if we know depth at some features
    void updateScale(float knownDepth, float pixelDisplacement);

    float getConfidence() const { return confidence_; }
    float getLastProcessTimeMs() const { return lastProcMs_; }

private:
    // Feature detection (FAST-like corners)
    void detectFeatures(const uint8_t* frame, int width, int height,
                        std::vector<Feature>& features);

    // Feature matching between frames
    int matchFeatures(const uint8_t* prevFrame, const uint8_t* currFrame,
                      int width, int height,
                      const std::vector<Feature>& prevFeats,
                      std::vector<Feature>& currFeats,
                      std::vector<std::pair<int,int>>& matches);

    // Estimate motion from matched features
    bool estimateMotion(const std::vector<Feature>& prevFeats,
                        const std::vector<Feature>& currFeats,
                        const std::vector<std::pair<int,int>>& matches,
                        float& dx, float& dy, float& dtheta);

    // Compute patch descriptor for matching
    void computeDescriptor(const uint8_t* frame, int width, int height,
                           int px, int py, uint8_t* desc, int descSize);

    // Simple KLT-style patch tracking
    bool trackPatch(const uint8_t* prevFrame, const uint8_t* currFrame,
                    int width, int height,
                    float prevX, float prevY,
                    float& currX, float& currY);

    float focalLength_ = 600.0f;
    float scaleEstimate_ = 1.0f;
    int maxFeatures_ = 200;
    float matchThreshold_ = 0.7f;
    int historySize_ = 500;

    Pose2D currentPose_;
    float velocityX_ = 0, velocityY_ = 0, omega_ = 0;
    float confidence_ = 0;
    float lastProcMs_ = 0;

    std::vector<uint8_t> prevFrame_;
    std::vector<Feature> prevFeatures_;
    int prevWidth_ = 0, prevHeight_ = 0;
    uint64_t prevTimestamp_ = 0;
    bool hasPrev_ = false;

    std::deque<Pose2D> trajectory_;
    mutable std::mutex mu_;

    int frameCount_ = 0;
    int matchedCount_ = 0;
};