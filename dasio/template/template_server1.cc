/** @file template_server1.cc
 * @brief Template for a single-service server application
 *
 * Replace boerf with a suitable name.
 */
#include "dasio/appid.h"
#include "dasio/server.h"
#include "oui.h"

using namespace DAS_IO;

AppID_t AppID("boerfd", "boerf server", "V1.0");

class boerf_ssclient : public Serverside_Client {
  public:
    boerf_ssclient(Authenticator *Auth, const char *iname);
    ~boerf_ssclient();
    // Include whatever virtual function overrides you need here
  protected:
    // Include any local data here
};

/**
 * @brief the Serverside_Client socket
 */
boerf_ssclient::boerf_ssclient(Authenticator *Auth, const char *iname)
    : Serverside_Client(Auth, iname) {}

boerf_ssclient::~boerf_ssclient() {}

boerf_ssclient *new_boerf_ssclient(Authenticator *Auth, SubService *SS) {
  SS = SS; // No need for this
  return new boerf_ssclient(Auth, Auth->get_client_app());
}

int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  Server S("boerf");
  S.Subs.add_subservice(new SubService("boerf", (socket_clone_t)new_boerf_ssclient, (void *)0));
  S.Start(Srv_Unix);
}