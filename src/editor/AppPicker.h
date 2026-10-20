#pragma once

#include "desktop/DesktopEntry.h"

#include <QAbstractListModel>
#include <QDialog>
#include <QHash>
#include <QIcon>
#include <QSet>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListView;

namespace prowal {

// Installed applications, filtered by a search string. Icons load on demand,
// so only visible rows ever touch the icon theme.
class AppListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  explicit AppListModel(QObject *parent = nullptr);

  void setApps(std::vector<DesktopEntry> apps);
  void setFilter(const QString &text, bool includeHidden);
  void setExisting(QSet<QString> desktopIds, QSet<QString> commands);
  const DesktopEntry &entry(const QModelIndex &index) const;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;

private:
  bool inGrid(const DesktopEntry &e) const;

  std::vector<DesktopEntry> m_apps;
  std::vector<int> m_visible;
  QString m_desktop;
  QSet<QString> m_existingIds;
  QSet<QString> m_existingCommands;
  mutable QHash<QString, QIcon> m_icons;
};

class AppPicker : public QDialog
{
  Q_OBJECT

public:
  explicit AppPicker(QWidget *parent = nullptr);

  // Items already in the current grid, marked "already in the grid".
  void setExisting(const std::vector<Item> &items);

signals:
  void addRequested(const std::vector<DesktopEntry> &entries);

protected:
  void showEvent(QShowEvent *e) override;
  bool eventFilter(QObject *watched, QEvent *e) override;

private:
  void refilter();
  void addSelected();

  AppListModel *m_model;
  QLineEdit *m_search;
  QCheckBox *m_hidden;
  QListView *m_list;
  QLabel *m_count;
};

} // namespace prowal
