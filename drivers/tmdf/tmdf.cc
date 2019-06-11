#include <errno.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <time.h>
#include "nl.h"
#include "oui.h"
#include "tmdf.h"
#include "tmdf_int.h"

using namespace DAS_IO;

DAS_IO::AppID_t DAS_IO::AppID("tmdf", "TMDF", "V1.0");

const char *df_path = "/";
const char *tmdf_name = "TMDF";
TMDF_t TMDF;

TMDF_Selectee::TMDF_Selectee( unsigned seconds, const char *name,
	void *data, unsigned short size )
    : TM_Selectee(name, data, size ) {
  fd = open(df_path, O_RDONLY);
  next = 0;
  secs = seconds;
  if (fd < 0) {
    msg( MSG_ERROR, "Error opening %s: %s", df_path, strerror(errno) );
  } else {
    report_size();
  }
}

TMDF_Selectee::~TMDF_Selectee() {
  report_size();
  if (fd >= 0) close(fd);
}

void TMDF_Selectee::report_size() {
  if (fd >= 0) {
    struct statvfs buf;
    if (fstatvfs(fd, &buf) ) {
      msg(MSG_ERROR, "fstatvfs reported %s", strerror(errno));
    } else {
      double fdsize = buf.f_blocks * buf.f_frsize;
      double used = buf.f_blocks - buf.f_bavail;
      const char *units = "Bytes";
      used = 100. * used / buf.f_blocks;
      if (fdsize > 1024*1024*1024) {
        fdsize /= 1024*1024*1024;
        units = "GB";
      } else if (fdsize > 1024*1024) {
        fdsize /= 1024*1024;
        units = "MB";
      } else if (fdsize > 1024) {
        fdsize /= 1024;
        units = "KB";
      }
      msg(0, "Drive '%s' total size: %.1lf %s: In use: %.1lf %%",
        df_path, fdsize, units, used);
    }
  }
}

bool TMDF_Selectee::tm_sync() {
  time_t now = time(NULL);
  if ( next == 0 || now >= next ) {
    next = now + secs;
    if (fd < 0)
      fd = open(df_path, O_RDONLY);
    if (fd >= 0) {
      struct statvfs buf;
      if (fstatvfs(fd, &buf) ) {
        msg(MSG_ERROR, "fstatvfs reported %s", strerror(errno));
        TMDF.usage = 65535;
      } else {
        double blks;
        blks = (buf.f_blocks - buf.f_bavail);
        blks = blks * 65534. / buf.f_blocks;
        TMDF.usage = (blks > 65534) ? 65534 : ((unsigned short)blks);
        msg(MSG_DEBUG, "f_blocks = %d  f_bavail = %d", buf.f_blocks, buf.f_bavail );
      }
    } else {
      TMDF.usage = 65535;
    }
  } else {
    //msg(-3, "next: %lu  now: %lu", next, now );
    msg(MSG_DBG(3), "next: %lu  now: %lu", next, now );
  }
  return false;
}

int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  msg(0, "Startup");
  { Selector S;
    Cmd_Selectee QC;
    TMDF_Selectee TM( 60, tmdf_name, &TMDF, sizeof(TMDF));
    S.add_child(&QC);
    S.add_child(&TM);
    S.event_loop();
  }
  msg(0, "Terminating");
}
