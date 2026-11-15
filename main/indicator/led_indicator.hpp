#pragma once

#include <cstdint>

#include "indicator/indicator.hpp"
#include "indicator/status.hpp"
#include "indicator/status_chain.hpp"
#include "shared/color.hpp"
#include "shared/constants.hpp"

class RgbLed;

class LedIndicator : public Indicator {
public:
    static constexpr Color color_for(Status status, std::int64_t now_us) {
        const Look look{look_for(status)};
        if (!look.blink) {
            return look.color;
        }
        return (now_us / BLINK_HALF_US) % 2 == 0 ? look.color : palette::OFF;
    }

    LedIndicator(RgbLed& led, const StatusChain& chain) : led_{led}, chain_{chain} {}

    void init() override;
    void update(std::int64_t now_us) override;

private:
    struct Look {
        Color color;
        bool blink{false};
    };

    static constexpr std::int64_t BLINK_HALF_US{250 * constants::US_PER_MS};

    static constexpr Look look_for(Status status) {
        switch (status) {
            case Status::Booting:
                return {.color = palette::BLUE, .blink = false};
            case Status::Nominal:
                return {.color = palette::GREEN, .blink = false};
            case Status::Warning:
                return {.color = palette::YELLOW, .blink = true};
            case Status::Error:
                break;
        }
        return {.color = palette::RED, .blink = true};
    }

    RgbLed& led_;
    const StatusChain& chain_;
};
