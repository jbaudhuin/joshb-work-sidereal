# Campanus/Regiomontanus mundane primary directions

## Status

**Implemented and validated.** Primary Directions now supports three mundane
systems, selectable via `A::PrimDirSystem` (`pdsPlacidus` / `pdsCampanus` /
`pdsRegiomontanus`, `Mundane/primDirSystem`, default Placidus — existing
users see no change): the original Placidus semi-arc construction, plus
Campanus and Regiomontanus mundane aspectual directions. A rapt-parallel
(mundane-midpoint) bracket-search solver was also built, validated, and
wired into the Events table as a kind of primary direction — gated by
`EventOptions::pdIncludeRaptParallels`, shown/hidden by PD's own toolbar
toggle, no separate control — see "Wired into the Events table" below.

This doc was originally a seed/placeholder; it's now the record of what was
actually built, since the three open questions below turned out to have
answers, and a couple of adjacent techniques turned out to be explicitly
out of scope.

## Why this is a separate technique, not a Placidus variant

Placidus's proportional-horizon (the locus of points sharing a given
meridian-distance/semi-arc ratio) is **not a great circle** — this is stated
explicitly in Makransky, *Primary Directions: A Primer of Calculation*,
Ch. VII ("The Placidus system is the only non-projection system considered
in this book"). That's why Placidus mundane aspects need the Placidus
Mundane Position (PMP) construction — a purely proportional, non-geometric
position measure — rather than genuine spherical trigonometry.

Campanus (and Regiomontanus) house division **is** built from great circles
through the prime vertical, so the "pole/copole" method — which Makransky
introduces in Ch. VII and then explicitly discards as *invalid for
Placidus* — is textbook-correct there. It isn't a workaround for Campanus;
PMP is the workaround, specifically because Placidus isn't a projection
system. A Campanus mundane position and a Placidus PMP are different
quantities for any body off the angles, so switching systems genuinely
changes computed dates — confirmed: with the default (Placidus) selected,
every existing date reproduces bit-for-bit; Campanus/Regiomontanus produce
materially different dates for off-angle bodies.

## Source material

- Makransky, Ch. IV: "The Campanus and Regiomontanus Systems" — house-cusp
  construction via the prime vertical pole; the pole/Q/W speculum; the
  per-system mundane aspectual-direction algorithms; "RAPT PARALLELS".
- Makransky, Ch. VII — the pole/copole derivation shown incorrect for
  Placidus but valid for Campanus/Regiomontanus.
- `C:\Users\jbaud\Downloads\++Bob Makransky Primary Directions_A Primer of Calculations I.pdf`.
  Poor scan quality, but **`pdftotext` extracts a usable text layer directly
  — page rendering/OCR-by-eye was not needed** (a change from what the
  original seed doc assumed). At least one confirmed transcription error in
  the scan: the Prince Charles reference speculum prints Moon's pole as
  15.58, which is a typo for 15.68 (only 15.68 reconciles with the book's
  own printed Q_Moon = 3.20). See "Rapt parallels" below for a second,
  larger apparent error in the same table's worked examples.

## Answers to the original open questions

1. **Is "under the pole" identical for Campanus and Regiomontanus?** Yes.
   Both divide by house circles through the N/S horizon points, so pole, Q,
   and W are computed identically for both — one shared speculum. They
   differ only in how the mundane **aspect point** is constructed: zenith
   distance along the prime vertical for Campanus, oblique ascension along
   the equator for Regiomontanus.
2. **How is this exposed as a setting?** A new, orthogonal enum
   (`PrimDirSystem`), not an extension of the existing `PrimDirMode`
   (Mundane/Zodiacal/Active — that governs ray construction and calc cost,
   a different axis). UI: a "Direction system" submenu in the Plain panel's
   Directions context menu, and a combo box in the "Directions && Speculum"
   settings group, both mirroring the existing `PrimDirMode` UI.
3. **Does the promissor side need equivalent treatment?** No — bare
   promissor, ray on the significator, unchanged from Placidus. Verified:
   the general Campanus/Regiomontanus formulas reproduce the classical
   angle-significator identities exactly when fed an angle's real (RA, Dec)
   (e.g. MC gives pole=0 ⇒ Arc = RA_promissor − RAMC).

## Implementation

**The key simplification, found while implementing:** Campanus Mundane
Position is exactly what `Star::pvPos` already computes —
`(swe_house_pos(RAMC, lat, eps, 'C', xx) − 1)/12 × 360`. Rather than
hand-transcribing Makransky's multi-case "ZD ALGORITHM" (whose OCR is
genuinely ambiguous — an "opposite sign" branch that numerically behaves
like the "same sign" branch in the one worked example available), CMP is
computed by reusing `swe_house_pos` directly: passing `eps = 0.0` makes its
internal ecliptic→equatorial step an identity, so it accepts `(RA, Dec)`
directly instead of `(ecl.lon, ecl.lat)`. This works uniformly for real
bodies and synthetic aspect points, gives a free cross-check (a real body's
computed CMP must equal its independently-computed `pvPos`), and sidesteps
circumpolar bodies entirely (Campanus/Regiomontanus have no analogue of
Placidus's "no semi-arc ⇒ no Mundane Position" problem).

From CMP, the rest follows via clean, unambiguous relations (all verified
against the book's own worked example, `CMP_Ap=272.32 → ZD_Ap=182.32 →
pole_Ap=1.82 → D_Ap=51.44`, reproduced exactly):

```
ZD   = |CMP − 90|                                    (no case logic)
pole = sign(lat) · |asin(sin(lat) · sin(ZD))|
Q    = asin(tan(Dec) · tan(pole))
W    = quadrant∈{1,4} ? RA − Q : RA + Q     (Regiomontanus Mundane Position)
```

`DirSpeculumEntry` (`astro-calc.h`) gained `zd/cmp/pole/q/w/quadrant`
fields, populated by `buildProjectionFields()` (only when a
Campanus/Regiomontanus system is active — Placidus never pays for this).

Three arc functions (`astro-calc.cpp`, next to `primaryDirectionArc`):

- `crMundaneConjunctionArc()` — shared by both systems (Formula IV-4/5/6):
  `Q_p = asin(tan D_p · tan pole_S)`; `W_p = RA_p ∓ Q_p` (sign by
  significator's quadrant); `Arc = W_p − W_s`.
- `campanusAspectPointArc()` — Ch. IV "CAMPANUS MUNDANE ASPECTUAL
  DIRECTIONS", the 7-step algorithm.
- `regiomontanusAspectPointArc()` — Ch. IV "REGIOMONTANUS MUNDANE ASPECTUAL
  DIRECTIONS".

All three verified against the book's own worked examples: `SU CONJ ME`
C-R direct (12.17°), `SA CONJ VE` C-R converse (−37.09°), `MO TRI SA`
Campanus mundane direct (14.30°), `MO TRI SA` Regiomontanus mundane
converse (−3.19°) — reproduced to within 0.02° (the book's own printed
precision) in all four cases, plus all 15 bodies' pole/Q/W/CMP in the
Prince Charles reference speculum.

Dispatch lives in `findPrimaryDirections()`'s significator-ray branch,
gated on `A::primDirSystem`.

## Deliberately out of scope (found while implementing, not built)

Two more book-documented techniques were identified but not built — real
added scope, not gaps in what was attempted:

- **Mundane parallels/contraparallels** (Ch. IV): a different construction
  (reflect across the meridian, not an aspect offset) with no ecliptic
  analog. Not in the existing `kRays` table at all (conjunction/sextile/
  square/trine/opposition only); Placidus doesn't support them either.
  Would need its own row type, not just a dispatch branch.
- **Campanus/Regiomontanus zodiacal directions**: the book puts the aspect
  offset on the **promissor's** longitude for this technique (confirmed:
  "the trine to the moon is at 0 VI 26" reproduces Moon's longitude + 120°
  exactly) — the opposite of this app's *existing* Placidus-zodiacal branch
  in `findPrimaryDirections()`, which offsets the **significator's**
  `eclLon`, and whose own code comment already flags itself "unverified
  against reference data." Building a C-R zodiacal variant would mean
  picking a side of a ray-placement question that predates this work and
  also affects Placidus. `primDirMode == prdZodiacal` combined with a
  non-Placidus system currently falls through to the mundane construction
  above — a genuine C-R mundane direction, just not this distinct technique.

## Rapt parallels (math built, not wired up)

Makransky's generalization: classically two bodies are carried by diurnal
rotation to equal mundane distances either side of an **angle**; Makransky
extends "an angle" to "the natal mundane position of any third body Z" — a
mundane-midpoint condition, `MP(X directed by α) + MP(Y directed by α) ==
2·MP_Z (mod 360)`, `Z` never itself directed.

No closed form exists (directing rotates the sphere about the celestial
pole, a different axis than CMP/W are measured against), and Makransky's
own open-secant iteration is unreliable — it converges to whichever root it
drifts toward, confirmed independently: its own seed formula,
`(RA_X+RA_Y)/2 − RA_Z`, gives 34.72° for the book's `SU/JU=ME` example
(close to the book's own answer, 28.72°/30.37°) but **−170.81°** for
`MO/UR=SU` — nowhere near the book's printed 3.37°/7.38°.

Built instead: `findRaptParallelArcs()` (`astro-calc.cpp`/`.h`) — a bounded
coarse-scan + bisect over a caller-supplied arc range, rejecting apparent
sign changes where the residual jumps by more than 90° between samples
(wrap artifacts of the mod-360 fold, not genuine roots). Validated:

- `SU/JU=ME`, both systems: reproduces the book's 28.72°/30.37° to within
  0.02°.
- `MO/UR=SU`: **does not** reproduce the book's printed 3.37°/7.38°. A wide
  search (±200°) finds exactly one root each system (−158.46° Campanus,
  −146.56° Regiomontanus) — landing next to the book's own seed (−170.81°,
  computed independently in Python from the book's own RA values), not
  anywhere near the printed answer. This is the same speculum table that
  already has one confirmed OCR typo (Moon's pole). Given the solver's own
  output is self-consistent with the book's own intermediate formula, and
  independently validated on a different triple, this reads as a second
  scan/print error in the book rather than an implementation bug — but
  isn't 100% certain, so anyone revisiting this should treat `MO/UR=SU` as
  unresolved rather than re-trusting it blindly.

**Wired into the Events table as a kind of primary direction** — not an
independent event type. First cut gave it its own `RP` toolbar toggle and
`etcRaptParallel` event type; reworked after review (the correct call —
"are they not, after all, a kind of primary direction?") to instead tag
rows `etcPrimaryDirections` and gate on `EventOptions::pdIncludeRaptParallels`,
exactly like `pdAnglesAsPromissors`. This buys two things for free that the
first cut had to build by hand: rows only ever show up when PD itself is
enabled (no separate visibility toggle to keep in sync), and every existing
PD-specific display/click special case (`EventsTableModel::
glyph()`/`display()`/`summary()`/`planetToText()`/
`getHouseRulershipWithNatalHouseString()`, and — the one genuinely new
behavior — the T/P/S-or-T/P/N-column focal-click preview of the legacy
Directions table) applies to rapt-parallel rows automatically, with zero
`|| eventType == ...` duplication anywhere.

- `AspectFinder::findRaptParallels()` (`astro-calc.cpp`, called right after
  `findPrimaryDirections()` inside the same `showPrimaryDirections()`
  gate, additionally gated on `pdIncludeRaptParallels`). Enumeration: Z
  restricted to the four angles by default (~420 triples for a 15-body
  chart), X/Y restricted to planets (classical), with `Events/
  raptParallelsAnyZ` as the opt-in toggle for arbitrary Z (~1,400 triples).
  The search date range maps to a symmetric arc window (`primaryDirectionDate()`
  always folds on `|arc|`, so one bracket-search call spanning
  `[-arcMax, arcMax]` covers both direct and converse hits); reuses
  `pdOrbDegrees` for the date-window orb rather than a separate setting.
  A no-op under Placidus (`findRaptParallelArcs()` already returns no hits;
  the finder pass bails early rather than paying for a search that can only
  be empty).
- **Display.** A rapt parallel has three bodies; the Events table has two
  body columns. Solved by reading the code, not by reusing what the seed
  version of this doc assumed: `ChartPlanetId` already has a built-in
  second-planet slot (`ChartPlanetId(pidX, pidY)`, `isMidpt()`), and its
  `name()`/`glyph()` already render a midpoint as `"Sun/Jup"` with zero new
  code. **No synthetic `Planet` object is needed at all** — the
  `std::deque<A::Planet>`/dangling-pointer mechanism this doc originally
  pointed at (`_syntheticMidpointPlanets`) turned out to be something else
  entirely: a chart-wheel-drawing-only convenience (letting the legacy
  `calculateAspects(ChartPlanetPtrMap)` API compute aspect lines for a
  paran-click midpoint), owned per-`AstroFileHandler`, rebuilt on every
  redraw — unrelated to `HarmonicEvent`/promissor-significator construction,
  which has always been synthetic-Planet-free. X/Y's midpoint gets the
  promissor role (`PlanetLoc` desc `"Dir"`/`"Con"`, same arc-sign convention
  as PD), Z the bare significator — same `PlanetRangeBySpeed` speed-key
  trick PD uses (`2e-9`/`1e-9`) to keep promissor/significator column-stable.
- **Distinguishing a rapt-parallel row from an ordinary PD row** (both now
  carry the same `etcPrimaryDirections` tag): no marker field needed —
  `ChartPlanetId::name()` always renders a midpoint promissor as `"X/Y"`, so
  a literal `/` in the promissor's rendered text is the discriminator. Used
  by `clickedCell()`'s focal-click handler to append `" (Rapt)"` to the
  anchor-row label (`"(Dir) Sun/Jup → Asc (Rapt)"`), and could be reused
  anywhere else a rapt-parallel-specific display tweak is ever wanted
  without touching the data model.
- Focal-click preview: since rows are genuinely `etcPrimaryDirections`, the
  existing single-click behavior (`Transits::clickedCell()`, preview the
  legacy Directions table pruned to nearby directed dates) and double-click
  no-op guard apply unchanged — no new branches were added for either.
- Known imperfection: the midpoint promissor's rulership+natal-house display
  mode (Ctrl+Click-twice cycling) shows only one of the two paired bodies'
  natal house (`ChartPlanetId`'s canonicalizing swap picks one), not a
  genuine midpoint house — cosmetic, not a crash, not yet fixed.

## The chart-wheel PD marker

The motivating goal for this whole effort — recorded here because the
Campanus/Regiomontanus work above is what made it nearly free. **Built**,
in two phases (`docs`/plan history: `mellow-giggling-pnueli.md`).

### Phase 1 — `pvPos` follows the active direction system

Planning the marker surfaced a blocking inconsistency: every `pvPos`
computation hardcoded `swe_house_pos(..., 'C', ...)` (Campanus), regardless
of `A::primDirSystem`. So Mundane display and `amcPrimeVertical` aspects
always showed Campanus positions even when directions were being computed
in Placidus (the default) or Regiomontanus — a phantom marker placed at a
Placidus-derived arc on a Campanus-scaled wheel would land visibly short of
its significator. Fixed at the source rather than patched at draw time:
`A::mundaneHouseSystem()` (`astro-calc.h`) maps `primDirSystem` to
`'P'`/`'C'`/`'R'`, and every `pvPos`-computing `swe_house_pos()` call
(`calculatePlanet`, `calculateStar`, `calculateComposite`,
`relocalizedPvPos`) now passes it instead of a literal `'C'`. Verified: SE's
own Placidus branch (`swehouse.c`, case `'P'`) is algebraically identical to
this codebase's own `placidusMundanePosition()` — swapping the letter is not
a change of meaning — and additionally handles circumpolar bodies via the
Otto Ludwig procedure instead of the NaN that lambda returns.

`primDirSystem` now feeds `calculatePlanet()`, which it didn't before —
`Plain::setPrimDirSystem()` and the `primDirSystemChanged` branches in both
`Plain::applySettings()` and `Chart::applySettings()` now call
`file(i)->calculate()` (the safe, no-signal path — see the bugs section
below), mirroring how `primDirMode` already worked. The `[PV]` button was
relabeled `[Mundane]` (label only — `pvPos` is no longer literally a
prime-vertical quantity except under Campanus).

### Phase 2 — a real bi-wheel, not a marker (superseded, see below)

The first cut here was a single-marker overlay (`Chart::drawDirectionFigure()`
— a ghost phantom + travel line drawn directly on the natal wheel for the
clicked row's promissor). It worked, but didn't answer the actual question:
seeing a directed chart means seeing *where everything else is too*, not one
body's arrival point. **Removed entirely** and replaced with a genuine
bi-wheel — see "The Primary Direction bi-wheel" below.

### The Primary Direction bi-wheel — built

**The conceptual resolution.** A PD event's derived chart is *not* a second
independently-timed chart the way a transit or progression is — it's the
*same* natal bodies (RA/Dec unchanged; direction moves nothing in the sky),
viewed through a mundane frame whose RAMC has been rotated by the event's
arc. Two consequences fall out of that:

- **Always anchor to the natal chart's angles**, regardless of the app's
  general Circle-start preference ("Ascendant (prefer outer)" has no meaning
  here — there's no independent outer chart to prefer). Mundane position is
  defined so Asc is always 0 *for whatever RAMC you evaluate it with*; the
  natal wheel's own Asc (pvPos=0, natal RAMC) and the directed promissor
  (pvPos=0 by construction of the arc solve) land at the *same screen point*
  automatically once both draw through one shared, natal-anchored rotation.
  No relocalization needed, no "which wheel is outer" branch.
- **Angles need the same directed treatment as any other body when they're
  the promissor.** A chart's own Asc/MC/Desc/IC are hardcoded to mundane
  0/270/180/90 — a display convention for whatever RAMC built the chart, not
  a directed computation. "Ascendant directed to Jupiter" needs Asc treated
  as a body with its own fixed natal RA/Dec, pushed through the same
  shift-and-reevaluate math as everything else — applied uniformly to all
  four angles in the derived chart, not just whichever one happens to be a
  given event's promissor.

**Two things turned out simpler than expected:**
- `TypeDerivedPD` already existed in `A::FileType` (already in the chart-type
  combo, already excluded from `classifyDirChart()`) — but `calculateAll()`
  only ever branched on `isProgressed()`, so selecting it silently produced
  the *same* secondary-progression chart as `TypeDerivedProg`. Fixed as a
  side effect of finally giving it its own branch.
- No new `EventType` was needed. `Transits::doubleClickedCell()`'s
  `etcPrimaryDirections` branch was *also* a deliberate no-op (mirroring the
  single-click preview's "never touch Type/GMT" stance), and double-click is
  already every other event type's "open a focused view" gesture in that
  function. Single-click keeps the existing Directions-table preview
  unchanged; double-click (gated on the T/P/S or T/P/N column, same as
  single-click) now opens the real bi-wheel via the same generic
  `setType/setBaseChart/emit updateSecond|addChart` pipeline Returns and
  Progressed charts already use — shift-double-click opens it as a bi-wheel
  in a new tab (current natal as file 0), plain double-click opens it alone.

**`InputData` additions** (`astro-data.h`): `_directedArc` (signed RA
degrees) and `_directedSystem` (an `A::PrimDirSystem`, stored as `int` —
`astro-data.h` predates `astro-calc.h`, no circular include) are frozen at
click time via `AstroFile::setDirectedArc()`, alongside the existing
`_isPrimaryDirected` flag `setType()` now sets for `TypeDerivedPD` (parallel
to `_isProgressed`). Frozen, not read live from `A::primDirSystem`, because
each system solves a genuinely different arc for the same promissor/
significator pair — re-evaluating a frozen arc against a different system's
house formula would break the "promissor lands exactly on significator"
invariant.

**`calculateAll()`'s new branch** (checked *before* `isProgressed()`, and
gated on `hasBaseChart()` too — selecting the type directly via the combo,
with no baseChart set, falls through to a normal chart rather than
reconstructing "natal" from the epoch-0 default):
1. Reconstruct natal `InputData` from `baseGMT()`, get `natalHouses` via the
   ordinary `calculateHouses()`.
2. `scope.houses.RAMC = natalRAMC + directedArc()`, then rebuild
   `Asc`/`MC`/`RAAC`/`RADC`/`cusp[]` from that RAMC directly via a new
   `calculateHousesFromRAMC()` (mirrors the existing `calculateHouses(input,
   progressedMC)` overload's body from `swe_houses_armc` onward, but skips
   its MC→RAMC round-trip — the target RAMC is already known).
3. Regular planets: unchanged from the existing loop, just fed `calcInput =
   &natalInput` (real natal ephemeris position) and the RAMC-shifted
   `scope.houses` (directed mundane position) — no copy-and-relocalize step
   needed, `calculatePlanet()` already does exactly this.
4. Angles: each gets its own fixed natal RA/Dec recovered the same way
   `Horoscope::applyExprecession()` already recovers Asc/Desc/MC's tropical
   longitude (ayanamsa-add for a true sidereal zodiac, since the house-
   position call is tropical-only), then evaluated against the shifted
   houses via the `eps=0` identity trick `buildProjectionFields()` already
   uses. `eclipticPos`/`equatorialPos` originally stayed at the *undirected*
   natal values — only `pvPos` reflected the direction — so ecliptic and
   equatorial display showed the two rings overlaid exactly. This doc called
   that "harmless, expected". **It was neither** — see "Directing the sky
   position too" below.

**Chart-side bypasses** — three places, all because a `TypeDerivedPD`
file's `pvPos` is *already* the final, correct directed value by
construction; relocalizing it again would evaluate the already-shifted
position back against the *other* file's unshifted RAMC, silently erasing
the direction:
- `Chart::ringFileIndex()` — forces the anchor to the *other* file whenever
  either file is `TypeDerivedPD`, overriding `circleStart` entirely.
- `Chart::displayPvPos()` — returns `pvPos` raw for both files in a
  `TypeDerivedPD` pair, before the normal `circleStart`-based ladder runs.
- `updateAspects()`'s `setPvFrameFile()` call — passes `-1` (disable
  cross-file aspect relocalization) for a `TypeDerivedPD` pair, same as the
  existing `Start_ZeroDegree` case.

The non-reference file's angle/house grid in mundane mode was already hidden
by the earlier bi-wheel reference-wheel work (`fileIndex != ringFileIndex()`)
— falls out for free once `ringFileIndex()` is forced away from the
`TypeDerivedPD` file.

**Declination** is unaffected by direction (doesn't depend on RAMC), so a
`TypeDerivedPD` file's declinations are identical to natal's —
`Chart::drawDeclinationBodies()` skips that file's row entirely rather than
drawing a redundant duplicate.

**Wiring.** Single-click on a PD row's T/P/S or T/P/N cell does two things:
the existing Directions-table focal preview on file(0) (unchanged), *and*
builds file(1) as the directed chart via the in-place
`transitsAF()`/`emit updateSecond(taf)` path — the same one Returns and
Progressed charts use. Double-click opens it in a new tab instead
(`new AstroFile` + `addChart`/`addChartWithTransits`), matching every other
event type's gesture. Note these are genuinely different pipelines: the
in-place one is what gives you the bi-wheel in the current tab.

**Two bugs this surfaced, both worth knowing:**

- **`Chart::getCircleMarker()` resolved a body's wheel by VALUE, not
  identity.** `Star::operator==` compares only `name` + `eclipticPos`, and a
  directed chart keeps its natal `eclipticPos` — so every file(1) body
  compared equal to its file(0) twin, first match won, and every aspect line
  touching a directed body anchored to the *natal* marker. Now resolves by
  pointer identity first (`aspectPlanet()` hands back a pointer into the
  file's own horoscope whenever PV relocalization is inactive), falling back
  to value comparison for the relocalized-copy case. This was latent for
  ordinary PV bi-wheels too — two real charts just never share an ecliptic
  position to the bit.
- **The observer-location plumbing corrupts any chart that reconstructs a
  natal basis from its own location.** `transitsAF()` *is* file(1), and five
  sites in `Transits` stamp the tab's transit location onto it, assuming
  file(1) is a freely-relocatable transit chart. For a directed chart that
  silently relocates the natal basis `calculateAll()` rebuilds, changing
  every directed position — it presented as "the aspect table recomputes
  wrong after a date-range change", with the chart type and file(0)
  untouched. Guarded by `Transits::transitFileIsDirected()`.
  **Still exposed: `TypeDerivedProg`/`TypeDerivedSA`.** Their `calculateAll()`
  branch reconstructs `natalInput` from the file's own location the same way,
  and they get the same stamping. Relocating a tab with a progressed chart
  open should shift its natal basis. Not fixed here only because it was out
  of scope; the guard is one condition away from covering them.
  Related fragility: `clickedCell()` sets the natal location *before*
  `setType(TypeDerivedPD)`, and the guard keys off type — so that ordering is
  load-bearing.

**Deferred, still:** the "focalized mundane aspects" idea (natal bodies in
mundane — not ecliptic — aspect to the significator, for context on why a
date matters); intermediate house cusps (2/3/5/6/8/9/11/12) don't get the
directed-angle treatment, since they're never drawn for this file type; the
mundane-vs-ecliptic aspect toggle below. Also unresolved: `angleTransit[]`
for a directed chart pairs `jd0` (natal instant) with `RAMC0` (directed
RAMC), so its datetimes sit ~arc/15 sidereal hours off. Unreachable now that
the time-derived Plain sections are greyed out for `TypeDerivedPD`, but still
in the data for any future consumer.

### Deferred: the full-ring scrubber view

A slider-driven arc `α`, applied live rather than frozen from a click —
answering "what's mundanely active around this age" rather than "why did
this specific row fire." The bi-wheel above is the same shape as this with
the arc source swapped: a `TypeDerivedPD` chart whose `setDirectedArc()` is
continuously re-set from a slider instead of one PD row, using the exact
same `calculateAll()` branch and chart-side bypasses already built.

One caveat to resolve when this phase starts: in the existing PV display
mode the outer ring still reads "Aries/Taurus/…" while actually meaning
houses 1-12 (no mode branch in the sign-drawing code). Tolerable for the
existing mundoscope; likely wants relabelling for a PD bi-wheel where the
distinction matters more. (The sign ring itself is now projected correctly —
see below — so the remaining gap is the *labelling* of the outer ring, plus
the mundane-vs-ecliptic aspect toggle, which the scrubber ring is where it
actually gets exercised, since it displays alongside real
ecliptic-coordinate content.)

### Sign delineation in the Mundane / Equatorial wheel — built

The `[PV]` → `[Mundane]` button rename (label only, deliberately *not* fully
coupled to the house-system combo — someone may want e.g. Equal-house cusps
displayed while still directing by Campanus) landed with the Phase 1/2 work
above. The sign ring itself has now been fixed too.

**The bug.** The zodiac ring was laid out as 12 equal 30° sectors in *every*
draw frame, straight off the static `zodiac.signs` table. In Mundane mode
that made the ring claim "house 1 = Aries, house 2 = Taurus, …" regardless of
what was rising — the sectors were really the *houses* (mundane houses are
exactly 30° each) wearing sign glyphs. Verified on a chart with Asc 15
Gemini: the mundane wheel put ♈ on the Ascendant. Equatorial mode had the
identical defect (bodies plot at RA, ring still at ecliptic longitudes).

**The fix.** `A::eclipticPointDisplayAngle()` (`astro-calc.cpp`, beside
`relocalizedPvPos()`) projects a point *on* the ecliptic into whichever frame
the wheel is drawn in — identity for ecliptic, `swe_cotrans` to RA for
equatorial, and the same `swe_house_pos(..., mundaneHouseSystem(), ...)` call
that produces `Star::pvPos` for mundane. Sign spans come out genuinely
unequal outside the ecliptic frame, which is the whole point. True sidereal
zodiacs (ids 2/3) get the ayanamsa added first, since both conversions are
tropical-only — same pattern as `horoscopeTropicalEquatorialPos()`.

- **Re-layout, not bake-in.** The ring was positioned once in
  `createScene()`; `updateScene()` only counter-rotated the glyphs. But the
  projection depends on RAMC/latitude/obliquity, which change on time and
  location edits that never rebuild the scene. Geometry moved into
  `Chart::layoutZodiacRing()`, called from both. Sector dividers are tracked
  in a new `signBorders` list (parallel to `signIcons`) and repositioned with
  `setLine()`/`setPos()` — **not** `moveBy()`, which is cumulative and would
  drift on every re-layout.
- **Gradient wraparound.** Exactly one sector straddles 0° in a projected
  frame, giving `a1 > a2`; gradient stops are sorted, so emitting that pair
  as-is inverts the band across the whole wheel. Split at the seam into
  `(a1→1.0)` and `(0.0→a2)`.
- **Degenerate fallback.** If any boundary fails to convert, or the twelve
  forward gaps don't sum to exactly one 360° turn, the whole ring falls back
  to the equal-30° layout — deterministic, rather than a scrambled wheel.
  Only reachable above ~66.5° latitude (boundaries sit on the ecliptic, so
  `|dec| ≤ eps`).
- **House grid restored.** With the ring no longer doubling as the house
  grid, Mundane mode now draws all twelve cusps (`cuspate(30*i, i)`) instead
  of only the four angles. Equatorial cusps are unchanged — houses are not
  equal in RA.
- **Cusp labels.** `cuspate()` named a zodiac sign for every cusp in every
  frame, so a mundane cusp 0 read "I+Aries" on every chart; it also read the
  *post-clockwise-flip* angle, naming the wrong sign on mirrored wheels. Both
  fixed: labels use the true angle, and only name a sign in the ecliptic
  frame.

Still deferred: mundane-aspect display mode (CMP/MP separation instead of
ecliptic longitude separation) when `[Mundane]` is active. The seam already
exists — `aspectModeAngle` in `drawMidpointFigures()` and the `repose` lambda
are both routed through `Chart::displayPvPos()` — so this is a metric swap on
an existing switch, not new infrastructure.

### Bi-wheels: the reference wheel, and precession — built

A mundane position is a **local-frame** quantity (a house position against
one chart's own RAMC, latitude and epoch), so a bi-wheel has to pick one
wheel's frame and draw everything in it.

- **One reference-wheel rule.** `Chart::ringFileIndex()` — file(1) for
  "Ascendant (prefer outer)" bi-wheels, else file(0) — now drives the
  rotation, the zodiac ring's projection, the sign set, and the angle/house
  grid. The sign-ring work above originally read `file()` (always file 0)
  while the rotation anchored to file(1), so with the shipped default
  (`circleStart=2` in `nsis/default-settings.ini`) the ring was projected
  against the *inner* chart while the wheel was rotated to the *outer* one.
  Note this is deliberately **not** the same as the body-relocalization
  anchor passed to `setPvFrameFile()` in `updateAspects()`, which returns
  `-1` for `Start_ZeroDegree` to disable relocalization entirely — the ring
  still has to be laid out against *some* file, and that is file 0.
- **Only the reference wheel's angles are drawn** in mundane mode. Previously
  `cuspate(30.0 * i, i)` ran for every file, so both wheels' grids landed on
  the identical 0/90/180/270 no matter how far apart their times and
  locations were. The other file's bodies are still shown, relocalized into
  the reference frame. Ecliptic and equatorial branches are untouched — those
  are shared frames, so per-file cusps are correct there.
- **Relocalization is now precession-corrected.**
  `A::relocalizedMundanePos(body, bodyScope, refScope)` replaces the old
  cross-file use of `relocalizedPvPos()`: it converts the body's stored
  *tropical* ecliptic position to RA/Dec, carries it from its own epoch to
  the reference chart's via `exprecess_equatorial()`, then reads it through
  `swe_house_pos(..., eps=0, ...)` (the identity trick from
  `buildProjectionFields()`). Without this, a natal-epoch longitude was being
  interpreted in the reference epoch's frame; the equinox precesses
  ~50.3″/yr, so the whole inner wheel sat ~0.9° off for a 1963 chart read in
  2026, growing with the chart's age. Identity when the epochs match.
  - Starting from `tropicalEclipticPos` rather than `equatorialPos` is
    deliberate: for a sidereal chart the latter is computed with
    `SEFLG_SIDEREAL` and is not trustworthy tropical RA, and
    `tropicalEclipticPos` is present on fixed stars too (`Planet`-only
    helpers like `horoscopeTropicalEquatorialPos()` would not cover them).
  - `AstroFileHandler::preparePvRelocalization()` was switched in the same
    pass. Drawn positions and aspect math **must** use the same projection —
    letting them diverge is the `project_pv_aspect_frame_mismatch` bug class,
    which shows up as cross-file aspect lines missing their markers.
  - `relocalizedPvPos()` is kept as the lower-level primitive for projecting
    into a synthetic frame belonging to the *same* chart and epoch, where
    precession is irrelevant — the PD marker's arc-rotated RAMC is its only
    caller.

**Mundane positions are ayanamsa-independent**, and should stay that way:
`calculatePlanet()` re-fetches each body with `flags & ~SEFLG_SIDEREAL` into
`tropicalEclipticPos` before calling `swe_house_pos()`. Where a body sits
relative to the horizon and meridian is physical, not a zodiac convention.
The one place ayanamsa legitimately enters is sign *boundaries*
(`eclipticPointDisplayAngle()`), since a sidereal boundary is at a different
physical longitude than the tropical one.

**Two `applyExprecession()` gaps found and left alone** (recorded so the PD
bi-wheel work doesn't re-derive them): it is gated on
`A::aspectMode == A::amcEquatorial`, and mundane/PV is a separate display
toggle that leaves `aspectMode` at ecliptic — so it never fires in mundane
mode. It also rewrites only `equatorialPos` and `houses.RAAC/RAMC/RADC`,
never `tropicalEclipticPos`. The precession fix above deliberately routes
around both rather than changing global `Horoscope` state.

Also corrected: `AstroFile::getParanGroupPlanets()`'s comment claimed
"fileId 0 = transit, 1 = natal ex-precessed". It is the reverse — **0 =
natal, 1 = transit** — per the finder's own construction, `paranAngleString()`,
`Chart::drawParanFigures()`, and the chart's file ordering.

## Pre-existing bugs found while testing this feature

Not part of Campanus/Regiomontanus itself, but surfaced by exercising the
new settings, and all pre-existing (not introduced by this feature — they
just hadn't been noticed before):

1. **Handler-ordering race.** Switching `Mundane/primDirSystem` or
   `Mundane/primDirMode` via the Plain panel's settings dialog landed "one
   change behind" — the Events table showed the *previous* system's dates,
   catching up only on the *next* change. `AstroWidget::applySettings()`
   calls its handlers in construction order (`Details, Harmonics, Transits,
   Speculum, Chart, Planets, Plain` — `mainwindow.cpp`), so
   `Transits::applySettings()` runs *before* `Plain::applySettings()`, but
   the former compared the dialog's new value against the live
   `A::primDirSystem`/`primDirMode` globals, which the latter doesn't
   mutate until afterward. Fixed by having `Transits::applySettings()`
   read and mutate those globals itself, guarded by `s.contains(key)`
   (`Mundane/*` keys are Plain's own, not Transits' — absent from any
   settings snapshot that didn't come through the combined multi-handler
   dialog, e.g. `Customizable::loadSettings()` at startup; reading them
   with a fallback default and unconditionally overwriting the live
   globals was silently resetting the user's selection and treating that
   as a "change" on every such call). `Plain`'s own change-detection was
   decoupled onto private shadow fields (`_lastPrimDirMode`/
   `_lastPrimDirSystem`) so it can't be short-circuited by a sibling
   handler having already normalized the global first.
2. **Quick-menu never notified the Events tab at all.**
   `Plain::setPrimDirMode()`/`setPrimDirSystem()` (the right-click menu, as
   opposed to the settings dialog) called `file(i)->calculate()` directly,
   which bypasses `AstroFile::change()` entirely, so
   `Transits::filesUpdated()` (a `changed(Members)` slot) never fired.
   A signal-emitting fix (`change(GMT, false)`, piggybacking the
   GMT-triggered recalc+notify path to carry a settings-only change) was
   tried and **reverted**: it corrupted `file(0)`-dependent event search
   entirely (natal-based passes — T=N, TNA, Par=N, P=P, P=N — stopped
   working; transit-only passes kept working), reproducing at every app
   launch since `Plain::applySettings()` runs automatically as part of
   startup's settings load, not just from interactive use. Suspected cause:
   `emit changed(GMT)` fans out to every `AstroFileHandler`, and something
   in that fan-out races or re-enters against a finder concurrently reading
   `file(0)`. Final fix: plain `calculate()` (recomputes `scope` in place,
   emits nothing — proven safe, what this codebase used successfully
   before) for the cases that need a real recompute (`primDirMode`), plus
   `AstroFile::markEventsForRecalc()` (pure bookkeeping — sets the stale
   flag, clears the search manifest, no recompute, no signal) so the
   Events tab picks up the change on its own next check rather than being
   force-pushed a notification.
3. **The same handler-ordering race, a second time, for Chart.** Once
   Phase 1 made `primDirSystem` feed `calculatePlanet()`, cycling the
   Direction System setting in the dialog and clicking Apply showed the
   *previous* system's positions in Mundane display, one Apply click behind
   — the same symptom as bug 1, in a different handler pair.
   `Chart::applySettings()` (`chart.cpp`) unconditionally calls
   `refreshAll()`, but Chart runs *before* Plain in handler order, so it
   painted using whatever `pvPos` `calculate()` had produced on the
   *previous* Apply — Plain's own `calculate()` (this Apply's) hadn't run
   yet. Comparing against the live `A::primDirSystem` global doesn't work
   here either, for the same reason bug 1's fix couldn't: by the time
   Chart's turn comes, `Transits::applySettings()` has already mutated it
   (Transits runs even earlier). Fixed the same way as bug 1: gave `Chart`
   its own shadow field (`_lastPrimDirSystem`, `chart.h`) so it detects the
   change and calls `file(i)->calculate()` itself, before `refreshAll()`
   paints, rather than relying on Plain having already done so.
4. **Bug 3's fix created a dangling-pointer crash (0xC0000005 in
   `describeAspectsTable`).** Giving `Chart::applySettings()` its own
   `file(i)->calculate()` added a *third* caller that replaces a shared
   `AstroFile`'s `Horoscope` — and `calculate()` deliberately emits nothing
   (see bug 2). Plain caches `AspectList`s holding `Planet*` into exactly
   that storage. `Plain::filesUpdated()` and `viewSettingsUpdated()` both
   clear `aspectsCached` unconditionally, but `Plain::applySettings()` only
   cleared it for the four settings it recognized, then called `refresh()`
   unconditionally — so after a Direction-System Apply it rendered a cache
   whose every pointer Chart had just freed. `describeAspectsTable`
   null-checks its planet pointers, so a *dangling* one goes straight
   through to reading a freed `QString`. Fixed by clearing `aspectsCached`
   unconditionally before that `refresh()`; an Apply click is not a hot
   path, and recomputing costs far less than the render it precedes.
5. **`Plain::applySettings()` read dialog-only keys without `s.contains()`.**
   `Chart` and `Transits` guard `Mundane/primDirSystem` / `primDirMode`
   behind `s.contains(...)` because those keys exist only when `s` came from
   the combined settings dialog. Plain didn't: it read the fallback
   (`pdsPlacidus` / `0`) as a deliberate user choice, clobbering both the
   live `A::` global and its own shadow field — which then disagreed with
   the other handlers' shadows, re-creating the bug-1/bug-3 lag class from a
   new direction. Now guarded to match.

## Angles are not in `horoscope().planets`

Found while wiring the directed angles into the bi-wheel, and worth stating
plainly because it invalidates an assumption several places appear to make:

`A::getPlanets(inclAsteroids, inclCentaurs)` returns
`getInnerPlanets() << getOuterPlanets()` — Sun through Pluto and the nodes,
**no angles and no house cusps**. `calculateAll()` loops over precisely that
list, so its `if (id == Planet_Asc) … else if (id == Planet_IC) …` chain
(and the identical chain in `calculateComposite()`) is **dead code**.
`horoscope().planets` has never contained an angle.

This is well camouflaged: the branches read as authoritative, `chart.cpp`'s
planet loop explicitly *skips* `p.id >= Planet_Asc && p.id <= House_12` as
though they were present, and `planets.value(Planet_Asc)` returns a
default-constructed `Planet` instead of failing — so the symptom is a silent
zero rather than a crash. It cost two separate debugging round-trips: the
`sigPlanets.contains=false` trace during the PD-marker work (misdiagnosed
then as "`calculateAll()` is composite-only") and, later, the four directed
angle cusps all rendering at 0°.

Consequence for this feature: the `directedAnglePv()` values were being
computed into dead branches. They now go to **`Horoscope::anglePv[4]`**,
indexed `pid - Angles_Start` = { Asc, IC, Desc, MC }, defaulting to the
defining 0/90/180/270 (correct for any ordinary chart — an angle sits where
it does by construction) and overwritten only for `TypeDerivedPD`, where each
entry is that natal angle's arrival place under the directed RAMC.
`Chart::updatePlanetsAndCusps()`'s mundane branch draws cusps 0/3/6/9 of the
directed wheel from it, labelled "Directed Asc/IC/Desc/MC"; the eight
intermediate cusps stay hidden, since nothing directs them and no PD event
can name them.

Deliberately **not** fixed by making `getPlanets()` yield angles:
`scope.planets` feeds `A::calculateAspects(set, scope.planets)`, so angles
would silently begin aspecting in every chart in the app.

## Rapt conjunctions — built

Noticed while watching PD bi-wheels: one body's directed mundane position
overtakes another's. Both bodies are directed by the same arc, yet their
mundane positions converge and cross. Two questions followed — is it in the
literature, and is it computable near the rapt-parallel loop.

### The literature

**Rapt motion is Placidus's own third category of direction**, alongside
direct and converse: both bodies are carried together by the *primum mobile*
rather than one being moved onto a fixed natal place. Its canonical member is
the **rapt parallel** — "the aspect between two points, both rapt by the
motion of the Primum Mobile, when they have the same distance from the
meridian, which is proportional to their semi-arcs." The contrast is explicit
in the sources: *"differently from the mundane parallel, where one point is
fixed on the natal position and the other moves, here both are directed."*
So the observation sits inside an established classical category.

The **same-side case appears not to be a named technique**, though. The rapt
parallel is always given as *equidistant, opposite sides of an angle*.
Nothing turned up treating "both bodies directed until their mundane
positions coincide" as a technique in its own right. "Rapt conjunction" is
this codebase's coinage, not a citation.

### The algebra: the rapt parallel with one sign flipped

Under Placidus, mundane position is a function of the proportional meridian
distance MD/SA. Direction by arc α advances both bodies' MD equally while
their semi-arcs stay fixed:

| | condition | solution |
|---|---|---|
| Rapt parallel (opposite sides) | (MD_A+α)/SA_A = −(MD_B+α)/SA_B | α = −(SA_A·MD_B + SA_B·MD_A) / (SA_A **+** SA_B) |
| Rapt conjunction (same side) | (MD_A+α)/SA_A = (MD_B+α)/SA_B | α = (SA_A·MD_B − SA_B·MD_A) / (SA_B **−** SA_A) |

The same "golden rule" — **sum in the denominator for the parallel,
difference for the conjunction**. Three consequences:

- **Bodies of equal declination never overtake.** Equal declination means
  equal semi-arc, which zeroes the denominator. Overtaking is caused
  *entirely* by the two bodies traversing the mundane frame at different
  rates. Near-equal declinations push α far outside any lifetime.
- **So most pairs legitimately yield nothing in range.** A sparse result set
  is correct here, not a bug — this is stated at the three places someone
  might otherwise "fix" it.
- The parallel's denominator is a sum and can never vanish. Presumably why
  *it* became the classical technique and this did not.

The closed form is only piecewise valid (a body crossing the horizon swaps
SDA for SNA) and has no Campanus/Regiomontanus analogue, so the
implementation reuses the parallel's bracket-and-bisect rather than solving
directly. It is the better conditioned of the two searches:
`foldSigned180(MP_A − MP_B)` is continuous everywhere, individual 360° wraps
at the Ascendant cancelling in the folded difference, whereas
`shortArcMidpoint()` has a genuine branch flip when X and Y near opposition.

### Supported under all three systems, unlike rapt parallels

A mundane **midpoint** needs a great circle, which is why
`findRaptParallelArcs()` bails on Placidus. Mundane **equality** does not —
it is the standard Placidian mundane conjunction — so `findRaptConjunctionArcs()`
runs under all three.

This exposed a latent bug: `mundanePositionAtArc()` ended
`return system == pdsCampanus ? e.cmp : e.w;`, so Placidus fell through to
`e.w`, the *Regiomontanus* quantity. Wrong, but unreachable, because the only
caller bailed on Placidus first. Adding a caller that doesn't made it
reachable, so it now has a proper `'P'` branch reading PMP off
`swe_house_pos` with the same `eps=0` identity trick. `buildProjectionFields()`
was deliberately left alone — its hardcoded `'C'` feeds Makransky's C-R
formulas and is not a display quantity.

### Column order and colour

Neither body is a promissor, so the pair is ordered by **actual mundane
speed at the crossing** (`mundaneSpeedAtArc()`, a central difference — under
Placidus d(MP)/d(arc) ∝ 1/SA, so the faster body is simply the one with the
smaller semi-arc). The faster body is the overtaker, so it takes the
promissor's usual left-hand column. The existing near-zero `2e-9`/`1e-9`
speed sentinels are reused rather than plumbing real speeds through: the
Events table already splits a same-file pair faster-left/slower-right, so
only their *order* needs to carry meaning, and nothing reading `speed` as a
real rate is misled.

The right-hand column is now gold for **every** PD row whose significator is
fixed at radix — ordinary directions and rapt parallels alike, extending the
existing transit×natal gold (`mixedMode()` misses these, both locs being
fileId 0). Rapt conjunctions deliberately stay plain: nothing is fixed.

That colour does double duty. `transits.cpp` identifies a rapt parallel by
the midpoint slash in the promissor name, which a rapt conjunction has no
reason to carry — so without intervention it would render identically to an
ordinary mundane conjunction of the same two bodies. **Right column gold =
fixed natal significator; right column plain = a second directed body.** No
new desc token, and the `"Dir"`/`"Con"` arc-sign parser is untouched.
`isRaptConjunction()` keys off the fact that `findRaptConjunctions()` marks
*both* locs `"Dir"`/`"Con"`, where an ordinary direction marks only the
promissor.

It tests those exact tokens rather than mere non-emptiness, which was the
first attempt and was wrong: a **ray** direction (`ray.offset != 0`) puts the
Almagest aspect-glyph codepoint plus an optional dexter/sinister `"D"`/`"S"`
in the *significator's* desc, so "both non-empty" would have misclassified
every square/trine/sextile/opposition direction as a rapt conjunction and
stripped its gold. Only conjunction directions leave it empty.

### The interpretive caveat

Every classical direction anchors to something natal: an ordinary direction
to its significator, a rapt parallel to a natal angle (or Makransky's Z). **A
rapt conjunction anchors to nothing natal** — it is a relation between two
moving bodies alone. That is the standing objection to rapt parallels, and it
lands harder here. Recorded, not resolved; the ask was for the computation,
with interpretation explicitly deferred. The uncoloured right column is that
caveat made visible.

### Text marker, not only colour

The colour above discriminates rapt conjunctions in the **Events table
only**. The derived chart's name and the row injected into the focused
Directions table are plain text with no colour to lean on, and both are built
by appending `" (Rapt)"` when the promissor's rendered name contains a `/` —
a test a rapt conjunction can never pass, having no midpoint promissor. So
both read exactly like an ordinary mundane conjunction of the same two
bodies.

Both sites (`clickedCell()`'s `focusLabel`, `doubleClickedCell()`'s `desc`)
now append `" (Rapt conj)"` via `EventsTableModel::isRaptConjunction()`.
`" (Rapt)"` is left as-is for the parallel; renaming it to `" (Rapt par)"`
for symmetry is a one-line change if the bare form ever reads as ambiguous.

## Clearing a PD focalization

A PD click had no "off". Every other cell in the Events table *replaces* the
focus and nothing clears it, so once a Directions row was clicked the tab was
stuck with that direction's constraints. `Harmonics::clearFocal()`
(`harmonics.cpp:462`) already solves the same problem for the harmonics table
by toggling on a re-click of the focal row; Transits had no equivalent.

**Focalized and unfocalized show the same wheel.** The directed bi-wheel
stays up either way — clearing does not tear it down. What differs is that
the focalized state:

- sets `focalPlanets`, which is what switches
  `AstroFileHandler::calculateAspects()` (`astro-gui.cpp:1187`, file(0)'s
  tables) and `calculateSynastryAspects()` (:1381, the bi-wheel's) onto their
  focal path and so constrains the aspect tables; and
- populates the direction-focus fields, which prune the Directions table to
  the event's date.

Re-clicking the focal row clears both. `Transits::_pdFocalRow` (a
`QPersistentModelIndex`, so it survives sorting and filtering) holds the
state, and is reset when any non-PD row is clicked so returning to the
Directions rows later starts focalized rather than matching a stale row
number and toggling straight back off.

**`focalPlanets` is deliberately left empty in both states**, and cleared
explicitly on entry so a preceding non-PD click's focal set cannot survive
into the PD view. Constraining it to the promissor/significator pair was
built and then removed: a non-empty focal set puts
`calculateSynastryAspects()` on its "exactly this" direct path, which
suppressed every *other* directed-to-natal mundane interaspect — including
those involving neither of the event's bodies. Those near-hits clustered
around the event turn out to be much of the value of the directed bi-wheel,
so the full-chart path is the right default.

What focalize/unfocalize governs is therefore the Directions-table preview
and the chart label only. That is still the distinction that makes a PD click
clearable, which is the whole reason `_pdFocalRow` exists.

Note this gap is not PD-specific — clicking a return or paran row likewise
mutates `file()` in place with no way back. PD merely made it conspicuous by
swapping the chart *type*. A general pre-click snapshot was considered and
deliberately not built.

## Directing the sky position too — built

The original design left a directed chart's `eclipticPos`/`equatorialPos` at
their natal values, reasoning that a primary direction rotates the *frame*
and moves nothing in the sky. True, but the consequence was that in ecliptic
or equatorial display the directed ring rendered as a **pixel-exact copy of
the natal ring**. This doc recorded that as "harmless, expected". It was not.

It cost a long debugging session, because the failure mode is actively
misleading rather than merely useless: the house cusps *do* move in those
modes (`calculateHousesFromRAMC()` rebuilds them from the shifted RAMC, and
the `amcEcliptic` cusp branch has no non-reference-file suppression), so the
wheel showed **rotating angles around static planets**. That looks exactly
like a half-broken feature, and it was misread as one — including a fruitless
hunt through the whole calc path before anyone thought to ask which display
mode was active. A probe printing `arc`/`RAMC`/`natRAMC`/`pvPos`/`natPv` into
the planet tooltip settled it in one hover: `dRAMC == arc` and `dPv` non-zero,
i.e. the mundane math had been right the entire time.

### The fix: commit to one framing

A primary direction has two exactly equivalent framings, because mundane
position depends only on `RA − RAMC` and declination:

1. **Frame rotates:** `RAMC += arc`, bodies hold still.
2. **Bodies rotate:** `RA −= arc`, declination untouched, frame holds still.

The first attempt mixed them: houses stayed frame-rotated (framing 1) while
the new `directBodyForDisplay()` moved the bodies (framing 2). Those are
equivalent only when applied *consistently* — mixed, they are additive. In
ecliptic and equatorial display, which draw absolute longitudes, the cusps
swept **forward** while the planets swept **backward**, at double the true
separation. That was the reported symptom: "the outer file's angles are being
directed in a converse direction to the outer file's planets."

`calculateAll()`'s isPD branch now uses **framing 2 throughout** — the same
framing `mundanePositionAtArc()` already uses for the direction solvers:

- `scope.houses` is the **natal** frame: no RAMC shift, no
  `calculateHousesFromRAMC()` call.
- `directPoint()` carries any fixed natal `(RA, Dec)` through the direction
  and reports where it lands in all three frames at once (mundane, ecliptic,
  equatorial). `directBodyForDisplay()` applies it to every planet and star;
  the same helper fills `anglePv`/`angleEcl`/`angleEqu` for the four angles.
- `pvPos` values are **unchanged** from before, by the framing equivalence, so
  mundane display is bit-identical to the version already validated.

Consequence worth internalising: **a directed chart's house grid is the natal
one.** What moves under direction is each natal angle *treated as a body*,
which is why the angles live in `angleEcl`/`angleEqu`/`anglePv` and not in
`houses.cusp[]`. `Chart::updatePlanetsAndCusps()` has a single
`TypeDerivedPD` block ahead of the frame switch that hides all twelve cusps
and draws those four in whichever frame is active — so they sweep *with* the
directed planets instead of against them.

### The South Node trap

`directBodyForDisplay()` first read `tropicalEclipticPos`, which drew the
South Node conjunct the North. SWE reports the **North** Node's coordinates
for both nodes; `calculatePlanet()` mirrors them into `equatorialPos`
(`astro-calc.cpp:1668-1675`) and, separately, adds 180° to `pvPos`
(`:1645`) — but never touches `tropicalEclipticPos`, which keeps the North
Node's longitude. Anything deriving a South Node position from that field
gets the wrong body. The helper now starts from `equatorialPos`, which is
already correct for both nodes and skips a redundant round trip through the
ecliptic.

### Three deliberate restraints

- **`tropicalEclipticPos` stays natal.** `relocalizedPvPos()` and
  `relocalizedMundanePos()` recompute a mundane position from it against a
  supplied RAMC, and that RAMC is now the natal one, so a directed value
  there would double-count the arc. `pvPos` is already final.
- **`sign`/`house`/`position` are re-derived** after directing, against the
  natal cusps. `calculatePlanet()` computed them from the body's natal
  longitude, which has just moved; leaving them would show a directed degree
  with a natal sign and house.
- **`drawRaptParallelFigure()` still bails outside mundane display**, but its
  comment was corrected: the old justification (the rings overlay exactly) is
  now false, while the real reason is more fundamental — a rapt parallel
  asserts a *mundane* midpoint relation, and drawing it on an ecliptic wheel
  would assert an ecliptic one that was never checked.

### The limit worth knowing

The defining coincidence of a primary direction is **mundane**. At the solved
arc the promissor's mundane position equals the significator's natal one, but
their RAs and ecliptic longitudes do not coincide — the sole exception being
an MC significator, where mundane conjunction reduces to RA equality. So
ecliptic and equatorial display show a faithful rotated sphere in which the
event's conjunction is *not visible as a conjunction*. That is a property of
the technique, not of the drawing, and mundane display remains the only mode
in which a direction can be read off the wheel directly.

Relatedly, and a persistent source of surprise: **the directed ring does not
rotate rigidly in mundane display.** Each body advances by its own fraction
of the arc, set by its semi-arc (under Placidus, `d(MP)/d(arc) ∝ 1/SA`), so
the ring visibly deforms as it turns. A worked example from the debugging
session: an 11.07° arc moved a Sun at dec +20.2° by only 8.69° of mundane
position, implying SDA ≈ 114.7° and a birth latitude near 48.5°N — correct
physics, since a high-declination Sun at northern latitude has a long diurnal
arc and so crosses the mundane frame more slowly than the RAMC advances. This
is the same asymmetry that makes rapt conjunctions possible at all.

## The promissor/significator switch matrix — built

`findPrimaryDirections()` enumerates every ordered pair from one body list
(planets *and* the four angles), so each of the four combinations needs its
own decision. Two settings cover them, each gating the *optional* half of its
role and leaving the classical half always on:

| | planet promissor | angle promissor |
|---|---|---|
| **angle significator** | always — the classical baseline | `pdAnglesAsPromissors` |
| **planet significator** | `pdPlanetsAsSignificators` | both switches |

`pdPlanetsAsSignificators` (added 2026-09-12) is the mirror of the older
`pdAnglesAsPromissors`, one line at the head of the significator loop:

```cpp
if (sig.angleKind == DirNotAngle && !pdPlanetsAsSignificators) continue;
```

One gate covers all four significator branches (C/R, angle, plain
conjunction, Placidus zodiacal/mundane ray) because they all run inside that
loop. **Both now default off**, which makes the out-of-the-box set exactly
the classical baseline: a planet directed to an angle, and nothing else. The
`pdAnglesAsPromissors` default changed from on in the same edit; because
every settings save writes the full key set, an existing `settings.ini`
carries `Events/pdAnglesAsPromissors=true` and keeps it — only fresh installs
see the new default.

Two consequences worth not mistaking for bugs:

- Turning planet significators off also removes the **own-ray**
  self-direction case (a planet directed to its own sextile, etc.). That case
  is exempted from the self-direction skip a few lines below, but its
  significator is a planet by construction, so it lives or dies with this
  flag.
- The flag deliberately does **not** govern the rapt parallel's significator
  Z. `raptParallelsAnyZ` already restricts Z to the angles by default and
  stays independent, so a planet Z is available without opening up ordinary
  planet-to-planet directions. Rapt conjunctions have no significator at all
  (both bodies are directed), so neither switch touches them.

### Settings-dialog housekeeping done alongside

The Events tab's PD rows had grown to eight labels, each repeating a
`"Primary Directions: "` prefix and carrying a two- or three-line
parenthetical. They are now a `beginGroup(tr("Primary Directions"))` box —
the pattern `Plain::setupSettingsEditor()` already used (`plain.cpp:2212`) —
with terse labels and the prose moved to tooltips, ordered scope → orb →
inclusions → right-ascension display. Patterns options got the same treatment
in their own box; `planetPairOrb` was moved *out* of that group, since it is
the general in-orb threshold for aspect event search and also scales the
search step rate, not a patterns-only setting.

Grouping alone still left the tab tall, so the six PD inclusion switches now
share **one** wrapped form row via the existing `addCheckBoxRow(label,
boxes, perRow)` (three across, reading order across then down), and the three
body/transit inclusions among the floating rows do the same. That takes the
tab from twenty form rows to eleven. A checkbox added this way has no form
label of its own — the box's own text is the label — so a control-only
tooltip still covers the words being hovered, which is why `bindRow()`
storing no `boundLabels` entry for those is correct rather than a gap.

Settings rows had no tooltip support at all before this. `AppSettingsEditor`
gained `setRowToolTip(key, tip)`, which sets the hint on the control **and**
on the `QLabel` that `QFormLayout::addRow(QString, QWidget*)` builds
internally — the latter recovered via `labelForField()` at add time (the only
moment the owning form layout is still `currentForm`) and cached in
`boundLabels`. The control alone is not enough: the label text is the larger
hover target, and for a checkbox the control is just the box. The method is
deliberately *not* named `setToolTip`, which would hide
`QWidget::setToolTip()` on an `AppSettingsEditor`.
