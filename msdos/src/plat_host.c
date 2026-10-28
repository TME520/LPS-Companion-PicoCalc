/* Host (Linux/gcc) platform for unit tests and headless demos. */
#include "lps.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

typedef struct {
    Platform iface;
    char rows[ROWS][COLUMNS+1];
    int reverse[ROWS];
    int palette;
    uint32_t generation;
    int active;
    int mounted;
    int write_fault;
    int capture; /* if set, dump screens to captures/NN.txt */
    int capture_n;
} HostPlat;

static HostPlat *H(Platform *p){ return (HostPlat*)p->user; }

static void h_clear(Platform *p){ HostPlat *h=H(p); int r; for(r=0;r<ROWS;r++){ memset(h->rows[r],' ',COLUMNS); h->rows[r][COLUMNS]=0; h->reverse[r]=0; } }
static void h_text(Platform *p, int row, int col, const char *s, int reverse){
    HostPlat *h=H(p); int c=col; if(row<0||row>=ROWS) return; if(reverse) h->reverse[row]=1;
    while(*s && c<COLUMNS) h->rows[row][c++]=*s++;
}
static void h_alert(Platform *p, int row, int col, const char *s){ h_text(p,row,col,s,1); }
static void h_large(Platform *p, const char *value, int red){ char b[41]; sprintf(b,"        >>>  %s  <<<",value); h_text(p,10,0,b,red); }
static void h_present(Platform *p){
    HostPlat *h=H(p); int r;
    if(h->capture){
        char path[128]; FILE *f; sprintf(path,"captures/%02d.txt", h->capture_n++);
        f=fopen(path,"w"); if(f){ for(r=0;r<ROWS;r++) fprintf(f,"%s%s\n", h->reverse[r]?"*":" ", h->rows[r]); fclose(f); }
    }
}
static void h_palette(Platform *p, int pal){ H(p)->palette=pal; }
static int h_batt(Platform *p){ (void)p; return 0; }
static enum LoadResult h_load(Platform *p, uint8_t *bytes, size_t capacity, size_t *size){
    HostPlat *h=H(p); uint32_t gen=0; int active=0; enum LoadResult r;
    if(!h->mounted) return LOAD_NO_STORAGE;
    r=save_load_ab(lps_data_dir(),bytes,capacity,size,&gen,&active);
    if(r==LOAD_OK){ h->generation=gen; h->active=active; }
    else if(r==LOAD_MISSING){ h->generation=0; h->active=0; }
    return r;
}
static int h_save(Platform *p, const uint8_t *bytes, size_t size){
    HostPlat *h=H(p);
    if(!h->mounted||h->write_fault) return 0;
    lps_ensure_dirs();
    if(!save_write_ab(lps_data_dir(),bytes,size,&h->generation,&h->active)){ h->write_fault=1; return 0; }
    return 1;
}
static int h_ics(Platform *p, const Day *day, uint32_t totalXp, uint32_t dayXp, int language){
    char d[160]; (void)p; lps_ensure_dirs(); lps_path_join(d,sizeof(d),lps_data_dir(),"EXPORT");
    return write_ics_file(d,day,totalXp,dayXp,language);
}
static int h_csv(Platform *p, const Day *day){
    char d[160]; (void)p; lps_ensure_dirs(); lps_path_join(d,sizeof(d),lps_data_dir(),"WEIGHTS");
    return write_weight_csv(d,day);
}

Platform *plat_host_create(int capture){
    static HostPlat h;
    memset(&h,0,sizeof(h));
    h.iface.user=&h;
    h.iface.clear=h_clear; h.iface.text=h_text; h.iface.alert=h_alert; h.iface.large_text=h_large;
    h.iface.present=h_present; h.iface.set_palette=h_palette; h.iface.battery_low=h_batt;
    h.iface.load=h_load; h.iface.save=h_save; h.iface.export_ics=h_ics; h.iface.export_weight_csv=h_csv;
    h.palette=PAL_GREEN; h.mounted=1; h.capture=capture;
    return &h.iface;
}

void plat_host_dump(Platform *p, FILE *out){
    HostPlat *h=H(p); int r;
    for(r=0;r<ROWS;r++) fprintf(out,"%s\n", h->rows[r]);
}
const char *plat_host_row(Platform *p, int row){ return H(p)->rows[row]; }
