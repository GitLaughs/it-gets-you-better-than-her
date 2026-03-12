#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <deque>
#include <string>

struct BBox {
    float x1, y1, x2, y2;
    float confidence;
    int classId;
    std::string label;
};

struct TrackedObject {
    int trackId;
    BBox bbox;
    float velocityX, velocityY;     // pixels per frame
    float predictedX, predictedY;   // predicted center next frame
    int age;                        // frames alive
    int hitStreak;                  // consecutive frames detected
    int missCount;                  // consecutive frames missed
    bool confirmed;                 // track confirmed after N hits
    float depth;                    // depth from depth estimator

    std::deque<std::pair<float,float>> history;  // center position history
};

class Tracker {
public:
    Tracker();
    ~Tracker();

    void init(int maxAge, int minHits, float iouThreshold,
              int maxTracks, int historyLen);

    // Update tracker with new detections
    void update(const std::vector<BBox>& detections);

    // Get all active tracks (confirmed only by default)
    std::vector<TrackedObject> getTracks(bool confirmedOnly = true) const;

    // Get specific track
    bool getTrack(int trackId, TrackedObject& out) const;

    // Update depth for nearest track to (cx, cy)
    void updateTrackDepth(int trackId, float depth);

    int getActiveTrackCount() const;
    int getTotalTracksCreated() const { return nextId_; }

    void reset();

private:
    // IoU computation
    float computeIoU(const BBox& a, const BBox& b) const;

    // Hungarian-like greedy association
    void associate(const std::vector<BBox>& detections,
                   std::vector<int>& assignments,
                   std::vector<int>& unmatched_dets,
                   std::vector<int>& unmatched_tracks);

    // Kalman-like prediction
    void predictTrack(TrackedObject& track);

    int maxAge_ = 15;
    int minHits_ = 3;
    float iouThreshold_ = 0.3f;
    int maxTracks_ = 50;
    int historyLen_ = 30;

    std::vector<TrackedObject> tracks_;
    mutable std::mutex mu_;
    int nextId_ = 0;
};