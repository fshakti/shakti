#ifndef SHAKTI_SYNTH_UI_H
#define SHAKTI_SYNTH_UI_H

#include <stdint.h>

#define SYNTH_UI_DESIGN_W 960
#define SYNTH_UI_DESIGN_H 540
#define SYNTH_UI_MAX_CMDS 4096
#define SYNTH_UI_TEXT_MAX 16
#define SYNTH_UI_SPECTRUM_BINS 64
#define SYNTH_UI_WAVEFORM_LEN 256

typedef struct UiRect {
    int x, y, w, h;
} UiRect;

typedef enum {
    UI_CHASSIS = 1,
    UI_HEADER_DECK,
    UI_PANEL_RECESSED,
    UI_BTN,
    UI_KNOB,
    UI_LED_STEP,
    UI_PAD,
    UI_PIANO_KEY,
    UI_RIBBON,
    UI_LABEL,
    UI_NUM,
    UI_SPECTRUM,
    UI_WAVEFORM,
    UI_VU_METER
} UiCmdKind;

typedef struct UiCmd {
    UiCmdKind kind;
    UiRect r;
    uint32_t c0, c1, c2, c3;
    float f0, f1;
    int idx0, idx1, idx2, idx3;
    char text[SYNTH_UI_TEXT_MAX];
} UiCmd;

typedef enum {
    SYNTH_VIZ_NONE = 0,
    SYNTH_VIZ_SPECTRUM = 1,
    SYNTH_VIZ_WAVEFORM = 2,
    SYNTH_VIZ_BOTH = 3
} SynthVizMode;

void synth_ui_begin(void);
const UiCmd *synth_ui_cmds(int *n);
void synth_ui_emit_chassis(void);
void synth_ui_emit_header_deck(UiRect r);
void synth_ui_emit_panel(UiRect r);
void synth_ui_emit_btn(UiRect r, uint32_t hi, uint32_t lo, int lit, int pressed, const char *txt);
void synth_ui_emit_knob(UiRect r, float val, const char *label);
void synth_ui_emit_led_step(UiRect r, int on, int playhead);
void synth_ui_emit_pad(UiRect r, int pressed, const char *lbl);
void synth_ui_emit_piano_key(UiRect r, int down, int style);
void synth_ui_emit_ribbon(UiRect track, float val);
void synth_ui_emit_label(int x, int y, const char *s, uint32_t c);
void synth_ui_emit_num(int x, int y, int n, uint32_t c);
void synth_ui_emit_spectrum(UiRect r, const float *mags, int n);
void synth_ui_emit_waveform(UiRect r, const float *samples, int n);
void synth_ui_emit_vu(UiRect r, float level);

#ifdef __cplusplus
extern "C" {
#endif

void synth_ui_flush_cpu(const UiCmd *cmds, int n, uint32_t *fb, int w, int h);
void synth_ui_flush_text_overlay(const UiCmd *cmds, int n, uint32_t *fb, int w, int h);

#ifdef __cplusplus
}
#endif

void synth_ui_flush(const UiCmd *cmds, int n, uint32_t *fb, int w, int h);
void synth_ui_flush_text_overlay(const UiCmd *cmds, int n, uint32_t *fb, int w, int h);

void synth_ui_push_audio_samples(const float *mono, int n);
void synth_ui_set_viz_mode(int mode);
int synth_ui_viz_mode(void);
void synth_ui_get_spectrum(float *out, int *n);
void synth_ui_get_waveform(float *out, int *n);
float synth_ui_vu_level(void);

#endif
