// main.c
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <storage/storage.h>

#include "microvium.h"

#define TAG "microvium"

// A function in the host (this file) for the VM to call
#define IMPORT_CONSOLE_CLEAR 1
#define IMPORT_CONSOLE_LOG 2
#define IMPORT_CONSOLE_WARN 3

// A function exported by VM to for the host to call
const mvm_VMExportID INIT = 1;
/* Use when needed
const mvm_VMExportID MAIN = 2;
*/

mvm_TeError resolveImport(mvm_HostFunctionID id, void*, mvm_TfHostFunction* out);
mvm_TeError console_clear(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);
mvm_TeError console_log(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);
mvm_TeError console_warn(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);

/*
typedef enum { // typedef enum CON_EVENT {
    CON_NONE, // no event
    CON_CLEAR, // console.clear();
    CON_LOG, // console.log();
    CON_WARN, // console.warn(); shouldn't do anything in draw_callback
} CON_EVENT;
*/

typedef struct {
    /*
    uint32_t idx;
    FuriMutex* mutex;
    */
    FuriString* conLog;
    /*
    int conY;
    int conX;
    */
    // CON_EVENT conEvent;
} Console;

Console* console;

ViewPort* view_port;

static void draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_frame(canvas, 0, 0, 128, 50);
    elements_text_box(canvas, 0, 0, 128, 50, AlignCenter, AlignTop, "chimken", true); // furi_string_get_cstr(console->conLog)
    /*
    if (console->conEvent == CON_NONE) {
        // do nothing
    } else if (console->conEvent == CON_CLEAR) {
        canvas_clear(canvas);
        canvas_draw_frame(canvas, 0, 0, 128, 22);
    } else if (console->conEvent == CON_LOG) {
        canvas_draw_str(canvas, console->conX, console->conY, furi_string_get_cstr(console->conLog));
    } else if (console->conEvent == CON_WARN) {
        // do nothing
    }
    console->conEvent = CON_NONE;
    */
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t js_run(uint8_t* fileBuff, size_t fileSize) {
    mvm_TeError err;
    mvm_VM* vm;
    mvm_Value init;
    mvm_Value result;

    // Restore the VM from the snapshot
    err = mvm_restore(&vm, fileBuff, fileSize, NULL, resolveImport);
    if (err != MVM_E_SUCCESS) {
        FURI_LOG_E(TAG, "Error with restore: %d", err);
        return err;
    }

    // Find the "sayHello" function exported by the VM
    err = mvm_resolveExports(vm, &INIT, &init, 1);
    if (err != MVM_E_SUCCESS) {
        FURI_LOG_E(TAG, "Error with exports: %d", err);
        return err;
    }

    // Call "sayHello"
    err = mvm_call(vm, init, &result, NULL, 0);
    if (err != MVM_E_SUCCESS) {
        FURI_LOG_E(TAG, "Error with call: %d", err);
        return err;
    }

    // Clean up
    mvm_runGC(vm, true);

    return 0;
}

int32_t js_app() {
    InputEvent event;
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* bytecode = storage_file_alloc(storage);
    storage_file_open(bytecode, EXT_PATH("script.mvm-bc"), FSAM_READ, FSOM_OPEN_EXISTING);
    size_t fileSize = storage_file_size(bytecode);
    FURI_LOG_I("microvium", "File Size: %d", fileSize);
    uint8_t* fileBuff;
    fileBuff = malloc(fileSize);
    storage_file_read(bytecode, fileBuff, fileSize);
    storage_file_close(bytecode);
    storage_file_free(bytecode);

    furi_record_close(RECORD_STORAGE);

    view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, NULL);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    console = malloc(sizeof(Console));
    console->conLog = furi_string_alloc();

    while(1) {
        furi_check(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk);
        if(event.key == InputKeyOk) {
            js_run(fileBuff, fileSize);
            break;
        }
    }
    furi_delay_ms(500);

    while(1) {
        furi_check(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk);
        if(event.key == InputKeyBack) {
            break;
        }
    }

    furi_string_free(console->conLog);

    furi_message_queue_free(event_queue);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);

    free(console);
    return 0;
}

void fatalError(void* vm, int e) {
    UNUSED(vm);
    FURI_LOG_E(TAG, "Error: %d\n", e);
    furi_crash("Microvium fatal error");
}

/*
 * This function is called by `mvm_restore` to search for host functions
 * imported by the VM based on their ID. Given an ID, it needs to pass back
 * a pointer to the corresponding C function to be used by the VM.
 */
mvm_TeError resolveImport(mvm_HostFunctionID funcID, void* context, mvm_TfHostFunction* out) {
    UNUSED(context);
    if (funcID == IMPORT_CONSOLE_LOG) {
      *out = console_log;
      return MVM_E_SUCCESS;
    } else if (funcID == IMPORT_CONSOLE_CLEAR) {
        *out = console_clear;
        return MVM_E_SUCCESS;
    } else if (funcID == IMPORT_CONSOLE_WARN) {}
    return MVM_E_UNRESOLVED_IMPORT;
}

mvm_TeError console_clear(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(vm);
    UNUSED(funcID);
    UNUSED(result);
    UNUSED(args);
    furi_assert(argCount == 0);
    // console->conEvent = CON_CLEAR;
    FURI_LOG_I(TAG, "console.clear()\n");
    furi_string_reset(console->conLog);
    view_port_update(view_port);
    return MVM_E_SUCCESS;
}

mvm_TeError console_log(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(funcID);
    UNUSED(result);
    furi_assert(argCount == 1); // furi_assert(argCount == 3);
    FURI_LOG_I(TAG, "console.log()\n");
    furi_string_printf(console->conLog, "%s\n", (const char*)mvm_toStringUtf8(vm, args[0], NULL));
    /*
    console->conX = mvm_toInt32(vm, args[1]);
    console->conY = mvm_toInt32(vm, args[2]);
    console->conEvent = CON_LOG;
    */
    view_port_update(view_port);
    return MVM_E_SUCCESS;
}

mvm_TeError console_warn(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(funcID);
    UNUSED(result);
    furi_assert(argCount == 1);
    FURI_LOG_I(TAG, "console.warn()");
    FURI_LOG_W(TAG, "%s\n", (const char*)mvm_toStringUtf8(vm, args[0], NULL));
    // console->conEvent = CON_WARN;
    return MVM_E_SUCCESS;
}
