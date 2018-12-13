#include "nl.h"
#include "dasio/socket.h"
#include "dasio/loop.h"

class echosrvr : public DAS_IO::Socket {
  public:
    echosrvr(const char *iname, int bufsz, const char *service, bool server=false);
    echosrvr(const char *iname, int bufsz, int fd, socket_type_t stype, const char *service, const char *hostname = 0);
    ~echosrvr();
    DAS_IO::Socket *new_client(const char *iname, int bufsz, int fd, socket_type_t stype, const char *service, const char *hostname=0);
    bool protocol_input();
    bool connected();
};

echosrvr::echosrvr(const char *iname, int bufsz, const char *service,
  bool server) : DAS_IO::Socket(iname, bufsz, service, server) {
}

echosrvr::echosrvr(const char *iname, int bufsz, int fd, socket_type_t stype, const char *service, const char *hostname)
    : DAS_IO::Socket(iname, bufsz, fd, stype, service, hostname) {
}
echosrvr::~echosrvr() {}

bool echosrvr::connected() {
  nl_error(0, "%s: connected. flags = %d", iname, flags);
}

DAS_IO::Socket *echosrvr::new_client(const char *iname, int bufsz, int fd, socket_type_t stype, const char *service, const char *hostname) {
  nl_error(0, "%s: New client connection created. %s fd = %d", this->iname, iname, fd);
  echosrvr *clt = new echosrvr(iname, bufsz, fd, stype, service, hostname);
  return clt;
}

bool echosrvr::protocol_input() {
  cp = 0;
  if (cp < nc) {
    switch (buf[cp]) {
      case 'E':
        nl_error(0, "Received '%s'", buf);
        iwrite((const char *)buf, nc, 1);
        report_ok(nc);
        return false;
      case 'Q':
        nl_error(0, "Received Quit command");
        report_ok(nc);
        return true;
      case 'C':
        nl_error(0, "Received Close command");
        report_ok(nc);
        close();
        ELoop->delete_child(this);
        return false;
      case 'A': // Close connection after acknowledge
        nl_error(0, "Received Acknowledge and close");
        iwrite("OK");
        report_ok(nc);
        close();
        ELoop->delete_child(this);
        return false;
      default:
        report_err("Unrecognized input:");
        consume(nc);
        return false;
    }
  }
}

const char *opt_string = "vo:mV";

int main(int argc, char **argv) {
  echosrvr server("IPCserver", 512, "cmd", true);
  DAS_IO::Loop ELoop;
  ELoop.add_child(&server);
  ELoop.event_loop();
  return 0;
}
