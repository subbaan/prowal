#pragma once

#include "config/GridOps.h"
#include "config/Model.h"

#include <QMainWindow>

#include <optional>

class QComboBox;
class QHBoxLayout;
class QLabel;

namespace prowal {

class AppPicker;
class IconPicker;
class Document;
class GridCanvas;
class PropertiesPanel;

class EditorWindow : public QMainWindow
{
  Q_OBJECT

public:
  EditorWindow(Document *doc, int grid, GroupPath group, std::optional<QPoint> select, QStringList loadWarnings);

  // Shows a place in the layout and brings the window forward (for a second
  // `prowal --edit`, e.g. a right click in the popup while the editor is open).
  void reveal(const QString &gridName, const GroupPath &group, std::optional<QPoint> select);

protected:
  void closeEvent(QCloseEvent *e) override;

private:
  void buildActions();
  void refresh();
  void refreshGridList();
  void refreshCrumbs();
  const std::vector<Item> &level() const;  // the items on the canvas
  int selectedIndex() const;
  void updateActions();
  void selectCell(std::optional<QPoint> cell);

  void addAt(QPoint cell, ItemType type = ItemType::Launcher);
  void addItem();
  void addGroup();
  void openGroup(int index);
  void goToLevel(size_t depth);
  void goUp();
  void moveToLevel(int index, const GroupPath &to);
  void deleteItem(int index);
  void testItem(int index);
  void moveItem(int index, QPoint cell);
  void editItem(int index);
  void showAppPicker();
  void placeItems(std::vector<Item> items, QPoint near, const QString &text);
  void dropFiles(QPoint cell, const QStringList &paths);

  void newGrid();
  void duplicateGrid();
  void renameGrid();
  void deleteGrid();
  void switchGrid(int grid);

  void preferences();
  void preview(const Config &config);
  void applySettings();

  bool save();
  void importApwal();
  void about();
  bool maybeSave();
  void showLoadWarnings();

  Document *m_doc;
  int m_grid;
  GroupPath m_group;  // the Group being edited inside the grid; empty = its top level
  std::optional<QPoint> m_selected;
  QString m_terminal;      // resolved: the configured one, or m_autoTerminal
  QString m_autoTerminal;  // detected once
  QString m_iconTheme;     // the icon theme setting currently applied
  QString m_groupStyle;
  QStringList m_loadWarnings;

  GridCanvas *m_canvas;
  PropertiesPanel *m_panel;
  QComboBox *m_gridBox;
  QWidget *m_crumbBar;
  QHBoxLayout *m_crumbs;
  QLabel *m_problems;
  AppPicker *m_picker = nullptr;
  IconPicker *m_iconPicker = nullptr;
  QAction *m_deleteAction = nullptr;
  QAction *m_testAction = nullptr;
  QAction *m_upAction = nullptr;
  QAction *m_renameGridAction = nullptr;
  QAction *m_deleteGridAction = nullptr;
};

} // namespace prowal
