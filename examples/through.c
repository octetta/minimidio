/*
  example_through.c — MIDI through: echo every input message to an output

  Build:
    macOS:   cc through.c -framework CoreMIDI -o through
    Windows: cl through.c
    Linux:   cc through.c -lasound -lpthread -o through

  Usage:
    ./through              -- input[0] → output[0]
    ./through 1 2          -- input[1] → output[2]

  This process will appear to other MIDI software as "midi-through".
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#  define mm_sleep_ms(ms) Sleep(ms)
#else
#  include <unistd.h>
#  define mm_sleep_ms(ms) usleep((ms)*1000)
#endif

#include <signal.h>

static volatile int g_running = 1;
#ifdef _WIN32
static BOOL WINAPI ctrl_handler(DWORD e) {
    if (e == CTRL_C_EVENT || e == CTRL_BREAK_EVENT) { g_running = 0; return TRUE; }
    return FALSE;
}
static void setup_ctrl_c(void) { SetConsoleCtrlHandler(ctrl_handler, TRUE); }
#else
static void sig_handler(int s) { (void)s; g_running = 0; }
static void setup_ctrl_c(void) { signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler); }
#endif


static void on_midi(mm_device* in_dev, const mm_message* msg, void* ud) {
    mm_device* out = (mm_device*)ud;
    (void)in_dev;

    if (msg->type == MM_SYSEX) {
        mm_out_send_sysex(out, msg->sysex, msg->sysex_size);
    } else {
        mm_out_send(out, msg);
    }
}

int main(int argc, char* argv[]) {
    uint32_t in_idx  = 0;
    uint32_t out_idx = 0;
    if (argc > 1) in_idx  = (uint32_t)atoi(argv[1]);
    if (argc > 2) out_idx = (uint32_t)atoi(argv[2]);

    setup_ctrl_c();

    mm_context ctx;
    mm_result r = mm_context_init(&ctx, "midi-through");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    printf("Client name : \"%s\"\n\n", ctx.name);

    uint32_t n_in  = mm_in_count(&ctx);
    uint32_t n_out = mm_out_count(&ctx);

    printf("MIDI Inputs:\n");
    for (uint32_t i = 0; i < n_in; i++) {
        char name[256]; mm_in_name(&ctx, i, name, sizeof(name));
        printf("  [%u] %s%s\n", i, name, i == in_idx ? "  <-- source" : "");
    }
    if (n_in == 0) printf("  (none)\n");

    printf("MIDI Outputs:\n");
    for (uint32_t i = 0; i < n_out; i++) {
        char name[256]; mm_out_name(&ctx, i, name, sizeof(name));
        printf("  [%u] %s%s\n", i, name, i == out_idx ? "  <-- destination" : "");
    }
    if (n_out == 0) printf("  (none)\n");

    if (n_in == 0 || n_out == 0) {
        printf("\nNeed at least one input and one output.\n");
        mm_context_uninit(&ctx);
        return 1;
    }
    if (in_idx >= n_in) {
        fprintf(stderr, "Input index %u out of range\n", in_idx);
        mm_context_uninit(&ctx); return 1;
    }
    if (out_idx >= n_out) {
        fprintf(stderr, "Output index %u out of range\n", out_idx);
        mm_context_uninit(&ctx); return 1;
    }

    mm_device out;
    r = mm_out_open(&ctx, &out, out_idx);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_out_open: %s\n", mm_result_string(r));
        mm_context_uninit(&ctx); return 1;
    }

    mm_device in;
    r = mm_in_open(&ctx, &in, in_idx, on_midi, &out);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_in_open: %s\n", mm_result_string(r));
        mm_out_close(&out); mm_context_uninit(&ctx); return 1;
    }

    mm_in_start(&in);

    char in_name[256], out_name[256];
    mm_in_name(&ctx, in_idx, in_name, sizeof(in_name));
    mm_out_name(&ctx, out_idx, out_name, sizeof(out_name));
    printf("\nThrough active: [%s] --> [%s]\n", in_name, out_name);
    printf("All messages forwarded (including SysEx, clock, transport).\n");
    printf("Press Ctrl-C to stop.\n");

    while (g_running) mm_sleep_ms(100);
    printf("\nStopping...\n");

    mm_in_stop(&in);
    mm_in_close(&in);
    mm_out_close(&out);
    mm_context_uninit(&ctx);
    return 0;
}
