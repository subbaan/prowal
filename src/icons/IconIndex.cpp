#include "icons/IconIndex.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <cstdlib>

namespace prowal {

namespace {

// QSettings reads an unquoted value containing a comma as a list, so take every
// index.theme value through here (toString() on a list is empty).
QString iniText(const QSettings &ini, const QString &key)
{
  return ini.value(key).toStringList().join(QStringLiteral(", "));
}

const QStringList kImageFilters{QStringLiteral("*.png"), QStringLiteral("*.svg"), QStringLiteral("*.svgz"),
                                QStringLiteral("*.xpm"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                QStringLiteral("*.gif"), QStringLiteral("*.ico"), QStringLiteral("*.webp")};

// Nominal size from a theme path like .../48x48/apps/x.png or .../apps/48/x.svg.
void sizeFromPath(const QString &relativeDir, int *size, bool *scalable)
{
  static const QRegularExpression sizeDir(QStringLiteral("^(\\d+)(?:x\\d+)?(?:@(\\d+)x?)?$"));
  *size = 0;
  *scalable = false;
  for (const QString &part : relativeDir.split(QLatin1Char('/'), Qt::SkipEmptyParts))
  {
    if (part == QLatin1String("scalable"))
      *scalable = true;
    const QRegularExpressionMatch m = sizeDir.match(part);
    if (m.hasMatch())
      *size = m.captured(1).toInt() * (m.captured(2).isEmpty() ? 1 : m.captured(2).toInt());
  }
}

// Lower is better.
int score(const IconFile &f, int preferred)
{
  const QString ext = QFileInfo(f.path).suffix().toLower();
  int s = 0;
  if (f.scalable || ext == QLatin1String("svg") || ext == QLatin1String("svgz"))
    s = 0;
  else if (f.size == 0)
    s = 40;  // unknown size: middling
  else if (f.size >= preferred)
    s = f.size - preferred;          // bigger scales down cleanly
  else
    s = 30 + (preferred - f.size) * 2;  // smaller looks blurry when scaled up
  if (ext == QLatin1String("xpm"))
    s += 50;
  return s;
}

void scanCollection(const QString &root, const QString &collection, bool themed, bool recursive,
                    int preferred, const std::function<bool()> &cancelled, std::vector<IconFile> *out)
{
  QHash<QString, IconFile> best;
  const QDir base(root);
  // No FollowSymlinks: a symlinked directory is not entered.
  QDirIterator it(root, kImageFilters, QDir::Files | QDir::Readable,
                  recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
  int n = 0;
  while (it.hasNext())
  {
    if ((++n & 1023) == 0 && cancelled && cancelled())
      return;
    const QString path = it.next();
    const QFileInfo fi = it.fileInfo();
    IconFile f;
    f.path = path;
    f.name = fi.completeBaseName();
    f.collection = collection;
    f.themed = themed;
    f.symbolic = f.name.endsWith(QLatin1String("-symbolic")) || path.contains(QLatin1String("/symbolic/"));
    if (themed)
      sizeFromPath(base.relativeFilePath(fi.path()), &f.size, &f.scalable);
    auto existing = best.find(f.name);
    if (existing == best.end())
      best.insert(f.name, f);
    else if (score(f, preferred) < score(*existing, preferred))
      *existing = f;
  }
  out->reserve(out->size() + size_t(best.size()));
  for (const IconFile &f : std::as_const(best))
    out->push_back(f);
}

} // namespace

std::vector<IconFolder> defaultIconFolders()
{
  std::vector<IconFolder> folders;
  auto add = [&](const QString &path) {
    if (QFileInfo(path).isDir())
      folders.push_back({path, true});
  };
  add(QDir::home().filePath(QStringLiteral(".icons")));
  add(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("icons")));
  add(QStringLiteral("/usr/share/icons"));
  add(QStringLiteral("/usr/share/pixmaps"));
  return folders;
}

std::vector<IconFolder> collectionDirs(const IconFolder &folder)
{
  std::vector<IconFolder> out;
  const QDir dir(folder.path);
  if (!dir.exists())
    return out;
  if (dir.exists(QStringLiteral("index.theme")))
  {
    out.push_back({folder.path, true});  // the folder is itself a theme
    return out;
  }
  out.push_back({folder.path, false});  // loose images in the folder
  if (!folder.recursive)
    return out;
  const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name);
  for (const QFileInfo &sub : subdirs)
    if (!sub.isSymLink())
      out.push_back({sub.absoluteFilePath(), true});
  return out;
}

std::vector<IconFile> scanIconCollection(const IconFolder &collection, int preferredSize,
                                         const std::function<bool()> &cancelled)
{
  std::vector<IconFile> out;
  const bool themed = QFileInfo::exists(collection.path + QStringLiteral("/index.theme"));
  scanCollection(collection.path, QFileInfo(collection.path).absoluteFilePath(), themed,
                 collection.recursive, preferredSize, cancelled, &out);
  std::sort(out.begin(), out.end(), [](const IconFile &a, const IconFile &b) {
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });
  return out;
}

std::vector<IconFile> scanIconFolder(const IconFolder &folder, int preferredSize,
                                     const std::function<bool()> &cancelled)
{
  std::vector<IconFile> out;
  for (const IconFolder &c : collectionDirs(folder))
  {
    if (cancelled && cancelled())
      break;
    std::vector<IconFile> part = scanIconCollection(c, preferredSize, cancelled);
    out.insert(out.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
  }
  return out;
}

QStringList themeChain(const QString &theme, const QStringList &searchPaths)
{
  QStringList chain;
  QStringList pending{theme};
  while (!pending.isEmpty())
  {
    const QString t = pending.takeFirst();
    if (t.isEmpty() || chain.contains(t))
      continue;
    chain << t;
    for (const QString &base : searchPaths)
    {
      const QString index = QDir(base).filePath(t + QStringLiteral("/index.theme"));
      if (QFileInfo::exists(index))
      {
        const QSettings ini(index, QSettings::IniFormat);
        pending += ini.value(QStringLiteral("Icon Theme/Inherits")).toStringList();
        break;
      }
    }
  }
  if (!chain.contains(QStringLiteral("hicolor")))
    chain << QStringLiteral("hicolor");
  return chain;
}

QString themeDisplayName(const QString &themeDir)
{
  const QString index = QDir(themeDir).filePath(QStringLiteral("index.theme"));
  if (QFileInfo::exists(index))
  {
    const QString name = iniText(QSettings(index, QSettings::IniFormat), QStringLiteral("Icon Theme/Name"));
    if (!name.isEmpty())
      return name;
  }
  return QDir(themeDir).dirName();
}

std::vector<IconThemeInfo> scanIconThemes(const QStringList &searchPaths)
{
  std::vector<IconThemeInfo> themes;
  QSet<QString> seen;
  for (const QString &base : searchPaths)
  {
    const QDir dir(base);
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
      const QString index = dir.filePath(name + QStringLiteral("/index.theme"));
      if (seen.contains(name) || !QFileInfo::exists(index))
        continue;
      const QSettings ini(index, QSettings::IniFormat);
      if (iniText(ini, QStringLiteral("Icon Theme/Directories")).isEmpty() ||
          ini.value(QStringLiteral("Icon Theme/Hidden")).toBool())
        continue;  // cursor themes have no icon directories
      seen.insert(name);
      const QString display = iniText(ini, QStringLiteral("Icon Theme/Name"));
      themes.push_back({name, display.isEmpty() ? name : display});
    }
  }
  std::sort(themes.begin(), themes.end(), [](const IconThemeInfo &a, const IconThemeInfo &b) {
    return a.display.compare(b.display, Qt::CaseInsensitive) < 0;
  });
  return themes;
}

} // namespace prowal
