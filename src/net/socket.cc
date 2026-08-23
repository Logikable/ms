#include "src/net/socket.h"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ms {
namespace {

#ifdef _WIN32
typedef int SockLen;
typedef char SockOptChar;

int LastError() {
  return WSAGetLastError();
}
bool IsWouldBlock(int error) {
  return error == WSAEWOULDBLOCK;
}
bool IsInProgress(int error) {
  return error == WSAEWOULDBLOCK;
}
bool IsInterrupted(int error) {
  return error == WSAEINTR;
}
void CloseHandle(SocketHandle handle) {
  closesocket(handle);
}
int PollHandles(pollfd* fds, size_t count, int timeout_ms) {
  return WSAPoll(fds, static_cast<ULONG>(count), timeout_ms);
}
#else
typedef socklen_t SockLen;
typedef void SockOptChar;

int LastError() {
  return errno;
}
bool IsWouldBlock(int error) {
  return error == EAGAIN || error == EWOULDBLOCK;
}
bool IsInProgress(int error) {
  return error == EINPROGRESS;
}
bool IsInterrupted(int error) {
  return error == EINTR;
}
void CloseHandle(SocketHandle handle) {
  close(handle);
}
int PollHandles(pollfd* fds, size_t count, int timeout_ms) {
  return poll(fds, static_cast<nfds_t>(count), timeout_ms);
}
#endif

// Every socket here is non-blocking, so this is done to each one the moment
// it exists -- the one accepted as well as the one connected.
bool SetNonBlocking(SocketHandle handle) {
#ifdef _WIN32
  u_long on = 1;
  return ioctlsocket(handle, FIONBIO, &on) == 0;
#else
  int flags = fcntl(handle, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Sends small writes straight out. The lobby would not notice, but a fight
// broadcasts a few hundred bytes every tick and Nagle would sit on them.
void SetNoDelay(SocketHandle handle) {
  int on = 1;
  setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const SockOptChar*>(&on), sizeof(on));
}

// How long the poll inside Connect may wait at once. The deadline is checked
// between waits, so an interrupted one costs at most this much.
constexpr int kConnectPollSliceMs = 100;

// Waits for `handle` to become writable, which is how a connection in
// progress reports that it has landed.
bool WaitWritable(SocketHandle handle, std::chrono::milliseconds timeout) {
  std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd entry = {};
    entry.fd = handle;
    entry.events = POLLOUT;
    int ready = PollHandles(&entry, 1, kConnectPollSliceMs);
    if (ready > 0) {
      return (entry.revents & POLLOUT) != 0;
    }
    if (ready < 0 && !IsInterrupted(LastError())) {
      return false;
    }
  }
  return false;
}

// Whether a connection that was still in progress ended up connected.
bool ConnectSucceeded(SocketHandle handle) {
  int error = 0;
  SockLen length = sizeof(error);
  if (getsockopt(handle, SOL_SOCKET, SO_ERROR,
                 reinterpret_cast<SockOptChar*>(&error), &length) != 0) {
    return false;
  }
  return error == 0;
}

// Opens one connection to one resolved address. Nothing means this address
// did not answer; the caller tries the next.
std::optional<Socket> ConnectTo(const addrinfo& address,
                                std::chrono::milliseconds timeout) {
  SocketHandle handle =
      socket(address.ai_family, address.ai_socktype, address.ai_protocol);
  if (handle == kInvalidSocket) {
    return std::nullopt;
  }
  Socket opened(handle);
  if (!SetNonBlocking(handle)) {
    return std::nullopt;
  }
  if (connect(handle, address.ai_addr,
              static_cast<SockLen>(address.ai_addrlen)) != 0) {
    if (!IsInProgress(LastError()) && !IsWouldBlock(LastError())) {
      return std::nullopt;
    }
    if (!WaitWritable(handle, timeout) || !ConnectSucceeded(handle)) {
      return std::nullopt;
    }
  }
  SetNoDelay(handle);
  return opened;
}

}  // namespace

bool StartSockets() {
#ifdef _WIN32
  static bool started = false;
  if (started) {
    return true;
  }
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    LOG(ERROR) << "WSAStartup failed";
    return false;
  }
  started = true;
#endif
  return true;
}

Socket::~Socket() {
  Close();
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
  other.handle_ = kInvalidSocket;
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    Close();
    handle_ = other.handle_;
    other.handle_ = kInvalidSocket;
  }
  return *this;
}

void Socket::Close() {
  if (handle_ != kInvalidSocket) {
    CloseHandle(handle_);
    handle_ = kInvalidSocket;
  }
}

std::optional<Socket> Listen(int port) {
  SocketHandle handle = socket(AF_INET, SOCK_STREAM, 0);
  if (handle == kInvalidSocket) {
    LOG(ERROR) << "Could not open a listening socket";
    return std::nullopt;
  }
  Socket listener(handle);
  // So a restart can take the port back rather than waiting out the last
  // connection's TIME_WAIT.
  int on = 1;
  setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const SockOptChar*>(&on), sizeof(on));

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));
  if (bind(handle, reinterpret_cast<const sockaddr*>(&address),
           sizeof(address)) != 0) {
    LOG(ERROR) << "Could not bind port " << port;
    return std::nullopt;
  }
  if (listen(handle, SOMAXCONN) != 0 || !SetNonBlocking(handle)) {
    LOG(ERROR) << "Could not listen on port " << port;
    return std::nullopt;
  }
  return listener;
}

int LocalPort(const Socket& socket) {
  sockaddr_in address = {};
  SockLen length = sizeof(address);
  if (getsockname(socket.handle(), reinterpret_cast<sockaddr*>(&address),
                  &length) != 0) {
    return 0;
  }
  return ntohs(address.sin_port);
}

std::optional<Socket> Accept(const Socket& listener) {
  SocketHandle handle = accept(listener.handle(), nullptr, nullptr);
  if (handle == kInvalidSocket) {
    if (!IsWouldBlock(LastError())) {
      LOG(WARNING) << "Accept failed: " << LastError();
    }
    return std::nullopt;
  }
  Socket accepted(handle);
  if (!SetNonBlocking(handle)) {
    return std::nullopt;
  }
  SetNoDelay(handle);
  return accepted;
}

std::optional<Socket> Connect(const std::string& host, int port,
                              std::chrono::milliseconds timeout) {
  addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) !=
      0) {
    return std::nullopt;
  }
  std::optional<Socket> connected;
  for (addrinfo* entry = found; entry != nullptr; entry = entry->ai_next) {
    connected = ConnectTo(*entry, timeout);
    if (connected.has_value()) {
      break;
    }
  }
  freeaddrinfo(found);
  return connected;
}

IoStatus Read(const Socket& socket, std::string& out) {
  char buffer[4096];
  int read = static_cast<int>(recv(socket.handle(), buffer, sizeof(buffer), 0));
  if (read > 0) {
    out.append(buffer, static_cast<size_t>(read));
    return IoStatus::kOk;
  }
  if (read == 0) {
    return IoStatus::kClosed;
  }
  return IsWouldBlock(LastError()) ? IoStatus::kWouldBlock : IoStatus::kError;
}

IoStatus Write(const Socket& socket, std::string& buffer) {
  if (buffer.empty()) {
    return IoStatus::kOk;
  }
#ifdef _WIN32
  int flags = 0;
#else
  // Without this a write to a socket the other end has closed kills the
  // process with SIGPIPE.
  int flags = MSG_NOSIGNAL;
#endif
  int written = static_cast<int>(send(socket.handle(), buffer.data(),
                                      static_cast<int>(buffer.size()), flags));
  if (written > 0) {
    buffer.erase(0, static_cast<size_t>(written));
    return IoStatus::kOk;
  }
  if (written == 0) {
    return IoStatus::kClosed;
  }
  return IsWouldBlock(LastError()) ? IoStatus::kWouldBlock : IoStatus::kError;
}

bool Poll(std::vector<PollTarget>& targets, std::chrono::milliseconds timeout) {
  std::vector<pollfd> entries;
  entries.reserve(targets.size());
  for (PollTarget& target : targets) {
    target.readable = false;
    target.writable = false;
    target.closed = false;
    pollfd entry = {};
    entry.fd = target.handle;
    if (target.want_read) {
      entry.events |= POLLIN;
    }
    if (target.want_write) {
      entry.events |= POLLOUT;
    }
    entries.push_back(entry);
  }
  int ready = PollHandles(entries.data(), entries.size(),
                          static_cast<int>(timeout.count()));
  if (ready < 0) {
    // A signal cut the wait short. Nothing is ready, and the caller's next
    // pass over its own state is exactly what a signal wants to reach.
    return IsInterrupted(LastError());
  }
  for (size_t i = 0; i < targets.size(); ++i) {
    targets[i].readable = (entries[i].revents & POLLIN) != 0;
    targets[i].writable = (entries[i].revents & POLLOUT) != 0;
    targets[i].closed =
        (entries[i].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
  }
  return true;
}

}  // namespace ms
