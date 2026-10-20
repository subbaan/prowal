# CLAUDE.md

Prowal: a Qt 6 / C++20 pop-up launcher for X11. `project.md` is the spec and roadmap (build its
milestones in order). `ARCHITECTURE.md` is the as-built record, including the gotchas: read the
relevant section before changing an area.

## Rules

- **Clean room:** never copy, translate or adapt apwal source, images or text
  (`/home/subbass/bin/claude/apwal`). It may only be consulted for behaviour and for the
  `.apwalrc.xml` format.
- **Version:** `project(Prowal VERSION x.y.z)` in `CMakeLists.txt` is the only place it's set. Bump
  the patch version for every code or build change and add a `CHANGELOG.md` entry.
- **Build after every change:** `make` (Release, owns the `./prowal` symlink), `make test`, and
  `make debug` (`-Werror`) before handing off.
- **User data:** never write the real `~/.config/prowal/` in tests or manual checks, and never
  write `~/.apwalrc.xml`. Use `--config` with a scratch file, and `XDG_CONFIG_HOME=<scratch>` for
  anything that opens the icon picker (it keeps `icon-picker.ini` there).
- **Test configs launch nothing real:** a copy of the user's layout runs their real programs
  (a stray Enter once started Baobab, a disk-usage scanner). Before driving a popup with clicks or
  keys, rewrite every launcher's command to `touch <scratch>/launched-<name>` and check none is
  left (ARCHITECTURE.md §14).
- **Don't take over the user's screen:** interactive checks run on a private Xvfb display (with
  picom for compositing), driven with `xdotool`. See ARCHITECTURE.md §14.
- Keep `prowal_core` free of QtWidgets and xcb so the tests stay headless. All xcb code lives in
  `src/platform/`.
- Style: match the existing code (two-space indent, braces on their own line, `m_` members,
  `prowal` namespace). Status messages go to stderr with `fprintf`, not `qWarning` (Qt sends
  those to journald when stderr isn't a terminal).
- No raw string literals in files moc processes.
