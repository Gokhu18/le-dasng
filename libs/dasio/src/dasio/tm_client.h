/** \file tm_client.h
 * TM Client Classes
 */
#ifndef TM_CLIENT_H_INCLUDED
#define TM_CLIENT_H_INCLUDED
#include <stdio.h>
#include "tm.h"
#include "tm_rcvr.h"
#include "client.h"

#ifdef __cplusplus

namespace DAS_IO {

/**
 * \brief Defines interface for tm client connection to TMbfr
 */
class tm_client : public DAS_IO::Client, public DAS_IO::tm_rcvr {
  public:
    tm_client(int bufsize, bool fast = true, const char *hostname = 0);
    // void resize_buffer(int bufsize_in);
    static char *srcnode;
    static const char *tm_client_hostname;
  protected:
    // int bfr_fd;
    bool app_input();
    bool app_connected();
    // bool tm_quit;
    virtual bool process_eof();
};

class ext_tm_client : public tm_client {
  public:
    inline ext_tm_client(int bufsize_in, const char *hostname = 0) :
      tm_client(bufsize_in, false, hostname) {}
  protected:
    void process_data();
};

}

extern "C" {
#endif

/* This is all that is exposed to a C module */
extern void tm_set_srcnode(char *nodename);

#ifdef __cplusplus
};
#endif

#endif
