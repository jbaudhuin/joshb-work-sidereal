# Background Finders &amp; Performance

The event engine is built to stay quick even when you ask a lot of it, and to keep
working without freezing the rest of the program. This page covers how fast the common
searches are, what happens during a long one, and the settings that govern background
work.

## How fast it is

The [Events panel](index.md) is designed for speed. On an optimized build, a full **year**
of the common event types — transits-to-transits, transits-to-natal, returns-to-natal,
and stations — takes about a **second**. Sweeping **every harmonic from H1 through H32**
over the same range takes roughly **four seconds**. Most of the time you will not notice
the search running at all; a few kinds of search, such as aspect-pattern matching, are
naturally heavier because the engine has to find each configuration's in-orb window and
its moment of closest orb.

## Searches that run in the background

Long [pattern searches](patterns.md) do not have to hold you at one tab. With the optional
**`backgroundFinders`** setting enabled, a search can **continue running when you switch
to another tab**, so you can keep working while the engine grinds through a wide range or
a demanding pattern. Each finder tracks its own state and can be paused, resumed, or
canceled per tab.

When you come back, **paused or backgrounded finders reconnect and repopulate** their
results — the list you left behind fills in as the finder catches up, rather than
starting over. A long-running search also shows a **progress cue** in the search field so
you can see it is still working.

!!! note
    `backgroundFinders` is a persisted preference. Leave it off and searches run in the
    foreground for the active tab; turn it on when you routinely launch long searches and
    want to keep moving between tabs.

## Caching

Zodiac **caches** results and validates the cache carefully, so switching tabs or
re-opening the same chart does not trigger a needless recomputation. View-only changes —
harmonic, house system, zodiac, aspect set — are treated as display settings and do not
by themselves force the events to be found again. The upshot is that the program does the
expensive work once and reuses it whenever the inputs have not actually changed.

---

**Back to:** [Events (Transits)](index.md) &nbsp;·&nbsp;
[The pattern search language](patterns.md)
