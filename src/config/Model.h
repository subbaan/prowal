#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

#include <utility>
#include <vector>

namespace prowal {

enum class ItemType { Launcher, Group };

// Cells run from -kMaxCell to kMaxCell each way: far more than fits on any
// screen, small enough that a typo can't make a gigantic window.
constexpr int kMaxCell = 99;

inline bool cellInRange(int x, int y)
{
  return x >= -kMaxCell && x <= kMaxCell && y >= -kMaxCell && y <= kMaxCell;
}

// One cell of a Grid. (0, 0) is the cell under the pointer; negative is up/left.
struct Item
{
  int x = 0;
  int y = 0;
  ItemType type = ItemType::Launcher;
  QString name;
  QString command;
  QString workingDir;
  QString icon;       // icon theme name, or a file path
  QString desktopId;  // optional link to the .desktop entry it came from
  bool runInShell = false;
  bool terminal = false;
  QMap<QString, QString> env;
  std::vector<Item> children;  // Group items only: the child Grid
  QJsonObject extra;           // unknown keys, preserved on save
};

struct Grid
{
  QString name;
  std::vector<Item> items;
  QJsonObject extra;
};

struct Behaviour
{
  int timeoutMs = 2000;
  bool stayOpenAfterLaunch = false;
  QString terminal;  // command prefix, e.g. "konsole -e"; empty = detect at launch
  QJsonObject extra;
};

struct Appearance
{
  QString mode = QStringLiteral("floating");  // floating | panel
  int iconSize = 48;
  int spacing = 8;
  QString labels = QStringLiteral("hover");   // hover | always | never
  QString motion = QStringLiteral("full");    // full | reduced | off
  QString groupStyle = QStringLiteral("badge"); // plain | badge | preview
  QString iconTheme;                          // empty = follow the desktop
  double animationSpeed = 1.0;                // >1 faster, <1 slower
  bool shadows = true;
  QString accentColor;                        // hover glow; empty = default
  QString panelColor;                         // panel mode; empty = default
  int panelOpacity = 82;                      // percent
  QJsonObject extra;
};

struct Config
{
  static constexpr int kSchema = 1;

  int schema = kSchema;
  Behaviour behaviour;
  Appearance appearance;
  std::vector<Grid> grids;
  QJsonObject extra;

  const Grid *findGrid(const QString &name) const
  {
    for (const Grid &g : grids)
      if (g.name == name)
        return &g;
    return nullptr;
  }
  Grid *findGrid(const QString &name)
  {
    return const_cast<Grid *>(std::as_const(*this).findGrid(name));
  }
};

inline const QString kDefaultGridName = QStringLiteral("main");

} // namespace prowal
