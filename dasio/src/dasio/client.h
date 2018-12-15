/** @file dasio/client.h */
#ifndef DASIO_CLIENT_H_INCLUDED
#define DASIO_CLIENT_H_INCLUDED

#include "socket.h"

namespace DAS_IO {
  
  class Client : public Socket {
    public:
      typedef enum { Clt_connecting, Clt_negotiating, Clt_negotiated }
                         clt_state_t;
      Client(const char *iname, int bufsz, const char *service, const char *sub_service);
      Client(const char *iname, int bufsz, const char *hostname, const char *service,
                    const char *sub_service);
      virtual ~Client();
      //bool ProcessData(int flag);
    
      /**
       * Called via ProcessData() when new data is available to Clients at the application level.
       *
       * See protocol_input() for details about parsing.
       *
       * @return true on an error requiring termination of the driver
       */
      virtual bool app_input();
      
      /**
       * Called when the negotiation is complete. Similar to connected(), but
       * called later in the process.
       *
       * @return true on an error requiring termination of the driver
       */
      virtual bool app_connected();
      
      bool protocol_input();
      void close();
      bool connected();
      inline bool is_negotiated() { return clt_state == Clt_negotiated; }
      inline void set_subservice(const char *subs) { sub_service = subs; }
    private:
      static const int obufsize = 128;
      char obuf[obufsize];
      clt_state_t clt_state;
      const char *sub_service;
  };
  
}

#endif
