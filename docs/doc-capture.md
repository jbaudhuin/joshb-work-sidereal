# Doc capture mode (developer tool)

Saves any widget — button, panel, dialog tab, chart wheel, open dropdown — as a
high-DPI PNG for the help site, using `QWidget::render()` instead of an OS
screenshot. Text comes out crisp, and overlapping windows never end up in the shot.

## Build and run

It is compiled in only on request, and stays dormant unless the app is started
with the flag.

**VS Code (CMake Tools):** click the build-variant item in the status bar (or
run *CMake: Select Variant*) and choose **Release + DocCapture**. Then launch
**Zodiac Doc Capture (CMake)** from the Run and Debug panel. Switch the variant
back to plain **Release** before building an installer.

**Command line:**

```bash
cmake -S . -B build-doccapture -G Ninja -DCMAKE_BUILD_TYPE=Release -DZODIAC_DOC_CAPTURE=ON
cmake --build build-doccapture
bin/zodiac.exe --doc-capture
```

qmake: add `CONFIG+=doc_capture`. Both build trees write `bin/zodiac.exe`, so
rebuild from `build/` (option OFF) before packaging a release.

| Option | Env var | Default |
|---|---|---|
| `--doc-capture-dir=<path>` | `ZODIAC_CAPTURE_DIR` | `Pictures/Zodiac captures` |
| `--doc-capture-dpr=<n>` | `ZODIAC_CAPTURE_DPR` | `2` |
| `--doc-capture-delay=<ms>` | — | `3000` |

Any `--doc-capture-*` option turns the mode on as well. While it is on, Qt
file dialogs replace native ones so they can be captured too.

## Keys

| Key | Action |
|---|---|
| Ctrl+Shift+F12 | Toggle capture mode |
| Mouse | Hover a widget to select it (clicks still work normally) |
| Wheel / PgUp / PgDn | Select the parent / child (Shift+wheel scrolls normally) |
| F11 | Freeze or unfreeze the selection |
| F12 | Save the selection |
| Shift+F12 | Save after a countdown (for hover states and tooltips; a visible tooltip is saved too, as `<name>-tooltip`) |
| Alt+F12 | Save with open popups composited on top (e.g. a combo with its list open) |
| Esc | Close an open popup, cancel a countdown, or leave capture mode |

## Output

Files are named from `objectName` (or the nearest named ancestor plus the
button/tab/dock text) plus a counter: `aspectsSelector-popup-001.png`.
Each save appends a line to `captures.jsonl` in the same folder, with file,
objectName, class, objectName path (`id`), title, screen geometry, pixel size,
dpr, capture mode and `helpPage` (the F1 topic of the widget, from
`HelpTopics`).

## Data folder

`--data-dir=<folder>` (or `ZODIAC_DATA_DIR`) replaces `Documents/zodiac-charts`
for the whole run: user charts, sessions, `settings.ini`, `APIKey.ini` and the
theme INI all live there. Point it at an empty folder for captures, so only the
sample charts appear in the database panel and no API key shows in Settings.
It works in every build, not just doc-capture ones, and always allows a second
instance next to your normal one.

## Manifest pass (interactive)

`help/shots.json` lists every image the help pages use. Start with

```bash
cd bin
zodiac.exe --new --data-dir=%TEMP%/zodiac-clean --doc-capture-manifest=../help/shots.json
```

Capture mode turns on by itself and the outline label shows the current entry
(`[3/49] file — caption`).

| Key | Action |
|---|---|
| Ctrl+PgDn / Ctrl+PgUp | Next / previous entry |
| F10 | Run the entry's `setup` steps |
| F12 / Shift+F12 / Alt+F12 | Save the selection as the entry's `file` in `help/docs/assets/img/` (overwriting the placeholder) and write the widget's path back into the entry's `target` (`"popups": true` for Alt) |

`--doc-capture-img-dir=<path>` sends the images elsewhere instead.

## Scripted run (batch)

```bash
cd bin
zodiac.exe --new --data-dir=%TEMP%/zodiac-clean --doc-capture-script=../help/shots.json
```

The window is sized and themed from `defaults`, the default chart is loaded,
`defaults.setup` runs (session-wide state such as the aspect set), then each `auto` entry with a `target` is run: its `setup` steps, a short
settle, the grab, then any dialog or popup left open is closed. Entries with
`mode: manual` or no `target` are skipped. A line per entry goes to stderr and
the whole result to `script-report.json` in the capture folder; the exit code
is 1 if any entry failed. It refuses to run without `--data-dir`, or from any folder but `bin/`: toolbar icons
and the theme's images (drop-down arrows) load relative to the working directory.
`--doc-capture-only=<regex>` limits the run to matching file names.

### Entry fields

| Field | Meaning |
|---|---|
| `file`, `page`, `caption`, `notes` | The image, the page that shows it, and what it must show |
| `mode` | `auto` or `manual` (outside the app, e.g. the installer) |
| `target` | `window` (main window), `dialog` (the open modal dialog), an objectName, or a path recorded by the manifest pass |
| `popups` | Composite open popups (menus, dropdowns) over the target |
| `theme`, `windowSize`, `settle` | Per-shot overrides of `defaults` (`settle`: ms before the grab, default 700) |
| `setup` | Steps, each `{ "action": argument }`, run in order |
| `teardown` | Steps run after the grab, to undo state later shots shouldn't inherit (e.g. `{"setHarmonic": 1}`) |

### Setup steps

Widgets are named by objectName or recorded path; buttons, tabs, combo items
and actions by their visible text (mnemonics and a trailing `…` ignored).

| Step | Argument |
|---|---|
| `loadChart` | `"Sample Charts/Olof Palme"`, or `{ "chart": …, "newTab": true }` / `{ "chart": …, "second": true }` |
| `triggerAction` | QAction objectName or text (`"Settings"`). Modal dialogs are fine: the script continues inside them |
| `click` | Button objectName, or `{ "widget": parent, "text": label, "checked": bool }` |
| `setTab` | `{ "widget": tabWidgetOrBar, "tab": text }` (widget optional) |
| `setCombo` | `{ "widget": combo, "text": item }` or `"index"` |
| `setHarmonic` | number, via `harmonicSelector` |
| `setText` | `{ "widget": lineEditOrCombo, "text": …, "submit": true }` |
| `setEventRange` | `{ "from": "1985-01-01", "to": "1986-02-28" }` |
| `selectRow` | `{ "widget": view, "row": n }` or `"text"`, optional `"column"`, `"double": true` |
| `contextMenu` | `{ "widget": view, "row"/"text": … }`; the menu stays open for the grab (use `"popups": true`) |
| `openPopup` | combo box or menu button |
| `hover` | widget (moves the real cursor) |
| `showDock` | dock objectName |
| `setTheme` | `dark`, `light` or `printable` |
| `resizeWindow` | `[width, height]` |
| `wait` | ms |
| `waitForFinders` | `true` or a timeout in ms: waits until no event search is running |
| `waitForWidget` | widget, until it is visible |
| `closeWindow` | a separate window such as the chart editor (never the main window) |
| `closePopups`, `closeDialogs` | — |

## Limitations

- The Planets slide is a native QML window, so `render()` can't draw it. It is
  grabbed from the scene graph at the screen's own DPR and scaled up.
- On Linux Wayland the outline windows can't be positioned; run with
  `QT_QPA_PLATFORM=xcb`.
- F1 help (`zodiac/src/helptopics.h`) maps objectNames to pages the same way;
  a future callout mode can reuse `WidgetPicker` too.
