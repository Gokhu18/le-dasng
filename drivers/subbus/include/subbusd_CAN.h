#ifndef SUBBUSD_CAN_H_INCLUDED
#define SUBBUSD_CAN_H_INCLUDED
#include "dasio/server.h"
#include "subbusd_int.h"
#ifdef HAVE_LINUX_CAN_H
  #include <linux/can.h>
  #include <linux/can/raw.h>
#else
  struct can_frame {
    uint32_t can_id;
    uint8_t  can_dlc;
    uint8_t  data[8];
  };
  #define CAN_MTU sizeof(struct can_frame)
  #define CAN_EFF_FLAG 0x80000000U
  #define CAN_RTR_FLAG 0x40000000U
  #define CAN_ERR_FLAG 0x20000000U
#endif

#define SUBBUSD_CAN_NAME "le-das CAN driver V1.0"
extern void subbusd_CAN_init_options(int argc, char **argv);
class subbusd_CAN;

class subbusd_CAN_client : public subbusd_client {
  public:
    subbusd_CAN_client(DAS_IO::Authenticator *auth, subbusd_CAN *fl);
    ~subbusd_CAN_client();
    bool incoming_sbreq();
    void request_complete(int16_t status, uint16_t n_bytes);
    // static const int subbusd_CAN_max_req_size = 512;
    // static const int subbusd_CAN_max_rep_size = 512;
  private:
    subbusd_CAN *flavor;
    /** Where we assemble CAN messages */
    struct can_frame frame;
};

class can_request {
  public:
    inline can_request(struct can_frame *frame, uint8_t *buf, int bufsz,
      subbusd_CAN_client *clt) : frame(frame), buf(buf), bufsz(bufsz),
      clt(clt) {}
    struct can_frame *frame;
    uint8_t *buf;
    int bufsz;
    subbusd_CAN_client *clt;
};

class CAN_socket : public DAS_IO::Interface {
  public:
    CAN_socket();
    ~CAN_socket();
    /** Open and setup the CAN socket */
    void setup();
    bool protocol_input();
    void enqueue_request(struct can_frame *frame, uint8_t *rep_buf, int buflen,
        subbusd_CAN_client *clt);
  private:
    void process_requests();
    std::list<can_request> reqs;
    bool request_pending;
    uint8_t seq_no, req_no;
    uint8_t rep_seq_no;
    uint16_t rep_len;
    #ifndef HAVE_LINUX_CAN_H
    uint8_t bytectr;
    #endif
};

class subbusd_CAN : public subbusd_flavor {
  public:
    subbusd_CAN();
    ~subbusd_CAN();
    void init_subbus();
    void shutdown_subbus();
    inline void enqueue_request(struct can_frame *frame, uint8_t *rep_buf,
        int buflen, subbusd_CAN_client *clt) {
          CAN->enqueue_request(frame, rep_buf, buflen, clt);
      }
  private:
    // CAN sockets, states, etc.
    CAN_socket *CAN;
};

/* The following definitions originated in BMM can_control.h
 */
#define CAN_ID_BOARD_MASK 0x780
#define CAN_ID_BOARD(x) (((x)<<7)&CAN_ID_BOARD_MASK)
#define CAN_ID_REPLY_BIT 0x040
#define CAN_ID_REQID_MASK 0x3F
#define CAN_ID_BDREQ_MASK (CAN_ID_BOARD_MASK|CAN_ID_REQID_MASK)
#define CAN_ID_REQID(x) ((x)&CAN_ID_REQID_MASK)
#define CAN_REPLY_ID(bd,req) \
    (CAN_ID_BOARD(bd)|(req&CAN_ID_REQID)|CAN_ID_REPLY_BIT)
#define CAN_REQUEST_ID(bd,req) (CAN_ID_BOARD(bd)|(CAN_ID_REQID(req)))
#define CAN_REQUEST_MATCH(id,bd) \
    ((id & (CAN_ID_BOARD_MASK|CAN_ID_REPLY_BIT)) == CAN_ID_BOARD(bd))

#define CAN_CMD_CODE_MASK 0x7
#define CAN_CMD_CODE(x) ((x) & CAN_CMD_CODE_MASK)
#define CAN_CMD_CODE_RD 0x0
#define CAN_CMD_CODE_RD_INC 0x1
#define CAN_CMD_CODE_RD_NOINC 0x2
#define CAN_CMD_CODE_RD_CNT_NOINC 0x3
#define CAN_CMD_CODE_WR_INC 0x4
#define CAN_CMD_CODE_WR_NOINC 0x5
#define CAN_CMD_CODE_ERROR 0x6
#define CAN_CMD_SEQ_MASK 0xF8 // Can report up to 112 words without wrapping
#define CAN_SEQ_CMD(s) (((s)<<3)&CAN_CMD_SEQ_MASK)
#define CAN_CMD_SEQ(c) (((c)&CAN_CMD_SEQ_MASK)>>3)
#define CAN_CMD(cmd,seq) (CAN_CMD_CODE(cmd)|CAN_SEQ_CMD(seq))

#define CAN_MAX_TXFR 224

#define CAN_ERR_BAD_REQ_RESP 1
#define CAN_ERR_NACK 2
#define CAN_ERR_OTHER 3
#define CAN_ERR_INVALID_CMD 4
#define CAN_ERR_BAD_ADDRESS 5
#define CAN_ERR_OVERFLOW 6
#define CAN_ERR_INVALID_SEQ 7

#endif
