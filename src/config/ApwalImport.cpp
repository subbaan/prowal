#include "config/ApwalImport.h"

#include "system/CommandLine.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QXmlStreamReader>

namespace prowal {

namespace {

struct RawApp
{
  QString cmdline, exec, args, path, icon;
  bool hasCmdline = false;
  int x = 0, y = 0;
  qint64 line = 0;
};

int readInt(QXmlStreamReader &xml, const char *what, int fallback, QStringList *warnings)
{
  const qint64 line = xml.lineNumber();
  const QString text = xml.readElementText().trimmed();
  bool ok = false;
  const int n = text.toInt(&ok);
  if (ok)
    return n;
  warnings->append(QStringLiteral("line %1: %2 \"%3\" is not a number; using %4")
                     .arg(line).arg(QLatin1String(what), text).arg(fallback));
  return fallback;
}

RawApp readApp(QXmlStreamReader &xml, QStringList *warnings)
{
  RawApp a;
  a.line = xml.lineNumber();
  while (xml.readNextStartElement())
  {
    const auto name = xml.name();
    if (name == QLatin1String("cmdline"))
    {
      a.cmdline = xml.readElementText();
      a.hasCmdline = true;
    }
    else if (name == QLatin1String("exec"))   // older files: exec + args
      a.exec = xml.readElementText();
    else if (name == QLatin1String("args"))
      a.args = xml.readElementText();
    else if (name == QLatin1String("path"))
      a.path = xml.readElementText();
    else if (name == QLatin1String("icon"))
      a.icon = xml.readElementText();
    else if (name == QLatin1String("x"))
      a.x = readInt(xml, "x", 0, warnings);
    else if (name == QLatin1String("y"))
      a.y = readInt(xml, "y", 0, warnings);
    else
      xml.skipCurrentElement();
  }
  return a;
}

void readPrefs(QXmlStreamReader &xml, ImportResult *r)
{
  while (xml.readNextStartElement())
  {
    if (xml.name() == QLatin1String("timeout"))
    {
      r->timeoutMs = readInt(xml, "timeout", 2000, &r->warnings);
      r->hasTimeout = true;
    }
    else if (xml.name() == QLatin1String("exit_at_app_launch"))
    {
      r->closeAfterLaunch = readInt(xml, "exit_at_app_launch", 1, &r->warnings) != 0;
      r->hasCloseAfterLaunch = true;
    }
    else
      xml.skipCurrentElement();
  }
}

QString commandOf(const RawApp &a)
{
  if (a.hasCmdline)
    return a.cmdline.trimmed();
  if (a.args.trimmed().isEmpty())
    return a.exec.trimmed();
  if (a.exec.trimmed().isEmpty())
    return a.args.trimmed();
  return a.exec.trimmed() + QLatin1Char(' ') + a.args.trimmed();
}

} // namespace

QString nameFromCommand(const QString &command)
{
  const SplitResult split = splitCommand(command);
  if (!split.ok || split.args.isEmpty())
    return QString();
  QString base = QFileInfo(split.args.first()).fileName();
  base.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
  QStringList words = base.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  for (QString &w : words)
    w[0] = w[0].toUpper();
  return words.join(QLatin1Char(' '));
}

ImportResult parseApwalXml(const QByteArray &data)
{
  ImportResult r;
  QXmlStreamReader xml(data);
  std::vector<RawApp> apps;

  if (!xml.readNextStartElement() || xml.name() != QLatin1String("apwalrc"))
  {
    r.error = xml.hasError() ? QStringLiteral("line %1: %2").arg(xml.lineNumber()).arg(xml.errorString())
                             : QStringLiteral("not an apwal file (no <apwalrc> root)");
    return r;
  }
  while (xml.readNextStartElement())
  {
    if (xml.name() == QLatin1String("apps"))
    {
      while (xml.readNextStartElement())
      {
        if (xml.name() == QLatin1String("app"))
          apps.push_back(readApp(xml, &r.warnings));
        else
          xml.skipCurrentElement();
      }
    }
    else if (xml.name() == QLatin1String("apwal_pref"))
      readPrefs(xml, &r);
    else
      xml.skipCurrentElement();  // icon browser settings etc.: not imported
  }
  if (xml.hasError())
  {
    r.error = QStringLiteral("line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
    return r;
  }

  QSet<QPair<int, int>> used;
  for (const RawApp &a : apps)
  {
    Item item;
    item.command = commandOf(a);
    if (item.command.isEmpty() && a.icon.trimmed().isEmpty())
    {
      ++r.skipped;  // apwal's empty placeholder entries
      continue;
    }
    item.x = a.x;
    item.y = a.y;
    item.workingDir = a.path.trimmed();
    item.name = nameFromCommand(item.command);
    item.icon = a.icon.trimmed();
    if (item.icon.isEmpty())
    {
      const SplitResult split = splitCommand(item.command);
      if (split.ok && !split.args.isEmpty())
        item.icon = QFileInfo(split.args.first()).fileName();  // try the icon theme
    }

    // apwal shows a second item in an occupied cell one step to the right;
    // do the same so the imported layout looks exactly like the old one.
    int moves = 0;
    while (used.contains({item.x, item.y}) && moves < 20)
    {
      ++item.x;
      ++moves;
    }
    if (moves > 0)
      r.warnings.append(QStringLiteral("line %1: \"%2\" shared cell (%3, %4); moved to (%5, %6)")
                          .arg(a.line).arg(item.name).arg(a.x).arg(a.y).arg(item.x).arg(item.y));
    used.insert({item.x, item.y});
    r.items.push_back(std::move(item));
  }
  r.imported = int(r.items.size());
  r.ok = true;
  return r;
}

ImportResult readApwalFile(const QString &path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
  {
    ImportResult r;
    r.error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
    return r;
  }
  ImportResult r = parseApwalXml(f.readAll());
  if (!r.ok)
    r.error = QStringLiteral("%1: %2").arg(path, r.error);
  return r;
}

void applyApwalImport(const ImportResult &import, Config *config, const QString &gridName)
{
  Grid *grid = config->findGrid(gridName);
  if (!grid)
  {
    config->grids.push_back(Grid{gridName, {}, {}});
    grid = &config->grids.back();
  }
  grid->items = import.items;
  if (import.hasTimeout)
    config->behaviour.timeoutMs = std::max(100, import.timeoutMs);
  if (import.hasCloseAfterLaunch)
    config->behaviour.stayOpenAfterLaunch = !import.closeAfterLaunch;
}

} // namespace prowal
