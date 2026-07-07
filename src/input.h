#ifndef SHAKTI_INPUT_H
#define SHAKTI_INPUT_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_STREAM_LINE 0
#define INPUT_STREAM_RAW  1
#define INPUT_STREAM_KEY  2

#define INPUT_WAIT_FOREVER (-1)

void input_hub_init(void);
void input_hub_shutdown(void);

V *v_input_stream(int mode, const char *prompt);
V *input_poll_ms(int ms);
V *input_wait_ms(int64_t ms);
V *input_readline(const char *prompt);
V *input_stream_next(V *stream);

int input_hub_read_char(char *out);
char *input_hub_readline_repl(const char *prompt);

void input_hub_inject_key(int code, int modifiers, const char *utf8, int down);
void input_hub_inject_mouse(int x, int y, int buttons);
void input_hub_inject_wheel(int delta);

int input_own_gui(void);
void input_set_own_gui(int on);

double input_get_x(void);
double input_get_y(void);
double input_get_wheel(void);
void input_set_x(double x);
void input_set_y(double y);
void input_set_wheel(double w);

int input_get_hz(void);
void input_set_hz(int hz);

V *input_get_qwerty(void);
void input_qwerty_reload(void);

V *input_event_dict(int code, int modifiers, const char *utf8, const char *kind);

void input_hub_pump(int timeout_ms);
int input_hub_take_stop(void);

#ifdef __cplusplus
}
#endif

#endif
