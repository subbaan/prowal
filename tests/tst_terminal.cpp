#include "system/Terminal.h"

#include <QSet>
#include <QTest>

using namespace prowal;

namespace {

ExecutableFinder installed(QSet<QString> programs)
{
  return [programs](const QString &p) { return programs.contains(p) ? "/usr/bin/" + p : QString(); };
}

} // namespace

class TestTerminal : public QObject
{
  Q_OBJECT

private slots:
  void prefersKonsole()
  {
    QCOMPARE(detectTerminal(installed({"xterm", "kitty", "konsole"}), ""), QString("konsole -e"));
  }

  void fallsBackByPopularity()
  {
    QCOMPARE(detectTerminal(installed({"xterm", "kitty", "gnome-terminal"}), ""),
             QString("gnome-terminal --"));
    QCOMPARE(detectTerminal(installed({"xterm", "kitty"}), ""), QString("kitty"));
    QCOMPARE(detectTerminal(installed({"xterm"}), ""), QString("xterm -e"));
    QCOMPARE(detectTerminal(installed({}), ""), QString());
  }

  void honoursTerminalVariable()
  {
    QCOMPARE(detectTerminal(installed({"konsole", "kitty"}), "kitty"), QString("kitty"));
    QCOMPARE(detectTerminal(installed({"konsole", "st"}), "st"), QString("st -e"));
    // Not installed: ignored.
    QCOMPARE(detectTerminal(installed({"konsole"}), "kitty"), QString("konsole -e"));
  }

  void installedList()
  {
    QCOMPARE(installedTerminals(installed({"xterm", "kitty", "konsole"})),
             (QStringList{"konsole -e", "kitty", "xterm -e"}));
    QVERIFY(installedTerminals(installed({})).isEmpty());
  }

  void prefixes()
  {
    QCOMPARE(terminalPrefixFor("wezterm"), QString("wezterm start --"));
    QCOMPARE(terminalPrefixFor("/usr/bin/xfce4-terminal"), QString("/usr/bin/xfce4-terminal -x"));
    QCOMPARE(terminalPrefixFor("foot"), QString("foot"));
  }
};

QTEST_GUILESS_MAIN(TestTerminal)
#include "tst_terminal.moc"
