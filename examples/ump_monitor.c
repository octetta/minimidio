/*
  ump_monitor.c — list ports, open one for raw Universal MIDI Packet input

  Build:
    Linux: cc ump_monitor.c -lasound -lpthread -o ump_monitor

  Usage:
    ./ump_monitor          -- opens input[0]
    ./ump_monitor 2        -- opens input[2]

  UMP input currently requires the ALSA sequencer MIDI 2.0/UMP API.
  Other backends return MM_NO_BACKEND for mm_in_open_ump.
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
#  define mm_sleep_ms(ms) usleep((ms) * 1000)
#endif

#include <signal.h>

static volatile int g_running = 1;

#ifdef _WIN32
static BOOL WINAPI ctrl_handler(DWORD e) {
    if (e == CTRL_C_EVENT || e == CTRL_BREAK_EVENT) {
        g_running = 0;
        return TRUE;
    }
    return FALSE;
}
static void setup_ctrl_c(void) { SetConsoleCtrlHandler(ctrl_handler, TRUE); }
#else
static void sig_handler(int s) { (void)s; g_running = 0; }
static void setup_ctrl_c(void) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
}
#endif

static void on_ump(mm_device* dev, const mm_ump_packet* pkt, void* ud) {
    (void)dev; (void)ud;
    printf("[%.3f] UMP", pkt->timestamp);
    for (uint8_t i = 0; i < pkt->word_count; i++) {
        printf(" %08x", pkt->words[i]);
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    uint32_t port_idx = 0;
    if (argc > 1) port_idx = (uint32_t)atoi(argv[1]);

    setup_ctrl_c();

    mm_context ctx;
    mm_result r = mm_context_init(&ctx, "ump-monitor");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    uint32_t caps = mm_context_caps(&ctx);
    printf("Client name : \"%s\"\n", ctx.name);
    printf("Capabilities: MIDI1=%s UMP=%s MIDI2=%s\n\n",
           (caps & MM_CAP_MIDI1) ? "yes" : "no",
           (caps & MM_CAP_UMP) ? "yes" : "no",
           (caps & MM_CAP_MIDI2) ? "yes" : "no");

    uint32_t count = mm_in_count(&ctx);
    printf("MIDI Inputs:\n");
    for (uint32_t i = 0; i < count; i++) {
        char name[256];
        mm_in_name(&ctx, i, name, sizeof(name));
        printf("  [%u] %s%s\n", i, name, i == port_idx ? "  <-- will open" : "");
    }
    if (count == 0) {
        printf("  (none)\n");
        mm_context_uninit(&ctx);
        return 0;
    }

    if (port_idx >= count) {
        fprintf(stderr, "\nPort index %u out of range (0..%u)\n",
                port_idx, count - 1);
        mm_context_uninit(&ctx);
        return 1;
    }

    mm_device dev;
    r = mm_in_open_ump(&ctx, &dev, port_idx, on_ump, NULL);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_in_open_ump: %s\n", mm_result_string(r));
        mm_context_uninit(&ctx);
        return 1;
    }

    r = mm_in_start(&dev);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_in_start: %s\n", mm_result_string(r));
        mm_in_close(&dev);
        mm_context_uninit(&ctx);
        return 1;
    }

    printf("\nListening for raw UMP input[%u]. Press Ctrl-C to stop.\n", port_idx);
    while (g_running) mm_sleep_ms(100);

    printf("\nStopping...\n");
    mm_in_stop(&dev);
    mm_in_close(&dev);
    mm_context_uninit(&ctx);
    return 0;
}
