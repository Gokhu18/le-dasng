#include "dasio/tm_gen_col.h"

using namespace DAS_IO;

unsigned short collector::majf_row = 0;
unsigned short collector::minf_row = 0;

collector::collector() : tm_generator() {
  regulated = true;
  regulation_optional = false;
}

collector::~collector() {}

void collector::init() {
  tm_generator::init(4, true);
}

void collector::event(enum tm_gen_event evt) {
  tm_generator::event(evt);
  if ( evt == tmg_event_start ) {
    next_minor_frame = majf_row = 0;
    minf_row = 0;
  }
}

void collector::commit_tstamp( mfc_t MFCtr, le_time_t time ) {
  tm_info.t_stmp.mfc_num = MFCtr;
  tm_info.t_stmp.secs = time;
  tm_generator::commit_tstamp(MFCtr, time);
}
