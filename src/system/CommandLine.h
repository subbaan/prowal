#pragma once

#include <QStringList>

namespace prowal {

struct SplitResult
{
  bool ok = false;
  QString error;
  QStringList args;
};

// Splits a command line into program + arguments the way a POSIX shell would for
// a simple command, without running a shell: whitespace separates words, '...'
// and "..." quote, backslash escapes, a word starting with '#' begins a comment,
// and a leading unquoted ~ or ~/ expands to the home directory. No variables,
// globs, pipes or redirection - items needing those set runInShell.
SplitResult splitCommand(const QString &command);

} // namespace prowal
