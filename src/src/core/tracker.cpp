#include "tracker.h"
#include "../utils/logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

static const char* MOD = "Tracker";

Tracker::Tracker() {}
Tracker::~Tracker() {}

void Tracker::init(int maxAge, int minHits, float iouThreshold,
                    int maxTracks, int historyLen) {
    maxAge_ = maxAge;
    minHits_ = minHits;
    iouThreshold_ = iouThreshold;
    maxTracks_ = maxTracks;
    historyLen_ = historyLen;

    LOG_I(MOD, "Tracker init: maxAge=%d minHits=%d iouThresh=%.2f maxTracks=%d",
          maxAge, minHits, iouThreshold, maxTracks);
}

float Tracker::computeIoU(const BBox& a, const BBox& b) const {
    float x1 = std::max(a.x1, b.x1);
    float y1 = std::max(a.y1, b.y1);
    float x2 = std::min(a.x2, b.x2);
    float y2 = std::min(a.y2, b.y2);

    float interW = std::max(0.0f, x2 - x1);
    float interH = std::max(0.0f, y2 - y1);
    float interArea = interW * interH;

    float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
    float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
    float unionArea = areaA + areaB - interArea;

    if (unionArea < 1e-6f) return 0;
    return interArea / unionArea;
}

void Tracker::predictTrack(TrackedObject& track) {
    // Simple linear prediction
    float cx = (track.bbox.x1 + track.bbox.x2) / 2.0f;
    float cy = (track.bbox.y1 + track.bbox.y2) / 2.0f;
    float w = track.bbox.x2 - track.bbox.x1;
    float h = track.bbox.y2 - track.bbox.y1;

    cx += track.velocityX;
    cy += track.velocityY;

    track.predictedX = cx;
    track.predictedY = cy;

    track.bbox.x1 = cx - w / 2;
    track.bbox.y1 = cy - h / 2;
    track.bbox.x2 = cx + w / 2;
    track.bbox.y2 = cy + h / 2;
}

void Tracker::associate(const std::vector<BBox>& detections,
                         std::vector<int>& assignments,
                         std::vector<int>& unmatched_dets,
                         std::vector<int>& unmatched_tracks) {
    assignments.clear();
    unmatched_dets.clear();
    unmatched_tracks.clear();

    int numDets = (int)detections.size();
    int numTracks = (int)tracks_.size();

    if (numTracks == 0) {
        for (int i = 0; i < numDets; ++i) unmatched_dets.push_back(i);
        return;
    }
    if (numDets == 0) {
        for (int i = 0; i < numTracks; ++i) unmatched_tracks.push_back(i);
        return;
    }

    // Compute IoU matrix
    std::vector<std::vector<float>> iouMatrix(numDets, std::vector<float>(numTracks, 0));
    for (int d = 0; d < numDets; ++d) {
        for (int t = 0; t < numTracks; ++t) {
            iouMatrix[d][t] = computeIoU(detections[d], tracks_[t].bbox);
        }
    }

    // Greedy assignment (Hungarian approximation)
    std::vector<bool> detUsed(numDets, false);
    std::vector<bool> trackUsed(numTracks, false);
    assignments.resize(numDets, -1);

    // Sort all pairs by IoU descending
    struct Pair { int d, t; float iou; };
    std::vector<Pair> pairs;
    for (int d = 0; d < numDets; ++d)
        for (int t = 0; t < numTracks; ++t)
            if (iouMatrix[d][t] > iouThreshold_)
                pairs.push_back({d, t, iouMatrix[d][t]});

    std::sort(pairs.begin(), pairs.end(),
              [](const Pair& a, const Pair& b) { return a.iou > b.iou; });

    for (auto& p : pairs) {
        if (detUsed[p.d] || trackUsed[p.t]) continue;
        assignments[p.d] = p.t;
        detUsed[p.d] = true;
        trackUsed[p.t] = true;
    }

    for (int d = 0; d < numDets; ++d)
        if (!detUsed[d]) unmatched_dets.push_back(d);
    for (int t = 0; t < numTracks; ++t)
        if (!trackUsed[t]) unmatched_tracks.push_back(t);
}

void Tracker::update(const std::vector<BBox>& detections) {
    std::lock_guard<std::mutex> lk(mu_);

    // Predict all tracks forward
    for (auto& tr : tracks_) {
        predictTrack(tr);
    }

    // Associate detections with tracks
    std::vector<int> assignments, unmatched_dets, unmatched_tracks;
    associate(detections, assignments, unmatched_dets, unmatched_tracks);

    // Update matched tracks
    for (int d = 0; d < (int)detections.size(); ++d) {
        if (assignments[d] < 0) continue;

        auto& tr = tracks_[assignments[d]];
        auto& det = detections[d];

        // Smooth velocity update
        float newCx = (det.x1 + det.x2) / 2.0f;
        float newCy = (det.y1 + det.y2) / 2.0f;
        float oldCx = (tr.bbox.x1 + tr.bbox.x2) / 2.0f;
        float oldCy = (tr.bbox.y1 + tr.bbox.y2) / 2.0f;

        float alpha = 0.3f; // smoothing factor
        tr.velocityX = alpha * (newCx - oldCx) + (1.0f - alpha) * tr.velocityX;
        tr.velocityY = alpha * (newCy - oldCy) + (1.0f - alpha) * tr.velocityY;

        tr.bbox = det;
        tr.hitStreak++;
        tr.missCount = 0;
        tr.age++;

        if (tr.hitStreak >= minHits_) tr.confirmed = true;

        // Update history
        tr.history.push_back({newCx, newCy});
        while ((int)tr.history.size() > historyLen_) tr.history.pop_front();
    }

    // Handle unmatched tracks (missed detections)
    for (int t : unmatched_tracks) {
        tracks_[t].missCount++;
        tracks_[t].hitStreak = 0;
        tracks_[t].age++;
    }

    // Create new tracks for unmatched detections
    for (int d : unmatched_dets) {
        if ((int)tracks_.size() < maxTracks_) {
            TrackedObject newTrack;
            newTrack.trackId = nextId_++;
            newTrack.bbox = detections[d];
            newTrack.velocityX = 0;
            newTrack.velocityY = 0;
            float cx = (detections[d].x1 + detections[d].x2) / 2.0f;
            float cy = (detections[d].y1 + detections[d].y2) / 2.0f;
            newTrack.predictedX = cx;
            newTrack.predictedY = cy;
            newTrack.age = 1;
            newTrack.hitStreak = 1;
            newTrack.missCount = 0;
            newTrack.confirmed = false;
            newTrack.depth = 0;
            newTrack.history.push_back({cx, cy});
            tracks_.push_back(newTrack);
        }
    }

    // Remove dead tracks
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
                       [this](const TrackedObject& t) {
                           return t.missCount > maxAge_;
                       }),
        tracks_.end());
}

std::vector<TrackedObject> Tracker::getTracks(bool confirmedOnly) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!confirmedOnly) return tracks_;

    std::vector<TrackedObject> result;
    for (auto& t : tracks_) {
        if (t.confirmed) result.push_back(t);
    }
    return result;
}

bool Tracker::getTrack(int trackId, TrackedObject& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& t : tracks_) {
        if (t.trackId == trackId) { out = t; return true; }
    }
    return false;
}

void Tracker::updateTrackDepth(int trackId, float depth) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& t : tracks_) {
        if (t.trackId == trackId) {
            t.depth = depth;
            return;
        }
    }
}

int Tracker::getActiveTrackCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    int cnt = 0;
    for (auto& t : tracks_) {
        if (t.confirmed && t.missCount == 0) cnt++;
    }
    return cnt;
}

void Tracker::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    tracks_.clear();
    nextId_ = 0;
    LOG_I(MOD, "Tracker reset");
}