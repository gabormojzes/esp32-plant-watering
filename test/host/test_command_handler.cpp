#include <doctest.h>

#include <initializer_list>
#include <limits>
#include <optional>

#include "hardware/adc_range.hpp"
#include "messaging/command_handler.hpp"
#include "shared/constants.hpp"

using constants::US_PER_S;

namespace {

constexpr float TARGET{50.0f};
constexpr float DEADBAND{5.0f};
constexpr std::uint16_t ADC_MIN{1000};
constexpr std::uint16_t ADC_MAX{3000};
constexpr Command::Sensor::Calibration NO_CALIBRATION{
    .adc_min = std::nullopt, .adc_max = std::nullopt
};
constexpr std::int64_t DURATION_US{5 * US_PER_S};
constexpr std::int64_t WAIT_TIME_US{60 * US_PER_S};
// The same two settings as a command carries them
constexpr double DURATION_S{static_cast<double>(DURATION_US) / static_cast<double>(US_PER_S)};
constexpr double WAIT_TIME_S{static_cast<double>(WAIT_TIME_US) / static_cast<double>(US_PER_S)};

struct Fixture {
    RuntimeSettings runtime_settings{RuntimeSettings::Config{
        .sensor_target = TARGET,
        .sensor_deadband = DEADBAND,
        .sensor_calibration = {.adc_min = ADC_MIN, .adc_max = ADC_MAX},
        .pump_duration_us = DURATION_US,
        .pump_wait_time_us = WAIT_TIME_US,
        .pump_automatic = true
    }};
    CommandHandler handler{runtime_settings};
};

// A command carrying the pump requests and nothing else
Command pump_request_command(std::optional<bool> run, std::optional<bool> stop) {
    return {
        .sensor = {.target = std::nullopt, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = run,
            .stop = stop,
        },
    };
}

}  // namespace

TEST_CASE("an empty command changes nothing") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply({}));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
}

TEST_CASE("a command updates every field it carries") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor = {.target = 75.0, .deadband = 10.0, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 7,
            .wait_time_s = 30,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == 75.0f);
    CHECK(fixture.runtime_settings.sensor_deadband() == 10.0f);
    CHECK(fixture.runtime_settings.pump_duration() == 7 * US_PER_S);
    CHECK(fixture.runtime_settings.pump_wait_time() == 30 * US_PER_S);
}

TEST_CASE("fields the command does not carry keep their values") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor = {.target = std::nullopt, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 7,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.sensor_deadband() == DEADBAND);
    CHECK(fixture.runtime_settings.pump_duration() == 7 * US_PER_S);
    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
}

TEST_CASE("half a band carried judges itself against the half in use") {
    Fixture fixture{};
    // Fits around the target in use
    CHECK(fixture.handler.apply({
        .sensor = {.target = std::nullopt, .deadband = 20.0, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.sensor_deadband() == 20.0f);
    // That band no longer fits
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = 5.0, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.sensor_deadband() == 20.0f);
}

TEST_CASE("a band carried whole moves both halves at once") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor = {.target = 5.0, .deadband = 5.0, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == 5.0f);
    CHECK(fixture.runtime_settings.sensor_deadband() == 5.0f);
}

TEST_CASE("a rejected value does not block the other fields") {
    Fixture fixture{};
    // The accepted field still counts as a change.
    CHECK(fixture.handler.apply({
        .sensor = {.target = 200.0, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 7,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);  // Out of range, rejected
    CHECK(fixture.runtime_settings.pump_duration() == 7 * US_PER_S);
}

TEST_CASE("every out-of-range value is rejected") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = -1.0, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 0,
            .wait_time_s = -1,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
}

TEST_CASE("whole seconds carried as whole doubles are set") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor = {.target = std::nullopt, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 6.0,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.pump_duration() == 6 * US_PER_S);
}

TEST_CASE("a command carrying only the values in use changes nothing") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = TARGET, .deadband = DEADBAND, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = DURATION_S,
            .wait_time_s = WAIT_TIME_S,
            .automatic = true,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.sensor_deadband() == DEADBAND);
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
    CHECK(fixture.runtime_settings.pump_automatic());
}

TEST_CASE("one changed field among values in use is a change") {
    SUBCASE("half of the band") {
        Fixture fixture{};
        CHECK(fixture.handler.apply({
            .sensor = {.target = TARGET, .deadband = 6.0, .calibration = NO_CALIBRATION},
            .pump = {
                .duration_s = DURATION_S,
                .wait_time_s = WAIT_TIME_S,
                .automatic = true,
                .run = std::nullopt,
                .stop = std::nullopt
            },
        }));
        CHECK(fixture.runtime_settings.sensor_deadband() == 6.0f);
    }
    SUBCASE("the wait time") {
        Fixture fixture{};
        CHECK(fixture.handler.apply({
            .sensor = {.target = TARGET, .deadband = DEADBAND, .calibration = NO_CALIBRATION},
            .pump = {
                .duration_s = DURATION_S,
                .wait_time_s = WAIT_TIME_S + 1,
                .automatic = true,
                .run = std::nullopt,
                .stop = std::nullopt
            },
        }));
        CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US + US_PER_S);
    }
    SUBCASE("automatic watering") {
        Fixture fixture{};
        CHECK(fixture.handler.apply({
            .sensor = {.target = TARGET, .deadband = DEADBAND, .calibration = NO_CALIBRATION},
            .pump = {
                .duration_s = DURATION_S,
                .wait_time_s = WAIT_TIME_S,
                .automatic = false,
                .run = std::nullopt,
                .stop = std::nullopt
            },
        }));
        CHECK_FALSE(fixture.runtime_settings.pump_automatic());
    }
}

TEST_CASE("fractional seconds are ignored, not truncated") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = std::nullopt, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 2.7,
            .wait_time_s = 0.5,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
}

TEST_CASE("seconds too large for microseconds are ignored") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = std::nullopt, .deadband = std::nullopt, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = 1e18,
            .wait_time_s = -1e18,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.pump_duration() == DURATION_US);
    CHECK(fixture.runtime_settings.pump_wait_time() == WAIT_TIME_US);
}

TEST_CASE("percents on a tenth are set, however the double lands") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor = {.target = 55.7, .deadband = 0.3, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == doctest::Approx(55.7f));
    CHECK(fixture.runtime_settings.sensor_deadband() == doctest::Approx(0.3f));
}

TEST_CASE("a percent that only just misses a tenth is set as that tenth") {
    Fixture fixture{};
    // 55.5, off by the last bits of a sender's arithmetic, is stored as the tenth.
    CHECK(fixture.handler.apply({
        .sensor =
            {.target = 55.50000000000005,
             .deadband = 0.50000000000000044,
             .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == 55.5f);
    CHECK(fixture.runtime_settings.sensor_deadband() == 0.5f);
}

TEST_CASE("percents finer than a tenth are rounded onto one") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor = {.target = 55.54, .deadband = 0.26, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == 55.5f);
    CHECK(fixture.runtime_settings.sensor_deadband() == 0.3f);
}

TEST_CASE("a percent that leaves the range once rounded drops the band") {
    Fixture fixture{};
    // Rounding comes first, and the range judges what would be set.
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = 100.06, .deadband = 0.5, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.sensor_deadband() == DEADBAND);
}

TEST_CASE("percents past what a double holds drop the band") {
    Fixture fixture{};
    constexpr double infinity{std::numeric_limits<double>::infinity()};
    CHECK_FALSE(fixture.handler.apply({
        .sensor = {.target = infinity, .deadband = -infinity, .calibration = NO_CALIBRATION},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == TARGET);
    CHECK(fixture.runtime_settings.sensor_deadband() == DEADBAND);
}

TEST_CASE("one unusable half drops the band, not just itself") {
    SUBCASE("the target is the unusable one") {
        Fixture fixture{};
        CHECK_FALSE(fixture.handler.apply({
            .sensor = {.target = 150.0, .deadband = 2.0, .calibration = NO_CALIBRATION},
            .pump = {
                .duration_s = std::nullopt,
                .wait_time_s = std::nullopt,
                .automatic = std::nullopt,
                .run = std::nullopt,
                .stop = std::nullopt
            },
        }));
        CHECK(fixture.runtime_settings.sensor_target() == TARGET);
        CHECK(fixture.runtime_settings.sensor_deadband() == DEADBAND);
    }
    SUBCASE("the deadband is the unusable one") {
        Fixture fixture{};
        CHECK_FALSE(fixture.handler.apply({
            .sensor = {.target = 40.0, .deadband = -1.0, .calibration = NO_CALIBRATION},
            .pump = {
                .duration_s = std::nullopt,
                .wait_time_s = std::nullopt,
                .automatic = std::nullopt,
                .run = std::nullopt,
                .stop = std::nullopt
            },
        }));
        CHECK(fixture.runtime_settings.sensor_target() == TARGET);
        CHECK(fixture.runtime_settings.sensor_deadband() == DEADBAND);
    }
}

TEST_CASE("a calibration carried whole moves both endpoints at once") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor =
            {.target = std::nullopt,
             .deadband = std::nullopt,
             .calibration = {.adc_min = 1200.0, .adc_max = 2900.0}},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = 1200, .adc_max = 2900}
    );
}

TEST_CASE("half a calibration carried judges itself against the half in use") {
    Fixture fixture{};
    // Above the adc_min in use
    CHECK(fixture.handler.apply({
        .sensor =
            {.target = std::nullopt,
             .deadband = std::nullopt,
             .calibration = {.adc_min = std::nullopt, .adc_max = 1500.0}},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = 1500}
    );
    // Past that adc_max
    CHECK_FALSE(fixture.handler.apply({
        .sensor =
            {.target = std::nullopt,
             .deadband = std::nullopt,
             .calibration = {.adc_min = 1600.0, .adc_max = std::nullopt}},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = 1500}
    );
}

TEST_CASE("a count that is no whole raw count drops the calibration, not just itself") {
    const std::initializer_list<double> unusable_counts{
        1200.5,
        -1.0,
        70000.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()
    };
    SUBCASE("the adc_min is the unusable one") {
        for (const double unusable : unusable_counts) {
            Fixture fixture{};
            CHECK_FALSE(fixture.handler.apply({
                .sensor =
                    {.target = std::nullopt,
                     .deadband = std::nullopt,
                     .calibration = {.adc_min = unusable, .adc_max = 2900.0}},
                .pump = {
                    .duration_s = std::nullopt,
                    .wait_time_s = std::nullopt,
                    .automatic = std::nullopt,
                    .run = std::nullopt,
                    .stop = std::nullopt
                },
            }));
            CHECK(
                fixture.runtime_settings.sensor_calibration() ==
                SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
            );
        }
    }
    SUBCASE("the adc_max is the unusable one") {
        for (const double unusable : unusable_counts) {
            Fixture fixture{};
            CHECK_FALSE(fixture.handler.apply({
                .sensor =
                    {.target = std::nullopt,
                     .deadband = std::nullopt,
                     .calibration = {.adc_min = 1200.0, .adc_max = unusable}},
                .pump = {
                    .duration_s = std::nullopt,
                    .wait_time_s = std::nullopt,
                    .automatic = std::nullopt,
                    .run = std::nullopt,
                    .stop = std::nullopt
                },
            }));
            CHECK(
                fixture.runtime_settings.sensor_calibration() ==
                SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
            );
        }
    }
}

TEST_CASE("an adc_max past what the ADC returns drops the calibration") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply({
        .sensor =
            {.target = std::nullopt,
             .deadband = std::nullopt,
             .calibration = {.adc_min = 1200.0, .adc_max = adc_range::MAX_RAW + 1.0}},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
}

TEST_CASE("a rejected calibration does not block the band") {
    Fixture fixture{};
    CHECK(fixture.handler.apply({
        .sensor =
            {.target = 60.0,
             .deadband = std::nullopt,
             .calibration = {.adc_min = ADC_MAX, .adc_max = ADC_MIN}},
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = std::nullopt,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.sensor_target() == 60.0f);
    CHECK(
        fixture.runtime_settings.sensor_calibration() ==
        SensorCalibration{.adc_min = ADC_MIN, .adc_max = ADC_MAX}
    );
}

TEST_CASE("automatic watering is turned off and back on as carried") {
    Fixture fixture{};
    constexpr Command::Sensor no_band{
        .target = std::nullopt, .deadband = std::nullopt, .calibration = NO_CALIBRATION
    };
    CHECK(fixture.handler.apply({
        .sensor = no_band,
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = false,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK_FALSE(fixture.runtime_settings.pump_automatic());
    CHECK(fixture.handler.apply({
        .sensor = no_band,
        .pump = {
            .duration_s = std::nullopt,
            .wait_time_s = std::nullopt,
            .automatic = true,
            .run = std::nullopt,
            .stop = std::nullopt
        },
    }));
    CHECK(fixture.runtime_settings.pump_automatic());
}

TEST_CASE("pump_request asks for a run or a stop as carried") {
    CHECK(
        CommandHandler::pump_request(pump_request_command(true, std::nullopt)) == PumpRequest::Run
    );
    CHECK(
        CommandHandler::pump_request(pump_request_command(std::nullopt, true)) == PumpRequest::Stop
    );
}

TEST_CASE("pump_request lets a stop outrank a run carried alongside it") {
    CHECK(CommandHandler::pump_request(pump_request_command(true, true)) == PumpRequest::Stop);
}

TEST_CASE("pump_request asks for nothing unless a flag is true") {
    CHECK_FALSE(CommandHandler::pump_request({}));
    CHECK_FALSE(CommandHandler::pump_request(pump_request_command(false, false)));
    // A stop carried as false holds back nothing.
    CHECK(CommandHandler::pump_request(pump_request_command(true, false)) == PumpRequest::Run);
}

TEST_CASE("pump requests change no setting") {
    Fixture fixture{};
    CHECK_FALSE(fixture.handler.apply(pump_request_command(true, true)));
}
