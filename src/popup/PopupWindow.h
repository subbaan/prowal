#pragma once

#include "config/Model.h"
#include "popup/Layout.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include <functional>

class QPainter;

namespace prowal {

class PopupWindow : public QWidget
{
  Q_OBJECT

public:
  // Shows the grid called gridName (which must exist) from its own copy of config.
  PopupWindow(Config config, const QString &gridName, QString terminalPrefix, bool compositing,
              QString configPath);

  // Lays the grid out around the current pointer position and shows it.
  void popupAtCursor();

  std::function<void(const char *)> trace;  // start-up tracing
  std::function<void()> closed;             // after the window has gone

  // A preview (from the preferences): right click just closes, no editor.
  void setPreview(bool preview) { m_preview = preview; }

protected:
  bool event(QEvent *e) override;
  void paintEvent(QPaintEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void closeEvent(QCloseEvent *e) override;
  void hideEvent(QHideEvent *e) override;

private:
  enum class Motion { Off, Reduced, Full };

  // The grid, or a Group opened inside it; m_levels.back() is on screen.
  struct Level
  {
    const std::vector<Item> *items = nullptr;
    QString name;
    QPoint cell;    // the Group's cell in its parent (unused for the top level)
    QPoint anchor;  // global point the origin cell's icon is centred on
  };

  struct CachedIcon
  {
    QPixmap pixmap;
    QPixmap shadow;
    bool available = true;
  };

  struct Entry
  {
    const Item *item = nullptr;  // nullptr: the back button
    QPoint cell;
    QPixmap pixmap;
    QPixmap shadow;
    QString label;
    bool available = true;
    qreal appearDelay = 0;  // ms after show
    qreal hover = 0;        // 0..1, animated towards 1 while hovered
    qreal pulseStart = -1;  // ms after show, or -1
  };

  void loadIcons(qreal dpr);
  const CachedIcon &iconFor(const Item &item, qreal dpr);
  void layoutLevel(const QRect &keep);
  void changeLevel(bool back);
  void enterGroup(int index);
  void goBack();
  void preloadStep();
  void applyShapeMask();
  QRect iconRect(int index) const;
  QRect labelRect(int index, bool full) const;
  QString elidedLabel(int index) const;

  void paintEntry(QPainter &p, int index, qreal now);
  void paintLabel(QPainter &p, int index, qreal opacity, bool full);
  void paintPanel(QPainter &p, qreal opacity);

  void setHover(int index);
  std::vector<QPoint> entryCells() const;
  void moveHighlight(QPoint dir);
  void highlightOrigin();
  void activate(int index, bool closeAfter);
  void showNote(int index, const QString &text);
  void openEditor(int index);
  void requestClose(qreal delayMs = 0);
  void pollPointer();
  void retryKeyboardGrab();

  qreal now() const;
  qreal levelTime(qreal t) const { return t - m_levelStart; }
  qreal duration(qreal ms) const { return ms / m_speed; }
  void ensureTicking();
  void tick();
  bool animating(qreal t) const;

  const Config m_config;
  const Grid *m_grid;
  QString m_terminal;
  bool m_compositing;
  QString m_configPath;
  Motion m_motion;
  qreal m_speed;
  bool m_labelsAlways;
  bool m_labelsHover;
  QColor m_accent;
  QFont m_labelFont;
  int m_labelHeight = 0;

  std::vector<Level> m_levels;
  std::vector<Entry> m_entries;
  LayoutResult m_layout;
  QRect m_ownWindow;  // global rect the current level needs (the window may be larger)
  QHash<const Item *, CachedIcon> m_icons;  // the config is const, so items stay put
  std::vector<const Item *> m_preload;      // Group contents still to load
  QTimer m_preloadTimer;

  // Level changes: the previous level fades out from a snapshot while the new one appears.
  qreal m_levelStart = 0;
  bool m_levelBack = false;
  QPixmap m_ghost;
  QRect m_ghostRect;   // global
  QPoint m_ghostFocus; // global point the snapshot zooms around
  QPixmap m_panelShadow;
  int m_hover = -1;
  bool m_keyboard = false;  // the highlight follows the arrow keys until the mouse moves
  QPoint m_lastMouse;
  bool m_preview = false;
  int m_pressed = -1;
  Qt::MouseButton m_pressedButton = Qt::NoButton;
  bool m_firstFrameSeen = false;

  QElapsedTimer m_clock;
  qreal m_lastTick = 0;
  bool m_closing = false;
  bool m_closeDone = false;
  qreal m_closeStart = 0;

  QTimer m_frameTimer;
  QTimer m_pollTimer;
  QTimer m_leaveTimer;
  QTimer m_grabTimer;
  int m_grabTries = 0;
};

} // namespace prowal
