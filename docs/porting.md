# Putting Purple's face on other ESP32 boards

The face is three header files in `board/sensecap-watcher/purple_face/` and
they don't know anything about the Watcher:

- `face_engine.h` draws the face into any RGB565 screen, in strips, and hands
  each finished strip to a function you give it
- `face_controller.h` makes it alive (blinks, gaze, breathing, moods) and maps
  XiaoZhi's emotions and states to faces
- `faces_data.h` + `FaceTypes.h` hold the 153 faces

`friends_lan.h` (the Wi-Fi games) is optional.

You need an ESP32-S3 (or similar) with PSRAM: the engine keeps its working
buffers in PSRAM (about 120 KB plus the frame) so the internal RAM stays free
for Wi-Fi and audio.

## Screen size

Set the size before including the engine. The face is scaled to fit the
shorter side:

```cpp
#define PURPLE_FACE_W 320
#define PURPLE_FACE_H 240
#include "purple_face/face_controller.h"
```

## The minimum: your own ESP32 project

```cpp
#include "purple_face/face_controller.h"

static void push_strip(void *ctx, int x, int y, int w, int h, const uint16_t *px) {
    // send w x h RGB565 pixels to the screen at (x, y)
    // e.g. esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, px);
    // (some panels want the bytes swapped first)
}

purple::FaceRenderer face(push_strip, nullptr);
purple::FaceController alive(face);

void face_task(void *) {
    face.begin();
    alive.begin();
    int64_t last = esp_timer_get_time();
    while (true) {
        int64_t now = esp_timer_get_time();
        alive.update((now - last) / 1e6f);   // seconds since the last frame
        last = now;
        face.render();                       // only redraws what changed
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
```

Then tell it what's going on:

```cpp
alive.setMode(purple::Mode::Listening);   // Idle, Listening, Speaking, Connecting, ...
alive.emotion("happy");                   // XiaoZhi emotion names, or any Purple face id
alive.reactFace("love", 2.0f);            // a Purple face for 2 seconds
alive.knob(+1);                           // glance right
```

`faces_data.h` lists every face id (`happy`, `sleepy`, `cool`, `party`, ...).

## Another XiaoZhi board

Look at `sensecap_watcher.cc`, search for `Purple`. The pattern:

1. Subclass the board's display class (`SpiLcdDisplay` or whatever it uses).
2. In `SetupUI()`, after the parent's, create a full-screen `lv_image` from a
   PSRAM RGB565 buffer, put it just above the background
   (`lv_obj_move_to_index(img, 1)`) and hide the emoji box.
3. Override `SetEmotion()` to pass the emotion to the controller instead of
   showing an emoji.
4. Start a render task: read `Application::GetInstance().GetDeviceState()`,
   call `update()` and `render()`, copy the changed rectangle into the image
   buffer under the display lock, and `lv_obj_invalidate_area()` it.

Things I learned on the Watcher:

- Pin the face task to the core LVGL **doesn't** use, at priority 1. Sharing a
  core with LVGL starves the idle task and the watchdog complains.
- Put all the face's buffers and the task stack in PSRAM. When I put them in
  internal RAM, the speech model couldn't load and the board crashed.
- Always sleep a few ms per frame so the idle task runs.
- On a round screen, keep text bars transparent: LVGL blending a 50 %
  background over the face every frame cost more than drawing the face.

## Making it faster or slower

In `face_engine.h`:

- `GLOW_DIV` (8): the glow is computed on a grid this many pixels apart. Bigger
  is faster and softer.
- `BAND_H` (16): strip height. Smaller strips use less RAM.
- In `render()` the glow is rebuilt every other frame; every frame looks the
  same and costs more.
