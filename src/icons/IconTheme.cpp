#include "icons/IconTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace prowal {

namespace {

bool themeInstalled(const QString &name)
{
  if (name.isEmpty())
    return false;
  for (const QString &base : QIcon::themeSearchPaths())
    if (QFileInfo::exists(QDir(base).filePath(name + QStringLiteral("/index.theme"))))
      return true;
  return false;
}

QString configFile(const QString &relative)
{
  return QStandardPaths::locate(QStandardPaths::GenericConfigLocation, relative);
}

QString xfceTheme()
{
  // Xfce writes this file as XML 1.1, which QXmlStreamReader rejects, so match
  // the one element we need instead of parsing.
  QFile f(configFile(QStringLiteral("xfce4/xfconf/xfce-perchannel-xml/xsettings.xml")));
  if (!f.open(QIODevice::ReadOnly))
    return QString();
  static const QRegularExpression re(
    QStringLiteral("<property\\s+name=\"IconThemeName\"[^>]*\\svalue=\"([^\"]*)\""));
  const QRegularExpressionMatch m = re.match(QString::fromUtf8(f.readAll()));
  if (!m.hasMatch())
    return QString();
  QString name = m.captured(1);
  return name.replace(QLatin1String("&amp;"), QLatin1String("&"));
}

QString iniValue(const QString &relative, const QString &key)
{
  const QString path = configFile(relative);
  if (path.isEmpty())
    return QString();
  return QSettings(path, QSettings::IniFormat).value(key).toString();
}

// The desktop's theme: what the platform theme plugin reported at start-up,
// else the desktop's own settings files.
QString desktopTheme(QString *source)
{
  static const QString platform = QIcon::themeName();  // before any override
  if (!platform.isEmpty() && platform != QLatin1String("hicolor"))
  {
    if (source)
      *source = QStringLiteral("platform theme");
    return platform;
  }
  const std::pair<const char *, QString> candidates[] = {
    {"xfce xsettings", xfceTheme()},
    {"gtk-3.0 settings.ini", iniValue(QStringLiteral("gtk-3.0/settings.ini"),
                                      QStringLiteral("Settings/gtk-icon-theme-name"))},
    {"kdeglobals", iniValue(QStringLiteral("kdeglobals"), QStringLiteral("Icons/Theme"))},
  };
  for (const auto &[from, name] : candidates)
  {
    if (themeInstalled(name))
    {
      if (source)
        *source = QLatin1String(from);
      return name;
    }
  }
  if (source)
    *source = QStringLiteral("default");
  return QString();
}

} // namespace

bool canFindIconThemeWithoutPlatformTheme()
{
  const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toUpper();
  if (desktop.contains(QLatin1String("XFCE")))
    return !xfceTheme().isEmpty();
  if (desktop.contains(QLatin1String("KDE")))
    return !iniValue(QStringLiteral("kdeglobals"), QStringLiteral("Icons/Theme")).isEmpty();
  return false;
}

QString setupIconTheme(const QString &overrideTheme)
{
  desktopIconTheme();  // remember the platform's theme before overriding it
  if (!overrideTheme.isEmpty())
  {
    QIcon::setThemeName(overrideTheme);
    return QStringLiteral("config: %1").arg(overrideTheme);
  }
  QString source;
  const QString theme = desktopTheme(&source);
  if (!theme.isEmpty() && theme != QIcon::themeName())
    QIcon::setThemeName(theme);
  return QStringLiteral("%1: %2").arg(source, QIcon::themeName().isEmpty() ? QStringLiteral("hicolor") : QIcon::themeName());
}

QString desktopIconTheme()
{
  return desktopTheme(nullptr);
}

std::vector<IconThemeInfo> installedIconThemes()
{
  return scanIconThemes(QIcon::themeSearchPaths());
}

} // namespace prowal
