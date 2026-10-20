#pragma once

#include "config/GridOps.h"
#include "config/Model.h"

#include <QDateTime>
#include <QObject>
#include <QPoint>
#include <QUndoStack>

#include <optional>

#include <functional>

namespace prowal {

// The config being edited. Every change goes through here as one undo step;
// steps store before/after snapshots of the whole config, which is small.
class Document : public QObject
{
  Q_OBJECT

public:
  Document(Config config, QString path, QObject *parent = nullptr);

  const Config &config() const { return m_config; }
  const QString &path() const { return m_path; }
  QUndoStack *undoStack() { return &m_undo; }

  bool isModified() const { return !m_undo.isClean(); }
  bool changedOnDisk() const;  // someone else wrote the file since we loaded/saved it
  bool save(QString *error);

  // Item edits work on one level: a grid's top level (empty group path) or the
  // items inside one of its Groups. A path that no longer exists is a no-op.
  const std::vector<Item> *items(int grid, const GroupPath &group) const;
  void setItems(int grid, const GroupPath &group, std::vector<Item> items, const QString &text);
  void addItem(int grid, const GroupPath &group, Item item);
  void deleteItem(int grid, const GroupPath &group, int index);
  void moveItem(int grid, const GroupPath &group, int index, QPoint cell);  // swaps with an item already there
  void editItem(int grid, const GroupPath &group, int index, const Item &after);
  // Moves an item to another level of the same grid: its own cell there if free,
  // otherwise the nearest free one. Returns the cell it landed in.
  std::optional<QPoint> moveToLevel(int grid, const GroupPath &from, int index, const GroupPath &to);

  // Preferences: one undo step, nothing if unchanged.
  void setSettings(const Behaviour &behaviour, const Appearance &appearance);

  int addGrid(const QString &name);
  int duplicateGrid(int grid, const QString &name);
  void renameGrid(int grid, const QString &name);
  void deleteGrid(int grid);

signals:
  void changed();

private:
  friend class SnapshotCommand;
  void push(std::vector<Grid> after, const QString &text);
  void push(Config after, const QString &text);
  // Applies `change` to a copy of one level; it returns the undo text, or empty for no change.
  void changeLevel(int grid, const GroupPath &group, const std::function<QString(std::vector<Item> &)> &change);
  void restore(const Config &config);

  Config m_config;
  QString m_path;
  QDateTime m_diskTime;
  QUndoStack m_undo;
};

} // namespace prowal
