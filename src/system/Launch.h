#pragma once

#include "config/Model.h"

#include <QStringList>

namespace prowal {

struct LaunchPlan
{
  bool ok = false;
  QString error;
  QString program;
  QStringList arguments;
};

// Works out exactly what would be run for an item, without running it.
LaunchPlan planLaunch(const Item &item, const QString &terminalPrefix);

// Starts the item detached from Prowal. Returns an error message on failure.
bool launchItem(const Item &item, const QString &terminalPrefix, QString *error);

// False when the item's program clearly isn't installed (drawn greyed out).
// Shell commands can't be checked and count as available.
bool programAvailable(const Item &item);

// Human-readable problems for the editor; empty when fine.
QString commandProblem(const Item &item, const QString &terminalPrefix);
QString workingDirProblem(const Item &item);

// "~/x" -> "/home/me/x"
QString expandHome(const QString &path);

} // namespace prowal
