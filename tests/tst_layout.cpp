#include "popup/Layout.h"

#include <QTest>

using namespace prowal;

namespace {

LayoutInput input(std::vector<QPoint> cells, QPoint cursor, QRect available)
{
  LayoutInput in;
  in.cells = std::move(cells);
  in.iconSize = 48;
  in.spacing = 8;  // pitch 56
  in.margins = QMargins(10, 10, 10, 10);
  in.cursor = cursor;
  in.available = available;
  return in;
}

QPoint globalCentre(const LayoutResult &r, size_t i)
{
  return r.window.topLeft() + r.cellRects[i].topLeft() + QPoint(r.pitchX / 2, 28);
}

const QRect kScreen(0, 0, 2560, 1440);

} // namespace

class TestLayout : public QObject
{
  Q_OBJECT

private slots:
  void originUnderCursor()
  {
    const LayoutResult r = computeLayout(input({{0, 0}, {-2, -2}, {4, 2}}, {1000, 700}, kScreen));
    QCOMPARE(globalCentre(r, 0), QPoint(1000, 700));
    QCOMPARE(globalCentre(r, 1), QPoint(1000 - 112, 700 - 112));
    QCOMPARE(globalCentre(r, 2), QPoint(1000 + 224, 700 + 112));
    QCOMPARE(r.window.size(), QSize(7 * 56 + 20, 5 * 56 + 20));
    QCOMPARE(r.grid.topLeft(), QPoint(10, 10));
  }

  void originCellEmpty()
  {
    // No item at (0, 0): the others still sit where they would around the cursor.
    const LayoutResult r = computeLayout(input({{2, 1}, {3, 1}}, {1000, 700}, kScreen));
    QCOMPARE(globalCentre(r, 0), QPoint(1000 + 112, 700 + 56));
  }

  void clampShiftsWholeGrid()
  {
    // Near the bottom-right corner: grid shifts up/left, spacing unchanged.
    const LayoutResult r = computeLayout(input({{0, 0}, {1, 0}, {0, 1}}, {2550, 1435}, kScreen));
    const QRect gridGlobal = r.grid.translated(r.window.topLeft());
    QCOMPARE(gridGlobal.right(), kScreen.right());
    QCOMPARE(gridGlobal.bottom(), kScreen.bottom());
    QCOMPARE(globalCentre(r, 1) - globalCentre(r, 0), QPoint(56, 0));
    QCOMPARE(globalCentre(r, 2) - globalCentre(r, 0), QPoint(0, 56));
  }

  void clampTopLeft()
  {
    const LayoutResult r = computeLayout(input({{0, 0}, {-3, -3}}, {5, 5}, kScreen));
    QCOMPARE(r.grid.translated(r.window.topLeft()).topLeft(), QPoint(0, 0));
    // The shadow margin may hang off-screen.
    QCOMPARE(r.window.topLeft(), QPoint(-10, -10));
  }

  void secondMonitor()
  {
    // Cursor on a monitor to the right; must not spill back onto the first one.
    const QRect right(2560, 0, 1920, 1080);
    const LayoutResult r = computeLayout(input({{0, 0}, {-3, 0}}, {2570, 500}, right));
    QCOMPARE(r.grid.translated(r.window.topLeft()).left(), 2560);
  }

  void labelsAndUnevenMargins()
  {
    LayoutInput in = input({{0, 0}, {0, 1}, {-1, -1}}, {1000, 700}, kScreen);
    in.labelHeight = 16;  // pitchY 72
    in.margins = QMargins(30, 10, 30, 40);
    const LayoutResult r = computeLayout(in);
    QCOMPARE(r.pitchY, 72);
    QCOMPARE(globalCentre(r, 0), QPoint(1000, 700));  // the icon, not the label, is under the cursor
    QCOMPARE(globalCentre(r, 1), QPoint(1000, 772));
    QCOMPARE(globalCentre(r, 2), QPoint(944, 628));
    QCOMPARE(r.window.size(), QSize(2 * 56 + 60, 3 * 72 + 50));
    QCOMPARE(r.grid.topLeft(), QPoint(30, 10));
  }

  void hitTesting()
  {
    const LayoutResult r = computeLayout(input({{0, 0}, {1, 0}}, {1000, 700}, kScreen));
    QCOMPARE(hitTest(r, r.cellRects[0].center()), 0);
    QCOMPARE(hitTest(r, r.cellRects[1].center()), 1);
    QCOMPARE(hitTest(r, QPoint(1, 1)), -1);  // margin
  }

  void empty()
  {
    const LayoutResult r = computeLayout(input({}, {10, 10}, kScreen));
    QVERIFY(r.cellRects.empty());
    QCOMPARE(hitTest(r, {10, 10}), -1);
  }

  void hugeGridIsClipped()
  {
    // Cells far apart: the window stays within the monitor plus margins, and the
    // origin is still under the cursor.
    const LayoutResult r = computeLayout(input({{0, 0}, {-99, -99}, {99, 99}}, {1280, 720}, kScreen));
    QVERIFY(kScreen.marginsAdded(QMargins(10, 10, 10, 10)).contains(r.window));
    QCOMPARE(globalCentre(r, 0), QPoint(1280, 720));
    QCOMPARE(hitTest(r, QPoint(1280, 720) - r.window.topLeft()), 0);
  }

  void keyboardNavigation()
  {
    //      x: -1  0  1  2
    // y -1:    a  .  b  .
    // y  0:    c  .  .  d
    // y  1:    .  e  .  .
    const std::vector<QPoint> cells{{-1, -1}, {1, -1}, {-1, 0}, {2, 0}, {0, 1}};
    // Origin empty: nearest wins, ties go up then left.
    QCOMPARE(nearestCell(cells, {0, 0}), 2);   // c (-1,0) and e (0,1) both at 1; c is higher
    QCOMPARE(nearestCell({}, {0, 0}), -1);
    QCOMPARE(nearestCell(cells, {2, 0}), 3);
    // Right from c skips the gap to d in the same row rather than the nearer diagonal e.
    QCOMPARE(cellInDirection(cells, cells[2], {1, 0}), 3);
    QCOMPARE(cellInDirection(cells, cells[2], {-1, 0}), -1);  // nothing further left
    QCOMPARE(cellInDirection(cells, cells[2], {0, -1}), 0);   // up to a
    QCOMPARE(cellInDirection(cells, cells[0], {1, 0}), 1);    // a -> b
    QCOMPARE(cellInDirection(cells, cells[4], {0, -1}), 2);   // e up: c scores 1+2, a and b 2+2
    QCOMPARE(cellInDirection(cells, cells[3], {0, 1}), 4);    // d down: only e
    // From an empty cell (the origin with nothing in it).
    QCOMPARE(cellInDirection(cells, {0, 0}, {1, 0}), 3);   // d, same row
    QCOMPARE(cellInDirection(cells, {0, 0}, {0, 1}), 4);   // e
    QCOMPARE(cellInDirection(cells, {0, 0}, {0, -1}), 0);  // a and b tie; the first listed wins
    QCOMPARE(cellInDirection({}, {0, 0}, {0, 1}), -1);
  }
};

QTEST_GUILESS_MAIN(TestLayout)
#include "tst_layout.moc"
