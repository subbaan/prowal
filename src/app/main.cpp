#include "config/ApwalImport.h"
#include "config/Config.h"
#include "config/GridOps.h"
#include "config/Paths.h"
#include "editor/Document.h"
#include "editor/EditorWindow.h"
#include "icons/IconTheme.h"
#include "platform/SingleInstance.h"
#include "platform/X11.h"
#include "popup/PopupWindow.h"
#include "system/Terminal.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <optional>

using namespace prowal;

namespace {

QElapsedTimer g_clock;
bool g_trace = false;

void trace(const char *what)
{
  if (g_trace)
    std::fprintf(stderr, "prowal: %6.1f ms  %s\n", g_clock.nsecsElapsed() / 1e6, what);
}

void printLines(const QStringList &lines, const char *prefix)
{
  for (const QString &line : lines)
    std::fprintf(stderr, "prowal: %s%s\n", prefix, qPrintable(line));
}

// Options that never show a window: run them without connecting to the display.
bool needsGui(int argc, char **argv)
{
  for (int i = 1; i < argc; ++i)
  {
    const QByteArray a(argv[i]);
    if (a == "-h" || a == "--help" || a == "--help-all" || a == "-?" || a == "-V" || a == "--version" ||
        a == "--import-apwal")
      return false;
  }
  return true;
}

bool needsEditor(int argc, char **argv)
{
  for (int i = 1; i < argc; ++i)
    if (QByteArray(argv[i]) == "--edit")
      return true;
  return false;
}

int runImport(const QString &configPath, const QString &apwalPath, const QString &gridName)
{
  const ImportResult imported = readApwalFile(apwalPath);
  if (!imported.ok)
  {
    std::fprintf(stderr, "prowal: import failed: %s\n", qPrintable(imported.error));
    return 1;
  }

  Config config;
  if (QFileInfo::exists(configPath))
  {
    LoadResult loaded = loadConfig(configPath);
    if (!loaded.ok)
    {
      std::fprintf(stderr, "prowal: %s\nprowal: fix or move that file first; nothing was changed.\n",
                   qPrintable(loaded.error));
      return 1;
    }
    config = std::move(loaded.config);
  }
  else
    config = defaultConfig(detectTerminal());

  applyApwalImport(imported, &config, gridName);
  QString error;
  if (!saveConfig(config, configPath, paths::backupDirFor(configPath), &error))
  {
    std::fprintf(stderr, "prowal: %s\n", qPrintable(error));
    return 1;
  }
  printLines(imported.warnings, "note: ");
  std::fprintf(stderr, "prowal: imported %d item(s) from %s into grid \"%s\" of %s\n",
               imported.imported, qPrintable(apwalPath), qPrintable(gridName), qPrintable(configPath));
  return 0;
}

// First run: bring over an apwal layout if there is one, otherwise start small.
bool createConfig(const QString &configPath, bool isDefaultPath)
{
  const QString apwal = paths::defaultApwalFile();
  if (isDefaultPath && QFileInfo::exists(apwal))
  {
    std::fprintf(stderr, "prowal: first run - importing your apwal layout from %s (read only)\n",
                 qPrintable(apwal));
    return runImport(configPath, apwal, kDefaultGridName) == 0;
  }
  QString error;
  if (!saveConfig(defaultConfig(detectTerminal()), configPath, paths::backupDirFor(configPath), &error))
  {
    std::fprintf(stderr, "prowal: %s\n", qPrintable(error));
    return false;
  }
  std::fprintf(stderr, "prowal: created %s\n", qPrintable(configPath));
  return true;
}

void fatal(const QString &message)
{
  std::fprintf(stderr, "prowal: %s\n", qPrintable(message));
  if (qobject_cast<QApplication *>(QCoreApplication::instance()))
    QMessageBox::critical(nullptr, QStringLiteral("Prowal"), message);
}

} // namespace

int main(int argc, char **argv)
{
  g_clock.start();
  g_trace = qEnvironmentVariableIsSet("PROWAL_TRACE_STARTUP");

  QCoreApplication::setApplicationName(QStringLiteral("prowal"));
  QCoreApplication::setApplicationVersion(QStringLiteral(PROWAL_VERSION));
  std::unique_ptr<QCoreApplication> app;
  if (needsGui(argc, argv))
  {
    // Prowal is an X11 program (pointer position, override-redirect popup,
    // compositor detection). In a Wayland session it runs through XWayland.
    const bool platformSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    if (!platformSet && qEnvironmentVariableIsEmpty("DISPLAY"))
    {
      std::fprintf(stderr, "prowal: no X11 display (DISPLAY is not set). Prowal needs X11 or XWayland.\n");
      return 1;
    }
    const bool forceXcb = !platformSet && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    if (forceXcb)
      qputenv("QT_QPA_PLATFORM", "xcb");
    // The desktop's Qt platform theme plugin (GTK3 on Xfce) costs ~25 ms to load
    // and the popup draws its own look anyway. Skip it where Prowal can read the
    // icon theme itself, and drop the override again so launched apps never see it.
    const bool skipTheme = !qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME") &&
                           !needsEditor(argc, argv) && canFindIconThemeWithoutPlatformTheme();
    if (skipTheme)
      qputenv("QT_QPA_PLATFORMTHEME", "generic");  // Qt's built-in fallback theme
    app = std::make_unique<QApplication>(argc, argv);
    if (skipTheme)
      qunsetenv("QT_QPA_PLATFORMTHEME");
    if (forceXcb)
      qunsetenv("QT_QPA_PLATFORM");  // launched apps should still use Wayland
  }
  else
    app = std::make_unique<QCoreApplication>(argc, argv);
  trace("application created");

  QCommandLineParser parser;
  parser.setApplicationDescription(
    QStringLiteral("Pop-up application launcher: a grid of icons around the mouse pointer."));
  parser.addHelpOption();
  const QCommandLineOption versionOpt({QStringLiteral("V"), QStringLiteral("version")},
                                      QStringLiteral("Show the version and exit."));
  const QCommandLineOption editOpt(QStringLiteral("edit"), QStringLiteral("Open the editor."));
  const QCommandLineOption configOpt({QStringLiteral("c"), QStringLiteral("config")},
                                     QStringLiteral("Use <file> instead of the default config."),
                                     QStringLiteral("file"));
  const QCommandLineOption gridOpt({QStringLiteral("g"), QStringLiteral("grid")},
                                   QStringLiteral("Show (or import into) the grid called <name>. "
                                                  "Default: main."),
                                   QStringLiteral("name"));
  const QCommandLineOption importOpt(QStringLiteral("import-apwal"),
                                     QStringLiteral("Import an apwal layout, replacing the grid's items "
                                                    "(default file: ~/.apwalrc.xml). The apwal file is "
                                                    "only read. The previous config is backed up."));
  const QCommandLineOption itemOpt(QStringLiteral("item"),
                                   QStringLiteral("With --edit: select the cell X,Y (e.g. -1,2)."),
                                   QStringLiteral("x,y"));
  const QCommandLineOption groupOpt(QStringLiteral("group"),
                                    QStringLiteral("With --edit: open inside the group at cell X,Y "
                                                   "(nested groups: X,Y/X,Y/...)."),
                                    QStringLiteral("path"));
  parser.addOptions({versionOpt, editOpt, configOpt, gridOpt, importOpt, itemOpt, groupOpt});
  parser.addPositionalArgument(QStringLiteral("apwal-file"),
                               QStringLiteral("With --import-apwal: the file to import."),
                               QStringLiteral("[apwal-file]"));
  parser.process(*app);

  if (parser.isSet(versionOpt))
  {
    std::printf("prowal %s\n", PROWAL_VERSION);
    return 0;
  }
  const QStringList positional = parser.positionalArguments();
  if (positional.size() > (parser.isSet(importOpt) ? 1 : 0))
  {
    std::fprintf(stderr, "prowal: unexpected argument \"%s\" (see --help)\n",
                 qPrintable(positional.last()));
    return 2;
  }

  const bool defaultPath = !parser.isSet(configOpt);
  const QString configPath = defaultPath ? paths::defaultConfigFile()
                                         : QFileInfo(parser.value(configOpt)).absoluteFilePath();
  const QString gridName = parser.isSet(gridOpt) ? parser.value(gridOpt) : kDefaultGridName;

  if (parser.isSet(importOpt))
    return runImport(configPath, positional.value(0, paths::defaultApwalFile()), gridName);

  std::optional<QPoint> selectCell;
  if (parser.isSet(itemOpt))
  {
    const QStringList xy = parser.value(itemOpt).split(QLatin1Char(','));
    bool okX = false, okY = false;
    if (xy.size() == 2)
      selectCell = QPoint(xy[0].trimmed().toInt(&okX), xy[1].trimmed().toInt(&okY));
    if (!okX || !okY)
    {
      std::fprintf(stderr, "prowal: --item needs X,Y, e.g. --item -1,2\n");
      return 2;
    }
  }

  GroupPath group;
  if (parser.isSet(groupOpt))
  {
    const auto parsed = parseGroupPath(parser.value(groupOpt));
    if (!parsed)
    {
      std::fprintf(stderr, "prowal: --group needs X,Y or X,Y/X,Y/..., e.g. --group 2,0\n");
      return 2;
    }
    group = *parsed;
  }

  // One editor per config: if one is open, it shows the request and we're done.
  std::unique_ptr<SingleInstance> instance;
  if (parser.isSet(editOpt))
  {
    instance = std::make_unique<SingleInstance>(configPath);
    QJsonObject request{{QStringLiteral("grid"), gridName}, {QStringLiteral("group"), formatGroupPath(group)}};
    if (selectCell)
      request.insert(QStringLiteral("item"), QStringLiteral("%1,%2").arg(selectCell->x()).arg(selectCell->y()));
    if (instance->sendToRunning(QJsonDocument(request).toJson(QJsonDocument::Compact)))
    {
      std::fprintf(stderr, "prowal: the editor for %s is already open; showing it there\n", qPrintable(configPath));
      return 0;
    }
  }

  if (!QFileInfo::exists(configPath) && !createConfig(configPath, defaultPath))
    return 1;

  const LoadResult loaded = loadConfig(configPath);
  if (!loaded.ok)
  {
    fatal(QStringLiteral("%1\n\nThe file has not been changed.").arg(loaded.error));
    return 1;
  }
  printLines(loaded.warnings, "warning: ");
  trace("config loaded");

  if (parser.isSet(editOpt))
  {
    setupIconTheme(loaded.config.appearance.iconTheme);
    Config config = loaded.config;
    if (config.grids.empty())
      config.grids.push_back(Grid{kDefaultGridName, {}, {}});
    int gridIndex = 0;
    for (size_t i = 0; i < config.grids.size(); ++i)
      if (config.grids[i].name == gridName)
        gridIndex = int(i);
    auto *editor = new EditorWindow(new Document(std::move(config), configPath), gridIndex, group, selectCell,
                                    loaded.warnings);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    instance->setParent(editor);
    instance.release()->listen([editor](const QByteArray &message) {
      const QJsonObject r = QJsonDocument::fromJson(message).object();
      const auto path = parseGroupPath(r.value(QStringLiteral("group")).toString());
      const auto cell = parseGroupPath(r.value(QStringLiteral("item")).toString());  // "x,y" is a one-step path
      editor->reveal(r.value(QStringLiteral("grid")).toString(), path.value_or(GroupPath()),
                     cell && cell->size() == 1 ? std::optional<QPoint>(cell->front()) : std::nullopt);
    });
    editor->show();
    trace("editor shown");
    QTimer::singleShot(0, [] { trace("editor event loop running"); });
    return app->exec();
  }

  const Grid *grid = loaded.config.findGrid(gridName);
  if (!grid)
  {
    QStringList names;
    for (const Grid &g : loaded.config.grids)
      names << g.name;
    fatal(QStringLiteral("there is no grid called \"%1\" (have: %2)")
            .arg(gridName, names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", "))));
    return 1;
  }

  const QString themeSource = setupIconTheme(loaded.config.appearance.iconTheme);
  const bool compositing = x11::compositorRunning();
  if (g_trace)
    std::fprintf(stderr, "prowal: icon theme from %s; compositor %s\n", qPrintable(themeSource),
                 compositing ? "running" : "not running");
  trace("environment checked");

  static_cast<QApplication *>(app.get())->setQuitOnLastWindowClosed(false);
  const QString terminal = loaded.config.behaviour.terminal.isEmpty() ? detectTerminal()
                                                                      : loaded.config.behaviour.terminal;
  PopupWindow popup(loaded.config, gridName, terminal, compositing, configPath);
  popup.trace = trace;
  popup.closed = [] { QCoreApplication::quit(); };
  popup.popupAtCursor();
  trace("popup shown");
  return app->exec();
}
