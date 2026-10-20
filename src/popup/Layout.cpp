#include "popup/Layout.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace prowal {

namespace {

// Position of a span of length `size` starting at `pos`, moved to lie within
// [lo, lo + range). A span too large to fit stays where it is, so the origin
// cell stays under the pointer; the window is clipped to the monitor instead.
int clampSpan(int pos, int size, int lo, int range)
{
  if (size >= range)
    return pos;
  return std::clamp(pos, lo, lo + range - size);
}

} // namespace

LayoutResult computeLayout(const LayoutInput &in)
{
  LayoutResult r;
  r.pitchX = in.iconSize + in.spacing;
  r.pitchY = in.iconSize + in.spacing + in.labelHeight;
  if (in.cells.empty())
  {
    r.window = QRect(in.cursor, QSize(1, 1));
    return r;
  }

  int minX = in.cells.front().x(), maxX = minX;
  int minY = in.cells.front().y(), maxY = minY;
  for (const QPoint &c : in.cells)
  {
    minX = std::min(minX, c.x());
    maxX = std::max(maxX, c.x());
    minY = std::min(minY, c.y());
    maxY = std::max(maxY, c.y());
  }

  const QSize gridSize((maxX - minX + 1) * r.pitchX, (maxY - minY + 1) * r.pitchY);
  r.grid = QRect(QPoint(in.margins.left(), in.margins.top()), gridSize);

  // Global top-left of the grid if the origin cell's icon is centred on the cursor.
  const int iconCentreY = (in.iconSize + in.spacing) / 2;
  const QPoint originCentre(r.grid.left() + (0 - minX) * r.pitchX + r.pitchX / 2,
                            r.grid.top() + (0 - minY) * r.pitchY + iconCentreY);
  QPoint gridTopLeft = in.cursor - originCentre + r.grid.topLeft();

  if (in.available.isValid())
  {
    gridTopLeft.setX(clampSpan(gridTopLeft.x(), gridSize.width(), in.available.left(), in.available.width()));
    gridTopLeft.setY(clampSpan(gridTopLeft.y(), gridSize.height(), in.available.top(), in.available.height()));
  }

  r.window = QRect(gridTopLeft - r.grid.topLeft(),
                   gridSize.grownBy(in.margins));

  r.cellRects.reserve(in.cells.size());
  for (const QPoint &c : in.cells)
    r.cellRects.emplace_back(r.grid.left() + (c.x() - minX) * r.pitchX,
                             r.grid.top() + (c.y() - minY) * r.pitchY, r.pitchX, r.pitchY);

  // A grid bigger than the monitor: keep the window to the monitor (plus margins),
  // so it never needs a huge buffer. Cells outside it simply aren't visible.
  if (in.available.isValid())
  {
    const QRect limit = in.available.marginsAdded(in.margins);
    const QRect clipped = r.window.intersected(limit);
    if (clipped != r.window && !clipped.isEmpty())
    {
      const QPoint shift = r.window.topLeft() - clipped.topLeft();
      r.window = clipped;
      r.grid.translate(shift);
      r.grid = r.grid.intersected(QRect(QPoint(0, 0), clipped.size()));
      for (QRect &cell : r.cellRects)
        cell.translate(shift);
    }
  }
  return r;
}

int hitTest(const LayoutResult &layout, QPoint p)
{
  for (size_t i = 0; i < layout.cellRects.size(); ++i)
    if (layout.cellRects[i].contains(p))
      return int(i);
  return -1;
}

int nearestCell(const std::vector<QPoint> &cells, QPoint target)
{
  int best = -1;
  int bestDistance = 0;
  for (size_t i = 0; i < cells.size(); ++i)
  {
    const QPoint d = cells[i] - target;
    const int distance = d.x() * d.x() + d.y() * d.y();
    // Ties go to reading order (up, then left), which is predictable.
    if (best < 0 || distance < bestDistance ||
        (distance == bestDistance && std::pair(cells[i].y(), cells[i].x()) <
                                       std::pair(cells[size_t(best)].y(), cells[size_t(best)].x())))
    {
      best = int(i);
      bestDistance = distance;
    }
  }
  return best;
}

int cellInDirection(const std::vector<QPoint> &cells, QPoint origin, QPoint dir)
{
  int best = -1;
  int bestScore = 0, bestSide = 0;
  for (size_t i = 0; i < cells.size(); ++i)
  {
    const QPoint d = cells[i] - origin;
    const int ahead = d.x() * dir.x() + d.y() * dir.y();
    if (ahead <= 0)
      continue;
    const int side = std::abs(d.x() * dir.y() - d.y() * dir.x());
    // Sideways counts double, so the same row/column wins over a nearer diagonal.
    const int score = ahead + 2 * side;
    if (best < 0 || score < bestScore || (score == bestScore && side < bestSide))
    {
      best = int(i);
      bestScore = score;
      bestSide = side;
    }
  }
  return best;
}

} // namespace prowal
