#pragma once

#include "config/Model.h"

#include <QStringList>

namespace prowal {

struct ImportResult
{
  bool ok = false;
  QString error;
  QStringList warnings;
  int imported = 0;
  int skipped = 0;
  std::vector<Item> items;
  bool hasTimeout = false;
  int timeoutMs = 0;
  bool hasCloseAfterLaunch = false;
  bool closeAfterLaunch = true;
};

// Reads an apwal layout (~/.apwalrc.xml). The file is only ever opened read-only.
ImportResult readApwalFile(const QString &path);
ImportResult parseApwalXml(const QByteArray &xml);

// Replaces the given grid's items with the imported ones and applies the imported
// behaviour settings. Other grids and settings are left alone.
void applyApwalImport(const ImportResult &import, Config *config, const QString &gridName);

// "/usr/bin/gimp-2.10 --new" -> "Gimp 2.10"
QString nameFromCommand(const QString &command);

} // namespace prowal
