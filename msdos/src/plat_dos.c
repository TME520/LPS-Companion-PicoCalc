/* DOS platform: BIOS text 80x25, int 16h keyboard, A/B saves under data dir. */
#include "lps.h"
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <conio.h>
#include <time.h>

typedef struct {
    Platform iface;
    char cells[ROWS][COLUMNS+1];
    char reverse[ROWS];
    int palette;
    uint32_t generation;
    int active; /* 0=A, 1=B */
    int write_fault;
    int mounted;
} DosPlat;

static DosPlat *self_of(Platform *p){ return (DosPlat*)p->user; }

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
static unsigned char attr_for(int palette, int reverse){
    /* CGA/EGA/VGA text attributes */
    unsigned char fg;
    switch(palette){
    case PAL_RED: fg=0x0C; break;
    case PAL_BLUE: fg=0x09; break;
    case PAL_ORANGE: fg=0x06; break;
    case PAL_PINK: fg=0x0D; break;
    default: fg=0x0A; break; /* green */
    }
    if(reverse) return (unsigned char)((fg<<4)|0x00);
    return (unsigned char)(0x00<<4|fg);
}
static void dos_present(Platform *p){
    DosPlat *d=self_of(p); int r,c;
    /* write directly to B800:0000 for speed on 8088 */
    {
#if defined(__WATCOMC__)
        unsigned char far *vid = (unsigned char far *)MK_FP(0xB800,0);
#else
        unsigned char *vid = (unsigned char*)0xB8000;
#endif
        unsigned char attr;
        for(r=0;r<ROWS;r++){
            attr=attr_for(d->palette, d->reverse[r]);
            for(c=0;c<80;c++){
                char ch = (c<COLUMNS)? d->cells[r][c] : ' ';
                vid[(r*80+c)*2]=(unsigned char)ch;
                vid[(r*80+c)*2+1]=attr;
            }
        }
        /* clear remaining rows 20-24 */
        for(r=ROWS;r<25;r++) for(c=0;c<80;c++){ vid[(r*80+c)*2]=' '; vid[(r*80+c)*2+1]=attr_for(d->palette,0); }
    }
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
    d.mounted=probe_storage();
    /* 80x25 text mode */
    { union REGS r; r.h.ah=0x00; r.h.al=0x03; int86(0x10,&r,&r); }
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
    /* BIOS timer ticks ~18.2 Hz at 0040:006C */
    unsigned long ticks;
#if defined(__WATCOMC__)
    ticks = *(unsigned long far *)MK_FP(0x40,0x6C);
#else
    ticks = *(unsigned long*)0x46C;
#endif
    return ticks * 55ul; /* approx ms */
}
