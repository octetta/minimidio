/*
  example_output.c — open a MIDI output and play a C major scale

  Build:
    macOS:   cc output.c -framework CoreMIDI -o output_test
    Windows: cl output.c
    Linux:   cc output.c -ldl -lpthread -o output_test

  Usage:
    ./output_test          -- opens output[0]
    ./output_test 2        -- opens output[2]

  This process will appear to other MIDI software as "midi-output".
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

static void note_on(mm_device* dev, uint8_t note, uint8_t vel) {
    mm_message m = {0};
    m.type = MM_NOTE_ON; m.channel = 0;
    m.data[0] = note; m.data[1] = vel;
    mm_out_send(dev, &m);
}

static void note_off(mm_device* dev, uint8_t note) {
    mm_message m = {0};
    m.type = MM_NOTE_OFF; m.channel = 0;
    m.data[0] = note; m.data[1] = 0;
    mm_out_send(dev, &m);
}

int main(int argc, char* argv[]) {
    uint32_t port_idx = 0;
    if (argc > 1) port_idx = (uint32_t)atoi(argv[1]);

    mm_context ctx;
    mm_result r = mm_context_init(&ctx, "midi-output");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    printf("Client name : \"%s\"\n\n", ctx.name);

    uint32_t count = mm_out_count(&ctx);
    printf("MIDI Outputs:\n");
    if (count == 0) {
        printf("  (none)\n");
        mm_context_uninit(&ctx);
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        char name[256];
        mm_out_name(&ctx, i, name, sizeof(name));
        printf("  [%u] %s%s\n", i, name, i == port_idx ? "  <-- will open" : "");
    }

    if (port_idx >= count) {
        fprintf(stderr, "\nPort index %u out of range (0..%u)\n",
                port_idx, count - 1);
        mm_context_uninit(&ctx);
        return 1;
    }

    mm_device dev;
    r = mm_out_open(&ctx, &dev, port_idx);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_out_open: %s\n", mm_result_string(r));
        mm_context_uninit(&ctx);
        return 1;
    }

    /* C major scale, middle C = MIDI 60 */
    const uint8_t scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    const char*   names[] = { "C4","D4","E4","F4","G4","A4","B4","C5" };

    printf("\nPlaying C major scale on output[%u]...\n\n", port_idx);
    for (size_t i = 0; i < sizeof(scale); i++) {
        printf("  %s  (MIDI %u)\n", names[i], scale[i]);
        note_on(&dev, scale[i], 100);
        mm_sleep_ms(300);
        note_off(&dev, scale[i]);
        mm_sleep_ms(50);
    }

    /* All-notes-off on channel 0 just to be tidy */
    mm_message alloff = {0};
    alloff.type = MM_CONTROL_CHANGE; alloff.channel = 0;
    alloff.data[0] = 123; alloff.data[1] = 0;
    mm_out_send(&dev, &alloff);

    printf("\nDone.\n");
    mm_out_close(&dev);
    mm_context_uninit(&ctx);
    return 0;
}
