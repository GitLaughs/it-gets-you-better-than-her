#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>

class ConfigLoader {
public:
    ConfigLoader();
    ~ConfigLoader();

    bool load(const std::string& filename);
    bool save(const std::string& filename) const;

    // Typed getters with defaults
    std::string getString(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def = 0) const;
    float getFloat(const std::string& key, float def = 0.0f) const;
    bool getBool(const std::string& key, bool def = false) const;

    // Setters
    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value);
    void setFloat(const std::string& key, float value);
    void setBool(const std::string& key, bool value);

    bool hasKey(const std::string& key) const;
    std::vector<std::string> getAllKeys() const;

    // Reload from file
    bool reload();

    void dump() const;

private:
    bool parseYaml(const std::string& content);
    std::string trim(const std::string& s) const;

    std::map<std::string, std::string> data_;
    std::string filename_;
    mutable std::mutex mu_;
};