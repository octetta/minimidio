/*
  example_daw_sync.c — Receive MIDI clock, transport, Song Position, and MTC
                       from a DAW and print live sync state to the terminal.

  Build:
    macOS:   cc daw_sync.c -framework CoreMIDI -o daw_sync
    Windows: cl daw_sync.c
    Linux:   cc daw_sync.c -lasound -lpthread -o daw_sync

  Usage:
    ./daw_sync             -- opens input[0]
    ./daw_sync 2           -- opens input[2]

  This process will appear to other MIDI software as "daw-sync".
  In your DAW, enable MIDI clock output and point it at this client.

  What it handles:
    MM_CLOCK         — 24 pulses per beat; used to estimate BPM
    MM_START         — DAW started from bar 1
    MM_CONTINUE      — DAW resumed from current position
    MM_STOP          — DAW stopped
    MM_SONG_POSITION — DAW jumped / rewound; decoded beat count
    MM_MTC_QUARTER_FRAME — accumulates into full SMPTE timecode frame
    MM_ACTIVE_SENSE  — DAW keepalive (silently tracked)
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

#include <stdio.h>
#include <string.h>
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


/* ── Transport state ─────────────────────────────────────────────────────── */

typedef struct {
    volatile int      running;
    volatile int      clock_count;    /* 0–23 within current beat */
    volatile int      beat;           /* beats since last Start   */
    volatile double   last_clock_ts;
    volatile double   bpm;
    volatile uint32_t song_pos;       /* most recent SPP (beats)  */
    mm_mtc_state      mtc;
} daw_state;

static daw_state g_state;

/* ── Callback ─────────────────────────────────────────────────────────────── */

static void on_midi(mm_device* dev, const mm_message* msg, void* ud) {
    daw_state* s = (daw_state*)ud;
    (void)dev;

    switch (msg->type) {

        case MM_START:
            s->running     = 1;
            s->clock_count = 0;
            s->beat        = 0;
            s->song_pos    = 0;
            printf("\n[TRANSPORT] START\n");
            fflush(stdout);
            break;

        case MM_CONTINUE:
            s->running = 1;
            printf("\n[TRANSPORT] CONTINUE  (beat %d, SPP %u)\n",
                   s->beat, s->song_pos);
            fflush(stdout);
            break;

        case MM_STOP:
            s->running = 0;
            printf("\n[TRANSPORT] STOP  (beat %d, BPM %.2f)\n",
                   s->beat, s->bpm);
            fflush(stdout);
            break;

        case MM_CLOCK:
            if (!s->running) break;

            /* BPM from inter-clock interval (24 clocks per quarter note) */
            if (s->last_clock_ts > 0.0) {
                double interval = msg->timestamp - s->last_clock_ts;
                if (interval > 0.0)
                    s->bpm = 60.0 / (interval * 24.0);
            }
            s->last_clock_ts = msg->timestamp;

            if (++s->clock_count >= 24) {
                s->clock_count = 0;
                s->beat++;
                printf("\r  Beat %-6d  BPM: %6.2f  SPP: %-6u   ",
                       s->beat, s->bpm, s->song_pos);
                fflush(stdout);
            }
            break;

        case MM_SONG_POSITION:
            s->song_pos = msg->song_position;
            /* 1 SPP beat = 1 MIDI beat = 6 clocks = 1/16 note
               quarter notes = song_position / 4                */
            printf("\n[SPP] beat %-6u  QN: %.2f  bar(4/4): %.2f\n",
                   msg->song_position,
                   msg->song_position / 4.0,
                   msg->song_position / 16.0);
            fflush(stdout);
            break;

        case MM_MTC_QUARTER_FRAME: {
            mm_mtc_frame frame;
            if (mm_mtc_push(&s->mtc, msg->data[0], &frame)) {
                printf("\n[MTC] %02d:%02d:%02d:%02d  %s  (%.3f s)\n",
                       frame.hours, frame.minutes,
                       frame.seconds, frame.frames,
                       mm_mtc_rate_string(frame.rate),
                       mm_mtc_to_seconds(&frame));
                fflush(stdout);
            }
            break;
        }

        case MM_ACTIVE_SENSE:
            /* DAW is alive — silently ignore to avoid flooding */
            break;

        case MM_RESET:
            s->running     = 0;
            s->clock_count = 0;
            s->beat        = 0;
            s->song_pos    = 0;
            s->bpm         = 0;
            printf("\n[RESET]\n");
            fflush(stdout);
            break;

        default: break;
    }
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char* argv[]) {
    uint32_t port_idx = 0;
    if (argc > 1) port_idx = (uint32_t)atoi(argv[1]);

    setup_ctrl_c();

    mm_context ctx;
    mm_result  r = mm_context_init(&ctx, "daw-sync");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    printf("=== DAW Sync Monitor — minimidio v0.3.0 ===\n\n");
    printf("Client name : \"%s\"\n", ctx.name);
    printf("Port names  : \"%s-in\" / \"%s-out\"\n\n", ctx.name, ctx.name);

    uint32_t count = mm_in_count(&ctx);
    if (count == 0) {
        printf("No MIDI input devices found.\n");
        mm_context_uninit(&ctx);
        return 0;
    }

    printf("MIDI Inputs:\n");
    for (uint32_t i = 0; i < count; i++) {
        char name[256];
        mm_in_name(&ctx, i, name, sizeof(name));
        printf("  [%u] %s%s\n", i, name, i == port_idx ? "  <-- will open" : "");
    }

    if (port_idx >= count) {
        fprintf(stderr, "\nPort index %u out of range (0..%u)\n",
                port_idx, count - 1);
        mm_context_uninit(&ctx);
        return 1;
    }

    memset(&g_state, 0, sizeof(g_state));

    mm_device dev;
    r = mm_in_open(&ctx, &dev, port_idx, on_midi, &g_state);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "\nmm_in_open: %s\n", mm_result_string(r));
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

    printf("\nWaiting for DAW clock/transport... (Ctrl-C to quit)\n");
    printf("Enable MIDI clock output in your DAW and route it to \"%s\".\n\n",
           ctx.name);
    printf("Handles: CLOCK  START  STOP  CONTINUE  SONG-POSITION  MTC  RESET\n\n");

    while (g_running) mm_sleep_ms(100);
    printf("\nStopping...\n");

    mm_in_stop(&dev);
    mm_in_close(&dev);
    mm_context_uninit(&ctx);
    return 0;
}
