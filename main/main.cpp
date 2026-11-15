#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.hpp"
#include "composition_root.hpp"
#include "periodic_task.hpp"

namespace {

// The period yields the CPU to lower-priority tasks. The spawn-only fields do
// not apply, because main's own task runs it.
constexpr PeriodicTask<Application>::Config APPLICATION_TASK_CONFIG{.period_ms = 10};

// Runs above every firmware task that irrigation control does not depend on
constexpr UBaseType_t APPLICATION_TASK_PRIORITY{6};

}  // namespace

extern "C" void app_main(void) {
    vTaskPrioritySet(nullptr, APPLICATION_TASK_PRIORITY);

    Application& app{create_application()};
    app.init();

    // Stack-local is safe because run_on_current_task() never returns, which keeps
    // this frame alive as long as the task.
    PeriodicTask<Application> application_task{APPLICATION_TASK_CONFIG, app};
    application_task.run_on_current_task();
}
