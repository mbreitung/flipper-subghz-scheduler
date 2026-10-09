#include "scheduler_random_io.h"
#include "views/scheduler_start_view_settings.h"

#include <flipper_format/flipper_format.h>
#include <furi.h>

#define TAG "SubGhzSchedulerRandomIO"

bool scheduler_load_random_bounds(Scheduler* scheduler, Storage* storage) {
    furi_assert(scheduler);
    furi_assert(storage);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    uint32_t lower = 0;
    uint32_t upper = 0;

    do {
        if(!flipper_format_file_open_existing(ff, SCHEDULER_RANDOM_INTERVAL_FILE)) {
            FURI_LOG_W(TAG, "no bounds file at %s", SCHEDULER_RANDOM_INTERVAL_FILE);
            break;
        }
        if(!flipper_format_read_uint32(ff, "Lower", &lower, 1)) {
            FURI_LOG_E(TAG, "missing Lower key");
            break;
        }
        if(!flipper_format_read_uint32(ff, "Upper", &upper, 1)) {
            FURI_LOG_E(TAG, "missing Upper key");
            break;
        }
        ok = true;
    } while(false);

    flipper_format_file_close(ff);
    flipper_format_free(ff);

    if(ok) {
        scheduler_set_random_bounds(scheduler, lower, upper);
        FURI_LOG_I(TAG, "loaded random bounds: %lu-%lu", lower, upper);
    }
    return ok;
}
