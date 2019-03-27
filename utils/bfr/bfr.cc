/** @file bfr.cc
 *  Telemetry Buffer
 *  est. 25 March 2019
 *  ported from ARP-DAS
 */

#include <list>
#include "rundir.h"
#include "bfr.h"
#include "oui.h"
#include "nl.h"
#include "nl_assert.h"
#include "dasio/msg.h"

using namespace DAS_IO;

AppID_t DAS_IO::AppID("bfr", "bfr server", "V1.0");

bool bfr_input_client::tmg_opened = false;
bfr_input_client *blocked_writer;
std::list<bfr_output_client*> all_readers;

int min_reader( dq_descriptor_t *tmqr ) {
  if ( tmqr != first_tmqr ) return 0;
  int min = tmqr->Qrows_expired + tmqr->n_Qrows;
  for ( ocb = all_readers; ocb != 0; ocb = ocb->next_ocb ) {
    if ( ocb->data.tmqr == tmqr && ocb->data.n_Qrows < min )
      min = ocb->data.n_Qrows;
  }
  return min;
}

bool bfr_input_client::auth_hook(Authenticator *Auth, SubService *SS) {
  // should lock the tm_queue here:
  if (tmg_opened) return false;
  tmg_opened = true;
  return true;
}

bfr_input_client::bfr_input_client(Authenticator *Auth, const char *iname, bool blocking)
    : Serverside_client(Auth, iname, bfr_input_client_ibufsize), blocking(blocking) {
  data.tmqr = 0;
  data.n_Qrows = 0;
  state = TM_STATE_HDR;
  bufsize = sizeof(part.hdr);
  buf = (char *)&part.hdr;
  write.nbrow_rec = 0;
  write.nbhdr_rec = 0;
  nc = 0;
  write.off_msg = 0;
  write.nb_rec = 0;
  write.off_queue = 0;
  data_state_eval = 0;
}

bfr_input_client::~bfr_input_client() {}

bool bfr_input_client::protocol_input() {
  blocked_writer = 0;
  
  // _IO_SET_WRITE_NBYTES( ctp, msg->i.nbytes );
  // Not necessary since we'll handle the return ourselves
  // However, that means we need to store the total message size
  // Use off_msg for total size.
  
  // We loop here as long as we have work to do. We can get out
  // if we've processed all the data in the message (nb_msg == 0)
  // or exhausted all the available space in the DQ (nbdata == 0)
  //------
  // In the porting process on 3/26/19, I will make the following replacements
  //   part.dptr replaced with buf
  //   part.nbdata replaced with bufsize
  //   write.nb_msg replace with nc
  //   ocb->rw. with nothing. See below
  //   ocb-> replace with nothing. I mean, not 'nothing', but nothing
  //   DQD_Queue.last replace with last_tmqr
  while ( nc > 0 && bufsize > 0 ) {
    int nb_read;
    nl_assert(nc <= bufsize);
    nb_read = nc < bufsize ?
      nc : bufsize;
    // memcpy(buf, &buf[cp], nb_read);
    buf += nb_read;
    bufsize -= nb_read;
    nc -= nb_read;
    write.off_msg += nb_read; // write.off_msg is maybe cp?? except it is persistent
    if ( state == TM_STATE_DATA ) {
      write.nb_rec -= nb_read;
      // write.off_rec += nb_read;
      write.off_queue += nb_read;
    }
    nl_assert(bufsize >= 0);
    if ( bufsize == 0 ) {
      switch ( state ) {
        case TM_STATE_HDR:
          if ( part.hdr.s.hdr.tm_id != TMHDR_WORD ) {
            msg(MSG_FATAL, "Invalid Message header" );
            // MsgError( write.rcvid, EINVAL );
            // Scan ahead for TMHDR_WORD
            bufsize = sizeof( part.hdr );
            buf = (char *)&part.hdr;
            return;
          }
          switch ( part.hdr.s.hdr.tm_type ) {
            case TMTYPE_INIT:
              if ( last_tmqr )
                msg(MSG_FATAL, "Second TMTYPE_INIT received" );
              write.off_queue = sizeof(tm_hdrs_t)-sizeof(tm_hdr_t);
              bufsize = sizeof(tm_info) - write.off_queue;
              buf = (char *)&tm_info;
              memcpy( buf, &part.hdr.raw[sizeof(tm_hdr_t)],
                      write.off_queue );
              buf += write.off_queue;
              state = TM_STATE_INFO;
              break;
            case TMTYPE_TSTAMP:
              if ( last_tmqr == 0 )
                msg(MSG_FATAL, "TMTYPE_TSTAMP received before _INIT" );
              lock(__FILE__, __LINE__);
              commit_tstamp(part.hdr.s.u.ts.mfc_num, part.hdr.s.u.ts.secs);
              unlock();
              new_rows++;
              state = TM_STATE_HDR; // already there!
              bufsize = sizeof( part.hdr );
              buf = (char *)&part.hdr;
              break;
            case TMTYPE_DATA_T1:
            case TMTYPE_DATA_T2:
            case TMTYPE_DATA_T3:
            case TMTYPE_DATA_T4:
              if ( last_tmqr == 0 )
                msg(MSG_FATAL, "Second TMTYPE_DATA* received before _INIT" );
              nl_assert( data_state_eval != 0 );
              nl_assert( part.hdr.s.hdr.tm_type == output_tm_type );
              write.nb_rec = part.hdr.s.u.dhdr.n_rows *
                write.nbrow_rec;
              write.off_queue = 0;
              new_rows += data_state_eval();
              // ### Make sure data_state_eval returns the number
              // of rows that have been completely added to DQ
              if ( write.nb_rec <= 0 ) {
                state = TM_STATE_HDR;
                bufsize = sizeof(part.hdr);
                buf = (char *)&part.hdr;
              } // else break out
              break;
            default:
              msg( 4, "Invalid state" );
          }
          break;
        case TM_STATE_INFO:
          // ### Check return value
          if (process_tm_info())
            msg(MSG_FATAL, "We cannot continue with this");
          blocking = false; // nonblock;
          new_rows++;
          state = TM_STATE_HDR; //### Use state-init function?
          bufsize = sizeof(part.hdr);
          buf = (char *)&part.hdr;
          break;
        case TM_STATE_DATA:
          new_rows += data_state_eval;
          if ( write.nb_rec <= 0 ) {
            state = TM_STATE_HDR;
            bufsize = sizeof(part.hdr);
            buf = (char *)&part.hdr;
          }
          break;
      }
    }
  }
  if ( nc == 0 ) {
    // MsgReply( write.rcvid, write.off_msg, 0, 0 );
    if ( new_rows ) run_read_queue();
    //### Mark us as not blocked: maybe that's nbdata != 0?
  } else {
    // We must have nbdata == 0 meaning we're going to block
    nl_assert(blocking);
    blocked_writer = this;
    run_read_queue();
  }
}

/** As soon as tm_info has been received, we can decide what
   data format to output, how much buffer space to allocate
   and in what configuration. We can then create the first
   timestamp record (with the TS in the tm_info) and the
   first dq_descriptor, albeit with no Qrows, but refrencing
   the the first timestamp. Then we can check to see if any
   readers are waiting, and initialize them.
   @return true if the incoming tm_info is insane.
*/
bool bfr_input_client::process_tm_info() {
  char *rowptr;
  int i;

  // Perform sanity checks
  if (tmi(nbminf) == 0 ||
      tmi(nbrow) == 0 ||
      tmi(nrowmajf) == 0 ||
      tmi(nrowsper) == 0 ||
      tmi(nsecsper) == 0 ||
      tmi(mfc_lsb) == tmi(mfc_msb) ||
      tmi(mfc_lsb) >= tmi(nbrow) ||
      tmi(mfc_msb) >= tmi(nbrow) ||
      tmi(nbminf) < tmi(nbrow) ||
      tmi(nbminf) % tmi(nbrow) != 0 ||
      tm_info.nrowminf != tmi(nbminf)/tmi(nbrow)) {
    msg(MSG_ERROR, "Sanity Checks failed on incoming stream" );
    return true;
  }

  // What data format should we output?
  lock(__FILE__, __LINE__);
  write.buf = NULL;

  int total_Qrows = nrowminf *
    ( ( tmi(nrowsper) * 60 + tmi(nsecsper)*tm_info.nrowminf - 1 )
        / (tmi(nsecsper)*tm_info.nrowminf) );

  init(total_Qrows, 1, false);

  switch (output_tm_type) {
    case TMTYPE_DATA_T1:
      msg(MSG_FATAL, "TMTYPE_DATA_T1 not supported");
      // write.nbhdr_rec = TM_HDR_SIZE_T1;
      // data_state_eval = data_state_T1;
      break;
    case TMTYPE_DATA_T2:
      msg(MSG_FATAL, "TMTYPE_DATA_T2 not supported");
      // write.nbhdr_rec = TM_HDR_SIZE_T2;
      // write.nbrow_rec = tmi(nbrow);
      // data_state_eval = data_state_T2;
      break;
    case TMTYPE_DATA_T3:
      write.nbhdr_rec = TM_HDR_SIZE_T3;
      write.nbrow_rec = tmi(nbrow) - 4;
      data_state_eval = data_state_T3;
      break;
    default:
      msg(4,"Invalid output_tm_type");
  }
  // if ( tmi(mfc_lsb) == 0 && tmi(mfc_msb) == 1
       // && tm_info.nrowminf == 1 ) {
    // Data_Queue.output_tm_type = TMTYPE_DATA_T3;
    // Data_Queue.nbQrow -= 4;
    // Data_Queue.nbDataHdr = TM_HDR_SIZE_T3;
		// write.nbhdr_rec = TM_HDR_SIZE_T3;
		// write.nbrow_rec = tmi(nbrow) - 4;
		// data_state_eval = data_state_T3;
  // } else if ( tm_info.nrowminf == 1 ) {
    // Data_Queue.output_tm_type = TMTYPE_DATA_T1;
    // Data_Queue.nbDataHdr = TM_HDR_SIZE_T1;
		// write.nbhdr_rec = TM_HDR_SIZE_T1;
		// data_state_eval = data_state_T1;
    // if ( tmi(nbrow) <= 4 )
      // msg(MSG_FATAL, "TM Frame with no non-synch data not supported" );
		// write.nbrow_rec = tmi(nbrow);
		// data_state_eval = data_state_T1;
  // } else {
    // Data_Queue.output_tm_type = TMTYPE_DATA_T2;
    // Data_Queue.nbDataHdr = TM_HDR_SIZE_T2;
		// write.nbhdr_rec = TM_HDR_SIZE_T2;
		// write.nbrow_rec = tmi(nbrow);
		// data_state_eval = data_state_T2;
  // }
  // Data_Queue.pbuf_size = Data_Queue.nbDataHdr + Data_Queue.nbQrow;
  // if ( Data_Queue.pbuf_size < sizeof(tm_hdr_t) + sizeof(tm_info_t) )
    // Data_Queue.pbuf_size = sizeof(tm_hdr_t) + sizeof(tm_info_t);

  // // how much buffer space to allocate?
  // // Let's default to one minute's worth, but make sure we get
  // // an integral number of minor frames
  // Data_Queue.total_Qrows = tm_info.nrowminf *
    // ( ( tmi(nrowsper) * 60 + tmi(nsecsper)*tm_info.nrowminf - 1 )
        // / (tmi(nsecsper)*tm_info.nrowminf) );
  // Data_Queue.total_size =
    // Data_Queue.nbQrow * Data_Queue.total_Qrows;
  // Data_Queue.first = Data_Queue.last = Data_Queue.full = 0;
  // Data_Queue.raw = new_memory(Data_Queue.total_size);
  // Data_Queue.row = new_memory(Data_Queue.total_Qrows * sizeof(char *));
  // rowptr = Data_Queue.raw;
  // for ( i = 0; i < Data_Queue.total_Qrows; i++ ) {
    // Data_Queue.row[i] = rowptr;
    // rowptr += Data_Queue.nbQrow;
  // }
  
  commit_tstamp(tm_info.t_stmp.mfc_num, tm_info.t_stmp.secs);
  unlock();
  return false;
}

// The job of data_state_eval is to decide how big the next
// chunk of data should be and where it should go.
// This involves finding space in the data queue and
// setting part.dptr and part.nbdata. We may need to expire
// rows from the data queue, but if we aren't nonblocking, we
// might block to allow the readers to handle what we've already
// got.
// data_state_eval() does not transfer any data via ReadMessage,
// but allocate_qrows() will copy any bytes from the hdr if
// ocb->state == TM_STATE_HDR
//
// Returns the number of rows that have been completed in the
// data queue.

// First check the data we've already moved into the queue
// The rows we read in will always begin at Data_Queue.last,
// which should be consistent with the end of the current
// tmqr data set. The number of rows is also guaranteed to
// fit within the Data_Queue without wrapping, so it should
// be safe to add nrrecd to Data_Queue.last, though it may
// be necessary to set last to zero afterwards.
//   T1->T1 just add to current tmqr without checks
//   T2->T2 Copy straight in, then verify continuity with previous
// records.
//   T3->T3 Copy straight in. Verify consecutive, etc.

int bfr_input_client::data_state_T3() {
  // int nrrecd = write.off_queue/write.nbrow_rec;
  // nl_assert(part.nbdata == 0); // redundant here: checked again in the loop below
  int nrowsfree, tot_nrrecd = 0;
  lock(__FILE__,__LINE__);
  
  {
    int nrrecd = write.off_queue/nbQrow;
    nrowsfree = 0;
    nl_assert(part.nbdata == 0);
    nl_assert(write.off_queue == nrrecd*nbQrow); // Not sure about this

    if (state == TM_STATE_DATA) {
      commit_rows(part.hdr.s.u.dhdr.mfctr, 0, nrrecd);
      part.hdr.s.u.dhdr.mfctr += nrrecd;
      write.nb_rec -= nrrecd * write.nbrow_rec;
      write.off_queue += nrrecd * write.nbrow_rec;
    }
    nrowsfree = allocate_rows(&buf);
    bufsize = nrowsfree * write.nbrow_rec;
    if (nrowsfree > 0 && state == TM_STATE_HDR) {
      // copy from hdr into buf, update accordingly
      write.off_queue = sizeof(tm_hdrs_t) - write.nbhdr_rec;
      // This could actually happen, but it shouldn't
      nl_assert( write.off_queue <= part.nbdata );
      part.nbdata -= write.off_queue;
      memcpy( buf,
              &part.hdr.raw[rw.write.nbhdr_rec],
              write.off_queue );
      buf += write.off_queue;
      write.nb_rec -= write.off_queue;
      state = TM_STATE_DATA;
    }
  }
  unlock();
  return tot_nrrecd;
}

void bfr_input_client::run_read_queue() {
  std::list<bfr_output_client*>::iterator ocbi;
  for (lock(__FILE__,__LINE__), ocbi = all_readers.start(), unlock();
       ocbi != all_readers.end();
       lock(__FILE__,__LINE__), ++ocbi, unlock()) {
    bfr_output_client *ocb = *ocbi;
    if (ocb->read.blocked) {
      ocb->read.blocked = false;
      read_reply(ocb);
    }
  }
  run_write_queue();
}

/* read_reply(ocb) is called when we know we have
   something to return on a read request.
   First determine either the largest complete record that is less
   than the requested size or the smallest complete record. If the
   smallest record is larger than the request size, allocate the
   partial buffer and copy the record into it.
   Partial buffer size, if allocated, should be the larger of the size
   of the tm_info message or a one-row data message (based on the
   assumption that if a small request comes in, the smallest full
   message will be chosen for output)

   It is assumed that ocb has already been removed from whatever wait
   queue it might have been on.
*/
void bfr_input_client::read_reply(bfr_output_client *ocb) {
  struct iovec iov[3];
  // int nb;
  
  if (! ocb->obuf_empty()) return;
  
  // if ( ocb->part.nbdata ) {
    // // nb = ocb->read.nbytes;
    // if ( ocb->part.nbdata < nb )
      // nb = ocb->part.nbdata;
    // MsgReply( ocb->read.rcvid, nb, ocb->part.dptr, nb );
    // ocb->part.dptr += nb;
    // ocb->part.nbdata -= nb;
    // // New state was already defined (or irrelevant)
  // } else 
  if ( ocb->data.tmqr == 0 ) {
    lock(__FILE__,__LINE__);
    if (next_tmqr(&ocb->data.tmqr)) {
      ocb->data.n_Qrows = 0;
    
      ocb->state = TM_STATE_HDR; // delete
      ocb->part.hdr.s.hdr.tm_id = TMHDR_WORD;
      ocb->part.hdr.s.hdr.tm_type = TMTYPE_INIT;

      // Message consists of
      //   tm_hdr_t (TMHDR_WORD, TMTYPE_INIT)
      //   tm_info_t with the current timestamp
      SETIOV( &iov[0], &ocb->part.hdr.s.hdr,
        sizeof(ocb->part.hdr.s.hdr) );
      SETIOV( &iov[1], &tm_info, sizeof(tm_info)-sizeof(tstamp_t) );
      SETIOV( &iov[2], &ocb->data.tmqr->TSq->TS, sizeof(tstamp_t) );
      nb = sizeof(tm_hdr_t) + sizeof(tm_info_t);
      do_read_reply( ocb, nb, iov, 3 );
    } // else enqueue_read( ocb, nonblock );
    unlock();
  } else {
    /* I've handled ocb->data.n_Qrows */
    dq_descriptor_t *tmqr = ocb->data.tmqr;

    lock(__FILE__,__LINE__);
    while (tmqr) {
      int nQrows_ready;
      
      /* DQD has a total of tmqr->Qrows_expired + tmqr->n_Qrows */
      if ( ocb->data.n_Qrows < tmqr->Qrows_expired ) {
        // then we've missed some data: make a note and set
        int n_missed = tmqr->Qrows_expired - ocb->data.n_Qrows;
        ocb->read.rows_missing += n_missed;
        ocb->data.n_Qrows = tmqr->Qrows_expired;
      }
      nQrows_ready = tmqr->n_Qrows + tmqr->Qrows_expired
                      - ocb->data.n_Qrows;
      assert( nQrows_ready >= 0 );
      if ( nQrows_ready > 0 ) {
        // ### make sure read.maxQrows < Data_Queue.total_Qrows
        // (it was not! fixed in io_read().
        if ( blocked_writer == 0 && dg_opened < 2 &&
             tmqr->next == 0 && nQrows_ready < ocb->read.maxQrows
             && ocb->hdr.attr->node_type == TM_DCo && !nonblock ) {
          // We want to wait for more
          // ### reasons not to wait:
          // ###   DG is blocked or has terminated
          enqueue_read( ocb, nonblock );
        } else {
          int XRow_Num, NMinf, Row_Num_start, n_iov;
          mfc_t MFCtr_start;
          int Qrow_start, nQ1, nQ2;
          
          if ( nQrows_ready > ocb->read.maxQrows )
            nQrows_ready = ocb->read.maxQrows;
          ocb->part.hdr.s.hdr.tm_id = TMHDR_WORD;
          ocb->part.hdr.s.hdr.tm_type = Data_Queue.output_tm_type;
          ocb->part.hdr.s.u.dhdr.n_rows = nQrows_ready;
          XRow_Num = tmqr->Row_num + ocb->data.n_Qrows;
          NMinf = XRow_Num/tm_info.nrowminf;
          MFCtr_start = tmqr->MFCtr + NMinf;
          Row_Num_start = XRow_Num % tm_info.nrowminf;
          switch ( Data_Queue.output_tm_type ) {
            case TMTYPE_DATA_T1: break;
            case TMTYPE_DATA_T2:
              ocb->part.hdr.s.u.dhdr.mfctr = MFCtr_start;
              ocb->part.hdr.s.u.dhdr.rownum = Row_Num_start;
              break;
            case TMTYPE_DATA_T3:
              ocb->part.hdr.s.u.dhdr.mfctr = MFCtr_start;
              break;
            default:
              nl_error(4,"Invalid output_tm_type" );
          }
          SETIOV( &iov[0], &ocb->part.hdr.s.hdr, Data_Queue.nbDataHdr );
          Qrow_start = tmqr->starting_Qrow + ocb->data.n_Qrows -
                          tmqr->Qrows_expired;
          if ( Qrow_start > Data_Queue.total_Qrows )
            Qrow_start -= Data_Queue.total_Qrows;
          nQ1 = nQrows_ready;
          nQ2 = Qrow_start + nQ1 - Data_Queue.total_Qrows;
          if ( nQ2 > 0 ) {
            nQ1 -= nQ2;
            SETIOV( &iov[2], Data_Queue.row[0], nQ2 * Data_Queue.nbQrow );
            n_iov = 3;
          } else n_iov = 2;
          SETIOV( &iov[1], Data_Queue.row[Qrow_start],
                      nQ1 * Data_Queue.nbQrow );
          ocb->data.n_Qrows += nQrows_ready;
          do_read_reply( ocb,
            Data_Queue.nbDataHdr + nQrows_ready * Data_Queue.nbQrow,
            iov, n_iov );
        }
        break; // out of while(tmqr)
      } else if ( tmqr->next ) {
        int do_TS = tmqr->TSq != tmqr->next->TSq;
        tmqr = dq_deref(tmqr, 1);
        // tmqr->ref_count++;
        ocb->data.tmqr = tmqr;
        ocb->data.n_Qrows = 0;
        if ( do_TS ) {
          ocb->part.hdr.s.hdr.tm_id = TMHDR_WORD;
          ocb->part.hdr.s.hdr.tm_type = TMTYPE_TSTAMP;
          SETIOV( &iov[0], &ocb->part.hdr.s.hdr, sizeof(tm_hdr_t) );
          SETIOV( &iov[1], &tmqr->TSq->TS, sizeof(tstamp_t) );
          do_read_reply( ocb, sizeof(tm_hdr_t)+sizeof(tstamp_t),
            iov, 2 );
          break;
        } // else loop through again
      } else {
        enqueue_read( ocb, nonblock );
        break;
      }
    }
    unlock();
  }
}

bfr_output_client::bfr_output_client(Authenticator *Auth, const char *iname, bool is_fast)
    : Serverside_client(Auth, iname, bfr_output_client_ibufsize), is_fast(is_fast) {
  data.tmqr = 0;
  data.n_Qrows = 0;
  state = TM_STATE_HDR;
  part.nbdata = 0;
  part.dptr = 0;
  read.buf = 0;
  read.nbyte = 0;
  read.maxQrows = 0;
  read.rows_missing = 0;
  all_readers.push_back(this);
}

bfr_output_client::~bfr_output_client() {
  all_readers.remove(this);
}

Serverside_client *new_bfr_input_client(Authenticator *Auth, SubService *SS) {
  bool blocking = (SS->name == "tm_bfr/input");
  return new bfr_input_client(Auth, Auth->get_client_app(), blocking);
}

Serverside_client *new_bfr_output_client(Authenticator *Auth, SubService *SS) {
  bool is_fast = (SS->name == "tm_bfr/fast");
  return new bfr_output_client(Auth, Auth->get_client_app(), is_fast);
}

void add_subservices(Server *S) {
  S->add_subservice(new SubService("tm_bfr/input", new_bfr_input_client,
      (void *)(0), bfr_input_client::auth_hook));
  S->add_subservice(new SubService("tm_bfr/input-nb", new_bfr_input_client,
      (void *)(0), bfr_input_client::auth_hook));
  S->add_subservice(new SubService("tm_bfr/optimized", new_bfr_output_client, (void *)(0)));
  S->add_subservice(new SubService("tm_bfr/fast", new_bfr_output_client, (void *)(0)));
}

/* Main method */
int main(int argc, char **argv) {
  oui_init_options(argc, argv);
  setup_rundir();
  
  Server S("tm_bfr");
  add_subservices(&S);
  S.Start(Server::Srv_Unix);
  return 0;
}