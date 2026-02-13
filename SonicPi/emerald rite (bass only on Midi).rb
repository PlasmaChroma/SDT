# =========================================================
#  SDT — Emerald Rite in Dub (Sonic Pi)
#  “Mer-ra Trúen Los’Zuur-ka ↕ Fal-ta” (As Above ↕ As Below)
#
#  Design:
#   - Foundation-Larynx (FDLX): sub-bass + room weight (Below)
#   - Spire-Larynx (SPLX): high shimmer halo (Above)
#   - Abyssal Edict Register (AER): sparse, hard-stopped stabs
#   - Alethe: field-state intro/outro (breath + truth-space)
#
#  Controls (live tweak while running):
#   set :skank_cutoff, 90
#   set :bass_cutoff, 70
#   set :dub_mix, 0.55
# =========================================================

use_bpm 72
use_debug false

# --- User-tweakable knobs ---
set :skank_cutoff, 95
set :bass_cutoff, 75
set :dub_mix, 0.60          # overall echo mix feel
set :space, 0.85            # reverb room size
set :damp, 0.55             # reverb damp
set :swing, 0.03            # micro-swing (seconds)

# --- MIDI (Bass only) ---
# Tip: run `puts midi_ports` in Sonic Pi to see your available MIDI port names.
#
# By default this script will NOT force a specific port name (helpful if your MIDI->CV interface
# changes names, you hot-swap devices, or you want to use Sonic Pi's default routing).
# If you want to pin it to one device, set :bass_midi_force_port to true and fill in :bass_midi_port.
set :bass_midi_force_port, false
set :bass_midi_port, "CHANGE_ME_MIDI_PORT"  # only used when :bass_midi_force_port is true
set :bass_midi_channel, 1                   # 1–16
set :bass_midi_vel, 95                      # base velocity (0–127)
set :bass_midi_vel_edict, 110               # heavier hits during :edict
set :bass_midi_vel_dissolve, 70             # fade-out velocity

# --- Mod Wheel (CC#1) for Modular Modulation ---
# CC#1 values are 0–127. Map this to PWM / wavefold / FM index / filter cutoff / Warps algo/level, etc.
# Keep it enabled, and the bass loop will "breathe" mod-wheel movement during held notes.
set :bass_cc1_enabled, true
set :bass_cc1_cc, 1                # Mod Wheel (CC#1)
set :bass_cc1_jitter, 2            # tiny randomness per step (0..8 is sane); set 0 for perfectly repeatable

# --- Global timing ---
BAR = 4.0   # 4 beats

# --- Simple helpers ---
define :sw do |t|
  # micro swing: nudge every other hit
  (spread 1, 2).tick ? sleep(t + get(:swing)) : sleep(t)
end

define :dub_fx do |&block|
  with_fx :reverb, room: get(:space), damp: get(:damp), mix: 0.45 do
    with_fx :echo, phase: 0.75, decay: 6, mix: get(:dub_mix) do
      with_fx :lpf, cutoff: 110 do
        block.call
      end
    end
  end
end

# --- Conductor / Arrangement ---
# 32 bars intro (~1:46), 64 bars groove, 32 bars axis, 32 bars edict, then dissolve
set :section, :intro
set :bar_count, 0

live_loop :conductor do
  bc = get(:bar_count) + 1
  set :bar_count, bc
  
  if bc == 1
    set :section, :intro
  elsif bc == 33
    set :section, :groove
  elsif bc == 97
    set :section, :axis
  elsif bc == 129
    set :section, :edict
  elsif bc == 161
    set :section, :dissolve
  end
  
  sleep BAR
end

# =========================================================
#  DRUMS — One-drop dub pulse
# =========================================================

live_loop :kick do
  s = get(:section)
  
  if [:groove, :axis, :edict].include?(s)
    # One-drop-ish: emphasize beat 3, keep it patient
    dub_fx do
      sample :bd_fat, amp: 1.6
    end
    sleep 2
    sleep 1
    dub_fx do
      sample :bd_fat, amp: (s == :edict ? 1.9 : 1.4)
    end
    sleep 1
  elsif s == :dissolve
    # fade to sparse heartbeats
    dub_fx do
      sample :bd_fat, amp: 0.9
    end
    sleep 4
  else
    sleep 4
  end
end

live_loop :snare do
  s = get(:section)
  
  if [:groove, :axis, :edict].include?(s)
    sleep 2
    dub_fx do
      # snare as a distant ritual stamp
      sample :sn_dolf, amp: (s == :edict ? 1.2 : 0.9), rate: 0.9
    end
    sleep 2
  elsif s == :dissolve
    sleep 8
  else
    sleep 4
  end
end

live_loop :hats do
  s = get(:section)
  
  if [:groove, :axis].include?(s)
    8.times do
      dub_fx do
        sample :drum_cymbal_closed, amp: 0.25, rate: 1.3
      end
      sw 0.5
    end
  elsif s == :edict
    # reduce hats: more law, less chatter
    4.times do
      dub_fx do
        sample :drum_cymbal_closed, amp: 0.16, rate: 1.2
      end
      sw 1.0
    end
  else
    sleep 4
  end
end

# =========================================================
#  SKANK — Offbeat chord stabs (dub guitar analogue)
# =========================================================

# Emerald-ish harmony palette (minor + suspended tension)
skank_chords = [
  chord(:e3, :minor7),
  chord(:g3, :minor7),
  chord(:a3, :sus4),
  chord(:b2, :minor7)
].ring

live_loop :skank do
  s = get(:section)
  
  if [:groove, :axis, :edict].include?(s)
    with_fx :lpf, cutoff: get(:skank_cutoff) do
      with_fx :slicer, phase: 0.25, mix: 0.15 do
        dub_fx do
          use_synth :pluck
          use_synth_defaults amp: (s == :edict ? 0.55 : 0.45), attack: 0.01, release: 0.18, coef: 0.6
          # offbeats: 2& and 4&
          sleep 0.5
          play_chord skank_chords.tick, pan: -0.2
          sleep 1.0
          play_chord skank_chords.look, pan: 0.25
          sleep 1.5
          play_chord skank_chords.tick, pan: -0.1
          sleep 1.0
        end
      end
    end
  elsif s == :dissolve
    # skank evaporates
    sleep 4
  else
    sleep 4
  end
end

# =========================================================
#  FOUNDATION-LARYNX — Sub bass (Below / Fal-ta)
#  MODIFIED: Bass is MIDI-only (note on/off) + CC#1 mod wheel motion
# =========================================================

bassline = (ring :e1, :e1, :g1, :b0, :a0, :e1, :g1, :d1)

# Mod Wheel patterns (CC#1) — chosen to feel like “pressure” more than melody.
# Groove: gentle, Axis: wider, Edict: assertive, Dissolve: down-ramp to stillness.
MW_GROOVE   = (ring 28, 34, 40, 46, 52, 46, 40, 34)
MW_AXIS     = (ring 42, 54, 66, 78, 70, 60, 50, 58)
MW_EDICT    = (ring 72, 84, 96, 110, 102, 92, 118, 88)
MW_DISSOLVE = (ring 64, 56, 48, 40, 32, 24, 16, 8, 0)

live_loop :sub_bass do
  s = get(:section)
  
  # MIDI defaults apply only to this loop/thread (so everything else stays internal audio)
  if get(:bass_midi_force_port)
    use_midi_defaults port: get(:bass_midi_port), channel: get(:bass_midi_channel)
  else
    use_midi_defaults channel: get(:bass_midi_channel)
  end
  
  cc_on   = get(:bass_cc1_enabled)
  cc_num  = get(:bass_cc1_cc)
  jitter  = get(:bass_cc1_jitter)
  
  if [:groove, :axis, :edict].include?(s)
    n = bassline.tick
    vel = (s == :edict ? get(:bass_midi_vel_edict) : get(:bass_midi_vel))
    
    # Optional “panic” safety (CC 123 = All Notes Off) for external synths that occasionally hang
    # midi_cc 123, 0
    
    midi_note_on note(n), velocity: vel
    
    sustain = 0.9
    gap     = 0.1
    
    # Mod-wheel movement *during* the held bass note.
    if cc_on
      mw_ring = (s == :edict ? MW_EDICT : (s == :axis ? MW_AXIS : MW_GROOVE))
      steps = (s == :edict ? 3 : (s == :axis ? 2 : 1))
      
      steps.times do
        v = mw_ring.tick(:mw)
        v = v + rrand_i(-jitter, jitter) if jitter && jitter > 0
        v = [[v, 0].max, 127].min
        midi_cc cc_num, v
        sleep sustain / steps
      end
    else
      sleep sustain
    end
    
    midi_note_off note(n)
    sleep gap
    
  elsif s == :dissolve
    # long tail note into silence + CC#1 down-ramp (great for filter closing / Warps “cooling”)
    n = :e1
    midi_note_on note(n), velocity: get(:bass_midi_vel_dissolve)
    
    if cc_on
      steps = MW_DISSOLVE.length
      MW_DISSOLVE.each do |v|
        v = v + rrand_i(-jitter, jitter) if jitter && jitter > 0
        v = [[v, 0].max, 127].min
        midi_cc cc_num, v
        sleep 4.0 / steps
      end
      midi_cc cc_num, 0
    else
      sleep 4
    end
    
    midi_note_off note(n)
    
  else
    # keep the channel clean between sections
    # midi_cc 123, 0
    if cc_on
      midi_cc cc_num, 0
    end
    sleep 1
  end
end

# =========================================================
#  SPIRE-LARYNX — Shimmer halo (Above / Nyathe)
# =========================================================

live_loop :spire_halo do
  s = get(:section)
  
  if [:intro, :axis, :dissolve].include?(s)
    dub_fx do
      use_synth :hollow
      use_synth_defaults amp: (s == :intro ? 0.25 : 0.18), attack: 0.3, release: 1.8
      # sparse “witness tones”
      play (ring :e5, :g5, :b5, :d6).choose, pan: rrand(-0.4, 0.4)
    end
    sleep (s == :intro ? 4 : 6)
  else
    sleep 4
  end
end

# =========================================================
#  DUB SIREN — Occasional FX swell (ritual signal)
# =========================================================

live_loop :dub_siren do
  s = get(:section)
  
  if [:groove, :axis].include?(s) && one_in(6)
    with_fx :echo, phase: 0.375, decay: 8, mix: 0.55 do
      with_fx :reverb, room: 0.9, mix: 0.6 do
        use_synth :blade
        n1 = (ring :e4, :g4, :a4, :b4).choose
        n2 = n1 + 7
        play n1, amp: 0.25, attack: 0.02, sustain: 0.25, release: 0.6, cutoff: 95
        sleep 0.25
        play n2, amp: 0.22, attack: 0.01, sustain: 0.20, release: 0.7, cutoff: 105
      end
    end
    sleep 6
  else
    sleep 4
  end
end

# =========================================================
#  SDT “TABLET PHRASES” — Sparse chant-motifs as synth-voice
# =========================================================

# Map SDT tokens → small melodic gestures (not “lyrics”, just sonic glyphs)
define :utter do |name|
  use_synth :prophet
  with_fx :reverb, room: 0.95, mix: 0.7 do
    with_fx :echo, phase: 0.5, decay: 7, mix: 0.45 do
      case name
      when :alethe
        play :e4, amp: 0.22, attack: 0.25, sustain: 0.6, release: 1.4, cutoff: 95
        sleep 1
        play :b3, amp: 0.18, attack: 0.2, sustain: 0.4, release: 1.2, cutoff: 90
      when :lyare
        play :g4, amp: 0.18, attack: 0.15, sustain: 0.35, release: 1.0, cutoff: 105
      when :mer_ra
        play_pattern_timed [:e3, :g3, :e3], [0.25, 0.5, 0.75], amp: 0.22, release: 0.9
      when :truen
        play :b3, amp: 0.20, attack: 0.02, sustain: 0.2, release: 1.1, cutoff: 90
      when :los
        # hard stop / stamp
        play :e3, amp: 0.35, attack: 0.001, sustain: 0.05, release: 0.2, cutoff: 80
      when :nyathe
        play :d5, amp: 0.16, attack: 0.08, sustain: 0.2, release: 1.3, cutoff: 120
      when :sur
        play :e2, amp: 0.22, attack: 0.02, sustain: 0.3, release: 1.0, cutoff: 70
      when :zuur_ka
        play :g2, amp: 0.22, attack: 0.02, sustain: 0.3, release: 1.0, cutoff: 75
        sleep 0.25
        play :e3, amp: 0.18, attack: 0.01, sustain: 0.2, release: 0.8, cutoff: 85
      when :fal_ta
        play :e2, amp: 0.25, attack: 0.02, sustain: 0.35, release: 1.1, cutoff: 65
        sleep 0.25
        play :b1, amp: 0.22, attack: 0.01, sustain: 0.25, release: 1.0, cutoff: 60
      end
    end
  end
end

live_loop :tablet_phrases do
  s = get(:section)
  
  if s == :intro
    utter :alethe
    sleep 3
    utter :lyare
    sleep 5
    utter :mer_ra
    sleep 7
  elsif s == :groove
    # very sparse — let the dub do the carrying
    if one_in(4)
      utter (ring :mer_ra, :truen, :lyare).choose
    end
    sleep 8
  elsif s == :axis
    # explicitly “As Above ↕ As Below” call
    utter :nyathe
    sleep 2
    utter :sur
    sleep 2
    utter :zuur_ka
    sleep 2
    utter :fal_ta
    sleep 4
  elsif s == :edict
    # AER: hard stamps — Nyathe. Sur. Los.
    utter :nyathe; sleep 1
    utter :sur;    sleep 1
    utter :los;    sleep 2
    utter :mer_ra; sleep 2
  elsif s == :dissolve
    utter :alethe
    sleep 8
  else
    sleep 4
  end
end

# =========================================================
#  OPTIONAL: “TAPE DUST” — tiny clicks / air (very low)
# =========================================================

live_loop :dust do
  s = get(:section)
  
  if [:intro, :axis, :dissolve].include?(s)
    with_fx :hpf, cutoff: 90 do
      with_fx :reverb, room: 0.95, mix: 0.35 do
        sample :elec_tick, amp: 0.06 if one_in(3)
        sample :perc_snap, amp: 0.04, rate: 0.8 if one_in(5)
      end
    end
    sleep 1
  else
    sleep 2
  end
end
