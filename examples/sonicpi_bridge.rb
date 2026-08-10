# Sonic Pi bridge — paste into Sonic Pi and Run before driving from Shakti.
# Shakti sends OSC cues to 127.0.0.1:4560; Sonic Pi listens by default.

live_loop :shakti_play do
  use_real_time
  note, amp, sustain = sync "/osc*/shakti/play"
  play note, amp: amp, sustain: sustain
end

live_loop :shakti_synth do
  use_real_time
  name, note, amp, sustain = sync "/osc*/shakti/synth"
  synth name.to_sym, note: note, amp: amp, sustain: sustain
end

live_loop :shakti_bpm do
  use_real_time
  tempo = sync "/osc*/shakti/bpm"
  use_bpm tempo
end

live_loop :shakti_stop do
  use_real_time
  sync "/osc*/shakti/stop"
  stop
end
