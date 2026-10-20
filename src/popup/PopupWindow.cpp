#include "popup/PopupWindow.h"

#include "icons/IconLoader.h"
#include "platform/X11.h"
#include "system/Launch.h"

#include <QBitmap>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QSet>
#include <QToolTip>

#include <cmath>
#include <cstdio>

namespace prowal {

namespace {

constexpr int kPollMs = 50;
constexpr int kFrameMs = 16;
constexpr int kGrabRetryMs = 25;
constexpr int kGrabRetries = 120;  // 3 s: long enough for a held-down launch key

// Animation timings at animationSpeed 1.0.
constexpr qreal kAppearMs = 150;
constexpr qreal kStaggerPerCellMs = 22;
constexpr qreal kMaxStaggerMs = 110;
constexpr qreal kReducedFadeMs = 110;
constexpr qreal kHoverMs = 90;
constexpr qreal kPulseMs = 170;
constexpr qreal kCloseMs = 130;
constexpr qreal kLevelMs = 150;   // group open/close cross-fade
constexpr qreal kPreloadDelayMs = 250;
constexpr int kPreloadPerStep = 2;
constexpr int kPreloadMax = 400;

constexpr qreal kHoverScale = 0.12;
constexpr qreal kPulseScale = 0.22;
constexpr int kShadowRadius = 7;
constexpr int kPanelRadius = 16;
constexpr int kPanelPad = 8;

const QColor kDefaultAccent(130, 175, 255);
const QColor kDefaultPanel(28, 31, 38);
const QColor kPillBackground(18, 20, 26, 225);
const QColor kPillText(240, 242, 248);

qreal ease(QEasingCurve::Type type, qreal t)
{
  return QEasingCurve(type).valueForProgress(std::clamp(t, 0.0, 1.0));
}

qreal easeOutBack(qreal t)
{
  QEasingCurve curve(QEasingCurve::OutBack);
  curve.setOvershoot(1.3);  // a gentle settle, not a bounce
  return curve.valueForProgress(std::clamp(t, 0.0, 1.0));
}

QColor colourOr(const QString &name, const QColor &fallback)
{
  const QColor c(name);
  return c.isValid() ? c : fallback;
}

} // namespace

PopupWindow::PopupWindow(Config config, const QString &gridName, QString terminalPrefix,
                         bool compositing, QString configPath)
  : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
  , m_config(std::move(config))
  , m_grid(m_config.findGrid(gridName))
  , m_terminal(std::move(terminalPrefix))
  , m_compositing(compositing)
  , m_configPath(std::move(configPath))
  , m_speed(m_config.appearance.animationSpeed)
  , m_labelsAlways(m_config.appearance.labels == QLatin1String("always"))
  , m_labelsHover(m_config.appearance.labels == QLatin1String("hover"))
  , m_accent(colourOr(m_config.appearance.accentColor, kDefaultAccent))
{
  Q_ASSERT(m_grid);
  const QString &motion = m_config.appearance.motion;
  if (!compositing || motion == QLatin1String("off"))
    m_motion = Motion::Off;  // without a compositor nothing can fade
  else if (motion == QLatin1String("reduced"))
    m_motion = Motion::Reduced;
  else
    m_motion = Motion::Full;

  setWindowTitle(QStringLiteral("Prowal"));
  setMouseTracking(true);
  if (m_compositing)
    setAttribute(Qt::WA_TranslucentBackground);

  m_labelFont = font();
  m_labelFont.setPixelSize(12);
  m_labelFont.setWeight(QFont::Medium);
  if (m_labelsAlways)
    m_labelHeight = QFontMetrics(m_labelFont).height() + 6;

  m_frameTimer.setInterval(kFrameMs);
  m_frameTimer.setTimerType(Qt::PreciseTimer);
  connect(&m_frameTimer, &QTimer::timeout, this, &PopupWindow::tick);
  m_pollTimer.setInterval(kPollMs);
  connect(&m_pollTimer, &QTimer::timeout, this, &PopupWindow::pollPointer);
  m_leaveTimer.setSingleShot(true);
  connect(&m_leaveTimer, &QTimer::timeout, this, [this] { requestClose(); });
  m_grabTimer.setInterval(kGrabRetryMs);
  connect(&m_grabTimer, &QTimer::timeout, this, &PopupWindow::retryKeyboardGrab);
  m_preloadTimer.setInterval(1);
  connect(&m_preloadTimer, &QTimer::timeout, this, &PopupWindow::preloadStep);
}

// ---------------------------------------------------------------------------
// Setup

const PopupWindow::CachedIcon &PopupWindow::iconFor(const Item &item, qreal dpr)
{
  auto it = m_icons.find(&item);
  if (it != m_icons.end())
    return it.value();
  const int size = m_config.appearance.iconSize;
  CachedIcon c;
  if (item.type == ItemType::Group)
    c.pixmap = groupIcon(item, m_config.appearance.groupStyle, size, dpr, m_accent);
  else
  {
    c.available = programAvailable(item);
    c.pixmap = loadItemIcon(item, size, dpr).pixmap;
    if (!c.available)
      c.pixmap = desaturated(c.pixmap);
  }
  if (m_compositing && m_config.appearance.shadows)
    c.shadow = shadowFor(c.pixmap, kShadowRadius, QColor(0, 0, 0, 200));
  return m_icons.insert(&item, c).value();
}

void PopupWindow::loadIcons(qreal dpr)
{
  m_entries.clear();
  const Level &level = m_levels.back();
  const bool inGroup = m_levels.size() > 1;
  QSet<QPair<int, int>> used;
  for (const Item &item : *level.items)
  {
    if (used.contains({item.x, item.y}))
      continue;  // duplicates were reported when the config loaded
    used.insert({item.x, item.y});

    const CachedIcon &icon = iconFor(item, dpr);
    Entry e;
    e.item = &item;
    e.cell = QPoint(item.x, item.y);
    e.available = icon.available;
    e.pixmap = icon.pixmap;
    e.shadow = icon.shadow;
    e.label = item.name.isEmpty() ? item.command : item.name;
    if (item.type == ItemType::Group)
      e.label += QStringLiteral("  \u203A");
    if (!e.available)
      e.label += QStringLiteral("  (not installed)");
    m_entries.push_back(std::move(e));
  }

  // Inside a Group, a free origin cell (under the pointer) becomes the way back.
  if (inGroup && !used.contains({0, 0}))
  {
    Entry e;
    e.pixmap = backIcon(m_config.appearance.iconSize, dpr, m_accent);
    const QString parent = m_levels.size() > 2 ? m_levels[m_levels.size() - 2].name : QString();
    e.label = parent.isEmpty() ? QStringLiteral("Back") : QStringLiteral("Back to %1").arg(parent);
    m_entries.push_back(std::move(e));
  }

  for (Entry &e : m_entries)
  {
    if (m_motion == Motion::Full && !m_levelBack)
      e.appearDelay = std::min(kMaxStaggerMs, std::hypot(e.cell.x(), e.cell.y()) * kStaggerPerCellMs);
  }
}

void PopupWindow::layoutLevel(const QRect &keep)
{
  const QPoint anchor = m_levels.back().anchor;
  QScreen *screen = QGuiApplication::screenAt(anchor);
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  const qreal dpr = screen->devicePixelRatio();

  loadIcons(dpr);
  if (trace)
    trace("icons loaded");

  LayoutInput in;
  for (const Entry &e : m_entries)
    in.cells.push_back(e.cell);
  in.iconSize = m_config.appearance.iconSize;
  in.spacing = m_config.appearance.spacing;
  in.labelHeight = m_labelHeight;
  if (m_compositing)
  {
    // Room for shadows and the panel edge; hover labels may overhang the grid
    // sideways and below.
    const int edge = kPanelPad + kShadowRadius + 6;
    const int side = m_labelsHover || m_labelsAlways ? std::max(edge, 48) : edge;
    const int bottom = m_labelsHover ? std::max(edge, QFontMetrics(m_labelFont).height() + 18) : edge;
    in.margins = QMargins(side, edge, side, bottom);
  }
  in.cursor = anchor;
  in.available = screen->availableGeometry();
  m_layout = computeLayout(in);
  m_ownWindow = m_layout.window;

  // While the previous level fades out the window covers both, so the snapshot isn't cut off.
  if (keep.isValid())
  {
    const QRect window = m_layout.window.united(keep);
    const QPoint shift = m_layout.window.topLeft() - window.topLeft();
    m_layout.window = window;
    m_layout.grid.translate(shift);
    for (QRect &r : m_layout.cellRects)
      r.translate(shift);
  }

  if (m_compositing && m_config.appearance.mode == QLatin1String("panel"))
  {
    const QRect panel = m_layout.grid.adjusted(-kPanelPad, -kPanelPad, kPanelPad, kPanelPad);
    m_panelShadow = roundedRectShadow(panel.size(), kPanelRadius, kShadowRadius + 4, QColor(0, 0, 0, 150), dpr);
  }

  setGeometry(m_layout.window);
  if (!m_compositing)
    applyShapeMask();
}

void PopupWindow::popupAtCursor()
{
  m_lastMouse = QCursor::pos();
  m_levels = {Level{&m_grid->items, m_grid->name, QPoint(), m_lastMouse}};
  layoutLevel(QRect());
  if (m_compositing)
    x11::drawsOwnShadows(winId());
  m_clock.start();
  show();
  raise();
  ensureTicking();
  m_pollTimer.start();
  pollPointer();
  retryKeyboardGrab();

  // Load what's inside the Groups while the user is still looking, so opening one is instant.
  std::function<void(const std::vector<Item> &)> collect = [&](const std::vector<Item> &items) {
    for (const Item &item : items)
      if (item.type == ItemType::Group)
        for (const Item &child : item.children)
        {
          if (m_preload.size() >= size_t(kPreloadMax))
            return;
          m_preload.push_back(&child);
          if (child.type == ItemType::Group)
            collect(child.children);
        }
  };
  collect(m_grid->items);
  if (!m_preload.empty())
    QTimer::singleShot(int(kPreloadDelayMs), this, [this] { m_preloadTimer.start(); });
}

void PopupWindow::preloadStep()
{
  const qreal dpr = devicePixelRatioF();
  for (int i = 0; i < kPreloadPerStep && !m_preload.empty(); ++i)
  {
    iconFor(*m_preload.back(), dpr);
    m_preload.pop_back();
  }
  if (m_preload.empty() || m_closing)
    m_preloadTimer.stop();
}

void PopupWindow::changeLevel(bool back)
{
  if (trace)
    trace("changing level");
  // Snapshot of what is on screen now, to fade out over the new level.
  m_ghost = QPixmap();
  QRect keep;
  if (m_motion != Motion::Off)
  {
    const QRect own = m_ownWindow.translated(-m_layout.window.topLeft());
    m_ghost = grab(own);
    m_ghostRect = m_ownWindow;
    keep = m_ownWindow;
  }

  QToolTip::hideText();
  m_levelBack = back;
  m_hover = -1;
  m_pressed = -1;
  m_pressedButton = Qt::NoButton;
  m_leaveTimer.stop();
  layoutLevel(keep);
  m_levelStart = now();
  ensureTicking();
  update();
  pollPointer();
  if (m_keyboard)
    highlightOrigin();
  else
    setHover(hitTest(m_layout, QCursor::pos() - m_layout.window.topLeft()));
  if (trace)
    trace(back ? "level closed" : "level opened");
}

void PopupWindow::enterGroup(int index)
{
  const Entry &entry = m_entries[size_t(index)];
  const QRect icon = iconRect(index);
  Level level;
  level.items = &entry.item->children;
  level.name = entry.item->name;
  level.cell = entry.cell;
  level.anchor = m_layout.window.topLeft() + icon.center();
  m_ghostFocus = level.anchor;
  m_levels.push_back(level);
  changeLevel(false);
}

void PopupWindow::goBack()
{
  if (m_levels.size() < 2)
    return;
  m_ghostFocus = m_levels.back().anchor;  // the child shrinks back into its Group's cell
  m_levels.pop_back();
  changeLevel(true);
}

void PopupWindow::retryKeyboardGrab()
{
  // Qt grabs the keyboard for popups once, when shown. If the window manager
  // still holds its shortcut grab at that moment, keep trying until it lets go.
  if (x11::grabKeyboard(winId()) || ++m_grabTries >= kGrabRetries)
    m_grabTimer.stop();
  else if (!m_grabTimer.isActive())
    m_grabTimer.start();
}

QRect PopupWindow::iconRect(int index) const
{
  const QRect cell = m_layout.cellRects[size_t(index)];
  const int s = m_config.appearance.iconSize;
  const int sp = m_config.appearance.spacing;
  return QRect(cell.left() + (cell.width() - s) / 2, cell.top() + sp / 2, s, s);
}

QString PopupWindow::elidedLabel(int index) const
{
  const int width = m_layout.cellRects[size_t(index)].width() - 2;
  return QFontMetrics(m_labelFont).elidedText(m_entries[size_t(index)].label, Qt::ElideRight, width - 10);
}

QRect PopupWindow::labelRect(int index, bool full) const
{
  const QFontMetrics fm(m_labelFont);
  const QString text = full ? m_entries[size_t(index)].label : elidedLabel(index);
  const QRect icon = iconRect(index);
  const int w = fm.horizontalAdvance(text) + 12;
  const int h = fm.height() + 4;
  const int top = m_labelsAlways ? icon.bottom() + m_config.appearance.spacing / 2 + 2 : icon.bottom() + 9;
  QRect r(icon.center().x() + 1 - w / 2, top, w, h);
  // Keep long labels inside the window.
  if (r.left() < 2)
    r.moveLeft(2);
  if (r.right() > width() - 3)
    r.moveRight(width() - 3);
  return r;
}

void PopupWindow::applyShapeMask()
{
  // No compositor: cut the window to the icons' (and fixed labels') outlines.
  QImage shape(size(), QImage::Format_ARGB32_Premultiplied);
  shape.fill(Qt::transparent);
  QPainter p(&shape);
  for (size_t i = 0; i < m_entries.size(); ++i)
  {
    const QPixmap &pm = m_entries[i].pixmap;
    const QSizeF logical = QSizeF(pm.size()) / pm.devicePixelRatio();
    const QPointF c = QRectF(iconRect(int(i))).center();
    p.drawPixmap(QPointF(c.x() - logical.width() / 2, c.y() - logical.height() / 2), pm);
    if (m_labelsAlways)
      p.fillRect(labelRect(int(i), false), Qt::black);
  }
  p.end();
  setMask(QBitmap::fromImage(shape.createAlphaMask(Qt::ThresholdAlphaDither)));
}

// ---------------------------------------------------------------------------
// Animation

qreal PopupWindow::now() const
{
  return m_clock.isValid() ? m_clock.nsecsElapsed() / 1e6 : 0;
}

void PopupWindow::ensureTicking()
{
  if (m_frameTimer.isActive())
    return;
  m_lastTick = now();
  m_frameTimer.start();
  update();
}

bool PopupWindow::animating(qreal t) const
{
  if (m_closing)
    return true;
  const qreal appear = m_motion == Motion::Full ? duration(kAppearMs) : duration(kReducedFadeMs);
  if (!m_ghost.isNull() && levelTime(t) < duration(kLevelMs))
    return true;
  for (size_t i = 0; i < m_entries.size(); ++i)
  {
    const Entry &e = m_entries[i];
    const qreal target = int(i) == m_hover ? 1 : 0;
    if (e.hover != target)
      return true;
    if (m_motion != Motion::Off && levelTime(t) < duration(e.appearDelay) + appear)
      return true;
    if (e.pulseStart >= 0 && t < e.pulseStart + duration(kPulseMs))
      return true;
  }
  return false;
}

void PopupWindow::tick()
{
  const qreal t = now();
  const qreal dt = t - m_lastTick;
  m_lastTick = t;

  const qreal step = m_motion == Motion::Off ? 1.0 : dt / duration(kHoverMs);
  for (size_t i = 0; i < m_entries.size(); ++i)
  {
    Entry &e = m_entries[i];
    const qreal target = int(i) == m_hover && !m_closing ? 1 : 0;
    e.hover = target > e.hover ? std::min(target, e.hover + step) : std::max(target, e.hover - step);
  }

  if (m_closing && t >= m_closeStart + (m_motion == Motion::Off ? 0 : duration(kCloseMs)))
  {
    m_frameTimer.stop();
    m_closeDone = true;
    close();
    return;
  }
  update();
  if (!animating(t))
    m_frameTimer.stop();
}

void PopupWindow::requestClose(qreal delayMs)
{
  if (m_closing)
    return;
  m_closing = true;
  m_closeStart = now() + delayMs;
  m_leaveTimer.stop();
  m_pollTimer.stop();
  ensureTicking();
}

// ---------------------------------------------------------------------------
// Painting

void PopupWindow::paintPanel(QPainter &p, qreal opacity)
{
  const QRectF panel = QRectF(m_layout.grid).adjusted(-kPanelPad, -kPanelPad, kPanelPad, kPanelPad);
  p.save();
  p.setOpacity(opacity);
  if (!m_panelShadow.isNull())
  {
    const qreal pad = kShadowRadius + 4;
    p.drawPixmap(QPointF(panel.left() - pad, panel.top() - pad + 4), m_panelShadow);
  }
  QColor bg = colourOr(m_config.appearance.panelColor, kDefaultPanel);
  bg.setAlphaF(m_config.appearance.panelOpacity / 100.0);
  p.setPen(QPen(QColor(255, 255, 255, 30), 1));
  p.setBrush(bg);
  p.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), kPanelRadius, kPanelRadius);
  p.restore();
}

void PopupWindow::paintEntry(QPainter &p, int index, qreal t)
{
  const Entry &e = m_entries[size_t(index)];
  const QRectF icon = iconRect(index);
  const QPointF centre = icon.center();

  qreal opacity = 1;
  qreal scale = 1;
  if (m_motion == Motion::Full)
  {
    const qreal a = (levelTime(t) - duration(e.appearDelay)) / duration(kAppearMs);
    opacity = ease(QEasingCurve::OutCubic, a);
    // Opening a level blooms out from the pointer; going back settles in from slightly larger.
    scale = m_levelBack ? 1.1 - 0.1 * ease(QEasingCurve::OutCubic, a) : 0.55 + 0.45 * easeOutBack(a);
    scale *= 1 + kHoverScale * ease(QEasingCurve::InOutQuad, e.hover);
    if (e.pulseStart >= 0)
    {
      const qreal pt = (t - e.pulseStart) / duration(kPulseMs);
      if (pt >= 0 && pt < 1)
        scale *= 1 + kPulseScale * std::sin(M_PI * pt);
    }
  }
  else if (m_motion == Motion::Reduced)
    opacity = ease(QEasingCurve::OutQuad, levelTime(t) / duration(kReducedFadeMs));
  if (opacity <= 0)
    return;

  p.save();
  p.setOpacity(p.opacity() * opacity);
  p.translate(centre);
  p.scale(scale, scale);
  p.translate(-centre);

  if (e.hover > 0 && m_compositing)
  {
    const qreal pad = std::max(3, m_config.appearance.spacing / 2);
    QColor fill = m_accent;
    fill.setAlphaF(0.22 * e.hover);
    QColor edge = m_accent;
    edge.setAlphaF(0.45 * e.hover);
    p.setPen(QPen(edge, 1));
    p.setBrush(fill);
    p.drawRoundedRect(icon.adjusted(-pad, -pad, pad, pad), 11, 11);
  }

  const QSizeF logical = QSizeF(e.pixmap.size()) / e.pixmap.devicePixelRatio();
  const QPointF topLeft(centre.x() - logical.width() / 2, centre.y() - logical.height() / 2);
  if (!e.shadow.isNull())
  {
    p.save();
    p.setOpacity(p.opacity() * (0.38 + 0.25 * e.hover));
    p.drawPixmap(topLeft - QPointF(kShadowRadius, kShadowRadius - 2 - 2 * e.hover), e.shadow);
    p.restore();
  }
  p.drawPixmap(topLeft, e.pixmap);
  p.restore();
}

void PopupWindow::paintLabel(QPainter &p, int index, qreal opacity, bool full)
{
  if (opacity <= 0)
    return;
  const QRect r = labelRect(index, full);
  p.save();
  p.setOpacity(p.opacity() * opacity);
  const bool pill = full || m_config.appearance.mode != QLatin1String("panel");
  if (pill)
  {
    p.setPen(Qt::NoPen);
    p.setBrush(kPillBackground);
    p.drawRoundedRect(QRectF(r), r.height() / 2.0, r.height() / 2.0);
  }
  p.setPen(kPillText);
  p.setFont(m_labelFont);
  p.drawText(r, Qt::AlignCenter, full ? m_entries[size_t(index)].label : elidedLabel(index));
  p.restore();
}

void PopupWindow::paintEvent(QPaintEvent *)
{
  const qreal t = now();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  if (!m_compositing)
    p.fillRect(rect(), QColor(40, 42, 48));

  // Closing: everything fades, and in full motion shrinks slightly.
  if (m_closing && m_motion != Motion::Off)
  {
    const qreal c = std::max(0.0, (t - m_closeStart) / duration(kCloseMs));
    p.setOpacity(1 - ease(QEasingCurve::InQuad, c));
    if (m_motion == Motion::Full)
    {
      const QPointF centre = QRectF(m_layout.grid).center();
      const qreal s = 1 - 0.06 * ease(QEasingCurve::InQuad, c);
      p.translate(centre);
      p.scale(s, s);
      p.translate(-centre);
    }
  }

  if (!m_ghost.isNull())
  {
    const qreal g = levelTime(t) / duration(kLevelMs);
    if (g >= 1)
      m_ghost = QPixmap();
    else
    {
      p.save();
      p.setOpacity(p.opacity() * (1 - ease(QEasingCurve::OutQuad, g)));
      if (m_motion == Motion::Full)
      {
        // Opening: the parent swells away around the Group. Back: the child shrinks into it.
        const QPointF focus = m_ghostFocus - m_layout.window.topLeft();
        const qreal s = m_levelBack ? 1 - 0.25 * ease(QEasingCurve::OutCubic, g)
                                    : 1 + 0.12 * ease(QEasingCurve::OutCubic, g);
        p.translate(focus);
        p.scale(s, s);
        p.translate(-focus);
      }
      p.drawPixmap(m_ghostRect.topLeft() - m_layout.window.topLeft(), m_ghost);
      p.restore();
    }
  }

  if (m_compositing && m_config.appearance.mode == QLatin1String("panel"))
  {
    const qreal fade = m_motion == Motion::Off ? 1 : ease(QEasingCurve::OutCubic, levelTime(t) / duration(kAppearMs));
    paintPanel(p, fade);
  }

  for (size_t i = 0; i < m_entries.size(); ++i)
    paintEntry(p, int(i), t);

  // Labels last, so a hovered label sits above neighbouring icons.
  if (m_labelsAlways)
  {
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
      const qreal a = m_motion == Motion::Off ? 1
                    : ease(QEasingCurve::OutCubic,
                           (levelTime(t) - duration(m_entries[i].appearDelay)) / duration(kAppearMs));
      paintLabel(p, int(i), a * (1 - m_entries[i].hover), false);
    }
  }
  if ((m_labelsHover || m_labelsAlways) && m_compositing)
  {
    for (size_t i = 0; i < m_entries.size(); ++i)
      if (m_entries[i].hover > 0)
        paintLabel(p, int(i), ease(QEasingCurve::OutQuad, m_entries[i].hover), true);
  }

  if (!m_firstFrameSeen)
  {
    m_firstFrameSeen = true;
    if (trace)
      trace("first frame painted");
  }
}

// ---------------------------------------------------------------------------
// Input

bool PopupWindow::event(QEvent *e)
{
  if (e->type() == QEvent::ToolTip)
  {
    // Labels are painted when compositing; tooltips are the fallback without one.
    auto *help = static_cast<QHelpEvent *>(e);
    const int index = hitTest(m_layout, help->pos());
    if (m_compositing || index < 0 || !m_labelsHover)
    {
      QToolTip::hideText();
      e->ignore();
      return true;
    }
    QToolTip::showText(help->globalPos(), m_entries[size_t(index)].label, this,
                       m_layout.cellRects[size_t(index)]);
    return true;
  }
  return QWidget::event(e);
}

void PopupWindow::setHover(int index)
{
  if (index == m_hover)
    return;
  m_hover = index;
  ensureTicking();
}

void PopupWindow::mouseMoveEvent(QMouseEvent *e)
{
  // Window moves (level changes) send moves without the pointer moving; those
  // mustn't steal the keyboard highlight.
  const QPoint global = e->globalPosition().toPoint();
  if (global == m_lastMouse)
    return;
  m_lastMouse = global;
  m_keyboard = false;
  if (!m_closing)
    setHover(hitTest(m_layout, e->position().toPoint()));
}

std::vector<QPoint> PopupWindow::entryCells() const
{
  std::vector<QPoint> cells;
  cells.reserve(m_entries.size());
  for (const Entry &e : m_entries)
    cells.push_back(e.cell);
  return cells;
}

void PopupWindow::highlightOrigin()
{
  setHover(nearestCell(entryCells(), QPoint(0, 0)));
}

void PopupWindow::moveHighlight(QPoint dir)
{
  m_keyboard = true;
  m_leaveTimer.stop();
  QToolTip::hideText();
  // With nothing highlighted yet, start from the cell under the pointer.
  const std::vector<QPoint> cells = entryCells();
  const QPoint from = m_hover >= 0 ? cells[size_t(m_hover)] : QPoint(0, 0);
  const int next = cellInDirection(cells, from, dir);
  if (next >= 0)
    setHover(next);
}

void PopupWindow::mousePressEvent(QMouseEvent *e)
{
  // Remember the press so that the release of the button that *opened* Prowal
  // (bound to a mouse button in the window manager) can't launch anything.
  m_pressed = hitTest(m_layout, e->position().toPoint());
  m_pressedButton = e->button();
  if (!rect().contains(e->position().toPoint()))
    QWidget::mousePressEvent(e);  // outside: let Qt close the popup
}

void PopupWindow::mouseReleaseEvent(QMouseEvent *e)
{
  const int index = hitTest(m_layout, e->position().toPoint());
  const bool matched = m_pressedButton == e->button() && m_pressed == index;
  m_pressed = -1;
  m_pressedButton = Qt::NoButton;
  if (!matched || m_closing)
    return;
  if (e->button() == Qt::RightButton)
  {
    if (m_preview)
    {
      requestClose();
      return;
    }
    openEditor(index);  // on an icon: edit that item; in a gap: just open the editor
    return;
  }
  if (index < 0)
  {
    requestClose();  // click in a gap
    return;
  }

  switch (e->button())
  {
  case Qt::LeftButton:
    activate(index, !m_config.behaviour.stayOpenAfterLaunch);
    break;
  case Qt::MiddleButton:
    activate(index, false);
    break;
  default:
    break;
  }
}

void PopupWindow::activate(int index, bool closeAfter)
{
  Entry &entry = m_entries[size_t(index)];
  if (!entry.item)
  {
    goBack();
    return;
  }
  if (entry.item->type == ItemType::Group)
  {
    enterGroup(index);
    return;
  }
  QString error;
  if (!launchItem(*entry.item, m_terminal, &error))
  {
    std::fprintf(stderr, "prowal: could not start \"%s\": %s\n", qPrintable(entry.item->name),
                 qPrintable(error));
    showNote(index, QStringLiteral("Couldn't start %1:\n%2").arg(entry.item->name, error));
    return;
  }
  // The program is already starting; the animation only acknowledges it.
  if (m_motion == Motion::Full)
  {
    entry.pulseStart = now();
    ensureTicking();
  }
  if (closeAfter)
    requestClose(m_motion == Motion::Full ? duration(kPulseMs) * 0.45 : 0);
}

void PopupWindow::openEditor(int index)
{
  QStringList args{QStringLiteral("--edit"), QStringLiteral("--config"), m_configPath,
                   QStringLiteral("--grid"), m_grid->name};
  QStringList group;
  for (size_t i = 1; i < m_levels.size(); ++i)
    group << QStringLiteral("%1,%2").arg(m_levels[i].cell.x()).arg(m_levels[i].cell.y());
  if (!group.isEmpty())
    args << QStringLiteral("--group") << group.join(QLatin1Char('/'));
  if (index >= 0 && m_entries[size_t(index)].item)
  {
    const QPoint cell = m_entries[size_t(index)].cell;
    args << QStringLiteral("--item") << QStringLiteral("%1,%2").arg(cell.x()).arg(cell.y());
  }
  if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args))
    std::fprintf(stderr, "prowal: could not start the editor\n");
  requestClose();
}

void PopupWindow::showNote(int index, const QString &text)
{
  const QRect cell = m_layout.cellRects[size_t(index)];
  QToolTip::showText(mapToGlobal(cell.bottomLeft()), text, this, cell, 4000);
}

void PopupWindow::keyPressEvent(QKeyEvent *e)
{
  if (m_closing)
    return;
  switch (e->key())
  {
  case Qt::Key_Left: moveHighlight(QPoint(-1, 0)); return;
  case Qt::Key_Right: moveHighlight(QPoint(1, 0)); return;
  case Qt::Key_Up: moveHighlight(QPoint(0, -1)); return;
  case Qt::Key_Down: moveHighlight(QPoint(0, 1)); return;
  case Qt::Key_Home:
    m_keyboard = true;
    highlightOrigin();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    if (m_hover >= 0)
    {
      // Like the mouse: Shift (as middle click does) keeps the popup open.
      const bool keepOpen = (e->modifiers() & Qt::ShiftModifier) || m_config.behaviour.stayOpenAfterLaunch;
      activate(m_hover, !keepOpen);
    }
    return;
  default:
    break;
  }
  if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Backspace)
  {
    if (m_levels.size() > 1)
      goBack();
    else if (e->key() == Qt::Key_Escape)
      requestClose();
    return;
  }
  QWidget::keyPressEvent(e);
}

void PopupWindow::pollPointer()
{
  // Polled rather than relying on enter/leave, which an X pointer grab (or a
  // failed one) makes unreliable. Keyboard users aren't timed out.
  if (m_keyboard)
    return;
  // From the layout, not mapFromGlobal: right after a level change the window
  // system may not have reported the new geometry yet.
  const QPoint local = QCursor::pos() - m_layout.window.topLeft();
  if (m_layout.grid.contains(local))
    m_leaveTimer.stop();
  else
  {
    setHover(-1);
    if (!m_leaveTimer.isActive())
      m_leaveTimer.start(m_config.behaviour.timeoutMs);
  }
}

void PopupWindow::closeEvent(QCloseEvent *e)
{
  // Qt closes popups itself on an outside click; turn that into the fade-out.
  if (!m_closeDone && m_motion != Motion::Off)
  {
    e->ignore();
    requestClose();
    return;
  }
  QWidget::closeEvent(e);
}

void PopupWindow::hideEvent(QHideEvent *e)
{
  QWidget::hideEvent(e);
  m_pollTimer.stop();
  m_preloadTimer.stop();
  m_frameTimer.stop();
  if (closed)
    QTimer::singleShot(0, this, [this] { closed(); });
}

} // namespace prowal
