#include "config/Config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace prowal;

namespace {

Config sample()
{
  Config c = defaultConfig("kitty");
  c.behaviour.timeoutMs = 1500;
  c.appearance.mode = "panel";
  c.appearance.iconSize = 64;
  c.appearance.animationSpeed = 1.5;
  c.appearance.shadows = false;
  c.appearance.accentColor = "#ff8800";
  c.appearance.panelOpacity = 60;
  Item fx;
  fx.x = 0;
  fx.y = -2;
  fx.name = "Firefox";
  fx.command = "firefox";
  fx.icon = "firefox";
  fx.env.insert("MOZ_X11_EGL", "1");
  Item group;
  group.x = -1;
  group.y = 1;
  group.type = ItemType::Group;
  group.name = "Games";
  group.icon = "applications-games";
  Item steam;
  steam.name = "Steam";
  steam.command = "steam";
  steam.terminal = true;
  group.children.push_back(steam);
  c.grids[0].items.push_back(fx);
  c.grids[0].items.push_back(group);
  return c;
}

} // namespace

class TestConfig : public QObject
{
  Q_OBJECT

private slots:
  void roundTrip()
  {
    const QByteArray json = serializeConfig(sample());
    const LoadResult r = parseConfig(json);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join('\n')));
    QCOMPARE(serializeConfig(r.config), json);

    const Grid *main = r.config.findGrid("main");
    QVERIFY(main);
    QCOMPARE(main->items.size(), size_t(3));
    const Item &group = main->items[2];
    QCOMPARE(group.type, ItemType::Group);
    QCOMPARE(group.children.size(), size_t(1));
    QVERIFY(group.children[0].terminal);
    QCOMPARE(main->items[1].env.value("MOZ_X11_EGL"), QString("1"));
    QCOMPARE(r.config.appearance.iconSize, 64);
    QCOMPARE(r.config.appearance.animationSpeed, 1.5);
    QVERIFY(!r.config.appearance.shadows);
    QCOMPARE(r.config.appearance.accentColor, QString("#ff8800"));
    QCOMPARE(r.config.appearance.panelOpacity, 60);
  }

  void badAppearanceValues()
  {
    const LoadResult r = parseConfig(
      "{\"grids\":[{\"name\":\"main\"}],\"appearance\":{\"animationSpeed\":0,\"accentColor\":\"not-a-colour\",\"panelColor\":\"#123\"}}");
    QVERIFY(r.ok);
    QCOMPARE(r.config.appearance.animationSpeed, 1.0);
    QCOMPARE(r.config.appearance.accentColor, QString());
    QCOMPARE(r.config.appearance.panelColor, QString("#123"));
    QCOMPARE(r.warnings.size(), 2);
  }

  void unknownKeysPreserved()
  {
    const QByteArray in = "{\"schema\":1,\"future\":{\"a\":1},\n"
      "      \"appearance\":{\"sparkle\":true},\n"
      "      \"grids\":[{\"name\":\"main\",\"colour\":\"red\",\"items\":[{\"x\":0,\"y\":0,\"command\":\"a\",\"pinned\":true}]}]}";
    const LoadResult r = parseConfig(in);
    QVERIFY(r.ok);
    const QJsonObject out = QJsonDocument::fromJson(serializeConfig(r.config)).object();
    QCOMPARE(out["future"].toObject()["a"].toInt(), 1);
    QCOMPARE(out["appearance"].toObject()["sparkle"].toBool(), true);
    const QJsonObject grid = out["grids"].toArray()[0].toObject();
    QCOMPARE(grid["colour"].toString(), QString("red"));
    QCOMPARE(grid["items"].toArray()[0].toObject()["pinned"].toBool(), true);
  }

  void badJsonReportsLine()
  {
    const LoadResult r = parseConfig("{\n  \"schema\": 1,\n  oops\n}");
    QVERIFY(!r.ok);
    QVERIFY2(r.error.contains("line 3"), qPrintable(r.error));
  }

  void tolerantItems()
  {
    const QByteArray in = "{\"grids\":[{\"name\":\"main\",\"items\":[\n"
      "      {\"x\":0,\"y\":0,\"command\":\"good\"},\n"
      "      {\"y\":1,\"command\":\"no x\"},\n"
      "      \"not an object\",\n"
      "      {\"x\":2,\"y\":0,\"type\":\"rocket\",\"command\":\"bad type\"},\n"
      "      {\"x\":0,\"y\":0,\"command\":\"duplicate\"},\n"
      "      {\"x\":100,\"y\":0,\"command\":\"too far\"},\n"
      "      {\"x\":-99,\"y\":99,\"command\":\"far corner\"},\n"
      "      {\"x\":1.5,\"y\":0,\"command\":\"half\"},\n"
      "      {\"x\":1e300,\"y\":0,\"command\":\"huge\"}\n"
      "    ]}],\"appearance\":{\"iconSize\":9000,\"mode\":\"sparkly\"}}";
    const LoadResult r = parseConfig(in);
    QVERIFY(r.ok);
    QCOMPARE(r.config.grids[0].items.size(), size_t(3));  // good + duplicate + far corner kept
    QCOMPARE(r.config.appearance.iconSize, 256);
    QCOMPARE(r.config.appearance.mode, QString("floating"));
    const QString all = r.warnings.join('\n');
    QVERIFY2(all.contains("x/y"), qPrintable(all));
    QVERIFY2(all.contains("not an object"), qPrintable(all));
    QVERIFY2(all.contains("rocket"), qPrintable(all));
    QVERIFY2(all.contains("more than one item at (0, 0)"), qPrintable(all));
    QVERIFY2(all.contains("iconSize"), qPrintable(all));
    QCOMPARE(all.count("is not a whole number from -99 to 99"), 3);
  }

  void saveBacksUpAndPrunes()
  {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("prowal/prowal.json");
    const QString backups = dir.filePath("prowal/backups");
    Config c = sample();
    QString error;

    QVERIFY2(saveConfig(c, path, backups, &error, 3), qPrintable(error));
    QVERIFY(!QDir(backups).exists());  // nothing to back up the first time

    for (int i = 0; i < 5; ++i)
    {
      c.behaviour.timeoutMs = 1000 + i;
      QVERIFY2(saveConfig(c, path, backups, &error, 3), qPrintable(error));
      QTest::qWait(2);  // distinct millisecond timestamps
    }
    QCOMPARE(QDir(backups).entryList({"prowal-*.json"}, QDir::Files).size(), 3);

    const LoadResult r = loadConfig(path);
    QVERIFY(r.ok);
    QCOMPARE(r.config.behaviour.timeoutMs, 1004);
  }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_config.moc"
