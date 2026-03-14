#include "config_loader.h"
#include "../utils/logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cctype>

static const char* MOD = "Config";

ConfigLoader::ConfigLoader() {}
ConfigLoader::~ConfigLoader() {}

std::string ConfigLoader::trim(const std::string& s) const {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool ConfigLoader::parseYaml(const std::string& content) {
    // Simple YAML parser: supports dotted keys, basic types
    // Format: key: value  or  section:\n  subkey: value
    std::istringstream stream(content);
    std::string line;
    std::string currentSection;
    int lineNum = 0;

    while (std::getline(stream, line)) {
        lineNum++;

        // Remove comments
        size_t commentPos = line.find('#');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }

        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        // Check if this is a section header (top-level key with no value, ending with ':')
        // Detect indentation level
        size_t indent = line.find_first_not_of(" \t");
        if (indent == std::string::npos) continue;

        size_t colonPos = trimmed.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = trim(trimmed.substr(0, colonPos));
        std::string value = (colonPos + 1 < trimmed.size())
                            ? trim(trimmed.substr(colonPos + 1))
                            : "";

        if (key.empty()) continue;

        // Determine if section header or key-value pair
        if (indent == 0) {
            if (value.empty()) {
                // Section header
                currentSection = key;
                continue;
            } else {
                // Top-level key-value
                std::lock_guard<std::mutex> lk(mu_);
                data_[key] = value;
            }
        } else {
            // Nested key under current section
            if (!currentSection.empty()) {
                std::string fullKey = currentSection + "." + key;
                std::lock_guard<std::mutex> lk(mu_);
                data_[fullKey] = value;
            } else {
                // Treat as top-level with dots
                std::lock_guard<std::mutex> lk(mu_);
                data_[key] = value;
            }
        }
    }

    return true;
}

bool ConfigLoader::load(const std::string& filename) {
    filename_ = filename;

    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        LOG_E(MOD, "Cannot open config file: %s", filename.c_str());

        // Try alternate locations
        std::vector<std::string> altPaths = {
            "/etc/vision/" + filename,
            "/opt/vision/" + filename,
            "../" + filename,
            "../../" + filename
        };

        for (auto& alt : altPaths) {
            ifs.open(alt);
            if (ifs.is_open()) {
                filename_ = alt;
                LOG_I(MOD, "Found config at: %s", alt.c_str());
                break;
            }
        }

        if (!ifs.is_open()) {
            LOG_W(MOD, "No config file found, using defaults");
            return false;
        }
    }

    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    {
        std::lock_guard<std::mutex> lk(mu_);
        data_.clear();
    }

    if (!parseYaml(content)) {
        LOG_E(MOD, "Failed to parse config: %s", filename_.c_str());
        return false;
    }

    LOG_I(MOD, "Config loaded: %s (%d entries)", filename_.c_str(), (int)data_.size());

    if (Logger::instance().getLevel() <= LogLevel::LOG_DEBUG) {
        dump();
    }

    return true;
}

bool ConfigLoader::save(const std::string& filename) const {
    std::lock_guard<std::mutex> lk(mu_);

    std::string outFile = filename.empty() ? filename_ : filename;
    std::ofstream ofs(outFile);
    if (!ofs.is_open()) {
        LOG_E(MOD, "Cannot write config: %s", outFile.c_str());
        return false;
    }

    // Group by section
    std::map<std::string, std::vector<std::pair<std::string,std::string>>> sections;

    for (auto& [k, v] : data_) {
        size_t dotPos = k.find('.');
        if (dotPos != std::string::npos) {
            std::string section = k.substr(0, dotPos);
            std::string subkey = k.substr(dotPos + 1);
            sections[section].push_back({subkey, v});
        } else {
            sections[""].push_back({k, v});
        }
    }

    ofs << "# Vision System Configuration\n";
    ofs << "# Auto-generated\n\n";

    // Write top-level keys first
    if (sections.count("")) {
        for (auto& [k, v] : sections[""]) {
            ofs << k << ": " << v << "\n";
        }
        ofs << "\n";
    }

    // Write sections
    for (auto& [sec, kvs] : sections) {
        if (sec.empty()) continue;
        ofs << sec << ":\n";
        for (auto& [k, v] : kvs) {
            ofs << "  " << k << ": " << v << "\n";
        }
        ofs << "\n";
    }

    ofs.close();
    LOG_I(MOD, "Config saved: %s", outFile.c_str());
    return true;
}

std::string ConfigLoader::getString(const std::string& key,
                                     const std::string& def) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = data_.find(key);
    if (it != data_.end()) {
        // Remove surrounding quotes if present
        std::string val = it->second;
        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            return val.substr(1, val.size() - 2);
        }
        return val;
    }
    return def;
}

int ConfigLoader::getInt(const std::string& key, int def) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = data_.find(key);
    if (it != data_.end()) {
        try {
            // Handle hex
            if (it->second.find("0x") == 0 || it->second.find("0X") == 0) {
                return (int)strtol(it->second.c_str(), nullptr, 16);
            }
            return std::stoi(it->second);
        } catch (...) {
            return def;
        }
    }
    return def;
}

float ConfigLoader::getFloat(const std::string& key, float def) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = data_.find(key);
    if (it != data_.end()) {
        try {
            return std::stof(it->second);
        } catch (...) {
            return def;
        }
    }
    return def;
}

bool ConfigLoader::getBool(const std::string& key, bool def) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = data_.find(key);
    if (it != data_.end()) {
        std::string v = it->second;
        // Convert to lowercase
        std::transform(v.begin(), v.end(), v.begin(), ::tolower);
        if (v == "true" || v == "yes" || v == "1" || v == "on") return true;
        if (v == "false" || v == "no" || v == "0" || v == "off") return false;
    }
    return def;
}

void ConfigLoader::setString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);
    data_[key] = value;
}

void ConfigLoader::setInt(const std::string& key, int value) {
    std::lock_guard<std::mutex> lk(mu_);
    data_[key] = std::to_string(value);
}

void ConfigLoader::setFloat(const std::string& key, float value) {
    std::lock_guard<std::mutex> lk(mu_);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", value);
    data_[key] = buf;
}

void ConfigLoader::setBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lk(mu_);
    data_[key] = value ? "true" : "false";
}

bool ConfigLoader::hasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    return data_.find(key) != data_.end();
}

std::vector<std::string> ConfigLoader::getAllKeys() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> keys;
    keys.reserve(data_.size());
    for (auto& [k, v] : data_) {
        keys.push_back(k);
    }
    return keys;
}

bool ConfigLoader::reload() {
    if (filename_.empty()) return false;
    LOG_I(MOD, "Reloading config: %s", filename_.c_str());
    return load(filename_);
}

void ConfigLoader::dump() const {
    std::lock_guard<std::mutex> lk(mu_);
    LOG_D(MOD, "=== Config Dump ===");
    for (auto& [k, v] : data_) {
        LOG_D(MOD, "  %s = %s", k.c_str(), v.c_str());
    }
    LOG_D(MOD, "===================");
}