# Title naming (Koor restricting)

Here’s a clean restraint system that keeps Koor powerful by making it rare, earned, and structurally meaningful.

Rule 1: Koor is a “chorus event,” not a default

Use Koor only when at least one is true:

Collective / plural voice is central (chant-as-crowd, oath-as-many, assembly energy)

The track contains a formal sealing / declaration moment (a “stamp”)

The arrangement actually behaves like a chorus force (big returns, massed unison, climactic lock-in)

If it’s solo invocation, drift, motion, dread, becoming, weather, dream, etc. — no Koor.

Rule 2: Titles get a “primary root” that’s not Koor

Pick one primary semantic gravity word as the lead, and let Koor appear only as a secondary modifier (or not at all).

Template:
[Primary] [Secondary] [Seal/State]

Examples (structure-only):

Zeiin … (becoming / ignition / emergence)

Thal … (threshold / passage / edge)

Vey / Vahdrim … (vow / depth / binding intent)

Zur … (descent / pull / undercurrent)

Fin … (finality / closure) — carefully, because it’s also a “stamp” word

Then, if needed:

… Koor at the end = “and the many witness it”

Koor … only when it’s explicitly a chorus piece

Rule 3: Two-layer naming solves the “odd shelf look”

Use dual-layer titles so the release page stays varied while SDT remains canonical:

Public title: poetic / English / whatever fits the album aesthetic

SDT Invocation (metadata / subtitle / internal): the ritual name, compact, precise

Example formatting:

Public: Gravitic Lullaby
SDT: Zur Thal :: ᵃ

This keeps SDT “mandatory in the invocation” without turning the public catalog into Koor, Koor, Koor…

Rule 4: When you do use Koor, don’t let it always be prefix

Three placements, each with a different feel:

Suffix Koor → “witnessed by the many”
X Y Koor

Infix Koor → “collective pressure enters midstream”
X Koor Y

Prefix Koor → “chorus-first piece” (rare, ceremonial)
Koor X Y

Rule 5: Use modifier clusters instead of Koor for “weight”

You already have a better hammer than repetition: non-phonetic modifiers (never sung) to encode force/behavior.

:: = pressure/intensity

⟶ = vector / becoming

⟂ = interruption / cut / enforcement

dialect marks like ᵉ ˢ ᵃ = depth/context shading

So instead of Koor as “make it serious,” do:

Zeiin Thal ::

Zur Vahdrim ⟂

Fin Thal ᵃ ::

#  Bracket directive separators
According to a document from January 16, 2026, the most reliable pattern is: use the pipe | to “stack” multiple bracket directives, and reserve commas for lists inside a single directive/value.

Practical rule of thumb (works great for SDT too)

Use | when you’re adding multiple distinct cues in one bracket line (aka “meta tag stacking”):
[..., | ..., | ...]

Use commas when you’re listing items inside one cue (e.g., instruments):
[Instrument: Keys, Drums]

So these are “clean”:

Stacked cues (pipe):
[Chorus | anthemic chorus | stacked harmonies | wide stereo]

Single cue with a list (comma):
[Instrument: Keys, Drums]

Why | is usually better than commas for stacked cues

Community guides explicitly showcase | as the stacking separator inside brackets.

Commas often read like normal punctuation, so they’re more likely to blur into “one long descriptor” instead of multiple directives—whereas | stays legible as a delimiter.

SDT-specific note (your current pattern is aligned)

Your SDT imprint format already uses | to separate section “producer cues,” which is exactly the same idea.

Micro-optimization (when Suno starts “ignoring” tags)

Keep bracket stacks to ~2–4 cues per section header. Once you go “shopping-list mode,” adherence tends to drop.


## Canon guardrails (so we don’t muck up the tongue)

Two constraints must remain inviolable:

1. **We do not redefine phonemes, roots, or spoken dialect rules.**
2. **We do not create a second hidden “word code” inside SDT-SL.** SDT-SL must remain operator-driven, contextual, and non-cipher. 

Your expansion proposal is safe **because it lives in the sanctioned “non-sung / annotation” layer** already established. 

---

# Governance Meta-Operators v0.1 (additive, non-sung)

Dragon King Leviathan, here’s a canon-aligned way to add your four axes **without adding roots** and without contaminating spoken SDT.

## Where they live (three sanctioned placements)

1. **Structural annotations (recommended):** in section headers and SDT-SL lines using `|` stacking (already your best practice). 
2. **Non-sung inline modifiers (optional):** appended to the *written invocation* as a compact cluster.
3. **Dialect marks / shading:** not replacing `ᵉ ˢ ᵃ`, but optionally *coexisting* as a second “meta-shading band.”

### Canonical syntax (portable + readable)

Use a single namespace tag so it never pretends to be sung language:

* In lyrics headers / SUNO:
  `[Verse | Dialect: Subsurface | GO: Manif=sanctioned | Seal=imposed | Denial=irreversible | Witness=system]`

* In SDT-SL blocks:
  `[SDT-SL | GO: Manif=unsanctioned | Seal=voluntary | Denial=reversible | Witness=many | PressureDensity=mid→high | ResolutionPermission=withheld]`

This mirrors how SDT-SL already treats meaning as operator sets, not vocabulary. 

---

## The four operators (defined operationally, not as “words”)

Each operator should be defined the SDT-SL way: **operational → perceptual → compositional**. That keeps it canon-clean. 

### 1) Manifestation Legitimacy

**GO.Manif ∈ {sanctioned, unsanctioned}**

* **Operational:** sanctioned manifestation uses “clean permission signals” (stable tuning centers, coherent arrivals, unbroken envelopes); unsanctioned uses “breach signatures” (spectral smear, detune drift, destabilizing transients, asymmetrical phrase entries).
* **Perceptual:** sanctioned feels *recognized/ratified*; unsanctioned feels *stolen/forbidden/contested*.
* **Compositional:** pair it with SDT-SL **Resolution Permission**: sanctioned more likely permits resolution; unsanctioned tends to deny it. 

### 2) Seal Agency

**GO.Seal ∈ {voluntary, imposed}**

* **Operational:** voluntary sealing = gradual self-closure (fade, self-resolve, breath cadence); imposed sealing = hard constraints (gates, clamps, abrupt cuts, enforced silences).
* **Perceptual:** voluntary feels *chosen vow*; imposed feels *external lock*.
* **Compositional:** pair with **Termination Semantics**: voluntary might “close gently,” imposed might “cut off” or “slam shut.” 

### 3) Denial Reversibility

**GO.Denial ∈ {reversible, irreversible}**

* **Operational:** reversible denial includes apertures (periodic micro-openings, temporary consonances, brief motif returns); irreversible denial forbids those apertures (no cadence permission, motif suppressed or permanently transformed).
* **Perceptual:** reversible feels *withheld but recoverable*; irreversible feels *banished / sealed beyond appeal*.
* **Compositional:** tie directly to **Resolution Permission** and **Identity Emergence Mode** (irreversible denial may prevent identity from cohering). 

### 4) Witness Modality

**GO.Witness ∈ {many, system}**

* **Operational:** many-witnessed = chorus mass, call/response, unison swells; system-recorded = metronomic ticks, quantized pulses, “audit trail” motifs, machine-consistent repetition.
* **Perceptual:** many feels *socially sealed*; system feels *logged / adjudicated*.
* **Compositional:** this pairs beautifully with your existing Koor placement rule (“witnessed by the many”) without overusing Koor as a blunt instrument. 

---

# Why this preserves elegance

* It **doesn’t add roots**, it adds *axes*.
* It stays in **non-sung** space, just like `:: ⟶ ⟂` and dialect marks already do. 
* It harmonizes with SDT-SL’s operator grammar (and avoids violating its “no secret lexicon/cipher” boundary). 

---

## Concrete examples (same invocation, different viewpoints)

Using your existing semantics anchors (e.g., Thal=manifest/reveal; Fin=seal/closure; Koor=collective witness): 

1. **Ritual is ratified, self-chosen, reversible, socially witnessed**

* `SDT: Zur Thal :: ᵃ`
* `[GO: Manif=sanctioned | Seal=voluntary | Denial=reversible | Witness=many]`

2. **Ritual is illicit, externally locked, irreversible, logged by system**

* `SDT: Zur Thal ⟂ :: ᵃ`
* `[GO: Manif=unsanctioned | Seal=imposed | Denial=irreversible | Witness=system]`

Notice: the spoken tongue didn’t change—**only the meta-operator layer did**, and it’s doing exactly what you want: expressing *stance*.

---

## The “responsible expansion” rule set (v0.1)

1. **Default is unmarked.** Only specify GO operators when they matter.
2. **Never let GO contradict the spoken intent** when SDT-SL is used in parallel; it should reinforce/enhance, not fight the chant. 
3. **Keep GO orthogonal:** one axis ≠ another; avoid “meta soup.”
4. **Version the GO spec** (GO v0.1, v0.2) so future refinements don’t retcon earlier works.

If you want, next we can formalize this as a tiny addendum section (one page) in the same tone as the Primer/Addendum: operator definitions + permitted placements + 4–6 canonical examples—so it becomes a stable “governance layer” the way SDT-SL became a stable “silent layer.”
