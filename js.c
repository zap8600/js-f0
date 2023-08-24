// main.c
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <furi.h>
#include <gui/gui.h>
//#include <storage/storage.h>
//#include <toolbox/stream/stream.h>
//#include <toolbox/stream/file_stream.h>

#include "microvium.h"

#include "script.mvm-bc.h"

// A function in the host (this file) for the VM to call
#define IMPORT_PRINT 1

// A function exported by VM to for the host to call
const mvm_VMExportID MAIN = 1234;

mvm_TeError resolveImport(mvm_HostFunctionID id, void*, mvm_TfHostFunction* out);
mvm_TeError print(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount);

/*
typedef struct {
    FuriThread* thread;
} JSThread;
*/

FuriString* conLog;
int conY;
int conX;

//JSThread* main_js_thread;

InputEvent event;
FuriMessageQueue* event_queue;

static void draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, conX, conY, furi_string_get_cstr(conLog));
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t js_run(size_t fileSize) {
    mvm_TeError err;
    mvm_VM* vm;
    mvm_Value sayHello;
    mvm_Value result;

    // Restore the VM from the snapshot
    err = mvm_restore(&vm, script_mvm_bc, fileSize, NULL, resolveImport);
    if (err != MVM_E_SUCCESS) return err;

    // Find the "sayHello" function exported by the VM
    err = mvm_resolveExports(vm, &MAIN, &sayHello, 1);
    if (err != MVM_E_SUCCESS) return err;

    // Call "sayHello"
    err = mvm_call(vm, sayHello, &result, NULL, 0);
    if (err != MVM_E_SUCCESS) return err;

    // Clean up
    mvm_runGC(vm, true);

    return 0;
}

/*
static void configure_and_start_thread(int funcID) {
    UNUSED(funcID); //kept for future use
    main_js_thread->thread = furi_thread_alloc();
    // main_js_thread->FUNCID = funcID;
    furi_thread_set_name(main_js_thread->thread, "Microvium");
    furi_thread_set_stack_size(main_js_thread->thread, 1024);
    furi_thread_set_context(main_js_thread->thread, main_js_thread);
    furi_thread_set_callback(main_js_thread->thread, js_thread_body);
    furi_thread_start(main_js_thread->thread);
    return;
}
*/

int32_t js_app() {
    //size_t fileSize;
    event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, NULL);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    /*
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    file_stream_open(stream, "/ext/script.mvm-bc", FSAM_READ, FSOM_OPEN_EXISTING);
    fileSize = stream_size(stream);
    file_stream_close(stream);
    stream_free(stream);
    */

    conLog = furi_string_alloc();

    while(1) {
        furi_check(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk);
        if(event.key == InputKeyOk) {
            js_run(script_mvm_bc_len); //storage_file_size(bytecode));
            break;
        }
    }
    furi_delay_ms(500);

    /*
    furi_thread_join(main_js_thread->thread);

    furi_thread_free(main_js_thread->thread);

    free(main_js_thread);
    */

    while(1) {
        furi_check(furi_message_queue_get(event_queue, &event, FuriWaitForever) == FuriStatusOk);
        if(event.key == InputKeyBack) {
            break;
        }
    }

    furi_string_free(conLog);

    //storage_file_close(bytecode);
    //storage_file_free(bytecode);

    //furi_record_close(RECORD_STORAGE);

    furi_message_queue_free(event_queue);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    return 0;
}

void fatalError(void* vm, int e) {
    UNUSED(vm);
    FURI_LOG_E("microvium", "Error: %d\n", e);
    furi_crash("Microvium fatal error");
}

/*
 * This function is called by `mvm_restore` to search for host functions
 * imported by the VM based on their ID. Given an ID, it needs to pass back
 * a pointer to the corresponding C function to be used by the VM.
 */
mvm_TeError resolveImport(mvm_HostFunctionID funcID, void* context, mvm_TfHostFunction* out) {
    UNUSED(context);
    if (funcID == IMPORT_PRINT) {
      *out = print;
      return MVM_E_SUCCESS;
    }
    return MVM_E_UNRESOLVED_IMPORT;
}

mvm_TeError print(mvm_VM* vm, mvm_HostFunctionID funcID, mvm_Value* result, mvm_Value* args, uint8_t argCount) {
    UNUSED(funcID);
    UNUSED(result);
    furi_assert(argCount == 3);
    furi_string_printf(conLog, "%s\n", (const char*)mvm_toStringUtf8(vm, args[0], NULL));
    conX = mvm_toInt32(vm, args[1]);
    conY = mvm_toInt32(vm, args[2]);
    return MVM_E_SUCCESS;
}
