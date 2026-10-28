#include "lps.h"
#include <stdio.h>
#include <string.h>
#ifdef __WATCOMC__
#include <direct.h>
#define MKDIR(p) mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

#ifdef LPS_DOS
#define LPS_SEP_STR "\\"
static char g_data_dir[128] = "C:\\LPS";
#else
#define LPS_SEP_STR "/"
static char g_data_dir[128] = "./LPS";
#endif

void lps_set_data_dir(const char *dir){
    size_t n; if(!dir || !*dir) return;
    n=strlen(dir); if(n>=sizeof(g_data_dir)) n=sizeof(g_data_dir)-1;
    memcpy(g_data_dir, dir, n); g_data_dir[n]=0;
    while(n>1 && (g_data_dir[n-1]=='\\' || g_data_dir[n-1]=='/')){ g_data_dir[--n]=0; }
}
const char *lps_data_dir(void){ return g_data_dir; }
void lps_path_join(char *out, size_t n, const char *a, const char *b){
    size_t la=strlen(a);
    if(la && (a[la-1]=='\\'||a[la-1]=='/'))
        sprintf(out, "%s%s", a, b);
    else
        sprintf(out, "%s%s%s", a, LPS_SEP_STR, b);
    (void)n;
}
int lps_ensure_dirs(void){
    char p[160];
    MKDIR(g_data_dir);
    lps_path_join(p,sizeof(p),g_data_dir,"EXPORT"); MKDIR(p);
    lps_path_join(p,sizeof(p),g_data_dir,"WEIGHTS"); MKDIR(p);
    return 1;
}
