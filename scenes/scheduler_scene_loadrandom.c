#include "helpers/scheduler_random_io.h"
#include "src/scheduler_app_i.h"
#include "views/scheduler_start_view_settings.h"

#define TAG "SubGHzSchedulerLoadRandom"

static void select_random_file(SchedulerApp* app) {
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".txt", &I_time_7px);
    browser_options.skip_assets = true;
    browser_options.hide_ext = false;
    browser_options.base_path = SCHEDULER_APP_FOLDER;

    FuriString* path = furi_string_alloc_set(app->random_file_path);
    if(dialog_file_browser_show(app->dialogs, path, path, &browser_options)) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        const bool ok =
            scheduler_load_random_bounds(app->scheduler, storage, furi_string_get_cstr(path));
        furi_record_close(RECORD_STORAGE);

        if(ok) {
            furi_string_set(app->random_file_path, path);
        } else {
            dialog_message_show_storage_error(
                app->dialogs, "Not a valid random\nbounds file!\nNeeds Lower/Upper");
        }
    }
    furi_string_free(path);
}

void scheduler_scene_loadrandom_on_enter(void* context) {
    furi_assert(context);
    SchedulerApp* app = context;
    select_random_file(app);
    scene_manager_set_scene_state(app->scene_manager, SchedulerSceneStart, MenuIndexRandomFile);
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, SchedulerSceneStart);
}

bool scheduler_scene_loadrandom_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void scheduler_scene_loadrandom_on_exit(void* context) {
    UNUSED(context);
}
