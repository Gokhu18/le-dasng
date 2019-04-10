/** @file template_server1.cc
 * @brief Template for a single-service server application
 *
 * Replace boerf with a suitable name.
 */
#include "dasio/appid.h"
#include "dasio/server.h"
#include "oui.h"

using namespace DAS_IO;

AppID_t DAS_IO::AppID("boerfd", "boerf server", "V1.0");

class boerf_ssclient : public Serverside_client {
  public:
    boerf_ssclient(Authenticator *Auth, const char *iname);
    // Specify how big the ibuf needs to be:
    static const int boerf_ssclient_ibufsize = 80;
    // Include whatever virtual function overrides you need here
  protected:
    ~boerf_ssclient();
    // Include any local data here
};

/**
 * @brief the Serverside_Client socket
 */
boerf_ssclient::boerf_ssclient(Authenticator *Auth, const char *iname)
    : Serverside_client(Auth, iname, boerf_ssclient_ibufsize) {}

boerf_ssclient::~boerf_ssclient() {}

Serverside_client *new_boerf_ssclient(Authenticator *Auth, SubService *SS) {
  SS = SS; // No need for this
  return new boerf_ssclient(Auth, Auth->get_client_app());
}

int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  Server S("boerf");
  // This is for a passive server like memo:
  S.set_passive_exit_threshold(1);
  S.add_subservice(new SubService("boerf", new_boerf_ssclient, (void *)0));
  S.Start(Server::Srv_Unix);
  return 0;
}
