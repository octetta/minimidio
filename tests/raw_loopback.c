/*
  raw_loopback.c — self-checking loopback harness for the raw-bytes API
                   (Slice 01, CoreMIDI). Modelled on examples/virtual.c.

  Build (macOS):
    cc tests/raw_loopback.c -framework CoreMIDI -framework CoreFoundation \
       -Wall -Wextra -o /tmp/raw_loopback

    NOTE: -framework CoreFoundation is required on the current Xcode CLT;
    -framework CoreMIDI alone does not re-export the CFString/CFRelease
    symbols that minimidio's CoreMIDI backend uses (see closing-report,
    F-8/F-14 amendment).

  Behaviour:
    Prints exactly one line "PASS T<n>" per passing ledger case to stdout,
    and exits non-zero (with a FAIL line on stderr) on the first failure.

  Topology (see slice-doc.md §D7): a single virtual *source* created with
  mm_out_open_virtual is opened as a raw input via mm_in_open_raw — the input
  connects to that source, so mm_out_send_raw on the source goes through the
  virtual MIDIReceived branch (the exact path the >256-byte SysEx must prove).
  A coverage case exercises mm_in_open_virtual_raw + mm_out_open (MIDISend
  branch). T6 opens a struct-mode input on the same source to prove the shared
  read proc still decodes structs.

  Intra-process virtual loopback is the one empirically-unproven assumption
  (slice-doc §D7). If no bytes ever arrive, the harness reports "no bytes
  received" distinctly from "wrong bytes", so the failure can be triaged as a
  loopback-topology amendment rather than a logic bug.
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define CTX_NAME       "mmraw-loopback"
#define CAP_MAX_MSGS   16
#define CAP_MSG_BYTES  (MM_SYSEX_BUF_SIZE + 8)
#define WAIT_MS        2000   /* generous: CoreMIDI delivers on its own thread */

/* ── Raw capture (byte buffers) ──────────────────────────────────────────── */
typedef struct {
    pthread_mutex_t lock;
    int    count;
    uint8_t bytes[CAP_MAX_MSGS][CAP_MSG_BYTES];
    size_t  len[CAP_MAX_MSGS];
} raw_capture;

static raw_capture g_raw;

static void raw_reset(raw_capture* c) {
    pthread_mutex_lock(&c->lock);
    c->count = 0;
    pthread_mutex_unlock(&c->lock);
}

static void on_raw(mm_device* dev, const uint8_t* data, size_t len,
                   double timestamp, void* ud) {
    (void)dev; (void)timestamp;
    raw_capture* c = (raw_capture*)ud;
    pthread_mutex_lock(&c->lock);
    if (c->count < CAP_MAX_MSGS && len <= CAP_MSG_BYTES) {
        memcpy(c->bytes[c->count], data, len);
        c->len[c->count] = len;
        c->count++;
    }
    pthread_mutex_unlock(&c->lock);
}

static int raw_count(raw_capture* c) {
    pthread_mutex_lock(&c->lock);
    int n = c->count;
    pthread_mutex_unlock(&c->lock);
    return n;
}

/* Wait until at least `want` messages have arrived, or timeout. */
static int wait_for_raw(raw_capture* c, int want, int timeout_ms) {
    int waited = 0;
    while (waited < timeout_ms) {
        if (raw_count(c) >= want) return 1;
        usleep(5000); waited += 5;
    }
    return raw_count(c) >= want;
}

/* ── Struct capture (for T6) ─────────────────────────────────────────────── */
typedef struct {
    pthread_mutex_t lock;
    int    count;
    mm_message msgs[CAP_MAX_MSGS];
} struct_capture;

static struct_capture g_struct;

static void on_struct(mm_device* dev, const mm_message* msg, void* ud) {
    (void)dev;
    struct_capture* c = (struct_capture*)ud;
    pthread_mutex_lock(&c->lock);
    if (c->count < CAP_MAX_MSGS) c->msgs[c->count++] = *msg;
    pthread_mutex_unlock(&c->lock);
}

static int struct_count(struct_capture* c) {
    pthread_mutex_lock(&c->lock);
    int n = c->count;
    pthread_mutex_unlock(&c->lock);
    return n;
}

static int wait_for_struct(struct_capture* c, int want, int timeout_ms) {
    int waited = 0;
    while (waited < timeout_ms) {
        if (struct_count(c) >= want) return 1;
        usleep(5000); waited += 5;
    }
    return struct_count(c) >= want;
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void die(const char* test, const char* why) {
    fprintf(stderr, "FAIL %s: %s\n", test, why);
    exit(1);
}

/* Find an endpoint named CTX_NAME in the input (is_in=1) or output list.
   Returns index, or -1. Retries to allow the OS to register the new endpoint. */
static int find_endpoint(mm_context* ctx, int is_in) {
    for (int attempt = 0; attempt < 40; attempt++) {  /* up to ~2s */
        uint32_t n = is_in ? mm_in_count(ctx) : mm_out_count(ctx);
        for (uint32_t i = 0; i < n; i++) {
            char name[128];
            mm_result r = is_in ? mm_in_name(ctx, i, name, sizeof(name))
                                : mm_out_name(ctx, i, name, sizeof(name));
            if (r == MM_SUCCESS && strstr(name, CTX_NAME) != NULL)
                return (int)i;
        }
        usleep(50000);
    }
    return -1;
}

static int bytes_eq(const uint8_t* a, const uint8_t* b, size_t n) {
    return memcmp(a, b, n) == 0;
}

int main(void) {
    pthread_mutex_init(&g_raw.lock, NULL);
    pthread_mutex_init(&g_struct.lock, NULL);

    mm_context ctx;
    mm_result r = mm_context_init(&ctx, CTX_NAME);
    if (r != MM_SUCCESS) die("init", mm_result_string(r));

    /* ── T5 / F-13: runtime caps advertise MM_CAP_RAW ── */
    if ((mm_context_caps(&ctx) & MM_CAP_RAW) == 0)
        die("T5", "MM_CAP_RAW not advertised by mm_context_caps");
    printf("PASS T5\n"); fflush(stdout);

    /* ── Primary path: virtual source + raw input connected to it ── */
    mm_device src;
    r = mm_out_open_virtual(&ctx, &src);
    if (r != MM_SUCCESS) die("setup", "mm_out_open_virtual failed");

    int in_idx = find_endpoint(&ctx, 1);
    if (in_idx < 0) die("setup", "virtual source did not appear in input list");

    mm_device in;
    r = mm_in_open_raw(&ctx, &in, (uint32_t)in_idx, on_raw, &g_raw);
    if (r != MM_SUCCESS) die("setup", "mm_in_open_raw failed");
    r = mm_in_start(&in);
    if (r != MM_SUCCESS) die("setup", "mm_in_start failed");
    usleep(200000);  /* let the connection settle */

    /* ── T1 / F-9: short channel message round-trips byte-exact ── */
    {
        const uint8_t msg[3] = { 0x90, 0x3C, 0x40 };
        raw_reset(&g_raw);
        if (mm_out_send_raw(&src, msg, sizeof(msg)) != MM_SUCCESS)
            die("T1", "mm_out_send_raw returned error");
        if (!wait_for_raw(&g_raw, 1, WAIT_MS)) die("T1", "no bytes received");
        if (g_raw.len[0] != 3 || !bytes_eq(g_raw.bytes[0], msg, 3))
            die("T1", "wrong bytes (expected 90 3C 40)");
        printf("PASS T1\n"); fflush(stdout);
    }

    /* ── T2 / F-10: note-on velocity 0 passes through unfolded ── */
    {
        const uint8_t msg[3] = { 0x90, 0x3C, 0x00 };
        raw_reset(&g_raw);
        if (mm_out_send_raw(&src, msg, sizeof(msg)) != MM_SUCCESS)
            die("T2", "mm_out_send_raw returned error");
        if (!wait_for_raw(&g_raw, 1, WAIT_MS)) die("T2", "no bytes received");
        /* Must stay 90 3C 00 — NOT folded to 80 .. */
        if (g_raw.len[0] != 3 || !bytes_eq(g_raw.bytes[0], msg, 3))
            die("T2", "velocity-0 not byte-exact (folded?)");
        if (g_raw.bytes[0][0] != 0x90)
            die("T2", "status changed (folded to note-off)");
        printf("PASS T2\n"); fflush(stdout);
    }

    /* ── T3 / F-11: >256-byte SysEx round-trips whole, one callback ── */
    {
        const size_t N = 300;            /* F0 + 298 data + F7 */
        uint8_t sysex[300];
        sysex[0] = 0xF0;
        for (size_t i = 1; i < N - 1; i++) sysex[i] = (uint8_t)(i & 0x7F);
        sysex[N - 1] = 0xF7;
        raw_reset(&g_raw);
        if (mm_out_send_raw(&src, sysex, N) != MM_SUCCESS)
            die("T3", "mm_out_send_raw returned error (size cap?)");
        if (!wait_for_raw(&g_raw, 1, WAIT_MS)) die("T3", "no bytes received");
        if (raw_count(&g_raw) != 1) die("T3", "expected exactly one callback");
        if (g_raw.len[0] != N) die("T3", "SysEx length mismatch (truncated?)");
        if (g_raw.bytes[0][0] != 0xF0 || g_raw.bytes[0][N - 1] != 0xF7)
            die("T3", "SysEx not intact F0..F7");
        if (!bytes_eq(g_raw.bytes[0], sysex, N))
            die("T3", "SysEx payload corrupted");
        printf("PASS T3\n"); fflush(stdout);
    }

    /* ── T4 / F-12: F8 mid-SysEx → own 1-byte callback AND clean SysEx ── */
    {
        /* F0 7E 00 <data> F8 <data> F7 */
        uint8_t buf[16]; size_t k = 0;
        buf[k++] = 0xF0; buf[k++] = 0x7E; buf[k++] = 0x00;
        buf[k++] = 0x01; buf[k++] = 0x02;
        buf[k++] = 0xF8;                 /* real-time clock injected mid-SysEx */
        buf[k++] = 0x03; buf[k++] = 0x04;
        buf[k++] = 0xF7;
        raw_reset(&g_raw);
        if (mm_out_send_raw(&src, buf, k) != MM_SUCCESS)
            die("T4", "mm_out_send_raw returned error");
        if (!wait_for_raw(&g_raw, 2, WAIT_MS))
            die("T4", "expected two callbacks (F8 + SysEx)");

        int saw_f8 = 0, saw_sysex_clean = 0;
        pthread_mutex_lock(&g_raw.lock);
        for (int m = 0; m < g_raw.count; m++) {
            if (g_raw.len[m] == 1 && g_raw.bytes[m][0] == 0xF8) saw_f8 = 1;
            if (g_raw.len[m] >= 2 && g_raw.bytes[m][0] == 0xF0 &&
                g_raw.bytes[m][g_raw.len[m] - 1] == 0xF7) {
                int has_f8 = 0;
                for (size_t b = 0; b < g_raw.len[m]; b++)
                    if (g_raw.bytes[m][b] == 0xF8) has_f8 = 1;
                if (!has_f8) saw_sysex_clean = 1;
            }
        }
        pthread_mutex_unlock(&g_raw.lock);
        if (!saw_f8) die("T4", "no standalone 1-byte F8 callback");
        if (!saw_sysex_clean) die("T4", "SysEx payload still contains F8");
        printf("PASS T4\n"); fflush(stdout);
    }

    /* ── Coverage: mm_in_open_virtual_raw + mm_out_open (MIDISend branch) ── */
    {
        mm_device vdst;
        r = mm_in_open_virtual_raw(&ctx, &vdst, on_raw, &g_raw);
        if (r != MM_SUCCESS) die("coverage", "mm_in_open_virtual_raw failed");
        r = mm_in_start(&vdst);
        if (r != MM_SUCCESS) die("coverage", "mm_in_start (virtual raw) failed");

        int out_idx = find_endpoint(&ctx, 0);
        if (out_idx < 0) die("coverage", "virtual destination not in output list");
        mm_device out;
        r = mm_out_open(&ctx, &out, (uint32_t)out_idx);
        if (r != MM_SUCCESS) die("coverage", "mm_out_open failed");
        usleep(200000);

        const uint8_t msg[3] = { 0xB0, 0x07, 0x7F };  /* CC volume */
        raw_reset(&g_raw);
        if (mm_out_send_raw(&out, msg, sizeof(msg)) != MM_SUCCESS)
            die("coverage", "mm_out_send_raw (real out) returned error");
        if (!wait_for_raw(&g_raw, 1, WAIT_MS))
            die("coverage", "no bytes received via virtual destination");
        if (g_raw.len[0] != 3 || !bytes_eq(g_raw.bytes[0], msg, 3))
            die("coverage", "wrong bytes via mm_in_open_virtual_raw path");
        fprintf(stderr, "[coverage] mm_in_open_virtual_raw + mm_out_open round-trip OK\n");

        mm_out_close(&out);
        mm_in_stop(&vdst);
        mm_in_close(&vdst);
    }

    /* ── T6 / F-14: struct-mode input on the SAME source still decodes ── */
    {
        int sidx = find_endpoint(&ctx, 1);   /* still the virtual source */
        if (sidx < 0) die("T6", "virtual source vanished from input list");
        mm_device in_struct;
        r = mm_in_open(&ctx, &in_struct, (uint32_t)sidx, on_struct, &g_struct);
        if (r != MM_SUCCESS) die("T6", "mm_in_open (struct) failed");
        r = mm_in_start(&in_struct);
        if (r != MM_SUCCESS) die("T6", "mm_in_start (struct) failed");
        usleep(200000);

        mm_message note;
        memset(&note, 0, sizeof(note));
        note.type = MM_NOTE_ON; note.channel = 5;
        note.data[0] = 0x40; note.data[1] = 0x65;

        pthread_mutex_lock(&g_struct.lock); g_struct.count = 0; pthread_mutex_unlock(&g_struct.lock);
        if (mm_out_send(&src, &note) != MM_SUCCESS)
            die("T6", "mm_out_send (struct) returned error");
        if (!wait_for_struct(&g_struct, 1, WAIT_MS))
            die("T6", "struct callback received nothing (shared read proc broken?)");
        mm_message got = g_struct.msgs[0];
        if (got.type != MM_NOTE_ON) die("T6", "struct decode wrong type");
        if (got.channel != 5 || got.data[0] != 0x40 || got.data[1] != 0x65)
            die("T6", "struct decode wrong data");
        printf("PASS T6\n"); fflush(stdout);

        mm_in_stop(&in_struct);
        mm_in_close(&in_struct);
    }

    /* ── Teardown ── */
    mm_in_stop(&in);
    mm_in_close(&in);
    mm_out_close(&src);
    mm_context_uninit(&ctx);
    return 0;
}
