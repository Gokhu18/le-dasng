/** @file dasio_socket.cc */
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "dasio/socket.h"
#include "dasio/loop.h"
#include "dasio/msg.h"
#include "nl.h"
#include "nl_assert.h"

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

namespace DAS_IO {

Socket::Socket(const char *iname, int bufsz, const char *service) :
    Interface(iname, bufsz),
    service(service),
    hostname(0),
    is_server(false),
    socket_state(Socket_disconnected),
    socket_type(Socket_Unix)  
{
  common_init();
  // connect();
}

Socket::Socket(const char *iname, const char *service,
        socket_type_t socket_type) :
    Interface(iname, 0),
    service(service),
    hostname(0),
    is_server(true),
    socket_state(Socket_disconnected),
    socket_type(socket_type)  
{
  common_init();
  // connect();
}

Socket::Socket(Socket *S, const char *iname, int bufsize, int fd) :
  Interface(iname, bufsize),
  service(S->service),
  hostname(0),
  is_server(false),
  socket_state(Socket_connected),
  socket_type(S->socket_type)
{
  common_init();
  this->fd = fd;
  is_server_client = true;
  flags = Fl_Except | Fl_Read;
}

Socket::~Socket() {
  if (fd >= 0) {
    close();
  }
  if (unix_name) {
    delete(unix_name);
    unix_name = 0;
  }
}

const char *Socket::company = "linkeng";

void Socket::common_init() {
  unix_name = 0;
  set_retries(-1, 5, 60);
  conn_fail_reported = false;
  is_server_client = false;
}

void Socket::connect() {
  switch (socket_type) {
    case Socket_Unix:
      if (unix_name == 0) {
        unix_name = new unix_name_t();
        if (!unix_name->set_service(service)) {
          msg(3, "%s: Invalid service name", iname);
        }
      }
      if (is_server) {
        if (!unix_name->lock()) {
          socket_state = Socket_locking;
          TO.Set(0,200);
          flags |= Fl_Timeout;
          return;
        }
        if (!unix_name->claim_server()) {
          msg(3, "%s: Unable to create server socket %s",
            iname, unix_name->get_svc_name());
        }
      }
      { struct hostent *hostinfo = 0;
        struct sockaddr_un local;
        nl_assert(iname != 0 && service != 0);
       
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
          msg(4, "socket() failure in DAS_IO::Socket(%s): %s", iname,
            std::strerror(errno));
            
        local.sun_family = AF_UNIX;
        strncpy(local.sun_path, unix_name->get_svc_name(), UNIX_PATH_MAX);
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
          msg(3, "fcntl() failure in DAS_IO::Socket(%s): %s", iname,
            std::strerror(errno));
        }

        if (is_server) {
          unlink(local.sun_path);
          if (bind(fd, (struct sockaddr *)&local, SUN_LEN(&local)) < 0) {
            msg(3, "bind() failure in DAS_IO::Socket(%s,%s): %s", iname,
              local.sun_path, std::strerror(errno));
          }

          if (listen(fd, MAXPENDING) < 0) {
            msg(3, "listen() failure in DAS_IO::Socket(%s): %s", iname,
              std::strerror(errno));
          }
          socket_state = Socket_listening;
          flags = 0;
        } else {
          /* Establish the connection to the server */
          if (::connect(fd, (struct sockaddr *)&local,
                SUN_LEN(&local)) < 0) {
            if (errno != EINPROGRESS) {
              msg(2, "connect() failure in DAS_IO::Socket(%s): %s", iname,
                std::strerror(errno));
              if (reset()) {
                msg(3, "%s: Connect failure fatal after all retries", iname);
              }
              return;
            }
          }
          socket_state = Socket_connecting;
          flags = Fl_Write;
        }
        flags |= Fl_Read | Fl_Except;
      }
      break;
    default:
      msg(3, "DAS_IO_Socket::Socket::connect: not implemented for socket_type %d", socket_type);
  }
}

bool Socket::ProcessData(int flag) {
  int sock_err;

  // msg(0, "%s: Socket::ProcessData(%d)", iname, flag);
  switch (socket_state) {
    case Socket_locking:
      connect();
      return false;
    case Socket_connecting:
      if (!readSockError(&sock_err)) {
        return reset();
      }
      if ((flag & Fl_Write) &&
          (sock_err == EISCONN || sock_err == 0)) {
        // msg(0, "Connected");
        socket_state = Socket_connected;
        TO.Clear();
        flags &= ~(Fl_Write|Fl_Timeout);
        flags |= Fl_Read;
        reconn_seconds = reconn_seconds_min;
        reconn_retries = 0;
        conn_fail_reported = false;
        return connected();
      } else {
        if (!conn_fail_reported) {
          if (TO.Expired()) {
            msg(2, "%s: connect error: Client Timeout", iname);
          } else {
            msg(2, "%s: connect error %d: %s", iname, sock_err,
              strerror(sock_err));
          }
          conn_fail_reported = true;
        }
        return( reset() || connect_failed());
      }
    case Socket_listening:
      if (flag & Fl_Read) {
        if (ELoop) {
          int new_fd = accept(fd, 0, 0);
          if (new_fd < 0) {
            msg(2, "%s: Error from accept(): %s", iname, strerror(errno));
            return false;
          } else {
            if (fcntl(new_fd, F_SETFL, fcntl(new_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
              msg(3, "fcntl() failure in DAS_IO::Socket(%s): %s", iname,
                std::strerror(errno));
            }
            Socket *client = new_client("clientcon", new_fd);
            return client->connected();
          }
        } else {
          msg(1, "Socket_listening: connection sensed, but no Loop");
        }
      }
      if (TO.Expired()) {
        return protocol_timeout();
      }
      break;
    case Socket_disconnected:
      // Handle timeout (i.e. attempt reconnection)
      if (TO.Expired()) {
        // msg(0, "%s: reconnecting after timeout", iname);
        TO.Clear();
        connect();
      }
      break;
    case Socket_connected:
      return Interface::ProcessData(flag);
    default:
      msg(4, "DAS_IO::Socket::ProcessData: Invalid socket_state: %d", socket_state);
  }
  return false;
}

void Socket::set_retries(int max_retries, int min_dly, int max_foldback_dly) {
  reconn_max = max_retries;
  reconn_seconds = reconn_seconds_min = min_dly;
  reconn_seconds_max = max_foldback_dly;
}

void Socket::close() {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
    socket_state = Socket_disconnected;
    TO.Clear();
    flags &= ~(Fl_Write|Fl_Read|Fl_Except|Fl_Timeout);
    if (unix_name) {
      if (unix_name->is_server()) {
        unix_name->release_server();
      }
    }
  }
}

bool Socket::reset() {
  close();
  if (reconn_max == 0) {
    msg(2, "%s: DAS_IO::Socket::reset(): No retries requested", iname);
    return true;
  } else if (reconn_max > 0 && reconn_retries++ >= reconn_max) {
    msg(2, "%s: DAS_IO::Socket::reset(): Maximum connection retries exceeded", iname);
    return true;
  }
  int delay_secs = reconn_seconds;
  reconn_seconds *= 2;
  if (reconn_seconds > reconn_seconds_max)
    reconn_seconds = reconn_seconds_max;
  TO.Set(delay_secs,0);
  flags |= Fl_Timeout;
  return false;
}

bool Socket::read_error(int my_errno) {
  msg(2, "%s: read error %d: %s", iname,
    my_errno, strerror(my_errno));
  
  if (is_server) {
    close();
    return true;
  } else if (is_server_client) {
    close();
    if (ELoop)
      ELoop->delete_child(this);
    return false;
  } else {
    return reset();
  }
}

bool Socket::iwrite_error(int my_errno) {
  msg(2, "%s: write error %d: %s", iname,
    my_errno, strerror(my_errno));
  
  if (is_server) {
    close();
    return true;
  } else if (is_server_client) {
    close();
    if (ELoop)
      ELoop->delete_child(this);
    return false;
  } else {
    return reset();
  }
}

bool Socket::protocol_except() {
  msg(2, "Socket::protocol_except not implemented");
  return true;
}

bool Socket::closed() {
  // msg(2, "%s: Socket closed", iname);
  
  if (is_server) {
    msg(2, "%s: Should not have been reading from listening socket", iname);
    return true;
  } else if (is_server_client) {
    msg(-2, "%s: Client disconnected", iname);
    if (ELoop)
      ELoop->delete_child(this);
    return false;
  } else {
    return true;
  }
}

bool Socket::connected() { return false; }

bool Socket::connect_failed() { return false; }

Socket *Socket::new_client(const char *iname, int fd) {
  Socket *rv = new Socket(this, iname, this->bufsize, fd);
  if (ELoop) ELoop->add_child(rv);
  return rv;
}

Socket *Socket::new_client() {
  Socket *rv = new_client(this->iname, this->fd);
  return rv;
}

bool Socket::readSockError(int *sock_err) {
  socklen_t optlen = sizeof(*sock_err);

  if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
        sock_err, &optlen) == -1) {
    msg(2, "%s: Error from getsockopt() %d: %s", iname, errno, strerror(errno));
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
