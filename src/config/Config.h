#pragma once

#include "config/Model.h"

#include <QByteArray>
#include <QStringList>

namespace prowal {

struct LoadResult
{
  bool ok = false;      // false: the file could not be used at all (see error)
  QString error;
  QStringList warnings; // problems that were worked around
  Config config;
};

// Parsing is tolerant: bad items are reported and skipped, unknown keys are kept.
LoadResult parseConfig(const QByteArray &json);
LoadResult loadConfig(const QString &path);

QByteArray serializeConfig(const Config &config);
QJsonObject itemToJson(const Item &item);

// Atomic save. The previous file (if any) is copied into backupDir first and
// only the newest keepBackups copies are kept.
bool saveConfig(const Config &config, const QString &path, const QString &backupDir,
                QString *error, int keepBackups = 10);

Config defaultConfig(const QString &terminalPrefix);

// Duplicate-cell and similar checks that don't stop the config loading.
QStringList validateGrid(const std::vector<Item> &items, const QString &where);

} // namespace prowal
