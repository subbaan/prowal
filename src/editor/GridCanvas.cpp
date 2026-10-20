#include "editor/GridCanvas.h"

#include "config/GridOps.h"
#include "icons/IconLoader.h"
#include "system/Launch.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace prowal {

namespace {

constexpr int kIcon = 48;
constexpr int kPitchX = 88;
constexpr int kPitchY = 84;
constexpr int kIconTop = 8;

} // namespace

GridCanvas::GridCanvas(QWidget *parent) : QWidget(parent)
{
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAutoFillBackground(false);
  setAcceptDrops(true);
}

void GridCanvas::setItems(const std::vector<Item> *items, const QString &terminalPrefix)
{
  m_items = items;
  m_problems.clear();
  m_grey.clear();
  if (m_items)
    for (const Item &item : *m_items)
    {
      QString problem;
      if (item.type == ItemType::Group)
      {
        int inside = 0;
        forEachItem(item.children, [&](const Item &child) {
          if (child.type == ItemType::Launcher &&
              (!commandProblem(child, terminalPrefix).isEmpty() || !workingDirProblem(child).isEmpty()))
            ++inside;
        });
        if (inside > 0)
          problem = tr("%n item(s) inside have problems", nullptr, inside);
      }
      else
      {
        problem = commandProblem(item, terminalPrefix);
        if (problem.isEmpty())
          problem = workingDirProblem(item);
      }
      m_problems.push_back(problem);
      m_grey.push_back(item.type == ItemType::Launcher && !programAvailable(item));
    }
  m_dragIndex = -1;
  m_dragging = false;
  update();
}

void GridCanvas::setSelectedCell(std::optional<QPoint> cell)
{
  m_selected = cell;
  if (cell)
    ensureCellVisible(*cell);
  update();
}

void GridCanvas::select(std::optional<QPoint> cell)
{
  if (cell == m_selected)
    return;
  m_selected = cell;
  update();
  emit cellSelected(cell);
}

// ---------------------------------------------------------------------------
// Geometry

QPoint GridCanvas::cellAt(QPointF pos) const
{
  const QPointF rel = pos - m_pan;
  return QPoint(std::clamp(int(std::floor(rel.x() / kPitchX)), -kMaxCell, kMaxCell),
                std::clamp(int(std::floor(rel.y() / kPitchY)), -kMaxCell, kMaxCell));
}

QRectF GridCanvas::cellRect(QPoint cell) const
{
  return QRectF(m_pan.x() + cell.x() * kPitchX, m_pan.y() + cell.y() * kPitchY, kPitchX, kPitchY);
}

QRectF GridCanvas::iconRect(QPoint cell) const
{
  const QRectF c = cellRect(cell);
  return QRectF(c.left() + (kPitchX - kIcon) / 2.0, c.top() + kIconTop, kIcon, kIcon);
}

int GridCanvas::indexAt(QPoint cell) const
{
  return m_items ? itemIndexAt(*m_items, cell) : -1;
}

void GridCanvas::centreOnOrigin()
{
  m_followCentre = false;
  m_pan = QPointF(width() / 2.0 - kPitchX / 2.0, height() / 2.0 - kPitchY / 2.0);
  update();
}

void GridCanvas::centreOnItems()
{
  // The middle of the layout (which always includes the pointer cell).
  int minX = 0, maxX = 0, minY = 0, maxY = 0;
  if (m_items)
    for (const Item &item : *m_items)
    {
      minX = std::min(minX, item.x);
      maxX = std::max(maxX, item.x);
      minY = std::min(minY, item.y);
      maxY = std::max(maxY, item.y);
    }
  const QPointF middle((minX + maxX + 1) * kPitchX / 2.0, (minY + maxY + 1) * kPitchY / 2.0);
  m_pan = QPointF(width() / 2.0, (height() - 30) / 2.0) - middle;
  update();
}

void GridCanvas::ensureCellVisible(QPoint cell)
{
  if (!isVisible() || width() <= 0)
    return;  // resizeEvent centres first, then calls this again
  const QRectF r = cellRect(cell).adjusted(-8, -8, 8, 8);
  QPointF delta;
  if (r.left() < 0)
    delta.rx() = -r.left();
  else if (r.right() > width())
    delta.rx() = width() - r.right();
  if (r.top() < 0)
    delta.ry() = -r.top();
  else if (r.bottom() > height())
    delta.ry() = height() - r.bottom();
  if (!delta.isNull())
    panBy(delta);
}

void GridCanvas::panBy(QPointF delta)
{
  m_pan += delta;
  m_followCentre = false;
  update();
}

void GridCanvas::resizeEvent(QResizeEvent *)
{
  if (!m_followCentre)
    return;
  centreOnItems();
  if (m_selected)
  {
    ensureCellVisible(*m_selected);
    m_followCentre = true;  // keep centring until the user pans
  }
}

// ---------------------------------------------------------------------------
// Painting

const QPixmap &GridCanvas::iconFor(int index)
{
  const Item &item = (*m_items)[size_t(index)];
  const bool grey = m_grey[size_t(index)];
  QString key = item.icon + QLatin1Char('|') + item.desktopId + (grey ? QStringLiteral("|grey") : QString());
  if (item.type == ItemType::Group)
  {
    // A preview shows the children's icons, so they are part of the key.
    key = QStringLiteral("group|") + item.icon;
    if (m_groupStyle == QLatin1String("preview"))
      for (const Item &child : item.children)
        key += QStringLiteral("|%1,%2,%3").arg(child.x).arg(child.y).arg(child.icon);
  }
  auto it = m_iconCache.find(key);
  if (it == m_iconCache.end())
  {
    QPixmap pm = item.type == ItemType::Group
                   ? groupIcon(item, m_groupStyle, kIcon, devicePixelRatioF(), palette().color(QPalette::Highlight))
                   : loadItemIcon(item, kIcon, devicePixelRatioF()).pixmap;
    if (grey)
      pm = desaturated(pm);
    it = m_iconCache.insert(key, pm);
  }
  return it.value();
}

void GridCanvas::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  const QPalette pal = palette();
  p.fillRect(rect(), pal.color(QPalette::Base));

  // Dot grid at cell corners.
  const QPoint first = cellAt(QPointF(0, 0));
  const QPoint last = cellAt(QPointF(width(), height()));
  QColor dot = pal.color(QPalette::Text);
  dot.setAlpha(55);
  p.setPen(Qt::NoPen);
  p.setBrush(dot);
  for (int y = first.y(); y <= last.y() + 1; ++y)
    for (int x = first.x(); x <= last.x() + 1; ++x)
      p.drawEllipse(cellRect(QPoint(x, y)).topLeft(), 1.3, 1.3);

  const QColor accent = pal.color(QPalette::Highlight);

  // The pointer cell: where the mouse is when the popup opens.
  {
    const QRectF r = cellRect(QPoint(0, 0)).adjusted(3, 3, -3, -3);
    QColor fill = accent;
    fill.setAlpha(28);
    QPen pen(accent, 1.2, Qt::DashLine);
    p.setPen(pen);
    p.setBrush(fill);
    p.drawRoundedRect(r, 10, 10);
    const QPointF c = iconRect(QPoint(0, 0)).center();
    if (indexAt(QPoint(0, 0)) < 0)
    {
      p.setPen(QPen(accent, 1.5));
      p.drawLine(c - QPointF(10, 0), c + QPointF(10, 0));
      p.drawLine(c - QPointF(0, 10), c + QPointF(0, 10));
      p.drawEllipse(c, 5, 5);
    }
  }

  // Legend.
  {
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.85);
    p.setFont(small);
    QColor muted = pal.color(QPalette::Text);
    muted.setAlpha(140);
    p.setPen(muted);
    p.drawText(rect().adjusted(10, 0, -10, -8), Qt::AlignLeft | Qt::AlignBottom | Qt::TextWordWrap,
               m_parentName
                 ? tr("Dashed cell: under the pointer when the group opens (left free, it becomes the "
                      "Back button).   Drag to move · double-click to edit or add · Backspace goes up")
                 : tr("Dashed cell: under the pointer when the popup opens.   Drag to move (onto an icon "
                      "to swap) · double-click to edit, add or open a group · Alt+arrows move · Home recentres"));
  }

  // Selection and hover.
  auto outline = [&](QPoint cell, int alpha, bool dashed) {
    QColor c = accent;
    c.setAlpha(alpha);
    p.setPen(QPen(accent, 1.5, dashed ? Qt::DashLine : Qt::SolidLine));
    p.setBrush(c);
    p.drawRoundedRect(cellRect(cell).adjusted(4, 4, -4, -4), 9, 9);
  };
  if (m_hoverCell && m_hoverCell != m_selected && !m_dragging)
  {
    QColor c = pal.color(QPalette::Text);
    c.setAlpha(18);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(cellRect(*m_hoverCell).adjusted(4, 4, -4, -4), 9, 9);
  }
  if (m_selected)
  {
    const bool empty = indexAt(*m_selected) < 0;
    outline(*m_selected, empty ? 20 : 70, empty);
    if (empty)
    {
      const QPointF c = iconRect(*m_selected).center();
      p.setPen(QPen(accent, 2));
      p.drawLine(c - QPointF(8, 0), c + QPointF(8, 0));
      p.drawLine(c - QPointF(0, 8), c + QPointF(0, 8));
    }
  }

  if (!m_items)
    return;

  // Items.
  const QFontMetrics fm(font());
  for (size_t i = 0; i < m_items->size(); ++i)
  {
    const Item &item = (*m_items)[i];
    const QPoint cell(item.x, item.y);
    if (m_dragging && int(i) == m_dragIndex)
      continue;
    if (int(i) != itemIndexAt(*m_items, cell))
      continue;  // a duplicate hidden under the first item in its cell

    const QRectF icon = iconRect(cell);
    const QPixmap &pm = iconFor(int(i));
    const QSizeF logical = QSizeF(pm.size()) / pm.devicePixelRatio();
    p.drawPixmap(QPointF(icon.center().x() - logical.width() / 2, icon.center().y() - logical.height() / 2), pm);

    if (!m_problems[i].isEmpty())
    {
      p.setPen(QPen(pal.color(QPalette::Base), 2));
      p.setBrush(QColor(230, 160, 40));
      p.drawEllipse(QPointF(icon.right() - 2, icon.top() + 2), 6, 6);
    }

    const QRectF label(cellRect(cell).left() + 2, icon.bottom() + 3, kPitchX - 4, fm.height());
    p.setPen(pal.color(QPalette::Text));
    p.setFont(font());
    p.drawText(label, Qt::AlignHCenter | Qt::AlignTop,
               item.type == ItemType::Group
                 ? fm.elidedText(item.name, Qt::ElideRight, int(label.width()) - fm.horizontalAdvance(QStringLiteral(" \u203A"))) +
                     QStringLiteral(" \u203A")
                 : fm.elidedText(item.name.isEmpty() ? item.command : item.name, Qt::ElideRight, int(label.width())));
  }

  // Files being dropped from outside (or from the app picker).
  if (m_dropCell)
  {
    QColor fill = accent;
    fill.setAlpha(45);
    p.setPen(QPen(accent, 2));
    p.setBrush(fill);
    p.drawRoundedRect(cellRect(*m_dropCell).adjusted(3, 3, -3, -3), 10, 10);
    p.setPen(accent);
    p.drawText(cellRect(*m_dropCell).adjusted(0, 0, 0, -4), Qt::AlignHCenter | Qt::AlignBottom,
               m_dropSetsIcon ? tr("set icon") : tr("add"));
  }

  // Item being dragged, and where it will land.
  if (m_dragging && m_dragIndex >= 0)
  {
    const QPoint target = cellAt(m_lastPos);
    const int other = indexAt(target);
    const bool swap = other >= 0 && other != m_dragIndex;
    const QColor c = swap ? QColor(230, 160, 40) : accent;
    QColor fill = c;
    fill.setAlpha(45);
    p.setPen(QPen(c, 2));
    p.setBrush(fill);
    p.drawRoundedRect(cellRect(target).adjusted(3, 3, -3, -3), 10, 10);
    if (swap)
    {
      p.setPen(c);
      p.drawText(cellRect(target).adjusted(0, 0, 0, -4), Qt::AlignHCenter | Qt::AlignBottom, tr("swap"));
    }
    const QPixmap &pm = iconFor(m_dragIndex);
    const QSizeF logical = QSizeF(pm.size()) / pm.devicePixelRatio();
    p.setOpacity(0.85);
    p.drawPixmap(m_lastPos - QPointF(logical.width() / 2, logical.height() / 2), pm);
    p.setOpacity(1);
  }
}

// ---------------------------------------------------------------------------
// Input

void GridCanvas::mousePressEvent(QMouseEvent *e)
{
  setFocus();
  m_pressPos = m_lastPos = e->position();
  const QPoint cell = cellAt(e->position());
  if (e->button() == Qt::LeftButton)
  {
    select(cell);
    m_dragIndex = indexAt(cell);
    m_panning = m_dragIndex < 0;
  }
  else if (e->button() == Qt::MiddleButton)
    m_panning = true;
  if (m_panning)
    setCursor(Qt::ClosedHandCursor);
}

void GridCanvas::mouseMoveEvent(QMouseEvent *e)
{
  const QPointF pos = e->position();
  if (m_panning)
    panBy(pos - m_lastPos);
  else if (m_dragIndex >= 0 && (e->buttons() & Qt::LeftButton))
  {
    if (!m_dragging && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
    {
      m_dragging = true;
      setCursor(Qt::DragMoveCursor);
    }
    if (m_dragging)
      update();
  }
  else
  {
    const QPoint cell = cellAt(pos);
    if (cell != m_hoverCell)
    {
      m_hoverCell = cell;
      update();
    }
  }
  m_lastPos = pos;
}

void GridCanvas::mouseReleaseEvent(QMouseEvent *e)
{
  if (m_dragging)
  {
    const QPoint target = cellAt(e->position());
    const int index = m_dragIndex;
    m_dragging = false;
    m_dragIndex = -1;
    m_selected = target;
    emit moveRequested(index, target);
    emit cellSelected(target);
  }
  m_dragIndex = -1;
  m_panning = false;
  unsetCursor();
  update();
}

void GridCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
  if (e->button() != Qt::LeftButton)
    return;
  const QPoint cell = cellAt(e->position());
  const int index = indexAt(cell);
  if (index >= 0 && (*m_items)[size_t(index)].type == ItemType::Group)
    emit openRequested(index);
  else if (index >= 0)
    emit editRequested(index);
  else
    emit addRequested(cell);
}

void GridCanvas::wheelEvent(QWheelEvent *e)
{
  QPointF delta = QPointF(e->angleDelta()) / 120.0 * 40.0;
  if (e->modifiers() & Qt::ShiftModifier)
    delta = QPointF(delta.y(), delta.x());
  panBy(delta);
}

void GridCanvas::keyPressEvent(QKeyEvent *e)
{
  if (e->key() == Qt::Key_Escape && m_dragging)
  {
    m_dragging = false;
    m_dragIndex = -1;
    unsetCursor();
    update();
    return;
  }
  if (e->key() == Qt::Key_Home)
  {
    centreOnOrigin();
    return;
  }

  QPoint step;
  switch (e->key())
  {
  case Qt::Key_Left: step = QPoint(-1, 0); break;
  case Qt::Key_Right: step = QPoint(1, 0); break;
  case Qt::Key_Up: step = QPoint(0, -1); break;
  case Qt::Key_Down: step = QPoint(0, 1); break;
  default: break;
  }
  if (!step.isNull())
  {
    const QPoint from = m_selected.value_or(QPoint(0, 0));
    QPoint to = m_selected ? from + step : from;
    to = QPoint(std::clamp(to.x(), -kMaxCell, kMaxCell), std::clamp(to.y(), -kMaxCell, kMaxCell));
    const int index = indexAt(from);
    if ((e->modifiers() & Qt::AltModifier) && index >= 0)
      emit moveRequested(index, to);  // Alt+arrow carries the item along
    select(to);
    ensureCellVisible(to);
    return;
  }

  const int index = m_selected ? indexAt(*m_selected) : -1;
  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_selected)
  {
    if (index >= 0 && (*m_items)[size_t(index)].type == ItemType::Group)
      emit openRequested(index);
    else if (index >= 0)
      emit editRequested(index);
    else
      emit addRequested(*m_selected);
    return;
  }
  QWidget::keyPressEvent(e);
}

void GridCanvas::contextMenuEvent(QContextMenuEvent *e)
{
  const QPoint cell = cellAt(e->pos());
  select(cell);
  const int index = indexAt(cell);
  QMenu menu(this);
  if (index >= 0)
  {
    const Item &item = (*m_items)[size_t(index)];
    const bool group = item.type == ItemType::Group;
    if (group)
      menu.addAction(tr("Open group"), this, [this, index] { emit openRequested(index); });
    menu.addAction(tr("Edit"), this, [this, index] { emit editRequested(index); });
    if (!group)
      menu.addAction(tr("Test launch"), this, [this, index] { emit testRequested(index); });

    // Other levels this item can move to: up a level, or into a Group beside it.
    QMenu *move = menu.addMenu(tr("Move to"));
    if (m_parentName)
      move->addAction(tr("Up to %1").arg(*m_parentName), this, [this, index] { emit moveUpRequested(index); });
    for (size_t i = 0; i < m_items->size(); ++i)
    {
      const Item &other = (*m_items)[i];
      if (other.type != ItemType::Group || int(i) == index || int(i) != indexAt(QPoint(other.x, other.y)))
        continue;
      const QPoint target(other.x, other.y);
      move->addAction(tr("Into %1").arg(other.name.isEmpty() ? tr("(unnamed group)") : other.name), this,
                      [this, index, target] { emit moveIntoRequested(index, target); });
    }
    move->setEnabled(!move->isEmpty());
    if (move->isEmpty())
      move->setTitle(tr("Move to (no groups here)"));
    menu.addSeparator();
    menu.addAction(group ? tr("Delete group") : tr("Delete"), this, [this, index] { emit deleteRequested(index); });
  }
  else
  {
    menu.addAction(tr("Add launcher here"), this, [this, cell] { emit addRequested(cell); });
    menu.addAction(tr("Add group here"), this, [this, cell] { emit addGroupRequested(cell); });
    menu.addAction(tr("Add from installed apps…"), this, [this, cell] { emit appsRequested(cell); });
  }
  menu.exec(e->globalPos());
}

namespace {

QStringList localFiles(const QMimeData *mime)
{
  QStringList files;
  if (mime->hasUrls())
    for (const QUrl &url : mime->urls())
      if (url.isLocalFile())
        files << url.toLocalFile();
  return files;
}

} // namespace

void GridCanvas::dragEnterEvent(QDragEnterEvent *e)
{
  if (localFiles(e->mimeData()).isEmpty())
    return;
  e->acceptProposedAction();
  dragMoveEvent(e);
}

void GridCanvas::dragMoveEvent(QDragMoveEvent *e)
{
  const QStringList files = localFiles(e->mimeData());
  const QPoint cell = cellAt(e->position());
  // A single image dropped on an icon replaces that icon's image.
  m_dropSetsIcon = files.size() == 1 && isImageFile(files.first()) && indexAt(cell) >= 0;
  if (cell != m_dropCell)
  {
    m_dropCell = cell;
    update();
  }
  e->acceptProposedAction();
}

void GridCanvas::dragLeaveEvent(QDragLeaveEvent *)
{
  m_dropCell.reset();
  update();
}

void GridCanvas::dropEvent(QDropEvent *e)
{
  const QStringList files = localFiles(e->mimeData());
  const QPoint cell = cellAt(e->position());
  m_dropCell.reset();
  update();
  if (files.isEmpty())
    return;
  e->acceptProposedAction();
  select(cell);
  emit filesDropped(cell, files);
}

void GridCanvas::leaveEvent(QEvent *)
{
  m_hoverCell.reset();
  update();
}

} // namespace prowal
