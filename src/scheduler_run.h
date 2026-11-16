#pragma once

#include "scheduler_app_i.h"
#include <flipper_format_i.h>

typedef struct ScheduleTxRun ScheduleTxRun;

void scheduler_start_tx(SchedulerApp* context);

// Joins and frees the TX thread on the calling (GUI) thread. With wait=false
// it only cleans up a thread that has already stopped. Returns true if a
// thread was freed.
bool scheduler_tx_thread_cleanup(SchedulerApp* app, bool wait);
