/* TCP sockets, with the same interface on POSIX and Winsock.
 *
 * Every socket here is non-blocking: the server polls a few dozen from one
 * thread, and the client polls one from a thread that must stay able to shut
 * down. So reads and writes report what they managed, and nothing waits.
 *
 * The handle is an integer instead of a platform type, which keeps <winsock2.h>
 * out of every file that uses a socket.
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

// Initializes the platform's socket library. Call once before anything else
// here; calling twice is harmless. On POSIX this does nothing and returns true.
bool StartSockets();

// One socket, closed when it goes out of scope. Moving one leaves the source
// empty.
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
  // Closes the socket now instead of at the end of the scope. Closing an
  // already closed socket does nothing.
  void Close();

 private:
  SocketHandle handle_ = kInvalidSocket;
};

// Opens a listening socket on `port`, or nothing if the port is unavailable.
// Port 0 asks the OS for a free port; a test uses LocalPort to find which.
std::optional<Socket> Listen(int port);

// Which port `socket` is bound to, or 0 if that cannot be read.
int LocalPort(const Socket& socket);

// Takes the next waiting connection off `listener`. Returns nothing if there's
// none, which is normal for a socket that isn't readable.
std::optional<Socket> Accept(const Socket& listener);

// Opens a connection to `host`:`port`, giving up after `timeout`. Doesn't log
// failures: a client that can't reach the server retries, and logging each
// attempt would repeat the same line forever.
std::optional<Socket> Connect(const std::string& host, int port,
                              std::chrono::milliseconds timeout);

// How a read or a write ended.
enum class IoStatus {
  kOk,
  // Nothing could be transferred without waiting. Not an error: try again when
  // the socket is next ready.
  kWouldBlock,
  // The other end has disconnected. Anything already read is still valid.
  kClosed,
  kError,
};

// Appends whatever `socket` has ready to `out`, up to what one read returns.
IoStatus Read(const Socket& socket, std::string& out);

// Writes as much of `buffer` as `socket` accepts and erases what was sent. A
// kOk that leaves bytes behind means the socket is full; wait until it's
// writable and call again.
IoStatus Write(const Socket& socket, std::string& buffer);

// One socket to wait on, and the result for it.
struct PollTarget {
  SocketHandle handle = kInvalidSocket;
  bool want_read = false;
  bool want_write = false;
  bool readable = false;
  bool writable = false;
  // The other end hung up, or the socket has an error. Read it once more, since
  // bytes may remain, then close it.
  bool closed = false;
};

// Waits until one of `targets` is ready or `timeout` expires, filling in what
// each can do. Returns false only if the wait itself failed. An empty `targets`
// sleeps for the timeout, which is what an idle server does.
bool Poll(std::vector<PollTarget>& targets, std::chrono::milliseconds timeout);

}  // namespace ms

#endif  // MS_SRC_NET_SOCKET_H_
