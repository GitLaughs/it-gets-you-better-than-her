#include "point_cloud.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <fstream>
#include <unordered_map>
#include <cstring>

static const char* MOD = "PointCloud";

PointCloudGenerator::PointCloudGenerator() {}

void PointCloudGenerator::init(float fx, float fy, float cx, float cy,
                                int downsampleStep, int maxPoints) {
    intrinsics_.fx = fx;
    intrinsics_.fy = fy;
    intrinsics_.cx = cx;
    intrinsics_.cy = cy;
    downsampleStep_ = std::max(1, downsampleStep);
    maxPoints_ = maxPoints;

    LOG_I(MOD, "PointCloud init: fx=%.1f fy=%.1f cx=%.1f cy=%.1f step=%d max=%d",
          fx, fy, cx, cy, downsampleStep, maxPoints);
}

void PointCloudGenerator::setCameraIntrinsics(const CameraIntrinsics& intr) {
    intrinsics_ = intr;
}

bool PointCloudGenerator::generate(const std::vector<float>& depthMap,
                                    const uint8_t* frame,
                                    int width, int height) {
    if (depthMap.empty() || !frame) return false;
    if ((int)depthMap.size() < width * height) return false;

    std::vector<Point3D> newPoints;
    newPoints.reserve(maxPoints_);

    float invFx = 1.0f / intrinsics_.fx;
    float invFy = 1.0f / intrinsics_.fy;

    float depthSum = 0;
    int depthCount = 0;

    for (int y = 0; y < height; y += downsampleStep_) {
        for (int x = 0; x < width; x += downsampleStep_) {
            int idx = y * width + x;
            float z = depthMap[idx];

            // Filter invalid depths
            if (z <= 0.01f || z > 50.0f) continue;
            if (std::isnan(z) || std::isinf(z)) continue;

            // Pinhole camera model: back-project to 3D
            Point3D pt;
            pt.z = z;
            pt.x = (x - intrinsics_.cx) * z * invFx;
            pt.y = (y - intrinsics_.cy) * z * invFy;
            pt.intensity = frame[idx];

            newPoints.push_back(pt);
            depthSum += z;
            depthCount++;

            if ((int)newPoints.size() >= maxPoints_) break;
        }
        if ((int)newPoints.size() >= maxPoints_) break;
    }

    // Optional voxel downsampling if still too many points
    if ((int)newPoints.size() > maxPoints_) {
        voxelDownsample(newPoints, 0.05f); // 5cm voxels
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        points_ = std::move(newPoints);
        lastMeanDepth_ = (depthCount > 0) ? depthSum / depthCount : 0;
    }

    return true;
}

void PointCloudGenerator::voxelDownsample(std::vector<Point3D>& points,
                                           float voxelSize) const {
    if (points.empty() || voxelSize <= 0) return;

    float invVoxel = 1.0f / voxelSize;

    // Use a simple hash map for voxel grid
    struct VoxelKey {
        int x, y, z;
        bool operator==(const VoxelKey& o) const {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct VoxelHash {
        size_t operator()(const VoxelKey& k) const {
            size_t h = 17;
            h = h * 31 + std::hash<int>()(k.x);
            h = h * 31 + std::hash<int>()(k.y);
            h = h * 31 + std::hash<int>()(k.z);
            return h;
        }
    };

    struct VoxelData {
        float sumX = 0, sumY = 0, sumZ = 0;
        int sumIntensity = 0;
        int count = 0;
    };

    std::unordered_map<VoxelKey, VoxelData, VoxelHash> voxels;

    for (auto& pt : points) {
        VoxelKey key;
        key.x = (int)std::floor(pt.x * invVoxel);
        key.y = (int)std::floor(pt.y * invVoxel);
        key.z = (int)std::floor(pt.z * invVoxel);

        auto& v = voxels[key];
        v.sumX += pt.x;
        v.sumY += pt.y;
        v.sumZ += pt.z;
        v.sumIntensity += pt.intensity;
        v.count++;
    }

    points.clear();
    points.reserve(voxels.size());

    for (auto& [key, v] : voxels) {
        Point3D pt;
        float inv = 1.0f / v.count;
        pt.x = v.sumX * inv;
        pt.y = v.sumY * inv;
        pt.z = v.sumZ * inv;
        pt.intensity = (uint8_t)(v.sumIntensity / v.count);
        points.push_back(pt);

        if ((int)points.size() >= maxPoints_) break;
    }
}

std::vector<Point3D> PointCloudGenerator::getPoints() const {
    std::lock_guard<std::mutex> lk(mu_);
    return points_;
}

int PointCloudGenerator::getPointCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return (int)points_.size();
}

std::vector<Point3D> PointCloudGenerator::getPointsInRadius(float x, float y,
                                                              float z, float radius) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Point3D> result;
    float r2 = radius * radius;

    for (auto& pt : points_) {
        float dx = pt.x - x;
        float dy = pt.y - y;
        float dz = pt.z - z;
        if (dx*dx + dy*dy + dz*dz <= r2) {
            result.push_back(pt);
        }
    }
    return result;
}

float PointCloudGenerator::getMeanDepth() const {
    std::lock_guard<std::mutex> lk(mu_);
    return lastMeanDepth_;
}

float PointCloudGenerator::getMedianDepth() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (points_.empty()) return 0;

    std::vector<float> depths;
    depths.reserve(points_.size());
    for (auto& pt : points_) depths.push_back(pt.z);

    size_t mid = depths.size() / 2;
    std::nth_element(depths.begin(), depths.begin() + mid, depths.end());
    return depths[mid];
}

bool PointCloudGenerator::exportPLY(const std::string& filename) const {
    std::lock_guard<std::mutex> lk(mu_);

    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        LOG_E(MOD, "Cannot open PLY file: %s", filename.c_str());
        return false;
    }

    ofs << "ply\n";
    ofs << "format ascii 1.0\n";
    ofs << "element vertex " << points_.size() << "\n";
    ofs << "property float x\n";
    ofs << "property float y\n";
    ofs << "property float z\n";
    ofs << "property uchar intensity\n";
    ofs << "end_header\n";

    for (auto& pt : points_) {
        ofs << pt.x << " " << pt.y << " " << pt.z << " "
            << (int)pt.intensity << "\n";
    }

    ofs.close();
    LOG_I(MOD, "Exported %d points to %s", (int)points_.size(), filename.c_str());
    return true;
}

bool PointCloudGenerator::estimateGroundPlane(float& a, float& b, float& c,
                                               float& d) const {
    std::lock_guard<std::mutex> lk(mu_);

    if (points_.size() < 10) {
        a = 0; b = 1; c = 0; d = 0; // default: Y=0
        return false;
    }

    // Simple RANSAC for plane fitting
    // Plane: ax + by + cz + d = 0

    int bestInliers = 0;
    float bestA = 0, bestB = 1, bestC = 0, bestD = 0;
    float inlierThresh = 0.05f; // 5cm

    int maxIter = 50;
    int n = (int)points_.size();

    // Pseudo-random using frame count as seed
    auto pseudoRand = [&](int iter, int sample) -> int {
        return (iter * 7919 + sample * 104729) % n;
    };

    for (int iter = 0; iter < maxIter; ++iter) {
        // Pick 3 random points
        int i0 = pseudoRand(iter, 0);
        int i1 = pseudoRand(iter, 1);
        int i2 = pseudoRand(iter, 2);

        if (i0 == i1 || i1 == i2 || i0 == i2) continue;

        auto& p0 = points_[i0];
        auto& p1 = points_[i1];
        auto& p2 = points_[i2];

        // Two edge vectors
        float v1x = p1.x - p0.x, v1y = p1.y - p0.y, v1z = p1.z - p0.z;
        float v2x = p2.x - p0.x, v2y = p2.y - p0.y, v2z = p2.z - p0.z;

        // Normal = cross product
        float nx = v1y * v2z - v1z * v2y;
        float ny = v1z * v2x - v1x * v2z;
        float nz = v1x * v2y - v1y * v2x;

        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len < 1e-6f) continue;

        nx /= len; ny /= len; nz /= len;
        float dd = -(nx * p0.x + ny * p0.y + nz * p0.z);

        // Count inliers
        int inliers = 0;
        for (int i = 0; i < n; i += 3) { // sample for speed
            float dist = std::abs(nx * points_[i].x + ny * points_[i].y +
                                  nz * points_[i].z + dd);
            if (dist < inlierThresh) inliers++;
        }

        if (inliers > bestInliers) {
            bestInliers = inliers;
            bestA = nx; bestB = ny; bestC = nz; bestD = dd;
        }
    }

    a = bestA; b = bestB; c = bestC; d = bestD;

    LOG_D(MOD, "Ground plane: %.3fx + %.3fy + %.3fz + %.3f = 0 (inliers=%d)",
          a, b, c, d, bestInliers);
    return bestInliers > 20;
}

void PointCloudGenerator::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    points_.clear();
    lastMeanDepth_ = 0;
    LOG_I(MOD, "Point cloud reset");
}