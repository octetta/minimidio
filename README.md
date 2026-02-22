# minimidio

> Single-file, cross-platform C header library for MIDI input/output.
> Modelled after [miniaudio](https://miniaud.io/).

```c
#define MINIMIDIO_IMPLEMENTATION
#include "minimidio.h"
```

One file to copy into your project. OS MIDI libraries are the only dependencies —
all present by default on macOS and Windows. On Linux you need `libasound2-dev`
for headers at build time and `libasound2` at runtime (standard on any ALSA system).

---

## Platform backends

| Platform | Backend | Link flag |
|----------|---------|-----------|
| macOS / iOS | CoreMIDI | `-framework CoreMIDI` |
| Windows (MSVC) | WinMM | automatic via `#pragma comment(lib, "winmm.lib")` |
| Windows (MinGW / Clang) | WinMM | `-lwinmm` |
| Linux | ALSA sequencer | `-lasound -lpthread` |

---

## Build

```bash
# macOS
cc my_app.c -framework CoreMIDI -o my_app

# Windows (MSVC) — winmm.lib linked automatically
cl my_app.c

# Windows (MinGW / Clang / Zig)
cc my_app.c -lwinmm -o my_app
zig cc my_app.c -target x86_64-windows-gnu -lwinmm -o my_app.exe

# Linux — requires libasound2-dev (headers) and libasound2 (runtime)
#   Ubuntu/Debian: sudo apt install libasound2-dev
#   Fedora/RHEL:   sudo dnf install alsa-lib-devel
cc my_app.c -lasound -lpthread -o my_app
```

---

## Quick start

```c
#define MINIMIDIO_IMPLEMENTATION
#include "minimidio.h"

static void on_midi(mm_device* dev, const mm_message* msg, void* ud) {
    (void)dev; (void)ud;
    if (msg->type == MM_NOTE_ON)
        printf("Note on  ch=%u note=%u vel=%u\n",
               msg->channel, msg->data[0], msg->data[1]);
}

int main(void) {
    mm_context ctx;
    mm_context_init(&ctx, "my-app");   /* name shown to other MIDI clients */

    mm_device dev;
    mm_in_open(&ctx, &dev, 0, on_midi, NULL);
    mm_in_start(&dev);

    /* ... run forever / event loop ... */

    mm_in_stop(&dev);
    mm_in_close(&dev);
    mm_context_uninit(&ctx);
}
```

---

## Client name

```c
mm_context_init(&ctx, "my-synth");   /* pass any name, or NULL for "minimidio" */
```

The name is what other MIDI software sees when it lists available clients:

**macOS** — Audio MIDI Setup, Logic, Ableton, any CoreMIDI app:
```
my-synth
  my-synth-in
  my-synth-out
```

**Linux** — `aconnect -l`, qjackctl, Ardour, JACK patchbay:
```
client 128: 'my-synth' [type=user]
    0 'my-synth-in'
    1 'my-synth-out'
```

**Windows** — WinMM has no client-name concept; the name is stored in
`ctx.name` and is accessible to your code but is not advertised to other apps.
Apps see only the hardware port name.

Port names are derived automatically as `"<name>-in"` and `"<name>-out"`.
The name is accessible at any time via `ctx.name`.

---

## Virtual ports

By default `mm_in_open` / `mm_out_open` connect *to* an existing hardware or
software port. Virtual ports flip this around — your process **becomes** a port
that other apps see in their MIDI lists and connect to freely.

```c
mm_device dev;

/* Become a MIDI destination — VMPK, DAWs, Pure Data send INTO us */
mm_in_open_virtual(&ctx, &dev, on_midi, NULL);
mm_in_start(&dev);

/* Become a MIDI source — other apps receive FROM us */
mm_device src;
mm_out_open_virtual(&ctx, &src);
mm_out_send(&src, &msg);   /* broadcasts to all subscribers */
```

After `mm_in_open_virtual` your process appears in every app's MIDI output
list under `ctx.name`. After `mm_out_open_virtual` it appears in every app's
MIDI input list.

### Platform notes

**macOS**: uses `MIDIDestinationCreate` / `MIDISourceCreate`. Appears
immediately in Logic, GarageBand, Ableton, VMPK, Pure Data — any CoreMIDI app.
No restart or rescan needed.

**Linux**: creates an ALSA sequencer port with `SUBS_WRITE` / `SUBS_READ`
capability flags. Visible in `aconnect -l`, qjackctl, Carla, Bitwig, Ardour,
VMPK, Pure Data. Wire it with:
```bash
aconnect "VMPK Output" "my-synth"   # connect VMPK to your virtual input
aconnect -l                          # list all ports
```

**Windows**: `mm_in_open_virtual` / `mm_out_open_virtual` return `MM_NO_BACKEND`.
WinMM has no virtual port API. Workaround: install
[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html), create a
virtual cable there, then use the regular `mm_in_open` / `mm_out_open` with
that port index.

### start / stop / close

`mm_in_start`, `mm_in_stop`, `mm_in_close`, and `mm_out_close` all work
identically for virtual and normal devices — the library handles the difference
internally. Check `dev.is_virtual` if you need to know which kind you have.

---

## DAW clock & transport — quick start

```c
#define MINIMIDIO_IMPLEMENTATION
#include "minimidio.h"

static int        beat      = 0;
static int        clocks    = 0;   /* 24 per beat */
static mm_mtc_state mtc     = {0};

void on_midi(mm_device* dev, const mm_message* msg, void* ud) {
    (void)dev; (void)ud;
    switch (msg->type) {
        case MM_START:    beat=0; clocks=0; puts("PLAY");     break;
        case MM_CONTINUE:                   puts("CONTINUE"); break;
        case MM_STOP:                       puts("STOP");     break;

        case MM_CLOCK:
            if (++clocks >= 24) { clocks=0; beat++; }
            break;

        case MM_SONG_POSITION:
            /* 1 SPP beat = 6 clocks = one 16th note */
            printf("SPP beat %u  (QN %.2f)\n",
                   msg->song_position, msg->song_position / 4.0);
            break;

        case MM_MTC_QUARTER_FRAME: {
            mm_mtc_frame frame;
            if (mm_mtc_push(&mtc, msg->data[0], &frame))
                printf("MTC %02d:%02d:%02d:%02d %s\n",
                       frame.hours, frame.minutes,
                       frame.seconds, frame.frames,
                       mm_mtc_rate_string(frame.rate));
            break;
        }
        default: break;
    }
}

int main(void) {
    mm_context ctx;
    mm_context_init(&ctx, "daw-sync");
    /* ... open device, start, loop ... */
}
```

---

## Full API reference

### Context

```c
/* name: shown to other MIDI clients (CoreMIDI, ALSA). NULL → "minimidio".
   On Windows the name is stored but not advertised by WinMM.            */
mm_result mm_context_init  (mm_context* ctx, const char* name);
mm_result mm_context_uninit(mm_context* ctx);
```

`ctx.name` is a `char[64]` field accessible after init:

```c
printf("Running as: %s\n", ctx.name);
```

### Enumeration

```c
uint32_t  mm_in_count (mm_context* ctx);
mm_result mm_in_name  (mm_context* ctx, uint32_t idx, char* buf, size_t bufsz);
uint32_t  mm_out_count(mm_context* ctx);
mm_result mm_out_name (mm_context* ctx, uint32_t idx, char* buf, size_t bufsz);
```

### Input

```c
mm_result mm_in_open  (mm_context* ctx, mm_device* dev, uint32_t idx,
                       mm_callback cb, void* userdata);
mm_result mm_in_start (mm_device* dev);
mm_result mm_in_stop  (mm_device* dev);
mm_result mm_in_close (mm_device* dev);
```

Callbacks arrive on a **background thread**. Do not call `mm_in_stop` or
`mm_in_close` from inside a callback.

### Output

```c
mm_result mm_out_open      (mm_context* ctx, mm_device* dev, uint32_t idx);
mm_result mm_out_send      (mm_device* dev, const mm_message* msg);
mm_result mm_out_send_sysex(mm_device* dev, const uint8_t* data, size_t size);
mm_result mm_out_close     (mm_device* dev);
```

---

## Message types

### Channel messages

| Type | Status | data[0] | data[1] |
|------|--------|---------|---------|
| `MM_NOTE_OFF` | 0x8n | note | velocity |
| `MM_NOTE_ON` | 0x9n | note | velocity |
| `MM_POLY_PRESSURE` | 0xAn | note | pressure |
| `MM_CONTROL_CHANGE` | 0xBn | CC number | value |
| `MM_PROGRAM_CHANGE` | 0xCn | program | — |
| `MM_CHANNEL_PRESSURE` | 0xDn | pressure | — |
| `MM_PITCH_BEND` | 0xEn | LSB | MSB |

### System common

| Type | Status | Notes |
|------|--------|-------|
| `MM_SYSEX` | 0xF0 | `msg->sysex` / `msg->sysex_size` |
| `MM_MTC_QUARTER_FRAME` | 0xF1 | `data[0]` = raw QF byte; feed to `mm_mtc_push()` |
| `MM_SONG_POSITION` | 0xF2 | `msg->song_position` = 14-bit beat count |
| `MM_SONG_SELECT` | 0xF3 | `data[0]` = song number |
| `MM_TUNE_REQUEST` | 0xF6 | no data |

### System real-time

| Type | Status | Meaning |
|------|--------|---------|
| `MM_CLOCK` | 0xF8 | 24 pulses per quarter note |
| `MM_START` | 0xFA | Play from bar 1 |
| `MM_CONTINUE` | 0xFB | Resume from current position |
| `MM_STOP` | 0xFC | Stop |
| `MM_ACTIVE_SENSE` | 0xFE | DAW keepalive (~300 ms) |
| `MM_RESET` | 0xFF | System reset |

---

## mm_message struct

```c
typedef struct mm_message {
    mm_message_type type;

    uint8_t  channel;       /* channel messages: 0–15                      */
    uint8_t  data[2];

    double   timestamp;     /* seconds since device was opened              */

    uint16_t song_position; /* MM_SONG_POSITION only: 14-bit beat count     */
                            /* quarter notes = song_position / 4.0          */

    const uint8_t* sysex;      /* MM_SYSEX only                            */
    size_t         sysex_size;
} mm_message;
```

---

## MTC utilities

```c
mm_mtc_state state = {0};   /* zero-init once per device */

/* In your callback: */
if (msg->type == MM_MTC_QUARTER_FRAME) {
    mm_mtc_frame frame;
    if (mm_mtc_push(&state, msg->data[0], &frame)) {
        /* fires once per 8 quarter-frames (twice per video frame) */
        printf("%02d:%02d:%02d:%02d @ %s\n",
               frame.hours, frame.minutes,
               frame.seconds, frame.frames,
               mm_mtc_rate_string(frame.rate));

        double t = mm_mtc_to_seconds(&frame);
    }
}
```

### `mm_mtc_rate` values

| Value | Meaning |
|-------|---------|
| `MM_MTC_24FPS` | 24 fps film |
| `MM_MTC_25FPS` | 25 fps PAL |
| `MM_MTC_30FPS_DROP` | 29.97 fps drop (NTSC video) |
| `MM_MTC_30FPS` | 30 fps non-drop |

---

## Song Position maths

```
msg->song_position  =  MIDI beats  (1 beat = 6 MIDI clocks = one 16th note)

quarter notes  =  song_position / 4.0
bars (4/4)     =  song_position / 16.0
```

---

## BPM from MIDI clock

```c
static double last_ts = 0;
static double bpm     = 0;

/* In MM_CLOCK handler: */
if (last_ts > 0) {
    double interval = msg->timestamp - last_ts;
    if (interval > 0) bpm = 60.0 / (interval * 24.0);
}
last_ts = msg->timestamp;
```

---

## Result codes

| Code | Meaning |
|------|---------|
| `MM_SUCCESS` | OK |
| `MM_ERROR` | Generic backend error |
| `MM_INVALID_ARG` | NULL pointer or bad argument |
| `MM_NO_BACKEND` | libasound not found (Linux only) |
| `MM_OUT_OF_RANGE` | Device index too large |
| `MM_ALREADY_OPEN` | Device already open |
| `MM_NOT_OPEN` | Device not open |
| `MM_ALLOC_FAILED` | Memory allocation failure |

```c
const char* mm_result_string(mm_result r);
```

---

## Configuration macros

| Macro | Default | Meaning |
|-------|---------|---------|
| `MM_MAX_PORTS` | 64 | Maximum enumerable ports |
| `MM_SYSEX_BUF_SIZE` | 4096 | Per-device sysex buffer (bytes) |
| `MM_ASSERT(x)` | `assert(x)` | Override assertion |

---

## Examples

| File | Client name | What it does |
|------|-------------|-------------|
| `examples/monitor.c` | `"midi-monitor"` | List ports, open input[N], print all messages |
| `examples/output.c` | `"midi-output"` | Open output[N], play a C major scale |
| `examples/through.c` | `"midi-through"` | Forward input[N] → output[N] in real time |
| `examples/daw_sync.c` | `"daw-sync"` | Clock, transport, SPP, MTC from a DAW |
| `examples/virtual.c` | `"my-synth"` | Virtual input — VMPK / DAW sends directly to us |

All examples accept a port index as a command-line argument:

```bash
./monitor 2          # open input[2]
./output_test 1      # open output[1]
./through 0 2        # input[0] → output[2]
./daw_sync 1         # open input[1]
```

---

## DAW compatibility

| DAW | Clock out | SPP | MTC |
|-----|-----------|-----|-----|
| Ableton Live | ✓ | ✓ | ✓ (separate port) |
| Logic Pro | ✓ | ✓ | ✓ |
| Reaper | ✓ | ✓ | ✓ |
| Bitwig | ✓ | ✓ | ✓ |
| FL Studio | ✓ | ✓ | partial |
| Cubase / Nuendo | ✓ | ✓ | ✓ |
| Pro Tools | ✓ | ✓ | ✓ (MTC primary) |

Enable MIDI clock output in your DAW's MIDI settings and route it to
the client name you registered with `mm_context_init`.

---

## Thread safety

`mm_context_*`, `mm_in_*`, `mm_out_*` should be called from one thread only.
The callback runs on a backend-managed background thread (CoreMIDI's run-loop
thread, WinMM's callback thread, or a `pthread` on Linux). Protect any shared
state with a mutex. `mm_out_send` / `mm_out_send_sysex` are safe to call from
the callback thread.

---

## Changelog

### v0.4.1 — bug fixes, no API changes
- **ALSA: switched from dlopen to `-lasound`**. All ALSA sequencer functions are
  inline wrappers in `<alsa/asoundlib.h>` and are not exported from `libasound.so`,
  making runtime symbol loading unworkable. Build now requires `-lasound -lpthread`
  and `libasound2-dev` headers (`apt install libasound2-dev` / `dnf install alsa-lib-devel`).
- **ALSA: fixed crash in port enumeration**. `snd_seq_client_info_malloc` and
  `snd_seq_port_info_malloc` are also inline-only. Replaced with
  `snd_seq_client_info_alloca` / `snd_seq_port_info_alloca` (stack allocation).
- **ALSA: fixed virtual port receive** — events from external subscribers were
  never delivered because `snd_seq_event_input_pending` was called with
  `fetch_sequencer=0`. Changed to `1` so the kernel ring is drained correctly.
- **ALSA: fixed compile error** — `snd_seq_ev_set_noteon` and related names are
  macros in `<alsa/seq_event.h>`. Using them as struct field names caused
  preprocessor expansion errors. All such calls are now made directly as the
  inline functions they are.

### v0.4.0
- `mm_in_open_virtual(ctx, dev, cb, ud)` — create a virtual MIDI destination.
  Other apps (VMPK, DAWs, Pure Data) see it in their output lists and connect freely.
- `mm_out_open_virtual(ctx, dev)` — create a virtual MIDI source.
  Other apps see it in their input lists; `mm_out_send` broadcasts to all subscribers.
- macOS: uses `MIDIDestinationCreate` / `MIDISourceCreate`. `mm_out_send` uses `MIDIReceived`.
- Linux: ALSA port with `CAP_SUBS_WRITE` / `CAP_SUBS_READ`. No explicit connect needed.
- Windows: returns `MM_NO_BACKEND` with guidance to use loopMIDI.
- `mm_device` gains `is_virtual` field. `mm_in_stop/close` and `mm_out_close` handle both cases.
- New example: `examples/virtual.c`.

### v0.3.0
- `mm_context_init(ctx, name)` — pass the client name your app registers under.
  `NULL` falls back to `"minimidio"`. Name stored in `ctx.name[64]`.
  Port names derived automatically as `"<name>-in"` / `"<name>-out"`.
- All examples updated: accept port index as CLI argument, print `ctx.name` on startup.

### v0.2.0
- Full DAW clock/transport support on all backends.
- New message types: `MM_MTC_QUARTER_FRAME`, `MM_SONG_POSITION`, `MM_SONG_SELECT`,
  `MM_TUNE_REQUEST`, `MM_ACTIVE_SENSE` (now all backends).
- New field: `mm_message.song_position`.
- MTC utilities: `mm_mtc_push`, `mm_mtc_to_seconds`, `mm_mtc_rate_string`.
- ALSA: replaced `usleep` busy-loop with `poll()` + wakeup pipe — zero added latency.
- ALSA: port enumeration catches DAW clock-only ports (no `SUBS_READ` cap).
- CoreMIDI: system-common block (0xF1–0xF6) parsed correctly.
- WinMM: `MIM_DATA` callback split into explicit real-time / system-common / channel paths.

### v0.1.0
- Initial release. CoreMIDI, WinMM, ALSA backends.
- Basic channel messages, SysEx, clock, start/stop/continue.

---

## License

MIT — see bottom of `minimidio.h`.
