// main.c
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/view.h>
#include <gui/modules/text_box.h>
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

ViewId current_view;
FuriMessageQueue* queue;

static void draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 30, "Hello world");
}

static void input_callback(InputEvent* input_event, void* context) {
    furi_assert(context);
    bool handled = false;
    // we set our callback context to be the view_dispatcher.
    ViewDispatcher* view_dispatcher = context;

    if(input_event->type == InputTypeShort) {
        if(input_event->key == InputKeyBack) {
            // Default back handler.
            handled = false;
        } else if(input_event->key == InputKeyLeft) {
            x--;
            handled = true;
        } else if(input_event->key == InputKeyRight) {
            x++;
            handled = true;
        } else if(input_event->key == InputKeyUp) {
            y--;
            handled = true;
        } else if(input_event->key == InputKeyDown) {
            y++;
            handled = true;
        } else if(input_event->key == InputKeyOk) {
            // switch the view!
            view_dispatcher_send_custom_event(view_dispatcher, 42);
            handled = true;
        }
    }

    return handled;
}

bool navigation_event_callback(void* context) {
    UNUSED(context);
    // We did not handle the event, so return false.
    return false;
}

bool custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    bool handled = false;
    // we set our callback context to be the view_dispatcher.
    ViewDispatcher* view_dispatcher = context;

    if(event == 42) {
        if(current_view == MyViewId) {
            current_view = MyOtherViewId;
        } else {
            current_view = MyViewId;
        }

        view_dispatcher_switch_to_view(view_dispatcher, current_view);
        handled = true;
    }

    // NOTE: The return value is not currently used by the ViewDispatcher.
    return handled;
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

    ViewDispatcher* view_dispatcher = view_dispatcher_alloc();

    // For this demo, we just use view_dispatcher as our application context.
    void* context = view_dispatcher;

    View* view1 = view_alloc();
    view_set_context(view1, context);
    view_set_draw_callback(view1, draw_callback);
    view_set_input_callback(view1, input_callback);
    view_set_orientation(view1, ViewOrientationHorizontal);

    TextBox* text_box = text_box_alloc();
    text_box_set_font(text_box, TextBoxFontText);

    // set param 1 of custom event callback (impacts tick and navigation too).
    view_dispatcher_set_event_callback_context(view_dispatcher, context);
    view_dispatcher_set_navigation_event_callback(
        view_dispatcher, navigation_event_callback);
    view_dispatcher_set_custom_event_callback(
        view_dispatcher, custom_event_callback);
    view_dispatcher_enable_queue(view_dispatcher);
    view_dispatcher_add_view(view_dispatcher, MyViewId, view1);
    view_dispatcher_add_view(view_dispatcher, MyOtherViewId, text_box_get_view(text_box));

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    current_view = MyViewId;
    view_dispatcher_switch_to_view(view_dispatcher, current_view);

    console = malloc(sizeof(Console));
    console->conLog = furi_string_alloc();

    js_run(fileBuff, fileSize);
    text_box_set_text(text_box, furi_string_get_cstr(console->conLog));

    view_dispatcher_run(view_dispatcher);

    furi_string_free(console->conLog);

    view_dispatcher_remove_view(view_dispatcher, MyViewId);
    view_dispatcher_remove_view(view_dispatcher, MyOtherViewId);
    furi_record_close(RECORD_GUI);
    view_dispatcher_free(view_dispatcher);

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
    // view_port_update(view_port);
    return MVM_E_SUCCESS;
}

mvm_TeError console_log(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(funcID);
    UNUSED(result);
    furi_assert(argCount == 1); // furi_assert(argCount == 3);
    FURI_LOG_I(TAG, "console.log()\n");
    furi_string_cat_printf(console->conLog, "%s\n", (const char*)mvm_toStringUtf8(vm, args[0], NULL));
    /*
    console->conX = mvm_toInt32(vm, args[1]);
    console->conY = mvm_toInt32(vm, args[2]);
    console->conEvent = CON_LOG;
    */
    //view_port_update(view_port);
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
