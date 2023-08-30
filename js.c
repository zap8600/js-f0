// main.c
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/view.h>
#include <gui/modules/text_box.h>
#include <gui/modules/dialog_ex.h>
#include <storage/storage.h>

#include "microvium.h"

#define TAG "microvium"

static int32_t js_run(void* context);

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
// mvm_TeError confirm(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);
mvm_TeError console_clear(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);
mvm_TeError console_log(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);
mvm_TeError console_warn(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);

typedef struct {
    FuriString* conLog;
} Console;

Console* console;

typedef enum {
    MyEventTypeKey,
    MyEventTypeDone,
} MyEventType;

typedef struct {
    MyEventType type; // The reason for this event.
    InputEvent input; // This data is specific to keypress data.
} MyEvent;

typedef enum {
    JSMain,
    JSConsole,
    //JSConfirm,
} ViewId;

FuriMessageQueue* queue;
ViewId current_view;

ViewDispatcher* view_dispatcher;

TextBox* text_box;

typedef struct {
    FuriThread* thread;
    uint8_t* fileBuff;
} JSRtThread;

/*
bool confirmGot = false;
bool confirmResult = false;
*/

static void draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 5, 30, "Hello world");
}

static bool input_callback(InputEvent* input_event, void* context) {
    UNUSED(context);
    bool handled = false;
    // we set our callback context to be the view_dispatcher.

    if(input_event->type == InputTypeShort) {
        if(input_event->key == InputKeyBack) {
            // Default back handler.
            handled = false; 
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
    ViewDispatcher* view_dispatcher_c = context;

    if(event == 42) {
        if(current_view == JSMain) {
            current_view = JSConsole;
        }

        view_dispatcher_switch_to_view(view_dispatcher_c, current_view);
        handled = true;
    }

    // NOTE: The return value is not currently used by the ViewDispatcher.
    return handled;
}

static uint32_t exit_console_callback(void* context) {
    UNUSED(context);
    return JSMain;
} 

/*
void confirm_callback(DialogExResult result, void* context) {
    UNUSED(context);
    if (result == DialogExResultLeft) {
        confirmGot = true;
        confirmResult = false;
    } else if (result == DialogExResultRight) {
        confirmGot = true;
        confirmResult = true;
    }
    current_view = JSMain;
    view_dispatcher_switch_to_view(view_dispatcher, current_view);
}
*/

static int32_t js_run(void* context) {
    JSRtThread* jsThread = (JSRtThread*)context;

    mvm_TeError err;
    mvm_VM* vm;
    mvm_Value init;
    mvm_Value result;

    // Restore the VM from the snapshot
    err = mvm_restore(&vm, jsThread->fileBuff, sizeof(jsThread->fileBuff), NULL, resolveImport);
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
    JSRtThread* jsThread = malloc(sizeof(JSRtThread));

    jsThread->thread = furi_thread_alloc_ex("microium", 1024, js_run, jsThread);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* bytecode = storage_file_alloc(storage);
    storage_file_open(bytecode, EXT_PATH("script.mvm-bc"), FSAM_READ, FSOM_OPEN_EXISTING);
    size_t fileSize = storage_file_size(bytecode);
    FURI_LOG_I("microvium", "File Size: %d", fileSize);
    jsThread->fileBuff = malloc(fileSize);
    storage_file_read(bytecode, jsThread->fileBuff, fileSize);
    storage_file_close(bytecode);
    storage_file_free(bytecode);

    furi_record_close(RECORD_STORAGE);

    view_dispatcher = view_dispatcher_alloc();

    // For this demo, we just use view_dispatcher as our application context.
    void* context = view_dispatcher;

    View* view1 = view_alloc();
    view_set_context(view1, context);
    view_set_draw_callback(view1, draw_callback);
    view_set_input_callback(view1, input_callback);
    view_set_orientation(view1, ViewOrientationHorizontal);

    text_box = text_box_alloc();
    text_box_set_font(text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(text_box), exit_console_callback);

    /*
    DialogEx* dialog_ex = dialog_ex_alloc();
    dialog_ex_set_context(dialog_ex, context);
    dialog_ex_set_result_callback(dialog_ex, confirm_callback);
    dialog_ex_set_left_button_text(dialog_ex, "No");
    dialog_ex_set_right_button_text(dialog_ex, "Yes");
    */

    // set param 1 of custom event callback (impacts tick and navigation too).
    view_dispatcher_set_event_callback_context(view_dispatcher, context);
    view_dispatcher_set_navigation_event_callback(
        view_dispatcher, navigation_event_callback);
    view_dispatcher_set_custom_event_callback(
        view_dispatcher, custom_event_callback);
    view_dispatcher_enable_queue(view_dispatcher);
    view_dispatcher_add_view(view_dispatcher, JSMain, view1);
    view_dispatcher_add_view(view_dispatcher, JSConsole, text_box_get_view(text_box));
    //view_dispatcher_add_view(view_dispatcher, JSConfirm, dialog_ex_get_view(dialog_ex));

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    current_view = JSMain;
    view_dispatcher_switch_to_view(view_dispatcher, current_view);

    console = malloc(sizeof(Console));
    console->conLog = furi_string_alloc();

    /*
    js_run(fileBuff, fileSize);
    text_box_set_text(text_box, furi_string_get_cstr(console->conLog));
    */

    furi_thread_start(jsThread->thread);
    view_dispatcher_run(view_dispatcher);

    furi_thread_join(jsThread->thread);
    furi_thread_free(jsThread->thread);
    free(jsThread);

    furi_string_free(console->conLog);
    free(console);

    view_dispatcher_remove_view(view_dispatcher, JSMain);
    view_dispatcher_remove_view(view_dispatcher, JSConsole);
    // view_dispatcher_remove_view(view_dispatcher, JSConfirm);
    furi_record_close(RECORD_GUI);
    view_dispatcher_free(view_dispatcher);

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

/*
mvm_TeError confirm(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(vm);
    UNUSED(funcID);
    UNUSED(result);
    UNUSED(args);
    furi_assert(argCount == 0);
    current_view = JSConfirm;
    view_dispatcher_switch_to_view(view_dispatcher, JSConfirm);
    while (!confirmGot) {}
    if(confirmResult) {
        FURI_LOG_I(TAG, "confirm(): Yes");
    } else if (!confirmResult) {
        FURI_LOG_I(TAG, "confirm(): No");
    }
    confirmGot = false;
    return MVM_E_SUCCESS;
}
*/

mvm_TeError console_clear(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(vm);
    UNUSED(funcID);
    UNUSED(result);
    UNUSED(args);
    furi_assert(argCount == 0);
    // console->conEvent = CON_CLEAR;
    FURI_LOG_I(TAG, "console.clear()\n");
    furi_string_reset(console->conLog);
    text_box_set_text(text_box, furi_string_get_cstr(console->conLog));
    return MVM_E_SUCCESS;
}

mvm_TeError console_log(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(funcID);
    UNUSED(result);
    furi_assert(argCount == 1); // furi_assert(argCount == 3);
    FURI_LOG_I(TAG, "console.log()\n");
    furi_string_cat_printf(console->conLog, "%s\n", (const char*)mvm_toStringUtf8(vm, args[0], NULL));
    text_box_set_text(text_box, furi_string_get_cstr(console->conLog));
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
