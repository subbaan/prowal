#include "editor/EditorWindow.h"

#include "config/ApwalImport.h"
#include "config/GridOps.h"
#include "config/Paths.h"
#include "desktop/DesktopEntry.h"
#include "editor/AppPicker.h"
#include "editor/Document.h"
#include "editor/GridCanvas.h"
#include "editor/IconPicker.h"
#include "editor/PreferencesDialog.h"
#include "editor/PropertiesPanel.h"
#include "icons/IconLoader.h"
#include "icons/IconTheme.h"
#include "platform/X11.h"
#include "popup/PopupWindow.h"
#include "system/Launch.h"
#include "system/Terminal.h"
#include "version.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace prowal {

namespace {

const QString kMainGrid = QStringLiteral("main");

QIcon themed(const char *name)
{
  return QIcon::fromTheme(QLatin1String(name));
}

std::vector<Item> flattened(const std::vector<Item> &items)
{
  std::vector<Item> all;
  forEachItem(items, [&all](const Item &item) { all.push_back(item); });
  return all;
}

} // namespace

EditorWindow::EditorWindow(Document *doc, int grid, GroupPath group, std::optional<QPoint> select,
                           QStringList loadWarnings)
  : m_doc(doc)
  , m_grid(grid)
  , m_group(std::move(group))
  , m_selected(select)
  , m_autoTerminal(detectTerminal())
  , m_iconTheme(doc->config().appearance.iconTheme)  // main() applied it already
  , m_loadWarnings(std::move(loadWarnings))
{
  m_doc->setParent(this);
  setWindowTitle(QStringLiteral("[*]%1 — Prowal Editor %2")
                   .arg(QFileInfo(m_doc->path()).fileName(), QStringLiteral(PROWAL_VERSION)));
  setWindowIcon(themed("applications-other"));

  m_canvas = new GridCanvas;
  m_panel = new PropertiesPanel;
  m_crumbBar = new QWidget;
  m_crumbs = new QHBoxLayout(m_crumbBar);
  m_crumbs->setContentsMargins(6, 3, 6, 3);
  m_crumbs->setSpacing(2);
  auto *canvasBox = new QWidget;
  auto *canvasLayout = new QVBoxLayout(canvasBox);
  canvasLayout->setContentsMargins(0, 0, 0, 0);
  canvasLayout->setSpacing(0);
  canvasLayout->addWidget(m_crumbBar);
  canvasLayout->addWidget(m_canvas, 1);
  auto *scroll = new QScrollArea;
  scroll->setWidget(m_panel);
  scroll->setWidgetResizable(true);
  scroll->setMinimumWidth(320);
  auto *split = new QSplitter;
  split->addWidget(canvasBox);
  split->addWidget(scroll);
  split->setStretchFactor(0, 1);
  setCentralWidget(split);

  m_gridBox = new QComboBox;
  m_gridBox->setToolTip(tr("Grid shown by: prowal --grid NAME (\"main\" is the default)"));
  m_problems = new QLabel;
  statusBar()->addPermanentWidget(m_problems);
  statusBar()->addPermanentWidget(new QLabel(QStringLiteral("Prowal %1").arg(QStringLiteral(PROWAL_VERSION))));

  buildActions();

  connect(m_doc, &Document::changed, this, &EditorWindow::refresh);
  connect(m_doc->undoStack(), &QUndoStack::cleanChanged, this, [this](bool clean) { setWindowModified(!clean); });
  connect(m_gridBox, &QComboBox::activated, this, &EditorWindow::switchGrid);

  connect(m_canvas, &GridCanvas::cellSelected, this, &EditorWindow::selectCell);
  connect(m_canvas, &GridCanvas::moveRequested, this, &EditorWindow::moveItem);
  connect(m_canvas, &GridCanvas::addRequested, this, [this](QPoint cell) { addAt(cell); });
  connect(m_canvas, &GridCanvas::editRequested, this, &EditorWindow::editItem);
  connect(m_canvas, &GridCanvas::deleteRequested, this, &EditorWindow::deleteItem);
  connect(m_canvas, &GridCanvas::testRequested, this, &EditorWindow::testItem);
  connect(m_canvas, &GridCanvas::openRequested, this, &EditorWindow::openGroup);
  connect(m_canvas, &GridCanvas::addGroupRequested, this, [this](QPoint cell) { addAt(cell, ItemType::Group); });
  connect(m_canvas, &GridCanvas::moveIntoRequested, this, [this](int index, QPoint group) {
    GroupPath to = m_group;
    to.push_back(group);
    moveToLevel(index, to);
  });
  connect(m_canvas, &GridCanvas::moveUpRequested, this, [this](int index) {
    GroupPath to = m_group;
    to.pop_back();
    moveToLevel(index, to);
  });

  connect(m_panel, &PropertiesPanel::itemEdited, this, [this](const Item &item) {
    const int index = selectedIndex();
    if (index >= 0)
      m_doc->editItem(m_grid, m_group, index, item);
  });
  connect(m_panel, &PropertiesPanel::testRequested, this, [this] { testItem(selectedIndex()); });
  connect(m_panel, &PropertiesPanel::addRequested, this, &EditorWindow::addItem);
  connect(m_panel, &PropertiesPanel::addGroupRequested, this, &EditorWindow::addGroup);
  connect(m_panel, &PropertiesPanel::openGroupRequested, this, [this] { openGroup(selectedIndex()); });
  connect(m_panel, &PropertiesPanel::appsRequested, this, &EditorWindow::showAppPicker);
  // One picker for the whole session, so its icon index is built only once.
  m_panel->iconChooser = [this](const QString &current) {
    if (!m_iconPicker)
      m_iconPicker = new IconPicker(this);
    return m_iconPicker->choose(current);
  };
  connect(m_canvas, &GridCanvas::appsRequested, this, [this](QPoint cell) {
    selectCell(cell);
    showAppPicker();
  });
  connect(m_canvas, &GridCanvas::filesDropped, this, &EditorWindow::dropFiles);

  resize(1240, 720);
  refresh();
  if (m_selected)
    m_canvas->setSelectedCell(m_selected);
  if (!m_loadWarnings.isEmpty())
    QTimer::singleShot(0, this, &EditorWindow::showLoadWarnings);
}

void EditorWindow::buildActions()
{
  QUndoStack *undo = m_doc->undoStack();

  auto *save = new QAction(themed("document-save"), tr("&Save"), this);
  save->setShortcut(QKeySequence::Save);
  connect(save, &QAction::triggered, this, &EditorWindow::save);
  auto *import = new QAction(tr("&Import from apwal…"), this);
  connect(import, &QAction::triggered, this, &EditorWindow::importApwal);
  auto *close = new QAction(tr("&Close"), this);
  close->setShortcuts({QKeySequence::Close, QKeySequence::Quit});
  connect(close, &QAction::triggered, this, &QWidget::close);

  QAction *undoAction = undo->createUndoAction(this, tr("&Undo"));
  undoAction->setIcon(themed("edit-undo"));
  undoAction->setShortcut(QKeySequence::Undo);
  QAction *redoAction = undo->createRedoAction(this, tr("&Redo"));
  redoAction->setIcon(themed("edit-redo"));
  redoAction->setShortcut(QKeySequence::Redo);

  auto *add = new QAction(themed("list-add"), tr("&Add launcher"), this);
  add->setShortcut(Qt::Key_Insert);
  add->setToolTip(tr("Add a launcher in the selected cell (or the nearest free one)"));
  connect(add, &QAction::triggered, this, &EditorWindow::addItem);
  auto *group = new QAction(themed("folder-new"), tr("Add &group"), this);
  group->setShortcut(Qt::CTRL | Qt::Key_G);
  group->setToolTip(tr("Add a group: an icon that opens its own grid of icons"));
  connect(group, &QAction::triggered, this, &EditorWindow::addGroup);
  auto *apps = new QAction(themed("applications-other"), tr("Add from &installed apps…"), this);
  apps->setShortcut(Qt::CTRL | Qt::Key_I);
  apps->setToolTip(tr("Pick from the applications in your menu (you can also drag them onto the grid)"));
  connect(apps, &QAction::triggered, this, &EditorWindow::showAppPicker);
  m_deleteAction = new QAction(themed("edit-delete"), tr("&Delete"), this);
  m_deleteAction->setShortcut(QKeySequence::Delete);
  connect(m_deleteAction, &QAction::triggered, this, [this] { deleteItem(selectedIndex()); });
  m_testAction = new QAction(themed("media-playback-start"), tr("&Test launch"), this);
  m_testAction->setShortcut(Qt::CTRL | Qt::Key_T);
  connect(m_testAction, &QAction::triggered, this, [this] { testItem(selectedIndex()); });

  m_upAction = new QAction(themed("go-up"), tr("&Up one level"), this);
  m_upAction->setShortcuts({QKeySequence(Qt::Key_Backspace), QKeySequence(Qt::ALT | Qt::Key_Up)});
  m_upAction->setToolTip(tr("Back out of this group (Backspace)"));
  connect(m_upAction, &QAction::triggered, this, &EditorWindow::goUp);
  auto *centre = new QAction(themed("zoom-original"), tr("&Centre on pointer cell"), this);
  centre->setShortcut(Qt::Key_Home);
  connect(centre, &QAction::triggered, m_canvas, &GridCanvas::centreOnOrigin);

  auto *newGrid = new QAction(tr("&New grid…"), this);
  connect(newGrid, &QAction::triggered, this, &EditorWindow::newGrid);
  auto *dupGrid = new QAction(tr("D&uplicate grid…"), this);
  connect(dupGrid, &QAction::triggered, this, &EditorWindow::duplicateGrid);
  m_renameGridAction = new QAction(tr("&Rename grid…"), this);
  connect(m_renameGridAction, &QAction::triggered, this, &EditorWindow::renameGrid);
  m_deleteGridAction = new QAction(tr("De&lete grid"), this);
  connect(m_deleteGridAction, &QAction::triggered, this, &EditorWindow::deleteGrid);

  auto *prefs = new QAction(themed("preferences-system"), tr("&Preferences…"), this);
  prefs->setShortcut(Qt::CTRL | Qt::Key_Comma);
  prefs->setToolTip(tr("Terminal, look and motion (Ctrl+,)"));
  connect(prefs, &QAction::triggered, this, &EditorWindow::preferences);

  auto *about = new QAction(tr("&About Prowal"), this);
  connect(about, &QAction::triggered, this, &EditorWindow::about);

  QMenu *file = menuBar()->addMenu(tr("&File"));
  file->addAction(save);
  file->addAction(import);
  file->addSeparator();
  file->addAction(close);
  QMenu *edit = menuBar()->addMenu(tr("&Edit"));
  edit->addAction(undoAction);
  edit->addAction(redoAction);
  edit->addSeparator();
  edit->addAction(add);
  edit->addAction(group);
  edit->addAction(apps);
  edit->addAction(m_deleteAction);
  edit->addAction(m_testAction);
  edit->addSeparator();
  edit->addAction(prefs);
  QMenu *grids = menuBar()->addMenu(tr("&Grid"));
  grids->addAction(newGrid);
  grids->addAction(dupGrid);
  grids->addAction(m_renameGridAction);
  grids->addAction(m_deleteGridAction);
  QMenu *view = menuBar()->addMenu(tr("&View"));
  view->addAction(m_upAction);
  view->addAction(centre);
  QMenu *help = menuBar()->addMenu(tr("&Help"));
  help->addAction(about);

  QToolBar *bar = addToolBar(tr("Main"));
  bar->setObjectName(QStringLiteral("main-toolbar"));
  bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  bar->addWidget(new QLabel(tr(" Grid: ")));
  bar->addWidget(m_gridBox);
  auto *gridButton = new QToolButton;
  gridButton->setText(tr("Grids"));
  gridButton->setPopupMode(QToolButton::InstantPopup);
  gridButton->setMenu(grids);
  bar->addWidget(gridButton);
  bar->addSeparator();
  bar->addAction(add);
  bar->addAction(group);
  bar->addAction(apps);
  bar->addAction(m_deleteAction);
  bar->addAction(m_testAction);
  bar->addSeparator();
  bar->addAction(undoAction);
  bar->addAction(redoAction);
  // Their text names the step ("Undo Move Firefox into Tools"), which would make
  // the toolbar jump about; the tooltip still shows it.
  for (QAction *a : {undoAction, redoAction})
    if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(a)))
      button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  bar->addSeparator();
  bar->addAction(save);
  bar->addAction(prefs);
}

// ---------------------------------------------------------------------------
// State

void EditorWindow::refresh()
{
  const Config &config = m_doc->config();
  m_grid = std::clamp(m_grid, 0, int(config.grids.size()) - 1);
  applySettings();
  // An undo can remove the Group being edited: fall back to the nearest level still there.
  while (!m_doc->items(m_grid, m_group))
  {
    m_selected = m_group.back();
    m_group.pop_back();
  }
  refreshGridList();
  refreshCrumbs();

  const std::vector<Item> &items = level();
  m_canvas->setItems(&items, m_terminal);
  const QStringList names = QStringList{config.grids[size_t(m_grid)].name} +
                            groupNames(config.grids[size_t(m_grid)].items, m_group);
  m_canvas->setParentName(m_group.empty() ? std::optional<QString>() : names[names.size() - 2]);
  m_canvas->setSelectedCell(m_selected);
  const int index = selectedIndex();
  m_panel->setItem(index >= 0 ? &items[size_t(index)] : nullptr, m_selected, m_terminal);
  updateActions();

  int problems = 0;
  forEachItem(config.grids[size_t(m_grid)].items, [&](const Item &item) {
    if (item.type == ItemType::Launcher &&
        (!commandProblem(item, m_terminal).isEmpty() || !workingDirProblem(item).isEmpty()))
      ++problems;
  });
  if (m_picker)
    m_picker->setExisting(flattened(config.grids[size_t(m_grid)].items));
  m_problems->setText(problems == 0 ? QString()
                                    : tr("%n item(s) with problems (marked with a dot)", nullptr, problems));
}

const std::vector<Item> &EditorWindow::level() const
{
  return *m_doc->items(m_grid, m_group);
}

void EditorWindow::updateActions()
{
  const int index = selectedIndex();
  m_deleteAction->setEnabled(index >= 0);
  m_testAction->setEnabled(index >= 0 && level()[size_t(index)].type == ItemType::Launcher);
  m_upAction->setEnabled(!m_group.empty());
}

void EditorWindow::refreshCrumbs()
{
  while (QLayoutItem *child = m_crumbs->takeAt(0))
  {
    delete child->widget();
    delete child;
  }
  const Grid &grid = m_doc->config().grids[size_t(m_grid)];
  QStringList names{grid.name};
  names += groupNames(grid.items, m_group);

  auto *up = new QToolButton;
  up->setDefaultAction(m_upAction);
  up->setAutoRaise(true);
  m_crumbs->addWidget(up);
  for (int i = 0; i < names.size(); ++i)
  {
    if (i > 0)
      m_crumbs->addWidget(new QLabel(QStringLiteral("\u203A")));
    auto *crumb = new QToolButton;
    crumb->setAutoRaise(true);
    crumb->setText(names[i]);
    crumb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    if (i == names.size() - 1)
    {
      QFont bold = crumb->font();
      bold.setBold(true);
      crumb->setFont(bold);
      crumb->setEnabled(false);
    }
    else
      connect(crumb, &QToolButton::clicked, this, [this, i] { goToLevel(size_t(i)); });
    m_crumbs->addWidget(crumb);
  }
  m_crumbs->addStretch();
  if (!m_group.empty())
  {
    auto *hint = new QLabel(tr("Inside a group: in the popup its items open around the group's icon."));
    hint->setEnabled(false);
    m_crumbs->addWidget(hint);
  }
}

void EditorWindow::refreshGridList()
{
  const Config &config = m_doc->config();
  m_gridBox->clear();
  for (const Grid &g : config.grids)
    m_gridBox->addItem(QStringLiteral("%1  (%2)").arg(g.name).arg(countItems(g.items)));
  m_gridBox->setCurrentIndex(m_grid);
  const bool isMain = config.grids[size_t(m_grid)].name == kMainGrid;
  m_renameGridAction->setEnabled(!isMain);
  m_deleteGridAction->setEnabled(!isMain && config.grids.size() > 1);
  const QString why = tr("\"main\" is the grid plain `prowal` shows, so it can't be renamed or deleted.");
  m_renameGridAction->setToolTip(isMain ? why : QString());
  m_deleteGridAction->setToolTip(isMain ? why : QString());
}

int EditorWindow::selectedIndex() const
{
  if (!m_selected)
    return -1;
  return itemIndexAt(level(), *m_selected);
}

void EditorWindow::selectCell(std::optional<QPoint> cell)
{
  m_selected = cell;
  const std::vector<Item> &items = level();
  const int index = selectedIndex();
  m_panel->setItem(index >= 0 ? &items[size_t(index)] : nullptr, m_selected, m_terminal);
  updateActions();
}

// ---------------------------------------------------------------------------
// Items

void EditorWindow::addAt(QPoint cell, ItemType type)
{
  const std::vector<Item> &items = level();
  const QPoint target = itemIndexAt(items, cell) < 0 ? cell : freeCellNear(items, cell);
  Item item;
  item.type = type;
  item.name = type == ItemType::Group ? tr("New group") : tr("New launcher");
  if (type == ItemType::Group)
    item.icon = QStringLiteral("folder");
  item.x = target.x();
  item.y = target.y();
  m_selected = target;
  m_doc->addItem(m_grid, m_group, item);
  m_panel->focusName();
  statusBar()->showMessage(type == ItemType::Group
                             ? tr("Added a group at (%1, %2): name it, then double-click it to fill it.")
                                 .arg(target.x())
                                 .arg(target.y())
                             : tr("Added a launcher at (%1, %2): give it a name and a command.")
                                 .arg(target.x())
                                 .arg(target.y()),
                           6000);
}

void EditorWindow::addItem()
{
  addAt(m_selected.value_or(QPoint(0, 0)));
}

void EditorWindow::addGroup()
{
  addAt(m_selected.value_or(QPoint(0, 0)), ItemType::Group);
}

void EditorWindow::openGroup(int index)
{
  if (index < 0 || level()[size_t(index)].type != ItemType::Group)
    return;
  const Item &group = level()[size_t(index)];
  m_group.emplace_back(group.x, group.y);
  m_selected.reset();
  refresh();
  m_canvas->centreOnItems();
  m_canvas->setFocus();
  statusBar()->showMessage(tr("Editing group %1 — Backspace or the bar above goes back up.").arg(group.name), 6000);
}

void EditorWindow::goToLevel(size_t depth)
{
  if (depth >= m_group.size())
    return;
  m_selected = m_group[depth];  // select the Group we came out of
  m_group.resize(depth);
  refresh();
  m_canvas->centreOnItems();
  m_canvas->setSelectedCell(m_selected);
  m_canvas->setFocus();
}

void EditorWindow::goUp()
{
  if (!m_group.empty())
    goToLevel(m_group.size() - 1);
}

void EditorWindow::moveToLevel(int index, const GroupPath &to)
{
  if (index < 0)
    return;
  const QString name = level()[size_t(index)].name;
  const auto landed = m_doc->moveToLevel(m_grid, m_group, index, to);
  if (!landed)
    return;
  const QStringList names = groupNames(m_doc->config().grids[size_t(m_grid)].items, to);
  statusBar()->showMessage(tr("Moved %1 to %2, cell (%3, %4).")
                             .arg(name, names.isEmpty() ? m_doc->config().grids[size_t(m_grid)].name : names.last())
                             .arg(landed->x())
                             .arg(landed->y()),
                           6000);
}

void EditorWindow::deleteItem(int index)
{
  if (index < 0)
    return;
  const Item &item = level()[size_t(index)];
  const QString name = item.name;
  const int inside = item.type == ItemType::Group ? countItems(item.children) : 0;
  m_doc->deleteItem(m_grid, m_group, index);
  statusBar()->showMessage(inside > 0 ? tr("Deleted group %1 and the %n item(s) in it — Ctrl+Z brings them back.",
                                           nullptr, inside)
                                          .arg(name)
                                      : tr("Deleted %1 — Ctrl+Z brings it back.").arg(name),
                           6000);
}

void EditorWindow::testItem(int index)
{
  if (index < 0)
    return;
  const Item &item = level()[size_t(index)];
  if (item.type != ItemType::Launcher)
    return;
  QString error;
  if (launchItem(item, m_terminal, &error))
    statusBar()->showMessage(tr("Started %1.").arg(item.name), 5000);
  else
    QMessageBox::warning(this, tr("Test launch"), tr("Couldn't start %1:\n%2").arg(item.name, error));
}

void EditorWindow::moveItem(int index, QPoint cell)
{
  m_selected = cell;
  m_doc->moveItem(m_grid, m_group, index, cell);
  m_canvas->setSelectedCell(cell);
  selectCell(cell);
}

void EditorWindow::editItem(int index)
{
  const Item &item = level()[size_t(index)];
  selectCell(QPoint(item.x, item.y));
  m_canvas->setSelectedCell(m_selected);
  m_panel->focusName();
}

void EditorWindow::showAppPicker()
{
  if (!m_picker)
  {
    m_picker = new AppPicker(this);
    connect(m_picker, &AppPicker::addRequested, this, [this](const std::vector<DesktopEntry> &entries) {
      std::vector<Item> items;
      for (const DesktopEntry &e : entries)
        items.push_back(itemFromDesktopEntry(e));
      placeItems(std::move(items), std::nullopt,
                 entries.size() == 1 ? tr("Add %1").arg(entries.front().name) : tr("Add %n apps", nullptr, int(entries.size())));
    });
  }
  m_picker->setExisting(flattened(m_doc->config().grids[size_t(m_grid)].items));
  m_picker->show();
  m_picker->raise();
  m_picker->activateWindow();
}

void EditorWindow::placeItems(std::vector<Item> items, std::optional<QPoint> near, const QString &text)
{
  if (items.empty())
    return;
  std::vector<Item> all = level();
  std::optional<QPoint> first;
  for (Item &item : items)
  {
    QPoint cell;
    if (near)
      cell = freeCellNear(all, *near);  // `near` itself when it is free
    else if (!first && m_selected && itemIndexAt(all, *m_selected) < 0)
      cell = *m_selected;
    else
      cell = nextFillCell(all, !m_group.empty());
    item.x = cell.x();
    item.y = cell.y();
    if (!first)
      first = cell;
    all.push_back(std::move(item));
  }
  m_selected = first;
  m_doc->setItems(m_grid, m_group, std::move(all), text);
  m_canvas->setSelectedCell(m_selected);
  statusBar()->showMessage(items.size() == 1 ? tr("Added at (%1, %2).").arg(first->x()).arg(first->y())
                                             : tr("Added %n items, starting at (%1, %2).", nullptr, int(items.size()))
                                                 .arg(first->x())
                                                 .arg(first->y()),
                           6000);
}

namespace {

// The desktop file id if the file lives in an applications directory.
QString desktopIdFor(const QString &path)
{
  for (const QString &dir : applicationDirs())
  {
    const QString rel = QDir(dir).relativeFilePath(path);
    if (!rel.startsWith(QLatin1String("..")))
      return QString(rel).replace(QLatin1Char('/'), QLatin1Char('-'));
  }
  return QString();
}

} // namespace

void EditorWindow::dropFiles(QPoint cell, const QStringList &paths)
{
  const std::vector<Item> &current = level();
  const int target = itemIndexAt(current, cell);

  // One image onto an icon: use it as that icon.
  if (paths.size() == 1 && isImageFile(paths.first()) && target >= 0)
  {
    Item item = current[size_t(target)];
    item.icon = paths.first();
    m_doc->editItem(m_grid, m_group, target, item);
    return;
  }

  std::vector<Item> items;
  QStringList skipped;
  for (const QString &path : paths)
  {
    const QFileInfo fi(path);
    if (fi.suffix() == QLatin1String("desktop"))
    {
      const auto entry = readDesktopFile(path, currentLocales());
      if (entry && entry->type == QLatin1String("Application") && !execToArgs(*entry).isEmpty())
      {
        DesktopEntry e = *entry;
        e.id = desktopIdFor(fi.absoluteFilePath());
        items.push_back(itemFromDesktopEntry(e));
        continue;
      }
    }
    else if (fi.isFile() && fi.isExecutable())
    {
      Item item;
      item.command = joinCommand({fi.absoluteFilePath()});
      item.name = nameFromCommand(item.command);
      item.icon = fi.completeBaseName();  // try the icon theme; change it in the panel
      items.push_back(item);
      continue;
    }
    skipped << fi.fileName();
  }
  placeItems(std::move(items), cell, tr("Add dropped items"));
  if (!skipped.isEmpty())
    statusBar()->showMessage(tr("Not added (not an application, .desktop file or program): %1")
                               .arg(skipped.join(QStringLiteral(", "))),
                             8000);
}

// ---------------------------------------------------------------------------
// Grids

void EditorWindow::switchGrid(int grid)
{
  if (grid < 0 || grid == m_grid)
    return;
  m_grid = grid;
  m_group.clear();
  m_selected.reset();
  refresh();
  m_canvas->centreOnItems();
}

void EditorWindow::newGrid()
{
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("New grid"),
                                             tr("Name (shown with: prowal --grid NAME):"),
                                             QLineEdit::Normal, uniqueGridName(m_doc->config(), tr("grid")), &ok)
                         .trimmed();
  if (!ok || name.isEmpty())
    return;
  if (m_doc->config().findGrid(name))
  {
    QMessageBox::information(this, tr("New grid"), tr("There is already a grid called \"%1\".").arg(name));
    return;
  }
  switchGrid(m_doc->addGrid(name));
}

void EditorWindow::duplicateGrid()
{
  const QString current = m_doc->config().grids[size_t(m_grid)].name;
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Duplicate grid"), tr("Name of the copy:"),
                                             QLineEdit::Normal, uniqueGridName(m_doc->config(), current), &ok)
                         .trimmed();
  if (!ok || name.isEmpty())
    return;
  if (m_doc->config().findGrid(name))
  {
    QMessageBox::information(this, tr("Duplicate grid"), tr("There is already a grid called \"%1\".").arg(name));
    return;
  }
  switchGrid(m_doc->duplicateGrid(m_grid, name));
}

void EditorWindow::renameGrid()
{
  const QString current = m_doc->config().grids[size_t(m_grid)].name;
  bool ok = false;
  const QString name =
    QInputDialog::getText(this, tr("Rename grid"), tr("New name:"), QLineEdit::Normal, current, &ok).trimmed();
  if (!ok || name.isEmpty() || name == current)
    return;
  if (m_doc->config().findGrid(name))
  {
    QMessageBox::information(this, tr("Rename grid"), tr("There is already a grid called \"%1\".").arg(name));
    return;
  }
  m_doc->renameGrid(m_grid, name);
}

void EditorWindow::deleteGrid()
{
  const QString name = m_doc->config().grids[size_t(m_grid)].name;
  m_doc->deleteGrid(m_grid);
  m_selected.reset();
  statusBar()->showMessage(tr("Deleted grid %1 — Ctrl+Z brings it back.").arg(name), 6000);
}

void EditorWindow::reveal(const QString &gridName, const GroupPath &group, std::optional<QPoint> select)
{
  const Config &config = m_doc->config();
  for (size_t i = 0; i < config.grids.size(); ++i)
    if (config.grids[i].name == gridName)
      m_grid = int(i);
  m_group = group;
  m_selected = select;
  refresh();  // falls back to the nearest level that exists
  m_canvas->centreOnItems();
  m_canvas->setSelectedCell(m_selected);
  if (isMinimized())
    showNormal();
  show();
  raise();
  activateWindow();
  statusBar()->showMessage(tr("Already open — showing it here."), 4000);
}

// ---------------------------------------------------------------------------
// Preferences

void EditorWindow::applySettings()
{
  // Settings are part of the document, so this also runs after an undo.
  const Config &config = m_doc->config();
  m_terminal = config.behaviour.terminal.isEmpty() ? m_autoTerminal : config.behaviour.terminal;
  if (config.appearance.iconTheme != m_iconTheme)
  {
    m_iconTheme = config.appearance.iconTheme;
    setupIconTheme(m_iconTheme);
    m_canvas->clearIconCache();
  }
  if (config.appearance.groupStyle != m_groupStyle)
  {
    m_groupStyle = config.appearance.groupStyle;
    m_canvas->setGroupStyle(m_groupStyle);
  }
}

void EditorWindow::preferences()
{
  m_canvas->setFocus();  // commit a field that is still being edited
  auto *dialog = new PreferencesDialog(m_doc->config(), m_autoTerminal, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &PreferencesDialog::applied, this, [this, dialog] {
    m_doc->setSettings(dialog->behaviour(), dialog->appearance());
    if (m_doc->isModified())
      statusBar()->showMessage(tr("Preferences changed — Save (Ctrl+S) to use them in the popup."), 8000);
  });
  connect(dialog, &PreferencesDialog::previewRequested, this, [this, dialog] {
    Config config = m_doc->config();
    config.behaviour = dialog->behaviour();
    config.appearance = dialog->appearance();
    preview(config);
  });
  dialog->open();
}

void EditorWindow::preview(const Config &config)
{
  const QString grid = config.grids[size_t(m_grid)].name;
  const QString terminal = config.behaviour.terminal.isEmpty() ? m_autoTerminal : config.behaviour.terminal;
  if (config.appearance.iconTheme != m_iconTheme)
    setupIconTheme(config.appearance.iconTheme);
  auto *popup = new PopupWindow(config, grid, terminal, x11::compositorRunning(), m_doc->path());
  popup->setPreview(true);
  popup->closed = [this, popup] {
    setupIconTheme(m_iconTheme);  // back to the editor's own setting
    popup->deleteLater();
  };
  popup->popupAtCursor();
}

// ---------------------------------------------------------------------------
// File

bool EditorWindow::save()
{
  m_canvas->setFocus();  // commit a field that is still being edited
  if (m_doc->changedOnDisk())
  {
    const auto answer = QMessageBox::warning(
      this, tr("Save"),
      tr("%1 was changed by something else since the editor opened it "
         "(for example prowal --import-apwal).\n\n"
         "Save your version over it? The other version is kept in the backups folder.")
        .arg(m_doc->path()),
      QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Save)
      return false;
  }
  QString error;
  if (!m_doc->save(&error))
  {
    QMessageBox::critical(this, tr("Save"), tr("Couldn't save:\n%1").arg(error));
    return false;
  }
  statusBar()->showMessage(tr("Saved. The next popup uses the new layout."), 5000);
  return true;
}

void EditorWindow::importApwal()
{
  const QString file = QFileDialog::getOpenFileName(this, tr("Import from apwal"), paths::defaultApwalFile(),
                                                    tr("apwal layouts (*.xml);;All files (*)"));
  if (file.isEmpty())
    return;
  const ImportResult r = readApwalFile(file);
  if (!r.ok)
  {
    QMessageBox::warning(this, tr("Import from apwal"), r.error);
    return;
  }
  const QString grid = m_doc->config().grids[size_t(m_grid)].name;
  const auto answer = QMessageBox::question(
    this, tr("Import from apwal"),
    tr("Replace the %1 item(s) in grid \"%2\" with the %3 from %4?\n\nYou can undo this.")
      .arg(m_doc->config().grids[size_t(m_grid)].items.size())
      .arg(grid)
      .arg(r.imported)
      .arg(file));
  if (answer != QMessageBox::Yes)
    return;
  m_selected.reset();
  m_group.clear();
  m_doc->setItems(m_grid, {}, r.items, tr("Import from apwal"));
  if (!r.warnings.isEmpty())
    QMessageBox::information(this, tr("Import from apwal"), r.warnings.join(QLatin1Char('\n')));
}

void EditorWindow::about()
{
  QMessageBox::about(this, tr("About Prowal"),
                     tr("<h3>Prowal %1</h3>"
                        "<p>A pop-up application launcher: a grid of icons around the mouse pointer.</p>"
                        "<p>A clean-room successor to <i>apwal</i> by Pascal Eberhard.</p>"
                        "<p>Free software under the GNU General Public License, version 3 or later. "
                        "There is no warranty.</p>"
                        "<p>Config: %2</p>")
                       .arg(QStringLiteral(PROWAL_VERSION), m_doc->path().toHtmlEscaped()));
}

bool EditorWindow::maybeSave()
{
  m_canvas->setFocus();  // commit a field that is still being edited
  if (!m_doc->isModified())
    return true;
  const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save your changes before closing?"),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                            QMessageBox::Save);
  if (answer == QMessageBox::Save)
    return save();
  return answer == QMessageBox::Discard;
}

void EditorWindow::closeEvent(QCloseEvent *e)
{
  if (maybeSave())
    e->accept();
  else
    e->ignore();
}

void EditorWindow::showLoadWarnings()
{
  QMessageBox box(QMessageBox::Warning, tr("Config warnings"),
                  tr("Some entries in the config had problems and were skipped or adjusted. "
                     "Saving will write the cleaned-up version (the old file is backed up first)."),
                  QMessageBox::Ok, this);
  box.setDetailedText(m_loadWarnings.join(QLatin1Char('\n')));
  box.exec();
}

} // namespace prowal
