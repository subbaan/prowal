#pragma once

#include "config/Model.h"

#include <QPoint>

#include <QStringList>

#include <functional>
#include <optional>

namespace prowal {

// Index of the first item in the given cell, or -1.
int itemIndexAt(const std::vector<Item> &items, QPoint cell);

// Moves items[index] to cell. If another item is there, the two swap places.
// Returns the index of the swapped item, or -1.
int moveOrSwap(std::vector<Item> &items, int index, QPoint cell);

// The free cell closest to `near` (searching outward ring by ring).
QPoint freeCellNear(const std::vector<Item> &items, QPoint near);

// The cells of the Groups leading from a Grid's top level down to a child grid;
// empty means the top level. Cells rather than indices, so a path stays valid
// while items are added or removed around it.
using GroupPath = std::vector<QPoint>;

// The items of the group at `path`, or nullptr if the path no longer leads to a Group.
std::vector<Item> *groupItems(std::vector<Item> &items, const GroupPath &path);
const std::vector<Item> *groupItems(const std::vector<Item> &items, const GroupPath &path);

// The names of the Groups along `path` (same length as the path, as far as it is valid).
QStringList groupNames(const std::vector<Item> &items, const GroupPath &path);

// "2,0/1,-1" <-> {(2,0), (1,-1)}; empty string <-> empty path.
QString formatGroupPath(const GroupPath &path);
std::optional<GroupPath> parseGroupPath(const QString &text);

// The item count of a grid including everything inside its Groups.
int countItems(const std::vector<Item> &items);

// Every item, depth first, Groups included.
void forEachItem(const std::vector<Item> &items, const std::function<void(const Item &)> &fn);

// A name not already used by a grid: base, base 2, base 3, ...
QString uniqueGridName(const Config &config, const QString &base);

} // namespace prowal
