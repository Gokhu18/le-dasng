#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "nl.h"
#include "parent.h"
#include "oui.h"

int quit_when_childless = 0;
int parent_timeout = 0;
pid_t monitor_pid = 0;

/**
 * @brief the Serverside_Client socket
 * The parent client will support a couple commands:
 * NoOp: (actually not a command, but useful simply to connect as an
 * indication that the node is up and parent is running.
 * Status: Dump useful information about what is running
 * Quit: Shutdown
 */
parent_ssclient::parent_ssclient(Authenticator *Auth, const char *iname)
    : Serverside_client(Auth, iname, parent_ssclient_ibufsize) {}


Serverside_client *new_parent_ssclient(Authenticator *Auth, SubService *SS) {
  SS = SS; // No need for this
  return new parent_ssclient(Auth, Auth->get_client_app());
}

/**
 * @return true if shutdown is requested
 */
bool parent_ssclient::protocol_input() {
  switch (buf[0]) {
    case 'Q':
      consume(nc);
      iwrite("OK\n");
      return true;
    default:
      report_err("%s: Invalid command", iname);
      iwrite("NOK\n");
      break;
  }
  return false;
}

parent_sigif::parent_sigif(Server *srvr)
      : Interface("ParSig",0),
        srvr(srvr),
        have_children(true),
        handled_INT(false),
        handled_timeout(false) {
  if (monitor_pid && !parent_timeout)
    parent_timeout = 3;
  if (parent_timeout && !monitor_pid) {
    TO.Set(parent_timeout,0);
    flags |= Fl_Timeout;
  }
}

bool parent_sigif::serialized_signal_handler(uint32_t signals_seen) {
  if (saw_signal(signals_seen, SIGCHLD)) {
    while (true) {
      int status;
      pid_t pid;
      pid = waitpid( -1, &status, WNOHANG );
      switch (pid) {
        case 0:
          // msg( 0, "Still have children: none have died" );
          break;
        case -1:
          switch (errno) {
            case ECHILD:
              have_children = 0;
              msg( 0, "parent: No more children");
              break;
            case EINTR:
              msg( 0, "parent: signal received during waitpid()" );
              break;
            default:
              msg( 2, "parent: Unexpected error from waitpid(): %s",
                strerror(errno));
          }
          break;
        default:
          msg( 0, "parent: Process %d terminated: status: %04X", pid, status );
          if (monitor_pid && pid == monitor_pid) {
            flags |= Fl_Timeout;
            TO.Set(parent_timeout,0);
          }
          continue; // make sure there are no more
      }
      break;
    }
  }
  if (saw_signal(signals_seen, SIGINT)) {
    handled_INT = 1;
    quit_when_childless = 1;
    if ( have_children ) {
      msg( 0, "parent: Received SIGINT, signaling children");
      killpg(getpgid(getpid()), SIGHUP);
      TO.Set(3,0);
      flags |= Fl_Timeout;
    } else {
      msg( 0, "parent: Received SIGINT");
    }
  }
  return (quit_when_childless && !have_children);
}

bool parent_sigif::protocol_timeout() {
  TO.Clear();
  if ( handled_INT )
    msg( 3, "parent: Timed out waiting for children after INT");
  if ( handled_timeout )
    msg( 3, "parent: Timed out waiting for children after timeout");
  if (have_children) {
    msg( 0, "parent: Received timeout, calling killpg()");
    handled_timeout = 1;
    killpg(getpgid(getpid()), SIGHUP);
    TO.Set(3,0);
    return false;
  } else {
    msg(0, "parent: Timed out");
    return true;
  }
}

int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  Server S("parent");
  S.add_subservice(new SubService("parent", new_parent_ssclient, (void *)0));
  parent_sigif *psi = new parent_sigif(&S);
  S.ELoop.add_child(psi);
  psi->signal(SIGCHLD);
  psi->signal(SIGINT);
  psi->signal(SIGHUP); // Need to handle (ignore) HUP or I'll see my own
  psi->serialized_signal_handler(1 << (SIGCHLD-1));
  S.Start(Server::Srv_Both);
  return 0;
}

