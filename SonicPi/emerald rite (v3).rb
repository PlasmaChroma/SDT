# =========================================================
# SDT — Emerald Rite in Dub (Sonic Pi)
# MIDI / AUDIO independent toggles + port-safe output
#
# Live toggles:
#   set :send_midi, true/false     # send MIDI out
#   set :play_audio, true/false    # allow Sonic Pi audio
#   set :force_port, true/false
#   set :debug_heartbeat, true/false
#   set :send_cc, true/false
#   set :panic, true               # All Notes Off (one-shot)
# =========================================================

use_bpm 72
use_debug false

# -------------------------
# Core toggles
# -------------------------
set :send_midi, true
set :play_audio, false            # <-- NEW: set true to hear Sonic Pi too
set :force_port, false            # keep false unless you must target a port
set :midi_port, "loopMIDI Port"   # only used if :force_port == true
set :debug_heartbeat, false

# -------------------------
# MIDI channels
# -------------------------
set :ch_drums, 10
set :ch_bass,  1
set :ch_skank, 2
set :ch_halo,  3
set :ch_phrase,4

# -------------------------
# CC + knobs
# -------------------------
set :send_cc, false      # enable CC output if mapped in Ableton
set :skank_cutoff, 95
set :bass_cutoff,  75
set :dub_mix,      0.60
set :space,        0.85
set :damp,         0.55
set :swing,        0.03

BAR = 4.0

# =========================================================
# Helpers
# =========================================================

define :sw do |t|
  (spread 1, 2).tick ? sleep(t + get(:swing)) : sleep(t)
end

define :dub_fx do |&block|
  with_fx :reverb, room: get(:space), damp: get(:damp), mix: 0.45 do
    with_fx :echo, phase: 0.75, decay: 6, mix: get(:dub_mix) do
      block.call
    end
  end
end

define :with_midi_port do |&block|
  if get(:force_port)
    use_midi_defaults port: get(:midi_port)
  else
    use_midi_defaults
  end
  block.call
end

define :midi_len do |note, len: 0.2, vel: 90, ch: 1|
  return unless get(:send_midi)
  with_midi_port do
    in_thread do
      midi_note_on note, velocity: vel, channel: ch
      sleep len
      midi_note_off note, channel: ch
    end
  end
end

define :midi_chord_len do |notes, len: 0.2, vel: 85, ch: 2|
  return unless get(:send_midi)
  with_midi_port do
    in_thread do
      notes.each { |n| midi_note_on n, velocity: vel, channel: ch }
      sleep len
      notes.each { |n| midi_note_off n, channel: ch }
    end
  end
end

define :send_cc_msg do |cc_num, value, ch: 1|
  return unless (get(:send_midi) && get(:send_cc))
  v = [[value.to_i, 0].max, 127].min
  with_midi_port { midi_cc cc_num, v, channel: ch }
end

define :panic_all_notes_off do
  return unless get(:send_midi)
  with_midi_port do
    [get(:ch_bass), get(:ch_skank), get(:ch_halo),
     get(:ch_phrase), get(:ch_drums)].each do |ch|
      midi_cc 123, 0, channel: ch
    end
  end
end

# =========================================================
# Conductor
# =========================================================

set :section, :intro
set :bar_count, 0

live_loop :conductor do
  if get(:panic)
    panic_all_notes_off
    set :panic, false
  end

  bc = get(:bar_count) + 1
  set :bar_count, bc

  set :section,
    case bc
    when 1..32     then :intro
    when 33..96    then :groove
    when 97..128   then :axis
    when 129..160  then :edict
    else :dissolve
    end

  sleep BAR
end

# =========================================================
# Debug heartbeat (optional)
# =========================================================

live_loop :heartbeat do
  if get(:debug_heartbeat)
    midi_len 84, len: 0.08, vel: 50, ch: get(:ch_phrase)
    if get(:play_audio)
      dub_fx { sample :elec_tick, amp: 0.08 } # tiny audible “I’m alive”
    end
  end
  sleep 1
end

# =========================================================
# DRUMS (ch10)
# =========================================================

live_loop :kick do
  s = get(:section)
  ch = get(:ch_drums)

  if [:groove, :axis, :edict].include?(s)
    midi_len 36, len: 0.06, vel: 100, ch: ch
    dub_fx { sample :bd_fat, amp: 1.6 } if get(:play_audio)
    sleep 3
    midi_len 36, len: 0.06, vel: 112, ch: ch
    dub_fx { sample :bd_fat, amp: 1.9 } if get(:play_audio)
    sleep 1
  else
    sleep 4
  end
end

live_loop :snare do
  s = get(:section)
  ch = get(:ch_drums)

  if [:groove, :axis, :edict].include?(s)
    sleep 2
    midi_len 38, len: 0.05, vel: 92, ch: ch
    dub_fx { sample :sn_dolf, amp: 0.9 } if get(:play_audio)
    sleep 2
  else
    sleep 4
  end
end

live_loop :hats do
  s = get(:section)
  ch = get(:ch_drums)

  if [:groove, :axis].include?(s)
    8.times do
      midi_len 42, len: 0.03, vel: 50, ch: ch
      dub_fx { sample :drum_cymbal_closed, amp: 0.20, rate: 1.25 } if get(:play_audio)
      sw 0.5
    end
  elsif s == :edict
    4.times do
      midi_len 42, len: 0.03, vel: 40, ch: ch
      dub_fx { sample :drum_cymbal_closed, amp: 0.12, rate: 1.15 } if get(:play_audio)
      sw 1.0
    end
  else
    sleep 4
  end
end

# =========================================================
# SKANK (ch2)
# =========================================================

skank_chords = [
  chord(:e3, :minor7),
  chord(:g3, :minor7),
  chord(:a3, :sus4),
  chord(:b2, :minor7)
].ring

live_loop :skank do
  s = get(:section)
  ch = get(:ch_skank)

  if [:groove, :axis, :edict].include?(s)
    send_cc_msg 74, get(:skank_cutoff), ch: ch

    sleep 0.5
    midi_chord_len skank_chords.tick, len: 0.16, vel: 76, ch: ch
    if get(:play_audio)
      with_fx :lpf, cutoff: get(:skank_cutoff) do
        dub_fx do
          use_synth :pluck
          play_chord skank_chords.look, amp: 0.45, release: 0.18
        end
      end
    end

    sleep 2.5
    midi_chord_len skank_chords.tick, len: 0.16, vel: 82, ch: ch
    if get(:play_audio)
      with_fx :lpf, cutoff: get(:skank_cutoff) do
        dub_fx do
          use_synth :pluck
          play_chord skank_chords.look, amp: 0.50, release: 0.18
        end
      end
    end
    sleep 1
  else
    sleep 4
  end
end

# =========================================================
# BASS – Foundation Larynx (ch1)
# =========================================================

bassline = (ring :e1, :e1, :g1, :b0, :a0, :e1, :g1, :d1)

live_loop :sub_bass do
  s = get(:section)
  ch = get(:ch_bass)

  if [:groove, :axis, :edict].include?(s)
    send_cc_msg 74, get(:bass_cutoff), ch: ch
    n = bassline.tick

    midi_len n, len: 0.95, vel: 105, ch: ch

    if get(:play_audio)
      dub_fx do
        use_synth :fm
        with_fx :lpf, cutoff: get(:bass_cutoff) do
          play n, amp: 1.2, attack: 0.01, sustain: 0.85, release: 0.2, depth: 2, divisor: 2
        end
      end
    end

    sleep 1
  else
    sleep 1
  end
end

# =========================================================
# HALO – Spire Larynx (ch3)
# =========================================================

live_loop :spire_halo do
  s = get(:section)
  ch = get(:ch_halo)

  if [:intro, :axis, :dissolve].include?(s)
    n = (ring :e5, :g5, :b5, :d6).choose
    midi_len n, len: 1.0, vel: 40, ch: ch

    if get(:play_audio)
      dub_fx do
        use_synth :hollow
        play n, amp: 0.18, attack: 0.3, release: 1.8
      end
    end

    sleep 4
  else
    sleep 4
  end
end

# =========================================================
# SDT TABLET PHRASES (ch4)
# =========================================================

define :utter do |sym|
  ch = get(:ch_phrase)

  case sym
  when :alethe
    midi_len :e4, len: 1.0, vel: 44, ch: ch
    sleep 1
    midi_len :b3, len: 0.9, vel: 40, ch: ch
    if get(:play_audio)
      dub_fx { use_synth :prophet; play :e4, amp: 0.22, attack: 0.25, sustain: 0.6, release: 1.4 }
    end
  when :mer_ra
    midi_len :e3, len: 0.25, vel: 52, ch: ch
    sleep 0.25
    midi_len :g3, len: 0.5, vel: 50, ch: ch
    if get(:play_audio)
      dub_fx { use_synth :prophet; play :g3, amp: 0.18, release: 0.9 }
    end
  when :nyathe
    midi_len :d5, len: 1.0, vel: 38, ch: ch
    if get(:play_audio)
      dub_fx { use_synth :prophet; play :d5, amp: 0.14, release: 1.3 }
    end
  when :sur
    midi_len :e2, len: 1.0, vel: 52, ch: ch
    if get(:play_audio)
      dub_fx { use_synth :prophet; play :e2, amp: 0.16, release: 1.0 }
    end
  when :fal_ta
    midi_len :e2, len: 0.9, vel: 56, ch: ch
    sleep 0.25
    midi_len :b1, len: 0.9, vel: 54, ch: ch
    if get(:play_audio)
      dub_fx { use_synth :prophet; play :b1, amp: 0.16, release: 1.0 }
    end
  end
end

live_loop :tablet_phrases do
  case get(:section)
  when :intro
    utter :alethe; sleep 8
  when :axis
    utter :nyathe; sleep 2
    utter :sur;    sleep 2
    utter :fal_ta; sleep 4
  when :edict
    utter :nyathe; sleep 1
    utter :sur;    sleep 3
  else
    sleep 4
  end
end
