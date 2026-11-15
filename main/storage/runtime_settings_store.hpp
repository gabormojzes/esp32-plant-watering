#pragma once

#include <cstdint>

#include "runtime_settings.hpp"
#include "storage/nvs_manager.hpp"

// Persists the runtime-settable values across reboots. A stored set is valid
// only under a matching version stamp. On a mismatch load() erases it and the
// boot defaults stand. save_if_dirty() writes the values first and stamps the
// version last.
class RuntimeSettingsStore {
public:
    struct Config {
        std::uint32_t version;
    };

    RuntimeSettingsStore(RuntimeSettings& runtime_settings, const Config& config);
    // Mounts flash and loads the stored values. On failure the boot defaults
    // stand.
    void init();
    // Marks a setting changed. The write happens on the next save_if_dirty().
    void mark_dirty();
    void save_if_dirty();

private:
    void load() const;
    void load_sensor_calibration(bool& unreadable) const;
    void save() const;

    RuntimeSettings& runtime_settings_;
    Config config_{};
    NvsManager nvs_;
    bool dirty_{false};
};
