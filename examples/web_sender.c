/*
  web_sender.c — Native virtual MIDI source for testing examples/web_monitor.c

  Build:
    macOS: cc web_sender.c -framework CoreMIDI -o web_sender
    Linux: cc web_sender.c -lasound -lpthread -o web_sender

  Usage:
    ./web_sender             -- send 4 test cycles on channel 1
    ./web_sender 0           -- send test cycles forever
    ./web_sender 8 2         -- send 8 test cycles on channel 2

  Start this process first, then open or refresh web_monitor.html in a browser
  with Web MIDI support. The browser should list this virtual source as an
  input named "web-midi-test-source".
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

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

static mm_result send_msg(mm_device* dev, mm_message_type type,
                          uint8_t channel, uint8_t d0, uint8_t d1) {
    mm_message m = {0};
    m.type = type;
    m.channel = channel;
    m.data[0] = d0;
    m.data[1] = d1;
    return mm_out_send(dev, &m);
}

static mm_result send_song_position(mm_device* dev, uint16_t beat) {
    mm_message m = {0};
    m.type = MM_SONG_POSITION;
    m.song_position = beat;
    return mm_out_send(dev, &m);
}

static mm_result send_realtime(mm_device* dev, mm_message_type type) {
    mm_message m = {0};
    m.type = type;
    return mm_out_send(dev, &m);
}

static void log_send(const char* label, mm_result r) {
    printf("  %-16s %s\n", label, mm_result_string(r));
    fflush(stdout);
}

static int run_cycle(mm_device* dev, uint8_t channel, unsigned int cycle) {
    uint8_t note = (uint8_t)(60 + (cycle % 8));
    uint8_t cc_value = (uint8_t)((cycle * 16) % 128);
    uint16_t bend = (cycle & 1) ? 4096 : 12288;

    printf("\nCycle %u\n", cycle + 1);
    log_send("Start", send_realtime(dev, MM_START));
    mm_sleep_ms(80);

    log_send("SongPosition", send_song_position(dev, (uint16_t)(cycle * 6)));
    mm_sleep_ms(80);

    log_send("ProgramChange",
             send_msg(dev, MM_PROGRAM_CHANGE, channel, (uint8_t)(cycle % 8), 0));
    mm_sleep_ms(80);

    log_send("ControlChange",
             send_msg(dev, MM_CONTROL_CHANGE, channel, 1, cc_value));
    mm_sleep_ms(80);

    log_send("PitchBend",
             send_msg(dev, MM_PITCH_BEND, channel,
                      (uint8_t)(bend & 0x7F), (uint8_t)((bend >> 7) & 0x7F)));
    mm_sleep_ms(80);

    log_send("NoteOn", send_msg(dev, MM_NOTE_ON, channel, note, 100));
    mm_sleep_ms(300);
    log_send("NoteOff", send_msg(dev, MM_NOTE_OFF, channel, note, 0));
    mm_sleep_ms(80);

    for (int i = 0; i < 6; i++) {
        mm_result r = send_realtime(dev, MM_CLOCK);
        if (r != MM_SUCCESS) {
            log_send("Clock", r);
            return 1;
        }
        mm_sleep_ms(20);
    }
    printf("  %-16s %s\n", "Clock x6", mm_result_string(MM_SUCCESS));

    log_send("Stop", send_realtime(dev, MM_STOP));
    return 0;
}

int main(int argc, char* argv[]) {
    unsigned int cycles = 4;
    uint8_t channel = 0;

    if (argc > 1) cycles = (unsigned int)strtoul(argv[1], NULL, 10);
    if (argc > 2) {
        unsigned long one_based = strtoul(argv[2], NULL, 10);
        if (one_based < 1 || one_based > 16) {
            fprintf(stderr, "Channel must be 1..16\n");
            return 1;
        }
        channel = (uint8_t)(one_based - 1);
    }

    setup_ctrl_c();

    mm_context ctx;
    mm_result r = mm_context_init(&ctx, "web-midi-test-source");
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_context_init: %s\n", mm_result_string(r));
        return 1;
    }

    mm_device out;
    r = mm_out_open_virtual(&ctx, &out);
    if (r != MM_SUCCESS) {
        fprintf(stderr, "mm_out_open_virtual: %s\n", mm_result_string(r));
        fprintf(stderr, "Virtual MIDI output is supported by CoreMIDI and ALSA.\n");
        mm_context_uninit(&ctx);
        return 1;
    }

    printf("Virtual MIDI source created: \"%s\"\n", ctx.name);
    printf("Open or refresh web_monitor.html, grant MIDI permission, then make\n");
    printf("sure it opens this input. Press Enter here to send test events.\n");
    (void)getchar();

    unsigned int cycle = 0;
    while (g_running && (cycles == 0 || cycle < cycles)) {
        if (run_cycle(&out, channel, cycle) != 0) break;
        cycle++;
        if (g_running && (cycles == 0 || cycle < cycles)) mm_sleep_ms(700);
    }

    printf("\nDone.\n");
    mm_out_close(&out);
    mm_context_uninit(&ctx);
    return 0;
}
