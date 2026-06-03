/*
  web_monitor.c — Web MIDI monitor for Emscripten builds

  Build:
    emcc web_monitor.c -sASYNCIFY -o web_monitor.html

  Serve the output over http://localhost or HTTPS, then open web_monitor.html.
  Web MIDI requires browser support and user permission. Define
  MM_WEBMIDI_ENABLE_SYSEX=1 before including minimidio.h if you need SysEx.
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

static const char* type_name(mm_message_type t) {
    switch (t) {
        case MM_NOTE_OFF:          return "NoteOff";
        case MM_NOTE_ON:           return "NoteOn";
        case MM_POLY_PRESSURE:     return "PolyPres";
        case MM_CONTROL_CHANGE:    return "CC";
        case MM_PROGRAM_CHANGE:    return "ProgChg";
        case MM_CHANNEL_PRESSURE:  return "ChanPres";
        case MM_PITCH_BEND:        return "PitchBnd";
        case MM_SYSEX:             return "SysEx";
        case MM_MTC_QUARTER_FRAME: return "MTC-QF";
        case MM_SONG_POSITION:     return "SongPos";
        case MM_SONG_SELECT:       return "SongSel";
        case MM_TUNE_REQUEST:      return "TuneReq";
        case MM_CLOCK:             return "Clock";
        case MM_START:             return "Start";
        case MM_CONTINUE:          return "Continue";
        case MM_STOP:              return "Stop";
        case MM_ACTIVE_SENSE:      return "ActSense";
        case MM_RESET:             return "Reset";
        default:                   return "Unknown";
    }
}

static void on_midi(mm_device* dev, const mm_message* msg, void* ud) {
    (void)dev; (void)ud;
    if (msg->type == MM_SYSEX) {
        printf("[%.3f] %s %zu bytes\n",
               msg->timestamp, type_name(msg->type), msg->sysex_size);
    } else if (msg->type == MM_SONG_POSITION) {
        printf("[%.3f] %s beat=%u\n",
               msg->timestamp, type_name(msg->type), msg->song_position);
    } else if (msg->type >= MM_CLOCK) {
        if (msg->type != MM_CLOCK)
            printf("[%.3f] %s\n", msg->timestamp, type_name(msg->type));
    } else {
        printf("[%.3f] %s ch=%u d0=%u d1=%u\n",
               msg->timestamp, type_name(msg->type),
               msg->channel, msg->data[0], msg->data[1]);
    }
    fflush(stdout);
}

int main(void) {
    static mm_context ctx;
    mm_result r = mm_context_init(&ctx, "web-midi-monitor");
    if (r != MM_SUCCESS) {
        printf("mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    uint32_t count = mm_in_count(&ctx);
    uint32_t port_idx = 0;
    int found_test_source = 0;

    printf("Web MIDI inputs:\n");
    for (uint32_t i = 0; i < count; i++) {
        char name[256];
        mm_in_name(&ctx, i, name, sizeof(name));
        if (!found_test_source && strstr(name, "web-midi-test-source")) {
            port_idx = i;
            found_test_source = 1;
        }
        printf("  [%u] %s\n", i, name);
    }

    if (count == 0) {
        printf("No Web MIDI input devices found.\n");
        mm_context_uninit(&ctx);
        return 0;
    }

    printf("Opening Web MIDI input[%u]%s.\n",
           port_idx, found_test_source ? " (web-midi-test-source)" : "");

    static mm_device dev;
    r = mm_in_open(&ctx, &dev, port_idx, on_midi, NULL);
    if (r != MM_SUCCESS) {
        printf("mm_in_open: %s\n", mm_result_string(r));
        mm_context_uninit(&ctx);
        return 1;
    }

    r = mm_in_start(&dev);
    if (r != MM_SUCCESS) {
        printf("mm_in_start: %s\n", mm_result_string(r));
        mm_in_close(&dev);
        mm_context_uninit(&ctx);
        return 1;
    }

    printf("Listening for Web MIDI input[%u]. Open the browser console for logs.\n",
           port_idx);

#ifdef __EMSCRIPTEN__
    emscripten_exit_with_live_runtime();
#endif
    return 0;
}
