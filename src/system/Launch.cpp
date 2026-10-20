#include "system/Launch.h"

#include "system/CommandLine.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace prowal {

LaunchPlan planLaunch(const Item &item, const QString &terminalPrefix)
{
  LaunchPlan plan;
  if (item.command.trimmed().isEmpty())
  {
    plan.error = QStringLiteral("no command set");
    return plan;
  }

  QStringList argv;
  if (item.runInShell)
    argv = {QStringLiteral("/bin/sh"), QStringLiteral("-c"), item.command};
  else
  {
    const SplitResult split = splitCommand(item.command);
    if (!split.ok)
    {
      plan.error = split.error;
      return plan;
    }
    if (split.args.isEmpty())
    {
      plan.error = QStringLiteral("command is only a comment");
      return plan;
    }
    argv = split.args;
  }

  if (item.terminal)
  {
    const SplitResult term = splitCommand(terminalPrefix);
    if (!term.ok || term.args.isEmpty())
    {
      plan.error = QStringLiteral("no terminal is configured");
      return plan;
    }
    argv = term.args + argv;
  }

  plan.program = argv.takeFirst();
  plan.arguments = argv;
  plan.ok = true;
  return plan;
}

bool launchItem(const Item &item, const QString &terminalPrefix, QString *error)
{
  const LaunchPlan plan = planLaunch(item, terminalPrefix);
  if (!plan.ok)
  {
    *error = plan.error;
    return false;
  }

  QProcess process;
  process.setProgram(plan.program);
  process.setArguments(plan.arguments);
  if (!item.workingDir.isEmpty())
  {
    const QString dir = expandHome(item.workingDir);
    if (!QFileInfo(dir).isDir())
    {
      *error = QStringLiteral("working directory %1 does not exist").arg(dir);
      return false;
    }
    process.setWorkingDirectory(dir);
  }
  if (!item.env.isEmpty())
  {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = item.env.begin(); it != item.env.end(); ++it)
      env.insert(it.key(), it.value());
    process.setProcessEnvironment(env);
  }

  if (!process.startDetached())
  {
    *error = QStringLiteral("%1: %2").arg(plan.program, process.errorString());
    return false;
  }
  return true;
}

bool programAvailable(const Item &item)
{
  if (item.runInShell)
    return true;
  const SplitResult split = splitCommand(item.command);
  if (!split.ok || split.args.isEmpty())
    return false;
  const QString program = split.args.first();
  if (program.contains(QLatin1Char('/')))
    return QFileInfo(program).isExecutable();
  return !QStandardPaths::findExecutable(program).isEmpty();
}

QString expandHome(const QString &path)
{
  if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/")))
    return QDir::homePath() + path.mid(1);
  return path;
}

QString commandProblem(const Item &item, const QString &terminalPrefix)
{
  if (item.type == ItemType::Group)
    return QString();
  if (item.command.trimmed().isEmpty())
    return QStringLiteral("No command set.");
  const LaunchPlan plan = planLaunch(item, terminalPrefix);
  if (!plan.ok)
    return plan.error.left(1).toUpper() + plan.error.mid(1) + QLatin1Char('.');
  if (!programAvailable(item))
  {
    const QString program = splitCommand(item.command).args.value(0);
    return program.contains(QLatin1Char('/'))
             ? QStringLiteral("%1 doesn't exist or isn't executable.").arg(program)
             : QStringLiteral("%1 isn't installed (not found in PATH).").arg(program);
  }
  if (item.terminal && QStandardPaths::findExecutable(plan.program).isEmpty() &&
      !QFileInfo(plan.program).isExecutable())
    return QStringLiteral("The terminal %1 isn't installed.").arg(plan.program);
  return QString();
}

QString workingDirProblem(const Item &item)
{
  if (item.workingDir.isEmpty() || QFileInfo(expandHome(item.workingDir)).isDir())
    return QString();
  return QStringLiteral("This folder doesn't exist.");
}

} // namespace prowal
