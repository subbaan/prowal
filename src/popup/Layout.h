#pragma once

#include <QMargins>
#include <QPoint>
#include <QRect>

#include <vector>

namespace prowal {

struct LayoutInput
{
  std::vector<QPoint> cells;  // grid cell of each item
  int iconSize = 48;
  int spacing = 8;
  int labelHeight = 0;        // extra height per row for always-visible labels
  QMargins margins;           // room around the grid (shadows, glow, hover labels)
  QPoint cursor;              // global position, logical pixels
  QRect available;            // available geometry of the monitor under the cursor
};

struct LayoutResult
{
  QRect window;                  // global window geometry
  QRect grid;                    // window-local rect covering every cell
  std::vector<QRect> cellRects;  // window-local, one per input cell (full pitch box)
  int pitchX = 0;
  int pitchY = 0;
};

// Centres cell (0, 0) on the cursor, then shifts the whole grid - never reflows
// it - so every cell lands on the cursor's monitor. The margins may fall off-screen.
// A grid larger than the monitor isn't shifted on that axis; the window is
// clipped to the monitor (plus margins) instead.
// The cursor sits on the centre of the origin cell's icon, not of its label.
LayoutResult computeLayout(const LayoutInput &in);

// Index into cellRects of the cell containing local point p, or -1.
int hitTest(const LayoutResult &layout, QPoint p);

// Keyboard navigation over grid cells (indices into `cells`, -1 for none).
// The cell nearest `target` (the origin cell itself when it is used).
int nearestCell(const std::vector<QPoint> &cells, QPoint target);
// The best cell in direction `dir` (one of the four unit steps) from the cell
// position `from` (which needn't be used): skips over gaps, prefers staying in
// the same row/column. -1 if there is none.
int cellInDirection(const std::vector<QPoint> &cells, QPoint from, QPoint dir);

} // namespace prowal
