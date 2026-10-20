#include "editor/IconPicker.h"

#include "icons/IconLoader.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QIcon>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <cmath>

namespace prowal {

namespace {

constexpr int kThumb = 40;         // logical thumbnail size
constexpr int kCellW = 92;
constexpr int kCellH = 80;
constexpr qint64 kCacheBytes = 64LL * 1024 * 1024;  // ~10k thumbnails: any one theme fits
constexpr int kMaxRecent = 48;

const QString kAll = QStringLiteral("*all");
const QString kRecent = QStringLiteral("*recent");

QImage decodeThumbnail(const QString &path, int px)
{
  QImageReader reader(path);
  const QSize native = reader.size();
  if (native.isValid() && reader.supportsOption(QImageIOHandler::ScaledSize) &&
      (native.width() > px || native.height() > px || QFileInfo(path).suffix().startsWith(QLatin1String("svg"))))
    reader.setScaledSize(native.scaled(px, px, Qt::KeepAspectRatio));
  QImage img = reader.read();
  if (img.isNull())
    return img;
  if (std::max(img.width(), img.height()) != px)
    img = img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

std::unique_ptr<QSettings> pickerSettings()
{
  return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("prowal"),
                                     QStringLiteral("icon-picker"));
}

} // namespace

// ---------------------------------------------------------------------------
// ThumbnailLoader

ThumbnailLoader::ThumbnailLoader(int pixels, QObject *parent) : QObject(parent), m_pixels(pixels)
{
  const int n = std::max(2, QThread::idealThreadCount() - 1);
  for (int i = 0; i < n; ++i)
  {
    m_threads.emplace_back(QThread::create([this] { run(); }));
    m_threads.back()->start(QThread::LowPriority);
  }
}

ThumbnailLoader::~ThumbnailLoader()
{
  {
    QMutexLocker lock(&m_mutex);
    m_stop = true;
    m_queue.clear();
  }
  m_wake.wakeAll();
  for (auto &t : m_threads)
    t->wait();
}

void ThumbnailLoader::setQueue(std::vector<Job> jobs)
{
  {
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
    for (Job &j : jobs)
      if (!m_busy.contains(j.key))
        m_queue.push_back(std::move(j));
  }
  m_wake.wakeAll();
}

bool ThumbnailLoader::inFlight(int key) const
{
  QMutexLocker lock(&m_mutex);
  return m_busy.contains(key);
}

void ThumbnailLoader::run()
{
  for (;;)
  {
    Job job;
    {
      QMutexLocker lock(&m_mutex);
      while (!m_stop && m_queue.empty())
        m_wake.wait(&m_mutex);
      if (m_stop)
        return;
      job = std::move(m_queue.front());
      m_queue.pop_front();
      m_busy.insert(job.key);
    }
    const QImage image = decodeThumbnail(job.path, m_pixels);
    {
      QMutexLocker lock(&m_mutex);
      m_busy.remove(job.key);
    }
    const int key = job.key;
    QMetaObject::invokeMethod(this, [this, key, image] { emit loaded(key, image); }, Qt::QueuedConnection);
  }
}

// ---------------------------------------------------------------------------
// IconGridView

IconGridView::IconGridView(QWidget *parent) : QAbstractScrollArea(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
}

int IconGridView::columns() const
{
  return std::max(1, viewport()->width() / kCellW);
}

void IconGridView::setCount(int count)
{
  m_count = count;
  m_current = -1;
  updateScrollBar();
  viewport()->update();
  emit viewChanged();
}

void IconGridView::updateScrollBar()
{
  const int rows = (m_count + columns() - 1) / columns();
  QScrollBar *bar = verticalScrollBar();
  bar->setRange(0, std::max(0, rows * kCellH - viewport()->height()));
  bar->setPageStep(viewport()->height());
  bar->setSingleStep(kCellH / 2);
}

QRect IconGridView::cellRect(int row) const
{
  const int cols = columns();
  const int left = (viewport()->width() - cols * kCellW) / 2;
  return QRect(left + (row % cols) * kCellW, (row / cols) * kCellH - verticalScrollBar()->value(), kCellW, kCellH);
}

int IconGridView::rowAt(QPoint pos) const
{
  const int cols = columns();
  const int left = (viewport()->width() - cols * kCellW) / 2;
  if (pos.x() < left || pos.x() >= left + cols * kCellW)
    return -1;
  const int row = ((pos.y() + verticalScrollBar()->value()) / kCellH) * cols + (pos.x() - left) / kCellW;
  return row >= 0 && row < m_count ? row : -1;
}

std::pair<int, int> IconGridView::visibleRows() const
{
  if (m_count == 0)
    return {-1, -1};
  const int cols = columns();
  const int top = verticalScrollBar()->value();
  const int first = (top / kCellH) * cols;
  const int last = std::min(m_count - 1, ((top + viewport()->height()) / kCellH + 1) * cols - 1);
  return {first, last};
}

void IconGridView::setCurrent(int row, bool scroll)
{
  if (row >= m_count)
    row = m_count - 1;
  m_current = row;
  if (scroll && row >= 0)
  {
    const int y = (row / columns()) * kCellH;
    QScrollBar *bar = verticalScrollBar();
    if (y < bar->value())
      bar->setValue(y);
    else if (y + kCellH > bar->value() + viewport()->height())
      bar->setValue(y + kCellH - viewport()->height());
  }
  viewport()->update();
  emit currentChanged(row);
}

void IconGridView::paintEvent(QPaintEvent *)
{
  QPainter p(viewport());
  p.setRenderHint(QPainter::Antialiasing);
  const QPalette pal = palette();
  p.fillRect(viewport()->rect(), pal.color(QPalette::Base));
  const auto [first, last] = visibleRows();
  if (first < 0)
    return;

  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.85);
  p.setFont(small);
  const QFontMetrics fm(small);
  QColor placeholder = pal.color(QPalette::Text);
  placeholder.setAlpha(18);

  for (int row = first; row <= last; ++row)
  {
    const QRect cell = cellRect(row);
    if (row == m_current)
    {
      QColor sel = pal.color(QPalette::Highlight);
      sel.setAlpha(hasFocus() ? 110 : 70);
      p.setPen(Qt::NoPen);
      p.setBrush(sel);
      p.drawRoundedRect(QRectF(cell).adjusted(3, 2, -3, -2), 8, 8);
    }
    const QRectF iconBox(cell.center().x() - kThumb / 2.0, cell.top() + 8, kThumb, kThumb);
    if (const QImage *img = thumbnailOf ? thumbnailOf(row) : nullptr)
    {
      const QSizeF logical = QSizeF(img->size()) / img->devicePixelRatio();
      p.drawImage(QPointF(iconBox.center().x() - logical.width() / 2, iconBox.center().y() - logical.height() / 2), *img);
    }
    else
    {
      p.setPen(Qt::NoPen);
      p.setBrush(placeholder);
      p.drawRoundedRect(iconBox.adjusted(4, 4, -4, -4), 6, 6);
    }
    p.setPen(pal.color(QPalette::Text));
    const QRect label(cell.left() + 3, int(iconBox.bottom()) + 5, cell.width() - 6, fm.height());
    p.drawText(label, Qt::AlignHCenter | Qt::AlignTop, fm.elidedText(nameOf ? nameOf(row) : QString(), Qt::ElideMiddle, label.width()));
  }
}

void IconGridView::resizeEvent(QResizeEvent *e)
{
  QAbstractScrollArea::resizeEvent(e);
  updateScrollBar();
  emit viewChanged();
}

void IconGridView::scrollContentsBy(int, int)
{
  viewport()->update();
  emit viewChanged();
}

void IconGridView::mousePressEvent(QMouseEvent *e)
{
  const int row = rowAt(e->position().toPoint());
  if (row >= 0)
    setCurrent(row, false);
}

void IconGridView::mouseDoubleClickEvent(QMouseEvent *e)
{
  const int row = rowAt(e->position().toPoint());
  if (row >= 0)
    emit activated(row);
}

void IconGridView::keyPressEvent(QKeyEvent *e)
{
  const int cols = columns();
  const int page = std::max(1, viewport()->height() / kCellH) * cols;
  int row = m_current < 0 ? 0 : m_current;
  switch (e->key())
  {
  case Qt::Key_Left: row -= 1; break;
  case Qt::Key_Right: row += 1; break;
  case Qt::Key_Up: row -= cols; break;
  case Qt::Key_Down: row += cols; break;
  case Qt::Key_PageUp: row -= page; break;
  case Qt::Key_PageDown: row += page; break;
  case Qt::Key_Home: row = 0; break;
  case Qt::Key_End: row = m_count - 1; break;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (m_current >= 0)
      emit activated(m_current);
    return;
  default:
    QAbstractScrollArea::keyPressEvent(e);
    return;
  }
  if (m_count > 0)
    setCurrent(std::clamp(row, 0, m_count - 1));
}

bool IconGridView::viewportEvent(QEvent *e)
{
  if (e->type() == QEvent::ToolTip && tooltipOf)
  {
    auto *help = static_cast<QHelpEvent *>(e);
    const int row = rowAt(help->pos());
    if (row >= 0)
      QToolTip::showText(help->globalPos(), tooltipOf(row), viewport(), cellRect(row));
    else
      QToolTip::hideText();
    return true;
  }
  return QAbstractScrollArea::viewportEvent(e);
}

// ---------------------------------------------------------------------------
// IconPicker

IconPicker::IconPicker(QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("Choose an icon"));
  resize(980, 680);
  m_cache.setMaxCost(kCacheBytes);
  createLoader();

  m_chain = themeChain(QIcon::themeName(), QIcon::themeSearchPaths());

  // Folders and recently used icons are per machine, not part of the layout.
  const auto settings = pickerSettings();
  const QStringList folders = settings->value(QStringLiteral("folders")).toStringList();
  for (const QString &f : folders)
    m_folders.push_back({f.section(QLatin1Char('|'), 0, 0), f.section(QLatin1Char('|'), 1, 1) != QLatin1String("0")});
  if (m_folders.empty())
    m_folders = defaultIconFolders();
  for (const QString &r : settings->value(QStringLiteral("recent")).toStringList())
  {
    IconFile f;
    f.name = r.section(QLatin1Char('\t'), 0, 0);  // the saved spec
    f.path = r.section(QLatin1Char('\t'), 1, 1);
    f.collection = kRecent;
    if (QFileInfo::exists(f.path))
      m_all.push_back(f);
  }

  m_sidebar = new QListWidget;
  m_sidebar->setMinimumWidth(200);
  m_search = new QLineEdit;
  m_search->setPlaceholderText(tr("Search icon names…"));
  m_search->setClearButtonEnabled(true);
  m_symbolic = new QCheckBox(tr("Symbolic icons"));
  m_symbolic->setToolTip(tr("Show the monochrome \"-symbolic\" variants too"));
  auto *foldersButton = new QPushButton(tr("Folders…"));
  m_grid = new IconGridView;
  m_preview = new QLabel;
  m_preview->setFixedSize(72, 72);
  m_preview->setAlignment(Qt::AlignCenter);
  m_info = new QLabel;
  m_info->setWordWrap(true);
  m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_exactFile = new QCheckBox(tr("Use this exact file (don't follow icon theme changes)"));
  m_status = new QLabel;
  m_progress = new QProgressBar;
  m_progress->setMaximumWidth(180);
  m_progress->setTextVisible(false);

  auto *buttons = new QDialogButtonBox;
  auto *fileButton = buttons->addButton(tr("Other &file…"), QDialogButtonBox::ActionRole);
  m_choose = buttons->addButton(tr("&Choose"), QDialogButtonBox::AcceptRole);
  buttons->addButton(QDialogButtonBox::Cancel);
  m_choose->setEnabled(false);

  auto *top = new QHBoxLayout;
  top->addWidget(m_search, 1);
  top->addWidget(m_symbolic);
  top->addWidget(foldersButton);
  auto *right = new QWidget;
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->addLayout(top);
  rightLayout->addWidget(m_grid, 1);
  auto *split = new QSplitter;
  split->addWidget(m_sidebar);
  split->addWidget(right);
  split->setStretchFactor(1, 1);

  auto *details = new QHBoxLayout;
  details->addWidget(m_preview);
  auto *infoBox = new QVBoxLayout;
  infoBox->addWidget(m_info);
  infoBox->addWidget(m_exactFile);
  details->addLayout(infoBox, 1);
  auto *bottom = new QHBoxLayout;
  bottom->addWidget(m_status, 1);
  bottom->addWidget(m_progress);
  bottom->addWidget(buttons);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(split, 1);
  layout->addLayout(details);
  layout->addLayout(bottom);

  m_filterTimer = new QTimer(this);
  m_filterTimer->setSingleShot(true);
  m_filterTimer->setInterval(120);
  m_requestTimer = new QTimer(this);
  m_requestTimer->setSingleShot(true);
  m_requestTimer->setInterval(30);

  m_grid->nameOf = [this](int row) {
    const IconFile &f = m_all[size_t(m_filtered[size_t(row)])];
    return f.collection == kRecent ? QFileInfo(f.path).completeBaseName() : f.name;
  };
  m_grid->tooltipOf = [this](int row) { return m_all[size_t(m_filtered[size_t(row)])].path; };
  m_grid->thumbnailOf = [this](int row) -> const QImage * { return m_cache.object(m_filtered[size_t(row)]); };

  connect(m_grid, &IconGridView::viewChanged, m_requestTimer, qOverload<>(&QTimer::start));
  connect(m_requestTimer, &QTimer::timeout, this, &IconPicker::requestThumbnails);
  connect(m_grid, &IconGridView::currentChanged, this, [this](int row) {
    m_userMoved = true;
    updateSelection(row);
  });
  connect(m_grid, &IconGridView::activated, this, &IconPicker::accept);
  connect(m_search, &QLineEdit::textChanged, m_filterTimer, qOverload<>(&QTimer::start));
  connect(m_filterTimer, &QTimer::timeout, this, &IconPicker::refilter);
  connect(m_symbolic, &QCheckBox::toggled, this, &IconPicker::refilter);
  connect(m_exactFile, &QCheckBox::toggled, this, [this] { updateSelection(m_grid->current()); });
  connect(m_sidebar, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
    if (!item)
      return;
    m_selectedCollection = item->data(Qt::UserRole).toString();
    refilter();
  });
  connect(foldersButton, &QPushButton::clicked, this, &IconPicker::editFolders);
  // Typing anywhere searches; Ctrl+F jumps to the search box.
  m_grid->installEventFilter(this);
  m_sidebar->installEventFilter(this);
  auto *find = new QAction(this);
  find->setShortcut(QKeySequence::Find);
  connect(find, &QAction::triggered, this, [this] {
    m_search->setFocus();
    m_search->selectAll();
  });
  addAction(find);
  connect(fileButton, &QPushButton::clicked, this, &IconPicker::chooseFile);
  connect(buttons, &QDialogButtonBox::accepted, this, &IconPicker::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  m_selectedCollection = kAll;
  rebuildCollections();
}

void IconPicker::createLoader()
{
  // A new loader on every rescan: deleting the old one waits for its threads,
  // and results it had already posted are dropped with it, so no stale image
  // can land on a reused index.
  delete m_loader;
  m_loader = new ThumbnailLoader(int(std::lround(kThumb * qApp->devicePixelRatio())), this);
  connect(m_loader, &ThumbnailLoader::loaded, this, [this](int key, QImage image) {
    // Kept as QImage: converting to QPixmap would hold a second copy.
    image.setDevicePixelRatio(qApp->devicePixelRatio());
    const int cost = int(std::max<qint64>(1, image.sizeInBytes()));
    m_cache.insert(key, new QImage(std::move(image)), cost);
    m_grid->viewport()->update();
  });
}

bool IconPicker::eventFilter(QObject *watched, QEvent *e)
{
  if ((watched == m_grid || watched == m_sidebar) && e->type() == QEvent::KeyPress)
  {
    auto *key = static_cast<QKeyEvent *>(e);
    const QString text = key->text();
    if (!text.isEmpty() && text.at(0).isPrint() && !text.at(0).isSpace() &&
        !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
    {
      m_search->setFocus();
      m_search->insert(text);
      return true;
    }
  }
  return QDialog::eventFilter(watched, e);
}

IconPicker::~IconPicker()
{
  stopIndexing();
}

QString IconPicker::choose(const QString &current)
{
  m_current = current;
  m_result.clear();
  m_userMoved = false;
  if (!m_indexer)
    startIndexing();
  else
    locateCurrent();
  exec();
  return m_result;
}

void IconPicker::showEvent(QShowEvent *e)
{
  QDialog::showEvent(e);
  m_search->setFocus();
  m_search->selectAll();
}

// ---------------------------------------------------------------------------
// Indexing (a background thread hands over one collection at a time)

void IconPicker::startIndexing()
{
  m_cancel = false;
  m_indexed = false;
  m_progress->show();
  m_progress->setValue(0);
  m_status->setText(tr("Finding icons…"));
  const std::vector<IconFolder> folders = m_folders;
  const int generation = ++m_generation;
  m_indexer.reset(QThread::create([this, folders, generation] {
    std::vector<IconFolder> collections;
    for (const IconFolder &f : folders)
      for (const IconFolder &c : collectionDirs(f))
        collections.push_back(c);
    const int total = int(collections.size());
    for (int i = 0; i < total; ++i)
    {
      if (m_cancel)
        return;
      std::vector<IconFile> files = scanIconCollection(collections[size_t(i)], 48, [this] { return m_cancel.load(); });
      QMetaObject::invokeMethod(
        this,
        [this, files = std::move(files), i, total, generation]() mutable {
          if (generation == m_generation)
            addBatch(std::move(files), i + 1, total);
        },
        Qt::QueuedConnection);
    }
    if (total == 0)
      QMetaObject::invokeMethod(this, [this] { addBatch({}, 0, 0); }, Qt::QueuedConnection);
  }));
  m_indexer->start(QThread::LowPriority);
}

void IconPicker::stopIndexing()
{
  m_cancel = true;
  if (m_indexer)
    m_indexer->wait();
}

void IconPicker::addBatch(std::vector<IconFile> files, int done, int total)
{
  const size_t base = m_all.size();
  if (!files.empty())
  {
    const QString dir = files.front().collection;
    if (!m_collectionLabel.contains(dir))
    {
      const QString label = files.front().themed ? themeDisplayName(dir) : QFileInfo(dir).fileName();
      m_collectionLabel.insert(dir, label);
      m_collections << dir;
    }
    m_collectionCount[dir] += int(files.size());
    m_all.insert(m_all.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
  }

  // New icons are appended, so rows already on screen keep their places.
  const QStringList words = m_search->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const int before = int(m_filtered.size());
  for (size_t i = base; i < m_all.size(); ++i)
  {
    const IconFile &f = m_all[i];
    if (m_selectedCollection != kAll && f.collection != m_selectedCollection)
      continue;
    if (f.symbolic && !m_symbolic->isChecked())
      continue;
    bool ok = true;
    for (const QString &w : words)
      ok = ok && f.name.contains(w, Qt::CaseInsensitive);
    if (ok)
      m_filtered.push_back(int(i));
  }
  if (int(m_filtered.size()) != before)
  {
    const int current = m_grid->current();
    const int scroll = m_grid->verticalScrollBar()->value();
    m_grid->setCount(int(m_filtered.size()));
    m_grid->verticalScrollBar()->setValue(scroll);
    if (current >= 0)
    {
      const bool moved = m_userMoved;
      m_grid->setCurrent(current, false);
      m_userMoved = moved;
    }
  }

  rebuildCollections();
  locateCurrent();
  if (total > 0)
    m_progress->setValue(done * 100 / total);
  const int icons = int(m_all.size());
  if (done >= total)
  {
    m_indexed = true;
    m_progress->hide();
    m_status->setText(tr("%L1 icons in %2 collections").arg(icons).arg(m_collections.size()));
  }
  else
    m_status->setText(tr("Finding icons… %L1 so far").arg(icons));
}

void IconPicker::rebuildCollections()
{
  // Current theme (and what it inherits) first, then the rest alphabetically.
  QStringList ordered = m_collections;
  std::stable_sort(ordered.begin(), ordered.end(), [this](const QString &a, const QString &b) {
    const int ia = m_chain.indexOf(QFileInfo(a).fileName());
    const int ib = m_chain.indexOf(QFileInfo(b).fileName());
    if ((ia >= 0) != (ib >= 0))
      return ia >= 0;
    if (ia >= 0 && ia != ib)
      return ia < ib;
    return m_collectionLabel.value(a).compare(m_collectionLabel.value(b), Qt::CaseInsensitive) < 0;
  });

  const QSignalBlocker block(m_sidebar);
  m_sidebar->clear();
  int all = 0;
  for (const QString &c : m_collections)
    all += m_collectionCount.value(c);
  int recent = 0;
  for (const IconFile &f : m_all)
    recent += f.collection == kRecent;

  auto add = [this](const QString &key, const QString &text) {
    auto *item = new QListWidgetItem(text, m_sidebar);
    item->setData(Qt::UserRole, key);
    item->setToolTip(key.startsWith(QLatin1Char('*')) ? QString() : key);
    if (key == m_selectedCollection)
      m_sidebar->setCurrentItem(item);
  };
  add(kAll, tr("All icons (%L1)").arg(all));
  add(kRecent, tr("Recently used (%1)").arg(recent));
  for (const QString &c : ordered)
  {
    QString label = m_collectionLabel.value(c);
    if (m_chain.contains(QFileInfo(c).fileName()))
      label += tr("  ★");
    add(c, QStringLiteral("%1 (%L2)").arg(label).arg(m_collectionCount.value(c)));
  }
}

void IconPicker::refilter()
{
  const QStringList words = m_search->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  m_filtered.clear();
  for (size_t i = 0; i < m_all.size(); ++i)
  {
    const IconFile &f = m_all[i];
    if (m_selectedCollection == kAll ? f.collection == kRecent : f.collection != m_selectedCollection)
      continue;
    if (f.symbolic && !m_symbolic->isChecked())
      continue;
    bool ok = true;
    for (const QString &w : words)
      ok = ok && f.name.contains(w, Qt::CaseInsensitive);
    if (ok)
      m_filtered.push_back(int(i));
  }
  m_grid->setCount(int(m_filtered.size()));
  if (!m_filtered.empty())
    m_grid->setCurrent(0);
  else
    updateSelection(-1);
}

void IconPicker::requestThumbnails()
{
  const auto [first, last] = m_grid->visibleRows();
  if (first < 0)
    return;
  const int n = int(m_filtered.size());
  const int screen = last - first + 1;
  const int budget = int(kCacheBytes / (qint64(kThumb) * kThumb * 4 * qApp->devicePixelRatio() * qApp->devicePixelRatio()));

  std::vector<ThumbnailLoader::Job> jobs;
  auto want = [&](int row) {
    const int key = m_filtered[size_t(row)];
    if (!m_cache.contains(key))
      jobs.push_back({key, m_all[size_t(key)].path});
  };
  // Visible first, then the screen below and above, then outward from here
  // until the memory budget would be full.
  for (int r = first; r <= last; ++r)
    want(r);
  for (int r = last + 1; r < std::min(n, last + 1 + 2 * screen); ++r)
    want(r);
  for (int r = first - 1; r >= std::max(0, first - screen); --r)
    want(r);
  int below = last + 1 + 2 * screen, above = first - screen - 1;
  int planned = std::min(n, last - first + 1 + 3 * screen);
  while (planned < budget && (below < n || above >= 0))
  {
    if (below < n)
    {
      want(below++);
      ++planned;
    }
    if (above >= 0 && planned < budget)
    {
      want(above--);
      ++planned;
    }
  }
  m_loader->setQueue(std::move(jobs));
}

// ---------------------------------------------------------------------------
// Selection and result

QString IconPicker::specFor(const IconFile &f) const
{
  if (f.collection == kRecent)
    return f.name;  // recent entries store the spec that was saved
  // An icon from the current theme (or one it inherits) is saved by name, so it
  // follows theme changes; anything else by its exact path.
  if (f.themed && m_chain.contains(QFileInfo(f.collection).fileName()) && !m_exactFile->isChecked())
    return f.name;
  return f.path;
}

void IconPicker::updateSelection(int row)
{
  m_choose->setEnabled(row >= 0);
  if (row < 0)
  {
    m_preview->clear();
    m_info->setText(m_filtered.empty() && m_indexed ? tr("No icons match.") : QString());
    m_exactFile->hide();
    return;
  }
  const IconFile &f = m_all[size_t(m_filtered[size_t(row)])];
  m_preview->setPixmap(loadIcon(f.path, 64, devicePixelRatioF()).pixmap);
  const bool followsTheme = f.themed && m_chain.contains(QFileInfo(f.collection).fileName());
  m_exactFile->setVisible(followsTheme);
  const QString spec = specFor(f);
  const QString how = spec == f.path ? tr("this exact file") : tr("follows your icon theme");
  m_info->setText(tr("<b>%1</b> — %2<br><small>%3</small><br>Saved as: <tt>%4</tt> (%5)")
                    .arg(f.name.toHtmlEscaped(),
                         (f.collection == kRecent ? tr("recently used") : m_collectionLabel.value(f.collection)).toHtmlEscaped(),
                         f.path.toHtmlEscaped(), spec.toHtmlEscaped(), how));
}

void IconPicker::locateCurrent()
{
  // Until the user moves the selection, keep trying to show the current icon.
  if (m_userMoved || m_current.isEmpty() || m_selectedCollection != kAll)
    return;
  const bool isPath = m_current.contains(QLatin1Char('/'));
  for (size_t row = 0; row < m_filtered.size(); ++row)
  {
    const IconFile &f = m_all[size_t(m_filtered[row])];
    const bool hit = isPath ? f.path == m_current
                            : (f.themed && f.name == m_current && m_chain.contains(QFileInfo(f.collection).fileName()));
    if (hit)
    {
      m_grid->setCurrent(int(row));
      m_userMoved = false;  // setCurrent counts as a move; this one was ours
      return;
    }
  }
}

void IconPicker::rememberRecent(const QString &spec, const QString &path)
{
  const auto settings = pickerSettings();
  QStringList recent = settings->value(QStringLiteral("recent")).toStringList();
  const QString entry = spec + QLatin1Char('\t') + path;
  recent.removeAll(entry);
  recent.prepend(entry);
  while (recent.size() > kMaxRecent)
    recent.removeLast();
  settings->setValue(QStringLiteral("recent"), recent);
}

void IconPicker::accept()
{
  const int row = m_grid->current();
  if (row < 0)
    return;
  const IconFile &f = m_all[size_t(m_filtered[size_t(row)])];
  m_result = specFor(f);
  rememberRecent(m_result, f.path);
  QDialog::accept();
}

void IconPicker::chooseFile()
{
  const QString file = QFileDialog::getOpenFileName(
    this, tr("Choose an icon file"), QDir::homePath(),
    tr("Images (*.png *.svg *.svgz *.xpm *.jpg *.jpeg *.gif *.ico *.webp *.bmp);;All files (*)"));
  if (file.isEmpty())
    return;
  m_result = file;
  rememberRecent(file, file);
  QDialog::accept();
}

void IconPicker::editFolders()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Icon folders"));
  auto *list = new QListWidget;
  for (const IconFolder &f : m_folders)
  {
    auto *item = new QListWidgetItem(f.path, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(f.recursive ? Qt::Checked : Qt::Unchecked);
  }
  auto *note = new QLabel(tr("Ticked folders include their subfolders. Symlinked folders are never followed. "
                             "Remove folders on slow or network drives to keep browsing quick."));
  note->setWordWrap(true);
  auto *add = new QPushButton(tr("Add…"));
  auto *remove = new QPushButton(tr("Remove"));
  auto *reset = new QPushButton(tr("Defaults"));
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  auto *row = new QHBoxLayout;
  row->addWidget(add);
  row->addWidget(remove);
  row->addWidget(reset);
  row->addStretch();
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(list);
  layout->addWidget(note);
  layout->addLayout(row);
  layout->addWidget(buttons);
  dialog.resize(560, 360);

  connect(add, &QPushButton::clicked, &dialog, [&] {
    const QString dir = QFileDialog::getExistingDirectory(&dialog, tr("Add icon folder"), QDir::homePath());
    if (dir.isEmpty())
      return;
    auto *item = new QListWidgetItem(dir, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
  });
  connect(remove, &QPushButton::clicked, &dialog, [&] { delete list->currentItem(); });
  connect(reset, &QPushButton::clicked, &dialog, [&] {
    list->clear();
    for (const IconFolder &f : defaultIconFolders())
    {
      auto *item = new QListWidgetItem(f.path, list);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Checked);
    }
  });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;

  m_folders.clear();
  QStringList saved;
  for (int i = 0; i < list->count(); ++i)
  {
    const bool recursive = list->item(i)->checkState() == Qt::Checked;
    m_folders.push_back({list->item(i)->text(), recursive});
    saved << list->item(i)->text() + (recursive ? QStringLiteral("|1") : QStringLiteral("|0"));
  }
  pickerSettings()->setValue(QStringLiteral("folders"), saved);

  // Start over with the new folders (recent entries stay).
  stopIndexing();
  createLoader();
  m_cache.clear();
  std::vector<IconFile> recent;
  for (const IconFile &f : m_all)
    if (f.collection == kRecent)
      recent.push_back(f);
  m_all = std::move(recent);
  m_collections.clear();
  m_collectionCount.clear();
  m_collectionLabel.clear();
  m_selectedCollection = kAll;
  refilter();
  rebuildCollections();
  startIndexing();
}

} // namespace prowal
