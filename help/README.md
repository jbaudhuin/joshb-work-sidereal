# Zodiac help site (source)

This folder is the **source** of Zodiac's user help, built with
[MkDocs](https://www.mkdocs.org/) and the
[Material](https://squidfunk.github.io/mkdocs-material/) theme. The same build serves two
purposes: it is bundled with the app (opened by F1 / Help, works offline) and published
online via GitHub Pages.

```
help/
  mkdocs.yml          site config + navigation
  requirements.txt    pinned MkDocs/Material versions
  docs/               the pages (Markdown)
  docs/assets/img/    screenshots (real captures; uncaptured ones are "SCREENSHOT PENDING" placeholders)
  shots.json          screenshot manifest — one entry per image the pages use
```

## Building

```powershell
pip install -r help/requirements.txt
mkdocs build -f help/mkdocs.yml      # writes the site to bin/help/
mkdocs serve -f help/mkdocs.yml      # live preview at http://127.0.0.1:8000
```

The built site goes to **`bin/help/`**, mirroring the installed layout (`$INSTDIR\help`),
so the dev build in `bin\` and an installed copy find the help at the same relative path:
`QCoreApplication::applicationDirPath() + "/help/<page>.html"`. `bin/help/` is generated
output and should not be committed.

URLs are flat (`events/patterns.html`, not `events/patterns/`) so the site works when
opened straight from disk. For the same reason Material's "instant navigation" is off
(it uses background requests that `file://` pages can't make).

## Online copy

`.github/workflows/deploy-help.yml` rebuilds and publishes to the `gh-pages` branch on
every push that touches `help/`. One-time setup on GitHub: **Settings → Pages → Build and
deployment → Deploy from a branch → `gh-pages` / root**. The site appears at
<https://jbaudhuin.github.io/joshb-work-sidereal/>.

## Screenshots

Every image a page shows is already wired in under its final filename in
`docs/assets/img/`. Replacing a placeholder PNG with a real capture of the same name is
all it takes; no page edits needed. `shots.json` lists each one and drives the app's
doc-capture mode (a developer build option; see `docs/doc-capture.md` in the repo root):

- `file` — the exact name the pages use (`docs/assets/img/<file>`).
- `page`, `caption`, `notes` — where it appears and what it must show.
- `mode` — `auto` (scriptable) or `manual` (captured by hand, e.g. the installer).
- `target` — the widget to grab: `window`, `dialog`, an objectName, or a widget path.
  The interactive manifest pass (`--doc-capture-manifest=help/shots.json`) fills it in
  as you capture each entry.
- `setup` — ordered steps that put the app in the right state, e.g.
  `{"loadChart": "Sample Charts/Olof Palme"}`, `{"triggerAction": "Settings"}`,
  `{"setTab": {"widget": "settingsTabs", "tab": "Chart"}}`,
  `{"setEventRange": {"from": "1985-01-01", "to": "1986-02-28"}}`,
  `{"waitForFinders": true}`.

Once entries have a target, `--doc-capture-script=help/shots.json` re-shoots them all
unattended.

Capture guidance:

- Use the neutral sample chart (Olof Palme) and a **clean data folder** with only the
  sample charts. Wide shots show the chart database and session list, so real names
  must not appear.
- Shoot text-heavy panels and dialogs **close up** (the panel fills the frame); chart
  wheels can be wide. Render at 2× for crisp text.
- Dark theme throughout, except the light half of the Themes pair.
- The captions were written from the README/CHANGELOG before the UI was photographed,
  so check each one against the real screen and correct it if needed.

## Writing guidelines

- **Accuracy over completeness.** Describe only what the app actually does; don't
  invent labels, values, or shortcuts.
- **Copyright.** `easy-directions.md` (Deborah Houlding) and `PSSR.md` (Kenneth Bowser)
  in the repo are third-party copyrighted texts. Don't reproduce them; explain the
  techniques in original words and name them if useful.
- **No personal data** in text or screenshots.
- Pages start with a single `# Title` matching the nav; cross-link with relative `.md`
  links; keys as `++f1++`, `++ctrl+c++`.
