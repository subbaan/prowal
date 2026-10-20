# Prowal Architecture (as built)

`project.md` is the spec and roadmap; this file records what was actually built, the decisions
taken on the way, and what was measured. Newest milestone last.

## 1. v0.1.0 — Popup foundation

### 1.1 Module map

| Path | Role | Links |
|---|---|---|
| `src/config/Model.h` | `Config` → `Grid`s → `Item`s (Groups hold child items). Every level keeps an `extra` `QJsonObject` of unknown keys. | — |
| `src/config/Config.*` | Tolerant JSON load (`parseConfig`), save (`saveConfig`: backup + `QSaveFile`), `validateGrid`, `defaultConfig`. | core |
| `src/config/ApwalImport.*` | Reads `~/.apwalrc.xml` with `QXmlStreamReader` (read-only), `applyApwalImport` replaces one Grid's items. | core |
| `src/config/Paths.*` | XDG config path, backup dir, apwal file path. | core |
| `src/system/CommandLine.*` | `splitCommand`: POSIX-shell-like word splitting without a shell (quotes, backslash, `#` comments, `~`). | core |
| `src/system/Terminal.*` | Terminal detection and per-terminal "execute" arguments. | core |
| `src/system/Launch.*` | `planLaunch` (pure: what would run) and `launchItem` (`QProcess::startDetached`). | core |
| `src/popup/Layout.*` | Pure geometry: cursor-relative placement, per-monitor clamping, hit testing. | core |
| `src/popup/PopupWindow.*` | The popup: one custom-painted `QWidget`, input handling, timers. | exe |
| `src/icons/IconLoader.*` | Theme name or file path → pixmap that fits the cell; placeholder; desaturate. | exe |
| `src/icons/IconTheme.*` | Makes `QIcon::fromTheme` follow the desktop's icon theme. | exe |
| `src/platform/X11.*` | Compositor detection, keyboard-grab retry. The only xcb code. | exe |
| `src/app/main.cpp` | Command line, first run, wiring, start-up trace. | exe |

`prowal_core` is a static library with no QtWidgets and no display dependency; all five unit
test programs link only against it.

### 1.2 Start-up path
`main` → (only `-h`, `-V`, `--import-apwal` skip the display: they run on a `QCoreApplication`)
→ create config on first run → `loadConfig` → pick Grid → terminal → icon theme → compositor check
→ `PopupWindow::popupAtCursor` (load icons, compute layout, show) → event loop. Hiding the popup
quits the app.

### 1.3 Measured
`PROWAL_TRACE_STARTUP=1`, the author's 32-item layout, Xvfb + picom, Release build:

| Stage | ms |
|---|---|
| QApplication created | 45–73 |
| config loaded + icons + layout + shown | +5 |
| first frame painted | **47–80 total** |

Almost all of it is `QApplication` construction (platform plugin, platform theme). Inside the
150 ms budget, so the rendered-icon cache and resident mode can wait. Re-measure on the real
display: the GTK platform theme talks to the xsettings daemon there.

## 2. X11 spike results (project.md §9.4)

Tested in Xvfb with picom (`--backend xrender`), driven by `xdotool`; also without a compositor.

| Question | Result |
|---|---|
| (a) Appears at the pointer, not moved by the WM | Yes. `Qt::Popup` is override-redirect on xcb, so the WM never places it. |
| (b) Keyboard for Esc | Yes, via Qt's popup keyboard grab — **plus a retry** (see §4). |
| (c) Close on outside click | Yes, Qt's popup pointer grab delivers it. |
| (d) Click-through gaps with shadows | Not pursued — see §3. |
| No compositor | Detected via `_NET_WM_CM_S<n>` owner; falls back to `setMask` from the icons' alpha. Works. |

## 3. Decision: gaps close the popup instead of clicking through

The pointer grab that makes click-outside-to-close and Esc reliable also means no click can
reach windows underneath, so input-shape click-through can't coexist with it. Closing on a gap
click was judged better anyway: with apwal bound to a desktop click, a stray click in a gap
could re-trigger the launcher or act on the desktop. project.md §4.2 updated.

## 4. Keyboard grab retry

The author launches apwal from an Xfce keyboard shortcut (`Launch9`). While a shortcut key is
held, the WM's passive grab is active and nobody else can grab the keyboard. Qt grabs once when
the popup is shown, so if the key is still down then, Esc would never work.
`PopupWindow::retryKeyboardGrab` retries `xcb_grab_keyboard` every 25 ms for up to 3 s.
Verified with a helper that holds a grab for 800 ms: Esc is ignored while held, then closes the
popup once released.

## 5. Other behaviour notes

- **Trigger-release guard:** an item only launches when press *and* release happen on it. The
  release of the button that opened Prowal (mouse-bound trigger) therefore can't launch the item
  under the pointer. Verified.
- **Leave timeout** is polled (50 ms) from `QCursor::pos()` against the grid rect rather than using
  enter/leave events, which are unreliable under grabs.
- **Duplicate cells:** config load warns; the popup shows the first item. The apwal import moves
  later duplicates right, as apwal displayed them.
- **Missing programs** are drawn greyscale at ~55 % opacity; with no compositor that opacity
  blends with the window colour (acceptable for the fallback).
- **Icon theme:** on Xfce, Qt 6 loads the GTK3 platform theme, which reports the real theme from
  xsettings. Fallbacks for when it doesn't (no platform theme, or `hicolor`): Xfce's
  `xsettings.xml`, then `gtk-3.0/settings.ini`, then `kdeglobals`. Note the author's
  `settings.ini` names a stale theme (`flattrcolor`), which is why xsettings comes first.
- **Logging** uses `fprintf(stderr)`, not `qWarning`: Qt sends `qWarning` to journald when stderr
  isn't a terminal, so messages vanished when run from a shortcut.

## 6. v0.2.0 — Polish

### 6.1 Animation engine
`PopupWindow` holds one `QElapsedTimer` (started when shown) and one 16 ms frame timer that runs
only while something is moving (`animating()`), then stops. Every frame is a pure function of
elapsed time, so there is no per-item `QPropertyAnimation` state to manage:

- **Appear:** delay = `min(110, 22 × distance-from-origin)` ms; 150 ms `OutCubic` fade plus a scale
  from 0.55 with `OutBack` (overshoot 1.3) for a gentle settle.
- **Hover:** a 0–1 value per item that moves towards its target at 1/90 ms; drives a 12 % lift,
  the accent glow, a stronger, lower shadow, and the label's fade.
- **Launch pulse:** `1 + 0.22 · sin(πt)` over 170 ms. The close fade starts 45 % into it. The
  process is started *before* any of this, so animation never delays a launch.
- **Close:** 130 ms `InQuad` fade, plus a 6 % shrink in full motion. `closeEvent` is ignored
  until the fade finishes, which also turns Qt's own popup-closes-on-outside-click into the fade
  (verified with instrumented logging: ~145 ms from click to hide).
- All durations are divided by `appearance.animationSpeed`. `reduced` = fades only, no stagger;
  `off`, or no compositor = no animation.

### 6.2 Shadows and labels
- Shadows are rendered once per icon at popup time: the silhouette's alpha is blurred with three
  box-blur passes (≈ Gaussian), radius 7 px. The panel shadow is a blurred rounded rect.
- Window margins grow to hold shadows and hover labels (48 px each side and room below when
  labels are on), so labels near the edge aren't clipped. Margins may hang off-screen; clamping
  uses the grid rect.
- `labels: always` adds a label row to each cell (`LayoutInput::labelHeight`); the pointer is
  still centred on the origin *icon*. Elided names expand to the full name on hover.
- Without a compositor, hover labels fall back to tooltips and fixed labels are part of the
  window shape.

### 6.3 Start-up: skipping the platform theme
Qt loads the desktop's platform theme plugin (GTK3 on Xfce) during `QApplication` construction:
about 25 ms, roughly half the total. The popup draws its own look and only needs the icon theme
name, which Prowal can read itself on Xfce (xsettings) and KDE (kdeglobals). So on those
desktops, unless the user set `QT_QPA_PLATFORMTHEME`, the popup sets it to `generic` (Qt's
built-in fallback; an unknown name just makes Qt fall through to GTK3) and **unsets it right
after `QApplication` is built**, so launched programs inherit a clean environment. `--edit` never
skips it.

Measured (Xvfb + picom, 32 items, Release): QApplication 6–14 ms, icons + shadows +8–12 ms,
**first frame 29–52 ms** (0.1.0: 47–89 ms).

### 6.4 Deferred: rendered-icon cache
Planned for 0.2 in project.md, but loading all 32 icons *including* blurred shadows measures
8–12 ms, well inside the 150 ms budget. A disk cache would add staleness problems (theme updates)
for no visible gain, so it's deferred until a measurement says otherwise.

## 7. v0.3.0 — Editor

### 7.1 Structure
| Path | Role | Links |
|---|---|---|
| `src/editor/Document.*` | The config being edited. Every change is a `SnapshotCommand` on a `QUndoStack` holding before/after copies of `Config::grids`; `changed()` drives all UI refreshes. Save = `saveConfig` + `setClean()`; `changedOnDisk()` compares the file's mtime with the last load/save. | core |
| `src/config/GridOps.*` | `itemIndexAt`, `moveOrSwap`, `freeCellNear`, `uniqueGridName`. | core |
| `src/system/Launch.*` | + `commandProblem`, `workingDirProblem`, `expandHome` for live validation. | core |
| `src/editor/GridCanvas.*` | Custom-painted canvas; emits requests (move/add/edit/delete/test), never mutates. | exe |
| `src/editor/PropertiesPanel.*` | Item form; commits on editingFinished / toggle / env focus-out; validates on every keystroke. | exe |
| `src/editor/EditorWindow.*` | Menus, toolbar, grid switcher, save/close/import flows; owns the Document. | exe |

**Why snapshots instead of fine-grained commands:** items have no stable ids (they're addressed
by index within a grid), and grids can be added, renamed and deleted. Whole-grid-list
snapshots make every step trivially reversible with no index bookkeeping; a 32-item layout is a
few KB per step. Selection is tracked by *cell*, not index, so it survives undo/redo.

### 7.2 Behaviour notes
- **Commit points:** a field edit becomes one undo step when the field loses focus or Enter is
  pressed, so typing a command isn't fifty undo steps. Save and close move focus to the canvas
  first, so a half-typed field is committed rather than lost.
- **Refreshes don't fight the user:** after a commit the panel only rewrites fields whose value
  actually changed (and shows long values from the start).
- **Initial view** centres the layout's bounding box (which always includes the pointer cell)
  so wide layouts fit; Home centres the pointer cell. A preselected `--item` cell is scrolled
  into view once the canvas has a size.
- **`main` is protected** from rename/delete because plain `prowal` shows it.
- **Saving over an external change** asks first; the other version is safe in `backups/`
  because `saveConfig` copies the existing file there before writing.
- **The popup's right-click** runs `QCoreApplication::applicationFilePath()` with `--edit
  --config <same file> --grid <same grid> [--item x,y]`, then fades out.
- The editor keeps Qt's desktop platform theme (unlike the popup), so it looks native.

### 7.3 Measured
Editor start (`PROWAL_TRACE_STARTUP=1`, Xvfb, 32 items): event loop running at **88–114 ms**.

## 8. v0.4.0 — Installed apps and drag-and-drop

### 8.1 `.desktop` handling (`src/desktop/DesktopEntry.*`, core)
- Only the `[Desktop Entry]` group is read; localised keys pick the first match from
  `{lang_COUNTRY, lang}`; string escapes (`\s \n \t \r \\`) are applied first and unknown ones
  are left for the Exec parser, as the spec orders it.
- **Exec → argv → command line:** Exec is tokenised with its own rules (double quotes with
  `\" \` \$ \\` escapes; a stray backslash outside quotes is tolerated because real files have
  them), field codes are dropped or expanded (`%i` → `--icon NAME`, `%c` name, `%k` file),
  Flatpak `@@`/`@@u`/`@@f` markers are dropped, and the argv is re-quoted with `joinCommand`
  so `splitCommand` gives exactly the same argv back (round-trip tested, including `'`, `~`,
  `#` and `$`). Stored commands therefore never depend on desktop-file quoting.
- **Listing** follows XDG order (`~/.local/share/applications` first, then each data dir —
  here also flatpak and snap exports); the first file with a given id wins, so a user copy with
  `Hidden=true` deletes a system app. Ids use `/` → `-` for subdirectories. `TryExec` misses are
  dropped (not installed); `NoDisplay`/`OnlyShowIn`/`NotShowIn` only affect the default view.
- Measured on the dev machine: **400 apps in 15 ms**, so the picker reads them synchronously
  each time it opens (newly installed apps just appear). No cache.

### 8.2 Picker and drops (`src/editor/AppPicker.*`, `GridCanvas` drop handling)
- `AppListModel` loads icons lazily from `DecorationRole`, so only visible rows touch the icon
  theme. Dragging an app exports its `.desktop` file as a `text/uri-list` URL, so the canvas has
  a single drop path for the picker and for file managers. The view overrides `startDrag` to
  show the app icon rather than Qt's whole-row picture.
- Placement (`EditorWindow::placeItems`): the first item goes to the drop/selected cell if free,
  each further one to the nearest free cell (`freeCellNear`); the whole batch is one undo step.
- "Already in the grid" matches by desktop id or identical command.

## 9. v0.5.0 — Icon picker

### 9.1 Requirement
The author liked apwal's "see every icon" browsing but not its start-up scan. Measured here:
`/usr/share/icons` + `~/.icons` + `~/.local/share/icons` + pixmaps = **142,812 distinct icons
(26k symbolic) in 55 collections** after keeping one file per (collection, name). Listing takes
~13 s cold in the background; decoding is ~0.4 ms per 40 px thumbnail. Pre-decoding all of them
would need ~900 MB, so "load everything" is: index everything in the background, decode on
demand fast enough that it looks loaded, and pre-load outward within a fixed budget.

### 9.2 Structure
- `src/icons/IconIndex.*` (core, tested): `collectionDirs` splits a folder into collections
  (a theme dir, a plain subfolder, or the folder's loose images); `scanIconCollection` keeps the
  best file per name (size from the path — `48x48`, `48`, `24@2x`, `scalable` — never by decoding;
  bigger beats smaller; SVG wins; PNG beats XPM). `QDirIterator` without `FollowSymlinks`, so links
  can't lead onto other drives. `themeChain` follows `Inherits=`.
- `src/editor/IconPicker.*`:
  - an indexer `QThread` posts one collection at a time (tagged with a generation number, so
    batches from an abandoned scan are ignored); new icons are only *appended*, so the rows on
    screen never move while the scan continues;
  - `ThumbnailLoader`: N−1 worker threads, LIFO-free ordered queue that the GUI *replaces* on
    every scroll (visible → next two screens → previous screen → outward to the budget), so
    no stale work is done first. Results come back as `QImage` via queued calls; the loader is
    recreated on rescan so late results can't land on reused indices;
  - `IconGridView`: a `QAbstractScrollArea` that paints only visible cells with arithmetic
    positions (a `QListView` in icon mode keeps per-item geometry and doesn't scale to 142k).
  - One picker per editor session, so the index is built once.

### 9.3 Memory
First version cached `QPixmap`s with a 200 MB budget and reached 644 MB: the cost counted the
`QImage` but the pixmap held another copy, and the budget was simply too generous. Now: cached
as `QImage` (drawn directly), 64 MB budget (~10k thumbnails — any single theme fits).
Measured: editor 128 MB → **~300 MB with the whole index built** (≈80 MB index, ≈90 MB
thumbnails). All of it is released when the editor closes; the popup never loads any of this.

### 9.4 What gets saved
An icon from the current theme or one it inherits is saved by name (follows theme changes);
anything else as its path. "Use this exact file" overrides. Recently used entries remember the
exact spec that was saved.

## 10. v0.6.0 — Groups

### 10.1 Addressing a level
A level is a grid plus a **group path**: the cells of the Groups from the grid's top level down
(`GroupPath = std::vector<QPoint>` in `config/GridOps.h`, text form `2,0/1,-1`). Cells rather
than indices, so a path survives items being added, deleted or moved *around* it. `groupItems()`
resolves a path (nullptr once it stops leading to a Group, e.g. after an undo). All `Document`
item operations take `(grid, path)`; `changeLevel()` copies the grids, applies the change to the
resolved level and pushes one snapshot step. `moveToLevel()` refuses to put a Group inside
itself or its own descendants.

### 10.2 Popup
- `PopupWindow` keeps a stack of `Level {items, name, cell, anchor}`; the top is on screen.
  `layoutLevel()` builds entries and runs `computeLayout()` with the level's anchor as the
  "cursor": a Group's anchor is its icon centre, so the child's origin cell lands exactly on it.
  Going back pops the stack and re-lays the parent from its stored anchor (same place as before).
- **Back button:** inside a Group, a free (0, 0) gets an entry with `item == nullptr`.
- **Cross-fade:** `changeLevel()` grabs the current level's own rect (`m_ownWindow`) into a
  pixmap, lays out the new level, and while the ghost fades (150 ms) the window is the union of
  the old and new rects, so the ghost isn't clipped; layout rects are shifted into that window.
  Opening: ghost scales up 12 % around the Group, new level blooms (stagger from its origin).
  Back: ghost shrinks 25 % into the Group's cell, parent settles in from 110 %. Reduced motion
  fades only; off (or no compositor) switches instantly.
- Animation times for entries are relative to `m_levelStart` (`levelTime()`); pulses and the
  close fade stay on the absolute clock.
- Pointer maths uses `m_layout.window` rather than `mapFromGlobal()`: right after `setGeometry`
  the window system may not have reported the new position yet.
- **Icons:** `iconFor()` caches pixmap + shadow + availability per `const Item *` (the config
  is const for the popup's lifetime). Group contents are preloaded 250 ms after showing, two
  items per 1 ms timer tick, capped at 400; group open measured ~4 ms preloaded, ~22 ms cold.
- Right-click inside a Group passes `--group <path>` to the editor.

### 10.3 Editor
`EditorWindow` holds `m_grid` + `m_group`; `level()` is what the canvas shows. `refresh()` pops
`m_group` until it resolves (an undo may have removed the Group) and selects the Group it fell
out of. Breadcrumbs are rebuilt on every refresh. Backspace/Alt+Up are a window `QAction`; line
edits still get Backspace because they claim it via ShortcutOverride. `GridCanvas` draws Groups
with `groupIcon()` in the configured style (cache key includes child icons for `preview`), and
emits `openRequested`, `moveIntoRequested`, `moveUpRequested`, `addGroupRequested`.

### 10.4 Group icons (`IconLoader`)
`groupIcon(item, style, size, dpr, accent)`: `plain` = the icon; `badge` = icon + dark disc with
a 2×2 glyph, bottom right; `preview` = dark rounded tile with the four children nearest the
origin (falls back to badge when empty). `backIcon()` draws the chevron button. All painted, so
they look the same in every icon theme.

## 11. v0.7.0 — Preferences and keyboard

### 11.1 Preferences (`src/editor/PreferencesDialog.*`)
- Tabs General (terminal, close-after, stay open), Look (style, panel colour/opacity, icon size,
  spacing, labels, group icons, icon theme, highlight colour, shadows), Motion (animation,
  speed). It edits copies of `Behaviour`/`Appearance` (so unknown keys in `extra` survive) and
  emits `applied()`; the editor turns that into `Document::setSettings()`, one undo step,
  saved with the layout. Nothing is written until Save, like any other edit.
- `Document` snapshots are now the **whole `Config`**, not just the grids, so settings and grid
  edits share one undo history. `EditorWindow::applySettings()` runs from every `refresh()` and
  re-resolves the terminal, icon theme and group style, so undoing a preference change takes
  effect in the editor too.
- Terminal: an editable combo of `installedTerminals()` (known terminals found on PATH, as
  prefixes) plus "Automatic (detected)" = empty, resolved at launch. "Try it" runs a 5-second
  `sh -c 'echo …; sleep 5'` in the chosen terminal.
- Icon theme: `installedIconThemes()` lists theme directories whose `index.theme` has
  `Directories=` (skips cursor themes) and isn't `Hidden`. `desktopIconTheme()` remembers the
  platform theme seen at start-up (a function-static, read before any override), so "follow the
  desktop" can be restored after an override was applied.
- Speed slider is logarithmic (−100…100 ↔ 0.25×…4×, normal in the middle); an untouched slider
  keeps a hand-written value it can't represent exactly.
- **Preview** builds a config from the dialog's current values and shows a real `PopupWindow`
  in the editor process, at the pointer. The popup now owns a copy of its `Config` and reports
  closing through a `closed` callback (main() quits there; the preview deletes itself and
  restores the editor's icon theme). In preview mode right click just closes it.

### 11.2 Keyboard navigation in the popup
- Arrows move the hover highlight (the same state the mouse drives, so labels and glow come for
  free). `cellInDirection(cells, from, dir)` in `Layout.*`: candidates strictly ahead; score =
  distance ahead + 2 × sideways distance, so the same row/column wins over a nearer diagonal and
  gaps are skipped. With nothing highlighted, movement starts from the origin cell (0, 0), used
  or not. Home goes to `nearestCell(0, 0)`.
- Enter/Return/Space activates (groups open, Back goes back); Shift keeps the popup open, like
  middle click. Opening or leaving a level by keyboard puts the highlight on the new level's
  origin.
- **Keyboard mode** starts with an arrow key and ends when the pointer really moves:
  `mouseMoveEvent` compares global positions, because moving/resizing the window on a level
  change produces synthetic moves. While in keyboard mode the pointer poll does nothing, so the
  leave timeout doesn't close the popup under someone using the keys.

## 12. v1.0.0 — Hardening and release

- **Single editor** (`src/platform/SingleInstance.*`): a Unix socket per config file in the
  runtime dir (`prowal-editor-<sha1(path)[:16]>.sock`, mode 0600). `--edit` first tries to
  connect and send `{"grid","group","item"}` (JSON); if nobody answers it removes any stale
  socket, listens, and opens. `EditorWindow::reveal()` handles requests. Plain POSIX +
  `QSocketNotifier`, so neither path links QtNetwork. Reads are capped at 64 KB and 500 ms.
  The socket is removed on normal exit; one left by a crash is replaced by the next editor.
- **desktopId fallback:** `findApplication(id, dirs)` (core) tries the id as a file name and
  with its dashes read as subdirectories; `loadItemIcon()` uses the app's current `Icon=` when a
  launcher's own icon isn't found. Only on a miss, so normal start-up is unaffected.
- **Cell range** ±99 (`kMaxCell`, `cellInRange()` in `Model.h`): the parser skips anything else
  (also non-integers, NaN-ish and 1e300), `Document` refuses moves/adds outside it, the canvas
  clamps. Keeps the layout maths far from `int` overflow.
- **Oversized grids:** `clampSpan()` no longer aligns a too-large grid to the monitor's start
  (that put the origin cell off-screen); it stays centred on the pointer, and `computeLayout()`
  clips the window to the monitor plus margins, shifting the local rects to match.
- **Platform:** no `DISPLAY` → message and exit 1 (before Qt aborts). `WAYLAND_DISPLAY` with no
  `QT_QPA_PLATFORM` → force `xcb` for Prowal and unset it again so children get Wayland.
- **Sanitizers:** `make sanitize` (`PROWAL_SANITIZE`, `build-asan/`). Run against all unit
  tests and scripted sessions (popup with groups/back/keys/launch, editor add/group/move/delete/
  undo/redo, icon picker full index, preferences + preview twice). No ASan/UBSan reports; LSan
  found no leaks from `src/`.
- **Packaging:** `docs/prowal.1.in` → `build/prowal.1` (version filled in), checked with
  `groff -man -ww`; `data/prowal-editor.desktop` passes `desktop-file-validate`; `make dist`
  tars the stripped binary, man page, desktop file, README, CHANGELOG and LICENSE.

## 13. Gotchas

- **moc vs raw strings:** moc failed to see `Q_OBJECT` in a test file containing C++ raw string
  literals with embedded quotes (link error: undefined vtable). Don't use `R"(...)"` in files that
  moc processes.
- **Xfce's xsettings.xml is XML 1.1**: `QXmlStreamReader` refuses it. `IconTheme.cpp` matches the
  `IconThemeName` element with a regex instead.
- **`QT_QPA_PLATFORMTHEME` unknown names don't disable themes**: Qt falls through to the next
  candidate. Use `generic`.
- **`pkill -f` / `pgrep -f` in test scripts** match the test script's own command line (and kill it); use `pgrep -x prowal` and check `/proc/PID/cmdline`, or kill by PID.
- **Test display focus:** with no window manager on Xvfb, keyboard focus is lost after a modal
  dialog closes, so later `xdotool key` presses go nowhere. Use `xdotool windowfocus --sync`
  before sending keys after a dialog. Not an issue under a real WM.
- **Never `pkill -x prowal` in tests**: it would also hit the user's real popup or editor on
  their own display. Kill by PID.
- **Compositor shadows on the popup:** picom (with its default `/etc/xdg/picom.conf`) shadows
  every window, which showed as a faint rectangle around the translucent popup. The popup sets
  `_GTK_FRAME_EXTENTS = 0,0,0,0` (`x11::drawsOwnShadows`), which picom's default rules exclude
  from shadows. picom's `fading` still fades the popup in; README tells picom users how to turn
  that off for `class_g = 'prowal'`. The test display uses picom, so this also affects screenshots.
- **QSettings and commas:** `QSettings` (IniFormat) reads an unquoted value containing a comma
  as a `QStringList`, and `toString()` on that is empty. `index.theme`'s `Directories=a,b,c`
  therefore looked empty and nearly every theme was dropped from the icon theme list (fixed in
  1.0.1). Read `index.theme` values with `iniText()` in `IconIndex.cpp` (`toStringList().join`).
- Only the main `build/` owns the `./prowal` symlink (`PROWAL_LINK_ROOT`); `make debug` builds in
  `build-debug/` with it off.

## 14. Testing method

- `make test`: unit tests (offscreen, no display). Fixture `tests/data/apwalrc-sample.xml` is
  synthetic, modelled on a real layout (comments in commands, legacy exec/args, placeholders,
  duplicate cells, negative coordinates).
- Interactive checks run on a private Xvfb display (`:77`) with picom, using a scratch config —
  never the real `~/.config/prowal` or the user's screen. `xdotool` drives clicks/keys; `import`
  takes screenshots. To see animation stages, use a scratch config with `animationSpeed: 0.25`
  and screenshot at fixed delays. picom paints the root window black, so put a light window
  (`display -geometry ... wall.png`) behind the popup to check shadows.
- Run the editor with `XDG_CONFIG_HOME=<scratch>` (copy Xfce's `xsettings.xml` in for the icon
  theme) so the icon picker's `icon-picker.ini` (recent icons, folders) is never written to the
  real `~/.config/prowal/`.
- **Launchers in test configs must be harmless.** Generate the config with every launcher's
  command replaced by `touch <scratch>/launched-<name>` (no shell, no terminal, no working
  directory), and grep that no other command is left. Whether something launched is then a file
  check. (In 0.7 development a stale binary turned a key sequence into Enter on Baobab from a
  copied real layout; it was killed by PID on the test display within ~1.5 s.)
- After a dialog closes on the test display, `xdotool windowfocus --sync` before any key, and
  rebuild with a plain `ninja -C build` (not chained after a command that can fail) before a run.
- **README screenshots** (`docs/screenshots/`) come from a demo layout (Papirus icons,
  well-known app names) on `:77`. The wallpaper is a generated ImageMagick plasma shown in a
  borderless `feh --no-fehbg -x` window (picom paints the root black). Popup shots use
  `touch` commands; editor shots show realistic commands (`firefox`, `code`) but run with
  `PATH=<scratch>/stubbin` holding stub scripts that only `touch`, and `HOME=<scratch>/home` so
  the icon picker doesn't show the user's own `~/.icons` collections. Xvfb captures no cursor:
  one is drawn on with ImageMagick. Window shots get a drop shadow; `optipng -o2` after.
- External drag-and-drop is tested with a throwaway helper that starts a `QDrag` with file URLs
  when clicked (kept in the session scratchpad, not the repo), driven by `xdotool` presses.
