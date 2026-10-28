#include "lps.h"
#include <stdio.h>
#include <string.h>

int ics_escape_append(char *dst, size_t cap, size_t *used, const char *text){
    size_t u=*used;
    while(*text){
        char c=*text++;
        if(c=='\\'||c==';'||c==','){ if(u+2>=cap) return 0; dst[u++]='\\'; dst[u++]=c; }
        else if(c=='\n'){ if(u+2>=cap) return 0; dst[u++]='\\'; dst[u++]='n'; }
        else { if(u+1>=cap) return 0; dst[u++]=c; }
    }
    dst[u]=0; *used=u; return 1;
}

int build_ics(const Day *day, uint32_t totalXp, uint32_t dayXp, int language, char *out, size_t cap){
    char ymd[16], nxt[16], line[160], tmp[96];
    Date tomorrow; size_t u=0; int en=language!=LANG_FR; unsigned i; int first;
    if(!valid_date(day->date)) return 0;
    tomorrow=day->date; if(!next_date(&tomorrow)) return 0;
    sprintf(ymd,"%04u%02u%02u",(unsigned)day->date.year,(unsigned)day->date.month,(unsigned)day->date.day);
    sprintf(nxt,"%04u%02u%02u",(unsigned)tomorrow.year,(unsigned)tomorrow.month,(unsigned)tomorrow.day);
    if(u+200>=cap) return 0;
    u+=sprintf(out+u,"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//LPS Companion//EN\r\nCALSCALE:GREGORIAN\r\nBEGIN:VEVENT\r\n");
    u+=sprintf(out+u,"UID:lps-companion-%s@picocalc\r\nDTSTART;VALUE=DATE:%s\r\nDTEND;VALUE=DATE:%s\r\n",ymd,ymd,nxt);
    u+=sprintf(out+u,"SUMMARY:LPS Companion\r\nDESCRIPTION:");
    if(!ics_escape_append(out,cap,&u, en?"Activities: ":"Activites: ")) return 0;
    first=1;
    for(i=0;i<ACTIVITY_COUNT;i++) if(day->flags&(1u<<i)){
        if(!first){ if(u+2>=cap) return 0; out[u++]=','; out[u++]=' '; out[u]=0; }
        if(!ics_escape_append(out,cap,&u, en?ACTIVITIES_EN[i]:ACTIVITIES_FR[i])) return 0;
        first=0;
    }
    if(first){ if(u+4>=cap) return 0; memcpy(out+u,"none",4); u+=4; out[u]=0; }
    sprintf(line,"\\NXP: +%lu (total %lu)",(unsigned long)dayXp,(unsigned long)totalXp);
    if(u+strlen(line)>=cap) return 0; memcpy(out+u,line,strlen(line)); u+=strlen(line); out[u]=0;
    if(day->expected>=0){
        if(u+2>=cap) return 0; out[u++]='\\'; out[u++]='N'; out[u]=0;
        sprintf(tmp,"%s: %ld.%03ld kg", en?"Expected weight":"Poids attendu",(long)(day->expected/1000),(long)(day->expected%1000));
        if(!ics_escape_append(out,cap,&u,tmp)) return 0;
    }
    if(day->actual>=0){
        if(u+2>=cap) return 0; out[u++]='\\'; out[u++]='N'; out[u]=0;
        sprintf(tmp,"%s: %ld.%03ld kg", en?"Actual weight":"Poids effectif",(long)(day->actual/1000),(long)(day->actual%1000));
        if(!ics_escape_append(out,cap,&u,tmp)) return 0;
    }
    if(day->note[0]){
        if(u+2>=cap) return 0; out[u++]='\\'; out[u++]='N'; out[u]=0;
        if(!ics_escape_append(out,cap,&u,"Note: ")) return 0;
        if(!ics_escape_append(out,cap,&u,day->note)) return 0;
    }
    if(u+40>=cap) return 0;
    u+=sprintf(out+u,"\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    return 1;
}

int write_ics_file(const char *export_dir, const Day *day, uint32_t totalXp, uint32_t dayXp, int language){
    char path[200], buf[2048]; FILE *f;
    if(!build_ics(day,totalXp,dayXp,language,buf,sizeof(buf))) return 0;
    sprintf(path,"%s\\%04u-%02u-%02u_LPS-Companion.ics", export_dir,
        (unsigned)day->date.year,(unsigned)day->date.month,(unsigned)day->date.day);
    f=fopen(path,"wb"); if(!f) return 0;
    if(fwrite(buf,1,strlen(buf),f)!=strlen(buf)){ fclose(f); remove(path); return 0; }
    if(fclose(f)!=0){ remove(path); return 0; }
    return 1;
}

int write_weight_csv(const char *weights_dir, const Day *day){
    char path[200], date[16], expected[16], actual[16], line[64], buf[8192];
    FILE *f; size_t used=0; long sz;
    if(!valid_date(day->date)) return 0;
    sprintf(path,"%s\\%04u.csv", weights_dir, (unsigned)day->date.year);
    sprintf(date,"%04u-%02u-%02u",(unsigned)day->date.year,(unsigned)day->date.month,(unsigned)day->date.day);
    expected[0]=0; actual[0]=0;
    if(day->expected>=0) sprintf(expected,"%ld.%03ld",(long)(day->expected/1000),(long)(day->expected%1000));
    if(day->actual>=0) sprintf(actual,"%ld.%03ld",(long)(day->actual/1000),(long)(day->actual%1000));
    sprintf(line,"%s,%s,%s\r\n",date,expected,actual);
    f=fopen(path,"rb");
    if(f){
        if(fseek(f,0,SEEK_END)!=0){ fclose(f); return 0; }
        sz=ftell(f); if(sz<0 || (size_t)sz>=sizeof(buf)){ fclose(f); return 0; }
        rewind(f);
        used=(size_t)fread(buf,1,(size_t)sz,f);
        fclose(f);
        if(used!=(size_t)sz) return 0;
        { size_t i; for(i=0;i+11<=used;i++)
            if((i==0||buf[i-1]=='\n') && memcmp(buf+i,date,10)==0 && buf[i+10]==',') return 1; }
    }
    f=fopen(path,"ab"); if(!f) return 0;
    if(used==0){ if(fwrite("date,expected_kg,measured_kg\r\n",1,30,f)!=30){ fclose(f); return 0; } }
    if(fwrite(line,1,strlen(line),f)!=strlen(line)){ fclose(f); return 0; }
    return fclose(f)==0;
}
