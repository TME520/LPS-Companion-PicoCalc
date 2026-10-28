/* DOS platform: BIOS-safe text for 8088 (Book 8088 / CGA / MDA). C89. */
#include "lps.h"
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <conio.h>

typedef struct {
    Platform iface;
    char cells[ROWS][COLUMNS+1];
    char reverse[ROWS];
    int palette;
    uint32_t generation;
    int active; /* 0=A, 1=B */
    int write_fault;
    int mounted;
    unsigned video_seg; /* 0xB800 colour or 0xB000 mono */
    int mono;
    int use_bios; /* 1 = int 10h teletype only (safest) */
} DosPlat;

static DosPlat *self_of(Platform *p){ return (DosPlat*)p->user; }

/* Early banner via BIOS teletype — works before any mode / VRAM setup. */
void plat_dos_bios_puts(const char *s){
    union REGS r;
    while(*s){
        if(*s=='\n'){
            r.h.ah=0x0E; r.h.al='\r'; r.h.bl=0x07; int86(0x10,&r,&r);
            r.h.ah=0x0E; r.h.al='\n'; r.h.bl=0x07; int86(0x10,&r,&r);
        }else{
            r.h.ah=0x0E; r.h.al=(unsigned char)*s; r.h.bl=0x07; int86(0x10,&r,&r);
        }
        s++;
    }
}

static void detect_video(DosPlat *d){
    union REGS r;
    unsigned equip;
    /* Equipment word at 0040:0010 — bit 4-5: 3=mono, else CGA/colour */
#if defined(__WATCOMC__)
    equip = *(unsigned far *)MK_FP(0x40, 0x10);
#else
    equip = *(unsigned *)0x410;
#endif
    d->mono = ((equip >> 4) & 3) == 3;

    /* Current BIOS mode */
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    if(r.h.al == 0x07) d->mono = 1;

    d->video_seg = d->mono ? 0xB000 : 0xB800;
    d->use_bios = 0; /* prefer direct VRAM with correct segment; BIOS fallback available */
}

static void ensure_text_mode(DosPlat *d){
    union REGS r;
    /* Only switch if not already in a usable 80-col text mode */
    r.h.ah = 0x0F;
    int86(0x10, &r, &r);
    if(d->mono){
        if(r.h.al != 0x07){
            r.h.ah = 0x00; r.h.al = 0x07; /* 80x25 mono */
            int86(0x10, &r, &r);
        }
    }else{
        /* Modes 2/3 are 80x25 colour; leave alone if already there */
        if(r.h.al != 0x02 && r.h.al != 0x03){
            r.h.ah = 0x00; r.h.al = 0x03;
            int86(0x10, &r, &r);
        }
    }
    /* Hide hardware cursor (optional; ignore failure) */
    r.h.ah = 0x01; r.h.ch = 0x20; r.h.cl = 0x00;
    int86(0x10, &r, &r);
}

static void dos_clear(Platform *p){
    DosPlat *d=self_of(p); int r;
    for(r=0;r<ROWS;r++){ memset(d->cells[r],' ',COLUMNS); d->cells[r][COLUMNS]=0; d->reverse[r]=0; }
}
static void dos_text(Platform *p, int row, int col, const char *s, int reverse){
    DosPlat *d=self_of(p); int c=col;
    if(row<0||row>=ROWS) return;
    if(reverse) d->reverse[row]=1;
    while(*s && c<COLUMNS){ d->cells[row][c++]=*s++; }
}
static void dos_alert(Platform *p, int row, int col, const char *s){ dos_text(p,row,col,s,1); }
static void dos_large(Platform *p, const char *value, int red){
    char line[41];
    sprintf(line,"        >>>  %s  <<<", value);
    dos_text(p, 10, 0, line, red);
    (void)red;
}
static unsigned char attr_for(DosPlat *d, int reverse){
    unsigned char fg;
    if(d->mono){
        /* MDA: bright = 0x0F, normal 0x07, reverse 0x70 */
        if(reverse) return 0x70;
        return 0x07;
    }
    switch(d->palette){
    case PAL_RED: fg=0x0C; break;
    case PAL_BLUE: fg=0x09; break;
    case PAL_ORANGE: fg=0x06; break;
    case PAL_PINK: fg=0x0D; break;
    default: fg=0x0A; break;
    }
    if(reverse) return (unsigned char)((fg<<4)|0x00);
    return (unsigned char)(fg);
}

static void present_bios(DosPlat *d){
    union REGS r; int row, col;
    /* Home cursor */
    r.h.ah=0x02; r.h.bh=0; r.h.dh=0; r.h.dl=0; int86(0x10,&r,&r);
    for(row=0;row<25;row++){
        for(col=0;col<80;col++){
            char ch = (row<ROWS && col<COLUMNS) ? d->cells[row][col] : ' ';
            unsigned char attr = attr_for(d, (row<ROWS)?d->reverse[row]:0);
            /* Write char+attr at cursor, advance */
            r.h.ah=0x09; r.h.al=(unsigned char)ch; r.h.bh=0; r.h.bl=attr;
            r.x.cx=1; int86(0x10,&r,&r);
            /* Advance cursor */
            r.h.ah=0x02; r.h.bh=0; r.h.dh=(unsigned char)row;
            r.h.dl=(unsigned char)(col+1);
            if(r.h.dl>=80){ r.h.dl=0; r.h.dh=(unsigned char)(row+1); }
            int86(0x10,&r,&r);
        }
    }
}

static void present_vram(DosPlat *d){
    int r,c;
#if defined(__WATCOMC__)
    unsigned char far *vid = (unsigned char far *)MK_FP(d->video_seg, 0);
#else
    unsigned char *vid = (unsigned char*)((unsigned long)d->video_seg << 4);
#endif
    for(r=0;r<25;r++){
        unsigned char attr = attr_for(d, (r<ROWS)?d->reverse[r]:0);
        for(c=0;c<80;c++){
            char ch = (r<ROWS && c<COLUMNS) ? d->cells[r][c] : ' ';
            vid[(r*80+c)*2]=(unsigned char)ch;
            vid[(r*80+c)*2+1]=attr;
        }
    }
}

static void dos_present(Platform *p){
    DosPlat *d=self_of(p);
    if(d->use_bios) present_bios(d);
    else present_vram(d);
}
static void dos_set_palette(Platform *p, int palette){ self_of(p)->palette=palette; }
static int dos_battery(Platform *p){ (void)p; return 0; }

static enum LoadResult dos_load(Platform *p, uint8_t *bytes, size_t capacity, size_t *size){
    DosPlat *d=self_of(p); uint32_t gen=0; int active=0; enum LoadResult r;
    if(!d->mounted) return LOAD_NO_STORAGE;
    r=save_load_ab(lps_data_dir(), bytes, capacity, size, &gen, &active);
    if(r==LOAD_OK){ d->generation=gen; d->active=active; }
    else if(r==LOAD_MISSING){ d->generation=0; d->active=0; }
    return r;
}
static int dos_save(Platform *p, const uint8_t *bytes, size_t size){
    DosPlat *d=self_of(p);
    if(!d->mounted || d->write_fault) return 0;
    lps_ensure_dirs();
    if(!save_write_ab(lps_data_dir(), bytes, size, &d->generation, &d->active)){ d->write_fault=1; return 0; }
    return 1;
}
static int dos_export_ics(Platform *p, const Day *day, uint32_t totalXp, uint32_t dayXp, int language){
    char export_dir[160];
    (void)p;
    lps_ensure_dirs();
    lps_path_join(export_dir,sizeof(export_dir),lps_data_dir(),"EXPORT");
    return write_ics_file(export_dir, day, totalXp, dayXp, language);
}
static int dos_export_csv(Platform *p, const Day *day){
    char weights_dir[160];
    (void)p;
    lps_ensure_dirs();
    lps_path_join(weights_dir,sizeof(weights_dir),lps_data_dir(),"WEIGHTS");
    return write_weight_csv(weights_dir, day);
}

static int probe_storage(void){
    char test[160]; FILE *f;
    lps_ensure_dirs();
    lps_path_join(test,sizeof(test),lps_data_dir(),"WRTEST.TMP");
    f=fopen(test,"wb"); if(!f) return 0;
    fputc('x',f); fclose(f); remove(test); return 1;
}

Platform *plat_dos_create(void){
    static DosPlat d;
    memset(&d,0,sizeof(d));
    d.iface.user=&d;
    d.iface.clear=dos_clear;
    d.iface.text=dos_text;
    d.iface.alert=dos_alert;
    d.iface.large_text=dos_large;
    d.iface.present=dos_present;
    d.iface.set_palette=dos_set_palette;
    d.iface.battery_low=dos_battery;
    d.iface.load=dos_load;
    d.iface.save=dos_save;
    d.iface.export_ics=dos_export_ics;
    d.iface.export_weight_csv=dos_export_csv;
    d.palette=PAL_GREEN;
    detect_video(&d);
    ensure_text_mode(&d);
    /* Storage probe AFTER video so a slow/failing disk never blanks with no banner */
    d.mounted=probe_storage();
    return &d.iface;
}

int plat_dos_get_key(void){
    int ch;
    if(!kbhit()) return -1;
    ch=getch();
    if(ch==0 || ch==0xE0){
        ch=getch();
        switch(ch){
        case 72: return KEY_UP;
        case 80: return KEY_DOWN;
        case 75: return KEY_LEFT;
        case 77: return KEY_RIGHT;
        case 83: return KEY_DELETE;
        case 59: return KEY_F1;
        case 60: return KEY_F2;
        case 61: return KEY_F3;
        case 62: return KEY_F4;
        case 63: return KEY_F5;
        default: return -1;
        }
    }
    if(ch==13) return KEY_ENTER;
    if(ch==27) return KEY_ESCAPE;
    if(ch==8) return KEY_BACKSPACE;
    if(ch==9) return KEY_TAB;
    if(ch>=32 && ch<=126) return ch;
    return -1;
}

lps_tick_t plat_dos_millis(void){
    unsigned long ticks;
#if defined(__WATCOMC__)
    ticks = *(unsigned long far *)MK_FP(0x40,0x6C);
#else
    ticks = *(unsigned long*)0x46C;
#endif
    return ticks * 55ul;
}
