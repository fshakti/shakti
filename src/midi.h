#ifndef SHAKTI_MIDI_H
#define SHAKTI_MIDI_H

#include "shakti.h"

#ifdef __cplusplus
extern "C" {
#endif

V *bi_midi_open(V **a, int n);
V *bi_midi_close(V **a, int n);
V *bi_midi_alive(V **a, int n);
V *bi_midi_backend(V **a, int n);
V *bi_midi_list(V **a, int n);
V *bi_midi_connect(V **a, int n);
V *bi_midi_disconnect(V **a, int n);
V *bi_midi_note_on(V **a, int n);
V *bi_midi_note_off(V **a, int n);
V *bi_midi_cc(V **a, int n);
V *bi_midi_program(V **a, int n);
V *bi_midi_raw(V **a, int n);
V *bi_midi_poll(V **a, int n);

void midi_decode_bytes(const unsigned char *data, int len);

#ifdef __cplusplus
}
#endif

#endif
