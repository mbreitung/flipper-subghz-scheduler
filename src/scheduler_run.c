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
    // Debug knobs, read from SCHEDULER_DEBUG_FILE at the start of each cycle
    uint32_t hold_ms; // >0: mimic native app button hold (Repeat=200, stop after hold_ms)
    bool reset_each_tx; // subghz_devices_reset before each preset load, like the native app
    bool set_tx; // subghz_devices_set_tx before start_async_tx, like the native app
};

#define SCHEDULER_DEBUG_FILE  EXT_PATH("subghz_scheduler/debug.txt")
#define NATIVE_HOLD_REPEAT    200
#define TX_THREAD_STACK_BYTES (4 * 1024)

/* Optional file, Flipper Format:
 *   Filetype: Flipper SubGHz Scheduler Debug
 *   Version: 1
 *   HoldMs: 1000
 *   Reset: 1      (reset radio before every send)
 *   SetTx: 1      (strobe TX before start_async_tx)
 *   NativeSeq: 1  (shorthand for Reset: 1 + SetTx: 1)
 */
static void tx_run_load_debug(ScheduleTxRun* tx_run) {
    tx_run->hold_ms = 0;
    // no debug file => same TX sequence as add_counter_loop
    tx_run->reset_each_tx = false;
    tx_run->set_tx = false;
    FlipperFormat* ff = flipper_format_file_alloc(tx_run->storage);
    if(flipper_format_file_open_existing(ff, SCHEDULER_DEBUG_FILE)) {
        uint32_t v = 0;
        if(flipper_format_read_uint32(ff, "HoldMs", &v, 1)) tx_run->hold_ms = v;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "NativeSeq", &v, 1)) {
            tx_run->reset_each_tx = tx_run->set_tx = v != 0;
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "Reset", &v, 1)) tx_run->reset_each_tx = v != 0;
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, "SetTx", &v, 1)) tx_run->set_tx = v != 0;
    }
    flipper_format_file_close(ff);
    flipper_format_free(ff);
    FURI_LOG_I(
        TAG,
        "debug: hold_ms=%lu reset=%d set_tx=%d",
        tx_run->hold_ms,
        tx_run->reset_each_tx,
        tx_run->set_tx);
}

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

static void transmit(
    SchedulerApp* app,
    const SubGhzDevice* device,
    SubGhzTransmitter* transmitter,
    const ScheduleTxRun* tx_run) {
    notification_message(app->notifications, &sequence_blink_stop);
    notification_message(app->notifications, &sequence_blink_start_cyan);

    if(tx_run->set_tx) {
        // Native app strobes STX itself before starting async TX
        bool set_tx = subghz_devices_set_tx(device);
        FURI_LOG_I(TAG, "set_tx returned %d", set_tx);
    }

    bool started = subghz_devices_start_async_tx(device, subghz_transmitter_yield, transmitter);
    FURI_LOG_I(TAG, "start_async_tx returned %d", started);
    if(started) {
        uint32_t waited_ms = 0;
        while(!subghz_devices_is_async_complete_tx(device)) {
            if(tx_run->hold_ms && waited_ms >= tx_run->hold_ms) break; // "button released"
            furi_delay_ms(10);
            waited_ms += 10;
        }
        FURI_LOG_I(
            TAG,
            "async_tx %s after %lu ms",
            subghz_devices_is_async_complete_tx(device) ? "completed" : "held/stopped",
            waited_ms);
        subghz_devices_stop_async_tx(device);
    } else {
        FURI_LOG_E(TAG, "start_async_tx FAILED (region/regulation?) freq=%lu", tx_run->frequency);
    }

    subghz_devices_idle(device);
    subghz_transmitter_stop(transmitter);
    subghz_transmitter_free(transmitter);
    notification_message(app->notifications, &sequence_blink_stop);
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
    tx_run_load_debug(tx_run);
    do {
        flipper_format_free(tx_run->fff_data);
        tx_run->fff_data = flipper_format_string_alloc();

        flipper_format_free(tx_run->fff_file);
        tx_run->fff_file = flipper_format_file_alloc(tx_run->storage);

        FURI_LOG_I(TAG, "Playlist item: %s", furi_string_get_cstr(tx_run->data));
        if(!flipper_format_file_open_existing(
               tx_run->fff_file, furi_string_get_cstr(tx_run->data))) {
            // Previously returned here without sleep/end, leaving the radio begun
            FURI_LOG_E(TAG, "Error loading file, skipping: %s", furi_string_get_cstr(tx_run->data));
            goto next_item;
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
        uint32_t file_te = 0, file_repeat = 0;
        flipper_format_read_uint32(tx_run->fff_data, "TE", &file_te, 1);
        flipper_format_rewind(tx_run->fff_data);
        bool has_repeat = flipper_format_read_uint32(tx_run->fff_data, "Repeat", &file_repeat, 1);
        FURI_LOG_I(
            TAG,
            "TE=%lu Repeat=%s%lu",
            file_te,
            has_repeat ? "" : "(default) ",
            has_repeat ? file_repeat : 10UL);
        flipper_format_rewind(tx_run->fff_data);

        uint8_t tx_count = scheduler_get_tx_count(app->scheduler);
        for(uint_fast8_t i = 0; i <= tx_count; ++i) {
            subghz_environment_set_protocol_registry(
                tx_run->environment, (void*)&subghz_protocol_registry);
            if(tx_run->hold_ms) {
                uint32_t rep = NATIVE_HOLD_REPEAT;
                flipper_format_rewind(tx_run->fff_data);
                flipper_format_insert_or_update_uint32(tx_run->fff_data, "Repeat", &rep, 1);
                flipper_format_rewind(tx_run->fff_data);
            }
            SubGhzTransmitter* transmitter = subghz_transmitter_alloc_init(
                tx_run->environment, furi_string_get_cstr(tx_run->protocol));
            FURI_LOG_I(TAG, "transmitter alloc_init: %p", (void*)transmitter);

            SubGhzProtocolStatus ds_status =
                subghz_transmitter_deserialize(transmitter, tx_run->fff_data);
            FURI_LOG_I(TAG, "deserialize status: %d", ds_status);

            FuriHalSubGhzPreset preset_enum =
                scheduler_get_subghz_preset_name(furi_string_get_cstr(tx_run->preset));
            if(tx_run->reset_each_tx) {
                subghz_devices_reset(device);
                FURI_LOG_I(TAG, "reset before preset load");
            }
            subghz_devices_idle(device);
            FURI_LOG_I(TAG, "load_preset enum=%d", preset_enum);
            subghz_devices_load_preset(device, preset_enum, NULL);
            subghz_devices_idle(device);
            uint32_t req_freq = tx_run->frequency;
            tx_run->frequency = subghz_devices_set_frequency(device, tx_run->frequency);
            FURI_LOG_I(
                TAG,
                "set_frequency req=%lu actual=%lu valid=%d",
                req_freq,
                tx_run->frequency,
                subghz_devices_is_frequency_valid(device, req_freq));

            transmit(app, device, transmitter, tx_run);
            FURI_LOG_D(TAG, "transmitting frequency: %ld", tx_run->frequency);

            furi_delay_ms(tx_run->tx_delay_ms);
        }
    next_item:;
        bool more = tx_run->filetype == SchedulerFileTypePlaylist &&
                    flipper_format_read_string(tx_run->fff_head, "sub", tx_run->data);
        FURI_LOG_I(TAG, "Playlist next=%d data=%s", more, furi_string_get_cstr(tx_run->data));
        if(!more) break;
    } while(true);

    subghz_devices_sleep(device);
    subghz_devices_end(device);
    tx_run_free(tx_run);
    FURI_LOG_I(
        TAG,
        "TX thread done, min free stack=%lu bytes",
        (uint32_t)furi_thread_get_stack_space(furi_thread_get_current_id()));

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
    app->thread = furi_thread_alloc_ex(
        "SchedulerTxThread", TX_THREAD_STACK_BYTES, scheduler_tx, app);
    furi_thread_set_state_callback(app->thread, scheduler_thread_state_callback);
    furi_thread_set_state_context(app->thread, app);
    furi_thread_start(app->thread);
}
