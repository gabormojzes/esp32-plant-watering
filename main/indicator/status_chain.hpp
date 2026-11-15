#pragma once

#include <optional>
#include <span>

#include "indicator/status.hpp"
#include "indicator/status_source.hpp"

class StatusChain {
public:
    struct Config {
        // Highest priority first
        std::span<const StatusSource* const> sources;
    };

    explicit StatusChain(const Config& config) : config_{config} {}

    Status status() const {
        for (const StatusSource* source : config_.sources) {
            if (const std::optional<Status> source_status{source->status()}) {
                return *source_status;
            }
        }
        return Status::Nominal;
    }

private:
    Config config_{};
};
