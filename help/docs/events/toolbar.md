# The Event-Filter Toolbar

Every [Events panel](index.md) carries a small toolbar across the top that decides
*which kinds of events* the finder looks for. Rather than casting new charts or digging
through a settings dialog, you flip the event types you care about on and off right here,
and the list below refreshes to match. Everything on this toolbar is remembered **per
tab** and saved with the session, so two charts open side by side can be hunting for
completely different things.

<figure markdown="span">
  ![the event-filter toolbar at the top of the Events panel: the S (stations) and R (returns) buttons, then the T, P, AP, PD, Par and Hel groups, whose dropdowns hold the individual event types (T=T, T=N, P=P and so on), with the skip-by-duration and refresh buttons at the left](../assets/img/events-toolbar-1.png){ loading=lazy }
  <figcaption>the event-filter toolbar at the top of the Events panel: the S (stations) and R (returns) buttons, then the T, P, AP, PD, Par and Hel groups, whose dropdowns hold the individual event types (T=T, T=N, P=P and so on), with the skip-by-duration and refresh buttons at the left</figcaption>
</figure>

## The three core toggles

The heart of the toolbar is a row of type toggles:

- **T=T** — *transiting to transiting*: aspects between the moving planets themselves.
- **T=N** — *transiting to natal*: the moving planets aspecting the birth chart.
- **P=P** — *progressed to progressed*: secondary progressions to progressions (and to
    natal).

Turn on as many as you like. With all three lit, the list mixes every kind of contact in
range; narrow it to just T=N, say, when you only want to watch transits landing on the
natal chart.

## Sub-mode groups

Alongside the core toggles are **split-button groups** that gather related event types
under a single control with a dropdown. As of v0.9.9 these grouped controls replace the
older row of individual filter actions, keeping the toolbar compact. They include:

- **Ingresses** — a planet entering a new sign (or house). See
    [Stations, ingresses &amp; returns](stations-ingresses.md).
- **Paranatellonta (parans)** — planet-to-planet and planet-to-star angular
    coincidences. See [Parans &amp; Primary Directions](../parans/index.md).
- **Heliacal** — a toggle that adds heliacal apparitions to the search. See
    [Heliacal apparitions](heliacal.md).

Stations and returns are likewise available as filters; open a group's dropdown to pick
exactly which sub-modes it contributes.

## Trimming the results

Two more per-tab controls help keep the list readable:

- **Skip events shorter than a duration.** A brief, fleeting contact can be filtered out
    by setting a minimum duration, so the list shows only events that stay in orb long
    enough to matter to you.
- **Auto-refresh.** Each tab can recompute its events on demand — or automatically — when
    you change the range or the filters, so the list never falls out of step with the
    toolbar above it.

!!! tip
    Because these settings live with the tab and travel in your session, a tab you have
    tuned for, say, outer-planet transits to natal will come back exactly that way the
    next time you open your work.

---

**Next:** [The pattern search language](patterns.md) &nbsp;·&nbsp;
[Background finders &amp; performance](performance.md)
