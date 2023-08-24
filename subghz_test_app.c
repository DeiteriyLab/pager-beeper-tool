#include "subghz_cli.h"
#include "subghz_test_app.h"

#include <stdio.h>  
#include <string.h> 

#include <furi.h>
#include <furi_hal.h>

#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/protocols/protocol_items.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <flipper_format/flipper_format_i.h>



static void subghz_cli_radio_device_power_off() {
    if(furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
}

static void subghz_cli_radio_device_power_on() {
    uint8_t attempts = 5;
    while(--attempts > 0) {
        if(furi_hal_power_enable_otg()) break;
    }
    if(attempts == 0) {
        if(furi_hal_power_get_usb_voltage() < 4.5f) {
            FURI_LOG_E(
                "TAG",
                "Error power otg enable. BQ2589 check otg fault = %d",
                furi_hal_power_check_otg_fault() ? 1 : 0);
        }
    }
}

static const SubGhzDevice* subghz_cli_command_get_device(uint32_t* device_ind) {
    const SubGhzDevice* device = NULL;
    switch(*device_ind) {
    case 1:
        subghz_cli_radio_device_power_on();
        device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        break;

    default:
        device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        break;
    }
    //check if the device is connected
    if(!subghz_devices_is_connect(device)) {
        subghz_cli_radio_device_power_off();
        device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        *device_ind = 0;
    }
    return device;
}

void concatenate_result_key(char* str1, char* str2, char* str3, char* str4, char* str5)
{   
    int i = 0, j = 0;
    while (str1[i] != '\0') {        //key prefix
        str5[j] = str1[i];
        i++;
        j++;
    }
        i = 0;
    while (str2[i] != '\0') {        //base key
        str5[j] = str2[i];
        i++;
        j++;
    }
        i = 0;
    while (str3[i] != '\0') {        //group number
        str5[j] = str3[i];
        i++;
        j++;
    }
        i = 0;
    while (str4[i] != '\0') {        //pager key
        str5[j] = str4[i];
        i++;
        j++;
    }
    str5[j] = '\0';
    return;
}

unsigned char reverse(unsigned char b) {
   b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
   b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
   b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
   return b;
}
 

void subghz_cli_command_tx(void* ctx) {
    SubghzTestApp* app = ctx;
    app->sending = 1;
    char key_prefix[] = "0x0";
    char base_key[20];
    snprintf(base_key, 20, "%02x", app->base_key);
    char pager_key[10];
    char group_number[10];
    snprintf(pager_key, 20, "%02x", reverse(app->pager_number - 256 * ((int)app->pager_number/256)));
    char result_key [20];
    
    snprintf(group_number, 20, "%d", reverse((app->pager_number/256)<<2));

    concatenate_result_key(key_prefix, base_key, group_number, pager_key, result_key);
    FURI_LOG_E ("result_key", result_key);
    app->key = strtoul(result_key, NULL, 16);

    uint32_t frequency = 433920000;
    uint32_t repeat = 10;
    uint32_t te = 228;
    uint32_t device_ind = 0; // 0 - CC1101_INT, 1 - CC1101_EXT

    subghz_devices_init();
    const SubGhzDevice* device = subghz_cli_command_get_device(&device_ind);

    FuriString* flipper_format_string = furi_string_alloc_printf(
        "Protocol: Princeton\n"
        "Bit: 24\n"
        "Key: 00 00 00 00 00 %02X %02X %02X\n"
        "TE: %lu\n"
        "Repeat: %lu\n",
        (uint8_t)((app->key >> 16) & 0xFFU),
        (uint8_t)((app->key >> 8) & 0xFFU),
        (uint8_t)(app->key & 0xFFU),
        te,
        repeat);

    FlipperFormat* flipper_format = flipper_format_string_alloc();
    Stream* stream = flipper_format_get_raw_stream(flipper_format);
    stream_clean(stream);
    stream_write_cstring(stream, furi_string_get_cstr(flipper_format_string));
    SubGhzEnvironment* environment = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(environment, (void*)&subghz_protocol_registry);

    SubGhzTransmitter* transmitter = subghz_transmitter_alloc_init(environment, "Princeton");
    subghz_transmitter_deserialize(transmitter, flipper_format);

    subghz_devices_begin(device);
    subghz_devices_reset(device);
    subghz_devices_load_preset(device, FuriHalSubGhzPresetOok650Async, NULL);
    frequency = subghz_devices_set_frequency(device, frequency);

    furi_hal_power_suppress_charge_enter();
    if(subghz_devices_start_async_tx(device, subghz_transmitter_yield, transmitter)) {
        while(!(subghz_devices_is_async_complete_tx(device))) {
            printf(".");
            fflush(stdout);
            furi_delay_ms(333);
        }
        subghz_devices_stop_async_tx(device);

    } else {
        printf("Transmission on this frequency is restricted in your region\r\n");
    }

    subghz_devices_sleep(device);
    subghz_devices_end(device);
    subghz_devices_deinit();
    subghz_cli_radio_device_power_off();

    furi_hal_power_suppress_charge_exit();

    flipper_format_free(flipper_format);
    subghz_transmitter_free(transmitter);
    subghz_environment_free(environment);
    app->sending = 0;
}

static void subghz_test_app_draw_callback(Canvas* canvas, void* ctx) {

    furi_assert(ctx);
    canvas_clear(canvas);
    SubghzTestApp* app = ctx;
    char current_key[20];

    snprintf(current_key, 20, "%02x", app->key);
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Pager Beeper Tool");

    if (app->key_segment_number == 0) {
        canvas_draw_str(canvas, 80, 25, "_");
    }
    else if (app->key_segment_number == 1) {
        canvas_draw_str(canvas, 86, 25, "_");
    }
    else if (app->key_segment_number == 2) {
        canvas_draw_str(canvas, 92, 25, "_");
    }
    else if (app->key_segment_number == 3) {
        canvas_draw_str(canvas, 80, 40, "_");
    }
    
    char base_key[20];
    char result_key[20];
    canvas_draw_str(canvas, 0, 25, "Base key:");
    if (app->base_key == 0){
        canvas_draw_str(canvas, 80, 25, "000");
    }
    else if (app->base_key < 256){
        canvas_draw_str(canvas, 80, 25, "0");
        snprintf(base_key, 20, "%02x", app->base_key);
        canvas_draw_str(canvas, 86, 25, base_key);
    }
    else {
        snprintf(base_key, 20, "%02x", app->base_key);
        canvas_draw_str(canvas, 80, 25, base_key);
    }
    

    char pager_number[20];
    snprintf(pager_number, 20, "%d", app->pager_number);
    canvas_draw_str(canvas, 0, 40, "Pager number:");
    canvas_draw_str(canvas, 80, 40, pager_number);

    snprintf(result_key, 20, "%02x", app->key);
    if (app->sending == 1) {
        canvas_draw_str(canvas, 0, 55, "Sending key:");
        canvas_draw_str(canvas, 80, 55, result_key);
    }
    else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 52, "Press OK to beep, < > to move");
        canvas_draw_str(canvas, 0, 60, "^ v to change value ↑ ↓");
    }

    furi_mutex_release(app->mutex);
}

static void subghz_test_app_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);

    FuriMessageQueue* event_queue = ctx;

    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

SubghzTestApp* subghz_test_app_alloc() {
    SubghzTestApp* app = malloc(sizeof(SubghzTestApp));

    app->view_port = view_port_alloc();
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    view_port_draw_callback_set(app->view_port, subghz_test_app_draw_callback, app);
    view_port_input_callback_set(app->view_port, subghz_test_app_input_callback, app->event_queue);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    return app;
}

void subghz_test_app_free(SubghzTestApp* app) {
    furi_assert(app);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);

    furi_message_queue_free(app->event_queue);

    furi_record_close(RECORD_GUI);
}

int32_t subghz_test_app(void* p) {
    UNUSED(p);
    SubghzTestApp* app = subghz_test_app_alloc();
    InputEvent event;
    int base_key = 0x000;

    while(1) {
        if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                if(event.key == InputKeyBack) break;
                else if(event.key == InputKeyOk) subghz_cli_command_tx(app);
                else if(event.key == InputKeyUp){
                    if (app->key_segment_number == 0){
                        if (base_key + 256 <= 4095){
                            base_key = base_key + 256;
                        }
                    }
                    else if (app->key_segment_number == 1){
                        if (base_key + 16 <= 4095){
                            base_key = base_key + 16;
                        }
                    }
                    else if (app->key_segment_number == 2){
                        if (base_key + 1 <= 4095){
                            base_key = base_key + 1;
                        }
                    }
                    else if (app->key_segment_number == 3){
                        app->pager_number = (app->pager_number + 1) % 4096;
                    } 
                    app->base_key = base_key;
                }
                else if(event.key == InputKeyDown){
                    if (app->key_segment_number == 0){
                        if (base_key - 256 >= 0){
                            base_key = base_key - 256;
                        }
                    }
                    else if (app->key_segment_number == 1){
                        if (base_key - 16 >= 0){
                            base_key = base_key - 16;
                        }
                    }
                    else if (app->key_segment_number == 2){
                        if (base_key - 1 >= 0){
                            base_key = base_key - 1;
                        }
                    }
                    else if (app->key_segment_number == 3){
                        if (app->pager_number > 0) {
                            app->pager_number = (app->pager_number - 1) % 4096;
                        }
                    } 
                    app->base_key = base_key;
                }
            }
            else if(event.type == InputTypeLong) {
                if(event.key == InputKeyLeft)
                    app->key_segment_number = (app->key_segment_number - 1 + 4) % 4;
                else if(event.key == InputKeyRight)
                    app->key_segment_number = (app->key_segment_number + 1) % 4;

                else if(event.key == InputKeyUp){
                    if (app->key_segment_number == 3){
                        app->pager_number = (app->pager_number + 20) % 4096;
                    } 
                    app->base_key = base_key;
                }

                else if(event.key == InputKeyDown){
                   if (app->key_segment_number == 3){
                        if (app->pager_number >= 0) {
                            app->pager_number = (app->pager_number - 20) % 4096;
                        }
                    } 
                    app->base_key = base_key;
                }

                else if(event.key == InputKeyOk){
                    int i;
                    
                    for (i = 1; i < 21; ++i)
                    {
                        if (event.key == InputKeyBack){
                            i=101;
                            FURI_LOG_E ("STOP", "trying to stop");
                        } 
                        else {
                            app->pager_number = i;
                            subghz_cli_command_tx(app);
                        } 
                    }
                }
                view_port_update(app->view_port);
            }
        }
    }

    subghz_test_app_free(app);
    return 0;
}