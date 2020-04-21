/* nparam.c Parametrized values for ntable (the text version ) */
#include <string.h>
#include "param.h"
#include "nctable.h"

int ColSpace = 1;

dims_t CalcWordDims(char *text, int attr) {
  dims_t wdims;

  attr = attr;
  wdims.Width.Space = strlen(text);
  wdims.Width.Glue = 0;
  wdims.Height.Space = 1;
  wdims.Height.Glue = 0;
  return wdims;
}

int DatumWidth(int ncols) {
  return ncols;
}

int DatumHeight(int nrows) {
  return nrows;
}

int RuleThickness = 1;

void preview_window( PTG_OUTPUT_FILE f, const char *name, int w, int h ) {
  if (do_output) {
    char* argv[2] = {"", "/dev/tty"};
    nct_init_options(2, argv);
    winnum = nct_init( name, w, h );
  }
}

void preview_label( PTG_OUTPUT_FILE f, const char *str, int r, int c ) {
  if (do_output) nct_string( winnum, 4, r, c, str );
}

void preview_field( PTG_OUTPUT_FILE f, int fldnum, int r, int c ) {
  if (do_output) {
    char buf[10];
    sprintf(buf, "%d", fldnum);
    nct_string( winnum, 2, r, c, buf );
  }
}

void preview_loop( PTG_OUTPUT_FILE f ) {
  if ( do_output) {
    if (nct_getch == 'q') {
      return;
    }
  }
}

void preview_vrule( PTG_OUTPUT_FILE f, int winnum, int a, int r, int c, int rule_id ) {
  if ( do_output ) {
    nct_vrule( winnum, a, r, c, GetIDrule(rule_id) );
  }
}

void preview_hrule( PTG_OUTPUT_FILE f, int winnum, int a, int r, int c, int rule_id ) {
  if ( do_output ) {
    nct_hrule( winnum, a, r, c, GetIDrule(rule_id) );
  }
}
