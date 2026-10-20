#pragma once

#include "icons/IconIndex.h"

#include <QAbstractScrollArea>
#include <QCache>
#include <QDialog>
#include <QImage>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <deque>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTimer;

namespace prowal {

// Decodes thumbnails on worker threads. The GUI hands it an ordered wish list
// (visible first, then ahead, then the rest) which replaces whatever is still
// queued, so scrolling never waits behind stale work.
class ThumbnailLoader : public QObject
{
  Q_OBJECT

public:
  ThumbnailLoader(int pixels, QObject *parent = nullptr);
  ~ThumbnailLoader() override;

  struct Job
  {
    int key;
    QString path;
  };
  void setQueue(std::vector<Job> jobs);
  bool inFlight(int key) const;

signals:
  void loaded(int key, const QImage &image);

private:
  void run();

  const int m_pixels;
  mutable QMutex m_mutex;
  QWaitCondition m_wake;
  std::deque<Job> m_queue;
  QSet<int> m_busy;
  bool m_stop = false;
  std::vector<std::unique_ptr<QThread>> m_threads;
};

// A fast grid over a large list: only visible cells are painted, positions are
// arithmetic, so 100k+ entries scroll smoothly.
class IconGridView : public QAbstractScrollArea
{
  Q_OBJECT

public:
  explicit IconGridView(QWidget *parent = nullptr);

  std::function<QString(int)> nameOf;
  std::function<QString(int)> tooltipOf;
  std::function<const QImage *(int)> thumbnailOf;  // nullptr: not loaded yet

  void setCount(int count);
  int count() const { return m_count; }
  void setCurrent(int row, bool scroll = true);
  int current() const { return m_current; }
  // Rows visible now (first, last), -1 if none.
  std::pair<int, int> visibleRows() const;
  int columns() const;

signals:
  void currentChanged(int row);
  void activated(int row);
  void viewChanged();

protected:
  void paintEvent(QPaintEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void scrollContentsBy(int dx, int dy) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  bool viewportEvent(QEvent *e) override;

private:
  int rowAt(QPoint pos) const;
  QRect cellRect(int row) const;
  void updateScrollBar();

  int m_count = 0;
  int m_current = -1;
};

class IconPicker : public QDialog
{
  Q_OBJECT

public:
  explicit IconPicker(QWidget *parent = nullptr);
  ~IconPicker() override;

  // Opens on `current` (theme name or path). Returns the chosen spec, or empty.
  QString choose(const QString &current);

protected:
  void showEvent(QShowEvent *e) override;
  bool eventFilter(QObject *watched, QEvent *e) override;

private:
  void startIndexing();
  void createLoader();
  void stopIndexing();
  void addBatch(std::vector<IconFile> files, int foldersDone, int foldersTotal);
  void rebuildCollections();
  void refilter();
  void requestThumbnails();
  void updateSelection(int row);
  QString specFor(const IconFile &f) const;
  void locateCurrent();
  void editFolders();
  void chooseFile();
  void accept() override;
  void rememberRecent(const QString &spec, const QString &path);

  std::vector<IconFile> m_all;
  std::vector<int> m_filtered;          // indices into m_all for the grid
  QStringList m_collections;            // in sidebar order, after the two fixed rows
  QHash<QString, int> m_collectionCount;
  QHash<QString, QString> m_collectionLabel;
  QStringList m_chain;                  // current icon theme + inherited themes
  std::vector<IconFolder> m_folders;
  std::vector<IconFile> m_recent;

  QCache<int, QImage> m_cache;
  ThumbnailLoader *m_loader = nullptr;
  std::unique_ptr<QThread> m_indexer;
  std::atomic_bool m_cancel{false};
  int m_generation = 0;  // bumped on rescan; stale batches are ignored
  bool m_indexed = false;

  QString m_current;
  QString m_result;
  bool m_userMoved = false;
  QString m_selectedCollection;

  QListWidget *m_sidebar;
  QLineEdit *m_search;
  QCheckBox *m_symbolic;
  IconGridView *m_grid;
  QLabel *m_preview;
  QLabel *m_info;
  QCheckBox *m_exactFile;
  QLabel *m_status;
  QProgressBar *m_progress;
  QPushButton *m_choose;
  QTimer *m_filterTimer;
  QTimer *m_requestTimer;
};

} // namespace prowal
