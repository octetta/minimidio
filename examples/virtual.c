/*
  example_virtual.c — Create a virtual MIDI input port that other apps
                      (VMPK, Pure Data, DAWs, etc.) can connect to and
                      send MIDI into.

  Build:
    macOS:   cc virtual.c -framework CoreMIDI -o virtual_in
    Windows: cl virtual.c        (returns MM_NO_BACKEND; use loopMIDI)
    Linux:   cc virtual.c -ldl -lpthread -o virtual_in

  Once running:
    macOS:  your app appears in every MIDI app's output port list under
            the name you passed to mm_context_init.
    Linux:  visible in `aconnect -l`; connect with:
              aconnect "VMPK Output" "my-synth"
            or use qjackctl / Carla patchbay to wire it visually.
    Windows: not supported natively — install loopMIDI and use mm_in_open.
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define mm_sleep_ms(ms) Sleep(ms)
#else
#  include <unistd.h>
#  define mm_sleep_ms(ms) usleep((ms)*1000)
#endif

static void on_midi(mm_device* dev, const mm_message* msg, void* ud) {
    (void)dev; (void)ud;
    switch (msg->type) {
        case MM_NOTE_ON:
            printf("[%.3f] NoteOn   ch=%-2u  note=%-3u vel=%u\n",
                   msg->timestamp, msg->channel, msg->data[0], msg->data[1]);
            break;
        case MM_NOTE_OFF:
            printf("[%.3f] NoteOff  ch=%-2u  note=%-3u\n",
                   msg->timestamp, msg->channel, msg->data[0]);
            break;
        case MM_CONTROL_CHANGE:
            printf("[%.3f] CC       ch=%-2u  cc=%-3u  val=%u\n",
                   msg->timestamp, msg->channel, msg->data[0], msg->data[1]);
            break;
        case MM_PITCH_BEND: {
            int bend = ((int)msg->data[1] << 7) | msg->data[0];
            printf("[%.3f] PitchBnd ch=%-2u  val=%d\n",
                   msg->timestamp, msg->channel, bend - 8192);
            break;
        }
        case MM_PROGRAM_CHANGE:
            printf("[%.3f] ProgChg  ch=%-2u  prog=%u\n",
                   msg->timestamp, msg->channel, msg->data[0]);
            break;
        case MM_CLOCK:   return; /* suppress — too frequent */
        case MM_START:   printf("[%.3f] START\n",    msg->timestamp); break;
        case MM_STOP:    printf("[%.3f] STOP\n",     msg->timestamp); break;
        case MM_CONTINUE:printf("[%.3f] CONTINUE\n", msg->timestamp); break;
        case MM_SYSEX:
            printf("[%.3f] SysEx  %zu bytes\n", msg->timestamp, msg->sysex_size);
            break;
        default:
            printf("[%.3f] msg type=0x%02X\n", msg->timestamp, (unsigned)msg->type);
            break;
    }
    fflush(stdout);
}

int main(void) {
    mm_context ctx;
    mm_result r = mm_context_init(&ctx, "my-synth");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    mm_device dev;
    r = mm_in_open_virtual(&ctx, &dev, on_midi, NULL);
    if (r == MM_NO_BACKEND) {
        fprintf(stderr,
            "Virtual ports are not supported on Windows/WinMM.\n"
            "Install loopMIDI (https://www.tobias-erichsen.de/software/loopmidi.html),\n"
            "create a virtual cable, then use mm_in_open() with that port index.\n");
        mm_context_uninit(&ctx);
        return 1;
    }
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_in_open_virtual: %s\n", mm_result_string(r));
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

    printf("Virtual MIDI input created: \"%s\"\n\n", ctx.name);
    printf("macOS : open any app → MIDI output list → select \"%s\"\n", ctx.name);
    printf("Linux : check port is visible:\n");
    printf("          aconnect -l\n");
    printf("        then connect VMPK manually if needed:\n");
    printf("          aconnect \"VMPK Output\" \"%s\"\n", ctx.name);
    printf("        or use qjackctl / Carla patchbay\n\n");
    printf("Waiting for MIDI... (Ctrl-C to quit)\n\n");

    while (1) mm_sleep_ms(100);

    mm_in_stop(&dev);
    mm_in_close(&dev);
    mm_context_uninit(&ctx);
    return 0;
}
