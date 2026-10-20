#pragma once

#include "config/Model.h"

#include <QStringList>

#include <optional>

namespace prowal {

// The [Desktop Entry] group of a freedesktop .desktop file.
struct DesktopEntry
{
  QString id;        // desktop file id, e.g. "org.gnome.Nautilus.desktop"; empty outside applications dirs
  QString file;      // absolute path
  QString type;
  QString name;
  QString genericName;
  QString comment;
  QString icon;
  QString exec;
  QString tryExec;
  QString workingDir;  // the Path key
  bool terminal = false;
  bool noDisplay = false;
  bool hidden = false;
  QStringList categories;
  QStringList keywords;
  QStringList onlyShowIn;
  QStringList notShowIn;
};

// locales: most specific first, e.g. {"en_GB", "en"}. Localised keys (Name[en_GB])
// use the first match, falling back to the plain key.
std::optional<DesktopEntry> parseDesktopEntry(const QByteArray &data, const QStringList &locales);
std::optional<DesktopEntry> readDesktopFile(const QString &path, const QStringList &locales);

// {"en_GB", "en"} for the current locale.
QStringList currentLocales();

// Exec value -> argv, following the spec's quoting rules, with field codes
// removed (%f %u ...), %i/%c/%k expanded, and flatpak's @@ markers dropped.
QStringList execToArgs(const DesktopEntry &entry, QString *error = nullptr);

// Quotes argv into one command line that splitCommand() turns back into argv.
QString joinCommand(const QStringList &args);

// A launcher item for the entry (name, command, icon, folder, terminal, desktopId).
Item itemFromDesktopEntry(const DesktopEntry &entry);

// Whether the entry would appear in the desktop's application menu.
bool shownInMenus(const DesktopEntry &entry, const QString &currentDesktop);

// Every application in the XDG applications directories (first directory wins
// for a given id, so user files override system ones), sorted by name.
// TryExec-not-installed entries are left out; menu-hidden ones are kept, check
// shownInMenus().
std::vector<DesktopEntry> listApplications(const QStringList &applicationDirs);
QStringList applicationDirs();  // $XDG_DATA_HOME/applications, then each of $XDG_DATA_DIRS

// The application with this desktop file id ("org.kde.dolphin.desktop", or
// "kde4-foo.desktop" for kde4/foo.desktop), first directory first.
std::optional<DesktopEntry> findApplication(const QString &id, const QStringList &applicationDirs);

} // namespace prowal
