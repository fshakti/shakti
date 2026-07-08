#ifndef SHAKTI_SYNTH_PLATFORM_H
#define SHAKTI_SYNTH_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* Platform lifecycle (Linux in synth.c, macOS in synth_mac.m). */
int synth_platform_init(char *err, size_t cap);
void synth_platform_shutdown(void);
int synth_platform_poll(int *cfg_w, int *cfg_h, int *cfg_count);
void synth_platform_present(void);
void synth_platform_request_maximize(void);

int synth_audio_start(char *err, size_t cap);
void synth_audio_stop(void);

/* Core hooks implemented in synth.c (shared UI/DSP). */
void synth_core_handle_click(int sx, int sy, int down);
void synth_core_handle_motion(int sx, int sy);
void synth_core_handle_release(int sx, int sy, int down);
void synth_core_handle_key(int key, int down);
void synth_core_ui_draw(void);
void synth_core_present_scale(void);
int synth_core_fb_design_init(void);
int synth_core_fb_resize(int w, int h);
void synth_core_render(float *out, int n);
void synth_core_set_alive(int alive);
int synth_core_is_alive(void);
int synth_core_want_maximize(void);
void synth_core_clear_want_maximize(void);
int synth_core_bump_maximize_tries(void);
int synth_core_get_maximize_tries(void);
void synth_core_audio_lock(void);
void synth_core_audio_unlock(void);
int synth_core_audio_running(void);
void synth_core_set_audio_run(int on);
uint32_t *synth_core_present_pixels(void);
int synth_core_present_width(void);
int synth_core_present_height(void);
int synth_core_dirty(void);
void synth_core_clear_dirty(void);
void synth_core_mark_dirty(void);

#endif
