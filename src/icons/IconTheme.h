#pragma once

#include "icons/IconIndex.h"

#include <QString>

#include <vector>

namespace prowal {

// Makes QIcon::fromTheme() follow the desktop's icon theme. Qt only knows it
// when a platform theme plugin tells it, so fall back to reading the desktop's
// own settings (Xfce xsettings, GTK settings.ini, kdeglobals).
// Returns a short description of where the theme came from, for tracing.
QString setupIconTheme(const QString &overrideTheme);

// The desktop's icon theme (what an empty override follows), ignoring any
// override applied since start-up. Empty if none can be found.
QString desktopIconTheme();

// Installed icon themes (not cursor-only themes), sorted by display name.
std::vector<IconThemeInfo> installedIconThemes();

// True on desktops whose icon theme setupIconTheme() can read from their own
// settings files (Xfce, KDE). Usable before the QApplication exists.
bool canFindIconThemeWithoutPlatformTheme();

} // namespace prowal
