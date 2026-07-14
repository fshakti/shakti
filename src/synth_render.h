#ifndef SHAKTI_SYNTH_RENDER_H
#define SHAKTI_SYNTH_RENDER_H

#include "synth.h"
#include "synth_ui.h"

#define SYNTH_ROWS 8
#define SYNTH_DRUM_ROWS 4
#define SYNTH_PADS 16
#define SYNTH_MAX_KEYS 128
#define SYNTH_KNOBS 8
#define SYNTH_ROW_LABEL_MAX 8

typedef struct SynthLayout {
    UiRect transport;
    UiRect metro_btn;
    UiRect metro_sound_btn;
    UiRect mute_btn;
    UiRect loop_rec_btn;
    UiRect loop_play_btn;
    UiRect loop_clr_btn;
    UiRect step_minus;
    UiRect step_box;
    UiRect step_plus;
    UiRect knobs[SYNTH_KNOBS];
    UiRect bpm_readout;
    UiRect vu_meter;
    UiRect seq_panel;
    UiRect seq_row[SYNTH_ROWS];
    UiRect seq_step[SYNTH_ROWS][SYNTH_MAX_STEPS];
    UiRect pad_panel;
    UiRect pads[SYNTH_PADS];
    UiRect spectrum_panel;
    UiRect waveform_panel;
    UiRect ribbon_panel;
    UiRect ribbon_track;
    UiRect keys_panel;
    UiRect keys[SYNTH_MAX_KEYS];
} SynthLayout;

typedef struct SynthRenderState {
    const SynthLayout *L;
    int design_w, design_h;
    int win_w, win_h;
    float ui_scale;
    int off_x, off_y;

    float knobs[SYNTH_KNOBS];
    const char *const *knob_lbl;

    uint64_t seq[SYNTH_ROWS];
    int step_len, step_pos, playing;
    int metro_on, metro_flash, metro_sound, mute;

    int loop_rec, loop_play, loop_has, loop_flash;
    int sample_loaded;
    char sample_name[48];

    int pad_down[SYNTH_PADS];
    const char *const *pad_lbl;

    int key_down[SYNTH_MAX_KEYS];
    int synth_keys, base_note;

    float ribbon;
    int bpm;
    float vu;
    int viz;

    int spectrum_n;
    float spectrum[SYNTH_UI_SPECTRUM_BINS];
    int waveform_n;
    float waveform[SYNTH_UI_WAVEFORM_LEN];
} SynthRenderState;

const char *synth_row_label(int row);

#ifdef SYNTH_HAVE_GL
int synth_gl_create_window(void *dpy, int scr, const char *title, int w, int h,
                           unsigned long *out_win);
void synth_gl_resize(int w, int h);
void synth_gl_render(const SynthRenderState *s);
void synth_gl_swap(void);
void synth_gl_shutdown(void);
#endif

#endif
