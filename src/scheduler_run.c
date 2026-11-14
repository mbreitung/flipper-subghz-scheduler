#include "scheduler_run.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <devices/cc1101_int/cc1101_int_interconnect.h>
#include <devices/devices.h>
#include <protocols/raw.h>
#include <subghz_protocol_registry.h>
#include <transmitter.h>

#define TAG "SubGhzSchedulerRun"

struct ScheduleTxRun {
    Storage* storage;
    FlipperFormat* fff_head;
    FlipperFormat* fff_file;
    FlipperFormat* fff_data;
    SubGhzEnvironment* environment;
    FuriString* protocol;
    FuriString* preset;
    FuriString* data;
    uint32_t ms_delay_delta;
    uint32_t frequency;
    uint16_t tx_delay_ms;
    bool filetype;
};

static ScheduleTxRun* tx_run_alloc() {
    ScheduleTxRun* tx_run = calloc(1, sizeof(ScheduleTxRun));
    tx_run->storage = furi_record_open(RECORD_STORAGE);
    tx_run->fff_head = flipper_format_file_alloc(tx_run->storage);
    tx_run->fff_file = flipper_format_file_alloc(tx_run->storage);
    tx_run->fff_data = flipper_format_string_alloc();
    tx_run->environment = subghz_environment_alloc();
    tx_run->protocol = furi_string_alloc();
    tx_run->preset = furi_string_alloc();
    tx_run->data = furi_string_alloc();
    return tx_run;
}

static void tx_run_free(ScheduleTxRun* tx_run) {
    flipper_format_free(tx_run->fff_file);
    flipper_format_file_close(tx_run->fff_head);
    flipper_format_free(tx_run->fff_head);
    flipper_format_free(tx_run->fff_data);
    furi_string_free(tx_run->preset);
    furi_string_free(tx_run->protocol);
    furi_string_free(tx_run->data);
    subghz_environment_free(tx_run->environment);
    furi_record_close(RECORD_STORAGE);
    free(tx_run);
}

static FuriHalSubGhzPreset scheduler_get_subghz_preset_name(const char* preset_name) {
    FuriHalSubGhzPreset preset = FuriHalSubGhzPresetIDLE;
    if(!strcmp(preset_name, "FuriHalSubGhzPresetOok270Async")) {
        preset = FuriHalSubGhzPresetOok270Async;
    } else if(!strcmp(preset_name, "FuriHalSubGhzPresetOok650Async")) {
        preset = FuriHalSubGhzPresetOok650Async;
    } else if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev238Async")) {
        preset = FuriHalSubGhzPreset2FSKDev238Async;
    } else if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev476Async")) {
        preset = FuriHalSubGhzPreset2FSKDev476Async;
    } else if(!strcmp(preset_name, "FuriHalSubGhzPresetCustom")) {
        preset = FuriHalSubGhzPresetCustom;
    } else {
        FURI_LOG_E(TAG, "Unknown or incompatible preset");
    }
    return preset;
}

static const char* const radio_device_name[RADIO_DEVICE_COUNT] = {
    SUBGHZ_DEVICE_CC1101_INT_NAME,
    SUBGHZ_DEVICE_CC1101_EXT_NAME,
};

static void
    transmit(SchedulerApp* app, const SubGhzDevice* device, SubGhzTransmitter* transmitter) {
    notification_message(app->notifications, &sequence_blink_stop);
    notification_message(app->notifications, &sequence_blink_start_cyan);

    bool started = subghz_devices_start_async_tx(device, subghz_transmitter_yield, transmitter);
    FURI_LOG_I(TAG, "start_async_tx returned %d", started);
    if(started) {
        uint32_t waited_ms = 0;
        while(!subghz_devices_is_async_complete_tx(device)) {
            furi_delay_ms(10);
            waited_ms += 10;
        }
        FURI_LOG_I(TAG, "async_tx completed after %lu ms", waited_ms);
        subghz_devices_stop_async_tx(device);
    }

    subghz_devices_idle(device);
    subghz_transmitter_stop(transmitter);
    subghz_transmitter_free(transmitter);
    notification_message(app->notifications, &sequence_blink_stop);
}

#define TX_WAIT_SLICE_MS 50

// Sleeps for `ms` in short slices so a stop request (Back) is noticed quickly.
// Returns false if stopped early.
static bool tx_wait(SchedulerApp* app, uint32_t ms) {
    while(ms > 0) {
        if(app->stop_tx) return false;
        const uint32_t slice = ms < TX_WAIT_SLICE_MS ? ms : TX_WAIT_SLICE_MS;
        furi_delay_ms(slice);
        ms -= slice;
    }
    return !app->stop_tx;
}

static int32_t scheduler_tx(void* context) {
    furi_assert(context);
    SchedulerApp* app = context;
    ScheduleTxRun* tx_run = tx_run_alloc();

    const bool radio = scheduler_get_radio(app->scheduler);
    const SubGhzDevice* device = subghz_devices_get_by_name(radio_device_name[radio]);
    FURI_LOG_I(TAG, "device=%p radio=%d connect=%d",
               (void*)device, radio, device ? subghz_devices_is_connect(device) : 0);
    bool began = subghz_devices_begin(device);
    FURI_LOG_I(TAG, "begin returned %d", began);
    subghz_devices_reset(device);

    tx_run->filetype = scheduler_get_file_type(app->scheduler);
    if(tx_run->filetype == SchedulerFileTypePlaylist) {
        flipper_format_file_open_existing(
            tx_run->fff_head, furi_string_get_cstr(app->tx_file_path));
        flipper_format_read_string(tx_run->fff_head, "sub", tx_run->data);
    } else {
        furi_string_set_str(tx_run->data, furi_string_get_cstr(app->tx_file_path));
    }

    tx_run->tx_delay_ms = scheduler_get_tx_delay_ms(app->scheduler);
    do {
        flipper_format_free(tx_run->fff_data);
        tx_run->fff_data = flipper_format_string_alloc();

        flipper_format_free(tx_run->fff_file);
        tx_run->fff_file = flipper_format_file_alloc(tx_run->storage);

        FURI_LOG_I(TAG, "Playlist item: %s", furi_string_get_cstr(tx_run->data));
        if(!flipper_format_file_open_existing(
               tx_run->fff_file, furi_string_get_cstr(tx_run->data))) {
            FURI_LOG_E(TAG, "Error loading file!");
            tx_run_free(tx_run);
            return -1;
        }

        flipper_format_read_uint32(tx_run->fff_file, "Frequency", &tx_run->frequency, 1);
        flipper_format_read_string(tx_run->fff_file, "Preset", tx_run->preset);
        flipper_format_read_string(tx_run->fff_file, "Protocol", tx_run->protocol);
        FURI_LOG_I(
            TAG,
            "freq=%lu preset=%s proto=%s",
            tx_run->frequency,
            furi_string_get_cstr(tx_run->preset),
            furi_string_get_cstr(tx_run->protocol));

        if(furi_string_equal(tx_run->protocol, "RAW")) {
            subghz_protocol_raw_gen_fff_data(
                tx_run->fff_data,
                furi_string_get_cstr(tx_run->data),
                subghz_devices_get_name(device));
        } else {
            stream_copy_full(
                flipper_format_get_raw_stream(tx_run->fff_file),
                flipper_format_get_raw_stream(tx_run->fff_data));
        }
        flipper_format_file_close(tx_run->fff_file);

        uint8_t key_bytes[8] = {0};
        flipper_format_rewind(tx_run->fff_data);
        if(flipper_format_read_hex(
               tx_run->fff_data, "Key", key_bytes, sizeof(key_bytes))) {
            FURI_LOG_I(
                TAG,
                "Key: %02X %02X %02X %02X %02X %02X %02X %02X",
                key_bytes[0],
                key_bytes[1],
                key_bytes[2],
                key_bytes[3],
                key_bytes[4],
                key_bytes[5],
                key_bytes[6],
                key_bytes[7]);
        } else {
            FURI_LOG_I(TAG, "Key: <not read>");
        }
        flipper_format_rewind(tx_run->fff_data);

        uint8_t tx_count = scheduler_get_tx_count(app->scheduler);
        for(uint_fast8_t i = 0; i <= tx_count && !app->stop_tx; ++i) {
            subghz_environment_set_protocol_registry(
                tx_run->environment, (void*)&subghz_protocol_registry);
            SubGhzTransmitter* transmitter = subghz_transmitter_alloc_init(
                tx_run->environment, furi_string_get_cstr(tx_run->protocol));
            FURI_LOG_I(TAG, "transmitter alloc_init: %p", (void*)transmitter);

            SubGhzProtocolStatus ds_status =
                subghz_transmitter_deserialize(transmitter, tx_run->fff_data);
            FURI_LOG_I(TAG, "deserialize status: %d", ds_status);

            FuriHalSubGhzPreset preset_enum =
                scheduler_get_subghz_preset_name(furi_string_get_cstr(tx_run->preset));
            subghz_devices_idle(device);
            FURI_LOG_I(TAG, "load_preset enum=%d", preset_enum);
            subghz_devices_load_preset(device, preset_enum, NULL);
            subghz_devices_idle(device);
            uint32_t req_freq = tx_run->frequency;
            tx_run->frequency = subghz_devices_set_frequency(device, tx_run->frequency);
            FURI_LOG_I(TAG, "set_frequency req=%lu actual=%lu", req_freq, tx_run->frequency);

            transmit(app, device, transmitter);
            FURI_LOG_D(TAG, "transmitting frequency: %ld", tx_run->frequency);

            if(!tx_wait(app, tx_run->tx_delay_ms)) break;
        }
        if(app->stop_tx) {
            FURI_LOG_I(TAG, "TX stopped by user");
            break;
        }
        bool more = tx_run->filetype == SchedulerFileTypePlaylist &&
                    flipper_format_read_string(tx_run->fff_head, "sub", tx_run->data);
        FURI_LOG_I(TAG, "Playlist next=%d data=%s", more, furi_string_get_cstr(tx_run->data));
        if(!more) break;
    } while(true);

    subghz_devices_sleep(device);
    subghz_devices_end(device);
    tx_run_free(tx_run);

    return 0;
}

static void
    scheduler_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    SchedulerApp* app = context;
    furi_assert(app->thread == thread);

    if(state == FuriThreadStateStopped) {
        FURI_LOG_I(TAG, "Thread stopped");
        furi_thread_free(thread);
        app->thread = NULL;
        app->is_transmitting = false;
        if(scheduler_get_tx_mode(app->scheduler) == SchedulerTxModeOneShot) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SchedulerSceneStart);
        }
    } else if(state == FuriThreadStateStarting) {
        FURI_LOG_I(TAG, "Thread starting");
        app->is_transmitting = true;
    }
}

void scheduler_start_tx(SchedulerApp* app) {
    if(app->thread || app->is_transmitting) {
        return;
    }
    app->stop_tx = false;
    app->thread = furi_thread_alloc_ex("SchedulerTxThread", 1024, scheduler_tx, app);
    furi_thread_set_state_callback(app->thread, scheduler_thread_state_callback);
    furi_thread_set_state_context(app->thread, app);
    furi_thread_start(app->thread);
}
