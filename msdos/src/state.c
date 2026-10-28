/* state.c - dates, XP, encode/decode (Companion v1.11 wire formats 1-6). */
#include "lps.h"
#include <stdio.h>
#include <string.h>

const char *ACTIVITIES_FR[ACTIVITY_COUNT] = {
    "Marche","Regime","Bible","Messe","Travail","Projet","Sieste","Gurumed",
    "Maladie","Conge","Weekend","Sortie","Jeu","Docteur","Lecture","Shopping"
};
const char *ACTIVITIES_EN[ACTIVITY_COUNT] = {
    "Walk","Diet","Bible","Mass","Work","Project","Nap","Gurumed",
    "Illness","Day off","Weekend","Outing","Gaming","Doctor","Reading","Shopping"
};
/* French accented labels use ASCII fallbacks in DOS code page; UI maps display. */
const char *GIFTS_FR[6] = {"Carnet de poche","Tasse de the","Boussole","Radio de poche","Mini-ordinateur","Lanterne"};
const char *GIFTS_EN[6] = {"Pocket notebook","Cup of tea","Compass","Pocket radio","Mini computer","Lantern"};
const uint32_t GIFT_XP[6] = {30,70,120,180,250,330};
const Date FIRST_DATE = {2000,1,1};
const Date LAST_DATE = {2099,12,31};
const Date DEFAULT_DATE = {2026,9,17};

int leap(uint16_t year){ return year%4==0 && (year%100!=0 || year%400==0); }
unsigned days_in_month(uint16_t year, uint8_t month){
    static const uint8_t normal[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1||month>12) return 0;
    if(month==2 && leap(year)) return 29;
    return normal[month-1];
}
int valid_date(Date d){
    return d.year>=FIRST_DATE.year && d.year<=LAST_DATE.year && d.month>=1 && d.month<=12
        && d.day>=1 && d.day<=days_in_month(d.year,d.month);
}
int next_date(Date *d){
    if(!valid_date(*d) || (d->year==LAST_DATE.year && d->month==12 && d->day==31)) return 0;
    if(++d->day <= days_in_month(d->year,d->month)) return 1;
    d->day=1; if(++d->month<=12) return 1; d->month=1; ++d->year; return 1;
}
void date_text(Date d, char *out, size_t n){ sprintf(out, "%04u-%02u-%02u", (unsigned)d.year, (unsigned)d.month, (unsigned)d.day); (void)n; }
int parse_date(const char *text, Date *d){
    unsigned i; Date t;
    if(!text || strlen(text)!=10 || text[4]!='-' || text[7]!='-') return 0;
    for(i=0;i<10;i++) if(i!=4 && i!=7 && (text[i]<'0'||text[i]>'9')) return 0;
    t.year=(uint16_t)((text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0');
    t.month=(uint8_t)((text[5]-'0')*10+text[6]-'0');
    t.day=(uint8_t)((text[8]-'0')*10+text[9]-'0');
    if(!valid_date(t)) return 0; *d=t; return 1;
}
unsigned count_flags(uint16_t flags){ unsigned n=0; for(;flags;flags>>=1) n+=flags&1; return n; }
uint32_t points(const Day *d){ unsigned n=count_flags(d->flags); return n*10u + (n>=3?10u:0u); }
int parse_weight(const char *s, int32_t *grams){
    uint32_t whole=0, fraction=0; unsigned digits=0, decimals=0;
    if(!s || !*s){ *grams=-1; return 1; }
    while(*s>='0'&&*s<='9'){ if(++digits>3) return 0; whole=whole*10u+(unsigned)(*s++-'0'); }
    if(!digits) return 0;
    if(*s=='.'||*s==','){ ++s; while(*s>='0'&&*s<='9'){ if(++decimals>3) return 0; fraction=fraction*10u+(unsigned)(*s++-'0'); }
        if(!decimals) return 0; }
    if(*s) return 0;
    for(;decimals<3;++decimals) fraction*=10u;
    *grams=(int32_t)(whole*1000u+fraction); return *grams>0;
}
void weight_text(int32_t grams, char *out, size_t n){
    if(grams<0){ sprintf(out,"--"); (void)n; }
    else sprintf(out,"%ld.%03ld",(long)(grams/1000),(long)(grams%1000));
}
uint32_t crc32(const uint8_t *p, size_t n){
    uint32_t c=0xfffffffful;
    while(n--){ int i; c^=*p++; for(i=0;i<8;i++) c=(c>>1)^(0xedb88320ul & (uint32_t)-(int32_t)(c&1)); }
    return ~c;
}
uint32_t day_index(Date d){
    uint32_t n=0; unsigned y,m;
    for(y=2000;y<d.year;++y) n+=leap((uint16_t)y)?366u:365u;
    for(m=1;m<d.month;++m) n+=days_in_month(d.year,(uint8_t)m);
    return n+d.day;
}
unsigned weekday(Date d){
    static const unsigned offsets[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    unsigned y=d.year; if(d.month<3) --y;
    return (y+y/4-y/100+y/400+offsets[d.month-1]+d.day)%7;
}
void state_init(State *s){
    unsigned i;
    memset(s,0,sizeof(*s));
    s->language=LANG_EN; s->palette=PAL_GREEN;
    s->today.number=1; s->today.date=DEFAULT_DATE;
    s->today.expected=-1; s->today.actual=-1;
    /* Companion default-constructs every history Day with number=1; decode requires number>0. */
    for(i=0;i<HISTORY_LENGTH;i++){
        s->history[i].number=1;
        s->history[i].expected=-1;
        s->history[i].actual=-1;
    }
}

static void put_n(Blob *b, uint32_t v, unsigned n){
    while(n--){ b->bytes[b->size++]=(uint8_t)v; v>>=8; }
}
static void put_day(Blob *b, const Day *d){
    unsigned i;
    put_n(b,d->date.year,2); put_n(b,d->date.month,1); put_n(b,d->date.day,1);
    put_n(b,d->number,4); put_n(b,d->flags,2);
    put_n(b, d->expected<0 ? 0xfffffffful : (uint32_t)d->expected, 4);
    put_n(b, d->actual<0 ? 0xfffffffful : (uint32_t)d->actual, 4);
    for(i=0;i<=NOTE_LENGTH;i++) put_n(b,(uint8_t)d->note[i],1);
}
void encode_state(const State *s, Blob *b){
    unsigned i,j; uint32_t crc;
    b->size=0;
    put_n(b,WIRE_MAGIC,4); put_n(b,6,2); put_n(b,s->xp,4); put_n(b,s->next,1); put_n(b,s->count,1);
    put_n(b,s->language,1); put_n(b,s->palette,1);
    put_day(b,&s->today);
    for(i=0;i<HISTORY_LENGTH;i++) put_day(b,&s->history[i]);
    put_n(b,s->taskCount,1);
    for(i=0;i<TASK_CAPACITY;i++){ put_n(b,s->tasks[i].status,1); for(j=0;j<=TASK_NAME_LENGTH;j++) put_n(b,(uint8_t)s->tasks[i].name[j],1); }
    put_n(b,s->reminderCount,1);
    for(i=0;i<REMINDER_CAPACITY;i++){
        put_n(b,s->reminders[i].from.year,2); put_n(b,s->reminders[i].from.month,1); put_n(b,s->reminders[i].from.day,1);
        put_n(b,s->reminders[i].duration,2);
        for(j=0;j<=REMINDER_TEXT_LENGTH;j++) put_n(b,(uint8_t)s->reminders[i].text[j],1);
    }
    crc=crc32(b->bytes,b->size); put_n(b,crc,4);
}

static uint32_t get_n(const uint8_t *bytes, size_t *pos, unsigned n){
    uint32_t v=0; unsigned i; for(i=0;i<n;i++) v|=(uint32_t)bytes[(*pos)++]<<(i*8); return v;
}
int decode_state(const uint8_t *bytes, size_t size, State *out){
    size_t pos; uint32_t version; State s; int valid; unsigned i,j;
    if(size!=WIRE_SIZE && size!=WIRE_V5 && size!=WIRE_V4 && size!=WIRE_V3 && size!=WIRE_V2 && size!=WIRE_V1) return 0;
    pos=size-4;
    if(get_n(bytes,&pos,4)!=crc32(bytes,size-4)) return 0;
    pos=0; if(get_n(bytes,&pos,4)!=WIRE_MAGIC) return 0;
    version=get_n(bytes,&pos,2);
    if(!((version==1&&size==WIRE_V1)||(version==2&&size==WIRE_V2)||(version==3&&size==WIRE_V3)
        ||(version==4&&size==WIRE_V4)||(version==5&&size==WIRE_V5)||(version==6&&size==WIRE_SIZE))) return 0;
    memset(&s,0,sizeof(s));
    s.today.expected=s.today.actual=-1;
    for(i=0;i<HISTORY_LENGTH;i++){ s.history[i].expected=-1; s.history[i].actual=-1; }
    s.xp=get_n(bytes,&pos,4); s.next=(uint8_t)get_n(bytes,&pos,1); s.count=(uint8_t)get_n(bytes,&pos,1);
    if(version>=2){ uint32_t language=get_n(bytes,&pos,1); if(language>1) return 0; s.language=(uint8_t)language; }
    if(version>=4){ uint32_t palette=get_n(bytes,&pos,1); if(palette>4) return 0; s.palette=(uint8_t)palette; }
    else s.palette=PAL_GREEN;
    valid = s.next<HISTORY_LENGTH && s.count<=HISTORY_LENGTH;
    {
        /* day decoder */
        int di;
        for(di=-1; di<(int)HISTORY_LENGTH; di++){
            Day *d = (di<0)? &s.today : &s.history[di];
            uint32_t wv;
            d->date.year=0; d->date.month=0; d->date.day=0;
            if(version>=3){ d->date.year=(uint16_t)get_n(bytes,&pos,2); d->date.month=(uint8_t)get_n(bytes,&pos,1); d->date.day=(uint8_t)get_n(bytes,&pos,1); }
            d->number=get_n(bytes,&pos,4); d->flags=(uint16_t)get_n(bytes,&pos,2);
            wv=get_n(bytes,&pos,4);
            if(wv==0xfffffffful) d->expected=-1; else if(wv==0||wv>999999ul){ valid=0; d->expected=-1; } else d->expected=(int32_t)wv;
            wv=get_n(bytes,&pos,4);
            if(wv==0xfffffffful) d->actual=-1; else if(wv==0||wv>999999ul){ valid=0; d->actual=-1; } else d->actual=(int32_t)wv;
            for(j=0;j<=NOTE_LENGTH;j++) d->note[j]=(char)get_n(bytes,&pos,1);
            valid = valid && d->number>0 && (d->flags & ~ACTIVITY_MASK)==0 && d->note[NOTE_LENGTH]==0
                && (d->date.year==0 || valid_date(d->date));
            { int ended=0; for(j=0;j<=NOTE_LENGTH;j++){ char c=d->note[j]; if(!c) ended=1; else if(!ended && (c<32||c>126)) valid=0; } }
        }
    }
    if(version>=5){
        s.taskCount=(uint8_t)get_n(bytes,&pos,1); valid=valid && s.taskCount<=TASK_CAPACITY;
        for(i=0;i<TASK_CAPACITY;i++){
            uint32_t status=get_n(bytes,&pos,1); if(status>2) valid=0; s.tasks[i].status=(uint8_t)status;
            for(j=0;j<=TASK_NAME_LENGTH;j++) s.tasks[i].name[j]=(char)get_n(bytes,&pos,1);
            valid=valid && s.tasks[i].name[TASK_NAME_LENGTH]==0;
            { int ended=0; for(j=0;j<=TASK_NAME_LENGTH;j++){ char c=s.tasks[i].name[j]; if(!c) ended=1; else if(!ended && (c<32||c>126)) valid=0; } }
            if(i<s.taskCount && s.tasks[i].name[0]==0) valid=0;
        }
    }
    if(version>=6){
        s.reminderCount=(uint8_t)get_n(bytes,&pos,1); valid=valid && s.reminderCount<=REMINDER_CAPACITY;
        for(i=0;i<REMINDER_CAPACITY;i++){
            Reminder *r=&s.reminders[i];
            r->from.year=(uint16_t)get_n(bytes,&pos,2); r->from.month=(uint8_t)get_n(bytes,&pos,1); r->from.day=(uint8_t)get_n(bytes,&pos,1);
            r->duration=(uint16_t)get_n(bytes,&pos,2);
            for(j=0;j<=REMINDER_TEXT_LENGTH;j++) r->text[j]=(char)get_n(bytes,&pos,1);
            valid=valid && r->text[REMINDER_TEXT_LENGTH]==0;
            { int ended=0; for(j=0;j<=REMINDER_TEXT_LENGTH;j++){ char c=r->text[j]; if(!c) ended=1; else if(!ended && (c<32||c>126)) valid=0; } }
            if(i<s.reminderCount) valid=valid && valid_date(r->from) && r->duration>=1 && r->duration<=365 && r->text[0]!=0;
        }
    }
    if(!valid) return 0;
    *out=s; return 1;
}
