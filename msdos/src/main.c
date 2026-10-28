/* LPSCOMP.EXE entry (DOS) / host interactive. */
#include "lps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LPS_DOS
Platform *plat_dos_create(void);
int plat_dos_get_key(void);
lps_tick_t plat_dos_millis(void);
void plat_dos_bios_puts(const char *s);
#else
Platform *plat_host_create(int capture);
void plat_host_dump(Platform *p, FILE *out);
#endif

static void usage(void){
    puts("LPS Companion MS-DOS v" LPS_VERSION_STR);
    puts("Usage: LPSCOMP [data_dir]");
    puts("  data_dir  LPS folder (default C:\\LPS or ./LPS on host)");
    puts("  Env LPSDIR overrides default.");
}

int main(int argc, char **argv){
    Platform *hw; App *app; static unsigned char store[8192];
    const char *dir=0; char *env;
#ifdef LPS_DOS
    /* First visible proof of life via BIOS teletype (before dirs / mode). */
    plat_dos_bios_puts("\r\nLPS COMPANION v" LPS_VERSION_STR "\r\n");
#endif
    env=getenv("LPSDIR");
    if(argc>=2){
        if(strcmp(argv[1],"-h")==0||strcmp(argv[1],"--help")==0){ usage(); return 0; }
        dir=argv[1];
    } else if(env && *env) dir=env;
#ifdef LPS_DOS
    else dir="C:\\LPS";
#else
    else dir="./LPS";
#endif
    lps_set_data_dir(dir);
    /* Do not hard-fail if mkdir fails — UI still starts (read-only / blocked). */
    lps_ensure_dirs();

    if(app_bytes()>sizeof(store)){
#ifdef LPS_DOS
        plat_dos_bios_puts("App too large\r\n");
#else
        puts("App too large");
#endif
        return 1;
    }
    app=(App*)store;

#ifdef LPS_DOS
    hw=plat_dos_create();
#else
    hw=plat_host_create(0);
#endif
    app_init(app,hw);
    app_start(app);

#ifdef LPS_DOS
    for(;;){
        int k;
        app_tick(app, plat_dos_millis());
        k=plat_dos_get_key();
        if(k>=0){
            if(app_screen(app)==SCR_HOME && (k=='q'||k=='Q')) break;
            app_key(app,k);
        }
    }
#else
    {
        char line[128];
        plat_host_dump(hw,stdout);
        while(fgets(line,sizeof(line),stdin)){
            size_t n=strlen(line); while(n&&(line[n-1]=='\n'||line[n-1]=='\r')) line[--n]=0;
            if(strcmp(line,"quit")==0) break;
            if(strcmp(line,"up")==0) app_key(app,KEY_UP);
            else if(strcmp(line,"down")==0) app_key(app,KEY_DOWN);
            else if(strcmp(line,"left")==0) app_key(app,KEY_LEFT);
            else if(strcmp(line,"right")==0) app_key(app,KEY_RIGHT);
            else if(strcmp(line,"enter")==0||n==0) app_key(app,KEY_ENTER);
            else if(strcmp(line,"esc")==0) app_key(app,KEY_ESCAPE);
            else if(strcmp(line,"back")==0) app_key(app,KEY_BACKSPACE);
            else if(n==1) app_key(app,(unsigned char)line[0]);
            plat_host_dump(hw,stdout);
        }
    }
#endif
    return 0;
}
