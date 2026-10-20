#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace prowal {

using ExecutableFinder = std::function<QString(const QString &)>;

// The prefix that runs a command inside the given terminal, e.g. "konsole -e",
// "gnome-terminal --", or just "kitty" for terminals that take the command as-is.
QString terminalPrefixFor(const QString &program);

// Picks a terminal for first-run defaults: $TERMINAL if it names an installed
// program, then Konsole, then the most common other terminals. Returns the
// prefix (see terminalPrefixFor), or an empty string if none is installed.
QString detectTerminal(const ExecutableFinder &findExecutable, const QString &envTerminal);
QString detectTerminal();

// Prefixes of every known terminal that is installed, in detection order.
QStringList installedTerminals(const ExecutableFinder &findExecutable);
QStringList installedTerminals();

} // namespace prowal
