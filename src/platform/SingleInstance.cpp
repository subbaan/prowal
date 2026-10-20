#include "platform/SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace prowal {

namespace {

constexpr int kMaxMessage = 64 * 1024;
constexpr int kReadTimeoutMs = 500;

bool fillAddress(const QString &path, sockaddr_un *addr)
{
  const QByteArray bytes = QFile::encodeName(path);
  if (size_t(bytes.size()) >= sizeof(addr->sun_path))
    return false;
  std::memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  std::memcpy(addr->sun_path, bytes.constData(), size_t(bytes.size()));
  return true;
}

} // namespace

SingleInstance::SingleInstance(const QString &configPath, QObject *parent) : QObject(parent)
{
  // The runtime dir is private to the user (0700); Qt makes one if XDG_RUNTIME_DIR is unset.
  QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (base.isEmpty())
    base = QDir::tempPath();
  const QByteArray hash =
    QCryptographicHash::hash(QFile::encodeName(configPath), QCryptographicHash::Sha1).toHex().left(16);
  m_path = QDir(base).filePath(QStringLiteral("prowal-editor-%1.sock").arg(QString::fromLatin1(hash)));
}

SingleInstance::~SingleInstance()
{
  if (m_fd >= 0)
  {
    ::close(m_fd);
    QFile::remove(m_path);
  }
}

bool SingleInstance::sendToRunning(const QByteArray &message)
{
  sockaddr_un addr;
  if (!fillAddress(m_path, &addr))
    return false;
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return false;
  bool sent = false;
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
  {
    qsizetype done = 0;
    while (done < message.size())
    {
      const ssize_t n = ::write(fd, message.constData() + done, size_t(message.size() - done));
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        break;
      done += n;
    }
    sent = done == message.size();
  }
  ::close(fd);
  return sent;
}

bool SingleInstance::listen(std::function<void(const QByteArray &)> handler)
{
  sockaddr_un addr;
  if (!fillAddress(m_path, &addr))
    return false;
  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return false;
  // Nobody answered sendToRunning(), so a socket file left here is stale (a crashed editor).
  ::unlink(addr.sun_path);
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || ::listen(fd, 4) != 0)
  {
    ::close(fd);
    return false;
  }
  ::chmod(addr.sun_path, 0600);
  m_fd = fd;
  m_handler = std::move(handler);
  m_notifier = new QSocketNotifier(qintptr(fd), QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this, &SingleInstance::accept);
  return true;
}

void SingleInstance::accept()
{
  const int client = ::accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
  if (client < 0)
    return;
  // The sender writes one short message and closes; read it with a time limit
  // so a stuck client can't hang the editor.
  QByteArray message;
  char buffer[4096];
  pollfd pfd{client, POLLIN, 0};
  while (message.size() < kMaxMessage && ::poll(&pfd, 1, kReadTimeoutMs) > 0)
  {
    const ssize_t n = ::read(client, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    message.append(buffer, n);
  }
  ::close(client);
  if (m_handler && !message.isEmpty())
    m_handler(message);
}

} // namespace prowal
