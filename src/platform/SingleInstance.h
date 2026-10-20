#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class QSocketNotifier;

namespace prowal {

// One editor per config file. The first editor listens on a Unix socket in the
// user's runtime directory (named after the config path); a second `--edit` for
// the same file sends its request there and exits, and the first editor shows it.
// Plain POSIX sockets, so the popup never loads QtNetwork.
class SingleInstance : public QObject
{
  Q_OBJECT

public:
  explicit SingleInstance(const QString &configPath, QObject *parent = nullptr);
  ~SingleInstance() override;

  // Hands `message` to a running instance. True if one took it.
  bool sendToRunning(const QByteArray &message);

  // Starts listening; `handler` gets each message from later instances.
  // False if the socket couldn't be created (then every editor just runs alone).
  bool listen(std::function<void(const QByteArray &)> handler);

  const QString &socketPath() const { return m_path; }

private:
  void accept();

  QString m_path;
  int m_fd = -1;
  QSocketNotifier *m_notifier = nullptr;
  std::function<void(const QByteArray &)> m_handler;
};

} // namespace prowal
