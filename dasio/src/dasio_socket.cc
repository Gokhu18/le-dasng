/** @file dasio_socket.cc */
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "sys/socket.h"
#include "sys/un.h"
#include "dasio_socket.h"
#include "nl.h"
#include "nl_assert.h"

namespace DAS_IO {

Socket::Socket(const char *iname, int bufsz, const char *service, bool server) :
    Interface(iname, bufsz),
    service(service),
    hostname(0),
    is_server(server),
    socket_state(Socket_disconnected),
    socket_type(Socket_Unix)  
{
  common_init();
  connect();
}

Socket::~Socket() {}

void Socket::common_init() {
  set_retries(-1, 5, 60);
  conn_fail_reported = false;
}

void Socket::connect() {
  reconn_seconds = reconn_seconds_min;
  reconn_retries = 0;
  
  switch (socket_type) {
    case Socket_Unix:
      { struct hostent *hostinfo = 0;
        struct sockaddr_un local;
        nl_assert(iname != 0 && service != 0);
        const char *fmt = "/var/linkeng/%s/%s";
        const char *Exp = std::getenv("Experiment");
        if (Exp == 0) {
          Exp = "none";
        }
        int svc_len = snprintf(0, 0, fmt, Exp, service);
        if (svc_len > UNIX_PATH_MAX)
          nl_error(3, "Service name /var/linkeng/%s/%s exceeds UNIX_PATH_MAX (%d) chars",
            Exp, service, UNIX_PATH_MAX);
        char *new_unix_path = (char *)new_memory(svc_len+1);
        snprintf(new_unix_path, svc_len+1, fmt, Exp, service);
        unix_path = (const char *)new_unix_path;
        
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
          nl_error(4, "socket() failure in DAS_IO::Socket(%s): %s", iname,
            std::strerror(errno));
            
        local.sun_family = AF_UNIX;
        strncpy(local.sun_path, unix_path, UNIX_PATH_MAX);
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
          nl_error(3, "fcntl() failure in DAS_IO::Socket(%s): %s", iname,
            std::strerror(errno));
        }

        if (is_server) {
          unlink(local.sun_path);
          if (bind(fd, (struct sockaddr *)&local, SUN_LEN(&local)) < 0) {
            nl_error(3, "bind() failure in DAS_IO::Socket(%s,%s): %s", iname,
              unix_path, std::strerror(errno));
          }

          if (listen(fd, MAXPENDING) < 0) {
            nl_error(3, "listen() failure in DAS_IO::Socket(%s): %s", iname,
              std::strerror(errno));
          }
          socket_state = Socket_listening;
        } else {
          /* Establish the connection to the server */
          if (::connect(fd, (struct sockaddr *)&local,
                SUN_LEN(&local)) < 0) {
            if (errno != EINPROGRESS) {
              nl_error(3, "connect() failure in DAS_IO::Socket(%s): %s", iname,
                std::strerror(errno));
            }
          }
          socket_state = Socket_connecting;
        }
        flags = Fl_Read | Fl_Except;
      }
      break;
    default:
      nl_error(3, "DAS_IO_Socket::Socket::connect: not implemented for socket_type %d", socket_type);
  }
}

bool Socket::ProcessData(int flag) {
  int sock_err;

  switch (socket_state) {
    case Socket_connecting:
      if (!readSockError(&sock_err))
        return 0;
      if ((flag & Fl_Write) &&
          (sock_err == EISCONN || sock_err == 0)) {
        nl_error(0, "Connected");
        socket_state = Socket_connected;
        TO.Clear();
        flags &= ~(Fl_Write|Fl_Timeout);
        flags |= Fl_Read;
        reconn_retries = 0;
        conn_fail_reported = false;
        connected();
        return 0;
      } else {
        if (!conn_fail_reported) {
          if (TO.Expired()) {
            nl_error(2, "connect error: Client Timeout");
          } else {
            nl_error(2, "connect error: %s",
              strerror(sock_err));
          }
        }
        reset();
        connect_failed();
        return 0;
      }
      break;
    case Socket_listening:
      nl_error(3, "Socket_listening: not implemented");
    case Socket_disconnected:
      // Handle timeout (i.e. attempt reconnection)
      if (TO.Expired()) {
        connect();
      }
      break;
    case Socket_connected:
      return Interface::ProcessData(flag);
    default:
      nl_error(4, "DAS_IO::Socket::ProcessData: Invalid socket_state: %d", socket_state);
  }
  // flag &= ~(Fl_Write|Fl_Read|
            // Fl_Except|Fl_Timeout);
  // if (flag)
    // return ProcessFlag(flag);
  // if (socket_state == Socket_writing)
    // process_requests();
  return 0;
}

void Socket::set_retries(int max_retries, int min_dly, int max_foldback_dly) {
  reconn_max = max_retries;
  reconn_seconds_min = min_dly;
  reconn_seconds_max = max_foldback_dly;
}

void Socket::close() {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
    socket_state = Socket_disconnected;
    TO.Clear();
    flags &= ~(Fl_Write|Fl_Read|Fl_Except|Fl_Timeout);
  }
}

void Socket::reset() {
  close();
  if (reconn_max == 0) {
    nl_error(3, "DAS_IO::Socket::reset(): No retries requested");
  } else if (reconn_retries++ >= reconn_max) {
    nl_error(3, "DAS_IO::Socket::reset(): Maximum connection retries exceeded");
  }
  int delay_secs = reconn_seconds;
  reconn_seconds *= 2;
  if (reconn_seconds > reconn_seconds_max)
    reconn_seconds = reconn_seconds_max;
  TO.Set(delay_secs,0);
  flags |= Fl_Timeout;
}

void Socket::connect_failed() {}

bool Socket::readSockError(int *sock_err) {
  socklen_t optlen = sizeof(*sock_err);

  if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
        sock_err, &optlen) == -1) {
    nl_error(2, "Error %d from getsockopt", errno);
    reset();
    return false;
  }
  return true;
}

/**
 * Create a TCP connection to the specified hostname and service/port.
 */
//Socket::Socket(const char *iname, int bufsz, const char *hostname, const char *service);
/**
 * Called by server when creating client interfaces after accept().
 * @param iname The interface name
 * @param bufsz Size of the input buffer
 * @fd The socket
 */
//Socket::Socket(const char *iname, int bufsz, int fd);
//Socket::~Socket();

}
