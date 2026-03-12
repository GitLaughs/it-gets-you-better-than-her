#include "../src/config/config_loader.h"
#include "../src/utils/logger.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    printf("[TEST] %s ... ", name); \
    fflush(stdout);

#define PASS() \
    do { printf("PASS\n"); testsPassed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); testsFailed++; } while(0)

#define ASSERT_EQ(a, b, msg) \
    if ((a) != (b)) { FAIL(msg); return; } 

#define ASSERT_STREQ(a, b, msg) \
    if ((a) != (b)) { \
        printf("FAIL: %s (got '%s', expected '%s')\n", msg, (a).c_str(), (b).c_str()); \
        testsFailed++; return; \
    }

#define ASSERT_NEAR(a, b, eps, msg) \
    if (std::abs((a) - (b)) > (eps)) { FAIL(msg); return; }

// Create temporary config file
static const char* TMP_CONFIG = "/tmp/test_vision_config.yaml";

void createTestConfig() {
    std::ofstream ofs(TMP_CONFIG);
    ofs << "# Test configuration\n"
        << "name: test_system\n"
        << "version: 1\n"
        << "\n"
        << "camera:\n"
        << "  device: /dev/video0\n"
        << "  width: 1280\n"
        << "  height: 720\n"
        << "  fps: 90\n"
        << "  focal_length_px: 600.0\n"
        << "  auto_exposure: true\n"
        << "\n"
        << "detector:\n"
        << "  model: yolov8n.onnx\n"
        << "  input_size: 320\n"
        << "  conf_threshold: 0.5\n"
        << "  nms_threshold: 0.45\n"
        << "  threads: 2\n"
        << "\n"
        << "depth:\n"
        << "  min_depth: 0.1\n"
        << "  max_depth: 10.0\n"
        << "\n"
        << "features:\n"
        << "  detection: true\n"
        << "  depth: false\n"
        << "  tracking: yes\n"
        << "  hand_control: no\n"
        << "\n"
        << "system:\n"
        << "  max_memory_mb: 256\n"
        << "  priority: -10\n"
        << "  hex_value: 0xFF\n"
        << "\n";
    ofs.close();
}

void test_load_config() {
    TEST("Load config file");
    createTestConfig();
    ConfigLoader cfg;
    bool ok = cfg.load(TMP_CONFIG);
    ASSERT_EQ(ok, true, "load should succeed");
    PASS();
}

void test_string_values() {
    TEST("String values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_STREQ(cfg.getString("camera.device"), std::string("/dev/video0"), "camera.device");
    ASSERT_STREQ(cfg.getString("detector.model"), std::string("yolov8n.onnx"), "detector.model");
    ASSERT_STREQ(cfg.getString("name"), std::string("test_system"), "name");
    PASS();
}

void test_int_values() {
    TEST("Integer values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_EQ(cfg.getInt("camera.width"), 1280, "camera.width");
    ASSERT_EQ(cfg.getInt("camera.height"), 720, "camera.height");
    ASSERT_EQ(cfg.getInt("camera.fps"), 90, "camera.fps");
    ASSERT_EQ(cfg.getInt("detector.input_size"), 320, "detector.input_size");
    ASSERT_EQ(cfg.getInt("detector.threads"), 2, "detector.threads");
    PASS();
}

void test_float_values() {
    TEST("Float values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_NEAR(cfg.getFloat("camera.focal_length_px"), 600.0f, 0.01f, "focal_length");
    ASSERT_NEAR(cfg.getFloat("detector.conf_threshold"), 0.5f, 0.01f, "conf_threshold");
    ASSERT_NEAR(cfg.getFloat("detector.nms_threshold"), 0.45f, 0.01f, "nms_threshold");
    ASSERT_NEAR(cfg.getFloat("depth.min_depth"), 0.1f, 0.01f, "min_depth");
    ASSERT_NEAR(cfg.getFloat("depth.max_depth"), 10.0f, 0.01f, "max_depth");
    PASS();
}

void test_bool_values() {
    TEST("Boolean values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_EQ(cfg.getBool("features.detection"), true, "detection=true");
    ASSERT_EQ(cfg.getBool("features.depth"), false, "depth=false");
    ASSERT_EQ(cfg.getBool("features.tracking"), true, "tracking=yes");
    ASSERT_EQ(cfg.getBool("features.hand_control"), false, "hand_control=no");
    ASSERT_EQ(cfg.getBool("camera.auto_exposure"), true, "auto_exposure=true");
    PASS();
}

void test_defaults() {
    TEST("Default values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_STREQ(cfg.getString("nonexistent", "default"), std::string("default"), "string default");
    ASSERT_EQ(cfg.getInt("nonexistent", 42), 42, "int default");
    ASSERT_NEAR(cfg.getFloat("nonexistent", 3.14f), 3.14f, 0.01f, "float default");
    ASSERT_EQ(cfg.getBool("nonexistent", true), true, "bool default true");
    ASSERT_EQ(cfg.getBool("nonexistent", false), false, "bool default false");
    PASS();
}

void test_hex_values() {
    TEST("Hex integer values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_EQ(cfg.getInt("system.hex_value"), 255, "0xFF should be 255");
    PASS();
}

void test_has_key() {
    TEST("Has key check");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_EQ(cfg.hasKey("camera.width"), true, "camera.width exists");
    ASSERT_EQ(cfg.hasKey("nonexistent"), false, "nonexistent doesn't exist");
    PASS();
}

void test_set_values() {
    TEST("Set values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    cfg.setString("custom.key", "hello");
    cfg.setInt("custom.number", 999);
    cfg.setFloat("custom.pi", 3.14159f);
    cfg.setBool("custom.flag", true);

    ASSERT_STREQ(cfg.getString("custom.key"), std::string("hello"), "custom string");
    ASSERT_EQ(cfg.getInt("custom.number"), 999, "custom int");
    ASSERT_NEAR(cfg.getFloat("custom.pi"), 3.14159f, 0.001f, "custom float");
    ASSERT_EQ(cfg.getBool("custom.flag"), true, "custom bool");
    PASS();
}

void test_save_and_reload() {
    TEST("Save and reload");
    const char* tmpOut = "/tmp/test_vision_config_out.yaml";

    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    cfg.setString("extra.key", "saved_value");
    bool saved = cfg.save(tmpOut);
    ASSERT_EQ(saved, true, "save should succeed");

    ConfigLoader cfg2;
    bool loaded = cfg2.load(tmpOut);
    ASSERT_EQ(loaded, true, "reload should succeed");
    ASSERT_EQ(cfg2.getInt("camera.width"), 1280, "width after reload");
    ASSERT_STREQ(cfg2.getString("extra.key"), std::string("saved_value"), "extra.key after reload");

    unlink(tmpOut);
    PASS();
}

void test_missing_file() {
    TEST("Missing file handling");
    ConfigLoader cfg;
    bool ok = cfg.load("/nonexistent/path/config.yaml");
    // Should return false but not crash
    ASSERT_EQ(ok, false, "missing file should return false");
    // Defaults should still work
    ASSERT_EQ(cfg.getInt("anything", 123), 123, "default after failed load");
    PASS();
}

void test_negative_values() {
    TEST("Negative values");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    ASSERT_EQ(cfg.getInt("system.priority"), -10, "negative int");
    PASS();
}

void test_get_all_keys() {
    TEST("Get all keys");
    ConfigLoader cfg;
    cfg.load(TMP_CONFIG);
    auto keys = cfg.getAllKeys();
    bool found = false;
    for (auto& k : keys) {
        if (k == "camera.width") found = true;
    }
    ASSERT_EQ(found, true, "camera.width should be in keys");
    ASSERT_EQ((int)keys.size() > 10, true, "should have many keys");
    PASS();
}

int main() {
    Logger::instance().init(3, "", 0); // Errors only

    printf("=======================================\n");
    printf("  Config Loader Tests\n");
    printf("=======================================\n\n");

    createTestConfig();

    test_load_config();
    test_string_values();
    test_int_values();
    test_float_values();
    test_bool_values();
    test_defaults();
    test_hex_values();
    test_has_key();
    test_set_values();
    test_save_and_reload();
    test_missing_file();
    test_negative_values();
    test_get_all_keys();

    // Cleanup
    unlink(TMP_CONFIG);

    printf("\n=======================================\n");
    printf("  Results: %d passed, %d failed\n", testsPassed, testsFailed);
    printf("=======================================\n");

    return testsFailed > 0 ? 1 : 0;
}