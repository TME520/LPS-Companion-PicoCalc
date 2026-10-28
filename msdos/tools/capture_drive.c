#include "../src/lps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Platform *plat_host_create(int capture);
void plat_host_dump(Platform *p, FILE *out);
const char *plat_host_row(Platform *p, int row);

static void snap(Platform *hw, const char *name){
    char path[128]; FILE *f; int r;
    sprintf(path,"captures/%s.txt", name);
    f=fopen(path,"w"); if(!f) return;
    for(r=0;r<ROWS;r++) fprintf(f,"%s\n", plat_host_row(hw,r));
    fclose(f);
    printf("wrote %s\n", path);
}

int main(void){
    Platform *hw; App *app; static unsigned char store[8192];
    lps_set_data_dir("/tmp/lps-msdos-cap");
    lps_ensure_dirs();
    hw=plat_host_create(0);
    app=(App*)store; app_init(app,hw); app_start(app);
    snap(hw,"01-home-en");
    app_key(app,'1'); snap(hw,"02-activities");
    app_key(app,KEY_ESCAPE); app_key(app,'4'); snap(hw,"03-kanban");
    app_key(app,KEY_ESCAPE); app_key(app,'5'); snap(hw,"04-rosary-mode");
    app_key(app,KEY_ESCAPE); app_key(app,'9'); snap(hw,"05-timer");
    app_key(app,KEY_ESCAPE); app_key(app,'7'); app_key(app,'1'); app_key(app,'2'); app_key(app,KEY_ENTER);
    app_key(app,'3'); /* save FR */ snap(hw,"06-home-fr");
    return 0;
}
