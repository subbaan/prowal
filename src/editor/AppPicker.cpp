#include "editor/AppPicker.h"

#include "icons/IconLoader.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QColor>
#include <QDrag>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace prowal {

namespace {

constexpr int kIconSize = 32;

bool matches(const DesktopEntry &e, const QStringList &words)
{
  for (const QString &w : words)
  {
    if (e.name.contains(w, Qt::CaseInsensitive) || e.genericName.contains(w, Qt::CaseInsensitive) ||
        e.comment.contains(w, Qt::CaseInsensitive) || e.exec.contains(w, Qt::CaseInsensitive) ||
        e.id.contains(w, Qt::CaseInsensitive))
      continue;
    bool keyword = false;
    for (const QString &k : e.keywords + e.categories)
      keyword = keyword || k.contains(w, Qt::CaseInsensitive);
    if (!keyword)
      return false;
  }
  return true;
}

// Drags show the app's icon instead of Qt's default picture of the whole row.
class AppListView : public QListView
{
public:
  using QListView::QListView;

protected:
  void startDrag(Qt::DropActions actions) override
  {
    const QModelIndexList rows = selectedIndexes();
    if (rows.isEmpty())
      return;
    auto *drag = new QDrag(this);
    drag->setMimeData(model()->mimeData(rows));
    const QIcon icon = rows.first().data(Qt::DecorationRole).value<QIcon>();
    const QPixmap pm = icon.pixmap(QSize(48, 48), devicePixelRatioF());
    drag->setPixmap(pm);
    drag->setHotSpot(QPoint(24, 24));
    drag->exec(actions, Qt::CopyAction);
  }
};

} // namespace

AppListModel::AppListModel(QObject *parent)
  : QAbstractListModel(parent), m_desktop(qEnvironmentVariable("XDG_CURRENT_DESKTOP"))
{
}

void AppListModel::setApps(std::vector<DesktopEntry> apps)
{
  beginResetModel();
  m_apps = std::move(apps);
  m_visible.clear();
  endResetModel();
}

void AppListModel::setFilter(const QString &text, bool includeHidden)
{
  const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  beginResetModel();
  m_visible.clear();
  for (size_t i = 0; i < m_apps.size(); ++i)
    if ((includeHidden || shownInMenus(m_apps[i], m_desktop)) && matches(m_apps[i], words))
      m_visible.push_back(int(i));
  endResetModel();
}

void AppListModel::setExisting(QSet<QString> desktopIds, QSet<QString> commands)
{
  m_existingIds = std::move(desktopIds);
  m_existingCommands = std::move(commands);
  if (!m_visible.empty())
    emit dataChanged(index(0), index(int(m_visible.size()) - 1));
}

const DesktopEntry &AppListModel::entry(const QModelIndex &index) const
{
  return m_apps[size_t(m_visible[size_t(index.row())])];
}

bool AppListModel::inGrid(const DesktopEntry &e) const
{
  return (!e.id.isEmpty() && m_existingIds.contains(e.id)) ||
         m_existingCommands.contains(itemFromDesktopEntry(e).command);
}

int AppListModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : int(m_visible.size());
}

QVariant AppListModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid())
    return {};
  const DesktopEntry &e = entry(index);
  switch (role)
  {
  case Qt::DisplayRole:
  {
    QString second = e.genericName.isEmpty() ? e.comment : e.genericName;
    if (inGrid(e))
      second = tr("✓ already in the grid");
    return second.isEmpty() ? e.name : e.name + QLatin1Char('\n') + second;
  }
  case Qt::DecorationRole:
  {
    auto it = m_icons.find(e.icon);
    if (it == m_icons.end())
      it = m_icons.insert(e.icon, QIcon(loadIcon(itemFromDesktopEntry(e).icon, kIconSize, qApp->devicePixelRatio()).pixmap));
    return it.value();
  }
  case Qt::ToolTipRole:
    return QStringLiteral("%1\n%2").arg(itemFromDesktopEntry(e).command, e.file);
  case Qt::ForegroundRole:
    if (inGrid(e))
      return QColor(128, 128, 128);
    return {};
  default:
    return {};
  }
}

Qt::ItemFlags AppListModel::flags(const QModelIndex &index) const
{
  return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList AppListModel::mimeTypes() const
{
  return {QStringLiteral("text/uri-list")};
}

QMimeData *AppListModel::mimeData(const QModelIndexList &indexes) const
{
  // Dragging an app is dragging its .desktop file, which the canvas accepts
  // the same way as a drop from a file manager.
  QList<QUrl> urls;
  for (const QModelIndex &i : indexes)
    urls << QUrl::fromLocalFile(entry(i).file);
  auto *mime = new QMimeData;
  mime->setUrls(urls);
  return mime;
}

// ---------------------------------------------------------------------------

AppPicker::AppPicker(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Add from installed apps"));
  setModal(false);
  resize(460, 620);

  m_model = new AppListModel(this);
  m_search = new QLineEdit;
  m_search->setPlaceholderText(tr("Search names, descriptions, keywords…"));
  m_search->setClearButtonEnabled(true);
  m_search->installEventFilter(this);
  m_hidden = new QCheckBox(tr("Include apps hidden from menus"));
  m_list = new AppListView;
  m_list->setModel(m_model);
  m_list->setIconSize(QSize(kIconSize, kIconSize));
  m_list->setUniformItemSizes(true);
  m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_list->setDragEnabled(true);
  m_list->setDragDropMode(QAbstractItemView::DragOnly);
  m_list->setSpacing(2);
  m_count = new QLabel;
  auto *hint = new QLabel(tr("Double-click (or Enter) to add next to the selected cell, or drag apps onto the grid."));
  hint->setWordWrap(true);

  auto *buttons = new QDialogButtonBox;
  QPushButton *add = buttons->addButton(tr("&Add"), QDialogButtonBox::AcceptRole);
  QPushButton *close = buttons->addButton(QDialogButtonBox::Close);
  add->setAutoDefault(false);
  close->setAutoDefault(false);  // Enter adds, it never closes the picker

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(m_search);
  layout->addWidget(m_hidden);
  layout->addWidget(m_list, 1);
  layout->addWidget(m_count);
  layout->addWidget(hint);
  layout->addWidget(buttons);

  connect(m_search, &QLineEdit::textChanged, this, &AppPicker::refilter);
  connect(m_hidden, &QCheckBox::toggled, this, &AppPicker::refilter);
  connect(m_list, &QListView::activated, this, &AppPicker::addSelected);
  connect(add, &QPushButton::clicked, this, &AppPicker::addSelected);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
}

void AppPicker::showEvent(QShowEvent *e)
{
  // Read the applications each time the picker opens, so newly installed apps
  // appear (about 15 ms for 400 apps).
  m_model->setApps(listApplications(applicationDirs()));
  refilter();
  m_search->setFocus();
  m_search->selectAll();
  QDialog::showEvent(e);
}

bool AppPicker::eventFilter(QObject *watched, QEvent *e)
{
  // Arrow keys in the search box move through the list; Enter adds.
  if (watched == m_search && e->type() == QEvent::KeyPress)
  {
    auto *key = static_cast<QKeyEvent *>(e);
    if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up || key->key() == Qt::Key_PageDown ||
        key->key() == Qt::Key_PageUp)
    {
      QCoreApplication::sendEvent(m_list, e);
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
    {
      addSelected();
      return true;
    }
  }
  return QDialog::eventFilter(watched, e);
}

void AppPicker::setExisting(const std::vector<Item> &items)
{
  QSet<QString> ids, commands;
  for (const Item &item : items)
  {
    if (!item.desktopId.isEmpty())
      ids.insert(item.desktopId);
    commands.insert(item.command);
  }
  m_model->setExisting(std::move(ids), std::move(commands));
}

void AppPicker::refilter()
{
  m_model->setFilter(m_search->text(), m_hidden->isChecked());
  m_count->setText(tr("%n app(s)", nullptr, m_model->rowCount()));
  if (m_model->rowCount() > 0)
    m_list->setCurrentIndex(m_model->index(0));
}

void AppPicker::addSelected()
{
  std::vector<DesktopEntry> entries;
  QModelIndexList rows = m_list->selectionModel()->selectedIndexes();
  if (rows.isEmpty() && m_list->currentIndex().isValid())
    rows << m_list->currentIndex();
  std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
  for (const QModelIndex &i : rows)
    entries.push_back(m_model->entry(i));
  if (!entries.empty())
    emit addRequested(entries);
}

} // namespace prowal
