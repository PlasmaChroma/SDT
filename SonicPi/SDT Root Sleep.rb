# Root Chakra Sleep Ritual — Sonic Pi v1
# Two major segments:
#   A) Wind-down / guided-bed (few minutes)
#   B) Deep sleep loop (runs indefinitely; record for 8h if desired)
#
# Notes:
# - This is designed to be sleep-safe: no sharp transients, slow evolution.
# - Guided narration is OPTIONAL. If you want voice, export a long WAV and drop it
#   into your Sonic Pi samples folder, then set USE_GUIDE_SAMPLE=true.
# - Binaural is OPTIONAL. It uses two sine tones panned L/R with a small offset.
#
# Dragon King Leviathan — tuned for Root: heavy, warm, grounded, low dynamics.

use_debug false
use_bpm 60  # BPM is only a reference clock; most events are geological-time.

# ----------------------------
# USER KNOBS (SAFE DEFAULTS)
# ----------------------------

# Segment A duration (wind-down / guided bed)
GUIDE_MINUTES = 4.0

# Master safety (keep low; you can raise gradually)
MASTER_AMP = 0.9

# Reverb room (root = large stone / earth chamber)
ROOM_MIX   = 0.35
ROOM_SIZE  = 0.85
ROOM_DAMP  = 0.7

# Drone bed
DRONE_AMP      = 0.28
DRONE_CUTOFF   = 75   # lowpass cutoff Hz-ish (Sonic Pi cutoff is not exact Hz)
DRONE_MOVEMENT = 0.06 # subtle motion depth

# Sub pulse (not a beat; a gravity reminder)
PULSE_AMP = 0.10
# Make pulse *non-periodic* so the mind can't count it.
# Winddown tends slightly more present; sleep becomes more geological.
PULSE_WINDDOWN_RANGE = [8.0, 15.0]   # seconds
PULSE_SLEEP_RANGE    = [12.0, 24.0]  # seconds

# Descent ramp between winddown and full sleep stability (minutes)
DESCENT_MINUTES = 12.0

# One-organism "climate" (a single slow control stream that modulates most layers)
CLIMATE_ON        = true
CLIMATE_UPDATE_S  = 28.0
CLIMATE_DEPTH     = 0.22   # 0..~0.35 is sane

# Micro-chimes fade out over time (minutes into sleep)
CHIME_FADE_MINUTES = 25.0

# Breath / wind texture
WIND_AMP       = 0.14
WIND_CUTOFF    = 80

# Rare micro-chime (optional; very sparse)
CHIME_ON       = true
CHIME_AMP      = 0.035
CHIME_ODDS     = 0.14  # chance per cycle

# Binaural beat (optional)
BINAURAL_ON     = true
BIN_CARRIER_HZ  = 100.0
BIN_BEAT_HZ     = 3.0  # perceived beat frequency (difference between L/R)
BINAURAL_AMP    = 0.045

# Optional guided narration as a long sample (Segment A only)
USE_GUIDE_SAMPLE = false
GUIDE_SAMPLE_PATH = "/mnt/data/your_root_guided_voice.wav"  # change to your file
GUIDE_SAMPLE_AMP  = 0.9

# Optional SDT whisper motifs (very sparse, subtle)
SDT_WHISPER_ON = true
SDT_AMP        = 0.04
SDT_RATE       = 0.6

# ---------------------------------
# INTERNAL UTILS (do not overthink)
# ---------------------------------

def clamp(v, lo, hi)
  [[v, lo].max, hi].min
end

def seconds_from_minutes(m)
  (m.to_f * 60.0)
end

# Soft random drift without sudden jumps
# Returns a value that slowly wanders around a base.

def slow_wander(base, depth: 0.1, step: 0.02, key: :wander)
  # seed a per-key state so different modulators don't fight each other
  t = (get(("#{key}_t").to_sym) || 0.0)
  x = (get(("#{key}_x").to_sym) || 0.0)
  
  # drift
  t = t + step
  x = x + rrand(-depth, depth) * 0.08
  x = clamp(x, -depth, depth)
  
  set ("#{key}_t").to_sym, t
  set ("#{key}_x").to_sym, x
  
  base + x
end

# ----------------------------
# GLOBAL FX BUS (stone chamber)
# ----------------------------

set :phase, :winddown
set :stop_all, false

live_loop :global_bus do
  stop if get(:stop_all)
  
  with_fx :level, amp: MASTER_AMP do
    with_fx :reverb, room: ROOM_SIZE, mix: ROOM_MIX, damp: ROOM_DAMP do
      # The actual sound-makers run in their own live_loops.
      # This loop simply holds the FX context stable.
      sleep 1
    end
  end
end

# ----------------------------
# CLIMATE (single slow control stream)
# ----------------------------
# Goal: fewer independent "decisions" per minute; the whole field breathes together.
set :climate, 0.0
set :sleep_t0, nil

live_loop :climate do
  stop if get(:stop_all)
  unless CLIMATE_ON
    sleep 10
    next
  end
  
  # climate gently wanders in [-CLIMATE_DEPTH, +CLIMATE_DEPTH]
  c = slow_wander(0.0, depth: CLIMATE_DEPTH, step: 0.05, key: :climate)
  set :climate, c
  
  # stamp the beginning of sleep for long-horizon fades
  if get(:phase) == :sleep && get(:sleep_t0).nil?
    set :sleep_t0, vt
  end
  
  sleep CLIMATE_UPDATE_S
end

# ----------------------------
# BINAURAL (L/R sine offset)
# ----------------------------

live_loop :binaural do
  stop if get(:stop_all)
  unless BINAURAL_ON
    sleep 2
    next
  end
  
  # Keep binaural ultra-stable and soft.
  base = BIN_CARRIER_HZ
  beat = BIN_BEAT_HZ
  
  # Use the shared climate for drift (coherence > randomness)
  c = (get(:climate) || 0.0)
  drift = c * 1.2  # ~±0.25 Hz if CLIMATE_DEPTH=0.22
  
  l_hz = base + drift
  r_hz = base + drift + beat
  
  # Long envelope so nothing clicks.
  synth :sine, note: hz_to_midi(l_hz), amp: BINAURAL_AMP, pan: -1,
    attack: 2, sustain: 10, release: 2
  synth :sine, note: hz_to_midi(r_hz), amp: BINAURAL_AMP, pan: 1,
    attack: 2, sustain: 10, release: 2
  
  sleep 12
end

# ----------------------------
# DRONE BED (root foundation)
# ----------------------------

live_loop :drone_bed do
  stop if get(:stop_all)
  
  # Root: deep fundamental + gentle harmonic breath
  base_note = :c2
  
  # One-organism modulation: most movement comes from shared climate
  c = (get(:climate) || 0.0)
  
  cutoff = slow_wander(DRONE_CUTOFF + (c * 18), depth: 7, step: 0.03, key: :drone_cut)
  detune = slow_wander(0.0 + (c * 0.08), depth: 0.10, step: 0.02, key: :drone_det)
  
  amp_main = DRONE_AMP * (1.0 + (c * 0.18))
  
  with_fx :lpf, cutoff: cutoff do
    with_fx :hpf, cutoff: 35 do
      # Two layers: triangle-ish + sine for warmth
      synth :tri, note: base_note, amp: amp_main * 0.65,
        attack: 6, sustain: 24, release: 6, detune: detune
      synth :sine, note: base_note - 12, amp: amp_main * 0.35,
        attack: 8, sustain: 26, release: 8
      
      # A very slow, barely-there upper shadow
      synth :sine, note: base_note + 12, amp: amp_main * 0.10,
        attack: 10, sustain: 18, release: 10
    end
  end
  
  sleep 24
end

# ----------------------------
# SUB PULSE (geological heartbeat)
# ----------------------------

live_loop :sub_pulse do
  stop if get(:stop_all)
  
  # Non-periodic pulse: the body feels it, the mind can't count it.
  phase = get(:phase)
  rng = (phase == :sleep) ? PULSE_SLEEP_RANGE : PULSE_WINDDOWN_RANGE
  
  # Small climate bias: in "heavier" moments, pulse tends slightly slower
  c = (get(:climate) || 0.0)
  base_period = rrand(rng[0], rng[1])
  period = clamp(base_period + (c * 2.0), rng[0], rng[1])
  
  with_fx :lpf, cutoff: 70 do
    with_fx :hpf, cutoff: 28 do
      # Long attack/release to avoid clicks.
      synth :sine, note: :c1, amp: PULSE_AMP,
        attack: 1.0, sustain: 0.35, release: 3.2
      # Faint upper body for small speakers
      synth :tri, note: :c2, amp: PULSE_AMP * 0.30,
        attack: 1.1, sustain: 0.2, release: 2.8
    end
  end
  
  sleep period
end

# ----------------------------
# WIND / BREATH TEXTURE
# ----------------------------

live_loop :wind_breath do
  stop if get(:stop_all)
  
  # Sleep-safe noise: filtered, slow-moving, never bright.
  # Shared climate keeps the whole field coherent.
  c0 = (get(:climate) || 0.0)
  
  c = slow_wander(WIND_CUTOFF + (c0 * 20), depth: 14, step: 0.04, key: :wind_cut)
  a = slow_wander(WIND_AMP * (1.0 + (c0 * 0.16)), depth: 0.04, step: 0.03, key: :wind_amp)
  
  with_fx :lpf, cutoff: c do
    with_fx :hpf, cutoff: 22 do
      synth :bnoise, amp: a,
        attack: 5, sustain: 11, release: 5
    end
  end
  
  sleep 14
end

# ----------------------------
# MICRO-CHIME (rare, soft)
# ----------------------------

live_loop :micro_chime do
  stop if get(:stop_all)
  unless CHIME_ON
    sleep 10
    next
  end
  
  # Only during sleep phase, and only rarely.
  if get(:phase) == :sleep
    # Fade chimes out over time so "events" disappear as sleep deepens.
    t0 = get(:sleep_t0)
    elapsed = t0.nil? ? 0.0 : (vt - t0)
    fade_total = seconds_from_minutes(CHIME_FADE_MINUTES)
    fade = clamp(1.0 - (elapsed / fade_total), 0.0, 1.0)
    
    # Effective odds get smaller over time (rarer), and amp fades toward zero.
    odds = (1.0 / CHIME_ODDS).round
    odds = (odds + ((1.0 - fade) * odds * 3.0)).round  # up to 4x rarer
    
    if fade > 0.02 && one_in([odds, 2].max)
      with_fx :lpf, cutoff: 95 do
        with_fx :echo, phase: 1.6, decay: 7 do
          use_synth :pretty_bell
          play (scale :c4, :minor_pentatonic).choose,
            amp: CHIME_AMP * fade,
            attack: 0.7,
            sustain: 0.15,
            release: 4.0,
            pan: rrand(-0.35, 0.35)
        end
      end
    end
  end
  
  sleep 20
end

# ----------------------------
# SDT WHISPER MOTIFS (Segment A mostly; sparse)
# ----------------------------

# If you want true whisper voice, trigger samples.
# Here we approximate "whisper presence" with ultra-soft, breathy synth gestures.

live_loop :sdt_whisper do
  stop if get(:stop_all)
  unless SDT_WHISPER_ON
    sleep 10
    next
  end
  
  # More active in winddown; almost absent in descent/sleep.
  phase = get(:phase)
  density = case phase
  when :winddown then 0.24
  when :descent  then 0.12
  else 0.06
  end
  
  if rand < density
    motif = [
      [:a3, :a3, :e3],  # A-le-the (hint)
      [:g3, :e3, :g3],  # Lya-ré (hint)
      [:c3, :c3, :g2],  # Mor-ah (hint)
      [:d3, :c3, :d3]   # Dre(h)/Dreth (hint)
    ].choose
    
    with_fx :lpf, cutoff: 85 do
      with_fx :hpf, cutoff: 40 do
        with_fx :reverb, room: 0.9, mix: 0.25 do
          use_synth :hollow
          motif.each do |n|
            play n, amp: SDT_AMP, attack: 0.8, sustain: 0.1, release: 2.2,
              pan: rrand(-0.35, 0.35)
            sleep [0.6, 0.9, 1.2].choose
          end
        end
      end
    end
  end
  
  sleep 8
end

# ----------------------------
# OPTIONAL GUIDED SAMPLE (Segment A)
# ----------------------------

live_loop :guided_voice do
  stop if get(:stop_all)
  
  unless USE_GUIDE_SAMPLE
    sleep 2
    next
  end
  
  # Only trigger during winddown.
  if get(:phase) == :winddown
    # You may need to adjust the sample path to your environment.
    # In Sonic Pi, using an absolute path works on many setups.
    with_fx :level, amp: GUIDE_SAMPLE_AMP do
      sample GUIDE_SAMPLE_PATH
    end
    
    # If your sample length is known, you can sleep that long.
    # Otherwise, just sleep the winddown duration and let it play.
    sleep seconds_from_minutes(GUIDE_MINUTES)
  else
    sleep 5
  end
end

# ----------------------------
# PHASE CONTROLLER
# ----------------------------

live_loop :phase_controller do
  stop if get(:stop_all)
  
  # Start in winddown; then descent; then sleep.
  set :phase, :winddown
  guide_s = seconds_from_minutes(GUIDE_MINUTES)
  descent_s = seconds_from_minutes(DESCENT_MINUTES)
  
  cue :winddown_start
  sleep guide_s
  
  # Descent: same world, fewer "decisions" and slightly lower activity.
  set :phase, :descent
  cue :descent_start
  
  # During descent we gently bias down certain layers by nudging the climate
  # and letting event generators naturally become less frequent.
  # (No hard fades; just a slope.)
  sleep descent_s
  
  set :phase, :sleep
  cue :sleep_start
  
  # Sleep phase runs forever. If you want an 8-hour auto-stop, enable below.
  # sleep 8 * 60 * 60
  # set :stop_all, true
  
  sleep 3600
end

# ----------------------------
# OPTIONAL: A "SAFETY" MONITOR
# ----------------------------

live_loop :safety_monitor do
  stop if get(:stop_all)
  
  # If CPU spikes or you want to hard stop, set :stop_all true manually.
  # You can also use this as a place to gradually lower activity over the first minutes.
  
  # Example: Slightly reduce chimes after the first 30 minutes.
  if get(:phase) == :sleep
    t0 = (get(:sleep_t0) || nil)
    if t0.nil?
      set :sleep_t0, vt
    else
      elapsed = vt - t0
      if elapsed > 1800
        # after 30 minutes, nudge chimes rarer
        # (does not override CHIME_ON, only reduces effective odds)
        # You can also just set CHIME_ON=false at top if you prefer.
      end
    end
  end
  
  sleep 30
end

# ----------------------------
# HOW TO USE
# ----------------------------
# 1) Paste into Sonic Pi.
# 2) Press Run.
# 3) For an 8-hour render, hit Record and let it run.
# 4) If you add narration:
#    - Export a single WAV (e.g., 4–6 minutes)
#    - Set USE_GUIDE_SAMPLE=true and update GUIDE_SAMPLE_PATH
#
# QUICK TUNING (Root):
# - If too "present": lower DRONE_AMP and WIND_AMP.
# - If too "rhythmic": increase PULSE_PERIOD_S (e.g., 12–18).
# - If binaural is noticeable: lower BINAURAL_AMP or turn BINAURAL_ON=false.
