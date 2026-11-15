#pragma once

#include <optional>

#include "fault/fault.hpp"
#include "fault/fault_store.hpp"
#include "indicator/status.hpp"
#include "indicator/status_source.hpp"

class FaultStatusSource : public StatusSource {
public:
    explicit FaultStatusSource(const FaultStore& fault_store) : fault_store_{fault_store} {}

    std::optional<Status> status() const override {
        const std::optional<Severity> worst{fault_store_.worst_severity()};
        if (!worst) {
            return std::nullopt;
        }
        return status_for(*worst);
    }

private:
    static constexpr Status status_for(Severity severity) {
        switch (severity) {
            case Severity::Warning:
                return Status::Warning;
            case Severity::Error:
                return Status::Error;
        }
        // A severity outside the switch maps to the gravest status
        return Status::Error;
    }

    const FaultStore& fault_store_;
};
