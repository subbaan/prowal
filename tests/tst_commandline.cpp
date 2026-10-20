#include "system/CommandLine.h"
#include "system/Launch.h"

#include <QDir>
#include <QTest>

using namespace prowal;

class TestCommandLine : public QObject
{
  Q_OBJECT

private slots:
  void split_data()
  {
    QTest::addColumn<QString>("command");
    QTest::addColumn<QStringList>("expected");

    const QString home = QDir::homePath();
    QTest::newRow("plain") << "firefox" << QStringList{"firefox"};
    QTest::newRow("args") << "steam  -nofriendsui" << QStringList{"steam", "-nofriendsui"};
    QTest::newRow("single quotes") << "sh -c 'echo a b'" << QStringList{"sh", "-c", "echo a b"};
    QTest::newRow("double quotes") << "xterm -title \"My \\\"term\\\"\""
                                   << QStringList{"xterm", "-title", "My \"term\""};
    QTest::newRow("backslash space") << "open My\\ File" << QStringList{"open", "My File"};
    QTest::newRow("adjacent quotes") << "a\"b\"'c'd" << QStringList{"abcd"};
    QTest::newRow("empty quoted arg") << "prog \"\"" << QStringList{"prog", ""};
    QTest::newRow("comment") << "xfce4-terminal # xterm -fg wheat" << QStringList{"xfce4-terminal"};
    QTest::newRow("comment with quotes") << "lxterminal #xfce4-terminal # gnome-terminal --profile=\"Bash\" ~/ "
                                         << QStringList{"lxterminal"};
    QTest::newRow("hash inside word") << "echo a#b" << QStringList{"echo", "a#b"};
    QTest::newRow("tilde") << "ls ~ ~/x a~/y '~/z'"
                           << QStringList{"ls", home, home + "/x", "a~/y", "~/z"};
    QTest::newRow("only comment") << "# nothing" << QStringList{};
  }

  void split()
  {
    QFETCH(QString, command);
    QFETCH(QStringList, expected);
    const SplitResult r = splitCommand(command);
    QVERIFY(r.ok);
    QCOMPARE(r.args, expected);
  }

  void unterminated()
  {
    QVERIFY(!splitCommand("echo 'oops").ok);
    QVERIFY(!splitCommand("echo \"oops").ok);
  }

  void planPlain()
  {
    Item item;
    item.command = "gimp --new-instance";
    const LaunchPlan plan = planLaunch(item, "konsole -e");
    QVERIFY(plan.ok);
    QCOMPARE(plan.program, QString("gimp"));
    QCOMPARE(plan.arguments, QStringList{"--new-instance"});
  }

  void planTerminal()
  {
    Item item;
    item.command = "htop -d 10";
    item.terminal = true;
    LaunchPlan plan = planLaunch(item, "gnome-terminal --");
    QVERIFY(plan.ok);
    QCOMPARE(plan.program, QString("gnome-terminal"));
    QCOMPARE(plan.arguments, (QStringList{"--", "htop", "-d", "10"}));

    plan = planLaunch(item, "");
    QVERIFY(!plan.ok);
  }

  void planShell()
  {
    Item item;
    item.command = "cd ~/x && make | less";
    item.runInShell = true;
    const LaunchPlan plan = planLaunch(item, "kitty");
    QVERIFY(plan.ok);
    QCOMPARE(plan.program, QString("/bin/sh"));
    QCOMPARE(plan.arguments, (QStringList{"-c", "cd ~/x && make | less"}));
  }

  void planEmpty()
  {
    Item item;
    QVERIFY(!planLaunch(item, "kitty").ok);
    item.command = "# just a comment";
    QVERIFY(!planLaunch(item, "kitty").ok);
  }
};

QTEST_GUILESS_MAIN(TestCommandLine)
#include "tst_commandline.moc"
