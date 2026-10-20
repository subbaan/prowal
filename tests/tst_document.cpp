#include "config/Config.h"
#include "config/GridOps.h"
#include "editor/Document.h"
#include "system/Launch.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace prowal;

namespace {

Item launcher(const QString &name, int x, int y)
{
  Item item;
  item.name = name;
  item.command = name.toLower();
  item.x = x;
  item.y = y;
  return item;
}

Config sample()
{
  Config c = defaultConfig("kitty");
  c.grids[0].items = {launcher("A", 0, 0), launcher("B", 1, 0), launcher("C", -1, -1)};
  return c;
}

QPoint cellOf(const Document &doc, int index)
{
  const Item &it = doc.config().grids[0].items[size_t(index)];
  return QPoint(it.x, it.y);
}

} // namespace

class TestDocument : public QObject
{
  Q_OBJECT

private slots:
  void gridOps()
  {
    std::vector<Item> items = sample().grids[0].items;
    QCOMPARE(itemIndexAt(items, {1, 0}), 1);
    QCOMPARE(itemIndexAt(items, {5, 5}), -1);
    QCOMPARE(freeCellNear(items, {0, 0}), QPoint(0, -1));  // ring 1, reading order
    QCOMPARE(moveOrSwap(items, 0, {1, 0}), 1);  // A and B swap
    QCOMPARE(QPoint(items[0].x, items[0].y), QPoint(1, 0));
    QCOMPARE(QPoint(items[1].x, items[1].y), QPoint(0, 0));
    QCOMPARE(moveOrSwap(items, 2, {3, 3}), -1);
    QCOMPARE(uniqueGridName(sample(), "main"), QString("main 2"));
    QCOMPARE(uniqueGridName(sample(), "games"), QString("games"));
  }

  void moveSwapUndoRedo()
  {
    Document doc(sample(), "/nonexistent/prowal.json");
    QSignalSpy changed(&doc, &Document::changed);
    QVERIFY(!doc.isModified());

    doc.moveItem(0, {}, 0, {1, 0});
    QCOMPARE(cellOf(doc, 0), QPoint(1, 0));
    QCOMPARE(cellOf(doc, 1), QPoint(0, 0));
    QVERIFY(doc.isModified());
    QCOMPARE(doc.undoStack()->undoText(), QString("Swap A and B"));

    doc.moveItem(0, {}, 2, {4, 4});
    QCOMPARE(doc.undoStack()->undoText(), QString("Move C"));

    doc.undoStack()->undo();
    doc.undoStack()->undo();
    QCOMPARE(cellOf(doc, 0), QPoint(0, 0));
    QCOMPARE(cellOf(doc, 2), QPoint(-1, -1));
    QVERIFY(!doc.isModified());

    doc.undoStack()->redo();
    QCOMPARE(cellOf(doc, 0), QPoint(1, 0));
    QVERIFY(changed.count() >= 5);

    const int steps = doc.undoStack()->count();
    doc.moveItem(0, {}, 0, {1, 0});  // already there: no step
    QCOMPARE(doc.undoStack()->count(), steps);
  }

  void addEditDelete()
  {
    Document doc(sample(), "/nonexistent/prowal.json");
    doc.addItem(0, {}, launcher("D", 2, 2));
    QCOMPARE(doc.config().grids[0].items.size(), size_t(4));

    Item edited = doc.config().grids[0].items[3];
    edited.command = "d --fast";
    doc.editItem(0, {}, 3, edited);
    QCOMPARE(doc.config().grids[0].items[3].command, QString("d --fast"));
    const int steps = doc.undoStack()->count();
    doc.editItem(0, {}, 3, edited);  // unchanged: no step
    QCOMPARE(doc.undoStack()->count(), steps);

    doc.deleteItem(0, {}, 1);
    QCOMPARE(doc.config().grids[0].items.size(), size_t(3));
    QCOMPARE(doc.config().grids[0].items[1].name, QString("C"));

    doc.undoStack()->undo();  // delete
    doc.undoStack()->undo();  // edit
    QCOMPARE(doc.config().grids[0].items[1].name, QString("B"));
    QCOMPARE(doc.config().grids[0].items[3].command, QString("d"));
  }

  void groups()
  {
    Config c = sample();
    Item games;
    games.type = ItemType::Group;
    games.name = "Games";
    games.x = 2;
    games.y = 0;
    Item emu;
    emu.type = ItemType::Group;
    emu.name = "Emulators";
    emu.children = {launcher("Mame", 0, 0)};
    games.children = {launcher("Doom", 0, 0), emu};
    c.grids[0].items.push_back(games);

    // Paths and their text form.
    QCOMPARE(parseGroupPath("2,0/0,0"), std::optional<GroupPath>(GroupPath{{2, 0}, {0, 0}}));
    QCOMPARE(parseGroupPath(""), std::optional<GroupPath>(GroupPath{}));
    QVERIFY(!parseGroupPath("2,x"));
    QCOMPARE(formatGroupPath({{2, 0}, {-1, 3}}), QString("2,0/-1,3"));
    QCOMPARE(countItems(c.grids[0].items), 7);

    Document doc(c, "/nonexistent/prowal.json");
    const GroupPath inGames{{2, 0}};
    const GroupPath inEmu{{2, 0}, {0, 0}};
    QVERIFY(!doc.items(0, inEmu));  // (0,0) in Games is Doom, a launcher
    QCOMPARE(groupNames(doc.config().grids[0].items, inGames), QStringList{"Games"});

    // Edits inside a group.
    doc.addItem(0, inGames, launcher("Quake", 1, 0));
    QCOMPARE(doc.items(0, inGames)->size(), size_t(3));
    doc.moveItem(0, inGames, 0, {5, 5});
    QCOMPARE(doc.items(0, inGames)->at(0).x, 5);
    QCOMPARE(doc.undoStack()->undoText(), QString("Move Doom"));

    // Emulators sits at (0,0) inside Games now that Doom moved.
    QVERIFY(doc.items(0, inEmu));
    QCOMPARE(doc.items(0, inEmu)->at(0).name, QString("Mame"));

    // Move a top-level item into Games: it keeps its cell when free.
    const auto landed = doc.moveToLevel(0, {}, 2, inGames);  // C at (-1,-1)
    QCOMPARE(landed, std::optional<QPoint>(QPoint(-1, -1)));
    QCOMPARE(doc.config().grids[0].items.size(), size_t(3));
    QCOMPARE(doc.undoStack()->undoText(), QString("Move C into Games"));
    // ...and back up, onto the nearest free cell.
    QVERIFY(doc.moveToLevel(0, inGames, 3, {}));
    QCOMPARE(doc.config().grids[0].items.size(), size_t(4));

    // A group can't go into itself or its own children.
    const int games2 = itemIndexAt(doc.config().grids[0].items, {2, 0});
    QVERIFY(!doc.moveToLevel(0, {}, games2, inGames));
    QVERIFY(!doc.moveToLevel(0, {}, games2, inEmu));

    // Undo back to the start; the removed path becomes a no-op.
    while (doc.undoStack()->canUndo())
      doc.undoStack()->undo();
    QCOMPARE(doc.items(0, inGames)->size(), size_t(2));
    const int steps = doc.undoStack()->count();
    doc.addItem(0, {{9, 9}}, launcher("X", 0, 0));  // no group there
    QCOMPARE(doc.undoStack()->count(), steps);
  }

  void settings()
  {
    Document doc(sample(), "/nonexistent/prowal.json");
    Behaviour b = doc.config().behaviour;
    Appearance a = doc.config().appearance;
    doc.setSettings(b, a);  // unchanged: no step
    QCOMPARE(doc.undoStack()->count(), 0);
    b.terminal = "kitty";
    a.groupStyle = "preview";
    doc.setSettings(b, a);
    QCOMPARE(doc.config().behaviour.terminal, QString("kitty"));
    QCOMPARE(doc.undoStack()->undoText(), QString("Change preferences"));
    // Settings and grid edits undo independently, in order.
    doc.moveItem(0, {}, 0, {7, 7});
    doc.undoStack()->undo();
    QCOMPARE(doc.config().appearance.groupStyle, QString("preview"));
    doc.undoStack()->undo();
    QCOMPARE(doc.config().appearance.groupStyle, QString("badge"));
    QVERIFY(!doc.isModified());
  }

  void grids()
  {
    Document doc(sample(), "/nonexistent/prowal.json");
    QCOMPARE(doc.addGrid("games"), 1);
    QCOMPARE(doc.duplicateGrid(0, "work"), 2);
    QCOMPARE(doc.config().grids[2].items.size(), size_t(3));
    doc.renameGrid(1, "play");
    QVERIFY(doc.config().findGrid("play"));
    doc.deleteGrid(1);
    QCOMPARE(doc.config().grids.size(), size_t(2));
    doc.undoStack()->undo();
    doc.undoStack()->undo();
    QVERIFY(doc.config().findGrid("games"));
  }

  void saveAndDiskChanges()
  {
    QTemporaryDir dir;
    const QString path = dir.filePath("prowal.json");
    QString error;
    QVERIFY(saveConfig(sample(), path, dir.filePath("backups"), &error));

    Document doc(loadConfig(path).config, path);
    QVERIFY(!doc.changedOnDisk());
    doc.moveItem(0, {}, 2, {3, 3});
    QVERIFY(doc.save(&error));
    QVERIFY(!doc.isModified());
    QVERIFY(!doc.changedOnDisk());
    QCOMPARE(loadConfig(path).config.grids[0].items[2].x, 3);

    QTest::qWait(20);  // filesystem timestamp resolution
    QFile f(path);
    QVERIFY(f.open(QIODevice::Append));
    f.write("\n");
    f.close();
    QVERIFY(doc.changedOnDisk());
  }

  void problems()
  {
    Item item;
    QVERIFY(commandProblem(item, "kitty").contains("No command"));
    item.command = "sh -c true";
    QVERIFY(commandProblem(item, "kitty").isEmpty());
    item.command = "no-such-program-xyz";
    QVERIFY(commandProblem(item, "kitty").contains("isn't installed"));
    item.command = "/nonexistent/bin/tool";
    QVERIFY(commandProblem(item, "kitty").contains("doesn't exist"));
    item.command = "sh 'unterminated";
    QVERIFY(commandProblem(item, "kitty").contains("Unterminated"));
    item.command = "sh";
    item.terminal = true;
    QVERIFY(commandProblem(item, "no-such-terminal-xyz -e").contains("terminal"));

    item.workingDir = "/";
    QVERIFY(workingDirProblem(item).isEmpty());
    item.workingDir = "~";
    QVERIFY(workingDirProblem(item).isEmpty());
    item.workingDir = "/nonexistent/dir";
    QVERIFY(!workingDirProblem(item).isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "tst_document.moc"
