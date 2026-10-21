#include "config/GridOps.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace prowal {

int itemIndexAt(const std::vector<Item> &items, QPoint cell)
{
  for (size_t i = 0; i < items.size(); ++i)
    if (items[i].x == cell.x() && items[i].y == cell.y())
      return int(i);
  return -1;
}

int moveOrSwap(std::vector<Item> &items, int index, QPoint cell)
{
  Item &moving = items[size_t(index)];
  const QPoint from(moving.x, moving.y);
  if (from == cell)
    return -1;
  const int other = itemIndexAt(items, cell);
  if (other >= 0)
  {
    items[size_t(other)].x = from.x();
    items[size_t(other)].y = from.y();
  }
  moving.x = cell.x();
  moving.y = cell.y();
  return other;
}

QPoint freeCellNear(const std::vector<Item> &items, QPoint near)
{
  for (int ring = 0; ring < 1000; ++ring)
  {
    // Walk the ring's cells in reading order so results are predictable.
    for (int dy = -ring; dy <= ring; ++dy)
      for (int dx = -ring; dx <= ring; ++dx)
      {
        if (std::max(std::abs(dx), std::abs(dy)) != ring)
          continue;
        const QPoint c = near + QPoint(dx, dy);
        if (itemIndexAt(items, c) < 0)
          return c;
      }
  }
  return near;
}

QPoint nextFillCell(const std::vector<Item> &items, bool keepCentre)
{
  int left = 0, right = 0, top = 0, bottom = 0;
  if (!items.empty())
  {
    left = right = items.front().x;
    top = bottom = items.front().y;
    for (const Item &item : items)
    {
      left = std::min(left, item.x);
      right = std::max(right, item.x);
      top = std::min(top, item.y);
      bottom = std::max(bottom, item.y);
    }
  }
  // Too narrow: the kFillWidth columns holding the items that are closest to centred on the
  // pointer. Adding items inside them keeps the same columns.
  if (right - left + 1 < kFillWidth)
  {
    left = std::min(left, std::max(-(kFillWidth / 2), right - kFillWidth + 1));
    right = left + kFillWidth - 1;
  }

  auto usable = [&](QPoint c) { return itemIndexAt(items, c) < 0 && !(keepCentre && c == QPoint(0, 0)); };
  for (int y = top; y <= bottom; ++y)
    for (int x = left; x <= right; ++x)
      if (usable(QPoint(x, y)))
        return QPoint(x, y);
  // Full: start a row on top (empty, so only the centre cell can be unusable).
  const QPoint start(left, top - 1);
  return usable(start) ? start : start + QPoint(1, 0);
}

std::vector<Item> *groupItems(std::vector<Item> &items, const GroupPath &path)
{
  return const_cast<std::vector<Item> *>(groupItems(std::as_const(items), path));
}

const std::vector<Item> *groupItems(const std::vector<Item> &items, const GroupPath &path)
{
  const std::vector<Item> *level = &items;
  for (const QPoint &cell : path)
  {
    const int index = itemIndexAt(*level, cell);
    if (index < 0 || (*level)[size_t(index)].type != ItemType::Group)
      return nullptr;
    level = &(*level)[size_t(index)].children;
  }
  return level;
}

QStringList groupNames(const std::vector<Item> &items, const GroupPath &path)
{
  QStringList names;
  const std::vector<Item> *level = &items;
  for (const QPoint &cell : path)
  {
    const int index = itemIndexAt(*level, cell);
    if (index < 0 || (*level)[size_t(index)].type != ItemType::Group)
      break;
    names << (*level)[size_t(index)].name;
    level = &(*level)[size_t(index)].children;
  }
  return names;
}

QString formatGroupPath(const GroupPath &path)
{
  QStringList parts;
  for (const QPoint &cell : path)
    parts << QStringLiteral("%1,%2").arg(cell.x()).arg(cell.y());
  return parts.join(QLatin1Char('/'));
}

std::optional<GroupPath> parseGroupPath(const QString &text)
{
  GroupPath path;
  for (const QString &part : text.split(QLatin1Char('/'), Qt::SkipEmptyParts))
  {
    const QStringList xy = part.split(QLatin1Char(','));
    bool okX = false, okY = false;
    if (xy.size() != 2)
      return std::nullopt;
    const int x = xy[0].trimmed().toInt(&okX);
    const int y = xy[1].trimmed().toInt(&okY);
    if (!okX || !okY)
      return std::nullopt;
    path.emplace_back(x, y);
  }
  return path;
}

int countItems(const std::vector<Item> &items)
{
  int n = 0;
  forEachItem(items, [&n](const Item &) { ++n; });
  return n;
}

void forEachItem(const std::vector<Item> &items, const std::function<void(const Item &)> &fn)
{
  for (const Item &item : items)
  {
    fn(item);
    if (item.type == ItemType::Group)
      forEachItem(item.children, fn);
  }
}

QString uniqueGridName(const Config &config, const QString &base)
{
  if (!config.findGrid(base))
    return base;
  for (int n = 2;; ++n)
  {
    const QString name = QStringLiteral("%1 %2").arg(base).arg(n);
    if (!config.findGrid(name))
      return name;
  }
}

} // namespace prowal
