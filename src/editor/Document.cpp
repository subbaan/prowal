#include "editor/Document.h"

#include "config/Config.h"
#include "config/GridOps.h"
#include "config/Paths.h"

#include <QFileInfo>
#include <QUndoCommand>

#include <algorithm>

namespace prowal {

class SnapshotCommand : public QUndoCommand
{
public:
  SnapshotCommand(Document *doc, Config before, Config after, const QString &text)
    : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after))
  {}
  void redo() override { m_doc->restore(m_after); }
  void undo() override { m_doc->restore(m_before); }

private:
  Document *m_doc;
  Config m_before;
  Config m_after;
};

namespace {

QDateTime modifiedTime(const QString &path)
{
  const QFileInfo fi(path);
  return fi.exists() ? fi.lastModified() : QDateTime();
}

} // namespace

Document::Document(Config config, QString path, QObject *parent)
  : QObject(parent), m_config(std::move(config)), m_path(std::move(path)), m_diskTime(modifiedTime(m_path))
{
}

bool Document::changedOnDisk() const
{
  return modifiedTime(m_path) != m_diskTime;
}

bool Document::save(QString *error)
{
  if (!saveConfig(m_config, m_path, paths::backupDirFor(m_path), error))
    return false;
  m_diskTime = modifiedTime(m_path);
  m_undo.setClean();
  return true;
}

void Document::push(std::vector<Grid> after, const QString &text)
{
  Config config = m_config;
  config.grids = std::move(after);
  push(std::move(config), text);
}

void Document::push(Config after, const QString &text)
{
  m_undo.push(new SnapshotCommand(this, m_config, std::move(after), text));
}

void Document::restore(const Config &config)
{
  m_config = config;
  emit changed();
}

void Document::setSettings(const Behaviour &behaviour, const Appearance &appearance)
{
  Config after = m_config;
  after.behaviour = behaviour;
  after.appearance = appearance;
  if (serializeConfig(after) == serializeConfig(m_config))
    return;
  push(std::move(after), QStringLiteral("Change preferences"));
}

const std::vector<Item> *Document::items(int grid, const GroupPath &group) const
{
  if (grid < 0 || size_t(grid) >= m_config.grids.size())
    return nullptr;
  return groupItems(m_config.grids[size_t(grid)].items, group);
}

void Document::changeLevel(int grid, const GroupPath &group,
                           const std::function<QString(std::vector<Item> &)> &change)
{
  if (!items(grid, group))
    return;
  std::vector<Grid> after = m_config.grids;
  const QString text = change(*groupItems(after[size_t(grid)].items, group));
  if (!text.isEmpty())
    push(std::move(after), text);
}

void Document::setItems(int grid, const GroupPath &group, std::vector<Item> items, const QString &text)
{
  changeLevel(grid, group, [&](std::vector<Item> &level) {
    level = std::move(items);
    return text;
  });
}

void Document::addItem(int grid, const GroupPath &group, Item item)
{
  changeLevel(grid, group, [&](std::vector<Item> &level) {
    if (!cellInRange(item.x, item.y))
      return QString();
    const bool isGroup = item.type == ItemType::Group;
    level.push_back(std::move(item));
    return isGroup ? QStringLiteral("Add group") : QStringLiteral("Add item");
  });
}

void Document::deleteItem(int grid, const GroupPath &group, int index)
{
  changeLevel(grid, group, [&](std::vector<Item> &level) {
    const QString name = level[size_t(index)].name;
    level.erase(level.begin() + index);
    return QStringLiteral("Delete %1").arg(name);
  });
}

void Document::moveItem(int grid, const GroupPath &group, int index, QPoint cell)
{
  changeLevel(grid, group, [&](std::vector<Item> &level) {
    const QString name = level[size_t(index)].name;
    if (QPoint(level[size_t(index)].x, level[size_t(index)].y) == cell || !cellInRange(cell.x(), cell.y()))
      return QString();
    const int swapped = moveOrSwap(level, index, cell);
    return swapped >= 0 ? QStringLiteral("Swap %1 and %2").arg(name, level[size_t(swapped)].name)
                        : QStringLiteral("Move %1").arg(name);
  });
}

void Document::editItem(int grid, const GroupPath &group, int index, const Item &item)
{
  changeLevel(grid, group, [&](std::vector<Item> &level) {
    if (itemToJson(level[size_t(index)]) == itemToJson(item))
      return QString();
    level[size_t(index)] = item;
    return QStringLiteral("Edit %1").arg(item.name);
  });
}

std::optional<QPoint> Document::moveToLevel(int grid, const GroupPath &from, int index, const GroupPath &to)
{
  const std::vector<Item> *source = items(grid, from);
  if (!source || index < 0 || size_t(index) >= source->size() || from == to)
    return std::nullopt;
  // A Group can't go inside itself.
  const Item &moving = (*source)[size_t(index)];
  GroupPath own = from;
  own.emplace_back(moving.x, moving.y);
  if (to.size() >= own.size() && std::equal(own.begin(), own.end(), to.begin()))
    return std::nullopt;

  std::vector<Grid> after = m_config.grids;
  std::vector<Item> &src = *groupItems(after[size_t(grid)].items, from);
  Item item = src[size_t(index)];
  src.erase(src.begin() + index);
  // Paths are cells, so removing the item doesn't move the target.
  std::vector<Item> *dst = groupItems(after[size_t(grid)].items, to);
  if (!dst)
    return std::nullopt;
  const QPoint cell = freeCellNear(*dst, QPoint(item.x, item.y));
  item.x = cell.x();
  item.y = cell.y();
  const QString name = item.name;
  dst->push_back(std::move(item));
  const QStringList names = groupNames(after[size_t(grid)].items, to);
  push(std::move(after), names.isEmpty() ? QStringLiteral("Move %1 to the top level").arg(name)
                                         : QStringLiteral("Move %1 into %2").arg(name, names.last()));
  return cell;
}

int Document::addGrid(const QString &name)
{
  std::vector<Grid> after = m_config.grids;
  after.push_back(Grid{name, {}, {}});
  push(std::move(after), QStringLiteral("Add grid %1").arg(name));
  return int(m_config.grids.size()) - 1;
}

int Document::duplicateGrid(int grid, const QString &name)
{
  std::vector<Grid> after = m_config.grids;
  Grid copy = after[size_t(grid)];
  copy.name = name;
  after.push_back(std::move(copy));
  push(std::move(after), QStringLiteral("Duplicate grid as %1").arg(name));
  return int(m_config.grids.size()) - 1;
}

void Document::renameGrid(int grid, const QString &name)
{
  std::vector<Grid> after = m_config.grids;
  const QString old = after[size_t(grid)].name;
  after[size_t(grid)].name = name;
  push(std::move(after), QStringLiteral("Rename grid %1 to %2").arg(old, name));
}

void Document::deleteGrid(int grid)
{
  std::vector<Grid> after = m_config.grids;
  const QString name = after[size_t(grid)].name;
  after.erase(after.begin() + grid);
  push(std::move(after), QStringLiteral("Delete grid %1").arg(name));
}

} // namespace prowal
