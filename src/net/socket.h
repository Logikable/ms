/* TCP sockets, the same shape on POSIX and on Winsock.
 *
 * Every socket here is non-blocking: the server polls a few dozen of them from
 * one thread, and the client polls one from a thread that has to stay able to
 * shut down. So a read or a write reports what it managed and nothing waits.
 *
 * The handle is an integer rather than a platform type, which keeps
 * <winsock2.h> out of every file that touches a socket.
 */
#ifndef MS_SRC_NET_SOCKET_H_
#define MS_SRC_NET_SOCKET_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ms {

#ifdef _WIN32
using SocketHandle = uintptr_t;
inline constexpr SocketHandle kInvalidSocket = ~static_cast<uintptr_t>(0);
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

// Readies the platform's socket library. Call once before anything else here;
// calling twice is harmless. Nothing to do on POSIX, where it returns true.
bool StartSockets();

// One socket, closed when it goes out of scope. Moving one leaves the source
// holding nothing.
class Socket {
 public:
  Socket() = default;
  explicit Socket(SocketHandle handle) : handle_(handle) {
  }
  ~Socket();
  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  bool valid() const {
    return handle_ != kInvalidSocket;
  }
  SocketHandle handle() const {
    return handle_;
  }
  // Closes the socket now rather than at the end of the scope. Closing one
  // already closed does nothing.
  void Close();

 private:
  SocketHandle handle_ = kInvalidSocket;
};

// Opens a listening socket on `port`, or nothing if the port cannot be taken.
// Port 0 asks the OS for a free one -- see LocalPort, which is how a test
// finds out which.
std::optional<Socket> Listen(int port);

// Which port `socket` is bound to, or 0 if that cannot be read.
int LocalPort(const Socket& socket);

// Takes the next waiting connection off `listener`. Nothing means there was
// none, which is the ordinary answer for a socket that is not readable.
std::optional<Socket> Accept(const Socket& listener);

// Opens a connection to `host`:`port`, giving up after `timeout`. Quiet about
// failure: a client that cannot reach the server retries, and a log line per
// attempt would say the same thing forever.
std::optional<Socket> Connect(const std::string& host, int port,
                              std::chrono::milliseconds timeout);

// How a read or a write ended.
enum class IoStatus {
  kOk,
  // Nothing could be moved without waiting. Not a problem: ask again when the
  // socket next says it is ready.
  kWouldBlock,
  // The other end has gone. Whatever was already read is still worth having.
  kClosed,
  kError,
};

// Appends whatever `socket` has ready to `out`, up to what one read returns.
IoStatus Read(const Socket& socket, std::string& out);

// Writes as much of `buffer` as `socket` will take and erases what went. A
// kOk that leaves bytes behind means the socket filled up; wait for it to be
// writable and call again.
IoStatus Write(const Socket& socket, std::string& buffer);

// One socket in a wait, and what came back for it.
struct PollTarget {
  SocketHandle handle = kInvalidSocket;
  bool want_read = false;
  bool want_write = false;
  bool readable = false;
  bool writable = false;
  // The other end hung up, or the socket is in error. Read it once more --
  // there can still be bytes -- and then close it.
  bool closed = false;
};

// Waits until one of `targets` is ready or `timeout` runs out, filling in what
// each one can do. Returns false only if the wait itself failed. An empty
// `targets` sleeps for the timeout, which is what an idle server does.
bool Poll(std::vector<PollTarget>& targets, std::chrono::milliseconds timeout);

}  // namespace ms

#endif  // MS_SRC_NET_SOCKET_H_
