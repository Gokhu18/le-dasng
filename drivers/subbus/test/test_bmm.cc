/** @file test_bmm.cc
 * @brief Test interfaces to subbus modules
 */
#include <stdio.h>
#include "dasio/appid.h"
#include "oui.h"
#include "subbuspp.h"
#include "nl.h"

DAS_IO::AppID_t DAS_IO::AppID("test_bmm", "BMM Test Program", "V1.0");

typedef struct {
  uint16_t n_words;
  char name[0x50];
} device_name_t;

/* Conversion factors for output in Volts and Amps */
#define VCONV (0.025/16)
#define ICONV28 (20e-3/(16*7))
#define ICONV50 (20e-3/(16*3))
#define ADINCONV28 ((0.5e-3 * (2+29.4))/(16*2))
#define ADINCONV50 ((0.5e-3 * (2+59.0))/(16*2))

void identify_board(subbuspp *P, uint8_t bdid) {
  uint16_t bdid_hi = bdid<<8;
  msg(0,"read_subbus(0x%02X03)", bdid);
  uint16_t value = P->read_subbus(bdid_hi | 0x03);
  msg(0, "  Board Type: %d", value>>8);
  msg(0, "  Board S/N:  %d", value & 0xFF);
  value = P->read_subbus(bdid_hi | 0x02);
  msg(0, "  Build No:   %u", value);
  
  char mreqstr[30];
  snprintf(mreqstr, 30, "%X|28@%X", bdid_hi|6, bdid_hi|7);
  subbus_mread_req *mreq = P->pack_mread_request(0x29, mreqstr);
  device_name_t devname;
  uint16_t nread;
  int rv = P->mread_subbus_nw(mreq, (uint16_t*)&devname, &nread);
  if (rv < 0) {
    msg(2, "Error %d from mread", rv);
  } else {
    msg(0, "nr:%u/%u '%s'", nread, devname.n_words, &devname.name[0]);
  }
}

int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  subbuspp *P = new subbuspp("subbusd", "CAN");
  int subfunc = P->load();
  if (subfunc) {
    msg(0, "Subbus subfunction %d, name %s", subfunc, P->get_subbus_name());
  } else {
    msg(3, "Failed to connect with subbus");
  }

  identify_board(P, 1);
  
  uint16_t PM0I1 = P->read_subbus(0x0121);
  uint16_t PM0V1 = P->read_subbus(0x0122);
  uint16_t PM0V2 = P->read_subbus(0x0123);
  msg(0, "  V1: %5.2lf V", PM0V1*VCONV);
  msg(0, "  V2: %5.2lf V", PM0V2*ADINCONV28);
  msg(0, "   I: %6.3lf A", PM0I1*ICONV50);
  
  identify_board(P, 2);
  P->subbus_quit();
}
