# Planets Panel

The **Planets** panel is a dockable table listing where every body stands: the
Ascendant and Midheaven first, then the Sun through Pluto, the nodes and any
other bodies the chart carries. Open it from the panels menu like the Details,
Events and Speculum panels.

When a second chart is loaded (a transit, progression, return…), its positions
appear in a **Chart 2** column beside the first, so the two can be read across
row by row. Positions are ecliptic longitudes in the zodiac the tab is
displaying, written with the sign's glyph; an **R** marks a retrograde body.

Further columns give chart 1's **declination**, **right ascension**, **mundane**
position (round the circle of the current direction system, 0° at the
Ascendant) and **house**. Right-click the column headings to choose which of
them to show.

Click a row to select that planet in the other panels. Select cells and press
++ctrl+c++ to copy them (positions are copied with the sign's abbreviation).

## Finding a chart from its positions

Press **Search** to turn the table into a search form. This is meant for
recovering a chart from positions alone — a horoscope printed in an old book,
or a modern chart whose date you want to establish.

1. Tick the bodies whose positions you know. A ticked row is part of the
   search, and its position becomes editable: type over it with any of
   `15 Leo`, `15°23' Leo`, `15Le23`, `Leo 15 23`, or an absolute longitude such
   as `135.4`. What you type is rewritten the way the table writes positions
   (`9 Can 51` becomes `9°51' Can`), to the precision you gave and no further.
   ++tab++ and ++enter++ move on to the next ticked row. Unticked rows are
   ignored and simply go on showing the open chart.

    When only the sign is known, type the sign by itself (`Leo`). The body
    may then stand anywhere in that sign, and the Orb is how far past the
    sign's edges still counts.
2. Choose the **Zodiac** the positions are given in. It starts out matching the
   tab but is independent of it: you can search for a tropical chart while the
   wheel shows a sidereal one. While searching, the whole table is shown in
   this zodiac.
3. Set the **Orb** — how far a body may stand from what you typed. Printed
   charts given to the whole degree want at least 1°.
4. Set the range of **Years** to look through. Years use astronomical
   numbering (0 is 1 BC).
5. Press **Find**.

Matches are listed best fit first. Select one — by clicking, or with the arrow
keys — to show it. Where it goes depends on what is open:

- beside chart 1, as chart 2, when chart 1 is a person, event, return or
  composite chart;
- in place of chart 1 when it is any other kind of chart (a new untitled
  chart, say) — the positions the search took from it are kept as if typed;
- in a tab of its own when no chart is open.

Stepping through the list re-points that same chart, and the search is left as
it was. Double-click or middle-click a match to open it in a new tab, as that
tab's chart 1, to keep.

A row you tick but do not type over stands for the open chart's own position.
The tick box at the top of the column ticks or unticks every row at once, so
ticking it and pressing **Find** looks for the chart you already have open —
the quickest way to try the search out. Unticking a row puts the chart's value
back.

!!! tip "When nothing matches"
    Tables before the modern era were often degrees off for Mercury and Mars.
    Raise **Misses** to let one or two bodies fall outside the orb, or widen
    the orb.

### What the angles add

Planets alone fix the date; the Moon fixes the time to within a few hours.
The Ascendant and Midheaven do more, in one of two ways:

- **Without a known place**, the Midheaven gives the chart's local mean time
  directly, and Ascendant and Midheaven together give the **latitude**. The
  **longitude** is the difference between that local time and Universal Time,
  and UT comes from the bodies — in practice the Moon. The Earth turns 27 times
  faster than the Moon moves, so any vagueness in the Moon's position is
  magnified 27-fold in the longitude: a Moon given to the whole degree leaves
  it uncertain by about ±14°, one given to the minute by about ±0.5°. The
  uncertainty shown beside each value follows from how precisely you stated
  the positions (and widens if they disagree with one another by more than
  that); the Orb only decides whether a chart counts as a match.

  A place is suggested from the result: the nearest city when the position is
  closely fixed, otherwise the largest one within the uncertainty. The notes of
  the opened chart say how far the solved position lies from it. For dates from
  1900 the **Zone** column gives the clock time that place kept, and the chart
  opens in that zone time rather than local mean time — check any
  daylight-saving time against a historical source.
- **With "Known place" ticked**, the angles are used the other way round: they
  pin the time of day to within minutes, and the Moon becomes a check on it.

The Ascendant by itself needs either a Midheaven or a known place.

### Charts given by sign

Old horoscopes often give no more than the sign each body stood in and the
sign that was rising. Type the signs alone, tick **Known place** and enter the
place the chart is thought to come from — it cannot be worked out from a
rising sign.

Seven bodies by sign normally single out one stretch of a day or two in many
centuries. The rising sign then narrows each day of that stretch to the two
hours or so during which it was on the Ascendant, so there is one match per
day. The **UT** column gives the middle of the span and its half-length
(`14:58 ±1h`); nothing in the data says where within it the chart falls. If
the Moon or another body is known to the degree, the days reduce to one.

!!! note
    For the chart's data as text tables you can copy — with rulerships, aspects
    and more — see the [Tables (Text) view](tables.md). For the properties of a
    single planet, see [Details](details.md).
