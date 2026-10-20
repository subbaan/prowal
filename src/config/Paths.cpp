#include "config/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace prowal::paths {

QString defaultConfigFile()
{
  const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  return QDir(base).filePath(QStringLiteral("prowal/prowal.json"));
}

QString backupDirFor(const QString &configFile)
{
  return QFileInfo(configFile).absoluteDir().filePath(QStringLiteral("backups"));
}

QString defaultApwalFile()
{
  return QDir::home().filePath(QStringLiteral(".apwalrc.xml"));
}

} // namespace prowal::paths
