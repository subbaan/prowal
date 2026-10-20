#pragma once

#include <QString>

namespace prowal::paths {

QString defaultConfigFile();               // $XDG_CONFIG_HOME/prowal/prowal.json
QString backupDirFor(const QString &configFile); // <config dir>/backups
QString defaultApwalFile();                // ~/.apwalrc.xml

} // namespace prowal::paths
