/*
  example_monitor.c — list MIDI ports, open the first input, print all messages

  Build:
    macOS:   cc monitor.c -framework CoreMIDI -o monitor
    Windows: cl monitor.c
    Linux:   cc monitor.c -ldl -lpthread -o monitor

  This process will appear to other MIDI software as "midi-monitor".
  Change the name passed to mm_context_init() to suit your application.
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

static const char* type_name(mm_message_type t) {
    switch (t) {
        case MM_NOTE_OFF:          return "NoteOff  ";
        case MM_NOTE_ON:           return "NoteOn   ";
        case MM_POLY_PRESSURE:     return "PolyPres ";
        case MM_CONTROL_CHANGE:    return "CC       ";
        case MM_PROGRAM_CHANGE:    return "ProgChg  ";
        case MM_CHANNEL_PRESSURE:  return "ChanPres ";
        case MM_PITCH_BEND:        return "PitchBnd ";
        case MM_SYSEX:             return "SysEx    ";
        case MM_MTC_QUARTER_FRAME: return "MTC-QF   ";
        case MM_SONG_POSITION:     return "SongPos  ";
        case MM_SONG_SELECT:       return "SongSel  ";
        case MM_TUNE_REQUEST:      return "TuneReq  ";
        case MM_CLOCK:             return "Clock    ";
        case MM_START:             return "Start    ";
        case MM_CONTINUE:          return "Continue ";
        case MM_STOP:              return "Stop     ";
        case MM_ACTIVE_SENSE:      return "ActSense ";
        case MM_RESET:             return "Reset    ";
        default:                   return "Unknown  ";
    }
}

static void on_midi(mm_device* dev, const mm_message* msg, void* ud) {
    (void)dev; (void)ud;

    if (msg->type == MM_SYSEX) {
        printf("[%8.3f] SysEx     %zu bytes:", msg->timestamp, msg->sysex_size);
        size_t show = msg->sysex_size < 16 ? msg->sysex_size : 16;
        for (size_t i = 0; i < show; i++) printf(" %02X", msg->sysex[i]);
        if (msg->sysex_size > 16) printf(" ...");
        printf("\n");

    } else if (msg->type == MM_SONG_POSITION) {
        printf("[%8.3f] %s beat=%-5u  (QN %.2f)\n",
               msg->timestamp, type_name(msg->type),
               msg->song_position, msg->song_position / 4.0);

    } else if (msg->type == MM_MTC_QUARTER_FRAME) {
        printf("[%8.3f] %s piece=0x%02X\n",
               msg->timestamp, type_name(msg->type), msg->data[0]);

    } else if (msg->type >= MM_CLOCK) {
        /* real-time / single-byte — skip printing on every clock tick to
           avoid flooding; print start/stop/continue/SPP/reset only */
        switch (msg->type) {
            case MM_CLOCK: return;   /* too frequent — suppress */
            default:
                printf("[%8.3f] %s\n", msg->timestamp, type_name(msg->type));
        }

    } else {
        printf("[%8.3f] %s ch=%-2u  d0=%-3u d1=%-3u\n",
               msg->timestamp, type_name(msg->type),
               msg->channel, msg->data[0], msg->data[1]);
    }
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    /* Optional: pass port index as first argument */
    uint32_t port_idx = 0;
    if (argc > 1) port_idx = (uint32_t)atoi(argv[1]);

    mm_context ctx;
    mm_result r = mm_context_init(&ctx, "midi-monitor");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    printf("Client name : \"%s\"\n\n", ctx.name);

    printf("=== MIDI Inputs ===\n");
    uint32_t in_count = mm_in_count(&ctx);
    if (in_count == 0) {
        printf("  (none)\n");
    } else {
        for (uint32_t i = 0; i < in_count; i++) {
            char name[256];
            mm_in_name(&ctx, i, name, sizeof(name));
            printf("  [%u] %s%s\n", i, name, i == port_idx ? "  <-- will open" : "");
        }
    }

    printf("\n=== MIDI Outputs ===\n");
    uint32_t out_count = mm_out_count(&ctx);
    if (out_count == 0) {
        printf("  (none)\n");
    } else {
        for (uint32_t i = 0; i < out_count; i++) {
            char name[256];
            mm_out_name(&ctx, i, name, sizeof(name));
            printf("  [%u] %s\n", i, name);
        }
    }

    if (in_count == 0) {
        printf("\nNo MIDI input devices found.\n");
        mm_context_uninit(&ctx);
        return 0;
    }

    if (port_idx >= in_count) {
        fprintf(stderr, "\nPort index %u out of range (0..%u)\n",
                port_idx, in_count - 1);
        mm_context_uninit(&ctx);
        return 1;
    }

    printf("\nOpening input [%u]...\n", port_idx);
    mm_device dev;
    r = mm_in_open(&ctx, &dev, port_idx, on_midi, NULL);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_in_open: %s\n", mm_result_string(r));
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

    printf("Listening... press Ctrl-C to stop.\n");
    printf("(MIDI Clock ticks are suppressed to avoid flooding)\n\n");
    printf("  timestamp   type       ch   d0  d1\n");
    printf("  ---------   ---------  --   --  --\n");

    while (1) mm_sleep_ms(100);

    mm_in_stop(&dev);
    mm_in_close(&dev);
    mm_context_uninit(&ctx);
    return 0;
}
