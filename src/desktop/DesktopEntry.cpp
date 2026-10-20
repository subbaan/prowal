#include "desktop/DesktopEntry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace prowal {

namespace {

// String-type escapes from the spec: \s \n \t \r \\ .
QString unescape(const QString &value)
{
  QString out;
  out.reserve(value.size());
  for (qsizetype i = 0; i < value.size(); ++i)
  {
    const QChar c = value.at(i);
    if (c != QLatin1Char('\\') || i + 1 >= value.size())
    {
      out += c;
      continue;
    }
    const QChar n = value.at(++i);
    if (n == QLatin1Char('s'))
      out += QLatin1Char(' ');
    else if (n == QLatin1Char('n'))
      out += QLatin1Char('\n');
    else if (n == QLatin1Char('t'))
      out += QLatin1Char('\t');
    else if (n == QLatin1Char('r'))
      out += QLatin1Char('\r');
    else if (n == QLatin1Char('\\'))
      out += QLatin1Char('\\');
    else
    {
      out += c;  // not a string escape (e.g. \" in Exec): leave for the Exec parser
      out += n;
    }
  }
  return out;
}

QStringList splitList(const QString &value)
{
  QStringList out;
  QString current;
  for (qsizetype i = 0; i < value.size(); ++i)
  {
    const QChar c = value.at(i);
    if (c == QLatin1Char('\\') && i + 1 < value.size() && value.at(i + 1) == QLatin1Char(';'))
    {
      current += QLatin1Char(';');
      ++i;
    }
    else if (c == QLatin1Char(';'))
    {
      if (!current.trimmed().isEmpty())
        out << unescape(current.trimmed());
      current.clear();
    }
    else
      current += c;
  }
  if (!current.trimmed().isEmpty())
    out << unescape(current.trimmed());
  return out;
}

bool isExecutable(const QString &program)
{
  if (program.contains(QLatin1Char('/')))
    return QFileInfo(program).isExecutable();
  return !QStandardPaths::findExecutable(program).isEmpty();
}

QString iconName(const QString &icon)
{
  // Theme names are given without extension, but some files include one anyway.
  if (icon.startsWith(QLatin1Char('/')))
    return icon;
  static const QRegularExpression ext(QStringLiteral("\\.(png|svg|svgz|xpm)$"),
                                      QRegularExpression::CaseInsensitiveOption);
  QString name = icon;
  return name.remove(ext);
}

} // namespace

std::optional<DesktopEntry> parseDesktopEntry(const QByteArray &data, const QStringList &locales)
{
  QHash<QString, QString> plain;
  QHash<QString, QHash<QString, QString>> localised;
  bool inGroup = false;
  bool sawGroup = false;

  const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
  for (QString line : lines)
  {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
      continue;
    if (line.startsWith(QLatin1Char('[')))
    {
      inGroup = line == QLatin1String("[Desktop Entry]");
      sawGroup = sawGroup || inGroup;
      continue;
    }
    if (!inGroup)
      continue;
    const qsizetype eq = line.indexOf(QLatin1Char('='));
    if (eq <= 0)
      continue;
    QString key = line.left(eq).trimmed();
    const QString value = line.mid(eq + 1).trimmed();
    const qsizetype bracket = key.indexOf(QLatin1Char('['));
    if (bracket > 0 && key.endsWith(QLatin1Char(']')))
      localised[key.left(bracket)].insert(key.mid(bracket + 1, key.size() - bracket - 2), value);
    else if (!plain.contains(key))
      plain.insert(key, value);
  }
  if (!sawGroup)
    return std::nullopt;

  auto get = [&](const char *key) {
    const QString k = QLatin1String(key);
    const auto loc = localised.constFind(k);
    if (loc != localised.constEnd())
      for (const QString &l : locales)
        if (loc->contains(l))
          return unescape(loc->value(l));
    return unescape(plain.value(k));
  };
  auto raw = [&](const char *key) { return plain.value(QLatin1String(key)); };
  auto flag = [&](const char *key) { return raw(key).trimmed() == QLatin1String("true"); };

  DesktopEntry e;
  e.type = raw("Type");
  e.name = get("Name");
  e.genericName = get("GenericName");
  e.comment = get("Comment");
  e.icon = unescape(raw("Icon"));
  e.exec = unescape(raw("Exec"));
  e.tryExec = unescape(raw("TryExec"));
  e.workingDir = unescape(raw("Path"));
  e.terminal = flag("Terminal");
  e.noDisplay = flag("NoDisplay");
  e.hidden = flag("Hidden");
  e.categories = splitList(raw("Categories"));
  e.onlyShowIn = splitList(raw("OnlyShowIn"));
  e.notShowIn = splitList(raw("NotShowIn"));
  // Keywords are localised lists.
  const auto kw = localised.constFind(QStringLiteral("Keywords"));
  QString keywords = plain.value(QStringLiteral("Keywords"));
  if (kw != localised.constEnd())
    for (const QString &l : locales)
      if (kw->contains(l))
      {
        keywords = kw->value(l);
        break;
      }
  e.keywords = splitList(keywords);
  return e;
}

std::optional<DesktopEntry> readDesktopFile(const QString &path, const QStringList &locales)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return std::nullopt;
  auto entry = parseDesktopEntry(f.readAll(), locales);
  if (entry)
    entry->file = QFileInfo(path).absoluteFilePath();
  return entry;
}

QStringList currentLocales()
{
  const QString name = QLocale::system().name();  // "en_GB"
  QStringList out{name};
  const qsizetype us = name.indexOf(QLatin1Char('_'));
  if (us > 0)
    out << name.left(us);
  return out;
}

QStringList execToArgs(const DesktopEntry &entry, QString *error)
{
  // Tokenise: whitespace separates; "..." quotes, where \" \` \$ \\ are escapes.
  QStringList tokens;
  QString current;
  bool inWord = false;
  bool quoted = false;
  const QString &exec = entry.exec;
  for (qsizetype i = 0; i < exec.size(); ++i)
  {
    const QChar c = exec.at(i);
    if (quoted)
    {
      if (c == QLatin1Char('"'))
        quoted = false;
      else if (c == QLatin1Char('\\') && i + 1 < exec.size())
        current += exec.at(++i);
      else
        current += c;
    }
    else if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
    {
      if (inWord)
        tokens << current;
      current.clear();
      inWord = false;
    }
    else if (c == QLatin1Char('"'))
    {
      quoted = true;
      inWord = true;
    }
    else if (c == QLatin1Char('\\') && i + 1 < exec.size())
    {
      current += exec.at(++i);  // lenient: not allowed by the spec, but seen in the wild
      inWord = true;
    }
    else
    {
      current += c;
      inWord = true;
    }
  }
  if (quoted)
  {
    if (error)
      *error = QStringLiteral("unterminated quote in Exec");
    return {};
  }
  if (inWord)
    tokens << current;

  static const QSet<QString> dropped{
    QStringLiteral("%f"), QStringLiteral("%F"), QStringLiteral("%u"), QStringLiteral("%U"),
    QStringLiteral("%d"), QStringLiteral("%D"), QStringLiteral("%n"), QStringLiteral("%N"),
    QStringLiteral("%v"), QStringLiteral("%m"), QStringLiteral("@@"),  QStringLiteral("@@u"),
    QStringLiteral("@@f")};
  QStringList args;
  for (const QString &t : tokens)
  {
    if (dropped.contains(t))
      continue;
    if (t == QLatin1String("%i"))
    {
      if (!entry.icon.isEmpty())
        args << QStringLiteral("--icon") << entry.icon;
      continue;
    }
    if (t == QLatin1String("%c"))
    {
      args << entry.name;
      continue;
    }
    if (t == QLatin1String("%k"))
    {
      args << entry.file;
      continue;
    }
    // Codes embedded in a longer argument (deprecated): drop the code, keep %%.
    QString arg;
    for (qsizetype i = 0; i < t.size(); ++i)
    {
      if (t.at(i) == QLatin1Char('%') && i + 1 < t.size())
      {
        if (t.at(i + 1) == QLatin1Char('%'))
          arg += QLatin1Char('%');
        ++i;
      }
      else
        arg += t.at(i);
    }
    args << arg;
  }
  if (args.isEmpty() && error)
    *error = QStringLiteral("no command in Exec");
  return args;
}

QString joinCommand(const QStringList &args)
{
  static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_@%+=:,./-]+$"));
  QStringList out;
  for (const QString &a : args)
  {
    if (safe.match(a).hasMatch())
      out << a;
    else
      out << QLatin1Char('\'') + QString(a).replace(QLatin1String("'"), QLatin1String("'\\''")) + QLatin1Char('\'');
  }
  return out.join(QLatin1Char(' '));
}

Item itemFromDesktopEntry(const DesktopEntry &entry)
{
  Item item;
  item.name = entry.name;
  item.command = joinCommand(execToArgs(entry));
  item.icon = iconName(entry.icon);
  item.workingDir = entry.workingDir;
  item.terminal = entry.terminal;
  item.desktopId = entry.id;
  return item;
}

bool shownInMenus(const DesktopEntry &entry, const QString &currentDesktop)
{
  if (entry.hidden || entry.noDisplay || entry.type != QLatin1String("Application"))
    return false;
  const QStringList desktops = currentDesktop.split(QLatin1Char(':'), Qt::SkipEmptyParts);
  auto any = [&](const QStringList &list) {
    for (const QString &d : desktops)
      if (list.contains(d, Qt::CaseInsensitive))
        return true;
    return false;
  };
  if (!entry.onlyShowIn.isEmpty() && !any(entry.onlyShowIn))
    return false;
  return !any(entry.notShowIn);
}

QStringList applicationDirs()
{
  QStringList dirs;
  for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation))
    if (!dirs.contains(d) && QFileInfo(d).isDir())
      dirs << d;
  return dirs;
}

std::optional<DesktopEntry> findApplication(const QString &id, const QStringList &dirs)
{
  if (id.isEmpty() || id.contains(QLatin1Char('/')) || !id.endsWith(QLatin1String(".desktop")))
    return std::nullopt;
  // An id's dashes may stand for subdirectories: try "a-b-c", "a/b-c", "a/b/c".
  QStringList candidates{id};
  QString path = id;
  for (qsizetype dash = path.indexOf(QLatin1Char('-')); dash >= 0; dash = path.indexOf(QLatin1Char('-'), dash + 1))
  {
    path[dash] = QLatin1Char('/');
    candidates << path;
  }
  const QStringList locales = currentLocales();
  for (const QString &dir : dirs)
    for (const QString &relative : candidates)
    {
      const QString file = QDir(dir).filePath(relative);
      if (!QFileInfo(file).isFile())
        continue;
      auto entry = readDesktopFile(file, locales);
      if (entry)
      {
        entry->id = id;
        return entry;
      }
    }
  return std::nullopt;
}

std::vector<DesktopEntry> listApplications(const QStringList &dirs)
{
  const QStringList locales = currentLocales();
  QSet<QString> seen;
  std::vector<DesktopEntry> apps;
  for (const QString &dir : dirs)
  {
    const QDir base(dir);
    QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
      const QString path = it.next();
      const QString id = base.relativeFilePath(path).replace(QLatin1Char('/'), QLatin1Char('-'));
      if (seen.contains(id))
        continue;  // an earlier directory (e.g. the user's) overrides this one
      seen.insert(id);
      auto entry = readDesktopFile(path, locales);
      if (!entry || entry->hidden || entry->type != QLatin1String("Application") || entry->name.isEmpty())
        continue;
      if (!entry->tryExec.isEmpty() && !isExecutable(entry->tryExec))
        continue;  // not installed
      entry->id = id;
      apps.push_back(std::move(*entry));
    }
  }
  std::sort(apps.begin(), apps.end(), [](const DesktopEntry &a, const DesktopEntry &b) {
    return QString::localeAwareCompare(a.name, b.name) < 0;
  });
  return apps;
}

} // namespace prowal
