#include "config/Config.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace prowal {

namespace {

QJsonObject withoutKeys(QJsonObject o, std::initializer_list<const char *> keys)
{
  for (const char *k : keys)
    o.remove(QLatin1String(k));
  return o;
}

QString cellName(const Item &item)
{
  return QStringLiteral("(%1, %2)").arg(item.x).arg(item.y);
}

bool parseItem(const QJsonValue &value, Item *out, QStringList *warnings, const QString &where);

std::vector<Item> parseItems(const QJsonValue &value, QStringList *warnings, const QString &where)
{
  std::vector<Item> items;
  if (value.isUndefined() || value.isNull())
    return items;
  if (!value.isArray())
  {
    warnings->append(QStringLiteral("%1: \"items\" is not a list; ignored").arg(where));
    return items;
  }
  const QJsonArray array = value.toArray();
  for (qsizetype i = 0; i < array.size(); ++i)
  {
    Item item;
    if (parseItem(array.at(i), &item, warnings, QStringLiteral("%1, item %2").arg(where).arg(i + 1)))
      items.push_back(std::move(item));
  }
  return items;
}

bool parseItem(const QJsonValue &value, Item *out, QStringList *warnings, const QString &where)
{
  if (!value.isObject())
  {
    warnings->append(QStringLiteral("%1: not an object; skipped").arg(where));
    return false;
  }
  const QJsonObject o = value.toObject();

  if (!o.value(QLatin1String("x")).isDouble() || !o.value(QLatin1String("y")).isDouble())
  {
    warnings->append(QStringLiteral("%1: missing or non-numeric x/y; skipped").arg(where));
    return false;
  }
  const double x = o.value(QLatin1String("x")).toDouble();
  const double y = o.value(QLatin1String("y")).toDouble();
  if (x != std::floor(x) || y != std::floor(y) || std::abs(x) > kMaxCell || std::abs(y) > kMaxCell)
  {
    warnings->append(QStringLiteral("%1: cell (%2, %3) is not a whole number from -%4 to %4; skipped")
                       .arg(where).arg(x).arg(y).arg(kMaxCell));
    return false;
  }
  out->x = int(x);
  out->y = int(y);

  const QString type = o.value(QLatin1String("type")).toString(QStringLiteral("launcher"));
  if (type == QLatin1String("launcher"))
    out->type = ItemType::Launcher;
  else if (type == QLatin1String("group"))
    out->type = ItemType::Group;
  else
  {
    warnings->append(QStringLiteral("%1: unknown type \"%2\"; skipped").arg(where, type));
    return false;
  }

  out->name = o.value(QLatin1String("name")).toString();
  out->command = o.value(QLatin1String("command")).toString();
  out->workingDir = o.value(QLatin1String("workingDir")).toString();
  out->icon = o.value(QLatin1String("icon")).toString();
  out->desktopId = o.value(QLatin1String("desktopId")).toString();
  out->runInShell = o.value(QLatin1String("runInShell")).toBool(false);
  out->terminal = o.value(QLatin1String("terminal")).toBool(false);

  const QJsonObject env = o.value(QLatin1String("env")).toObject();
  for (auto it = env.begin(); it != env.end(); ++it)
    out->env.insert(it.key(), it.value().toString());

  if (out->type == ItemType::Group)
    out->children = parseItems(o.value(QLatin1String("items")), warnings,
                               QStringLiteral("%1 group \"%2\"").arg(where, out->name));
  else if (out->command.trimmed().isEmpty())
    warnings->append(QStringLiteral("%1 %2: launcher has no command").arg(where, cellName(*out)));

  out->extra = withoutKeys(o, {"x", "y", "type", "name", "command", "workingDir", "icon",
                               "desktopId", "runInShell", "terminal", "env", "items"});
  return true;
}

QString pickEnum(const QJsonObject &o, const char *key, const QString &fallback,
                 std::initializer_list<const char *> allowed, QStringList *warnings)
{
  const QJsonValue v = o.value(QLatin1String(key));
  if (v.isUndefined())
    return fallback;
  const QString s = v.toString();
  for (const char *a : allowed)
    if (s == QLatin1String(a))
      return s;
  warnings->append(QStringLiteral("appearance.%1: \"%2\" is not valid; using \"%3\"")
                     .arg(QLatin1String(key), s, fallback));
  return fallback;
}

int pickInt(const QJsonObject &o, const char *section, const char *key, int fallback,
            int min, int max, QStringList *warnings)
{
  const QJsonValue v = o.value(QLatin1String(key));
  if (v.isUndefined())
    return fallback;
  if (!v.isDouble())
  {
    warnings->append(QStringLiteral("%1.%2 is not a number; using %3")
                       .arg(QLatin1String(section), QLatin1String(key)).arg(fallback));
    return fallback;
  }
  const int n = v.toInt();
  if (n < min || n > max)
  {
    const int clamped = std::clamp(n, min, max);
    warnings->append(QStringLiteral("%1.%2 = %3 is out of range; using %4")
                       .arg(QLatin1String(section), QLatin1String(key)).arg(n).arg(clamped));
    return clamped;
  }
  return n;
}

double pickDouble(const QJsonObject &o, const char *section, const char *key, double fallback,
                  double min, double max, QStringList *warnings)
{
  const QJsonValue v = o.value(QLatin1String(key));
  if (v.isUndefined())
    return fallback;
  if (!v.isDouble() || v.toDouble() < min || v.toDouble() > max)
  {
    warnings->append(QStringLiteral("%1.%2 must be a number from %3 to %4; using %5")
                       .arg(QLatin1String(section), QLatin1String(key)).arg(min).arg(max).arg(fallback));
    return fallback;
  }
  return v.toDouble();
}

QString pickColor(const QJsonObject &o, const char *key, QStringList *warnings)
{
  const QString s = o.value(QLatin1String(key)).toString();
  if (s.isEmpty() || QColor::isValidColorName(s))
    return s;
  warnings->append(QStringLiteral("appearance.%1: \"%2\" is not a colour; using the default")
                     .arg(QLatin1String(key), s));
  return QString();
}

QJsonArray serializeItems(const std::vector<Item> &items);

QJsonObject serializeItem(const Item &item)
{
  QJsonObject o = item.extra;
  o.insert(QLatin1String("x"), item.x);
  o.insert(QLatin1String("y"), item.y);
  o.insert(QLatin1String("type"),
           item.type == ItemType::Group ? QStringLiteral("group") : QStringLiteral("launcher"));
  o.insert(QLatin1String("name"), item.name);
  if (item.type == ItemType::Launcher)
    o.insert(QLatin1String("command"), item.command);
  o.insert(QLatin1String("icon"), item.icon);
  if (!item.workingDir.isEmpty())
    o.insert(QLatin1String("workingDir"), item.workingDir);
  if (!item.desktopId.isEmpty())
    o.insert(QLatin1String("desktopId"), item.desktopId);
  if (item.runInShell)
    o.insert(QLatin1String("runInShell"), true);
  if (item.terminal)
    o.insert(QLatin1String("terminal"), true);
  if (!item.env.isEmpty())
  {
    QJsonObject env;
    for (auto it = item.env.begin(); it != item.env.end(); ++it)
      env.insert(it.key(), it.value());
    o.insert(QLatin1String("env"), env);
  }
  if (item.type == ItemType::Group)
    o.insert(QLatin1String("items"), serializeItems(item.children));
  return o;
}

QJsonArray serializeItems(const std::vector<Item> &items)
{
  QJsonArray array;
  for (const Item &item : items)
    array.append(serializeItem(item));
  return array;
}

void pruneBackups(const QString &backupDir, int keep)
{
  QDir dir(backupDir);
  QStringList files = dir.entryList({QStringLiteral("prowal-*.json")}, QDir::Files, QDir::Name);
  while (files.size() > keep)
    dir.remove(files.takeFirst());  // names are timestamps, so the first is the oldest
}

} // namespace

QJsonObject itemToJson(const Item &item)
{
  return serializeItem(item);
}

QStringList validateGrid(const std::vector<Item> &items, const QString &where)
{
  QStringList warnings;
  QSet<QPair<int, int>> seen;
  for (const Item &item : items)
  {
    const QPair<int, int> cell(item.x, item.y);
    if (seen.contains(cell))
      warnings.append(QStringLiteral("%1: more than one item at %2; only the first is shown")
                        .arg(where, cellName(item)));
    seen.insert(cell);
    if (item.type == ItemType::Group)
      warnings += validateGrid(item.children, QStringLiteral("%1 group \"%2\"").arg(where, item.name));
  }
  return warnings;
}

LoadResult parseConfig(const QByteArray &json)
{
  LoadResult r;
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
  if (perr.error != QJsonParseError::NoError)
  {
    // Report a line number: offsets are useless to someone fixing the file by hand.
    const int line = int(json.left(perr.offset).count('\n')) + 1;
    r.error = QStringLiteral("not valid JSON at line %1: %2").arg(line).arg(perr.errorString());
    return r;
  }
  if (!doc.isObject())
  {
    r.error = QStringLiteral("top level is not a JSON object");
    return r;
  }
  const QJsonObject root = doc.object();
  Config &c = r.config;

  c.schema = root.value(QLatin1String("schema")).toInt(Config::kSchema);
  if (c.schema > Config::kSchema)
    r.warnings.append(QStringLiteral("config schema %1 is newer than this Prowal understands (%2); "
                                     "reading what it can").arg(c.schema).arg(Config::kSchema));

  const QJsonObject b = root.value(QLatin1String("behaviour")).toObject();
  c.behaviour.timeoutMs = pickInt(b, "behaviour", "timeoutMs", c.behaviour.timeoutMs, 100, 600000, &r.warnings);
  c.behaviour.stayOpenAfterLaunch = b.value(QLatin1String("stayOpenAfterLaunch")).toBool(false);
  c.behaviour.terminal = b.value(QLatin1String("terminal")).toString();
  c.behaviour.extra = withoutKeys(b, {"timeoutMs", "stayOpenAfterLaunch", "terminal"});

  const QJsonObject a = root.value(QLatin1String("appearance")).toObject();
  Appearance &ap = c.appearance;
  ap.mode = pickEnum(a, "mode", ap.mode, {"floating", "panel"}, &r.warnings);
  ap.iconSize = pickInt(a, "appearance", "iconSize", ap.iconSize, 16, 256, &r.warnings);
  ap.spacing = pickInt(a, "appearance", "spacing", ap.spacing, 0, 64, &r.warnings);
  ap.labels = pickEnum(a, "labels", ap.labels, {"hover", "always", "never"}, &r.warnings);
  ap.motion = pickEnum(a, "motion", ap.motion, {"full", "reduced", "off"}, &r.warnings);
  ap.groupStyle = pickEnum(a, "groupStyle", ap.groupStyle, {"plain", "badge", "preview"}, &r.warnings);
  ap.iconTheme = a.value(QLatin1String("iconTheme")).toString();
  ap.animationSpeed = pickDouble(a, "appearance", "animationSpeed", ap.animationSpeed, 0.25, 4.0, &r.warnings);
  ap.shadows = a.value(QLatin1String("shadows")).toBool(ap.shadows);
  ap.accentColor = pickColor(a, "accentColor", &r.warnings);
  ap.panelColor = pickColor(a, "panelColor", &r.warnings);
  ap.panelOpacity = pickInt(a, "appearance", "panelOpacity", ap.panelOpacity, 0, 100, &r.warnings);
  ap.extra = withoutKeys(a, {"mode", "iconSize", "spacing", "labels", "motion", "groupStyle", "iconTheme",
                             "animationSpeed", "shadows", "accentColor", "panelColor", "panelOpacity"});

  const QJsonArray grids = root.value(QLatin1String("grids")).toArray();
  for (qsizetype i = 0; i < grids.size(); ++i)
  {
    const QJsonObject g = grids.at(i).toObject();
    Grid grid;
    grid.name = g.value(QLatin1String("name")).toString();
    if (grid.name.isEmpty())
    {
      r.warnings.append(QStringLiteral("grid %1 has no name; skipped").arg(i + 1));
      continue;
    }
    if (c.findGrid(grid.name))
    {
      r.warnings.append(QStringLiteral("grid \"%1\" appears twice; the second copy is skipped").arg(grid.name));
      continue;
    }
    const QString where = QStringLiteral("grid \"%1\"").arg(grid.name);
    grid.items = parseItems(g.value(QLatin1String("items")), &r.warnings, where);
    r.warnings += validateGrid(grid.items, where);
    grid.extra = withoutKeys(g, {"name", "items"});
    c.grids.push_back(std::move(grid));
  }
  if (c.grids.empty())
    r.warnings.append(QStringLiteral("config has no grids"));

  c.extra = withoutKeys(root, {"schema", "behaviour", "appearance", "grids"});
  r.ok = true;
  return r;
}

LoadResult loadConfig(const QString &path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
  {
    LoadResult r;
    r.error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
    return r;
  }
  LoadResult r = parseConfig(f.readAll());
  if (!r.ok)
    r.error = QStringLiteral("%1: %2").arg(path, r.error);
  return r;
}

QByteArray serializeConfig(const Config &c)
{
  QJsonObject root = c.extra;
  root.insert(QLatin1String("schema"), Config::kSchema);

  QJsonObject b = c.behaviour.extra;
  b.insert(QLatin1String("timeoutMs"), c.behaviour.timeoutMs);
  b.insert(QLatin1String("stayOpenAfterLaunch"), c.behaviour.stayOpenAfterLaunch);
  b.insert(QLatin1String("terminal"), c.behaviour.terminal);
  root.insert(QLatin1String("behaviour"), b);

  const Appearance &ap = c.appearance;
  QJsonObject a = ap.extra;
  a.insert(QLatin1String("mode"), ap.mode);
  a.insert(QLatin1String("iconSize"), ap.iconSize);
  a.insert(QLatin1String("spacing"), ap.spacing);
  a.insert(QLatin1String("labels"), ap.labels);
  a.insert(QLatin1String("motion"), ap.motion);
  a.insert(QLatin1String("groupStyle"), ap.groupStyle);
  a.insert(QLatin1String("iconTheme"), ap.iconTheme);
  a.insert(QLatin1String("animationSpeed"), ap.animationSpeed);
  a.insert(QLatin1String("shadows"), ap.shadows);
  a.insert(QLatin1String("accentColor"), ap.accentColor);
  a.insert(QLatin1String("panelColor"), ap.panelColor);
  a.insert(QLatin1String("panelOpacity"), ap.panelOpacity);
  root.insert(QLatin1String("appearance"), a);

  QJsonArray grids;
  for (const Grid &g : c.grids)
  {
    QJsonObject o = g.extra;
    o.insert(QLatin1String("name"), g.name);
    o.insert(QLatin1String("items"), serializeItems(g.items));
    grids.append(o);
  }
  root.insert(QLatin1String("grids"), grids);

  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool saveConfig(const Config &config, const QString &path, const QString &backupDir,
                QString *error, int keepBackups)
{
  auto fail = [error](const QString &msg) {
    if (error)
      *error = msg;
    return false;
  };

  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return fail(QStringLiteral("cannot create directory for %1").arg(path));

  if (QFileInfo::exists(path))
  {
    if (!QDir().mkpath(backupDir))
      return fail(QStringLiteral("cannot create backup directory %1").arg(backupDir));
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString backup = QDir(backupDir).filePath(QStringLiteral("prowal-%1.json").arg(stamp));
    if (!QFile::copy(path, backup))
      return fail(QStringLiteral("cannot back up %1 to %2; not saving").arg(path, backup));
    pruneBackups(backupDir, keepBackups);
  }

  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly))
    return fail(QStringLiteral("cannot write %1: %2").arg(path, out.errorString()));
  out.write(serializeConfig(config));
  if (!out.commit())
    return fail(QStringLiteral("cannot write %1: %2").arg(path, out.errorString()));
  return true;
}

Config defaultConfig(const QString &terminalPrefix)
{
  Config c;
  c.behaviour.terminal = terminalPrefix;
  Grid main;
  main.name = kDefaultGridName;
  if (!terminalPrefix.isEmpty())
  {
    Item term;
    term.name = QStringLiteral("Terminal");
    term.command = terminalPrefix.section(QLatin1Char(' '), 0, 0);
    term.icon = QStringLiteral("utilities-terminal");
    main.items.push_back(term);
  }
  c.grids.push_back(std::move(main));
  return c;
}

} // namespace prowal
