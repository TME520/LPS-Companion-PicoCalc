/* Dual SAVE_A.BIN / SAVE_B.BIN with LPR1 magic, generation, CRC (firmware parity).
 * Large buffers in BSS. Write path: ONE slot only, no verify-read (slow on 8088 HDD). */
#include "lps.h"
#include <stdio.h>
#include <string.h>

static uint8_t g_rec_a[RECORD_SIZE];
static uint8_t g_rec_b[RECORD_SIZE];

static void put32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t get32(const uint8_t *p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

static int read_record(const char *path, uint8_t *record, size_t *record_size){
    FILE *f; long sz; size_t n;
    f=fopen(path,"rb"); if(!f) return -1;
    if(fseek(f,0,SEEK_END)!=0){ fclose(f); return 0; }
    sz=ftell(f); if(sz<=0){ fclose(f); return 0; }
    rewind(f);
    if((size_t)sz > RECORD_SIZE){ fclose(f); return 0; }
    n=fread(record,1,(size_t)sz,f); fclose(f);
    if(n!=(size_t)sz) return 0;
    *record_size=n; return 1;
}

static int record_valid(const uint8_t *record, size_t record_size, size_t *payload_size, uint32_t *generation){
    size_t ok_sizes[]={RECORD_SIZE, WIRE_V5+16, WIRE_V4+16, WIRE_V3+16, WIRE_V2+16, WIRE_V1+16};
    unsigned i; int right=0; uint32_t magic, gen, size, crc;
    for(i=0;i<6;i++) if(record_size==ok_sizes[i]){ right=1; break; }
    if(!right || record_size<16) return 0;
    magic=get32(record); gen=get32(record+4); size=get32(record+8);
    if(magic!=SAVE_MAGIC) return 0;
    if(size+16!=record_size) return 0;
    crc=get32(record+record_size-4);
    if(crc!=crc32(record,record_size-4)) return 0;
    if(!decode_state(record+12, size, &lps_decode_scratch)) return 0;
    *payload_size=size; *generation=gen; return 1;
}

enum LoadResult save_load_ab(const char *lps_dir, uint8_t *payload, size_t cap, size_t *size, uint32_t *generation_out, int *active_out){
    char pa[200], pb[200];
    size_t sa=0, sb=0, ps_a=0, ps_b=0;
    uint32_t ga=0, gb=0;
    int va=0, vb=0, ma, mb;
    lps_path_join(pa,sizeof(pa),lps_dir,"SAVE_A.BIN");
    lps_path_join(pb,sizeof(pb),lps_dir,"SAVE_B.BIN");
    ma=read_record(pa,g_rec_a,&sa); mb=read_record(pb,g_rec_b,&sb);
    if(ma==1) va=record_valid(g_rec_a,sa,&ps_a,&ga);
    if(mb==1) vb=record_valid(g_rec_b,sb,&ps_b,&gb);
    if(ma==-1 && mb==-1) return LOAD_MISSING;
    if(!va && !vb) return LOAD_ERROR;
    if(vb && (!va || gb>ga)){
        if(ps_b>cap) return LOAD_ERROR;
        memcpy(payload, g_rec_b+12, ps_b); *size=ps_b;
        if(generation_out) *generation_out=gb;
        if(active_out) *active_out=1;
        return LOAD_OK;
    }
    if(ps_a>cap) return LOAD_ERROR;
    memcpy(payload, g_rec_a+12, ps_a); *size=ps_a;
    if(generation_out) *generation_out=ga;
    if(active_out) *active_out=0;
    return LOAD_OK;
}

int save_write_ab(const char *lps_dir, const uint8_t *payload, size_t size, uint32_t *generation, int *active){
    char path[200]; FILE *f; unsigned slot; size_t n;
    if(size!=WIRE_SIZE) return 0;
    if(*generation==0xfffffffful) return 0;
    /* Alternate ONE slot per commit (Companion parity) — never write A and B together. */
    slot = (*active==0)?1u:0u;
    memset(g_rec_a,0,sizeof(g_rec_a));
    put32(g_rec_a, SAVE_MAGIC);
    put32(g_rec_a+4, (*generation)+1);
    put32(g_rec_a+8, (uint32_t)size);
    memcpy(g_rec_a+12, payload, size);
    put32(g_rec_a+RECORD_SIZE-4, crc32(g_rec_a, RECORD_SIZE-4));
    lps_path_join(path,sizeof(path),lps_dir, slot? "SAVE_B.BIN":"SAVE_A.BIN");
    f=fopen(path,"wb"); if(!f) return 0;
    n=fwrite(g_rec_a,1,RECORD_SIZE,f);
    /* fclose flushes; no verify-read (was encode+CRC + full read+decode on slow HDD). */
    if(n!=RECORD_SIZE || fclose(f)!=0){ remove(path); return 0; }
    *active=(int)slot; (*generation)++;
    return 1;
}
