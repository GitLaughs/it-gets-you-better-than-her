#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <string>

struct Point3D {
    float x, y, z;      // 3D position in camera frame (meters)
    uint8_t intensity;   // grayscale value
};

struct CameraIntrinsics {
    float fx = 600.0f;   // focal length x (pixels)
    float fy = 600.0f;   // focal length y (pixels)
    float cx = 640.0f;   // principal point x
    float cy = 360.0f;   // principal point y
};

class PointCloudGenerator {
public:
    PointCloudGenerator();

    void init(float fx, float fy, float cx, float cy,
              int downsampleStep, int maxPoints);

    // Generate point cloud from depth map and grayscale frame
    bool generate(const std::vector<float>& depthMap,
                  const uint8_t* frame,
                  int width, int height);

    // Get current point cloud
    std::vector<Point3D> getPoints() const;
    int getPointCount() const;

    // Get points within radius of a 3D position
    std::vector<Point3D> getPointsInRadius(float x, float y, float z,
                                            float radius) const;

    // Statistical analysis
    float getMeanDepth() const;
    float getMedianDepth() const;

    // Export point cloud to simple PLY format
    bool exportPLY(const std::string& filename) const;

    // Compute simple ground plane estimation (RANSAC-lite)
    bool estimateGroundPlane(float& a, float& b, float& c, float& d) const;

    void setCameraIntrinsics(const CameraIntrinsics& intrinsics);
    CameraIntrinsics getIntrinsics() const { return intrinsics_; }

    void reset();

private:
    // Voxel grid downsampling
    void voxelDownsample(std::vector<Point3D>& points, float voxelSize) const;

    CameraIntrinsics intrinsics_;
    int downsampleStep_ = 4;
    int maxPoints_ = 50000;

    std::vector<Point3D> points_;
    mutable std::mutex mu_;

    float lastMeanDepth_ = 0;
};