/** @file dasio_tm.cc */
#include "dasio_tm.h"

DAS_IO::TM::TM(const char *iname, const char *datum, const char *data,
        uint16_t size)
    : DAS_IO::Socket::Negotiate(iname, 10, "DG", 0),
      data(data), data_len(size) {
  snprintf(sub_service, subsvc_len, "data/%s", datum);
  set_subservice(sub_service);
}

// DAS_IO::TM::TM(const char *iname, const char *hostname, const char *datum,
        // const char *data, uint16_t size) {
  // snprintf(sub_service, subsvc_len, "data/%s", datum);
  // DAS_IO::Socket::Negotiate(iname, 10, hostname, "DG", sub_service);
// }

DAS_IO::TM::~TM() {}

bool DAS_IO::TM::protocol_input() {
  report_ok(nc);
  if (iwrite(data, data_len)) return true;
  if (ELoop) ELoop->set_gflag(0);
  return false;
}

bool DAS_IO::TM::connected() {
  if (is_negotiated()) {
    if (ELoop) ELoop->set_gflag(0);
    return false;
  } else return DAS_IO::Socket::Negotiate::connected();
}
