#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace prowal {

struct IconFolder
{
  QString path;
  bool recursive = true;
};

// One browsable icon: the best file for a name within a collection.
struct IconFile
{
  QString path;
  QString name;        // file name without extension
  QString collection;  // the collection's directory (absolute path)
  bool themed = false; // collection is an icon theme (has index.theme)
  bool scalable = false;
  bool symbolic = false;
  int size = 0;        // nominal size from the directory name; 0 = unknown
};

// The default folders the icon picker browses.
std::vector<IconFolder> defaultIconFolders();

// A folder splits into collections: the folder itself (its loose images, or the
// whole thing if it is a theme) and, when recursive, each subfolder (a theme if
// it has index.theme, otherwise a plain collection including its subfolders).
// Symlinked directories are never entered, so a link can't lead the scan onto
// other drives.
std::vector<IconFolder> collectionDirs(const IconFolder &folder);

// Icons in one collection, one per name: themes keep the file closest to
// `preferredSize` (scalable counts as a perfect match), plain folders prefer
// PNG/SVG over XPM. Sorted by name. `cancelled` is polled.
std::vector<IconFile> scanIconCollection(const IconFolder &collection, int preferredSize,
                                         const std::function<bool()> &cancelled = {});

// All collections of a folder, concatenated.
std::vector<IconFile> scanIconFolder(const IconFolder &folder, int preferredSize,
                                     const std::function<bool()> &cancelled = {});

// Theme name plus everything it inherits (from index.theme files), ending with hicolor.
QStringList themeChain(const QString &theme, const QStringList &searchPaths);

// Human-readable theme name from index.theme's Name=, or the directory name.
QString themeDisplayName(const QString &themeDir);

struct IconThemeInfo
{
  QString name;     // directory name, as stored in the config
  QString display;  // Name= from index.theme
};

// The icon themes (not cursor-only themes) in `searchPaths`, sorted by display
// name. A theme in an earlier path hides one of the same name in a later path.
std::vector<IconThemeInfo> scanIconThemes(const QStringList &searchPaths);

} // namespace prowal
