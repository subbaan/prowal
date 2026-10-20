#include "desktop/DesktopEntry.h"
#include "system/CommandLine.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace prowal;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(content);
}

DesktopEntry entryWithExec(const QString &exec)
{
  DesktopEntry e;
  e.exec = exec;
  e.name = "My App";
  e.icon = "my-icon";
  e.file = "/usr/share/applications/my.desktop";
  return e;
}

} // namespace

class TestDesktop : public QObject
{
  Q_OBJECT

private slots:
  void parseBasics()
  {
    const QByteArray data =
      "# comment\n"
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Name=Files\n"
      "Name[de]=Dateien\n"
      "Name[en_GB]=Files (GB)\n"
      "GenericName=File Manager\n"
      "Comment=Browse\\sthe\\tfiles\n"
      "Icon=org.gnome.Nautilus.png\n"
      "Exec=nautilus --new-window %U\n"
      "Terminal=false\n"
      "NoDisplay=true\n"
      "Categories=GNOME;GTK;Utility;Core;\n"
      "Keywords=folder;manager;\n"
      "Keywords[de]=Ordner;\n"
      "OnlyShowIn=GNOME;Unity;\n"
      "[Desktop Action new-window]\n"
      "Name=Should be ignored\n"
      "Exec=ignored\n";
    auto e = parseDesktopEntry(data, {"de_DE", "de"});
    QVERIFY(e);
    QCOMPARE(e->name, QString("Dateien"));
    QCOMPARE(e->comment, QString("Browse the\tfiles"));
    QCOMPARE(e->exec, QString("nautilus --new-window %U"));
    QVERIFY(e->noDisplay);
    QCOMPARE(e->categories, (QStringList{"GNOME", "GTK", "Utility", "Core"}));
    QCOMPARE(e->keywords, QStringList{"Ordner"});
    QCOMPARE(e->onlyShowIn, (QStringList{"GNOME", "Unity"}));

    e = parseDesktopEntry(data, {"en_GB", "en"});
    QCOMPARE(e->name, QString("Files (GB)"));
    e = parseDesktopEntry(data, {"fr_FR", "fr"});
    QCOMPARE(e->name, QString("Files"));
    QCOMPARE(e->keywords, (QStringList{"folder", "manager"}));

    QVERIFY(!parseDesktopEntry("Name=x\n", {}));
  }

  void exec_data()
  {
    QTest::addColumn<QString>("exec");
    QTest::addColumn<QStringList>("args");
    QTest::newRow("field codes") << "gimp-2.10 %U" << QStringList{"gimp-2.10"};
    QTest::newRow("all codes") << "app %f %F %u %U %d %D %n %N %v %m x" << QStringList{"app", "x"};
    QTest::newRow("icon") << "app %i" << QStringList{"app", "--icon", "my-icon"};
    QTest::newRow("name and file") << "app --title %c --desktop %k"
                                   << QStringList{"app", "--title", "My App", "--desktop",
                                                  "/usr/share/applications/my.desktop"};
    QTest::newRow("percent") << "app --rate=50%%" << QStringList{"app", "--rate=50%"};
    QTest::newRow("embedded code") << "app --file=%f" << QStringList{"app", "--file="};
    QTest::newRow("quotes") << "sh -c \"echo \\\"hi there\\\" \\$HOME\""
                            << QStringList{"sh", "-c", "echo \"hi there\" $HOME"};
    QTest::newRow("flatpak") << "/usr/bin/flatpak run --branch=stable --command=foo org.foo.Foo @@u %U @@"
                             << QStringList{"/usr/bin/flatpak", "run", "--branch=stable", "--command=foo",
                                            "org.foo.Foo"};
  }

  void exec()
  {
    QFETCH(QString, exec);
    QFETCH(QStringList, args);
    QString error;
    QCOMPARE(execToArgs(entryWithExec(exec), &error), args);
    QVERIFY(error.isEmpty());
  }

  void execUnterminated()
  {
    QString error;
    QVERIFY(execToArgs(entryWithExec("app \"oops"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
  }

  void joinRoundTrips()
  {
    const QStringList cases[] = {
      {"plain", "--flag=1", "/usr/bin/x"},
      {"sh", "-c", "echo \"hi\" && ls | wc -l"},
      {"it's", "", "a b", "~/not-expanded", "#not-a-comment", "$HOME", "back\\slash"},
    };
    for (const QStringList &args : cases)
    {
      const SplitResult r = splitCommand(joinCommand(args));
      QVERIFY(r.ok);
      QCOMPARE(r.args, args);
    }
    QCOMPARE(joinCommand({"gimp", "--new-instance"}), QString("gimp --new-instance"));
  }

  void itemFromEntry()
  {
    DesktopEntry e = entryWithExec("htop %F");
    e.icon = "htop.png";
    e.terminal = true;
    e.workingDir = "/tmp";
    e.id = "htop.desktop";
    const Item item = itemFromDesktopEntry(e);
    QCOMPARE(item.name, QString("My App"));
    QCOMPARE(item.command, QString("htop"));
    QCOMPARE(item.icon, QString("htop"));  // extension stripped from a theme name
    QVERIFY(item.terminal);
    QCOMPARE(item.workingDir, QString("/tmp"));
    QCOMPARE(item.desktopId, QString("htop.desktop"));

    e.icon = "/opt/app/icon.png";
    QCOMPARE(itemFromDesktopEntry(e).icon, QString("/opt/app/icon.png"));
  }

  void menus()
  {
    DesktopEntry e = entryWithExec("x");
    e.type = "Application";
    QVERIFY(shownInMenus(e, "XFCE"));
    e.onlyShowIn = {"KDE"};
    QVERIFY(!shownInMenus(e, "XFCE"));
    QVERIFY(shownInMenus(e, "ubuntu:KDE"));
    e.onlyShowIn.clear();
    e.notShowIn = {"XFCE"};
    QVERIFY(!shownInMenus(e, "XFCE"));
    e.notShowIn.clear();
    e.noDisplay = true;
    QVERIFY(!shownInMenus(e, "XFCE"));
  }

  void listing()
  {
    QTemporaryDir tmp;
    const QString user = tmp.filePath("user/applications");
    const QString system = tmp.filePath("system/applications");
    const QByteArray app = "[Desktop Entry]\nType=Application\nExec=sh\nName=";
    writeFile(system + "/beta.desktop", app + "Beta");
    writeFile(system + "/alpha.desktop", app + "Alpha (system)");
    writeFile(user + "/alpha.desktop", app + "Alpha (user)");  // overrides the system one
    writeFile(system + "/kde/gamma.desktop", app + "Gamma");   // id kde-gamma.desktop
    writeFile(system + "/deleted.desktop", app + "Deleted");
    writeFile(user + "/deleted.desktop", app + "Deleted\nHidden=true");  // user deleted it
    writeFile(system + "/link.desktop", "[Desktop Entry]\nType=Link\nName=Link\nURL=https://x");
    writeFile(system + "/missing.desktop", app + "Missing\nTryExec=no-such-program-xyz");
    writeFile(system + "/nodisplay.desktop", app + "Quiet\nNoDisplay=true");

    const std::vector<DesktopEntry> apps = listApplications({user, system});
    QStringList names, ids;
    for (const DesktopEntry &e : apps)
    {
      names << e.name;
      ids << e.id;
    }
    QCOMPARE(names, (QStringList{"Alpha (user)", "Beta", "Gamma", "Quiet"}));
    QVERIFY(ids.contains("kde-gamma.desktop"));
    QVERIFY(apps[0].file.startsWith(user));

    // Looking one up by id: user first, subdirectory ids, nothing outside the dirs.
    QCOMPARE(findApplication("alpha.desktop", {user, system})->name, QString("Alpha (user)"));
    QCOMPARE(findApplication("kde-gamma.desktop", {user, system})->name, QString("Gamma"));
    QCOMPARE(findApplication("kde-gamma.desktop", {user, system})->id, QString("kde-gamma.desktop"));
    QVERIFY(!findApplication("nope.desktop", {user, system}));
    QVERIFY(!findApplication("../system/applications/beta.desktop", {user}));
    QVERIFY(!findApplication("", {user, system}));
  }
};

QTEST_GUILESS_MAIN(TestDesktop)
#include "tst_desktop.moc"
