#include "system/CommandLine.h"

#include <QDir>

namespace prowal {

SplitResult splitCommand(const QString &command)
{
  enum class State { Normal, Single, Double };

  SplitResult r;
  State state = State::Normal;
  QString word;
  bool inWord = false;
  bool tilde = false;  // word began with an unquoted '~'
  const qsizetype n = command.size();

  auto finishWord = [&] {
    if (tilde && (word == QLatin1String("~") || word.startsWith(QLatin1String("~/"))))
      word.replace(0, 1, QDir::homePath());
    r.args.append(word);
    word.clear();
    inWord = false;
    tilde = false;
  };

  for (qsizetype i = 0; i < n; ++i)
  {
    const QChar c = command.at(i);
    switch (state)
    {
    case State::Normal:
      if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n'))
      {
        if (inWord)
          finishWord();
      }
      else if (c == QLatin1Char('#') && !inWord)
      {
        while (i + 1 < n && command.at(i + 1) != QLatin1Char('\n'))
          ++i;
      }
      else if (c == QLatin1Char('\''))
      {
        state = State::Single;
        inWord = true;
      }
      else if (c == QLatin1Char('"'))
      {
        state = State::Double;
        inWord = true;
      }
      else if (c == QLatin1Char('\\'))
      {
        if (i + 1 < n)
        {
          ++i;
          if (command.at(i) == QLatin1Char('\n'))
            continue;  // line continuation
          word += command.at(i);
        }
        else
          word += c;
        inWord = true;
      }
      else
      {
        if (!inWord && c == QLatin1Char('~'))
          tilde = true;
        word += c;
        inWord = true;
      }
      break;

    case State::Single:
      if (c == QLatin1Char('\''))
        state = State::Normal;
      else
        word += c;
      break;

    case State::Double:
      if (c == QLatin1Char('"'))
        state = State::Normal;
      else if (c == QLatin1Char('\\') && i + 1 < n &&
               QStringLiteral("\"\\$`\n").contains(command.at(i + 1)))
      {
        ++i;
        if (command.at(i) != QLatin1Char('\n'))
          word += command.at(i);
      }
      else
        word += c;
      break;
    }
  }

  if (state != State::Normal)
  {
    r.error = state == State::Single ? QStringLiteral("unterminated ' quote")
                                     : QStringLiteral("unterminated \" quote");
    r.args.clear();
    return r;
  }
  if (inWord)
    finishWord();
  r.ok = true;
  return r;
}

} // namespace prowal
