#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct SchedulerApp SchedulerApp;

void scheduler_start_view_rebuild(SchedulerApp* app);

void scheduler_start_view_reset(SchedulerApp* app);

// The "Random File" row is only shown in Random timing mode, so list positions
// after it shift by one. These convert between MenuIndices and list positions.
uint32_t scheduler_start_view_list_index(SchedulerApp* app, uint32_t menu_index);
uint32_t scheduler_start_view_menu_index(SchedulerApp* app, uint32_t list_index);
