#pragma once

#include "src/subghz_scheduler.h"
#include <storage/storage.h>

// Reads Lower/Upper (seconds) from a Flipper Format bounds file at `path`.
// On success applies them to the scheduler; on failure leaves it untouched.
bool scheduler_load_random_bounds(Scheduler* scheduler, Storage* storage, const char* path);
