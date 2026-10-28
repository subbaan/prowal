# Changelog

## 1.0.3 — 2026-10-28

- README: the top screenshot is now the plain apwal-style popup: monochrome icons floating around
  the pointer on a flat background, with no panel and no labels. The panel, colour and group
  shots sit in a row below it.

## 1.0.2 — 2026-10-21

- **Fix:** apps added with *Add from installed apps* went one step up and left of the last one,
  so repeated adds made a diagonal line. They now fill the gaps in the grid's outline first
  (top row first, left to right), then start a new row on top at the left and fill it. A free
  selected cell is still used for the first one; a narrow grid widens to 5 columns; inside a
  group the centre cell stays free for the Back button.
- README: the panel look is the top screenshot; the plain popup and an open group sit below it.

## 1.0.1 — 2026-10-20

- **Fix:** Preferences → Look → Icon theme listed only themes with a single icon directory, so
  almost every real theme (Papirus, Breeze, Adwaita…) was missing or marked "(not installed)".
  `index.theme` values with commas are now read correctly, including theme names.
- README screenshots (`docs/screenshots/`), made from a demo layout on a private display.

## 1.0.0 — 2026-10-20

First release.

- **One editor per config:** a second `prowal --edit` (for example a right click in the popup
  while the editor is open) brings the open editor forward at the clicked item instead of
  starting another one that could overwrite its changes.
- **Icons that apps rename:** if a launcher's icon disappears and it was added from an installed
  app, Prowal shows the icon that app's `.desktop` file names now; the editor says so.
- **Safer configs:** cells must be whole numbers from -99 to 99 (others are skipped with a
  warning), and a grid bigger than the monitor is shown centred on the pointer with the window
  cut to the monitor, instead of a huge window with the middle cell off-screen.
- In a Wayland session Prowal runs through XWayland by itself (launched apps still get Wayland);
  with no display at all it says so instead of crashing.
- **Man page** (`prowal.1`), an editor menu entry (`prowal-editor.desktop`), install rules for
  packagers, and `make dist` for the release tarball. `make sanitize` builds and tests with
  AddressSanitizer and UBSan; the popup, groups, keyboard, editor, icon picker and preferences
  were run under it with no reports and no leaks from Prowal's code.
- README rewritten as a user guide.

## 0.7.0 — 2026-10-20

Preferences, and the keyboard in the popup.

- **Preferences** (editor: Edit → Preferences…, toolbar, Ctrl+,):
  - *General:* the terminal for "Run in a terminal" launchers (Automatic, any installed one, or
    type your own) with a **Try it** button; how long the popup waits after the pointer leaves;
    keep the popup open after launching.
  - *Look:* floating icons or a panel (colour, opacity), icon size, spacing, labels, group icon
    style, icon theme (or follow the desktop), highlight colour, shadows.
  - *Motion:* full / reduced / off, and speed (0.25×–4×, normal in the middle).
  - **Preview** shows the popup with the settings as they are in the dialog, at the pointer,
    before you apply them. **Restore Defaults** resets the dialog.
  - Applying is one undoable step (Ctrl+Z undoes it) and is saved with the layout (Ctrl+S).
- **Keyboard in the popup:** arrow keys move the highlight (starting from the cell under the
  pointer, skipping gaps, preferring the same row or column); **Enter** or **Space** launches or
  opens a group (**Shift+Enter** keeps the popup open); **Home** goes back to the middle. While
  you use the keys the popup doesn't time out; moving the mouse takes over again.
- Editor: Undo/Redo in the toolbar are icon-only, so the toolbar no longer changes width with
  each step's name (the tooltip still says what will be undone).

## 0.6.0 — 2026-10-20

Groups: icons that open their own grid (submenus).

- **In the popup**, clicking a group opens its items in place, centred on the group's icon, with a
  short cross-fade (the previous level swells away, the group's items bloom out from the
  pointer). Groups nest. **Esc** or **Backspace** goes back up a level (Esc at the top closes);
  if a group leaves its centre cell free, that cell becomes a **Back** button, so it all works
  with the mouse alone.
- Group icons follow the `groupStyle` setting: `badge` (default: the group's icon with a small
  grid badge), `preview` (a 2×2 tile of the first four icons inside) or `plain`. A group with no
  icon uses the theme's `folder`.
- What's inside groups is loaded in the background just after the popup appears, so opening a
  group takes ~4 ms (~22 ms if clicked within the first quarter second). Start-up is unchanged.
- **In the editor:** Add group (Ctrl+G, toolbar, right-click an empty cell). Double-click or
  Enter opens a group; a breadcrumb bar (main › Tools › …) and Backspace / Alt+Up go back out.
  Right-click an item → **Move to** → *Into <group>* / *Up to <parent>*; it keeps its cell if
  free there. The properties panel shows what a group contains, with an **Open group** button.
  Deleting a group says how many items went with it (Ctrl+Z restores them). Problem dots on a
  group mean something inside it has a problem.
- Right-click inside a group in the popup opens the editor inside that group
  (`prowal --edit --group X,Y[/X,Y…] --item X,Y`).
- Fix: compositors that add shadows to every window (picom's default config) drew a faint
  rectangle around the whole popup. The popup now marks itself as drawing its own shadows
  (`_GTK_FRAME_EXTENTS`), which picom's defaults respect.

## 0.5.0 — 2026-10-20

The icon picker (Icon → Choose… in the editor).

- **See everything:** every icon in your icon folders, browsable in one "All icons" view or per
  theme/folder, with live thumbnails. The window opens instantly and fills in while a
  background scan runs (142,812 icons in 55 collections took about 13 s on the dev machine;
  the first ~30,000 were browsable after 0.4 s).
- Thumbnails decode on background threads, visible ones first, then ahead of where you're
  scrolling, then outward until a 64 MB budget is full. Jumping to the end of 142k icons shows
  thumbnails within ~150 ms. The scan never follows symlinked folders.
- One entry per icon per theme (the file nearest 48 px; scalable SVG preferred). Monochrome
  "-symbolic" icons are hidden unless asked for.
- Search by name (type anywhere, or Ctrl+F); your current icon theme and what it inherits are
  listed first (★). Icons from that theme are saved by **name**, so they follow theme changes;
  anything else is saved as the exact file (switchable).
- **Recently used** icons, **Other file…** for any image, and **Folders…** to choose which
  folders are browsed (defaults: ~/.icons, ~/.local/share/icons, /usr/share/icons,
  /usr/share/pixmaps). These are kept per machine in `~/.config/prowal/icon-picker.ini`, not in
  your layout.

## 0.4.0 — 2026-10-20

Adding programs the easy way.

- **Add from installed apps** (Ctrl+I, toolbar, right-click an empty cell): a searchable list of
  the applications in your menus, with icons. Search matches names, descriptions, keywords and
  commands. Enter or double-click adds next to the selected cell; select several to add them
  all; or drag apps straight onto the grid. Apps already in the grid are marked. "Include apps
  hidden from menus" shows the rest.
- Name, command, icon, start-in folder and run-in-terminal come from the app's `.desktop` file.
  Field codes (`%U` etc.) are removed, Flatpak's `@@` markers dropped, and Exec quoting is
  followed exactly (checked against every app on the dev machine, including Wine and Flatpak
  entries).
- **Drag and drop onto the grid** from a file manager: `.desktop` files and programs (scripts,
  AppImages, binaries) become launchers where you drop them; an image dropped onto an icon
  replaces its picture. Other files are refused with a message.

## 0.3.0 — 2026-10-20

The editor (`prowal --edit`), replacing apwal's.

- Opens in about 0.1 s; nothing is scanned at start-up.
- Grid canvas around a marked pointer cell: drag an icon to move it, drop it on another to swap,
  Alt+arrows to nudge, double-click to edit (or to add in an empty cell), Delete to remove,
  right-click for a menu. Pan by dragging empty space, middle-dragging or scrolling; Home
  recentres.
- Properties panel: name, command, run through the shell, run in a terminal, start-in folder,
  icon (theme name or file, with preview and Browse…), environment variables. Problems (program
  not installed, folder missing, icon not found) show as you type, and as dots on the canvas.
- Test launch (Ctrl+T), full undo/redo, Save (Ctrl+S) with automatic backups, a prompt for
  unsaved changes, and a warning if the file was changed by something else since it was opened.
- Grids: switch, create, duplicate, rename and delete named grids (`main` is protected).
- File → Import from apwal… replaces the current grid's items (undoable).
- In the popup, right-click an icon to open the editor on that item; right-click a gap to open
  the editor.
- `--item X,Y` (with `--edit`) selects a cell on opening.

## 0.2.0 — 2026-10-20

Polish.

- Opening animation: icons bloom outward from the pointer, nearest first, fading and scaling in
  with a gentle settle. Clicks work from the first frame.
- Hover: the icon lifts with a soft accent glow and its name appears in a label underneath.
- Launch: the clicked icon pulses and the grid fades out (the program starts immediately).
  Esc, timeout and clicks outside or in a gap also fade out instead of vanishing.
- Soft drop shadows under icons; panel mode has a shadowed, rounded translucent panel.
- Labels: `hover` (default), `always` (names under every icon, elided to fit), or `never`.
- New appearance settings: `motion` (`full` / `reduced` = fades only / `off`), `animationSpeed`,
  `shadows`, `accentColor`, `panelColor`, `panelOpacity`.
- Faster start-up: on Xfce and KDE the popup skips Qt's desktop theme plugin (~25 ms) and reads
  the icon theme itself. Measured first frame: 29–52 ms (was 47–89 ms).
- Fixed: the Xfce icon theme was never read from xsettings (Xfce writes that file as XML 1.1,
  which Qt's XML reader rejects), so a stale GTK setting could win.

## 0.1.0 — 2026-10-20

First version: the popup foundation.

- Popup grid centred on the pointer, kept on the pointer's monitor without reflowing.
- Icons from the icon theme by name or from files (PNG, SVG, XPM, ...), scaled to fit and
  centred; follows the desktop icon theme; missing programs shown greyed out.
- Left click launches and closes; middle click launches and stays open. Esc, a click outside or a
  click in a gap closes. Closes after a timeout once the pointer leaves.
- The release of the button or key that opened Prowal can't launch anything; the keyboard grab is
  retried while a window-manager shortcut key is still held.
- Floating and panel looks with a compositor; a shaped-window fallback without one.
- JSON config at `~/.config/prowal/prowal.json`: tolerant loading (bad entries skipped with
  warnings, unknown keys kept), atomic saves, 10 rotating backups.
- Named grids (`--grid NAME`).
- apwal import (`--import-apwal [FILE]`, automatic on first run); the apwal file is only read.
- Terminal detection for terminal items: `$TERMINAL`, then Konsole, then the most common others.
- `-h/--help`, `-V/--version`, and `PROWAL_TRACE_STARTUP=1` start-up timing.
