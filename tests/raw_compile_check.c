/*
  raw_compile_check.c — type-checks the raw entry points on every backend.

  WinMM and WebMIDI have no in-loop runtime test path (no virtual ports / needs a
  browser), so their acceptance bar is a clean compile of the new call sites. This
  tiny TU references all three raw functions and the MM_CAP_RAW bit so the
  cross/emcc builds exercise them; nothing actually runs (the calls are guarded by
  `if (0)`).

  Build (any backend):
    macOS:   cc  tests/raw_compile_check.c -framework CoreMIDI -framework CoreFoundation -o /tmp/cm
    Linux:   cc  tests/raw_compile_check.c -lasound -lpthread -o /tmp/al
    Windows: zig cc tests/raw_compile_check.c -target x86_64-windows-gnu -lwinmm -o /tmp/wm.exe
    Web:     emcc tests/raw_compile_check.c -sASYNCIFY -o /tmp/web.js
*/

#define MINIMIDIO_IMPLEMENTATION
#include "../minimidio.h"

int main(void) {
    mm_context ctx;
    mm_device  dev;
    mm_raw_callback cb = 0;
    uint32_t caps = MM_CAP_RAW;          /* reference the capability bit */
    uint8_t  msg[1] = { 0x90 };

    if (0) {                              /* type-check the call sites; never run */
        mm_in_open_raw(&ctx, &dev, 0, cb, (void*)0);
        mm_in_open_virtual_raw(&ctx, &dev, cb, (void*)0);
        mm_out_send_raw(&dev, msg, sizeof(msg));
        caps |= mm_context_caps(&ctx);
    }

    (void)ctx; (void)dev; (void)cb; (void)caps; (void)msg;
    return 0;
}
