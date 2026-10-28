#ifndef ROSARY_H
#define ROSARY_H
#include "lps.h"
#define RP_SIGN 0
#define RP_CREED 1
#define RP_OPEN_OF 2
#define RP_OPEN_HAIL 3
#define RP_GLORY 4
#define RP_MYSTERY 5
#define RP_SCRIPTURE 6
#define RP_MEDITATION 7
#define RP_SILENCE 8
#define RP_OUR_FATHER 9
#define RP_HAIL 10
#define RP_FATIMA 11
#define RP_HHQ 12
#define RP_CLOSING 13
#define RP_END_SIGN 14
const char *mystery_set_name(int lang, int set);
const char *mystery_name(int lang, int set, unsigned decade);
unsigned rosary_steps(int mode);
const char *rosary_text(int lang, int mode, int set, unsigned rosaryDecade, unsigned step);
const char *rosary_part_name(int lang, int mode, unsigned rosaryDecade, unsigned step);
int rosary_part(int mode, unsigned rosaryDecade, unsigned step);
unsigned rosary_view_decade(int mode, unsigned rosaryDecade, unsigned step);
unsigned rosary_view_count(int mode, unsigned rosaryDecade, unsigned step);
#endif
