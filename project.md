# Prowal

## Project Specification and Development Roadmap

**Status:** v1.0.1 (release) built — see `ARCHITECTURE.md` for the as-built record.
**Platform:** Linux, X11 (xcb), compositing window manager preferred (xfwm4 on the dev machine).
**Stack:** C++20, Qt 6 Widgets, CMake. No KDE Frameworks.

---

## 1. Summary

Prowal is a pop-up application launcher. Run it (typically bound to a mouse button on the
desktop or a hotkey) and a grid of icons appears **around the mouse pointer**, with the grid's
origin cell directly under the cursor. Every item has a fixed position relative to that origin,
so the launcher is used by muscle memory: Firefox is always "one up", the terminal is always
"two right, two down". Click an icon to launch it; move away and the grid fades out.

Prowal is a clean-room successor to *apwal* (Pascal Eberhard, 2002–2004, GPL-2), which the author
has used daily for ~20 years. It keeps apwal's core idea and fixes what aged badly: slow editor
start-up, fixed 48 px icons with no scaling, raw-path-only icons, crash-on-bad-config, unsafe
saves, X11 single-screen assumptions, and the GTK2 dependency.

### 1.1 Clean-room rule

No apwal source code, images or text is copied, translated or adapted. apwal is referenced only
for (a) user-visible behaviour, described in this document in our own words, and (b) its
`~/.apwalrc.xml` file format, which Prowal reads in order to import an existing layout.

---

## 2. Principles

1. **Instant.** The popup must be on screen and clickable fast enough that it feels like part of
   the desktop. Budget: **≤ 150 ms** from process start to first frame on the dev machine,
   measured, not guessed (§9.3). Animation never delays input.
2. **Muscle memory is sacred.** Positions are cursor-relative grid cells. Nothing Prowal does on
   its own (sorting, auto-layout, collision handling) may move an item the user placed.
3. **User data is sacred.** Config writes are atomic with rotating backups. Prowal never writes to
   `~/.apwalrc.xml`. A malformed config never crashes the program — bad entries are reported and
   skipped, and the file is not overwritten until the user saves.
4. **Polished, not busy.** Motion is short and purposeful, can be reduced or switched off, and
   degrades cleanly when no compositor is running.
5. **The editor is a tool you visit occasionally.** It must open instantly and make the common
   task — "add this program" — take seconds.

---

## 3. Vocabulary

- **Grid:** the set of items shown together in one popup. A config holds one or more *named*
  Grids (§4.5); the default is `main`.
- **Cell:** an integer `(x, y)` position. `(0, 0)` is the cell under the pointer; negative values are
  up/left.
- **Item:** something in a cell — a **Launcher** (runs a command) or a **Group** (opens a child Grid,
  i.e. a submenu).
- **Popup:** the on-screen window showing a Grid.
- **Editor:** the window for arranging and configuring items.

---

## 4. Popup behaviour

### 4.1 Positioning
- The origin cell is centred on the pointer.
- The Grid is clamped to the **available geometry of the monitor the pointer is on** (not the whole
  X screen). If clamping shifts the Grid, the items shift together — the layout never reflows.
- The bounding box covers only cells from min to max x/y actually used; empty cells inside it are
  transparent.

### 4.2 Input
- **Left click** on an item: launch it (or open the Group).
- **Middle click:** launch and keep the popup open (for launching several things).
- **Right click on an item:** open the Editor with that item selected. **Right click on empty
  space:** open the Editor.
- **Esc:** close (or, inside a Group, return to the parent Grid). **Backspace** also goes up a level.
- **Click outside the Grid:** close.
- **Keyboard (v0.7):** arrow keys move a highlight starting at the origin cell; Enter launches.
- **Click in a gap between icons:** close. *(Changed from the original plan, where gaps were
  click-through. The popup grabs the pointer — needed for Esc and click-outside-to-close — and
  that also stops a stray click from reaching the desktop underneath. See ARCHITECTURE.md §3.)*

### 4.3 Closing
- After the pointer leaves the Grid, close after `timeout` ms (default 2000; imported from apwal).
  Re-entering cancels the countdown.
- After a launch, close unless the user middle-clicked or `stayOpenAfterLaunch` is set.
- Launch failure: the item shakes briefly and an inline message appears under it; the popup stays
  open until dismissed. No modal dialog.

### 4.4 Groups (submenus)
- A Group item opens its child Grid **in place**, centred on the Group's cell, with a short
  cross-fade. The parent Grid is not shown behind it.
- Groups nest. The Esc/Backspace chain walks back up.
- Replaces apwal's workaround of a launcher that re-runs itself with another config file.

### 4.5 Named Grids (`--grid NAME`)
- A config can hold several independent top-level Grids, each with its own name and layout:
  e.g. `main`, `games`, `work`, `media`.
- `prowal` shows `main`; `prowal --grid games` shows `games` instead. Bind each to a different key
  or mouse button in the window manager and you get several launchers from one config and one
  Editor — a different set of icons under each trigger, all positioned around the pointer.
- **Groups vs Grids:** a Group is a submenu *inside* a Grid (you click into it); a Grid is a
  separate *entry point* (you choose it by which key you press). A Group can't be shown directly,
  a Grid can.
- The Editor (0.3) gets a Grid switcher (add / rename / delete / duplicate). `--import-apwal`
  imports into `main` unless `--grid` names another, so several apwal configs (e.g. old
  `apwal --config` submenus) can each become their own Grid.

---

## 5. Look and motion

### 5.1 Appearance modes (configurable)
- **Floating** (default): no background. Each icon has a soft drop shadow.
- **Panel:** a rounded, translucent tinted panel behind the Grid with a soft outer shadow.
  Colour follows the Qt palette unless overridden. *Note: xfwm4 offers no background blur, so
  "frosted" means a translucent tint with optional subtle noise, not true blur.*
- **No compositor:** both modes fall back to a hard-edged shaped window (icons only, alpha
  threshold), no shadows, no animation. Detected at start-up.

### 5.2 Sizing
- Icon size configurable (24–128 px, default 48), rendered at the screen's device pixel ratio.
  Icons are scaled to fit and centred in their cell — never cropped.
- Cell spacing configurable (default 8 px).
- Labels: `hover` (default) / `always` / `never`.
- Group icons (`groupStyle`): `plain`, `badge` (default: the Group's own icon with a small folder
  badge) or `preview` (a 2×2 mini-grid of the first four child icons). Preview has no runtime cost
  worth worrying about: the child icons are already in the rendered-icon cache (§9.3), the
  composite is built once and cached too, and it is only rebuilt when the Group's contents change.
  Still a preference, because some people find the mini icons too busy at small sizes.

### 5.3 Motion (defaults; all durations configurable via one "speed" factor)
| Moment | Effect | Timing |
|---|---|---|
| Open | Icons bloom outward from the pointer: origin cell first, then by ring distance; each fades in and scales 0.6 → 1.0 with a slight overshoot | ~120 ms per icon, ~15 ms stagger per ring, whole Grid ≤ 200 ms |
| Hover | Icon scales to 1.12 with a soft glow; label fades in beneath | 80 ms |
| Launch | Clicked icon pulses; Grid fades out | 120 ms + 100 ms |
| Close | Fade out | 120 ms |
| Group open/close | Cross-fade + small scale | 150 ms |

- Input is live from the first frame; a click during the open animation works.
- Motion setting: `full` / `reduced` (fades only) / `off`.

---

## 6. Items and launching

### 6.1 Launcher fields
- `name` — shown as label/tooltip.
- `command` — split into program + arguments using shell-style quoting (no shell). `runInShell`
  flag passes it to `/bin/sh -c` instead, for pipes, `&&`, `~` expansion etc.
- `workingDir` — optional.
- `env` — optional extra environment variables.
- `terminal` — optional: run inside the user's terminal emulator (§6.2).
- `icon` — either an **icon theme name** (`firefox`) or a **file path** (PNG, SVG, XPM, …).
- `desktopId` — optional link to the `.desktop` entry it was created from (used to refresh
  name/icon if the app updates them; never changes the position).

### 6.2 Terminal emulator
- One user-defined setting, `behaviour.terminal`: the command prefix that runs something in a
  terminal (`konsole -e`, `gnome-terminal --`, `kitty`, …). Editable in Preferences (0.7).
- First run fills it in automatically: `$TERMINAL` if it names an installed program; otherwise
  **Konsole**; otherwise the first installed of gnome-terminal, kgx, xfce4-terminal, kitty,
  alacritty, wezterm, ghostty, foot, tilix, terminator, mate-terminal, lxterminal, qterminal, xterm.
  Known terminals get the right "execute" argument automatically; unknown ones get `-e`.
- If the setting is empty, detection runs at launch time instead (nothing is written).

### 6.3 Launching
- Detached process (`QProcess::startDetached`) with the working directory set directly — no
  `chdir` of Prowal itself.
- An item whose program can't be found is drawn desaturated with a small warning badge, in both
  the popup and the Editor.

---

## 7. Configuration

### 7.1 Location and format
- `$XDG_CONFIG_HOME/prowal/prowal.json` (normally `~/.config/prowal/prowal.json`). `--config FILE`
  overrides.
- Versioned JSON (`"schema": 1`), via Qt's built-in JSON support — no extra dependency, fast to
  parse, migrations keyed on `schema`.
- Sketch:

```json
{
  "schema": 1,
  "behaviour": { "timeoutMs": 2000, "stayOpenAfterLaunch": false, "terminal": "xfce4-terminal -x" },
  "appearance": { "mode": "floating", "iconSize": 48, "spacing": 8, "labels": "hover", "motion": "full" },
  "grid": {
    "items": [
      { "x": 0, "y": 0, "type": "launcher", "name": "Thunar", "command": "thunar", "icon": "org.xfce.thunar" },
      { "x": 1, "y": 1, "type": "group", "name": "Games", "icon": "applications-games",
        "grid": { "items": [ ] } }
    ]
  }
}
```

### 7.2 Safety
- Writes go through `QSaveFile` (write temp, fsync, rename).
- Before each save, the previous file is copied to `~/.config/prowal/backups/` (keep the last 10,
  timestamped).
- Load is tolerant: unknown keys are preserved and ignored; invalid items are skipped with a
  warning shown in the Editor; duplicate cells are reported, not silently moved.

### 7.3 Importing from apwal
- On first run, if no Prowal config exists and `~/.apwalrc.xml` does, offer to import it (the
  popup shows a one-time banner; the Editor shows a dialog). `prowal --import-apwal [FILE]` does it
  explicitly and can be re-run during the transition period.
- The apwal file is opened read-only and never modified.
- Mapped: each app's command line (plus the older separate exec/args fields), working path, icon
  path and x/y; the timeout; the close-after-launch setting. Names are derived from the command
  or matching `.desktop` entry. Icon-browser settings are not imported.
- Command lines are kept verbatim. A trailing `# comment` (the author's config has several, used
  to keep alternative commands) keeps working because Prowal's command splitter treats a word
  starting with `#` as the start of a comment, as apwal did.
- Duplicate cells are resolved the way apwal displayed them (the later item moves right), so the
  imported layout looks identical.

---

## 8. Editor

### 8.1 Opening
- `prowal --edit`, or right click in the popup.
- **Opens instantly.** Nothing in the Editor's start-up waits on icon scanning; the icon picker
  (§8.5) indexes in the background only when it's first opened.
- Version shown in the title bar and the About dialog.

### 8.2 Layout canvas
- The Grid drawn at real icon size on a dot grid, with the origin cell marked by a crosshair
  ("this is where your pointer is").
- Pan by dragging empty space or scrolling; the canvas extends as needed.
- Drag an item to move it; dropping onto an occupied cell swaps the two.
- Double-click an item to edit it, double-click an empty cell to add a launcher there.
- Delete key removes the selection. Full undo/redo (`QUndoStack`).
- Breadcrumb bar for navigating into and out of Groups.

### 8.3 Properties panel
- Fields from §6.1 with live validation: command not found, working directory missing, icon not
  found — each shown inline, not just in red text.
- **Test launch** button.

### 8.4 Adding programs
- **Add from installed apps:** a searchable list built from the XDG `.desktop` files
  (`$XDG_DATA_DIRS/applications`, `~/.local/share/applications`), with icons, honouring
  `NoDisplay`/`Hidden`. Pick one, or drag it onto the canvas — name, command (field codes like `%U`
  removed), icon and `Terminal=` are filled in.
- Drag a `.desktop` file or an executable from a file manager onto the canvas.
- Manual add (empty launcher in the chosen cell).

### 8.5 Icon picker
- **Theme tab:** search icons by name across the current theme and its fallbacks (hicolor, etc.).
  The index is built from directory listings and `index.theme` files only — **no image decoding** —
  on a background thread, and cached in `$XDG_CACHE_HOME/prowal/` keyed by directory mtimes.
  Thumbnails are rendered lazily, only for visible results.
- **Files tab:** browse any folder; thumbnails load lazily in the background.
- **Recent tab:** icons used recently.
- Size filters exist but are optional, not a gate on loading.

### 8.6 Saving
- Explicit Save (Ctrl+S); unsaved-changes prompt on close. A running popup is not affected; the
  next popup reads the new file.

### 8.7 Preferences
- Appearance (mode, icon size, spacing, labels, motion, panel colour/opacity), behaviour (timeout,
  stay open, terminal emulator), and a button to re-run the apwal import.

---

## 9. Technical design

### 9.1 Build and versioning
- CMake; `project(Prowal VERSION x.y.z)` is the **single source of truth**, written into a
  generated `version.h`. The About dialog, `-V` and the Editor title all read it.
- Build output in `build/`; the build recreates a `prowal` symlink in the project root.
- Every code or build change bumps the patch version unless a milestone says otherwise.
- `make`/`ninja` warnings-as-errors in debug builds.

### 9.2 Command line
`QCommandLineParser`:
```
prowal                     show the popup
prowal --edit              open the Editor
prowal --config FILE       use FILE instead of the default config
prowal --grid NAME         show the named Grid instead of "main" (§4.5)
prowal --import-apwal [F]  import an apwal layout (default ~/.apwalrc.xml) into
                           "main", or the Grid named by --grid
prowal -h | --help
prowal -V | --version
```

### 9.3 Start-up performance
- Popup path links only QtCore/QtGui/QtWidgets; Editor code is in the same binary but not touched
  until `--edit`.
- **Rendered-icon cache:** each item's icon, rendered at its final pixel size, is cached in
  `$XDG_CACHE_HOME/prowal/icons/` keyed by (source, size, DPR, source mtime). A warm popup loads
  only small PNGs — no SVG rasterising, no theme lookups.
- `PROWAL_TRACE_STARTUP=1` prints timestamps for: process start, config parsed, icons ready, first
  frame. Used to check the 150 ms budget and to catch regressions.
- If the budget can't be met, add an optional resident mode later (§11) — not v1.

### 9.4 Window and platform code
- All X11-specific behaviour (compositor detection via the `_NET_WM_CM_S<n>` selection, input
  shape for click-through, override-redirect/stay-on-top, pointer position) lives behind one
  `PopupWindow` platform layer, so a Wayland/layer-shell backend can be added later without
  touching the rest.
- **Spike (first task of v0.1.0):** confirm on xfwm4 that a translucent, frameless Qt 6 window can
  (a) appear at the pointer without the WM moving it, (b) take keyboard focus for Esc,
  (c) close on outside click, and (d) set an X input shape so gaps are click-through while
  shadows still render. Record the result in `ARCHITECTURE.md`.
- Rendering: one custom-painted widget (`QPainter`), item animations driven by a single
  `QVariantAnimation`/timer, no per-icon widgets.

### 9.5 Modules (initial)
- `app/` — `main.cpp`, command line.
- `config/` — model (Grid, Item), JSON load/save, schema migration, apwal XML import
  (`QXmlStreamReader`).
- `popup/` — `PopupWindow`, layout/positioning, painter, animations, launching.
- `icons/` — icon resolution (theme name or path), rendered-icon cache, theme index.
- `desktop/` — `.desktop` file parser and application list.
- `editor/` — main window, canvas, properties, icon picker, preferences, undo commands.

### 9.6 Testing
- Qt Test unit tests for: config round-trip, tolerant loading of broken files, schema migration,
  apwal import (against a fixture modelled on a real layout), command splitting, `.desktop`
  parsing and field-code stripping, grid positioning and per-monitor clamping maths.
- Manual visual checklist per milestone, run against a temporary config, never the live one.
- Tests must never read or write the real `~/.config/prowal/` or `~/.apwalrc.xml`.

---

## 10. Milestones

Build vertical slices in order; each ends with a working, usable program.

| Version | Theme | Done when |
|---|---|---|
| **0.1.0** | Popup foundation | X11 spike recorded; CMake skeleton, `-h`/`-V`; JSON config load/save; apwal import; popup positioned and clamped per monitor; theme/path icons scaled and centred; click to launch; timeout, Esc, outside click; floating mode, no animation. **Can replace the apwal popup day-to-day.** |
| **0.2.0** | Polish | Bloom/hover/launch/close animations; labels; shadows; panel mode; no-compositor fallback; motion setting; start-up trace and budget met. *(Rendered-icon cache deferred: not needed at measured speeds — ARCHITECTURE.md §6.4.)* |
| **0.3.0** | Editor core | Canvas, drag/swap, add/delete, properties with validation, test launch, undo/redo, atomic save + backups, right-click-from-popup opens Editor on that item. **Replaces the apwal editor.** |
| **0.4.0** | Installed apps | `.desktop` parser, Add-from-apps dialog, drag-and-drop of `.desktop` files and executables. |
| **0.5.0** | Icon picker | Background index of every icon folder, browse-everything view, name search, threaded lazy thumbnails, recent icons, configurable folders. *(No disk cache: indexing is background and progressive — ARCHITECTURE.md §9.)* |
| **0.6.0** | Groups | Group items, in-place child Grids, breadcrumb editing, Esc/Backspace navigation. *(Plus: a Back button in a group's free centre cell; Move to ‹group› / Up in the editor; background preloading of group contents.)* |
| **0.7.0** | Preferences & keyboard | Preferences dialog; arrow-key/Enter navigation in the popup. *(Plus: live Preview from the dialog; settings share the editor's undo history.)* |
| **1.0.0** | Release | Hardening, man page, README, git release with built binary. *(Hardening: sanitizer runs, single editor per config, cell range, oversized-grid clipping, Wayland/no-display handling, desktopId icon fallback.)* |

Until 0.3.0 the old apwal editor can still be used: edit in apwal, then re-run
`prowal --import-apwal` to bring changes across.

---

## 11. Later / not in v1

- Resident mode (pre-loaded process woken over a local socket) if start-up ever feels slow.
- Wayland backend via layer-shell (wlroots/KDE); pointer position limits apply there.
- Running-app indicators.
- Translations.

---

## 12. Decisions log

1. Terminal emulator: user-defined, auto-detected on first run (Konsole first) — §6.2.
2. Group icons: `plain` / `badge` / `preview`, a preference — §5.2.
3. Named Grids and `--grid NAME`: in from 0.1.0 — §4.5.
4. Gaps between icons close the popup rather than passing clicks through — §4.2.
