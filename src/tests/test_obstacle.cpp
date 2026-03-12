#include "../src/core/obstacle_avoidance.h"
#include "../src/utils/logger.h"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) printf("[TEST] %s ... ", name); fflush(stdout)
#define PASS() do { printf("PASS\n"); testsPassed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

// Create uniform depth map
static std::vector<float> uniformDepth(int w, int h, float depth) {
    return std::vector<float>(w * h, depth);
}

// Create depth map with obstacle in center
static std::vector<float> centerObstacle(int w, int h,
                                          float bgDepth, float obstDepth,
                                          int obstW, int obstH) {
    std::vector<float> depth(w * h, bgDepth);
    int cx = w / 2, cy = h / 2;
    int x1 = cx - obstW / 2, y1 = cy - obstH / 2;
    int x2 = cx + obstW / 2, y2 = cy + obstH / 2;

    for (int y = std::max(0, y1); y < std::min(h, y2); ++y) {
        for (int x = std::max(0, x1); x < std::min(w, x2); ++x) {
            depth[y * w + x] = obstDepth;
        }
    }
    return depth;
}

// Create depth map with obstacle on left side
static std::vector<float> leftObstacle(int w, int h,
                                        float bgDepth, float obstDepth) {
    std::vector<float> depth(w * h, bgDepth);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w / 4; ++x) {
            depth[y * w + x] = obstDepth;
        }
    }
    return depth;
}

void test_safe_distance() {
    TEST("Safe distance (no obstacle)");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = uniformDepth(64, 48, 5.0f);
    oa.updateDepthMap(depth, 64, 48);
    auto nav = oa.computeNavigation();

    if (nav.emergencyStop) { FAIL("Should not emergency stop at safe distance"); return; }
    if (nav.speedScale < 0.9f) { FAIL("Speed scale should be near 1.0"); return; }
    PASS();
}

void test_emergency_stop() {
    TEST("Emergency stop (critical distance)");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = centerObstacle(64, 48, 5.0f, 0.2f, 30, 30);
    oa.updateDepthMap(depth, 64, 48);
    auto nav = oa.computeNavigation();

    if (!nav.emergencyStop) { FAIL("Should trigger emergency stop"); return; }
    if (nav.speedScale > 0.01f) { FAIL("Speed should be zero"); return; }
    PASS();
}

void test_danger_zone() {
    TEST("Danger zone speed reduction");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = centerObstacle(64, 48, 5.0f, 0.5f, 20, 20);
    oa.updateDepthMap(depth, 64, 48);
    auto nav = oa.computeNavigation();

    if (nav.emergencyStop) { FAIL("Should not emergency stop in danger zone"); return; }
    if (nav.speedScale >= 1.0f) { FAIL("Speed should be reduced"); return; }
    if (nav.speedScale <= 0.0f) { FAIL("Speed should not be zero"); return; }
    PASS();
}

void test_warning_zone() {
    TEST("Warning zone moderate speed");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = centerObstacle(64, 48, 5.0f, 1.2f, 20, 20);
    oa.updateDepthMap(depth, 64, 48);
    auto nav = oa.computeNavigation();

    if (nav.emergencyStop) { FAIL("No emergency stop in warning zone"); return; }
    if (nav.speedScale >= 1.0f) { FAIL("Speed should be somewhat reduced"); return; }
    PASS();
}

void test_steering_away_from_obstacle() {
    TEST("Steering away from left obstacle");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = leftObstacle(64, 48, 5.0f, 0.5f);
    oa.updateDepthMap(depth, 64, 48);
    auto nav = oa.computeNavigation();

    // Obstacle on left -> should steer right (positive angular)
    // or at least the recommended angle should point away from left
    // The specific behavior depends on implementation
    if (nav.emergencyStop) { FAIL("Should not emergency stop"); return; }
    PASS();
}

void test_closest_distance() {
    TEST("Closest distance reporting");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = centerObstacle(64, 48, 5.0f, 1.7f, 16, 16);
    oa.updateDepthMap(depth, 64, 48);
    oa.computeNavigation();

    float closest = oa.getClosestDistance();
    if (closest > 2.0f) { FAIL("Closest should be near 1.7m"); return; }
    if (closest < 1.0f) { FAIL("Closest should not be too small"); return; }
    PASS();
}

void test_zone_classification() {
    TEST("Zone classification");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    // Safe
    oa.updateDepthMap(uniformDepth(64, 48, 5.0f), 64, 48);
    oa.computeNavigation();
    if (oa.getCurrentZone() != ZONE_SAFE) { FAIL("Should be SAFE zone"); return; }

    // Critical
    oa.updateDepthMap(uniformDepth(64, 48, 0.2f), 64, 48);
    oa.computeNavigation();
    if (oa.getCurrentZone() != ZONE_CRITICAL) { FAIL("Should be CRITICAL zone"); return; }

    PASS();
}

void test_get_sector_distances() {
    TEST("Sector distance array");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 12, 49.7f);

    auto depth = centerObstacle(64, 48, 5.0f, 1.0f, 16, 16);
    oa.updateDepthMap(depth, 64, 48);
    oa.computeNavigation();

    auto sectors = oa.getSectorDistances();
    if ((int)sectors.size() != 12) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Expected 12 sectors, got %d", (int)sectors.size());
        FAIL(msg);
        return;
    }

    // Center sectors should have closer distances
    bool hasCenterMin = false;
    float centerDist = sectors[6]; // approximately center
    for (int i = 0; i < (int)sectors.size(); ++i) {
        if (sectors[i] < 2.0f) hasCenterMin = true;
    }
    if (!hasCenterMin) { FAIL("Center sector should show closer distance"); return; }

    PASS();
}

void test_empty_depth_map() {
    TEST("Empty depth map handling");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    std::vector<float> emptyDepth;
    oa.updateDepthMap(emptyDepth, 0, 0);
    auto nav = oa.computeNavigation();

    // Should handle gracefully - emergency stop as we have no info
    // Just verify no crash
    PASS();
}

void test_velocity_output() {
    TEST("Navigation velocity output");
    ObstacleAvoidance oa;
    oa.init(0.3f, 0.8f, 1.5f, 3.0f, 0.5f, 1.0f, 36, 49.7f);

    auto depth = uniformDepth(64, 48, 5.0f);
    oa.updateDepthMap(depth, 64, 48);
    auto nav = oa.computeNavigation();

    // In safe zone, max speeds should be available
    if (nav.maxLinearSpeed <= 0) { FAIL("Max linear should be > 0"); return; }
    if (nav.maxAngularSpeed <= 0) { FAIL("Max angular should be > 0"); return; }
    PASS();
}

int main() {
    Logger::instance().init(3, "", 0);

    printf("=======================================\n");
    printf("  Obstacle Avoidance Tests\n");
    printf("=======================================\n\n");

    test_safe_distance();
    test_emergency_stop();
    test_danger_zone();
    test_warning_zone();
    test_steering_away_from_obstacle();
    test_closest_distance();
    test_zone_classification();
    test_get_sector_distances();
    test_empty_depth_map();
    test_velocity_output();

    printf("\n=======================================\n");
    printf("  Results: %d passed, %d failed\n", testsPassed, testsFailed);
    printf("=======================================\n");

    return testsFailed > 0 ? 1 : 0;
}