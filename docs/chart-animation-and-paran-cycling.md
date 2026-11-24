# Chart Animation & Paran Cycling — Design

Status: design + Phase 1 in progress
Author: (design conversation, 2026-06)

## 1. Motivation

Two related user needs converged into one feature family:

1. **Paran cycling.** The `Par` / `Par=N` event search reports each paran at its
   date of *minimum orb* ("focal paran"). But the focal pair (e.g. `Sat As +
   Jup-r As`) is in orb over a *range* (e.g. 5/24–7/18), and within that range it
   skims in and out of configuration with *different fixed stars* day by day. The
   single focal snapshot misleads: the stars shown on the peak day (Vega Mc,
   Spica Ds, Revati-r As…) look like permanent members when they are transient.
   The user wants to **cycle through the focal pair's in-orb moments** and watch
   the surrounding stars come and go.

2. **Chart animation.** More generally, a transport control (play / step / step
   to end / step back / to beginning) that drives a chart's evaluated moment
   across an event's range, animating the wheel — for ranged event types
   (T=T, T=N, OT=N, P=P, P=N, IP=N) as well.

Both reduce to one primitive: **set the chart's evaluated moment and re-render.**
The events table already does this discretely (clicking a row). A transport adds
continuous / stepped control over the same primitive.

## 2. Key architectural findings (from code trace)

### 2.1 The evaluated-moment primitive already fans out everywhere

`AstroFile::setGMT()` → `change(GMT)` → `recalculate()` (once) →
`emit changed()` → every `AstroFileHandler::fileUpdatedSlot` →
`dispatchUpdate` → each handler's `filesUpdated()`. So one time change already
redraws the wheel, tables, planets and all docked panels. The events table uses
exactly this (`clickedCell` stamps GMT / base chart). A slider/transport is the
same operation with a different input device.

### 2.2 Which file carries the scrubable time

For a **Par=N biwheel** the Directions table is rendered from **`file(1)`** (the
moving / locus chart); `file(0)` is the natal **radix**, passed only as
`natalContext` to ex-precess natal bodies. `describeParans` reads the paran
moment from `scopes.first()->getGMT()` and derives the natal ex-precession day
from it. So:

- Scrub the **moving chart** (`file(1)` for Par=N, the single `file(0)` for a
  transit-only Par). Never the radix.
- The radix's ex-precession day follows automatically — one time change moves
  both the live fixed-star roster and the natal ex-precession.

Robust rule for the control: drive **whichever focused file is `TypeParan` (or
has a base chart) and carries occurrences**, not a hard-coded index.

### 2.3 The paran occurrence list is computed during the search and discarded

`findParans()` walks day by day; `trackSubCluster` already computes each daily
crossing's cluster-mean datetime (`peakDT`) and spread. Only the *tightest* day
(`peakDateTime`/`peakJd`) plus first/last survive into the emitted event. The
intermediate per-day occurrences — exactly "the moments the focal pair is in
orb" — are thrown away. Harvesting them is an append per active day; no extra
ephemeris work, no new solver. The existing wrap-merge (across the UTC midnight
boundary) already unifies a single crossing split across the day boundary, so a
"twice in one day near midnight" reads as one honest occurrence.

### 2.4 Per-frame cost: the wheel path is clean

`Chart::filesUpdated` does: `updateScene` + `updatePlanetsAndCusps` (Qt
graphics) + `updateAspects` + midpoint/paran figures. `updateAspects` →
`calculateAspects(set, planets)` is **pure O(n²) arithmetic over already-computed
positions — no `swe_*` calls, no pattern search.** Focal/event charts take a
slightly heavier branch (synthetic midpoints + override aspect set) but still no
ephemeris. The wheel is safe to run per frame.

The per-`setGMT` floor is `recalculate()` → `calculateAll()`: houses + ~17
planets (1 `swe_calc_ut` each) + N stars (1 `swe_fixstar` each) — moderate. The
expensive `angleTransit` rise/set computation (4× `swe_rise_trans` + 4×
`swe_calc_ut` per body) is **gated behind `primDirMode == prdActive`**
(astro-calc.cpp:1418, 1981); the default `prdMundane`/`prdZodiacal` modes use a
cheap analytic formula. `primDirMode` is flipped only by the Tables view.

### 2.5 The expensive handlers — and the visibility gate that already exists

| Handler | On GMT change | Hosting | Gated already? |
|---|---|---|---|
| **Chart** (wheel) | scene + positions + aspects (cheap) | slide | n/a — keep per frame |
| **Plain** (Tables) | `refresh()` → full HTML incl. `describeParans`, speculum | slide | **yes** (slide hidden when not current) |
| **Harmonics** | `describePlanet()` → `A::findHarmonics` (search to H=240) | **dock** | only if dock hidden |
| **Speculum** | rebuild speculum table | **dock** | only if dock hidden |
| **Details** | planet detail text | **dock** | only if dock hidden |
| **Transits** (events) | reconcile / finder | **dock** | **yes** (visibility + needs-refresh/Auto) |

**Crucial correction:** the base `AstroFileHandler` *already* defers updates for
any handler that is not visible. `fileUpdatedSlot` and `displaySettingsSlot`
both check `isVisible() && !isAnyFileSuspended()`; if not, they accumulate flags
into `delayMembers`/`delayViewMembers` and set `delayUpdate`. The base
`showEvent` (astro-gui.h:463) calls `resumeUpdate()`, which flushes. The slide
widget `hide()`s non-current slides (slidewidget.cpp:100, 197).

Consequence: **slides (Chart/Planets/Tables) are mutually exclusive**, so Plain
never recomputes while you watch the wheel — it is already gated. The
*"visibility gating"* originally planned as Phase 1 is therefore **already done
by the framework**; adding per-handler `isVisible()` checks would be dead code.

The real per-frame snag is **docked panels that are genuinely visible alongside
the wheel** (Harmonics, Speculum, Details). Visibility gating cannot help — they
are *supposed* to be visible. They need a **scrub-mode** suppression that skips
their recompute *while a continuous interaction is in flight* and does one
catch-up when it ends.

### 2.6 The broken drag-to-change-time feature shares this root cause

`RotatingCircleItem::sceneEvent` (chart.cpp:78) calls `file->setGMT()` on every
mouse-move while dragging the Ascendant/Sun. With any heavy dock open, each move
triggers `findHarmonics` et al. → unusable. This is the same problem as
animation and is the natural **acceptance test**: if dragging the Ascendant is
smooth with Harmonics open, animation will be too.

## 3. Design

### 3.1 Scrub mode

A process-global flag (in the `A::` namespace next to `primDirMode`):

```cpp
extern bool scrubbing;                 // astro-calc.h
inline bool isScrubbing()      { return scrubbing; }
inline void setScrubbing(bool b) { scrubbing = b; }
```

While `scrubbing`:
- `recalculate()` → `calculateAll()` takes the **light path**: the
  `primDirMode == prdActive` angle-transit branch is skipped (falls to the cheap
  analytic branch). The wheel never needs rise/set times.
- Heavy **dock** handlers (Harmonics, Speculum, Details; Transits for safety)
  early-return from `filesUpdated`. The wheel (Chart) and Plain are untouched
  (Plain is a slide, already hidden during wheel interaction; and Plain *should*
  refresh during paran discrete stepping, which is not scrubbing).

Exiting scrub mode: the controller clears the flag and forces one full recompute
+ redispatch (`file->change(AstroFile::GMT)` always recalculates and emits, even
when the GMT value is unchanged), so the suppressed panels catch up exactly once
with exact positions/angle-transits.

### 3.2 Two interaction regimes

- **Continuous** (drag, animation playback): scrub mode on for the duration,
  wheel-only per frame, catch-up on release/stop. Rate for playback is
  **range-normalized**: a general "animation duration" option (default ~10 s)
  apportioned across the event's range, driven by **elapsed wall-clock**
  (`position = clamp((now − start)/budget) · span`) so total duration is
  constant across event spans and machine speeds; frame count is emergent.
- **Discrete** (paran occurrence stepping): user-paced, *not* scrub mode. Each
  step recomputes everything for the visible view (including the Directions
  table — that is the point). Play disabled.

### 3.3 Paran occurrence cache

Harvested in `findParans` (no extra ephemeris):
- `ParanState` gains `QVector<QPair<QDateTime,qint64>> occurrences;`
  (cluster-mean datetime, spread secs), appended once per active day (guard:
  append only on day advance — `cur.endDate != d` — to avoid sub-tuple dupes).
- `emitParan` converts spread→deg and hands the list to the event:
  `HarmonicEvent::setOccurrences(...)` (parallel to `setRange`).
- `clickedCell` stamps it onto the `TypeParan` `AstroFile`
  (`setParanOccurrences`) next to `setParanGroupPlanets`, and it is
  regenerated on the next event click.

  **Update (2026-09-12): now persisted** for `TypeParan`/`TypeApparition`
  via `AstroFile::derivationState()`, which every serializer routes through
  (the `.dat` plus all three session paths). The original "not persisted"
  note was fine while a paran chart only ever existed as file(1) of a live
  tab, but a chart reopened on its own never receives the click that would
  regenerate the list, so it could never cycle. The group and the
  occurrences define what the chart *is*; they are not recoverable from its
  moment and place. Still transient on file(0), where the same fields hold
  focal display state for the radix.

### 3.4 Transport toolbar (new draggable toolbar)

Matches the existing draggable-toolbar paradigm (file bar; slide bar; panels/
settings/about bar). Stable button layout, mode-dependent enablement:

```
 ⏮ to-begin   ◀ step-back   ▶/⏸ play/pause   ▶ step-fwd   ⏭ to-end   ⊙ snap-to-anchor
```

- Paran, ≥2 occurrences: step/begin/end enabled, **play disabled**, ⊙ = tightest
  orb.
- Paran, 1 occurrence: all disabled (or toolbar hidden).
- Ranged aspect (T=T/T=N/OT=N/P=P/P=N/IP=N): play enabled, ⊙ = exactitude.

Each opted-in event type provides a small **scrub model**: discrete stop-set *or*
continuous range + anchor + whether play is meaningful. The toolbar reads the
model; no per-type UI code. "Free mode" (a generic continuous scrubber with no
anchor) is **dropped** — the events table already cycles rows via up/down arrows;
value only exists for sub-event paran stepping and ranged-event playback.

Hosting: the control belongs at the **tab / mainwindow** level so one scrub
drives every synced view, not buried in the Tables toolbar.

### 3.5 Safety rule

Only enable the transport on a chart with a **moving layer** (`hasBaseChart()`,
or a derived / `TypeParan` type). Never scrub a primary natal chart's own birth
moment (it would move the subject and dirty the saved chart).

## 4. Phasing

- **Phase 1 (revised): scrub-mode foundation + fix drag-to-change-time.**
  The originally-planned "visibility gating" is already provided by the base
  class, so Phase 1 becomes the scrub flag + light `recalculate()` path + heavy
  dock-handler suppression, validated by un-breaking the Ascendant drag. Inert
  until the flag is set.
- **Phase 2: paran occurrence harvest + cache** (`findParans` → `HarmonicEvent`
  → `AstroFile`).
- **Phase 3: transport toolbar**, paran discrete model first (play disabled).
- **Phase 4: continuous animation** for ranged types (play + range-normalized
  wall-clock loop).

## 5. Open items

- Confirm whether docks are tabified (only the current tab visible) — narrows how
  often multiple heavy docks are simultaneously visible, but does not change the
  scrub-mode need.
- Decide whether the catch-up on scrub-exit should also flush accumulated
  `delayMembers` (currently the forced `change(GMT)` re-derives everything, which
  is sufficient but does one redundant full recalc).
- `scrubbing` is a plain global like `primDirMode`; both are GUI-thread state.
  Keep all writes on the GUI thread (finder threads do not touch `calculateAll`).

## 6. Heliacal apparitions

Status: implemented — extends the discrete occurrence transport of §3–4.

### 6.1 Model

The heliacal event search (`etcHeliacalEvents` for planets, `etcHeliacalStars`
for fixed stars) no longer emits one row per phase point. Instead each body's
**apparition** — its span of visibility from morning-first (**MF**) to
evening-last (**EL**) — is a single row, navigated with the same discrete
occurrence transport used for parans (§3.4). It reuses
`HarmonicEvent::setOccurrences` plus a **parallel** `setOccurrenceLabels`
(1:1 with the occurrences; empty for parans, which keep the plain `i/n · orb`
label). By body class:

- **Fixed stars + outer planets (Mars/Jupiter/Saturn).** `swe_heliacal_ut`
  yields only rising/setting (types 1/2) for these, so one all-night apparition
  with **five** labeled stops: MF · Acr · **Cul (anchor)** · Cs · EL.
- **Inner planets (Mercury/Venus).** Two apparitions per synodic cycle, anchored
  at greatest elongation (GWe morning / GEe evening), three stops each.
- **Moon.** Discrete EF/ML crescent rows (no occurrences) — the non-apparition
  path in `clickedCell` keys off `occurrences().isEmpty()`.

### 6.2 Anchor vs phase — two different kinds of moment

The five stops are **not** all computed the same way, and this is the source of a
genuine (non-bug) counter-intuition worth recording:

- **Culmination (Cul)** is an **angle-transit**: the date the body's upper
  meridian transit best matches solar midnight (the Sun's lower transit) — the
  body highest at midnight, ≈ opposition in equatorial longitude. This is the
  apparition **anchor** (the row's headline date and where the ⊙ button snaps).
- **Acronychal rising (Acr)** and **cosmical setting (Cs)** are
  **visibility / phasis** events (in the sense of Ptolemy's *Phaseis*): the date
  the body **rises exactly at sunset** (Acr) and the date it **sets exactly at
  sunrise** (Cs). Each pins a *different body event* to a *different solar
  event*, so each falls on its **own date**, generally weeks from the
  culmination.

Consequence: Acr, Cul and Cs do **not** collapse onto a single "best night."
The intuitive picture — rises at dusk → culminates at midnight → sets at dawn,
all within one ~18 h night — is a *different* construction (the culmination
night's own rise/transit/set). Because the Sun's rise/set times drift
asymmetrically near opposition (equation of time + changing solar declination),
the phasis dates Acr and Cs can even land in counter-intuitive order — e.g.
Cs *before* Cul *before* Acr, or Acr well after the culmination. The stops are
sorted chronologically before storage and each carries its own label, so the
navigator always steps in true time order with correct names; only the naïve
dusk→midnight→dawn *expectation* is wrong.

### 6.3 Terminology (the 2×2)

Dusk vs dawn × rising vs setting:

|             | rises                        | sets                        |
|-------------|------------------------------|-----------------------------|
| at sunset   | acronychal rising (**Acr**)  | acronychal setting ≈ **EL** |
| at sunrise  | cosmical rising ≈ **MF**     | cosmical setting (**Cs**)   |

"Heliacal" is the near-the-Sun *visibility* pair sitting on two of those corners:
heliacal rising ≈ cosmical rising (**MF**), heliacal setting ≈ acronychal setting
(**EL**). Note that Swiss Ephemeris aliases `SE_COSMICAL_SETTING` to
`SE_ACRONYCHAL_SETTING` (both `6`, both flagged "not implemented"); we compute
Acr/Cs/Cul ourselves in `findHeliacalEvents` (astro-calc.cpp) via the
body-agnostic `acronychalJd` / `cosmicalSetJd` / `culminationJd` helpers
(star by name, planet by `ipl`, via `swe_rise_trans`).

### 6.4 Deferred idea

Whether to offer an alternative **best-night** reading — Acr/Cs taken as the
culmination night's own dusk-rise and dawn-set (the ~18–24 h triple many expect),
instead of (or alongside) the strict phasis dates. Kept as strict phasis for now.
