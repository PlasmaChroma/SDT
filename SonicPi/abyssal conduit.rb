# ============================================
# ABYSSAL CONDUIT — MIDI OUT EDITION
# Sonic Pi generates MIDI; Ableton is the sound engine.
# ============================================

use_bpm 72
use_debug false

# --- MIDI PORT ---
# Replace with your loopback port name exactly.
# To print ports: run `puts midi_available_ports` once.
MIDI_PORT = "SonicPi_To_Ableton"

use_midi_defaults port: MIDI_PORT

# --- Channel map (change if you like) ---
CH_DRUMS = 10   # classic drum channel (but Ableton doesn't require this)
CH_BASS  = 1
CH_SUB   = 2
CH_DRONE = 3
CH_CHANT = 4
CH_HATS  = 5

# --- Drum note map (GM-ish defaults; adjust to your Drum Rack) ---
KICK_NOTE = 36  # C1
HAT_NOTE  = 42  # Closed hat

# --- Global controls ---
set :root, :e2
set :density, 0.55
set :section, :intro

define :prob do |p|
  rand < p
end

define :sect do
  get(:section) || :intro
end

define :dens do
  get(:density) || 0.5
end

# --- MIDI helpers ---
define :m_note do |n, dur=0.25, vel=90, ch=1|
  with_midi_defaults channel: ch do
    midi_note_on n, velocity: vel
    sleep dur
    midi_note_off n
  end
end

define :m_hit do |n, vel=100, ch=10|
  # short note for drums/percussion (safer than immediate off)
  with_midi_defaults channel: ch do
    midi_note_on n, velocity: vel
    sleep 0.04
    midi_note_off n
  end
end

define :panic! do
  # MIDI "All Notes Off" (CC 123) on our channels
  [CH_BASS, CH_SUB, CH_DRONE, CH_CHANT, CH_HATS, CH_DRUMS].each do |ch|
    with_midi_defaults channel: ch do
      midi_cc 123, 0
    end
  end
end

# --- Form controller (cycles forever) ---
in_thread(name: :form) do
  loop do
    set :section, :intro
    sleep 32
    set :section, :rise
    sleep 64
    set :section, :ritual
    sleep 96
  end
end

# ----------------------------
# THUMP: Kick + Sub impact
# ----------------------------
live_loop :thump do
  root = note(get(:root))
  
  pat =
  case sect
  when :intro  then spread(3, 16)
  when :rise   then spread(5, 16)
  else              spread(7, 16)
  end
  
  step = tick(:thump_step) % 16
  hit  = pat[step]
  
  if hit && prob(0.85 + dens * 0.1)
    # Kick for Drum Rack
    m_hit KICK_NOTE, (90 + (dens * 30)).to_i, CH_DRUMS
    
    # Sub "body" as a very short low note (route to a sub instrument in Ableton)
    sub_vel = (70 + dens * 40).to_i
    m_note root, 0.18, sub_vel, CH_SUB
  end
  
  sleep (ring 0.25, 0.25, 0.25, 0.25, 0.25, 0.23, 0.27, 0.25).tick(:swing)
end

# ----------------------------
# BASS: Foundation Larynx (MIDI)
# ----------------------------
live_loop :bass, sync: :thump do
  root = note(get(:root))
  
  n =
  if sect == :intro
    (ring root, root, root - 5, root).tick(:bass_n)
  else
    (ring root, root - 5, root - 7, root).tick(:bass_n)
  end
  
  vel = (75 + dens * 45).to_i
  dur = (sect == :intro ? 0.35 : 0.5)
  
  m_note n, dur, vel, CH_BASS
  sleep 0.5
end

# ----------------------------
# DRONE: slow chord bed (MIDI)
# ----------------------------
live_loop :drone do
  root = note(get(:root))
  notes = chord(root, :minor7).take(3)
  
  # send note_on for chord, hold, then note_off (prevents stuck notes)
  with_midi_defaults channel: CH_DRONE do
    notes.each { |nn| midi_note_on nn, velocity: (45 + dens * 20).to_i }
  end
  
  hold = (sect == :intro ? 6 : 4)
  sleep hold
  
  with_midi_defaults channel: CH_DRONE do
    notes.each { |nn| midi_note_off nn }
  end
  
  sleep (sect == :intro ? 2 : 1)
end

# ----------------------------
# HATS / SIGNAL LAYER (MIDI)
# ----------------------------
live_loop :hats, sync: :thump do
  if sect != :intro
    if spread(11, 16).tick(:hat_step) && prob(0.6 + dens * 0.25)
      vel = (25 + dens * 55).to_i
      m_hit HAT_NOTE, vel, CH_HATS
    end
  end
  
  sleep 0.25
end

# ----------------------------
# CHANT: SDT-like vowel line (MIDI)
# ----------------------------
live_loop :chant do
  root = note(get(:root))
  degs = (ring 0, 0, 3, 0, 5, 3, 7, 3).tick(:deg)
  n = (scale(root, :minor_pentatonic, num_octaves: 2))[degs]
  
  if sect == :ritual && prob(0.45 + dens * 0.25)
    # tiny detune via pitch selection (MIDI doesn't support microdetune directly)
    # so we imply it via occasional octave/neighbor flicker
    nn = (prob(0.12) ? n + 12 : n)
    
    vel = (35 + dens * 55).to_i
    dur = (ring 0.2, 0.25, 0.35, 0.18).tick(:chant_dur)
    
    m_note nn, dur, vel, CH_CHANT
  end
  
  sleep (ring 0.5, 0.25, 0.25, 0.5, 1.0).tick(:chant_t)
end
