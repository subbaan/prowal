#pragma once

#include "config/Model.h"

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QWidget>

#include <optional>

namespace prowal {

// Shows one grid around the pointer cell and turns mouse/keyboard gestures into
// requests. It never changes the items itself: the editor applies requests
// through the Document (so they can be undone) and then calls setItems again.
class GridCanvas : public QWidget
{
  Q_OBJECT

public:
  explicit GridCanvas(QWidget *parent = nullptr);

  // items must stay valid until the next setItems call.
  void setItems(const std::vector<Item> *items, const QString &terminalPrefix);
  void setGroupStyle(const QString &style) { m_groupStyle = style; clearIconCache(); }
  void clearIconCache() { m_iconCache.clear(); update(); }
  // Inside a Group: the name of the level above, offered by "Move up to".
  void setParentName(std::optional<QString> name) { m_parentName = std::move(name); }
  void setSelectedCell(std::optional<QPoint> cell);
  std::optional<QPoint> selectedCell() const { return m_selected; }
  void centreOnOrigin();
  void centreOnItems();
  void ensureCellVisible(QPoint cell);

  QSize sizeHint() const override { return QSize(720, 560); }

signals:
  void cellSelected(std::optional<QPoint> cell);
  void moveRequested(int index, QPoint cell);
  void addRequested(QPoint cell);
  void editRequested(int index);
  void deleteRequested(int index);
  void testRequested(int index);
  void appsRequested(QPoint cell);
  void openRequested(int index);
  void addGroupRequested(QPoint cell);
  void moveIntoRequested(int index, QPoint groupCell);
  void moveUpRequested(int index);
  void filesDropped(QPoint cell, const QStringList &paths);

protected:
  void paintEvent(QPaintEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dragMoveEvent(QDragMoveEvent *e) override;
  void dragLeaveEvent(QDragLeaveEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private:
  QPoint cellAt(QPointF pos) const;
  QRectF cellRect(QPoint cell) const;
  QRectF iconRect(QPoint cell) const;
  int indexAt(QPoint cell) const;
  const QPixmap &iconFor(int index);
  void select(std::optional<QPoint> cell);
  void panBy(QPointF delta);

  const std::vector<Item> *m_items = nullptr;
  std::vector<QString> m_problems;  // per item; empty = fine
  std::vector<bool> m_grey;         // per item: program not installed
  QHash<QString, QPixmap> m_iconCache;
  QString m_groupStyle = QStringLiteral("badge");
  std::optional<QString> m_parentName;

  QPointF m_pan;            // widget position of the origin cell's top-left
  bool m_followCentre = true;
  std::optional<QPoint> m_selected;
  std::optional<QPoint> m_hoverCell;

  int m_dragIndex = -1;
  bool m_dragging = false;
  bool m_panning = false;
  std::optional<QPoint> m_dropCell;
  bool m_dropSetsIcon = false;
  QPointF m_pressPos;
  QPointF m_lastPos;
};

} // namespace prowal
