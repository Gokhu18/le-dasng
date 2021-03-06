#ifndef NBOX_H_INCLUDED
#define NBOX_H_INCLUDED

#include "ptg_gen.h"

extern unsigned char *GetIDrule(int id);
extern int IsVertical(int index);
void NewRule( int Row, int Col, int Width, int Height, int Attr, int index );
extern PTGNode print_rules( int tblname );
extern PTGNode define_rules( int tblname );

#endif
