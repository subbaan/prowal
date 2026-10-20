#include "system/Terminal.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace prowal {

namespace {

struct KnownTerminal
{
  const char *program;
  const char *execArgs;  // what goes between the program and the command
};

// Detection order after $TERMINAL: Konsole first (Prowal is a Qt app), then
// roughly by how common each is on Linux desktops.
constexpr KnownTerminal kKnownTerminals[] = {
  {"konsole", "-e"},
  {"gnome-terminal", "--"},
  {"kgx", "--"},
  {"xfce4-terminal", "-x"},
  {"kitty", ""},
  {"alacritty", "-e"},
  {"wezterm", "start --"},
  {"ghostty", "-e"},
  {"foot", ""},
  {"tilix", "-e"},
  {"terminator", "-x"},
  {"mate-terminal", "-x"},
  {"lxterminal", "-e"},
  {"qterminal", "-e"},
  {"xterm", "-e"},
};

} // namespace

QString terminalPrefixFor(const QString &program)
{
  const QString base = QFileInfo(program).fileName();
  for (const KnownTerminal &t : kKnownTerminals)
  {
    if (base == QLatin1String(t.program))
    {
      const QString args = QLatin1String(t.execArgs);
      return args.isEmpty() ? program : program + QLatin1Char(' ') + args;
    }
  }
  return program + QStringLiteral(" -e");  // the most common convention
}

QString detectTerminal(const ExecutableFinder &findExecutable, const QString &envTerminal)
{
  const QString env = envTerminal.trimmed();
  if (!env.isEmpty() && !env.contains(QLatin1Char(' ')) && !findExecutable(env).isEmpty())
    return terminalPrefixFor(env);

  for (const KnownTerminal &t : kKnownTerminals)
  {
    const QString program = QLatin1String(t.program);
    if (!findExecutable(program).isEmpty())
      return terminalPrefixFor(program);
  }
  return QString();
}

QStringList installedTerminals(const ExecutableFinder &findExecutable)
{
  QStringList found;
  for (const KnownTerminal &t : kKnownTerminals)
    if (!findExecutable(QLatin1String(t.program)).isEmpty())
      found << terminalPrefixFor(QLatin1String(t.program));
  return found;
}

QStringList installedTerminals()
{
  return installedTerminals([](const QString &p) { return QStandardPaths::findExecutable(p); });
}

QString detectTerminal()
{
  return detectTerminal([](const QString &p) { return QStandardPaths::findExecutable(p); },
                        qEnvironmentVariable("TERMINAL"));
}

} // namespace prowal
