# Heliacal rising/setting stars (dawn method)

Chart setting: **Show heliacal rising/setting stars (mundane, dawn method)**, with
**Heliacal star candidates** choosing the candidate pool.

## What it shows

In mundane (PV) display mode, on the anchor ring, two fixed stars are tinted and
enlarged:

- **Heliacal rising star**: of the candidate stars, the one that most recently
  rose *together with the Sun* before the chart's date. Equivalently, it is the
  last candidate to rise in the east before sunrise on the morning of the
  chart's local date.
- **Heliacal setting star**: the same for setting in the west.

The tooltip reports how many days before the chart's date the star rose (set)
with the Sun. This is found by stepping back one morning at a time and
interpolating the day on which the star's rising (setting) coincided with
sunrise.

There is deliberately no twilight-visibility (arcus visionis) test. Those
events are the *visible* first/last appearances, which the event finder reports
separately as MF/EL heliacal events. This overlay is about which star most
recently came up (went down) with the Sun.

## Candidate pools

| Setting | Candidates |
|---|---|
| Curated stars (default) | the stars listed in `bin/astroprocessor/curated_stars.csv` |
| Curated + brighter than mag 2.5 | the above plus any catalogue star brighter than magnitude 2.5 |
| Whole catalogue | every loaded star (curated, magnitude ≤ 2.2, or zodiacal) |

`curated_stars.csv` is a plain list of Swiss Ephemeris star names, one per line
(`#` lines are comments). Every listed star is also always loaded into the star
catalogue, whatever its magnitude. Edit it to your own selection; the change
takes effect on the next launch, and a name that is not in `bin/swe/sefstars.txt`
is logged at startup.

The candidate pool decides the answer. Because every star rises about four
minutes earlier each day relative to the Sun, the whole catalogue nearly always
yields some faint star that rose with the Sun only a day or two ago. A curated
list or a brightness cutoff keeps the result to stars of interest.

## Accuracy notes

Checked against two published reference charts:

- **Risings** agree with them to the day.
- **Settings** came out 1.5 to 3 days *fewer* than the reference in both
  cases. Since all candidates shift together, this does not change which star
  is chosen; it is probably a difference in how "sets with the Sun" is timed
  (sunrise definition or refraction).

## Attribution

The method follows the approach described by Bernadette Brady in *Brady's Book
of Fixed Stars* (Weiser, 1998). Zodiac Sidereal is an independent
implementation and is not affiliated with or endorsed by the author. The
curated star list is the app's own selection, not a reproduction of any
published list.
