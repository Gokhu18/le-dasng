/** @file modbus_rtu.cc */
#include <string.h>
#include "dasio/modbus_rtu.h"
#include "nl.h"

namespace DAS_IO { namespace Modbus {
  
  RTU::RTU(const char *iname, int bufsz, const char *path, int open_flags)
      : DAS_IO::Serial(iname, bufsz, path, open_flags) {
    
  }
  
  RTU::RTU(const char *iname, int bufsz) : DAS_IO::Serial(iname, bufsz) {
    
  }
  
  RTU::~RTU() {}
  
  /**
   * Parses the incoming response
   * @return true if the event loop should terminate
   */
  bool RTU::protocol_input() {
    if (!pending) {
      report_err("%s: Unexpected input", iname);
      consume(nc);
    } else {
      if (nc < 5) return false; /* dev, cmd|err, exc. code, CRC */
      else if (!(buf[1] & 0x80) && (nc < pending->rep_sz)) return false;
      else if (buf[0] != pending->req_buf[0]) {
        report_err("%s: Reply address mismatch. Expected 0x%02X, Recd 0x%02X",
            iname, pending->req_buf[0], buf[0]);
        consume(nc);
      } else if ((buf[1] & 0x7F) != pending->req_buf[1]) {
        report_err("%s: Reply function code mismatch. Expected 0x%02X, Recd 0x%02X",
            iname, pending->req_buf[1], buf[1] & 0x7F);
        consume(nc);
      } else if (buf[1] & 0x80) {
        if (crc_ok(buf, 5)) {
          report_err("%s: Modbus error code %d", iname, buf[2]);
        } else {
          report_err("%s: CRC error on Modbus error message", iname);
        }
        nl_error(0, "%s: Request was: %s", iname, pending->ascii_escape());
        consume(nc);
      } else if (!crc_ok(buf, pending->rep_sz)) {
        nl_error(2, "%s: %s on reply", iname,
          nc > pending->rep_sz ? "CRC error + extra chars" : "CRC error");
        nl_error(0, "%s: Request was: %s", iname, pending->ascii_escape());
        consume(nc);
      } else {
        cp = pending->rep_sz;
        process_pdu();
        if (nc > cp) {
          report_err("%s: Extra chars after reply", iname);
          consume(nc);
        } else {
          report_ok(nc);
        }
      }
      dispose_pending();
    }
    process_requests();
    return false;
  }

  void RTU::process_pdu() {
    if (pending) {
      pending->process_pdu();
    }
  }
  
  /**
   * Terminates the current request/response and advances
   * to the next request.
   * @return true if the event loop should terminate
   */
  bool RTU::protocol_timeout() {
    if (pending) {
      report_err("%s: Timeout awaiting reply", iname);
      nl_error(0, "%s: Request was: %s", iname, pending->ascii_escape());
      consume(nc);
      dispose_pending();
    }
    TO.Clear();
    process_requests();
    return false;
  }

  /**
   * Reinitializes the list of poll requests.
   * @return true if the event loop should terminate
   */
  bool RTU::tm_sync() {
    if (cur_poll == polls.end()) {
      cur_poll = polls.begin();
    }
    return process_requests();
  }

  bool RTU::crc_ok(uint8_t *rep, unsigned nb) {
    if (pending) {
      unsigned short crc_rep = (rep[nb-1]<<8) + rep[nb-2];
      unsigned short crc_calc = pending->crc(rep, nb-2);
      return crc_rep == crc_calc;
    } else {
      return false;
    }
  }
  
  bool RTU::process_requests() {
    while (!pending) {
      if (!cmds.empty()) {
        pending = cmds.front();
        cmds.pop_front();
      } else if (cur_poll != polls.end()) {
        pending = *cur_poll;
        ++cur_poll;
      }
      if (pending) {
        if (pending->get_req_state() == RTU::modbus_req::Req_ready) {
          return iwrite((const char *)&pending->req_buf[0], pending->req_sz);
        } else {
          dispose_pending();
        }
      }
    }
    return false;
  }
  
  RTU::modbus_req *RTU::new_modbus_req() {
    if (req_free.empty()) {
      return new RTU::modbus_req;
    } else {
      RTU::modbus_req *req = req_free.front();
      req_free.pop_front();
      return req;
    }
  }
  
  RTU::modbus_req::modbus_req() {
    req_state = Req_unconfigured;
    device = 0;
    address = 0;
    rep_sz = req_sz = 0;
    count = 0;
    devID = 0;
  }
  
  void RTU::modbus_req::setup(RTU::modbus_device *device,
          uint8_t function_code, uint16_t address, uint16_t count) {
    uint8_t byte_count;
    this->device = device;
    this->devID = device->get_devID();
    this->count = count;
    this->address = address;
    req_buf[0] = devID;
    req_buf[1] = function_code;
    word_swap(&req_buf[2], (uint8_t*)&address);
    switch (function_code) {
      case 1:
      case 2:
        word_swap(&req_buf[4], (uint8_t*)&count);
        req_sz = 8;
        byte_count = (count+7)/8;
        rep_sz = 5+byte_count;
        req_state = Req_pre_crc;
        crc_set();
        return;
      case 3:
      case 4:
        word_swap(&req_buf[4], (uint8_t*)&count);
        req_sz = 8;
        rep_sz = 5+2*count;
        req_state = Req_pre_crc;
        crc_set();
        return;
      case 5:
      case 6:
        req_sz = 8;
        req_sz = 8;
        break;
      case 15:
        word_swap(&req_buf[4], (uint8_t*)&count);
        byte_count = (count+7)/8;
        req_buf[6] = byte_count;
        req_sz = 9 + byte_count;
        rep_sz = 8;
        break;
      case 16:
        word_swap(&req_buf[4], (uint8_t*)&count);
        byte_count = 2*count;
        req_buf[6] = byte_count;
        req_sz = 9 + byte_count;
        rep_sz = 8;
        break;
      default:
        nl_error(2, "%s: modbus_req::setup: Unsupported function code: %d", function_code);
        req_state = Req_unconfigured;
        return;
    }
    req_state = Req_addressed;
    return;
  }

  void RTU::modbus_req::setup_data(uint8_t *data) {
    if (req_state != Req_addressed) {
      nl_error(2, "%s: setup_data(): Invalid input state %d", device->get_iname(), req_state);
    } else {
      uint8_t function_code = req_buf[1];
      uint8_t byte_count;
      switch (function_code) {
        case 1:
        case 2:
        case 3:
        case 4:
          nl_error(2, "%s: setup_data() invalid for read functions", device->get_iname());
          break;
        case 5:
        case 6:
        case 16:
          nl_error(2,
            "%s: setup_data(uint8_t) incorrect data type for function_code %d",
            device->get_iname(), function_code);
          break;
        case 15:
          byte_count = req_buf[6];
          memcpy(&req_buf[7], data, byte_count);
          req_state = Req_pre_crc;
          return;
        default:
          nl_error(2, "%s: setup_data() Invalid function %d",
            device->get_iname(), function_code);
          break;
      }
    }
    req_state = Req_unconfigured;
    return;
  }

  void RTU::modbus_req::setup_data(uint16_t *data) {
    if (req_state != Req_addressed) {
      nl_error(2, "%s/%s: setup_data(): Invalid input state %d",
          device->get_iname(), device->get_dev_name(), req_state);
    } else {
      uint8_t function_code = req_buf[1];
      uint8_t word_count;
      switch (function_code) {
        case 1:
        case 2:
        case 3:
        case 4:
          nl_error(2, "%s/%s: setup_data() invalid for read functions",
            device->get_iname(), device->get_dev_name());
          break;
        case 5:
        case 6:
        case 16:
          word_swap(&word_count, &req_buf[4]);
          for (int i = 0; i < word_count; ++i) {
            word_swap(&req_buf[7+2*i], (uint8_t*)&data[i]);
          }
          req_state = Req_pre_crc;
          return;
        case 15:
          nl_error(2,
            "%s/%s: setup_data(uint8_t) incorrect data type for function_code %d",
            device->get_iname(), device->get_dev_name(), function_code);
          break;
        default:
          nl_error(2, "%s/%s: setup_data() Invalid function %d",
            device->get_iname(), device->get_dev_name(), function_code);
          break;
      }
    }
    req_state = Req_unconfigured;
    return;
  }
  
  /**
   * Requires no arguments because this is only called on the
   * request, and the buffer and size are stored in the object.
   */
  void RTU::modbus_req::crc_set() {
    if (req_state == Req_pre_crc) {
      uint16_t crc_calc = crc(req_buf, req_sz-2);
      req_buf[req_sz-2] = crc_calc & 0xFF;
      req_buf[req_sz-1] = (crc_calc>>8) & 0xFF;
      req_state = Req_ready;
    } else {
      nl_error(2, "%s: Incomplete request in crc_set(): %d", device->get_iname(),
        req_state);
      req_state = Req_unconfigured;
    }
  }

  uint16_t RTU::modbus_req::crc(uint8_t *puchMsg, uint16_t usDataLen) {

    /* Table of CRC values for high�order byte */
    static unsigned char auchCRCHi[] = {
      0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
      0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
      0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
      0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
      0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
      0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
      0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
      0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
      0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
      0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
      0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
      0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
      0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
      0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
      0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
      0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
      0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
      0x40
    };

    /* Table of CRC values for low�order byte */
    static unsigned char auchCRCLo[] = {
      0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
      0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
      0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
      0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
      0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
      0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
      0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
      0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
      0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
      0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
      0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
      0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
      0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
      0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
      0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
      0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
      0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
      0x40
    };
    
    uint8_t uchCRCHi = 0xFF ; /* high byte of CRC initialized */
    uint8_t uchCRCLo = 0xFF ; /* low byte of CRC initialized */
    uint8_t uIndex ; /* will index into CRC lookup table */
    while (usDataLen--) {
      uIndex = uchCRCLo ^ *puchMsg++ ; /* calculate the CRC */
      uchCRCLo = uchCRCHi ^ auchCRCHi[uIndex] ;
      uchCRCHi = auchCRCLo[uIndex] ;
    }
    return (uchCRCHi << 8 | uchCRCLo) ;
  }

  void RTU::modbus_req::process_pdu() {
    if (device) {
      device->process_pdu(this, address);
    }
  }
  
  /* This byte order comes from the Watlow driver, for which the documentation was very
   * sketchy. Modbus is supposed to use BigEndian, and it appears to do so on the
   * 16-bit register level, but apparently the combination of registers to form longer
   * data types (such as 32-bit floats), may be application dependent.
   * As such, verify the correct byte order, and make necessary changes.
   */
  void RTU::modbus_req::float_swap(uint8_t *dest, uint8_t *src) {
    word_swap(dest, src);
    word_swap(dest+2, src+2);
  }
  
  void RTU::modbus_req::word_swap(uint8_t *dest, uint8_t *src) {
    dest[1] = src[0];
    dest[0] = src[1];
  }

  RTU::modbus_device::modbus_device(RTU *MB,
      const char *dev_name, uint8_t dev_addr)
      : MB(MB), dev_name(dev_name), devID(devID) {
    if (!MB || !dev_name) {
      nl_error(3, "Invalid modbus_device construction");
    }
  }

  RTU::modbus_device::~modbus_device() { }

} } // Close out Modbu and DAS_IO namespaces
