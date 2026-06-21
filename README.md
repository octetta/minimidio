<img src="logo.png" width="200">

# minimidio

[![CodeFactor](https://www.codefactor.io/repository/github/octetta/minimidio/badge)](https://www.codefactor.io/repository/github/octetta/minimidio)

> Single-file, cross-platform C header library for MIDI input/output.
> Modelled after [miniaudio](https://miniaud.io/).

```c
#define MINIMIDIO_IMPLEMENTATION
#include "minimidio.h"
```

One file to copy into your project. Native builds use the OS MIDI libraries.
On Linux you need `libasound2-dev` for headers at build time and `libasound2`
at runtime (standard on any ALSA system). Web builds use the browser Web MIDI
API through Emscripten.

---

## Platform backends

| Platform | Backend | Link flag |
|----------|---------|-----------|
| macOS / iOS | CoreMIDI | `-framework CoreMIDI` |
| Windows (MSVC) | WinMM | automatic via `#pragma comment(lib, "winmm.lib")` |
| Windows (MinGW / Clang) | WinMM | `-lwinmm` |
| Linux | ALSA sequencer | `-lasound -lpthread` |
| Web / Emscripten | Web MIDI | `-sASYNCIFY` |

Raw byte input/output is available on all current backends through
`MM_CAP_RAW`. Raw virtual input follows virtual-port support, so it is
available on macOS and Linux but returns `MM_NO_BACKEND` on WinMM and Web MIDI.
MIDI 2.0 / UMP support is currently implemented for Linux ALSA builds with
new enough ALSA sequencer UMP headers/runtime. Other backends keep the MIDI
1.0 API and return `MM_NO_BACKEND` for raw UMP calls.

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

# Web / Emscripten — requires browser Web MIDI support, permission,
# and localhost/HTTPS
emcc my_app.c -sASYNCIFY -o my_app.html
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

**Web / Emscripten**: `mm_in_open_virtual` / `mm_out_open_virtual` return
`MM_NO_BACKEND`. The browser Web MIDI API can access user-approved MIDI
devices, but it cannot create OS-level virtual MIDI ports.

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
uint32_t  mm_context_caps  (mm_context* ctx);
```

`ctx.name` is a `char[64]` field accessible after init:

```c
printf("Running as: %s\n", ctx.name);
```

`mm_context_caps` returns a bitmask:

| Flag | Meaning |
|------|---------|
| `MM_CAP_MIDI1` | Existing `mm_message` MIDI 1.0 API |
| `MM_CAP_UMP` | Raw Universal MIDI Packet I/O |
| `MM_CAP_MIDI2` | Backend can opt into MIDI 2.0 UMP mode |
| `MM_CAP_VIRTUAL_IN` | Virtual MIDI input supported |
| `MM_CAP_VIRTUAL_OUT` | Virtual MIDI output supported |
| `MM_CAP_RAW` | Byte-transparent MIDI I/O |

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
mm_result mm_in_open_ump(mm_context* ctx, mm_device* dev, uint32_t idx,
                         mm_ump_callback cb, void* userdata);
mm_result mm_in_open_raw(mm_context* ctx, mm_device* dev, uint32_t idx,
                         mm_raw_callback cb, void* userdata);
mm_result mm_in_start (mm_device* dev);
mm_result mm_in_stop  (mm_device* dev);
mm_result mm_in_close (mm_device* dev);
mm_result mm_in_open_virtual_raw(mm_context* ctx, mm_device* dev,
                                 mm_raw_callback cb, void* userdata);
```

Callbacks arrive on a **background thread**. Do not call `mm_in_stop` or
`mm_in_close` from inside a callback.

Calling `mm_in_start` on an already-started input returns `MM_ALREADY_OPEN`.
Calling `mm_in_stop` on an open input that has not been started is harmless and
returns `MM_SUCCESS`.

### Output

```c
mm_result mm_out_open      (mm_context* ctx, mm_device* dev, uint32_t idx);
mm_result mm_out_send      (mm_device* dev, const mm_message* msg);
mm_result mm_out_send_ump  (mm_device* dev, const mm_ump_packet* pkt);
mm_result mm_out_send_sysex(mm_device* dev, const uint8_t* data, size_t size);
mm_result mm_out_send_raw  (mm_device* dev, const uint8_t* data, size_t len);
mm_result mm_out_close     (mm_device* dev);
```

### Raw MIDI bytes

Use the raw byte API when you need the original MIDI 1.0 wire bytes instead
of `mm_message` normalization:

```c
typedef void (*mm_raw_callback)(mm_device* dev,
                                const uint8_t* data, size_t len,
                                double timestamp, void* userdata);

if (mm_context_caps(&ctx) & MM_CAP_RAW) {
    mm_in_open_raw(&ctx, &dev, 0, on_raw, NULL);
    mm_in_start(&dev);
}

if (mm_context_caps(&ctx) & MM_CAP_VIRTUAL_IN) {
    mm_in_open_virtual_raw(&ctx, &virtual_in, on_raw, NULL);
}

uint8_t note_on[] = { 0x90, 60, 100 };
mm_out_send_raw(&out, note_on, sizeof(note_on));
```

`mm_in_open_raw` opens the same input indices as `mm_in_open`, but each
callback receives one complete byte message. It is byte-transparent: note-on
with velocity 0 is not folded into note-off, status bytes are not normalized,
large SysEx messages are delivered whole, and system real-time bytes are
delivered as their own one-byte callbacks even when interleaved with SysEx.
`mm_in_start`, `mm_in_stop`, and `mm_in_close` are shared with the normal input
path.

`mm_in_open_virtual_raw` is the byte-transparent twin of
`mm_in_open_virtual`: it creates a named MIDI destination that other apps send
into, then delivers those bytes to the raw callback. Like the normal virtual
input API, it returns `MM_NO_BACKEND` on WinMM and Web MIDI.

`mm_out_send_raw` sends the supplied bytes exactly as given through an opened
output or virtual output. There is no fixed 256-byte SysEx cap in minimidio's
raw path.

### MIDI 2.0 / UMP

The existing `mm_message` API remains MIDI 1.0-oriented. Raw MIDI 2.0
compatibility is additive through Universal MIDI Packets:

```c
typedef struct mm_ump_packet {
    uint32_t words[4];
    uint8_t  word_count;    /* 1..4 */
    double   timestamp;
} mm_ump_packet;

typedef void (*mm_ump_callback)(mm_device* dev,
                                const mm_ump_packet* pkt,
                                void* userdata);
```

On Linux with ALSA UMP support, `mm_in_open_ump` receives raw UMP packets and
`mm_out_send_ump` sends raw packets through an opened output. Unsupported
backends return `MM_NO_BACKEND`.

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
| `MM_NO_BACKEND` | Feature not supported by this backend |
| `MM_OUT_OF_RANGE` | Device index too large |
| `MM_ALREADY_OPEN` | Device already open or input already started |
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
| `MM_WEBMIDI_ENABLE_SYSEX` | 0 | Request browser SysEx permission on Web MIDI |
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
| `examples/ump_monitor.c` | `"ump-monitor"` | Raw UMP input monitor for ALSA MIDI 2.0 |
| `examples/web_monitor.c` | `"web-midi-monitor"` | Web MIDI input monitor for Emscripten |
| `examples/web_sender.c` | `"web-midi-test-source"` | Native virtual source for testing Web MIDI input |

Most terminal examples accept a port index as a command-line argument:

```bash
./monitor 2          # open input[2]
./output_test 1      # open output[1]
./through 0 2        # input[0] → output[2]
./daw_sync 1         # open input[1]
./ump_monitor 1      # open input[1] for raw UMP
./web_sender 0       # emit Web MIDI test events until Ctrl-C
```

The Web MIDI example opens `web-midi-test-source` when present, otherwise
input[0], after browser permission is granted:

```bash
emcc examples/web_monitor.c -sASYNCIFY -o web_monitor.html
```

To test Web MIDI without physical MIDI hardware on Linux or macOS, build both
the browser monitor and the native virtual source:

```bash
emcc examples/web_monitor.c -sASYNCIFY -o web_monitor.html

# Linux
cc examples/web_sender.c -lasound -lpthread -o web_sender

# macOS
cc examples/web_sender.c -framework CoreMIDI -o web_sender
```

In one terminal, serve the Web build from `localhost`:

```bash
python3 -m http.server 8000
```

In another terminal, start the native source:

```bash
./web_sender
```

Then open <http://localhost:8000/web_monitor.html> in a browser with Web MIDI
support.

When the browser asks for MIDI permission, allow it. The Web MIDI input list
should include `web-midi-test-source`; press Enter in `web_sender` to emit
Note, CC, pitch bend, song position, clock, start, and stop messages.

On Linux, sandboxed browser packages can hide ALSA sequencer MIDI devices from
Web MIDI. If `aconnect -l` shows `web-midi-test-source` but the browser lists
no MIDI inputs, try a non-Flatpak/non-Snap browser or grant the browser device
access, for example:

```bash
flatpak override --user --device=all com.google.Chrome
flatpak kill com.google.Chrome
```

On Windows, WinMM cannot create virtual MIDI ports, so `web_sender` returns
`MM_NO_BACKEND`. For hardware-free Web MIDI testing, install loopMIDI, create a
virtual cable, open `web_monitor.html`, then send to the loopMIDI output with a
normal output example:

```bat
cl examples\output.c
output.exe 0
```

Use the output index that corresponds to your loopMIDI cable.

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
thread, WinMM's callback thread, a `pthread` on Linux, or the browser event
loop on Web MIDI). Protect any shared state with a mutex when using native
threads. `mm_out_send`, `mm_out_send_sysex`, and `mm_out_send_raw` are safe to
call from the callback thread.

---

## Web MIDI / Emscripten

When compiling with Emscripten, minimidio uses the browser Web MIDI API.
Build with Asyncify because `mm_context_init` requests MIDI access through the
browser's asynchronous permission flow:

```bash
emcc examples/web_monitor.c -sASYNCIFY -o web_monitor.html
```

Serve the page from `localhost` or HTTPS. Browser support is not universal, and
users may deny MIDI permission. SysEx is disabled by default; opt in before
including the header if your app needs it:

```c
#define MM_WEBMIDI_ENABLE_SYSEX 1
#define MINIMIDIO_IMPLEMENTATION
#include "minimidio.h"
```

Virtual MIDI ports are not available in browsers, so the virtual-port APIs
return `MM_NO_BACKEND` on Web MIDI.

For hardware-free browser testing on Linux or macOS, use
`examples/web_sender.c`. It creates a native virtual source named
`web-midi-test-source`; `examples/web_monitor.c` automatically opens that
source when the browser exposes it. On Windows, use loopMIDI or another
virtual MIDI cable and send to it with `examples/output.c`.

---

## MIDI 2.0 / UMP

MIDI 2.0 compatibility is opt-in and additive. Existing `mm_in_open`,
`mm_out_send`, and `mm_message` code continues to use the MIDI 1.0-style API.
Applications that need raw Universal MIDI Packets can check capabilities and
open a UMP input:

```c
if (mm_context_caps(&ctx) & MM_CAP_UMP) {
    mm_in_open_ump(&ctx, &dev, 0, on_ump, NULL);
    mm_in_start(&dev);
}
```

Linux/ALSA is the first implemented UMP backend. It requires ALSA sequencer
UMP support in the installed headers/runtime. macOS CoreMIDI has MIDI 2.0
support at the platform level, but minimidio's CoreMIDI backend does not yet
expose raw UMP. Windows requires a future Windows MIDI Services backend; WinMM
cannot carry real MIDI 2.0 UMP. Web MIDI is currently byte-message oriented,
so raw UMP calls return `MM_NO_BACKEND` there.

---

## Changelog

### v0.5.0-dev — MIDI 2.0 / UMP compatibility branch
- **API: added raw UMP support** via `mm_ump_packet`, `mm_ump_callback`,
  `mm_in_open_ump`, and `mm_out_send_ump`.
- **API: added raw byte-transparent MIDI support** via `MM_CAP_RAW`,
  `mm_raw_callback`, `mm_in_open_raw`, `mm_in_open_virtual_raw`, and
  `mm_out_send_raw`.
- **API: added `mm_context_caps`** for backend feature discovery.
- **ALSA: added MIDI 2.0/UMP path** using ALSA sequencer UMP APIs when
  available. Unsupported ALSA builds return `MM_NO_BACKEND` for UMP calls.
- **Examples: added `examples/raw_monitor.c` and `examples/ump_monitor.c`**
  for raw byte and raw UMP input monitoring.
- **Compatibility: existing MIDI 1.0 APIs remain unchanged**.

### v0.4.1 — bug fixes, no API changes
- **Web / Emscripten: added Web MIDI backend**. Normal input/output works
  through the browser Web MIDI API when built with `-sASYNCIFY`. SysEx is
  opt-in via `MM_WEBMIDI_ENABLE_SYSEX`; virtual ports return `MM_NO_BACKEND`.
- **Web / Emscripten: fixed compatibility with newer Emscripten glue**.
  The backend no longer depends on omitted `stringToUTF8` or
  `getWasmTableEntry` helpers.
- **Examples: added `examples/web_sender.c`**. Native virtual MIDI source for
  testing the Web MIDI monitor without physical hardware.
- **All backends: tightened argument validation** for port-name buffers and
  unsupported output message types.
- **ALSA: fixed input start/stop lifecycle**. Stop-before-start is harmless,
  double-start returns `MM_ALREADY_OPEN`, and thread creation failures clean up.
- **ALSA: fixed output parity** for poly pressure and channel pressure.
- **ALSA: fixed exact-size SysEx receive buffering** at `MM_SYSEX_BUF_SIZE`.
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

MIT — see `LICENSE` and the bottom of `minimidio.h`.

See `AUTHORSHIP.md` for the project authorship and AI-assisted development
process statement.
