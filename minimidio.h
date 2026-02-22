/*
minimidio.h - v0.4.1 - Single-file cross-platform MIDI input/output library

CHANGES v0.4.1
  Bug fixes — no API changes.

  ALSA:
    - Dropped dlopen/dlsym approach. All ALSA sequencer functions are inline
      wrappers in <alsa/asoundlib.h> and are not exported from libasound.so,
      so runtime symbol loading was never viable. The backend now links
      directly: compile with -lasound -lpthread.
      Install headers with: apt install libasound2-dev  (or dnf install alsa-lib-devel)
    - Fixed crash in port enumeration (mm_out_count, mm_in_count, mm_in_open,
      mm_out_open): snd_seq_client_info_malloc and snd_seq_port_info_malloc are
      also inline-only. Replaced with snd_seq_client_info_alloca /
      snd_seq_port_info_alloca (stack allocation) and the corresponding inline
      set/get calls. No heap allocation in enumeration.
    - Fixed virtual port receive: snd_seq_event_input_pending() was called with
      fetch_sequencer=0, so events from external subscribers sat in the kernel
      ring and were never drained. Changed to fetch_sequencer=1.
    - Fixed ALSA name-collision bug: struct fields and call sites for
      snd_seq_ev_set_noteon, snd_seq_ev_set_direct, etc. conflicted with macros
      of the same name defined in <alsa/seq_event.h>, producing compile errors.
      These are now called directly as inline functions (no struct field needed).

CHANGES v0.4.0
  Virtual port support — other apps (VMPK, DAWs, Pure Data, etc.) can now
  connect freely to your process without any manual patching:

    mm_in_open_virtual (&ctx, &dev, cb, ud)  // we become a MIDI destination
    mm_out_open_virtual(&ctx, &dev)           // we become a MIDI source

  macOS/CoreMIDI:
    mm_in_open_virtual  → MIDIDestinationCreate   (appears in every app's output list)
    mm_out_open_virtual → MIDISourceCreate         (appears in every app's input list)
    mm_out_send / mm_out_send_sysex use MIDIReceived() to broadcast to subscribers.

  Linux/ALSA:
    Virtual ports use CAP_SUBS_WRITE (input) / CAP_SUBS_READ (output) so
    aconnect, qjackctl, VMPK, Pd, DAWs can freely subscribe. No explicit
    snd_seq_connect_from/to call — apps wire themselves.

  Windows/WinMM:
    Returns MM_NO_BACKEND. WinMM has no virtual port API.
    Workaround: install loopMIDI (https://www.tobias-erichsen.de/software/loopmidi.html),
    create a virtual cable there, then use mm_in_open / mm_out_open with that port.

  mm_device gains an is_virtual field (int). mm_in_start/stop/close and
  mm_out_close all branch correctly for virtual vs. normal devices.

CHANGES v0.3.0
  mm_context_init() now takes a name parameter:

    mm_context_init(&ctx, "my-synth");
    mm_context_init(&ctx, NULL);        // uses "minimidio"

  The name is what other MIDI software sees when enumerating clients:
    macOS:  shown in Audio MIDI Setup and any CoreMIDI-using app
    Linux:  shown in aconnect -l, qjackctl, Ardour, etc.
    Windows: accepted and stored but unused (WinMM has no client-name concept)

  Port names are derived automatically: "<name>-in" and "<name>-out".

CHANGES v0.2.0
  Full DAW clock & transport support on all three backends:

  NEW MESSAGE TYPES:
    MM_MTC_QUARTER_FRAME  (0xF1) MTC quarter-frame nibbles; accumulate 8 with
                                 mm_mtc_push() to decode a full SMPTE frame.
    MM_SONG_POSITION      (0xF2) Song Position Pointer; decoded into
                                 msg->song_position (14-bit beat count).
    MM_SONG_SELECT        (0xF3) Song number in data[0].
    MM_TUNE_REQUEST       (0xF6) Single-byte tune request.
    MM_ACTIVE_SENSE       (0xFE) Now dispatched on ALL backends (was macOS-only).

  NEW FIELD: mm_message.song_position  (uint16_t, valid for MM_SONG_POSITION)

  NEW UTILITIES (header-only, always available):
    mm_mtc_push()          accumulate quarter-frames; returns 1 + decoded frame
                           when 8 have been received.
    mm_mtc_to_seconds()    convert mm_mtc_frame to wall-clock seconds.
    mm_mtc_rate_string()   human-readable frame-rate string.

  ALSA FIXES:
    - Replaced usleep(500) busy-loop with poll() on sequencer file
      descriptors + a wakeup pipe → zero added latency on clock ticks.
    - Added snd_seq_poll_descriptors_count / snd_seq_poll_descriptors.
    - Port enumeration now accepts ports with only SND_SEQ_PORT_CAP_READ
      (no SUBS_READ) so DAW clock-only ports are visible.
    - SND_SEQ_EVENT_SONGPOS, SND_SEQ_EVENT_QFRAME, SND_SEQ_EVENT_SONGSEL,
      SND_SEQ_EVENT_SENSING, SND_SEQ_EVENT_TUNE_REQUEST, SND_SEQ_EVENT_RESET,
      SND_SEQ_EVENT_KEYPRESS (poly pressure) all dispatched.

  COREMIDI FIXES:
    - System-common block (0xF1–0xF6) parsed correctly; previously fell through.
    - Real-time block now dispatches Active Sense and is strict about undefined
      status bytes (0xF4, 0xF5, 0xF9, 0xFD) — they are skipped cleanly.
    - mm_out_send() handles all new message types.

  WINMM FIXES:
    - MIM_DATA callback now decodes 0xF1/0xF2/0xF3/0xF6/0xF8–0xFF explicitly
      instead of delegating to mm_make_message (which lost system-common info).
    - mm_out_send() handles all new message types via midiOutShortMsg packing.

ABOUT
  minimidio is modelled after miniaudio: a single C header that gives you MIDI
  input and output on macOS, Windows, and Linux with no external dependencies
  beyond what the OS already ships.

  Platform backends:
    macOS / iOS  : CoreMIDI   (CoreMIDI.framework  — always present)
    Windows      : WinMM      (winmm.dll            — always present)
    Linux        : ALSA seq   (libasound            — link with -lasound)

LICENSE
  MIT — see end of file.

QUICK START

    #define MINIMIDIO_IMPLEMENTATION
    #include "minimidio.h"

    void my_callback(mm_device* dev, const mm_message* msg, void* userdata) {
        switch (msg->type) {
            case MM_CLOCK:
                // fires 24× per beat from a DAW – count 24 for one beat
                break;
            case MM_START:
                // DAW pressed Play from bar 1
                break;
            case MM_CONTINUE:
                // DAW resumed from current position
                break;
            case MM_STOP:
                // DAW stopped
                break;
            case MM_SONG_POSITION:
                // DAW scrubbed / rewound; beat = msg->song_position
                // quarter-note = msg->song_position / 4.0
                break;
            case MM_MTC_QUARTER_FRAME: {
                // Accumulate 8 quarter-frames to get a SMPTE timecode frame:
                static mm_mtc_state mtc;
                mm_mtc_frame frame;
                if (mm_mtc_push(&mtc, msg->data[0], &frame))
                    printf("MTC %02d:%02d:%02d.%02d %s\n",
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
        mm_context_init(&ctx, "my-app");   // "my-app" visible in aconnect, AMS, etc.
        // or: mm_context_init(&ctx, NULL) to use default "minimidio"
        for (uint32_t i = 0; i < mm_in_count(&ctx); i++) {
            char name[128];
            mm_in_name(&ctx, i, name, sizeof(name));
            printf("[%u] %s\n", i, name);
        }
        mm_device dev;
        mm_in_open(&ctx, &dev, 0, my_callback, NULL);
        mm_in_start(&dev);
        // ... event loop / sleep ...
        mm_in_stop(&dev);
        mm_in_close(&dev);
        mm_context_uninit(&ctx);
    }

SONG POSITION MATHS

    // msg->song_position is in MIDI beats (1 beat = 6 clocks = one 16th note)
    double quarter_notes = msg->song_position / 4.0;
    double bars          = quarter_notes / time_sig_numerator;

CONFIGURATION DEFINES (before #include)

    #define MM_MAX_PORTS          64   // max enumerable ports
    #define MM_SYSEX_BUF_SIZE  4096   // per-device sysex buffer (bytes)
    #define MM_ASSERT(x)              // override assertion macro
*/

#ifndef MINIMIDIO_H
#define MINIMIDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ── Configuration ─────────────────────────────────────────────────────────── */

#ifndef MM_MAX_PORTS
#  define MM_MAX_PORTS 64
#endif
#ifndef MM_SYSEX_BUF_SIZE
#  define MM_SYSEX_BUF_SIZE 4096
#endif
#ifndef MM_ASSERT
#  include <assert.h>
#  define MM_ASSERT(x) assert(x)
#endif

/* ── Result codes ───────────────────────────────────────────────────────────── */

typedef enum mm_result {
    MM_SUCCESS      =  0,
    MM_ERROR        = -1,
    MM_INVALID_ARG  = -2,
    MM_NO_BACKEND   = -3,
    MM_OUT_OF_RANGE = -4,
    MM_ALREADY_OPEN = -5,
    MM_NOT_OPEN     = -6,
    MM_ALLOC_FAILED = -7,
} mm_result;

/* ── Message types ──────────────────────────────────────────────────────────── */

typedef enum mm_message_type {
    /* Channel messages — type = (status >> 4) & 0x0F */
    MM_NOTE_OFF             = 0x08,   /* 0x8n */
    MM_NOTE_ON              = 0x09,   /* 0x9n */
    MM_POLY_PRESSURE        = 0x0A,   /* 0xAn */
    MM_CONTROL_CHANGE       = 0x0B,   /* 0xBn */
    MM_PROGRAM_CHANGE       = 0x0C,   /* 0xCn */
    MM_CHANNEL_PRESSURE     = 0x0D,   /* 0xDn */
    MM_PITCH_BEND           = 0x0E,   /* 0xEn */

    /* System common — unique values that don't clash with channel messages */
    MM_SYSEX                = 0x10,   /* 0xF0 … 0xF7 */
    MM_MTC_QUARTER_FRAME    = 0x11,   /* 0xF1  data[0] = type+nibble byte */
    MM_SONG_POSITION        = 0x12,   /* 0xF2  song_position = 14-bit beat */
    MM_SONG_SELECT          = 0x13,   /* 0xF3  data[0] = song number */
    MM_TUNE_REQUEST         = 0x14,   /* 0xF6  no data */

    /* System real-time */
    MM_CLOCK                = 0x18,   /* 0xF8  24 pulses per quarter note */
    MM_START                = 0x1A,   /* 0xFA */
    MM_CONTINUE             = 0x1B,   /* 0xFB */
    MM_STOP                 = 0x1C,   /* 0xFC */
    MM_ACTIVE_SENSE         = 0x1E,   /* 0xFE  300ms keepalive from DAW */
    MM_RESET                = 0x1F,   /* 0xFF */
} mm_message_type;

/* ── MTC timecode ───────────────────────────────────────────────────────────── */

typedef enum mm_mtc_rate {
    MM_MTC_24FPS      = 0,
    MM_MTC_25FPS      = 1,
    MM_MTC_30FPS_DROP = 2,   /* 29.97 drop-frame */
    MM_MTC_30FPS      = 3,
} mm_mtc_rate;

typedef struct mm_mtc_frame {
    uint8_t     hours;
    uint8_t     minutes;
    uint8_t     seconds;
    uint8_t     frames;
    mm_mtc_rate rate;
} mm_mtc_frame;

/* Keep one per input device, zero-initialise before first use */
typedef struct mm_mtc_state {
    uint8_t pieces[8];   /* quarter-frame nibbles indexed by type nibble */
    uint8_t count;       /* how many pieces received so far (0–7) */
} mm_mtc_state;

/* ── MIDI message ───────────────────────────────────────────────────────────── */

typedef struct mm_message {
    mm_message_type type;

    uint8_t  channel;       /* channel messages: 0–15                       */
    uint8_t  data[2];       /* note/cc/vel/value etc.                        */
    double   timestamp;     /* seconds since device opened                   */

    /* MM_SONG_POSITION only:
       14-bit beat count (1 beat = 6 MIDI clocks = one 16th note).
       Quarter notes = song_position / 4.0                                   */
    uint16_t song_position;

    /* MM_SYSEX only */
    const uint8_t* sysex;
    size_t         sysex_size;
} mm_message;

/* ── Forward declarations ───────────────────────────────────────────────────── */

typedef struct mm_context mm_context;
typedef struct mm_device  mm_device;

/* Called from a background thread. Do NOT call mm_in_stop/close from within. */
typedef void (*mm_callback)(mm_device* dev, const mm_message* msg, void* userdata);

/* ══════════════════════════════════════════════════════════════════════════════
   MTC utilities — header-only, always available
   ══════════════════════════════════════════════════════════════════════════ */

/*  Push one MTC quarter-frame byte (the raw data[0] from MM_MTC_QUARTER_FRAME).
    Returns 1 and fills *out when all 8 pieces have been collected.
    Returns 0 otherwise. State persists between calls; reset with memset(s,0).  */
static inline int mm_mtc_push(mm_mtc_state* s, uint8_t qf, mm_mtc_frame* out)
{
    uint8_t nibble = qf & 0x0F;
    uint8_t piece  = (qf >> 4) & 0x07;  /* 0–7: which field */
    s->pieces[piece] = nibble;
    if (++s->count < 8) return 0;
    s->count = 0;
    /* piece layout:
       0 = frames  LSN   1 = frames  MSN (bit 4 only)
       2 = seconds LSN   3 = seconds MSN
       4 = minutes LSN   5 = minutes MSN
       6 = hours   LSN   7 = hours   MSN (bit 0) + rate (bits 1-2)          */
    out->frames  = (uint8_t)( s->pieces[0] | (s->pieces[1] << 4));
    out->seconds = (uint8_t)( s->pieces[2] | (s->pieces[3] << 4));
    out->minutes = (uint8_t)( s->pieces[4] | (s->pieces[5] << 4));
    out->hours   = (uint8_t)( s->pieces[6] | ((s->pieces[7] & 0x01) << 4));
    out->rate    = (mm_mtc_rate)((s->pieces[7] >> 1) & 0x03);
    return 1;
}

static inline const char* mm_mtc_rate_string(mm_mtc_rate r)
{
    switch (r) {
        case MM_MTC_24FPS:      return "24fps";
        case MM_MTC_25FPS:      return "25fps";
        case MM_MTC_30FPS_DROP: return "29.97fps (drop)";
        case MM_MTC_30FPS:      return "30fps";
        default:                return "unknown";
    }
}

/* Convert a decoded MTC frame to seconds from midnight. */
static inline double mm_mtc_to_seconds(const mm_mtc_frame* f)
{
    static const double rates[] = { 24.0, 25.0, 29.97, 30.0 };
    double fps = rates[f->rate & 3];
    return (double)f->hours   * 3600.0
         + (double)f->minutes *   60.0
         + (double)f->seconds
         + (double)f->frames  / fps;
}

/* Helper: pack raw status + 2 data bytes into an mm_message */
static inline mm_message mm_make_message(uint8_t status, uint8_t d1, uint8_t d2)
{
    mm_message m;
    memset(&m, 0, sizeof(m));
    m.type    = (mm_message_type)((status >> 4) & 0x0F);
    m.channel = status & 0x0F;
    m.data[0] = d1;
    m.data[1] = d2;
    return m;
}

/* ══════════════════════════════════════════════════════════════════════════════
   Platform detection
   ══════════════════════════════════════════════════════════════════════════ */

#if defined(__APPLE__)
#  define MM_BACKEND_COREMIDI
#elif defined(_WIN32)
#  define MM_BACKEND_WINMM
#elif defined(__linux__)
#  define MM_BACKEND_ALSA
#else
#  error "minimidio: unsupported platform"
#endif

/* ══════════════════════════════════════════════════════════════════════════════
   Backend-private structs (not for user code)
   ══════════════════════════════════════════════════════════════════════════ */

#if defined(MM_BACKEND_COREMIDI)
#  include <CoreMIDI/CoreMIDI.h>

typedef struct { MIDIClientRef client; } mm__ctx_coremidi;

typedef struct {
    MIDIPortRef          port;       /* non-virtual: the port we created     */
    MIDIEndpointRef      endpoint;   /* non-virtual: the hardware endpoint   */
    MIDIEndpointRef      virt_ep;    /* virtual: the endpoint we OWN        */
    MIDISysexSendRequest sysex_req;
    uint8_t              sysex_buf[MM_SYSEX_BUF_SIZE];
} mm__dev_coremidi;

#elif defined(MM_BACKEND_WINMM)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmsystem.h>
#  pragma comment(lib, "winmm.lib")

typedef struct { int dummy; } mm__ctx_winmm;

typedef struct {
    HMIDIIN  in;
    HMIDIOUT out;
    MIDIHDR  sysex_hdr;
    uint8_t  sysex_buf[MM_SYSEX_BUF_SIZE];
} mm__dev_winmm;

#elif defined(MM_BACKEND_ALSA)
#  include <alsa/asoundlib.h>
#  include <pthread.h>
#  include <poll.h>

/* ALSA sequencer API: all functions are inline wrappers in <alsa/asoundlib.h>
   over internal primitives — none are directly dlsym-able.
   We link -lasound directly, the same way macOS links -framework CoreMIDI.   */

typedef struct mm__ctx_alsa {
    snd_seq_t* seq;
    int        client_id;
} mm__ctx_alsa;

typedef struct mm__dev_alsa {
    int            port_id;
    int            target_client;
    int            target_port;
    pthread_t      thread;
    volatile int   running;
    int            wake_pipe[2];   /* [0]=read [1]=write, used to unblock poll() */
    uint8_t        sysex_buf[MM_SYSEX_BUF_SIZE];
    size_t         sysex_pos;
} mm__dev_alsa;

#endif /* backends */

/* ══════════════════════════════════════════════════════════════════════════════
   Public structs
   ══════════════════════════════════════════════════════════════════════════ */

struct mm_context {
#if defined(MM_BACKEND_COREMIDI)
    mm__ctx_coremidi cm;
#elif defined(MM_BACKEND_WINMM)
    mm__ctx_winmm    wm;
#elif defined(MM_BACKEND_ALSA)
    mm__ctx_alsa     al;
#endif
    int  initialized;
    char name[64];   /* app name shown to other MIDI clients (CoreMIDI, ALSA) */
};

struct mm_device {
    mm_context* ctx;
    mm_callback callback;
    void*       userdata;
    int         is_input;
    int         is_open;
    int         is_virtual;  /* 1 = opened with mm_in/out_open_virtual */
#if defined(MM_BACKEND_COREMIDI)
    mm__dev_coremidi cm;
#elif defined(MM_BACKEND_WINMM)
    mm__dev_winmm    wm;
#elif defined(MM_BACKEND_ALSA)
    mm__dev_alsa     al;
#endif
};

/* ══════════════════════════════════════════════════════════════════════════════
   Public API declarations
   ══════════════════════════════════════════════════════════════════════════ */

/* name: the string other MIDI apps will see for this client, e.g. "my-synth".
   Pass NULL to use the default "minimidio".
   On Windows/WinMM this parameter is accepted but has no effect
   (WinMM has no client-name concept; you are always identified by
   the hardware port you opened).                                              */
mm_result   mm_context_init  (mm_context* ctx, const char* name);
mm_result   mm_context_uninit(mm_context* ctx);

uint32_t    mm_in_count (mm_context* ctx);
mm_result   mm_in_name  (mm_context* ctx, uint32_t idx, char* buf, size_t bufsz);
uint32_t    mm_out_count(mm_context* ctx);
mm_result   mm_out_name (mm_context* ctx, uint32_t idx, char* buf, size_t bufsz);

mm_result   mm_in_open  (mm_context* ctx, mm_device* dev, uint32_t idx,
                         mm_callback cb, void* userdata);
mm_result   mm_in_start (mm_device* dev);
mm_result   mm_in_stop  (mm_device* dev);
mm_result   mm_in_close (mm_device* dev);

/* Virtual input: creates a named destination that OTHER apps can connect to
   and send MIDI into. VMPK, DAWs, etc. will see it in their output lists.
   mm_in_start / mm_in_stop / mm_in_close work identically to the normal path.
   On Windows/WinMM returns MM_NO_BACKEND (WinMM has no virtual port API;
   use loopMIDI to create a virtual cable instead).                           */
mm_result   mm_in_open_virtual(mm_context* ctx, mm_device* dev,
                                mm_callback cb, void* userdata);

mm_result   mm_out_open      (mm_context* ctx, mm_device* dev, uint32_t idx);
mm_result   mm_out_send      (mm_device* dev, const mm_message* msg);
mm_result   mm_out_send_sysex(mm_device* dev, const uint8_t* data, size_t size);
mm_result   mm_out_close     (mm_device* dev);

/* Virtual output: creates a named source that OTHER apps can read from.
   Use mm_out_send / mm_out_send_sysex to push messages out to subscribers.
   On Windows/WinMM returns MM_NO_BACKEND (see note above).                  */
mm_result   mm_out_open_virtual(mm_context* ctx, mm_device* dev);

const char* mm_result_string(mm_result r);

/* ══════════════════════════════════════════════════════════════════════════════
   IMPLEMENTATION
   ══════════════════════════════════════════════════════════════════════════ */

#ifdef MINIMIDIO_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>

static const char* mm__result_strings[] = {
    "MM_SUCCESS","MM_ERROR","MM_INVALID_ARG","MM_NO_BACKEND",
    "MM_OUT_OF_RANGE","MM_ALREADY_OPEN","MM_NOT_OPEN","MM_ALLOC_FAILED",
};
const char* mm_result_string(mm_result r) {
    int i = -(int)r;
    if (i < 0 || i >= (int)(sizeof(mm__result_strings)/sizeof(*mm__result_strings)))
        return "MM_UNKNOWN";
    return mm__result_strings[i];
}

/* ─────────────────────────────────────────────────────────────────────────────
   CoreMIDI (macOS / iOS)
   ───────────────────────────────────────────────────────────────────────── */
#if defined(MM_BACKEND_COREMIDI)

#include <mach/mach_time.h>

static double mm__cm_ts(MIDITimeStamp ts) {
    static mach_timebase_info_data_t tb; static int init=0;
    if (!init) { mach_timebase_info(&tb); init=1; }
    return (double)ts * tb.numer / tb.denom * 1e-9;
}

static void mm__cm_read_proc(const MIDIPacketList* pl, void* ref, void* src)
{
    mm_device* dev = (mm_device*)ref; (void)src;
    if (!dev || !dev->callback) return;

    const MIDIPacket* pkt = &pl->packet[0];
    for (UInt32 i = 0; i < pl->numPackets; i++) {
        size_t j = 0;
        while (j < pkt->length) {
            uint8_t s  = pkt->data[j];
            double  ts = mm__cm_ts(pkt->timeStamp);
            mm_message msg; memset(&msg, 0, sizeof(msg)); msg.timestamp = ts;

            /* System real-time — single byte, may appear mid-packet */
            if (s >= 0xF8) {
                switch (s) {
                    case 0xF8: msg.type = MM_CLOCK;        break;
                    case 0xFA: msg.type = MM_START;        break;
                    case 0xFB: msg.type = MM_CONTINUE;     break;
                    case 0xFC: msg.type = MM_STOP;         break;
                    case 0xFE: msg.type = MM_ACTIVE_SENSE; break;
                    case 0xFF: msg.type = MM_RESET;        break;
                    default:   j++; continue; /* 0xF4/F5/F9/FD undefined */
                }
                dev->callback(dev, &msg, dev->userdata); j++; continue;
            }

            /* SysEx */
            if (s == 0xF0) {
                size_t start = j;
                while (j < pkt->length && pkt->data[j] != 0xF7) j++;
                if (j < pkt->length) j++;
                msg.type = MM_SYSEX; msg.sysex = &pkt->data[start];
                msg.sysex_size = j - start;
                dev->callback(dev, &msg, dev->userdata); continue;
            }

            /* System common 0xF1–0xF6 */
            if (s >= 0xF1 && s <= 0xF6) {
                j++;
                switch (s) {
                    case 0xF1:
                        msg.type = MM_MTC_QUARTER_FRAME;
                        if (j < pkt->length) msg.data[0] = pkt->data[j++];
                        dev->callback(dev, &msg, dev->userdata); break;
                    case 0xF2:
                        msg.type = MM_SONG_POSITION;
                        if (j + 1 < pkt->length) {
                            uint8_t lsb = pkt->data[j++];
                            uint8_t msb = pkt->data[j++];
                            msg.song_position = (uint16_t)(lsb | ((uint16_t)msb << 7));
                            msg.data[0] = lsb; msg.data[1] = msb;
                        }
                        dev->callback(dev, &msg, dev->userdata); break;
                    case 0xF3:
                        msg.type = MM_SONG_SELECT;
                        if (j < pkt->length) msg.data[0] = pkt->data[j++];
                        dev->callback(dev, &msg, dev->userdata); break;
                    case 0xF6:
                        msg.type = MM_TUNE_REQUEST;
                        dev->callback(dev, &msg, dev->userdata); break;
                    default: break; /* 0xF4, 0xF5 undefined */
                }
                continue;
            }

            /* Channel messages 0x80–0xEF */
            if (s >= 0x80) {
                msg.type    = (mm_message_type)((s >> 4) & 0x0F);
                msg.channel = s & 0x0F; j++;
                if (j < pkt->length) msg.data[0] = pkt->data[j++];
                switch (msg.type) {
                    case MM_NOTE_OFF: case MM_NOTE_ON: case MM_POLY_PRESSURE:
                    case MM_CONTROL_CHANGE: case MM_PITCH_BEND:
                        if (j < pkt->length) msg.data[1] = pkt->data[j++]; break;
                    default: break;
                }
                dev->callback(dev, &msg, dev->userdata); continue;
            }
            j++; /* running status byte / unknown — skip */
        }
        pkt = MIDIPacketNext(pkt);
    }
}

mm_result mm_context_init(mm_context* ctx, const char* name) {
    if (!ctx) return MM_INVALID_ARG;
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->name, (name && name[0]) ? name : "minimidio", sizeof(ctx->name)-1);
    CFStringRef cfname = CFStringCreateWithCString(NULL, ctx->name, kCFStringEncodingUTF8);
    OSStatus st = MIDIClientCreate(cfname, NULL, NULL, &ctx->cm.client);
    CFRelease(cfname);
    if (st != noErr) return MM_ERROR;
    ctx->initialized = 1; return MM_SUCCESS;
}
mm_result mm_context_uninit(mm_context* ctx) {
    if (!ctx || !ctx->initialized) return MM_INVALID_ARG;
    MIDIClientDispose(ctx->cm.client); ctx->initialized = 0; return MM_SUCCESS;
}

uint32_t mm_in_count (mm_context* ctx) { (void)ctx; return (uint32_t)MIDIGetNumberOfSources();      }
uint32_t mm_out_count(mm_context* ctx) { (void)ctx; return (uint32_t)MIDIGetNumberOfDestinations();  }

static mm_result mm__cm_name(MIDIEndpointRef ep, char* buf, size_t sz) {
    CFStringRef n = NULL;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &n);
    if (!n) MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &n);
    if (!n) { snprintf(buf, sz, "(unknown)"); return MM_ERROR; }
    CFStringGetCString(n, buf, (CFIndex)sz, kCFStringEncodingUTF8);
    CFRelease(n); return MM_SUCCESS;
}
mm_result mm_in_name(mm_context* ctx, uint32_t idx, char* buf, size_t sz) {
    (void)ctx;
    if (idx >= MIDIGetNumberOfSources()) return MM_OUT_OF_RANGE;
    return mm__cm_name(MIDIGetSource(idx), buf, sz);
}
mm_result mm_out_name(mm_context* ctx, uint32_t idx, char* buf, size_t sz) {
    (void)ctx;
    if (idx >= MIDIGetNumberOfDestinations()) return MM_OUT_OF_RANGE;
    return mm__cm_name(MIDIGetDestination(idx), buf, sz);
}

mm_result mm_in_open(mm_context* ctx, mm_device* dev, uint32_t idx,
                     mm_callback cb, void* ud)
{
    if (!ctx||!dev||!cb) return MM_INVALID_ARG;
    if (idx >= MIDIGetNumberOfSources()) return MM_OUT_OF_RANGE;
    memset(dev, 0, sizeof(*dev));
    dev->ctx=ctx; dev->callback=cb; dev->userdata=ud; dev->is_input=1;
    dev->cm.endpoint = MIDIGetSource(idx);
    char portname[80]; snprintf(portname, sizeof(portname), "%s-in", ctx->name);
    CFStringRef cfport = CFStringCreateWithCString(NULL, portname, kCFStringEncodingUTF8);
    OSStatus st = MIDIInputPortCreate(ctx->cm.client, cfport,
                                      mm__cm_read_proc, dev, &dev->cm.port);
    CFRelease(cfport);
    if (st != noErr) return MM_ERROR;
    dev->is_open=1; return MM_SUCCESS;
}
mm_result mm_in_start(mm_device* dev) {
    if (!dev||!dev->is_open||!dev->is_input) return MM_NOT_OPEN;
    if (dev->is_virtual) return MM_SUCCESS; /* CoreMIDI: other apps connect to us */
    return (MIDIPortConnectSource(dev->cm.port, dev->cm.endpoint, NULL) == noErr)
           ? MM_SUCCESS : MM_ERROR;
}
mm_result mm_in_stop(mm_device* dev) {
    if (!dev||!dev->is_open||!dev->is_input) return MM_NOT_OPEN;
    if (dev->is_virtual) return MM_SUCCESS;
    MIDIPortDisconnectSource(dev->cm.port, dev->cm.endpoint); return MM_SUCCESS;
}
mm_result mm_in_close(mm_device* dev) {
    if (!dev||!dev->is_open) return MM_NOT_OPEN;
    mm_in_stop(dev);
    if (dev->is_virtual)
        MIDIEndpointDispose(dev->cm.virt_ep);
    else
        MIDIPortDispose(dev->cm.port);
    dev->is_open=0; return MM_SUCCESS;
}

mm_result mm_out_open(mm_context* ctx, mm_device* dev, uint32_t idx) {
    if (!ctx||!dev) return MM_INVALID_ARG;
    if (idx >= MIDIGetNumberOfDestinations()) return MM_OUT_OF_RANGE;
    memset(dev, 0, sizeof(*dev)); dev->ctx=ctx; dev->is_input=0;
    dev->cm.endpoint = MIDIGetDestination(idx);
    char portname[80]; snprintf(portname, sizeof(portname), "%s-out", ctx->name);
    CFStringRef cfport = CFStringCreateWithCString(NULL, portname, kCFStringEncodingUTF8);
    OSStatus st = MIDIOutputPortCreate(ctx->cm.client, cfport, &dev->cm.port);
    CFRelease(cfport);
    if (st != noErr) return MM_ERROR;
    dev->is_open=1; return MM_SUCCESS;
}

mm_result mm_out_send(mm_device* dev, const mm_message* msg) {
    if (!dev||!dev->is_open||dev->is_input) return MM_NOT_OPEN;
    if (!msg) return MM_INVALID_ARG;
    uint8_t raw[3]; int len=1;
    switch (msg->type) {
        case MM_NOTE_OFF: case MM_NOTE_ON: case MM_POLY_PRESSURE:
        case MM_CONTROL_CHANGE: case MM_PITCH_BEND:
            raw[0]=(uint8_t)(((uint8_t)msg->type<<4)|(msg->channel&0xF));
            raw[1]=msg->data[0]; raw[2]=msg->data[1]; len=3; break;
        case MM_PROGRAM_CHANGE: case MM_CHANNEL_PRESSURE:
            raw[0]=(uint8_t)(((uint8_t)msg->type<<4)|(msg->channel&0xF));
            raw[1]=msg->data[0]; len=2; break;
        case MM_SONG_POSITION:
            raw[0]=0xF2; raw[1]=(uint8_t)(msg->song_position&0x7F);
            raw[2]=(uint8_t)((msg->song_position>>7)&0x7F); len=3; break;
        case MM_MTC_QUARTER_FRAME: raw[0]=0xF1; raw[1]=msg->data[0]; len=2; break;
        case MM_SONG_SELECT:       raw[0]=0xF3; raw[1]=msg->data[0]; len=2; break;
        case MM_TUNE_REQUEST:      raw[0]=0xF6; len=1; break;
        case MM_CLOCK:             raw[0]=0xF8; len=1; break;
        case MM_START:             raw[0]=0xFA; len=1; break;
        case MM_CONTINUE:          raw[0]=0xFB; len=1; break;
        case MM_STOP:              raw[0]=0xFC; len=1; break;
        case MM_ACTIVE_SENSE:      raw[0]=0xFE; len=1; break;
        case MM_RESET:             raw[0]=0xFF; len=1; break;
        default: return MM_INVALID_ARG;
    }
    MIDIPacketList pl; MIDIPacket* p = MIDIPacketListInit(&pl);
    p = MIDIPacketListAdd(&pl, sizeof(pl), p, 0, (ByteCount)len, raw);
    if (!p) return MM_ERROR;
    if (dev->is_virtual)
        return (MIDIReceived(dev->cm.virt_ep, &pl) == noErr) ? MM_SUCCESS : MM_ERROR;
    return (MIDISend(dev->cm.port, dev->cm.endpoint, &pl) == noErr) ? MM_SUCCESS : MM_ERROR;
}

mm_result mm_out_send_sysex(mm_device* dev, const uint8_t* data, size_t size) {
    if (!dev||!dev->is_open||dev->is_input) return MM_NOT_OPEN;
    if (!data||!size||size>MM_SYSEX_BUF_SIZE) return MM_INVALID_ARG;
    memcpy(dev->cm.sysex_buf, data, size);
    if (dev->is_virtual) {
        /* Virtual source: push sysex as a packet directly to subscribers */
        MIDIPacketList pl; MIDIPacket* p = MIDIPacketListInit(&pl);
        p = MIDIPacketListAdd(&pl, sizeof(pl), p, 0, (ByteCount)size,
                              dev->cm.sysex_buf);
        if (!p) return MM_ERROR;
        return (MIDIReceived(dev->cm.virt_ep, &pl) == noErr) ? MM_SUCCESS : MM_ERROR;
    }
    dev->cm.sysex_req.destination      = dev->cm.endpoint;
    dev->cm.sysex_req.data             = dev->cm.sysex_buf;
    dev->cm.sysex_req.bytesToSend      = (UInt32)size;
    dev->cm.sysex_req.complete         = false;
    dev->cm.sysex_req.completionProc   = NULL;
    dev->cm.sysex_req.completionRefCon = NULL;
    return (MIDISendSysex(&dev->cm.sysex_req) == noErr) ? MM_SUCCESS : MM_ERROR;
}
mm_result mm_out_close(mm_device* dev) {
    if (!dev||!dev->is_open) return MM_NOT_OPEN;
    if (dev->is_virtual) {
        MIDIEndpointDispose(dev->cm.virt_ep);
    } else {
        MIDIPortDispose(dev->cm.port);
    }
    dev->is_open=0; return MM_SUCCESS;
}

/* ── Virtual ports (CoreMIDI) ──────────────────────────────────────────────
   mm_in_open_virtual  → MIDIDestinationCreate: other apps send TO us.
   mm_out_open_virtual → MIDISourceCreate:      other apps receive FROM us.  */

mm_result mm_in_open_virtual(mm_context* ctx, mm_device* dev,
                              mm_callback cb, void* ud)
{
    if (!ctx||!dev||!cb) return MM_INVALID_ARG;
    memset(dev, 0, sizeof(*dev));
    dev->ctx=ctx; dev->callback=cb; dev->userdata=ud;
    dev->is_input=1; dev->is_virtual=1;

    CFStringRef cfname = CFStringCreateWithCString(NULL, ctx->name,
                                                    kCFStringEncodingUTF8);
    OSStatus st = MIDIDestinationCreate(ctx->cm.client, cfname,
                                        mm__cm_read_proc, dev,
                                        &dev->cm.virt_ep);
    CFRelease(cfname);
    if (st != noErr) return MM_ERROR;
    dev->is_open=1; return MM_SUCCESS;
}

/* For a virtual destination there is nothing to "connect" — other apps
   connect themselves to us — so start/stop are no-ops on CoreMIDI.          */
/* mm_in_start, mm_in_stop, mm_in_close are shared below */

mm_result mm_out_open_virtual(mm_context* ctx, mm_device* dev)
{
    if (!ctx||!dev) return MM_INVALID_ARG;
    memset(dev, 0, sizeof(*dev));
    dev->ctx=ctx; dev->is_input=0; dev->is_virtual=1;

    CFStringRef cfname = CFStringCreateWithCString(NULL, ctx->name,
                                                    kCFStringEncodingUTF8);
    OSStatus st = MIDISourceCreate(ctx->cm.client, cfname, &dev->cm.virt_ep);
    CFRelease(cfname);
    if (st != noErr) return MM_ERROR;
    dev->is_open=1; return MM_SUCCESS;
}

/* ─────────────────────────────────────────────────────────────────────────────
   WinMM (Windows)
   ───────────────────────────────────────────────────────────────────────── */
#elif defined(MM_BACKEND_WINMM)

mm_result mm_context_init(mm_context* ctx, const char* name) {
    if (!ctx) return MM_INVALID_ARG;
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->name, (name && name[0]) ? name : "minimidio", sizeof(ctx->name)-1);
    ctx->initialized=1; return MM_SUCCESS;
    /* Note: WinMM has no client-name concept; ctx->name is stored but unused
       by the backend. The app is identified to other software only by the
       hardware port it opens.                                                 */
}
mm_result mm_context_uninit(mm_context* ctx) { if(!ctx)return MM_INVALID_ARG; ctx->initialized=0; return MM_SUCCESS; }

uint32_t mm_in_count (mm_context* ctx) { (void)ctx; return (uint32_t)midiInGetNumDevs();  }
uint32_t mm_out_count(mm_context* ctx) { (void)ctx; return (uint32_t)midiOutGetNumDevs(); }

mm_result mm_in_name(mm_context* ctx, uint32_t idx, char* buf, size_t sz) {
    (void)ctx; MIDIINCAPSA c;
    if (midiInGetDevCapsA(idx,&c,sizeof(c))!=MMSYSERR_NOERROR) return MM_OUT_OF_RANGE;
    strncpy(buf,c.szPname,sz-1); buf[sz-1]='\0'; return MM_SUCCESS;
}
mm_result mm_out_name(mm_context* ctx, uint32_t idx, char* buf, size_t sz) {
    (void)ctx; MIDIOUTCAPSA c;
    if (midiOutGetDevCapsA(idx,&c,sizeof(c))!=MMSYSERR_NOERROR) return MM_OUT_OF_RANGE;
    strncpy(buf,c.szPname,sz-1); buf[sz-1]='\0'; return MM_SUCCESS;
}

static void CALLBACK mm__wm_in_proc(HMIDIIN hmi, UINT wmsg,
                                     DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2)
{
    mm_device* dev = (mm_device*)inst; (void)hmi;
    if (!dev || !dev->callback) return;

    if (wmsg == MIM_DATA) {
        uint8_t s  = (uint8_t)( p1        & 0xFF);
        uint8_t d1 = (uint8_t)((p1 >>  8) & 0xFF);
        uint8_t d2 = (uint8_t)((p1 >> 16) & 0xFF);
        double  ts = (double)p2 / 1000.0;

        mm_message msg; memset(&msg,0,sizeof(msg)); msg.timestamp=ts;

        /* Real-time */
        if (s >= 0xF8) {
            switch (s) {
                case 0xF8: msg.type=MM_CLOCK;        break;
                case 0xFA: msg.type=MM_START;        break;
                case 0xFB: msg.type=MM_CONTINUE;     break;
                case 0xFC: msg.type=MM_STOP;         break;
                case 0xFE: msg.type=MM_ACTIVE_SENSE; break;
                case 0xFF: msg.type=MM_RESET;        break;
                default: return;
            }
            dev->callback(dev, &msg, dev->userdata); return;
        }

        /* System common */
        if (s >= 0xF0) {
            switch (s) {
                case 0xF1:
                    msg.type=MM_MTC_QUARTER_FRAME; msg.data[0]=d1; break;
                case 0xF2:
                    msg.type=MM_SONG_POSITION; msg.data[0]=d1; msg.data[1]=d2;
                    msg.song_position=(uint16_t)(d1|((uint16_t)d2<<7)); break;
                case 0xF3:
                    msg.type=MM_SONG_SELECT; msg.data[0]=d1; break;
                case 0xF6:
                    msg.type=MM_TUNE_REQUEST; break;
                default: return;
            }
            dev->callback(dev, &msg, dev->userdata); return;
        }

        /* Channel messages */
        msg = mm_make_message(s, d1, d2); msg.timestamp=ts;
        dev->callback(dev, &msg, dev->userdata);

    } else if (wmsg == MIM_LONGDATA) {
        MIDIHDR* hdr = (MIDIHDR*)p1;
        if (hdr && hdr->dwBytesRecorded>0 && (uint8_t)hdr->lpData[0]==0xF0) {
            mm_message msg; memset(&msg,0,sizeof(msg));
            msg.type=MM_SYSEX; msg.timestamp=(double)p2/1000.0;
            msg.sysex=(const uint8_t*)hdr->lpData; msg.sysex_size=hdr->dwBytesRecorded;
            dev->callback(dev, &msg, dev->userdata);
        }
        midiInAddBuffer(dev->wm.in, hdr, sizeof(MIDIHDR));
    }
}

mm_result mm_in_open(mm_context* ctx, mm_device* dev, uint32_t idx,
                     mm_callback cb, void* ud)
{
    if (!ctx||!dev||!cb) return MM_INVALID_ARG;
    memset(dev,0,sizeof(*dev)); dev->ctx=ctx; dev->callback=cb; dev->userdata=ud; dev->is_input=1;
    if (midiInOpen(&dev->wm.in,(UINT)idx,(DWORD_PTR)mm__wm_in_proc,(DWORD_PTR)dev,
                   CALLBACK_FUNCTION) != MMSYSERR_NOERROR) return MM_ERROR;
    memset(&dev->wm.sysex_hdr,0,sizeof(dev->wm.sysex_hdr));
    dev->wm.sysex_hdr.lpData=(LPSTR)dev->wm.sysex_buf;
    dev->wm.sysex_hdr.dwBufferLength=MM_SYSEX_BUF_SIZE;
    midiInPrepareHeader(dev->wm.in,&dev->wm.sysex_hdr,sizeof(MIDIHDR));
    midiInAddBuffer(dev->wm.in,&dev->wm.sysex_hdr,sizeof(MIDIHDR));
    dev->is_open=1; return MM_SUCCESS;
}
mm_result mm_in_start(mm_device* dev) {
    if (!dev||!dev->is_open||!dev->is_input) return MM_NOT_OPEN;
    return (midiInStart(dev->wm.in)==MMSYSERR_NOERROR)?MM_SUCCESS:MM_ERROR;
}
mm_result mm_in_stop(mm_device* dev) {
    if (!dev||!dev->is_open||!dev->is_input) return MM_NOT_OPEN;
    midiInStop(dev->wm.in); return MM_SUCCESS;
}
mm_result mm_in_close(mm_device* dev) {
    if (!dev||!dev->is_open) return MM_NOT_OPEN;
    midiInStop(dev->wm.in);
    midiInUnprepareHeader(dev->wm.in,&dev->wm.sysex_hdr,sizeof(MIDIHDR));
    midiInClose(dev->wm.in); dev->is_open=0; return MM_SUCCESS;
}

mm_result mm_out_open(mm_context* ctx, mm_device* dev, uint32_t idx) {
    if (!ctx||!dev) return MM_INVALID_ARG;
    memset(dev,0,sizeof(*dev)); dev->ctx=ctx; dev->is_input=0;
    if (midiOutOpen(&dev->wm.out,(UINT)idx,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)
        return MM_ERROR;
    dev->is_open=1; return MM_SUCCESS;
}

mm_result mm_out_send(mm_device* dev, const mm_message* msg) {
    if (!dev||!dev->is_open||dev->is_input) return MM_NOT_OPEN;
    if (!msg) return MM_INVALID_ARG;
    DWORD pk;
    switch (msg->type) {
        case MM_SONG_POSITION:
            pk=0xF2|((DWORD)(msg->song_position&0x7F)<<8)
                   |((DWORD)((msg->song_position>>7)&0x7F)<<16); break;
        case MM_MTC_QUARTER_FRAME: pk=0xF1|((DWORD)msg->data[0]<<8); break;
        case MM_SONG_SELECT:       pk=0xF3|((DWORD)msg->data[0]<<8); break;
        case MM_TUNE_REQUEST:  pk=0xF6; break;
        case MM_CLOCK:         pk=0xF8; break;
        case MM_START:         pk=0xFA; break;
        case MM_CONTINUE:      pk=0xFB; break;
        case MM_STOP:          pk=0xFC; break;
        case MM_ACTIVE_SENSE:  pk=0xFE; break;
        case MM_RESET:         pk=0xFF; break;
        default: {
            uint8_t st=(uint8_t)(((uint8_t)msg->type<<4)|(msg->channel&0xF));
            pk=st|((DWORD)msg->data[0]<<8)|((DWORD)msg->data[1]<<16);
        }
    }
    return (midiOutShortMsg(dev->wm.out,pk)==MMSYSERR_NOERROR)?MM_SUCCESS:MM_ERROR;
}

mm_result mm_out_send_sysex(mm_device* dev, const uint8_t* data, size_t size) {
    if (!dev||!dev->is_open||dev->is_input) return MM_NOT_OPEN;
    if (!data||!size||size>MM_SYSEX_BUF_SIZE) return MM_INVALID_ARG;
    memcpy(dev->wm.sysex_buf,data,size);
    memset(&dev->wm.sysex_hdr,0,sizeof(dev->wm.sysex_hdr));
    dev->wm.sysex_hdr.lpData=(LPSTR)dev->wm.sysex_buf;
    dev->wm.sysex_hdr.dwBufferLength=(DWORD)size;
    dev->wm.sysex_hdr.dwBytesRecorded=(DWORD)size;
    midiOutPrepareHeader(dev->wm.out,&dev->wm.sysex_hdr,sizeof(MIDIHDR));
    MMRESULT r=midiOutLongMsg(dev->wm.out,&dev->wm.sysex_hdr,sizeof(MIDIHDR));
    while (midiOutUnprepareHeader(dev->wm.out,&dev->wm.sysex_hdr,sizeof(MIDIHDR))
           ==MIDIERR_STILLPLAYING) Sleep(1);
    return (r==MMSYSERR_NOERROR)?MM_SUCCESS:MM_ERROR;
}
mm_result mm_out_close(mm_device* dev) {
    if (!dev||!dev->is_open) return MM_NOT_OPEN;
    midiOutClose(dev->wm.out); dev->is_open=0; return MM_SUCCESS;
}

/* WinMM has no virtual port API. Users should install loopMIDI
   (https://www.tobias-erichsen.de/software/loopmidi.html) to create
   a virtual cable, then use mm_in_open / mm_out_open with that port. */
mm_result mm_in_open_virtual(mm_context* ctx, mm_device* dev,
                              mm_callback cb, void* ud)
{
    (void)ctx; (void)dev; (void)cb; (void)ud;
    return MM_NO_BACKEND;
}
mm_result mm_out_open_virtual(mm_context* ctx, mm_device* dev)
{
    (void)ctx; (void)dev;
    return MM_NO_BACKEND;
}

/* ─────────────────────────────────────────────────────────────────────────────
   ALSA sequencer (Linux) — compile with -lasound -lpthread only
   ───────────────────────────────────────────────────────────────────────── */
#elif defined(MM_BACKEND_ALSA)

#include <time.h>
#include <unistd.h>
#include <errno.h>

mm_result mm_context_init(mm_context* ctx, const char* name) {
    if (!ctx) return MM_INVALID_ARG;
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->name, (name && name[0]) ? name : "minimidio", sizeof(ctx->name)-1);
    if (snd_seq_open(&ctx->al.seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return MM_ERROR;
    snd_seq_set_client_name(ctx->al.seq, ctx->name);
    ctx->al.client_id = snd_seq_client_id(ctx->al.seq);
    ctx->initialized = 1; return MM_SUCCESS;
}

mm_result mm_context_uninit(mm_context* ctx) {
    if (!ctx||!ctx->initialized) return MM_INVALID_ARG;
    snd_seq_close(ctx->al.seq);
    ctx->initialized = 0; return MM_SUCCESS;
}

/* ── Port enumeration ────────────────────────────────────────────────────────
   cap_required: ALL these capability bits must be present.
   cap_any:      OR accept if ANY of these bits match (catches DAW clock ports
                 that expose CAP_READ but omit CAP_SUBS_READ).               */

typedef struct { int client, port; char name[256]; } mm__alsa_pi;
typedef struct { mm__alsa_pi ports[MM_MAX_PORTS]; uint32_t count; } mm__alsa_pl;

static void mm__alsa_enum(mm_context* ctx, mm__alsa_pl* lst,
                           unsigned int cap_req, unsigned int cap_any)
{
    mm__ctx_alsa* al = &ctx->al; lst->count = 0;

    /* snd_seq_client_info_alloca / snd_seq_port_info_alloca are stack-based
       macros in the ALSA headers — no malloc/free, no dlsym needed.         */
    snd_seq_client_info_t* ci;
    snd_seq_port_info_t*   pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);

    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(al->seq, ci) >= 0) {
        int cid = snd_seq_client_info_get_client(ci);
        if (cid == al->client_id) continue;
        snd_seq_port_info_set_client(pi, cid);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(al->seq, pi) >= 0) {
            unsigned int cap = snd_seq_port_info_get_capability(pi);
            int ok = ((cap&cap_req)==cap_req) || (cap_any && (cap&cap_any));
            if (!ok || lst->count >= MM_MAX_PORTS) continue;
            mm__alsa_pi* p = &lst->ports[lst->count++];
            p->client = cid;
            p->port   = snd_seq_port_info_get_port(pi);
            snprintf(p->name, sizeof(p->name), "%s:%s (%d:%d)",
                     snd_seq_client_info_get_name(ci),
                     snd_seq_port_info_get_name(pi), cid, p->port);
        }
    }
}

uint32_t mm_in_count(mm_context* ctx) {
    if (!ctx||!ctx->initialized) return 0;
    mm__alsa_pl lst;
    /* Accept full subscribe ports OR plain READ-only (DAW clock sources) */
    mm__alsa_enum(ctx, &lst,
        SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_CAP_READ);
    return lst.count;
}
uint32_t mm_out_count(mm_context* ctx) {
    if (!ctx||!ctx->initialized) return 0;
    mm__alsa_pl lst;
    mm__alsa_enum(ctx, &lst,
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, 0);
    return lst.count;
}
mm_result mm_in_name(mm_context* ctx, uint32_t idx, char* buf, size_t sz) {
    if (!ctx||!ctx->initialized||!buf) return MM_INVALID_ARG;
    mm__alsa_pl lst;
    mm__alsa_enum(ctx, &lst,
        SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_CAP_READ);
    if (idx >= lst.count) return MM_OUT_OF_RANGE;
    strncpy(buf, lst.ports[idx].name, sz-1); buf[sz-1]='\0'; return MM_SUCCESS;
}
mm_result mm_out_name(mm_context* ctx, uint32_t idx, char* buf, size_t sz) {
    if (!ctx||!ctx->initialized||!buf) return MM_INVALID_ARG;
    mm__alsa_pl lst;
    mm__alsa_enum(ctx, &lst,
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, 0);
    if (idx >= lst.count) return MM_OUT_OF_RANGE;
    strncpy(buf, lst.ports[idx].name, sz-1); buf[sz-1]='\0'; return MM_SUCCESS;
}

/* ── Receive thread — poll()-based, zero added latency ──────────────────────*/

static void* mm__alsa_recv_thread(void* arg)
{
    mm_device*    dev = (mm_device*)arg;
    mm__ctx_alsa* al  = &dev->ctx->al;
    mm__dev_alsa* da  = &dev->al;

    /* Build pollfd set: ALSA fds + wakeup pipe read end */
    int nalsa = snd_seq_poll_descriptors_count(al->seq, POLLIN);
    if (nalsa < 0) nalsa = 0;
    int nfds = nalsa + 1;
    struct pollfd* pfds = (struct pollfd*)malloc((size_t)nfds * sizeof(struct pollfd));
    if (!pfds) return NULL;

    snd_seq_poll_descriptors(al->seq, pfds, (unsigned)nalsa, POLLIN);
    pfds[nalsa].fd     = da->wake_pipe[0];
    pfds[nalsa].events = POLLIN;

    while (da->running) {
        if (poll(pfds, (nfds_t)nfds, -1) < 0) break;

        /* Wakeup pipe: stop requested */
        if (pfds[nalsa].revents & POLLIN) {
            char c; (void)read(da->wake_pipe[0], &c, 1); break;
        }

        /* Drain all pending events from the kernel buffer.
           Pass fetch_sequencer=1 to snd_seq_event_input_pending so it
           actually queries the kernel — without this, virtual-port events
           sit in the kernel ring and the pending count reads as 0.          */
        while (snd_seq_event_input_pending(al->seq, 1) > 0) {
            snd_seq_event_t* ev = NULL;
            int rc = snd_seq_event_input(al->seq, &ev);
            if (rc == -EAGAIN || rc == -ENOSPC) break; /* nothing left */
            if (rc < 0 || !ev) break;

            mm_message msg; memset(&msg, 0, sizeof(msg));
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            msg.timestamp = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;

            switch (ev->type) {
                /* ── Channel messages ── */
                case SND_SEQ_EVENT_NOTEON:
                    msg.type    = (ev->data.note.velocity > 0) ? MM_NOTE_ON : MM_NOTE_OFF;
                    msg.channel = ev->data.note.channel;
                    msg.data[0] = ev->data.note.note;
                    msg.data[1] = ev->data.note.velocity;
                    dev->callback(dev, &msg, dev->userdata); break;

                case SND_SEQ_EVENT_NOTEOFF:
                    msg.type=MM_NOTE_OFF; msg.channel=ev->data.note.channel;
                    msg.data[0]=ev->data.note.note; msg.data[1]=ev->data.note.velocity;
                    dev->callback(dev, &msg, dev->userdata); break;

                case SND_SEQ_EVENT_KEYPRESS:
                    msg.type=MM_POLY_PRESSURE; msg.channel=ev->data.note.channel;
                    msg.data[0]=ev->data.note.note; msg.data[1]=ev->data.note.velocity;
                    dev->callback(dev, &msg, dev->userdata); break;

                case SND_SEQ_EVENT_CONTROLLER:
                    msg.type=MM_CONTROL_CHANGE; msg.channel=ev->data.control.channel;
                    msg.data[0]=(uint8_t)ev->data.control.param;
                    msg.data[1]=(uint8_t)ev->data.control.value;
                    dev->callback(dev, &msg, dev->userdata); break;

                case SND_SEQ_EVENT_PGMCHANGE:
                    msg.type=MM_PROGRAM_CHANGE; msg.channel=ev->data.control.channel;
                    msg.data[0]=(uint8_t)ev->data.control.value;
                    dev->callback(dev, &msg, dev->userdata); break;

                case SND_SEQ_EVENT_CHANPRESS:
                    msg.type=MM_CHANNEL_PRESSURE; msg.channel=ev->data.control.channel;
                    msg.data[0]=(uint8_t)ev->data.control.value;
                    dev->callback(dev, &msg, dev->userdata); break;

                case SND_SEQ_EVENT_PITCHBEND: {
                    int pb=ev->data.control.value+8192;
                    msg.type=MM_PITCH_BEND; msg.channel=ev->data.control.channel;
                    msg.data[0]=(uint8_t)(pb&0x7F); msg.data[1]=(uint8_t)((pb>>7)&0x7F);
                    dev->callback(dev, &msg, dev->userdata); break;
                }

                /* ── Transport & clock ── */
                case SND_SEQ_EVENT_CLOCK:
                    msg.type=MM_CLOCK; dev->callback(dev,&msg,dev->userdata); break;
                case SND_SEQ_EVENT_START:
                    msg.type=MM_START; dev->callback(dev,&msg,dev->userdata); break;
                case SND_SEQ_EVENT_CONTINUE:
                    msg.type=MM_CONTINUE; dev->callback(dev,&msg,dev->userdata); break;
                case SND_SEQ_EVENT_STOP:
                    msg.type=MM_STOP; dev->callback(dev,&msg,dev->userdata); break;

                /* ── Song Position Pointer ── */
                case SND_SEQ_EVENT_SONGPOS: {
                    uint16_t pos=(uint16_t)ev->data.control.value;
                    msg.type=MM_SONG_POSITION; msg.song_position=pos;
                    msg.data[0]=(uint8_t)(pos&0x7F);
                    msg.data[1]=(uint8_t)((pos>>7)&0x7F);
                    dev->callback(dev,&msg,dev->userdata); break;
                }

                /* ── MTC quarter frame ── */
                case SND_SEQ_EVENT_QFRAME:
                    msg.type=MM_MTC_QUARTER_FRAME;
                    msg.data[0]=(uint8_t)ev->data.control.value;
                    dev->callback(dev,&msg,dev->userdata); break;

                /* ── Song Select ── */
                case SND_SEQ_EVENT_SONGSEL:
                    msg.type=MM_SONG_SELECT;
                    msg.data[0]=(uint8_t)ev->data.control.value;
                    dev->callback(dev,&msg,dev->userdata); break;

                /* ── Active Sensing ── */
                case SND_SEQ_EVENT_SENSING:
                    msg.type=MM_ACTIVE_SENSE;
                    dev->callback(dev,&msg,dev->userdata); break;

                /* ── Tune Request ── */
                case SND_SEQ_EVENT_TUNE_REQUEST:
                    msg.type=MM_TUNE_REQUEST;
                    dev->callback(dev,&msg,dev->userdata); break;

                /* ── Reset ── */
                case SND_SEQ_EVENT_RESET:
                    msg.type=MM_RESET;
                    dev->callback(dev,&msg,dev->userdata); break;

                /* ── SysEx (may arrive in chunks) ── */
                case SND_SEQ_EVENT_SYSEX: {
                    uint8_t* d=(uint8_t*)ev->data.ext.ptr;
                    size_t   n=ev->data.ext.len;
                    if (da->sysex_pos+n < MM_SYSEX_BUF_SIZE) {
                        memcpy(da->sysex_buf+da->sysex_pos, d, n);
                        da->sysex_pos += n;
                    }
                    if (n > 0 && d[n-1] == 0xF7) {
                        msg.type=MM_SYSEX; msg.sysex=da->sysex_buf;
                        msg.sysex_size=da->sysex_pos;
                        dev->callback(dev,&msg,dev->userdata);
                        da->sysex_pos=0;
                    }
                    break;
                }
                default: break;
            }
        }
    }

    free(pfds);
    return NULL;
}

mm_result mm_in_open(mm_context* ctx, mm_device* dev, uint32_t idx,
                     mm_callback cb, void* ud)
{
    if (!ctx||!ctx->initialized||!dev||!cb) return MM_INVALID_ARG;
    mm__alsa_pl lst;
    mm__alsa_enum(ctx, &lst,
        SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_CAP_READ);
    if (idx >= lst.count) return MM_OUT_OF_RANGE;

    memset(dev,0,sizeof(*dev));
    dev->ctx=ctx; dev->callback=cb; dev->userdata=ud; dev->is_input=1;
    dev->al.target_client = lst.ports[idx].client;
    dev->al.target_port   = lst.ports[idx].port;

    if (pipe(dev->al.wake_pipe) != 0) return MM_ERROR;

    char portname[80]; snprintf(portname, sizeof(portname), "%s-in", ctx->name);
    dev->al.port_id = snd_seq_create_simple_port(ctx->al.seq, portname,
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (dev->al.port_id < 0) {
        close(dev->al.wake_pipe[0]); close(dev->al.wake_pipe[1]); return MM_ERROR;
    }
    dev->is_open=1; return MM_SUCCESS;
}

mm_result mm_in_start(mm_device* dev) {
    if (!dev||!dev->is_open||!dev->is_input) return MM_NOT_OPEN;
    if (!dev->is_virtual) {
        mm__ctx_alsa* al=&dev->ctx->al;
        snd_seq_connect_from(al->seq, dev->al.port_id,
                                  dev->al.target_client, dev->al.target_port);
    }
    dev->al.running=1;
    pthread_create(&dev->al.thread, NULL, mm__alsa_recv_thread, dev);
    return MM_SUCCESS;
}

mm_result mm_in_stop(mm_device* dev) {
    if (!dev||!dev->is_open||!dev->is_input) return MM_NOT_OPEN;
    dev->al.running=0;
    char c=1; (void)write(dev->al.wake_pipe[1], &c, 1); /* wake the poll() */
    pthread_join(dev->al.thread, NULL);
    if (!dev->is_virtual) {
        mm__ctx_alsa* al=&dev->ctx->al;
        snd_seq_disconnect_from(al->seq, dev->al.port_id,
                                     dev->al.target_client, dev->al.target_port);
    }
    return MM_SUCCESS;
}

mm_result mm_in_close(mm_device* dev) {
    if (!dev||!dev->is_open) return MM_NOT_OPEN;
    if (dev->al.running) mm_in_stop(dev);
    close(dev->al.wake_pipe[0]); close(dev->al.wake_pipe[1]);
    snd_seq_delete_port(dev->ctx->al.seq, dev->al.port_id);
    dev->is_open=0; return MM_SUCCESS;
}

mm_result mm_out_open(mm_context* ctx, mm_device* dev, uint32_t idx) {
    if (!ctx||!ctx->initialized||!dev) return MM_INVALID_ARG;
    mm__alsa_pl lst;
    mm__alsa_enum(ctx, &lst,
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE, 0);
    if (idx >= lst.count) return MM_OUT_OF_RANGE;
    memset(dev,0,sizeof(*dev)); dev->ctx=ctx; dev->is_input=0;
    dev->al.target_client=lst.ports[idx].client;
    dev->al.target_port  =lst.ports[idx].port;
    char portname[80]; snprintf(portname, sizeof(portname), "%s-out", ctx->name);
    dev->al.port_id=snd_seq_create_simple_port(ctx->al.seq, portname,
        SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (dev->al.port_id < 0) return MM_ERROR;
    snd_seq_connect_to(ctx->al.seq, dev->al.port_id,
                                dev->al.target_client, dev->al.target_port);
    dev->is_open=1; return MM_SUCCESS;
}

/* snd_seq_ev_set_* are inline static functions/macros in the ALSA headers.
   We call them directly — no dlsym needed.                                  */
static void mm__alsa_send_ev(mm_device* dev, snd_seq_event_t* ev) {
    mm__ctx_alsa* al=&dev->ctx->al;
    snd_seq_ev_set_direct(ev);
    snd_seq_ev_set_source(ev, dev->al.port_id);
    snd_seq_ev_set_subs(ev);
    snd_seq_event_output(al->seq, ev);
    snd_seq_drain_output(al->seq);
}

mm_result mm_out_send(mm_device* dev, const mm_message* msg) {
    if (!dev||!dev->is_open||dev->is_input) return MM_NOT_OPEN;
    if (!msg) return MM_INVALID_ARG;
    snd_seq_event_t ev; memset(&ev,0,sizeof(ev));
    switch (msg->type) {
        case MM_NOTE_ON:
            snd_seq_ev_set_noteon(&ev,msg->channel,msg->data[0],msg->data[1]); break;
        case MM_NOTE_OFF:
            snd_seq_ev_set_noteoff(&ev,msg->channel,msg->data[0],msg->data[1]); break;
        case MM_CONTROL_CHANGE:
            snd_seq_ev_set_controller(&ev,msg->channel,msg->data[0],msg->data[1]); break;
        case MM_PITCH_BEND: {
            int pb=((int)msg->data[1]<<7)|msg->data[0];
            snd_seq_ev_set_pitchbend(&ev,msg->channel,pb-8192); break;
        }
        case MM_PROGRAM_CHANGE:
            snd_seq_ev_set_pgmchange(&ev,msg->channel,msg->data[0]); break;
        case MM_CLOCK:    ev.type=SND_SEQ_EVENT_CLOCK;    break;
        case MM_START:    ev.type=SND_SEQ_EVENT_START;    break;
        case MM_CONTINUE: ev.type=SND_SEQ_EVENT_CONTINUE; break;
        case MM_STOP:     ev.type=SND_SEQ_EVENT_STOP;     break;
        case MM_SONG_POSITION:
            ev.type=SND_SEQ_EVENT_SONGPOS;
            ev.data.control.value=msg->song_position; break;
        case MM_MTC_QUARTER_FRAME:
            ev.type=SND_SEQ_EVENT_QFRAME;
            ev.data.control.value=msg->data[0]; break;
        case MM_SONG_SELECT:
            ev.type=SND_SEQ_EVENT_SONGSEL;
            ev.data.control.value=msg->data[0]; break;
        case MM_TUNE_REQUEST:  ev.type=SND_SEQ_EVENT_TUNE_REQUEST; break;
        case MM_ACTIVE_SENSE:  ev.type=SND_SEQ_EVENT_SENSING;      break;
        case MM_RESET:         ev.type=SND_SEQ_EVENT_RESET;        break;
        default: return MM_INVALID_ARG;
    }
    mm__alsa_send_ev(dev,&ev); return MM_SUCCESS;
}

mm_result mm_out_send_sysex(mm_device* dev, const uint8_t* data, size_t size) {
    if (!dev||!dev->is_open||dev->is_input) return MM_NOT_OPEN;
    if (!data||!size||size>MM_SYSEX_BUF_SIZE) return MM_INVALID_ARG;
    memcpy(dev->al.sysex_buf, data, size);
    snd_seq_event_t ev; memset(&ev,0,sizeof(ev));
    ev.type=SND_SEQ_EVENT_SYSEX;
    ev.data.ext.len=(unsigned int)size;
    ev.data.ext.ptr=dev->al.sysex_buf;
    ev.flags=SND_SEQ_EVENT_LENGTH_VARIABLE;
    mm__alsa_send_ev(dev,&ev); return MM_SUCCESS;
}

mm_result mm_out_close(mm_device* dev) {
    if (!dev||!dev->is_open) return MM_NOT_OPEN;
    mm__ctx_alsa* al=&dev->ctx->al;
    if (!dev->is_virtual)
        snd_seq_disconnect_to(al->seq,dev->al.port_id,
                                   dev->al.target_client,dev->al.target_port);
    snd_seq_delete_port(al->seq,dev->al.port_id);
    dev->is_open=0; return MM_SUCCESS;
}

/* ── Virtual ports (ALSA) ──────────────────────────────────────────────────
   On ALSA, the difference between a normal and virtual port is just capability
   flags and whether we call snd_seq_connect_from/to ourselves.

   Virtual input  (other apps send to us):
     CAP_WRITE | CAP_SUBS_WRITE  — apps can connect their output here.
     We do NOT call snd_seq_connect_from; apps wire themselves.

   Virtual output (other apps receive from us):
     CAP_READ  | CAP_SUBS_READ   — apps can subscribe to our output.
     We do NOT call snd_seq_connect_to; apps subscribe themselves.
     mm_out_send uses snd_seq_ev_set_subs to broadcast to all subscribers.   */

mm_result mm_in_open_virtual(mm_context* ctx, mm_device* dev,
                              mm_callback cb, void* ud)
{
    if (!ctx||!ctx->initialized||!dev||!cb) return MM_INVALID_ARG;
    memset(dev,0,sizeof(*dev));
    dev->ctx=ctx; dev->callback=cb; dev->userdata=ud;
    dev->is_input=1; dev->is_virtual=1;

    if (pipe(dev->al.wake_pipe) != 0) return MM_ERROR;

    /* Port name is just the client name — no "-in" suffix for virtual ports,
       since the client name already identifies the app uniquely.              */
    dev->al.port_id = snd_seq_create_simple_port(ctx->al.seq, ctx->name,
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);
    if (dev->al.port_id < 0) {
        close(dev->al.wake_pipe[0]); close(dev->al.wake_pipe[1]); return MM_ERROR;
    }
    dev->is_open=1; return MM_SUCCESS;
}

/* mm_in_start for virtual ALSA: just launch the recv thread.
   No explicit connect — other apps connect to us.                            */
/* mm_in_start / mm_in_stop / mm_in_close are already defined above and
   handle the virtual case: stop skips snd_seq_disconnect_from when virtual. */

mm_result mm_out_open_virtual(mm_context* ctx, mm_device* dev)
{
    if (!ctx||!ctx->initialized||!dev) return MM_INVALID_ARG;
    memset(dev,0,sizeof(*dev));
    dev->ctx=ctx; dev->is_input=0; dev->is_virtual=1;

    dev->al.port_id = snd_seq_create_simple_port(ctx->al.seq, ctx->name,
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_MIDI_GENERIC);
    if (dev->al.port_id < 0) return MM_ERROR;
    /* No snd_seq_connect_to — subscribers connect themselves */
    dev->is_open=1; return MM_SUCCESS;
}

#endif /* ALSA backend */

#endif /* MINIMIDIO_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* MINIMIDIO_H */

/*
MIT License

Copyright (c) 2026 Joseph Stewart

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
