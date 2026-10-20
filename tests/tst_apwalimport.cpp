#include "config/ApwalImport.h"
#include "config/Config.h"

#include <QTest>

using namespace prowal;

class TestApwalImport : public QObject
{
  Q_OBJECT

private slots:
  void sampleFile()
  {
    const ImportResult r = readApwalFile(PROWAL_TEST_DATA "/apwalrc-sample.xml");
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.imported, 5);
    QCOMPARE(r.skipped, 1);  // the empty placeholder
    QVERIFY(r.hasTimeout);
    QCOMPARE(r.timeoutMs, 2000);
    QVERIFY(r.closeAfterLaunch);

    const Item &thunar = r.items[0];
    QCOMPARE(thunar.name, QString("Thunar"));
    QCOMPARE(QPoint(thunar.x, thunar.y), QPoint(0, 0));

    const Item &term = r.items[1];
    QCOMPARE(term.command, QString("xfce4-terminal # xterm -fg wheat -bg black"));  // kept verbatim
    QCOMPARE(term.name, QString("Xfce4 Terminal"));
    QCOMPARE(term.workingDir, QString("/tmp"));

    const Item &gimp = r.items[2];
    QCOMPARE(gimp.command, QString("gimp-2.10 --new-instance"));
    QCOMPARE(gimp.name, QString("Gimp 2.10"));
    QCOMPARE(gimp.icon, QString("gimp-2.10"));  // no icon: try the theme
    QCOMPARE(QPoint(gimp.x, gimp.y), QPoint(-2, 0));

    // Second item in (0, 0) moves right, as apwal displayed it.
    const Item &pamac = r.items[3];
    QCOMPARE(QPoint(pamac.x, pamac.y), QPoint(1, 0));
    QVERIFY(r.warnings.join('\n').contains("moved to (1, 0)"));
  }

  void malformed()
  {
    QVERIFY(!parseApwalXml("<apwalrc><apps><app>").ok);
    QVERIFY(!parseApwalXml("<other/>").ok);
    QVERIFY(!parseApwalXml("").ok);
  }

  void badNumbers()
  {
    const ImportResult r = parseApwalXml(
      "<apwalrc><apps><app><cmdline>a</cmdline><x></x><y>three</y></app></apps></apwalrc>");
    QVERIFY(r.ok);
    QCOMPARE(r.imported, 1);
    QCOMPARE(QPoint(r.items[0].x, r.items[0].y), QPoint(0, 0));
    QCOMPARE(r.warnings.size(), 2);
  }

  void applyKeepsOtherSettings()
  {
    Config c = defaultConfig("kitty");
    c.appearance.iconSize = 64;
    c.grids.push_back(Grid{"games", {}, {}});
    c.grids[1].items.push_back(Item{});

    const ImportResult r = readApwalFile(PROWAL_TEST_DATA "/apwalrc-sample.xml");
    applyApwalImport(r, &c, "main");
    QCOMPARE(c.findGrid("main")->items.size(), size_t(5));
    QCOMPARE(c.findGrid("games")->items.size(), size_t(1));
    QCOMPARE(c.appearance.iconSize, 64);
    QCOMPARE(c.behaviour.timeoutMs, 2000);
    QVERIFY(!c.behaviour.stayOpenAfterLaunch);
    QCOMPARE(c.behaviour.terminal, QString("kitty"));
  }

  void names()
  {
    QCOMPARE(nameFromCommand("/opt/resolve/bin/resolve"), QString("Resolve"));
    QCOMPARE(nameFromCommand("transmission-gtk"), QString("Transmission Gtk"));
    QCOMPARE(nameFromCommand("arma3_unix_launcher --x"), QString("Arma3 Unix Launcher"));
    QCOMPARE(nameFromCommand(""), QString());
  }
};

QTEST_GUILESS_MAIN(TestApwalImport)
#include "tst_apwalimport.moc"
