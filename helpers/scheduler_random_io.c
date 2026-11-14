#include "scheduler_random_io.h"
#include "views/scheduler_start_view_settings.h"

#include <flipper_format/flipper_format.h>
#include <furi.h>

#define TAG "SubGhzSchedulerRandomIO"

bool scheduler_load_random_bounds(Scheduler* scheduler, Storage* storage, const char* path) {
    furi_assert(scheduler);
    furi_assert(storage);
    furi_assert(path);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    uint32_t lower = 0;
    uint32_t upper = 0;

    do {
        if(!flipper_format_file_open_existing(ff, path)) {
            FURI_LOG_W(TAG, "no bounds file at %s", path);
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
        if(lower == 0 || upper < lower) {
            FURI_LOG_E(TAG, "invalid bounds: %lu-%lu", lower, upper);
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
