#ifndef SHAKTI_SYNTH_H
#define SHAKTI_SYNTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_MAX_STEPS 64
#define SYNTH_DEFAULT_STEPS 16

int synth_open(char *err, size_t err_cap);
void synth_close(void);
int synth_alive(void);
int synth_tick(char *err, size_t err_cap);
int synth_set_steps(int n, char *err, size_t err_cap);
int synth_steps(void);
int synth_set_metro(int on, char *err, size_t err_cap);
int synth_metro_on(void);
int synth_set_metro_sound(int sound, char *err, size_t err_cap);
int synth_metro_sound(void);

int synth_set_mute(int mute, char *err, size_t err_cap);
int synth_mute_on(void);

int synth_note_on(int note, float vel, char *err, size_t err_cap);
int synth_note_off(int note, char *err, size_t err_cap);
int synth_set_bpm(float bpm, char *err, size_t err_cap);
float synth_get_bpm(void);
int synth_set_level(float level, char *err, size_t err_cap);
float synth_get_level(void);
int synth_set_cutoff(float cutoff, char *err, size_t err_cap);
float synth_get_cutoff(void);
int synth_set_reso(float reso, char *err, size_t err_cap);
float synth_get_reso(void);
int synth_set_seq_row(int row, uint64_t mask, char *err, size_t err_cap);
int synth_play(int on, char *err, size_t err_cap);
int synth_playing(void);
int synth_mouse_press(int x, int y, char *err, size_t err_cap);
int synth_mouse_release(int x, int y, char *err, size_t err_cap);
int synth_set_viz(int mode, char *err, size_t err_cap);
int synth_viz_mode(void);

int synth_load_sample(const char *path, char *err, size_t err_cap);
int synth_sample_loaded(void);
const char *synth_sample_name(void);

int synth_set_row_note(int row, int midi, char *err, size_t err_cap);
int synth_row_note_get(int row);

int synth_looper_rec(int on, char *err, size_t err_cap);
int synth_looper_play(int on, char *err, size_t err_cap);
int synth_looper_clear(char *err, size_t err_cap);
int synth_looper_overdub(int on, char *err, size_t err_cap);
int synth_looper_rec_on(void);
int synth_looper_play_on(void);
int synth_looper_has_loop(void);

#ifdef __cplusplus
}
#endif

#endif
