/*
 @File subbusd_CAN_interface.cc
 */
#include <string.h>
#include "subbusd_CAN.h"
#include "nl.h"
#include "nl_assert.h"
#ifdef HAVE_LINUX_CAN_H
  #include <sys/ioctl.h>
  #include <sys/socket.h>
  #include <linux/can.h>
  #include <linux/can/raw.h>
  #include <linux/can/error.h>
  #include <net/if.h>
#endif

CAN_interface::CAN_interface() :
    request_processing(false),
    req_no(0),
    #ifdef USE_CAN_SOCKET
      #ifndef HAVE_LINUX_CAN_H
        bytectr(0),
      #endif
    #endif
    iface(this) {}

CAN_interface::~CAN_interface() {}

void CAN_interface::setup() {
  iface.setup();
}

void CAN_interface::enqueue_request(can_msg_t *can_msg, uint8_t *rep_buf, int buflen,
        subbusd_CAN_client *clt) {
  nl_assert(can_msg);
  msg(MSG_DBG(0), "enqueuing: %d", reqs.size());
  reqs.push_back(can_request(can_msg, rep_buf, buflen, clt));
  process_requests();
}

can_request CAN_interface::curreq() {
  nl_assert(!reqs.empty());
  return reqs.front();
}

/**
 * Called
 *  - When a new request has been enqueued
 *  - When a previous request has been completed
 *  - When the current request's output has been flushed and more
 *    may be required (req.msg->sb_can_seq > 0)
 */
void CAN_interface::process_requests() {
  if (iface.request_pending || request_processing || reqs.empty()) {
    msg(MSG_DBG(0), "process_requests() no action: %s",
      iface.request_pending ? "pending" : request_processing ? "processing"
      : "reqs.empty()");
    return;
  }
  request_processing = true;
  can_request req = reqs.front();
  /* A single request might require multiple packets */
  while (!iface.request_pending && iface.obuf_clear()) {
    uint8_t req_seq_no = req.msg->sb_can_seq;
    uint16_t offset = req_seq_no ? (req_seq_no*7 - 1) : 0;
    nl_assert(offset < req.msg->sb_nb);
    uint16_t nbdata = req.msg->sb_nb - offset;
    iface.reqfrm.can_id = CAN_REQUEST_ID(req.msg->device_id,req_no);
    if (req.msg->sb_can_seq) {
      if (nbdata > 7) nbdata = 7;
      iface.reqfrm.can_dlc = nbdata+1;
      iface.reqfrm.data[0] = CAN_CMD(req.msg->sb_can_cmd,req.msg->sb_can_seq);
      memcpy(&iface.reqfrm.data[1], &req.msg->sb_can[offset], nbdata);
    } else {
      iface.rep_recd = 0;
      if (nbdata > 6) nbdata = 6;
      iface.reqfrm.can_dlc = nbdata+2;
      iface.reqfrm.data[0] = CAN_CMD(req.msg->sb_can_cmd,req_seq_no);
      iface.reqfrm.data[1] = req.msg->sb_nb;
      memcpy(&iface.reqfrm.data[2], &req.msg->sb_can[offset], nbdata);
    }
    ++req.msg->sb_can_seq;
    if (offset+nbdata >= req.msg->sb_nb) {
      iface.request_pending = true;
      ++req_no;
    }
    iface.rep_seq_no = 0;
    if (nl_debug_level <= MSG_DBG(1)) {
      char msgbuf[80];
      int nc = 0;
      nc += snprintf(&msgbuf[nc], 80-nc, "CANout ID:x%02X Data:", iface.reqfrm.can_id);
      for (int i = 0; i < iface.reqfrm.can_dlc; ++i) {
        nc += snprintf(&msgbuf[nc], 80-nc, " %02X", iface.reqfrm.data[i]);
      }
      msg(MSG_DBG(1), "%s", msgbuf);
    }
    #ifdef HAVE_LINUX_CAN_H
      iwrite((const char *)&iface.reqfrm, CAN_MTU);
      if (!iface.obuf_clear()) {
        report_err("%s: process_requests() !obuf_empty() after iwrite", iname);
        iwrite_cancel();
        reqs.pop_front();
        memset(req.msg->buf, 0, req.msg->bufsz-rep_recd);
        req.clt->request_complete(SBS_NOACK, req.msg->bufsz);
        // req.clt->request_complete(SBS_TIMEOUT, 0);
        iface.request_pending = false;
        request_processing = false;
        return;
      }
      if (iface.request_pending) {
        msg(MSG_DBG(1), "%s: Setting timeout", iname);
        TO.Set(0,10);
        flags |= DAS_IO::Interface::Fl_Timeout;
      } else {
        msg(MSG_DBG(1), "%s: Request resolved immediately", iname);
      }
    #else
      // This is development/debugging code
      if (iface.request_pending) {
        for (int i = 0; i < req.msg->bufsz; ++i) {
          req.msg->buf[i] = bytectr++;
        }
        iface.request_pending = false;
        reqs.pop_front();
        request_processing = false; // this is a hack
        req.clt->request_complete(SBS_ACK, req.msg->bufsz);
        break;
      }
    #endif
  }
  request_processing = false;
}

CAN_socket::CAN_socket(CAN_interface *parent)
  : DAS_IO::Interface("if_CAN", CAN_MTU+1),
    request_pending(false),
    parent(parent)
    {}

CAN_socket::~CAN_socket() {}

void CAN_socket::setup() {
  #ifdef HAVE_LINUX_CAN_H
  struct sockaddr_can addr;
  struct can_filter filter;
  struct ifreq ifr;
	// * family: PF_CAN
	// * type: SOCK_RAW
	// * proto: CAN_RAW
  fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    msg(3, "%s: socket() returned error %d: %s",
      errno, strerror(errno));
  }
  if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
    msg(3, "fcntl() failure in DAS_IO::Socket(%s): %s", iname,
      strerror(errno));
  }
  // interface: "CAN0"
  addr.can_family = PF_CAN;
  strcpy(ifr.ifr_name, "can0");
  if (ioctl(fd, SIOCGIFINDEX, &ifr)) {
    msg(3, "%s: ioctl() error %d: %s", iname,
      errno, strerror(errno));
  }
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    msg(3, "%s: bind() error %d: %s",
      errno, strerror(errno));
  }
  filter.can_id = CAN_ID_REPLY_BIT;
  filter.can_mask = CAN_ID_REPLY_BIT;
  if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, &filter,
      sizeof(struct can_filter)) != 0) {
    msg(3, "%s: setsockopt() error %d setting filter: %s",
      errno, strerror(errno));
  }
  // Also want to receive error packets
  can_err_mask_t err_mask = (CAN_ERR_TX_TIMEOUT | CAN_ERR_LOSTARB |
      CAN_ERR_CRTL | CAN_ERR_PROT | CAN_ERR_TRX | CAN_ERR_ACK |
      CAN_ERR_BUSOFF | CAN_ERR_BUSERROR);
  if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask,
        sizeof(err_mask)) != 0) {
    msg(3, "%s: setsockopt() error %d setting error filter: %s",
      errno, strerror(errno));
  }
  flags = DAS_IO::Interface::Fl_Read;
  #endif
}

bool CAN_socket::iwritten(int nb) {
  if (obuf_empty() && !request_pending) {
    parent->process_requests();
  }
}

const char *CAN_socket::ascii_escape() {
  static char abuf[128];
  unsigned int anc = 0;
  for (unsigned lcp = 0; lcp < nc; lcp += CAN_MTU) {
    struct can_frame *repfrm = (struct can_frame*)&buf[lcp];
    unsigned int nb = nc-lcp;
    if (nb < CAN_MTU)
      anc += snprintf(&abuf[anc], 128-anc-1, "Short(%d):", nb);
    unsigned int dlc_offset = (&(repfrm->can_dlc) - &buf[lcp]);
    if (nb >= dlc_offset) {
      anc += snprintf(&abuf[anc], 128-anc-1, " ID:%02X", repfrm->can_id);
      if (nb > dlc_offset) {
        unsigned dlc = repfrm->can_dlc;
        anc += snprintf(&abuf[anc], 128-anc-1, " DLC:%u%s",
          dlc, dlc>8 ? "!" : "");
        if (dlc > 8) dlc = 8;
        unsigned int data_offset =
          (&(repfrm->data[0]) - &buf[lcp]);
        if (nb > data_offset) {
          if (nb < data_offset+dlc)
            dlc = nb-data_offset;
          anc += snprintf(&abuf[anc], 128-anc-1, " [");
          for (int i = 0; i < dlc; ++i) {
            anc += snprintf(&abuf[anc], 128-anc-1, "%s%02X",
              i ? " " : "", repfrm->data[i]);
          }
          anc += snprintf(&abuf[anc], 128-anc-1, "]");
          if (nb > CAN_MTU) {
            anc += snprintf(&abuf[anc], 128-anc-1, "\n");
          }
        }
      }
    } else {
      anc += snprintf(&abuf[anc], 128-anc-1, "[");
      for (int i = 0; i < nb; ++i) {
        anc += snprintf(&abuf[anc], 128-anc-1, "%s%02X",
          i ? " " : "", buf[lcp+i]);
      }
      anc += snprintf(&abuf[anc], 128-anc-1, "]");
    }
  }
  return abuf;
}

bool CAN_socket::protocol_input() {
  struct can_frame *repfrm = (struct can_frame*)&buf[0];
  // reassemble response as necessary
  if (nc < CAN_MTU) return false;
  if (nc != CAN_MTU) {
    msg(0, "%s: read %d, expected %d with can_dlc=%d",
      iname, nc, CAN_MTU, repfrm->can_dlc);
    // This could happen if the frame is shortened with less data
  }
  if (!request_pending) {
    report_err("%s: Unexpected input", iname);
    consume(nc);
    return false;
  }
  can_request request = parent->curreq();
  // check for CAN error frame
  if (repfrm->can_id & (CAN_EFF_FLAG|CAN_RTR_FLAG)) {
    report_err("%s: Unexpected packet type: ID:%08X", iname, repfrm->can_id);
    consume(nc);
    return false;
  }
  if (repfrm->can_id & CAN_ERR_FLAG) {
    report_err("%s: CAN error frame ID:0x%X", iname, repfrm->can_id & CAN_ERR_MASK);
    consume(nc);
    return false;
  }
  // check incoming ID with request
  if ((repfrm->can_id & CAN_SFF_MASK) !=
      ((reqfrm.can_id & CAN_SFF_MASK) | CAN_ID_REPLY_BIT)) {
    report_err("%s: Invalid ID: %X, expected %X", iname,
      repfrm->can_id, reqfrm.can_id | CAN_ID_REPLY_BIT);
    consume(nc);
    return false;
  }
  // check incoming cmd with request
  // check incoming seq with req_seq_no
  if (repfrm->can_dlc < 2) {
    report_err("%s: DLC:%d (<2)", iname, repfrm->can_dlc);
    consume(nc);
    return false;
  }
  if (repfrm->data[0] != CAN_CMD(reqfrm.data[0],rep_seq_no)) {
    if (CAN_CMD_CODE(repfrm->data[0]) == CAN_CMD_CODE_ERROR) {
      if (repfrm->data[2] == CAN_ERR_NACK) {
        memset(request.msg->buf, 0, request.msg->bufsz - rep_recd);
        request.clt->request_complete(SBS_NOACK, request.msg->bufsz);
      } else {
        report_err("%s: CAN_ERR %d", iname, repfrm->data[1]);
        request.clt->request_complete(SBS_RESP_ERROR, 0);
      }
    } else {
      report_err("%s: req/rep cmd,seq mismatch: %02X/%02X",
        iname, repfrm->data[0], reqfrm.data[0]);
      consume(nc);
      return false;
    }
    parent->pop_req();
    // reqs.pop_front();
    request_pending = false;
    report_ok(nc);
    TO.Clear();
    parent->process_requests();
    return false;
  }
  // if seq == 0, check len with request and update
  int nbdat = repfrm->can_dlc - 1; // not counting cmd byte
  uint8_t *data = &repfrm->data[1];
  if (CAN_CMD_SEQ(repfrm->data[0]) == 0) {
    rep_len = repfrm->data[1];
    if (rep_len > request.msg->bufsz) {
      report_err("%s: reply length %d exceeds request len %d",
        iname, rep_len, request.msg->bufsz);
      consume(nc);
      return false;
    }
    --nbdat;
    ++data;
    msg(MSG_DBG(2), "rep_recd: %d", rep_recd);
  }
  // check dlc_len against remaining request len
  if (rep_recd + nbdat > rep_len) {
    report_err("%s: msg overflow. cmdseq=%02X dlc=%d rep_len=%d",
      iname, repfrm->data[0], repfrm->can_dlc, rep_len);
    consume(nc);
    return false;
  }
  if (nl_debug_level <= MSG_DBG(1)) {
    msg(MSG_DBG(1), "CANin %s", ascii_escape());
  }
  
  // copy data into reply
  memcpy(request.msg->buf, data, nbdat);
  request.msg->buf += nbdat;
  rep_recd += nbdat;
  msg(MSG_DBG(2), "Seq:%d nbdat:%d recd:%d rep_len:%d",
    rep_seq_no, nbdat, rep_recd, rep_len);
  // update rep_seq_no
  ++rep_seq_no;
  report_ok(nc);
  msg(MSG_DBG(1), "%s: Clearing timeout", iname);
  TO.Clear();
  // If request is complete, call clt->request_complete
  if (rep_recd == rep_len) {
    //reqs.pop_front();
    parent->pop_req();
    // clearing request_pending after request_complete()
    // simply limits the depth of recursion
    request.clt->request_complete(SBS_ACK, rep_len);
    request_pending = false;
    parent->process_requests();
  }
  return false;
}

bool CAN_socket::protocol_timeout() {
  TO.Clear();
  if (request_pending) {
    can_request request = parent->curreq(); // reqs.front();
    msg(MSG_DBG(0), "%s: Timeout reading from ID:0x%X", iname,
      request.msg->device_id);
    consume(nc);
    parent->pop_req(); // reqs.pop_front();
    memset(request.msg->buf, 0, request.msg->bufsz-rep_recd);
    request.clt->request_complete(SBS_NOACK, request.msg->bufsz);
    request_pending = false;
    parent->process_requests();
  } else {
    msg(1, "%s: Timeout without request_pending", iname);
  }
  return false;
}

bool CAN_socket::closed() {
  msg(0, "%s: socket closed", iname);
  return true;
}
