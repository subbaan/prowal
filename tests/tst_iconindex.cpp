#include "icons/IconIndex.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace prowal;

namespace {

void touch(const QString &path, const QByteArray &content = "x")
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(content);
}

const IconFile *find(const std::vector<IconFile> &files, const QString &name, const QString &collectionEnd)
{
  for (const IconFile &f : files)
    if (f.name == name && f.collection.endsWith(collectionEnd))
      return &f;
  return nullptr;
}

} // namespace

class TestIconIndex : public QObject
{
  Q_OBJECT

private slots:
  void installedThemes()
  {
    QTemporaryDir tmp;
    const QString user = tmp.filePath("user");
    const QString system = tmp.filePath("system");
    // Several directories, as real themes have: QSettings reads the value as a list.
    touch(system + "/Papirus/index.theme",
          "[Icon Theme]\nName=Papirus\nInherits=breeze,hicolor\nDirectories=16x16/apps,48x48/apps\n");
    touch(system + "/One/index.theme", "[Icon Theme]\nName=Single, Directory\nDirectories=48x48/apps\n");
    touch(system + "/Cursors/index.theme", "[Icon Theme]\nName=Cursors\nInherits=Adwaita\n");
    touch(system + "/Secret/index.theme", "[Icon Theme]\nName=Secret\nHidden=true\nDirectories=48x48/apps\n");
    touch(system + "/Plain/index.theme", "[Icon Theme]\nName=System Plain\nDirectories=48x48/apps\n");
    touch(user + "/Plain/index.theme", "[Icon Theme]\nName=User Plain\nDirectories=48x48/apps,64x64/apps\n");
    touch(system + "/NoIndex/48x48/apps/x.png");

    const std::vector<IconThemeInfo> themes = scanIconThemes({user, system});
    QStringList names, displays;
    for (const IconThemeInfo &t : themes)
    {
      names << t.name;
      displays << t.display;
    }
    QCOMPARE(names, QStringList({"Papirus", "One", "Plain"}));
    QCOMPARE(displays, QStringList({"Papirus", "Single, Directory", "User Plain"}));
    QCOMPARE(themeDisplayName(system + "/One"), QStringLiteral("Single, Directory"));
  }

  void themesAndFolders()
  {
    QTemporaryDir tmp;
    const QString root = tmp.filePath("icons");
    touch(root + "/Fancy/index.theme", "[Icon Theme]\nName=Fancy Icons\nInherits=Base\n");
    touch(root + "/Fancy/16x16/apps/firefox.png");
    touch(root + "/Fancy/48x48/apps/firefox.png");
    touch(root + "/Fancy/256x256/apps/firefox.png");
    touch(root + "/Fancy/32x32/apps/gimp.png");
    touch(root + "/Fancy/64x64/apps/gimp.png");
    touch(root + "/Fancy/scalable/apps/vlc.svg");
    touch(root + "/Fancy/48x48/apps/vlc.png");
    touch(root + "/Fancy/symbolic/apps/vlc-symbolic.svg");
    touch(root + "/Fancy/apps/24@2x/thing.png");
    touch(root + "/Base/index.theme", "[Icon Theme]\nName=Base\n");
    touch(root + "/Base/48x48/apps/firefox.png");
    touch(root + "/My Collection/one.png");
    touch(root + "/My Collection/deeper/two.xpm");
    touch(root + "/My Collection/deeper/two.png");
    touch(root + "/loose.png");
    touch(root + "/notes.txt");
    QVERIFY(QFile::link(root + "/Fancy", root + "/Linked"));  // must not be entered

    const std::vector<IconFile> files = scanIconFolder({root, true}, 48);

    const IconFile *firefox = find(files, "firefox", "/Fancy");
    QVERIFY(firefox);
    QVERIFY(firefox->path.endsWith("48x48/apps/firefox.png"));
    QVERIFY(firefox->themed);
    QVERIFY(find(files, "firefox", "/Base"));  // other themes keep their own copy
    QVERIFY(find(files, "gimp", "/Fancy")->path.endsWith("64x64/apps/gimp.png"));  // bigger beats blurry
    QVERIFY(find(files, "vlc", "/Fancy")->path.endsWith(".svg"));
    QVERIFY(find(files, "vlc-symbolic", "/Fancy")->symbolic);
    QCOMPARE(find(files, "thing", "/Fancy")->size, 48);  // 24@2x

    const IconFile *two = find(files, "two", "/My Collection");
    QVERIFY(two);
    QVERIFY(two->path.endsWith(".png"));  // PNG preferred over XPM
    QVERIFY(!two->themed);
    QVERIFY(find(files, "one", "/My Collection"));
    QVERIFY(find(files, "loose", "/icons"));
    QVERIFY(!find(files, "notes", ""));
    for (const IconFile &f : files)
      QVERIFY2(!f.path.contains("/Linked/"), qPrintable(f.path));

    // Not recursive: only the loose files.
    const std::vector<IconFile> flat = scanIconFolder({root, false}, 48);
    QCOMPARE(flat.size(), size_t(1));

    QCOMPARE(themeChain("Fancy", {root}), (QStringList{"Fancy", "Base", "hicolor"}));
    QCOMPARE(themeDisplayName(root + "/Fancy"), QString("Fancy Icons"));
    QCOMPARE(themeDisplayName(root + "/My Collection"), QString("My Collection"));
  }

  void folderThatIsATheme()
  {
    QTemporaryDir tmp;
    touch(tmp.filePath("hicolor/index.theme"), "[Icon Theme]\nName=Hicolor\n");
    touch(tmp.filePath("hicolor/48x48/apps/a.png"));
    const std::vector<IconFolder> dirs = collectionDirs({tmp.filePath("hicolor"), false});
    QCOMPARE(dirs.size(), size_t(1));
    QVERIFY(dirs[0].recursive);
    QCOMPARE(scanIconFolder({tmp.filePath("hicolor"), false}, 48).size(), size_t(1));
  }

  void cancel()
  {
    QTemporaryDir tmp;
    for (int i = 0; i < 3000; ++i)
      touch(tmp.filePath(QStringLiteral("c/i%1.png").arg(i)));
    const auto files = scanIconCollection({tmp.filePath("c"), true}, 48, [] { return true; });
    QVERIFY(files.size() < 3000);
  }
};

QTEST_GUILESS_MAIN(TestIconIndex)
#include "tst_iconindex.moc"
