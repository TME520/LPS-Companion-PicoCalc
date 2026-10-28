/* app.c - LPS Companion UI state machine (v1.11 feature parity). C89. */
#include "lps.h"
#include "rosary.h"
#include <stdio.h>
#include <string.h>

/* State (~4.6K) and Blob (~6K) must NOT live on the tiny DOS stack. */
static State g_scratch;
static Blob g_blob;

struct App {
    Platform *hw;
    State state;
    int screen;
    unsigned selection, page, field, daysBack;
    int blocked;
    int pendingLanguage;
    int pendingPalette;
    char dateDraft[11];
    char draft[NOTE_LENGTH+1];
    char taskDraft[TASK_NAME_LENGTH+1];
    char weights[2][8];
    char message[100];
    uint32_t earned;
    uint8_t unlocked;
    int missingStorage;
    int kanbanColumn;
    unsigned reviewOffset;
    unsigned editingReminder;
    unsigned reminderField;
    char reminderDate[11];
    char reminderDuration[4];
    char reminderText[REMINDER_TEXT_LENGTH+1];
    unsigned editedTask;
    int rosaryMode;
    int rosarySet;
    unsigned rosaryDecade, rosaryStep, rosaryPage;
    lps_tick_t timerNow, timerLast, timerRemaining;
    int timerPaused;
    int dirty; /* RAM changed; disk SAVE pending */
};

static const char *tr(const App *a, const char *en, const char *fr){
    return a->state.language==LANG_FR?fr:en;
}
static void say(App *a, const char *text){ sprintf(a->message, "%s", text); }
static void read_only(App *a){ say(a, tr(a,"Archived day: read-only","Jour archive : lecture seule")); }
static unsigned timer_seconds(const App *a){ return (unsigned)((a->timerRemaining+999)/1000); }
static int displayed_palette(const App *a){ return a->screen==SCR_COLORS?a->pendingPalette:a->state.palette; }

static void line(App *a, int row, const char *text, int reverse){
    a->hw->text(a->hw, row, 0, text, reverse);
}
static void wrap_text(App *a, int row, const char *text, unsigned limit){
    unsigned i; char b[41]; size_t n;
    for(i=0;*text && i<limit;i++){
        n=strlen(text); if(n>40) n=40;
        memcpy(b,text,n); b[n]=0;
        line(a,row+(int)i,b,0);
        text+=n;
    }
}
static unsigned display_cells(const char *text){ unsigned n=0; while(*text++) n++; return n; }
static void cell_slice(const char *text, unsigned first, unsigned count, char *out, size_t capacity){
    size_t n=0;
    while(*text && first){ text++; first--; }
    while(*text && count && n+1<capacity){ out[n++]=*text++; count--; }
    out[n]=0;
}

/* Apply edit in RAM only — disk writes go through flush_save (slow on 8088 HDD). */
static int commit(App *a, const State *candidate){
    if(a->daysBack){ read_only(a); return 0; }
    if(a->blocked){ say(a, tr(a,"Unreadable save: writes blocked","Sauvegarde illisible : ecriture bloquee")); return 0; }
    a->state=*candidate;
    a->dirty=1;
    return 1;
}

/* Persist dirty state: one A/B slot, with visible status. */
static int flush_save(App *a){
    if(!a->dirty) return 1;
    if(a->blocked){ say(a, tr(a,"Unreadable save: writes blocked","Sauvegarde illisible : ecriture bloquee")); return 0; }
    if(a->missingStorage){ say(a, tr(a,"No storage","Pas de stockage")); return 0; }
    say(a, tr(a,"Saving...","Sauvegarde..."));
    app_render(a);
    encode_state(&a->state, &g_blob);
    if(!a->hw->save(a->hw, g_blob.bytes, g_blob.size)){
        say(a, tr(a,"Save failed: restart device","Echec sauvegarde : redemarrer"));
        app_render(a);
        return 0;
    }
    a->dirty=0;
    say(a, tr(a,"Saved","Enregistre"));
    return 1;
}

static unsigned tasks_in(const App *a, int status){
    unsigned n=0,i; for(i=0;i<a->state.taskCount;i++) if(a->state.tasks[i].status==status) n++; return n;
}
static unsigned task_at(const App *a, int status, unsigned position){
    unsigned i; for(i=0;i<a->state.taskCount;i++) if(a->state.tasks[i].status==status && position--==0) return i;
    return TASK_CAPACITY;
}
static unsigned task_position(const App *a, int status, unsigned index){
    unsigned position=0,i; for(i=0;i<index && i<a->state.taskCount;i++) if(a->state.tasks[i].status==status) position++;
    return position;
}
static const char *task_status_name(const App *a, int status){
    static const char *en[]={"TODO","DOING","DONE"};
    static const char *fr[]={"A FAIRE","EN COURS","FINI"};
    return a->state.language==LANG_FR?fr[status]:en[status];
}
static unsigned review_rows(const App *a){
    return 3+count_flags(a->state.today.flags)+tasks_in(a,TASK_TODO)+tasks_in(a,TASK_DOING)
        +(count_flags(a->state.today.flags)==0)+(tasks_in(a,TASK_TODO)==0)+(tasks_in(a,TASK_DOING)==0);
}
static void review_line(const App *a, unsigned index, char *out, size_t size){
    unsigned activities=count_flags(a->state.today.flags); unsigned i; int status;
    if(index==0){ sprintf(out, tr(a,"ACTIVITIES (%u)","ACTIVITES (%u)"), activities); return; }
    --index;
    for(i=0;i<ACTIVITY_COUNT;i++) if(a->state.today.flags&(1u<<i)){
        if(index--==0){ sprintf(out,"  %s", tr(a,ACTIVITIES_EN[i],ACTIVITIES_FR[i])); return; }
    }
    if(!activities){ if(index--==0){ sprintf(out,"  %s", tr(a,"None selected","Aucune selectionnee")); return; } }
    for(status=TASK_TODO; status<=TASK_DOING; status++){
        unsigned n=tasks_in(a,status);
        if(index--==0){ sprintf(out,"%s (%u)", task_status_name(a,status), n); return; }
        for(i=0;i<a->state.taskCount;i++) if(a->state.tasks[i].status==status){
            if(index--==0){ sprintf(out,"  %s", a->state.tasks[i].name); return; }
        }
        if(!n && index--==0){ sprintf(out,"  %s", tr(a,"None","Aucune")); return; }
    }
    out[0]=0; (void)size;
}
static int rem_expired(const App *a, const Reminder *r){
    return valid_date(a->state.today.date) && day_index(a->state.today.date)>=day_index(r->from)+r->duration;
}
static int rem_active(const App *a, const Reminder *r){
    return valid_date(a->state.today.date) && day_index(a->state.today.date)>=day_index(r->from) && !rem_expired(a,r);
}
static unsigned active_reminders(const App *a){
    unsigned n=0,i; for(i=0;i<a->state.reminderCount;i++) if(rem_active(a,&a->state.reminders[i])) n++; return n;
}
static void prune_reminders(App *a){
    unsigned kept=0,i;
    g_scratch=a->state;
    for(i=0;i<a->state.reminderCount;i++) if(!rem_expired(a,&a->state.reminders[i])) g_scratch.reminders[kept++]=a->state.reminders[i];
    if(kept!=a->state.reminderCount){
        for(i=kept;i<REMINDER_CAPACITY;i++){ memset(&g_scratch.reminders[i],0,sizeof(Reminder)); }
        g_scratch.reminderCount=(uint8_t)kept; commit(a,&g_scratch);
    }
}
static void go(App *a, int s){
    const Day *vd;
    a->screen=s; a->selection=0; a->message[0]=0;
    if(s==SCR_DAILY_REVIEW) a->reviewOffset=0;
    if(s==SCR_LANGUAGE) a->selection=(unsigned)a->pendingLanguage;
    if(s==SCR_COLORS) a->selection=(unsigned)a->pendingPalette;
    if(s==SCR_NOTE){ vd=app_viewed_day(a); memcpy(a->draft, vd->note, NOTE_LENGTH+1); }
    if(s==SCR_WEIGHT){
        int i; a->field=0; vd=app_viewed_day(a);
        for(i=0;i<2;i++){ int32_t v=i?vd->actual:vd->expected; memset(a->weights[i],0,8); if(v>=0) weight_text(v,a->weights[i],8); }
    }
    if(s==SCR_DATE){
        Date d; memset(a->dateDraft,0,sizeof(a->dateDraft));
        d=valid_date(a->state.today.date)?a->state.today.date:DEFAULT_DATE;
        date_text(d,a->dateDraft,sizeof(a->dateDraft));
    }
}
/* AZERTY (FR) unshifted top-row → digit, so Date/Weight work without Shift. */
static int azerty_digit(int key){
    switch(key){
    case '&': return '1';
    case 130: return '2'; /* é */
    case '"': return '3';
    case '\'': return '4';
    case '(': return '5';
    /* '-' is ASCII hyphen AND AZERTY unshifted 6 — keep as '-' for ISO dates */
    case 138: return '7'; /* è */
    case '_': return '8';
    case 135: return '9'; /* ç */
    case 133: return '0'; /* à */
    default: return key;
    }
}
static void edit_buf(char *value, size_t cap, int key){
    size_t len=strlen(value);
    if(key==KEY_BACKSPACE||key==127){ if(len) value[len-1]=0; }
    else if(key>=32 && key<=126 && len+1<cap){ value[len]=(char)key; value[len+1]=0; }
}
static void begin_reminder_edit(App *a, unsigned index){
    Reminder r; memset(&r,0,sizeof(r));
    a->editingReminder=index; a->reminderField=0;
    memset(a->reminderDate,0,sizeof(a->reminderDate));
    memset(a->reminderDuration,0,sizeof(a->reminderDuration));
    memset(a->reminderText,0,sizeof(a->reminderText));
    if(index<a->state.reminderCount) r=a->state.reminders[index];
    else { r.from=a->state.today.date; r.duration=1; }
    if(valid_date(r.from)) date_text(r.from,a->reminderDate,sizeof(a->reminderDate));
    sprintf(a->reminderDuration,"%u",(unsigned)r.duration);
    memcpy(a->reminderText,r.text,REMINDER_TEXT_LENGTH+1);
    go(a,SCR_REMINDER_EDIT);
}
static void begin_task_edit(App *a, unsigned index){
    a->editedTask=index; memset(a->taskDraft,0,sizeof(a->taskDraft));
    if(index<TASK_CAPACITY) memcpy(a->taskDraft,a->state.tasks[index].name,TASK_NAME_LENGTH+1);
    go(a,SCR_TASK_EDIT);
}
static void move_task_to(App *a, int target){
    unsigned n=tasks_in(a,a->kanbanColumn), index=n?task_at(a,a->kanbanColumn,a->selection):TASK_CAPACITY;
    Task moved; unsigned i;
    if(index>=a->state.taskCount) return;
    if(a->state.tasks[index].status==target){ say(a,tr(a,"Task is already in this column","Tache deja dans cette colonne")); return; }
    g_scratch=a->state; moved=g_scratch.tasks[index]; moved.status=(uint8_t)target;
    for(i=index;i+1<g_scratch.taskCount;i++) g_scratch.tasks[i]=g_scratch.tasks[i+1];
    g_scratch.tasks[g_scratch.taskCount-1]=moved;
    if(commit(a,&g_scratch)){ a->kanbanColumn=target; a->selection=tasks_in(a,target)-1; say(a,tr(a,"Task moved","Tache deplacee")); }
}
static void reorder_task(App *a, int downward){
    unsigned n=tasks_in(a,a->kanbanColumn); unsigned indices[TASK_CAPACITY]; unsigned i; unsigned destination; Task moved;
    if(n<2) return;
    for(i=0;i<n;i++) indices[i]=task_at(a,a->kanbanColumn,i);
    g_scratch=a->state; destination=a->selection;
    if(!downward && a->selection==0){
        moved=g_scratch.tasks[indices[0]]; for(i=0;i+1<n;i++) g_scratch.tasks[indices[i]]=g_scratch.tasks[indices[i+1]];
        g_scratch.tasks[indices[n-1]]=moved; destination=n-1;
    } else if(downward && a->selection==n-1){
        moved=g_scratch.tasks[indices[n-1]]; for(i=n-1;i>0;i--) g_scratch.tasks[indices[i]]=g_scratch.tasks[indices[i-1]];
        g_scratch.tasks[indices[0]]=moved; destination=0;
    } else {
        destination=downward?a->selection+1:a->selection-1;
        moved=g_scratch.tasks[indices[a->selection]];
        g_scratch.tasks[indices[a->selection]]=g_scratch.tasks[indices[destination]];
        g_scratch.tasks[indices[destination]]=moved;
    }
    if(commit(a,&g_scratch)) a->selection=destination;
}
static int suggested_mysteries(const App *a){
    if(!valid_date(a->state.today.date)) return MYS_JOYFUL;
    switch(weekday(a->state.today.date)){
    case 1: case 6: return MYS_JOYFUL;
    case 2: case 5: return MYS_SORROWFUL;
    case 4: return MYS_LUMINOUS;
    default: return MYS_GLORIOUS;
    }
}

size_t app_bytes(void){ return sizeof(struct App); }
void app_init(App *app, Platform *hw){
    memset(app,0,sizeof(*app));
    app->hw=hw;
    state_init(&app->state);
    app->screen=SCR_HOME;
    app->pendingLanguage=LANG_EN;
    app->pendingPalette=PAL_GREEN;
    app->editingReminder=REMINDER_CAPACITY;
    app->editedTask=TASK_CAPACITY;
    app->kanbanColumn=TASK_TODO;
    app->dirty=0;
}
const State *app_state(const App *app){ return &app->state; }
enum Screen app_screen(const App *app){ return (enum Screen)app->screen; }
const Day *app_viewed_day(const App *app){
    if(app->daysBack)
        return &app->state.history[(app->state.next+HISTORY_LENGTH-app->daysBack)%HISTORY_LENGTH];
    return &app->state.today;
}

void app_start(App *app){
    enum LoadResult r; size_t n=0;
    r=app->hw->load(app->hw,g_blob.bytes,sizeof(g_blob.bytes),&n);
    if(r==LOAD_NO_STORAGE){ app->blocked=1; app->missingStorage=1; say(app,"[!] Missing SD card!"); }
    else if(r==LOAD_ERROR || (r==LOAD_OK && !decode_state(g_blob.bytes,n,&app->state))){
        app->blocked=1; say(app, tr(app,"Read error: save protected","Erreur lecture : sauvegarde protegee"));
    }
    if(!app->blocked){ prune_reminders(app); if(app->dirty) flush_save(app); if(active_reminders(app)) app->screen=SCR_REMINDER_POPUP; }
    app_render(app);
}

void app_tick(App *app, lps_tick_t ms){
    unsigned before;
    app->timerNow=ms;
    if(app->screen!=SCR_TIMER_RUNNING) return;
    before=timer_seconds(app);
    if(!app->timerPaused){
        lps_tick_t elapsed=app->timerNow-app->timerLast;
        app->timerRemaining = elapsed>=app->timerRemaining ? 0 : app->timerRemaining-elapsed;
    }
    app->timerLast=app->timerNow;
    if(!app->timerRemaining){ go(app,SCR_TIMER_COMPLETE); app_render(app); }
    else if(before!=timer_seconds(app)) app_render(app);
}

/* --- key handler continues in app_key.c style: keep in this file --- */

void app_key(App *a, int k){
    if(k==KEY_ESCAPE){
        int target=SCR_HOME;
        int from=a->screen;
        if(a->screen==SCR_LANGUAGE||a->screen==SCR_COLORS) target=SCR_CONFIG;
        else if(a->screen==SCR_REMINDER_EDIT) target=SCR_REMINDERS;
        else if(a->screen==SCR_TASK_EDIT) target=SCR_KANBAN;
        else if(a->screen==SCR_ROSARY_SET) target=SCR_ROSARY_MODE;
        else if(a->screen==SCR_ROSARY_DECADE) target=SCR_ROSARY_SET;
        /* Persist when leaving an editor back to Home (date/activities/note/...). */
        if(target==SCR_HOME && a->dirty
           && (from==SCR_DATE||from==SCR_ACTIVITIES||from==SCR_NOTE||from==SCR_WEIGHT
               ||from==SCR_KANBAN||from==SCR_REMINDERS||from==SCR_CONFIG
               ||from==SCR_COLORS||from==SCR_LANGUAGE||from==SCR_DAILY_REVIEW
               ||from==SCR_ROSARY_MODE||from==SCR_ROSARY_COMPLETE||from==SCR_FINISH
               ||from==SCR_REWARD||from==SCR_TIMER_SELECT)){
            flush_save(a);
        }
        go(a,target); app_render(a); return;
    }
    a->message[0]=0;
    if(a->screen==SCR_REMINDER_POPUP){ go(a,SCR_HOME);
    }else if(a->screen==SCR_HOME){
        unsigned menuCount=a->daysBack?3u:11u;
        if(k==KEY_LEFT){
            if(a->daysBack<a->state.count){ a->daysBack++; a->selection=0; a->page=0; }
            else say(a, a->state.count?tr(a,"Oldest saved day","Plus ancien jour conserve"):tr(a,"No archived days","Aucun jour archive"));
        }
        if(k==KEY_RIGHT){ a->daysBack=0; a->selection=0; a->page=0; }
        if(k==KEY_UP) a->selection=(a->selection+menuCount-1)%menuCount;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%menuCount;
        if(!a->daysBack && k=='0'){ a->selection=0; k=KEY_ENTER; }
        else if(a->daysBack && k>='1' && k<='3'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        else if(!a->daysBack && k>='1' && k<='9'){ a->selection=(unsigned)(k-'0'); k=KEY_ENTER; }
        if((k=='0'||(k>='4'&&k<='9')) && a->daysBack) read_only(a);
        if(k==KEY_ENTER){
            static const int currentScreens[]={SCR_DATE,SCR_ACTIVITIES,SCR_NOTE,SCR_WEIGHT,SCR_KANBAN,SCR_ROSARY_MODE,SCR_DAILY_REVIEW,SCR_CONFIG,SCR_REMINDERS,SCR_TIMER_SELECT,SCR_FINISH};
            static const int archiveScreens[]={SCR_ACTIVITIES,SCR_NOTE,SCR_WEIGHT};
            int target=a->daysBack?archiveScreens[a->selection]:currentScreens[a->selection];
            if(target==SCR_KANBAN) a->kanbanColumn=TASK_TODO;
            if(target==SCR_REMINDERS) prune_reminders(a);
            if(target==SCR_CONFIG){ a->pendingLanguage=a->state.language; a->pendingPalette=a->state.palette; }
            go(a,target);
        }
    }else if(a->screen==SCR_TIMER_SELECT){
        if(k==KEY_UP) a->selection=(a->selection+2)%3;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%3;
        if(k==KEY_ENTER){ static const unsigned minutes[]={5,15,30}; a->timerRemaining=(lps_tick_t)minutes[a->selection]*60000ul; a->timerLast=a->timerNow; a->timerPaused=0; go(a,SCR_TIMER_RUNNING); }
    }else if(a->screen==SCR_TIMER_RUNNING){
        if(k==KEY_ENTER){ a->timerPaused=!a->timerPaused; a->timerLast=a->timerNow; }
    }else if(a->screen==SCR_TIMER_COMPLETE){
        if(k==KEY_ENTER) go(a,SCR_TIMER_SELECT);
    }else if(a->screen==SCR_REMINDERS){
        if(k==KEY_UP && a->state.reminderCount) a->selection=(a->selection+a->state.reminderCount-1)%a->state.reminderCount;
        else if(k==KEY_DOWN && a->state.reminderCount) a->selection=(a->selection+1)%a->state.reminderCount;
        else if(k=='n'||k=='N'){
            if(a->state.reminderCount>=REMINDER_CAPACITY) say(a,tr(a,"Limit: 3 reminders","Limite : 3 rappels"));
            else begin_reminder_edit(a,REMINDER_CAPACITY);
        }else if(k==KEY_ENTER && a->state.reminderCount) begin_reminder_edit(a,a->selection);
        else if(k==KEY_DELETE && a->state.reminderCount){
            unsigned i;
            g_scratch=a->state;
            for(i=a->selection;i+1<g_scratch.reminderCount;i++) g_scratch.reminders[i]=g_scratch.reminders[i+1];
            memset(&g_scratch.reminders[--g_scratch.reminderCount],0,sizeof(Reminder));
            if(commit(a,&g_scratch) && a->selection>=a->state.reminderCount && a->selection) a->selection--;
        }
    }else if(a->screen==SCR_REMINDER_EDIT){
        if(k==KEY_UP && a->reminderField) a->reminderField--;
        else if(k==KEY_DOWN && a->reminderField<2) a->reminderField++;
        else if(k==KEY_ENTER){
            Date from; unsigned duration=0; unsigned i;
            for(i=0;a->reminderDuration[i];i++){ char c=a->reminderDuration[i]; if(c<'0'||c>'9'){ duration=366; break; } duration=duration*10u+(unsigned)(c-'0'); }
            if(!parse_date(a->reminderDate,&from)||duration<1||duration>365||!a->reminderText[0]) say(a,tr(a,"Check date, 1-365 days and text","Verifiez date, 1-365 jours et texte"));
            else if(day_index(from)+duration<=day_index(a->state.today.date)) say(a,tr(a,"Reminder already expired","Rappel deja expire"));
            else {
                unsigned index=a->editingReminder<REMINDER_CAPACITY?a->editingReminder:a->state.reminderCount;
                g_scratch=a->state;
                g_scratch.reminders[index].from=from; g_scratch.reminders[index].duration=(uint16_t)duration;
                memcpy(g_scratch.reminders[index].text,a->reminderText,REMINDER_TEXT_LENGTH+1);
                if(index==g_scratch.reminderCount) g_scratch.reminderCount++;
                if(commit(a,&g_scratch)) go(a,SCR_REMINDERS);
            }
        }else if(k==KEY_BACKSPACE||k==127||(k>=32&&k<=126)){
            if(a->reminderField==0){ if((k>='0'&&k<='9')||k=='-'||k==KEY_BACKSPACE||k==127) edit_buf(a->reminderDate,sizeof(a->reminderDate),k); }
            else if(a->reminderField==1){ if((k>='0'&&k<='9')||k==KEY_BACKSPACE||k==127) edit_buf(a->reminderDuration,sizeof(a->reminderDuration),k); }
            else edit_buf(a->reminderText,sizeof(a->reminderText),k);
        }
    }else if(a->screen==SCR_DATE){
        if(k==KEY_ENTER){ Date d;
            g_scratch=a->state;
            if(!parse_date(a->dateDraft,&d)) say(a,tr(a,"Invalid date: YYYY-MM-DD, 2000-2099","Date invalide : AAAA-MM-JJ, 2000-2099"));
            else { g_scratch.today.date=d; if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); } }
        }else {
            int dk=azerty_digit(k);
            if((dk>='0'&&dk<='9')||dk=='-'||k==KEY_BACKSPACE||k==127) edit_buf(a->dateDraft,sizeof(a->dateDraft), (k==KEY_BACKSPACE||k==127)?k:dk);
            /* other keys ignored — never freeze */
        }
    }else if(a->screen==SCR_ACTIVITIES){
        unsigned remaining=ACTIVITY_COUNT-a->page*PAGE_SIZE;
        unsigned rows=remaining<PAGE_SIZE?remaining:PAGE_SIZE;
        if(k==KEY_LEFT||k==KEY_RIGHT){ a->page=(a->page+(k==KEY_RIGHT?1:PAGE_COUNT-1))%PAGE_COUNT; a->selection=0; }
        else if(k==KEY_UP) a->selection=(a->selection+rows-1)%rows;
        else if(k==KEY_DOWN) a->selection=(a->selection+1)%rows;
        else if(a->daysBack && (k==KEY_ENTER||(k>='1'&&k<='9'))) read_only(a);
        else if(k==KEY_ENTER||k==' '||(k>='1'&&k<='9')){
            unsigned id=k==KEY_ENTER||k==' '?a->page*PAGE_SIZE+a->selection:(unsigned)(azerty_digit(k)-'1');
            if(id<ACTIVITY_COUNT){
                g_scratch=a->state;
                g_scratch.today.flags^=(uint16_t)(1u<<id);
                if(commit(a,&g_scratch)){ a->page=id/PAGE_SIZE; a->selection=id%PAGE_SIZE; }
            }
        }
    }else if(a->screen==SCR_NOTE){
        if(a->daysBack){ if(k==KEY_ENTER) go(a,SCR_HOME); else read_only(a); }
        else if(k==KEY_ENTER){ g_scratch=a->state; memcpy(g_scratch.today.note,a->draft,NOTE_LENGTH+1); if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); } }
        else edit_buf(a->draft,sizeof(a->draft),k);
    }else if(a->screen==SCR_WEIGHT){
        if(a->daysBack){ if(k==KEY_ENTER) go(a,SCR_HOME); else read_only(a); }
        else if(k==KEY_UP||k==KEY_DOWN||k==KEY_TAB) a->field=1-a->field;
        else if(k==KEY_ENTER){ g_scratch=a->state;
            if(!parse_weight(a->weights[0],&g_scratch.today.expected)||!parse_weight(a->weights[1],&g_scratch.today.actual))
                say(a,tr(a,"Invalid weight (example: 110.500)","Poids invalide (exemple : 110,500)"));
            else if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); }
        }else { int dk=azerty_digit(k); if((dk>='0'&&dk<='9')||dk=='.'||dk==','||k==KEY_BACKSPACE||k==127) edit_buf(a->weights[a->field],8,(k==KEY_BACKSPACE||k==127)?k:dk); }
    }else if(a->screen==SCR_KANBAN){
        unsigned n=tasks_in(a,a->kanbanColumn); if(n && a->selection>=n) a->selection=n-1;
        if(k==KEY_LEFT||k==KEY_RIGHT){
            unsigned column=(unsigned)a->kanbanColumn; a->kanbanColumn=(int)((column+(k==KEY_RIGHT?1u:2u))%3u); a->selection=0;
        }else if(k==KEY_UP && n) a->selection=(a->selection+n-1)%n;
        else if(k==KEY_DOWN && n) a->selection=(a->selection+1)%n;
        else if(k=='n'||k=='N'){
            if(a->state.taskCount>=TASK_CAPACITY) say(a,tr(a,"Kanban is full (24 tasks)","Kanban complet (24 taches)"));
            else begin_task_edit(a,TASK_CAPACITY);
        }else if(k==KEY_ENTER && n) begin_task_edit(a,task_at(a,a->kanbanColumn,a->selection));
        else if(k==KEY_DELETE && n){
            unsigned index=task_at(a,a->kanbanColumn,a->selection); unsigned i;
            g_scratch=a->state;
            for(i=index;i+1<g_scratch.taskCount;i++) g_scratch.tasks[i]=g_scratch.tasks[i+1];
            memset(&g_scratch.tasks[--g_scratch.taskCount],0,sizeof(Task));
            if(commit(a,&g_scratch) && a->selection>=tasks_in(a,a->kanbanColumn) && a->selection) a->selection--;
        }else if(k>=KEY_F1 && k<=KEY_F3 && n) move_task_to(a, k-KEY_F1);
        else if((k=='1'||k=='2'||k=='3') && n) move_task_to(a, k-'1');
        else if(k==KEY_F4 || k=='-'){ if(n) reorder_task(a,0); }
        else if(k==KEY_F5 || k=='+'){ if(n) reorder_task(a,1); }
    }else if(a->screen==SCR_TASK_EDIT){
        if(k==KEY_ENTER){
            if(!a->taskDraft[0]) say(a,tr(a,"Task name cannot be empty","Le nom ne peut pas etre vide"));
            else {
                unsigned destination=a->editedTask;
                g_scratch=a->state;
                if(a->editedTask<TASK_CAPACITY) memcpy(g_scratch.tasks[a->editedTask].name,a->taskDraft,TASK_NAME_LENGTH+1);
                else if(g_scratch.taskCount>=TASK_CAPACITY){ say(a,tr(a,"Kanban is full (24 tasks)","Kanban complet (24 taches)")); app_render(a); return; }
                else { destination=g_scratch.taskCount; g_scratch.tasks[g_scratch.taskCount].status=(uint8_t)a->kanbanColumn; memcpy(g_scratch.tasks[g_scratch.taskCount].name,a->taskDraft,TASK_NAME_LENGTH+1); g_scratch.taskCount++; }
                if(commit(a,&g_scratch)){ a->editedTask=destination; a->selection=task_position(a,a->kanbanColumn,a->editedTask); a->screen=SCR_KANBAN; a->message[0]=0; }
            }
        }else edit_buf(a->taskDraft,sizeof(a->taskDraft),k);
    }else if(a->screen==SCR_DAILY_REVIEW){
        unsigned maximum=review_rows(a)>8?review_rows(a)-8:0;
        if(k==KEY_DOWN && a->reviewOffset<maximum) a->reviewOffset++;
        else if(k==KEY_UP && a->reviewOffset) a->reviewOffset--;
        else if(k==KEY_RIGHT) a->reviewOffset=(a->reviewOffset+8<maximum)?a->reviewOffset+8:maximum;
        else if(k==KEY_LEFT) a->reviewOffset=a->reviewOffset>8?a->reviewOffset-8:0;
    }else if(a->screen==SCR_CONFIG){
        if(k==KEY_UP) a->selection=(a->selection+2)%3;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%3;
        if(k>='1'&&k<='3'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){
            if(a->selection==0) go(a,SCR_LANGUAGE);
            else if(a->selection==1) go(a,SCR_COLORS);
            else { g_scratch=a->state; g_scratch.language=(uint8_t)a->pendingLanguage;
                if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); say(a,tr(a,"Configuration saved","Configuration enregistree")); }}
        }
    }else if(a->screen==SCR_LANGUAGE){
        if(k==KEY_UP||k==KEY_DOWN) a->selection=1-a->selection;
        if(k=='1'||k=='2'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->pendingLanguage=(int)a->selection; go(a,SCR_CONFIG); }
    }else if(a->screen==SCR_COLORS){
        if(k==KEY_UP) a->selection=(a->selection+4)%5;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%5;
        if(k>='1'&&k<='5') a->selection=(unsigned)(k-'1');
        a->pendingPalette=(int)a->selection;
        if(k==KEY_ENTER){ g_scratch=a->state; g_scratch.palette=(uint8_t)a->pendingPalette;
            if(commit(a,&g_scratch)){ flush_save(a); go(a,SCR_HOME); say(a,tr(a,"Color saved","Couleur enregistree")); }}
    }else if(a->screen==SCR_ROSARY_MODE){
        if(k==KEY_UP) a->selection=(a->selection+3)%4;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%4;
        if(k>='1'&&k<='4'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->rosaryMode=(int)a->selection; go(a,SCR_ROSARY_SET); }
    }else if(a->screen==SCR_ROSARY_SET){
        if(k==KEY_UP) a->selection=(a->selection+4)%5;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%5;
        if(k>='1'&&k<='5'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->rosarySet=a->selection? (int)(a->selection-1): suggested_mysteries(a);
            if(a->rosaryMode==ROS_DECADE) go(a,SCR_ROSARY_DECADE);
            else { a->rosaryStep=a->rosaryPage=0; go(a,SCR_ROSARY_PRAYER); }}
    }else if(a->screen==SCR_ROSARY_DECADE){
        if(k==KEY_UP) a->selection=(a->selection+4)%5;
        if(k==KEY_DOWN) a->selection=(a->selection+1)%5;
        if(k>='1'&&k<='5'){ a->selection=(unsigned)(k-'1'); k=KEY_ENTER; }
        if(k==KEY_ENTER){ a->rosaryDecade=a->selection; a->rosaryStep=a->rosaryPage=0; go(a,SCR_ROSARY_PRAYER); }
    }else if(a->screen==SCR_ROSARY_PRAYER){
        const char *text=rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep);
        unsigned pages=(display_cells(text)+359)/360;
        if(k==KEY_LEFT){
            if(a->rosaryPage) a->rosaryPage--;
            else if(a->rosaryStep){ a->rosaryStep--; text=rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep); a->rosaryPage=(display_cells(text)+359)/360-1; }
        }else if(k==KEY_RIGHT||k==KEY_ENTER||k==' '){
            if(a->rosaryPage+1<pages) a->rosaryPage++;
            else if(a->rosaryStep+1<rosary_steps(a->rosaryMode)){ a->rosaryStep++; a->rosaryPage=0; }
            else go(a,SCR_ROSARY_COMPLETE);
        }
    }else if(a->screen==SCR_ROSARY_COMPLETE){
        if(k==KEY_ENTER) go(a,SCR_HOME);
    }else if(a->screen==SCR_FINISH){
        if(k==KEY_ENTER){
            uint32_t gain=points(&a->state.today);
            if(a->state.today.number==0xfffffffful || a->state.xp>0xfffffffful-gain) say(a,tr(a,"Counter limit reached","Limite du compteur atteinte"));
            else if(!valid_date(a->state.today.date)) say(a,tr(a,"Set Date before closing the day","Renseignez Date avant de terminer le jour"));
            else {
                Date tomorrow; uint8_t won=0; unsigned i;
                g_scratch=a->state;
                g_scratch.history[g_scratch.next]=g_scratch.today;
                g_scratch.next=(uint8_t)((g_scratch.next+1)%HISTORY_LENGTH);
                if(g_scratch.count<HISTORY_LENGTH) g_scratch.count++;
                tomorrow=a->state.today.date;
                if(!next_date(&tomorrow)){ say(a,tr(a,"Calendar ends on 2099-12-31","Le calendrier s'arrete au 2099-12-31")); app_render(a); return; }
                if(!a->hw->export_weight_csv(a->hw,&a->state.today)){ say(a,tr(a,"Weight CSV export failed; day remains open","Export CSV poids echoue ; journee reste ouverte")); app_render(a); return; }
                if(!a->hw->export_ics(a->hw,&a->state.today,a->state.xp+gain,gain,a->state.language)){ say(a,tr(a,"ICS export failed; day remains open","Export ICS echoue ; journee reste ouverte")); app_render(a); return; }
                g_scratch.xp+=gain; memset(&g_scratch.today,0,sizeof(Day)); g_scratch.today.number=a->state.today.number+1; g_scratch.today.date=tomorrow; g_scratch.today.expected=-1; g_scratch.today.actual=-1;
                for(i=0;i<6;i++) if(a->state.xp<GIFT_XP[i] && g_scratch.xp>=GIFT_XP[i]) won|=(uint8_t)(1u<<i);
                if(commit(a,&g_scratch) && flush_save(a)){ a->earned=gain; a->unlocked=won; go(a,SCR_REWARD); }
            }
        }
    }else if(a->screen==SCR_REWARD && k==KEY_ENTER) go(a,SCR_HOME);
    app_render(a);
}

void app_render(App *a){
    char b[128]; const Day *day=app_viewed_day(a); char date[11]; int statusRow; unsigned i;
    a->hw->set_palette(a->hw, displayed_palette(a));
    a->hw->clear(a->hw);
    if(valid_date(day->date)) date_text(day->date,date,sizeof(date));
    else sprintf(date,"%s%lu", tr(a,"D","J"), (unsigned long)day->number);
    sprintf(b,"LPS COMPANION v%s    %s", LPS_VERSION_STR, date); line(a,0,b,1);
    sprintf(b, tr(a,"%lu XP earned","%lu XP acquis"), (unsigned long)a->state.xp); line(a,1,b,0);
    if(a->daysBack) line(a,2,tr(a,"ARCHIVED DAY - READ ONLY","JOUR ARCHIVE - LECTURE SEULE"),0);

    if(a->screen==SCR_HOME){
        line(a,3, a->daysBack?tr(a,"CLOSED DAY","JOUR TERMINE"):tr(a,"TODAY","AUJOURD'HUI"),0);
        sprintf(b,tr(a,"%u/%u activities - %lu XP %s","%u/%u activites - %lu XP %s"),
            count_flags(day->flags), ACTIVITY_COUNT, (unsigned long)points(day),
            a->daysBack?tr(a,"banked","valides"):tr(a,"pending","a valider")); line(a,4,b,0);
        {
            const char *menu[11];
            unsigned first=a->daysBack?1u:0u, total=a->daysBack?3u:11u;
            menu[0]="0  Date";
            menu[1]=tr(a,"1  Activities","1  Activites");
            menu[2]=tr(a,"2  Notepad","2  Bloc notes");
            menu[3]=tr(a,"3  Weight tracker","3  Suivi du poids");
            menu[4]="4  Kanban";
            menu[5]=tr(a,"5  Christian Rosary","5  Rosaire chretien");
            menu[6]=tr(a,"6  Daily review","6  Revue journaliere");
            menu[7]="7  Config";
            menu[8]=tr(a,"8  Reminders","8  Rappels");
            menu[9]=tr(a,"9  Timer","9  Minuteur");
            menu[10]=tr(a,"10 Close the day","10 Terminer le jour");
            for(i=0;i<total;i++) line(a,6+(int)i, menu[first+i], i==a->selection);
        }
        line(a,17,tr(a,"<- Previous day   -> Current day","<- Jour precedent   -> Jour actuel"),0);
    }else if(a->screen==SCR_TIMER_SELECT){
        const char *options[]={"5 minutes","15 minutes","30 minutes"};
        line(a,3,tr(a,"TIMER","MINUTEUR"),0);
        for(i=0;i<3;i++) line(a,6+(int)i*2, options[i], a->selection==i);
    }else if(a->screen==SCR_TIMER_RUNNING||a->screen==SCR_TIMER_COMPLETE){
        unsigned seconds=timer_seconds(a);
        line(a,3,tr(a,"TIMER","MINUTEUR"),0);
        sprintf(b,"%02u:%02u",seconds/60,seconds%60);
        a->hw->large_text(a->hw,b,seconds<60);
        if(a->screen==SCR_TIMER_COMPLETE) line(a,14,tr(a,"TIMER FINISHED!","MINUTEUR TERMINE !"),0);
        else if(a->timerPaused) line(a,14,tr(a,"PAUSED","EN PAUSE"),0);
        line(a,17, a->screen==SCR_TIMER_COMPLETE?tr(a,"Enter: Again   Esc: Back","Enter: Nouveau   Esc: Retour"):
            (a->timerPaused?tr(a,"Enter: Resume   Esc: Cancel","Enter: Reprendre   Esc: Annuler"):tr(a,"Enter: Pause   Esc: Cancel","Enter: Pause   Esc: Annuler")),0);
    }else if(a->screen==SCR_REMINDER_POPUP){
        unsigned row=6;
        line(a,3,tr(a,"REMINDERS","RAPPELS"),0);
        for(i=0;i<a->state.reminderCount;i++) if(rem_active(a,&a->state.reminders[i])){
            sprintf(b,"%u. %s", row-5, a->state.reminders[i].text); line(a,(int)row,b,0); row+=2;
        }
        line(a,15,tr(a,"Press any key to continue","Appuyez sur une touche"),0);
    }else if(a->screen==SCR_REMINDERS){
        line(a,3,tr(a,"REMINDERS (max 3)","RAPPELS (max 3)"),0);
        if(!a->state.reminderCount) line(a,6,tr(a,"No reminders","Aucun rappel"),0);
        for(i=0;i<a->state.reminderCount;i++){ char from[11]; date_text(a->state.reminders[i].from,from,sizeof(from));
            sprintf(b,"%s  %u %s",from,(unsigned)a->state.reminders[i].duration,tr(a,"days","jours"));
            line(a,6+(int)i*3,b,i==a->selection); line(a,7+(int)i*3,a->state.reminders[i].text,0);
        }
        line(a,16,tr(a,"N New  Enter Edit  Del Delete","N Nouveau Entree Modifier Suppr Effacer"),0);
    }else if(a->screen==SCR_REMINDER_EDIT){
        line(a,3,tr(a,"EDIT REMINDER","MODIFIER LE RAPPEL"),0);
        sprintf(b,tr(a,"From: %s","Debut : %s"),a->reminderDate); line(a,6,b,a->reminderField==0);
        sprintf(b,tr(a,"Days: %s","Jours : %s"),a->reminderDuration); line(a,8,b,a->reminderField==1);
        line(a,10,tr(a,"Text:","Texte :"),0); line(a,11,a->reminderText,a->reminderField==2);
        line(a,15,tr(a,"Up/Down fields  Enter save","Haut/Bas champs  Entree valider"),0);
    }else if(a->screen==SCR_DATE){
        line(a,3,"DATE",0); line(a,6,a->dateDraft,1);
        line(a,9,tr(a,"Calendar: 2000-01-01 to 2099-12-31","Calendrier : 2000-01-01 au 2099-12-31"),0);
        line(a,12,tr(a,"Enter: save / Esc: cancel","Entree : valider / Echap : annuler"),0);
        line(a,14,tr(a,"Next day is suggested after closing.","Jour suivant propose apres cloture."),0);
    }else if(a->screen==SCR_ACTIVITIES){
        unsigned end=(a->page+1)*PAGE_SIZE; if(end>ACTIVITY_COUNT) end=ACTIVITY_COUNT;
        sprintf(b,tr(a,"ACTIVITIES                      %u/%u","ACTIVITES                       %u/%u"),a->page+1,PAGE_COUNT); line(a,3,b,0);
        for(i=a->page*PAGE_SIZE;i<end;i++){
            sprintf(b,"%2u  %-24s [%c]", i+1, tr(a,ACTIVITIES_EN[i],ACTIVITIES_FR[i]), day->flags&(1u<<i)?'x':' ');
            line(a,5+(int)(i-a->page*PAGE_SIZE), b, i-a->page*PAGE_SIZE==a->selection);
        }
        line(a,15, a->daysBack?tr(a,"<- -> pages   Esc: day menu","<- -> pages   Echap : menu du jour"):tr(a,"Enter: toggle  1-9: keys  <- -> pages","Entree: cocher  1-9: racc.  <- -> pages"),0);
    }else if(a->screen==SCR_NOTE){
        line(a,3,tr(a,"NOTEPAD","BLOC NOTES"),0); wrap_text(a,5,a->draft,3);
        sprintf(b,tr(a,"%u/96 characters","%u/96 caracteres"),(unsigned)strlen(a->draft)); line(a,10,b,0);
        line(a,14, a->daysBack?tr(a,"Enter / Esc: day menu","Entree / Echap : menu du jour"):tr(a,"Enter: save / Esc: cancel","Entree : valider / Echap : annuler"),0);
    }else if(a->screen==SCR_WEIGHT){
        int32_t expg=0,actg=0; int valid;
        line(a,3,tr(a,"WEIGHT TRACKER (kg)","SUIVI DU POIDS (kg)"),0);
        sprintf(b,tr(a,"Expected : %s","Attendu  : %s"),a->weights[0]); line(a,6,b,a->field==0);
        sprintf(b,tr(a,"Actual   : %s","Effectif : %s"),a->weights[1]); line(a,9,b,a->field==1);
        valid=parse_weight(a->weights[0],&expg)&&parse_weight(a->weights[1],&actg)&&expg>=0&&actg>=0;
        if(valid){ int32_t d=actg-expg, absv=d<0?-d:d; sprintf(b,tr(a,"Difference: %c%ld.%03ld kg","Ecart : %c%ld.%03ld kg"), d<0?'-':'+', (long)(absv/1000),(long)(absv%1000)); }
        else sprintf(b,"%s",tr(a,"Difference: --","Ecart : --"));
        line(a,12,b,0);
        line(a,14,tr(a,"Blank = not recorded","Vide = non renseigne"),0);
        line(a,15, a->daysBack?tr(a,"Enter / Esc: day menu","Entree / Echap : menu du jour"):tr(a,"Up/Down: field - Enter: save","Haut/Bas : champ - Entree : valider"),0);
    }else if(a->screen==SCR_KANBAN){
        unsigned n=tasks_in(a,a->kanbanColumn); if(n && a->selection>=n) a->selection=n-1;
        sprintf(b,"KANBAN  < %s >  (%u)", task_status_name(a,a->kanbanColumn), n); line(a,3,b,0);
        if(!n) line(a,6,tr(a,"No tasks. Press N to create one.","Aucune tache. Appuyez sur N."),0);
        else {
            unsigned visible=9, first=a->selection>=visible?a->selection-visible+1:0;
            unsigned end=first+visible<n?first+visible:n; unsigned position;
            for(position=first;position<end;position++){
                unsigned index=task_at(a,a->kanbanColumn,position);
                sprintf(b,"%2u  %-32s", position+1, a->state.tasks[index].name);
                line(a,5+(int)(position-first),b,position==a->selection);
            }
        }
        line(a,14,tr(a,"N New  Enter Edit  Del Delete","N Nouveau Entree Modifier Suppr Effacer"),0);
        line(a,15,tr(a,"F1 TODO  F2 DOING  F3 DONE","F1 A FAIRE F2 EN COURS F3 FINI"),0);
        line(a,16,tr(a,"F4 Up F5 Down  <- -> Columns","F4 Monter F5 Descendre <- -> Colonnes"),0);
    }else if(a->screen==SCR_TASK_EDIT){
        line(a,3, a->editedTask<TASK_CAPACITY?tr(a,"EDIT TASK","MODIFIER LA TACHE"):tr(a,"NEW TASK","NOUVELLE TACHE"),0);
        line(a,6,a->taskDraft,1);
        sprintf(b,tr(a,"%u/32 characters","%u/32 caracteres"),(unsigned)strlen(a->taskDraft)); line(a,9,b,0);
        line(a,13,tr(a,"Enter: save task","Entree : enregistrer la tache"),0);
        line(a,15,tr(a,"Backspace: erase  Esc: cancel","Retour: effacer  Echap: annuler"),0);
    }else if(a->screen==SCR_DAILY_REVIEW){
        char weight[20]; unsigned total=review_rows(a);
        sprintf(b,"%s  %s", tr(a,"DAILY REVIEW","REVUE JOURNALIERE"), date); line(a,3,b,0);
        weight_text(a->state.today.actual,weight,sizeof(weight));
        sprintf(b,tr(a,"Actual weight: %s kg","Poids effectif : %s kg"), weight); line(a,4,b,0);
        for(i=0;i<8 && a->reviewOffset+i<total;i++){ review_line(a,a->reviewOffset+i,b,sizeof(b)); line(a,7+(int)i,b,0); }
        sprintf(b,tr(a,"Up/Down scroll  %u-%u/%u  Esc back","Haut/Bas defiler  %u-%u/%u  Echap retour"),
            a->reviewOffset+1, (a->reviewOffset+8<total?a->reviewOffset+8:total), total); line(a,16,b,0);
    }else if(a->screen==SCR_CONFIG){
        line(a,3,tr(a,"CONFIGURATION","CONFIGURATION"),0);
        sprintf(b,"%s: %s", tr(a,"1  Language","1  Langue"), a->pendingLanguage==LANG_EN?"English":"Francais"); line(a,6,b,a->selection==0);
        sprintf(b,"%s: PAL%u", tr(a,"2  Colors","2  Couleurs"), (unsigned)a->state.palette+1); line(a,7,b,a->selection==1);
        line(a,8,tr(a,"3  Save","3  Enregistrer"),a->selection==2);
        line(a,11,tr(a,"Language: select, then Save.","Langue : choisir, puis enregistrer."),0);
        line(a,12,tr(a,"Esc: discard changes","Echap : annuler les modifications"),0);
    }else if(a->screen==SCR_LANGUAGE){
        line(a,3,tr(a,"LANGUAGE","LANGUE"),0);
        line(a,6,"1  English",a->selection==0); line(a,7,"2  Francais",a->selection==1);
        line(a,10,tr(a,"Enter: select / Esc: back","Entree : choisir / Echap : retour"),0);
        line(a,11,tr(a,"Then Save in Configuration.","Puis enregistrer dans Configuration."),0);
    }else if(a->screen==SCR_COLORS){
        line(a,3,tr(a,"COLORS","COULEURS"),0);
        line(a,6,tr(a,"1  PAL1  red","1  PAL1  rouge"),a->selection==0);
        line(a,7,tr(a,"2  PAL2  green","2  PAL2  vert"),a->selection==1);
        line(a,8,tr(a,"3  PAL3  blue","3  PAL3  bleu"),a->selection==2);
        line(a,9,"4  PAL4  Orange",a->selection==3);
        line(a,10,tr(a,"5  PAL5  Pink","5  PAL5  Rose"),a->selection==4);
        line(a,13,tr(a,"Enter: save color","Entree : enregistrer couleur"),0);
    }else if(a->screen==SCR_ROSARY_MODE){
        line(a,3,tr(a,"CHOOSE ROSARY MODE","CHOISIR LE MODE DU ROSAIRE"),0);
        line(a,6,tr(a,"1  One decade             3-5 min","1  Une dizaine            3-5 min"),a->selection==0);
        line(a,8,tr(a,"2  Full Rosary          15-20 min","2  Rosaire complet      15-20 min"),a->selection==1);
        line(a,10,tr(a,"3  Scripture + guidance 20-30 min","3  Ecriture + meditation 20-30 min"),a->selection==2);
        line(a,12,tr(a,"4  Slow contemplation   30-40 min","4  Contemplation lente  30-40 min"),a->selection==3);
        line(a,15,tr(a,"Choose before every new session.","Choix demande a chaque nouvelle seance."),0);
    }else if(a->screen==SCR_ROSARY_SET){
        line(a,3,tr(a,"CHOOSE THE MYSTERIES","CHOISIR LES MYSTERES"),0);
        sprintf(b,tr(a,"1  Today: %s","1  Aujourd'hui : %s"), mystery_set_name(a->state.language,suggested_mysteries(a))); line(a,6,b,a->selection==0);
        for(i=0;i<4;i++){ sprintf(b,"%u  %s",i+2, mystery_set_name(a->state.language,(int)i)); line(a,7+(int)i,b,a->selection==i+1); }
        line(a,14,tr(a,"Today's cycle is selected by date.","Cycle du jour propose automatiquement."),0);
    }else if(a->screen==SCR_ROSARY_DECADE){
        sprintf(b,tr(a,"ONE DECADE - %s MYSTERIES","UNE DIZAINE - MYSTERES %s"), mystery_set_name(a->state.language,a->rosarySet)); line(a,3,b,0);
        for(i=0;i<5;i++){ sprintf(b,"%u  %s",i+1, mystery_name(a->state.language,a->rosarySet,i)); line(a,6+(int)i,b,a->selection==i); }
        line(a,14,tr(a,"Choose the mystery to pray.","Choisissez le mystere a prier."),0);
    }else if(a->screen==SCR_ROSARY_PRAYER){
        int part=rosary_part(a->rosaryMode,a->rosaryDecade,a->rosaryStep);
        unsigned decade=rosary_view_decade(a->rosaryMode,a->rosaryDecade,a->rosaryStep);
        unsigned count=rosary_view_count(a->rosaryMode,a->rosaryDecade,a->rosaryStep);
        const char *text=rosary_text(a->state.language,a->rosaryMode,a->rosarySet,a->rosaryDecade,a->rosaryStep);
        unsigned pages=(display_cells(text)+359)/360;
        sprintf(b,"%s  %u/%u", rosary_part_name(a->state.language,a->rosaryMode,a->rosaryDecade,a->rosaryStep), a->rosaryStep+1, rosary_steps(a->rosaryMode));
        line(a,3,b,1);
        if(part==RP_MYSTERY||part==RP_SCRIPTURE||part==RP_MEDITATION||part==RP_SILENCE){
            sprintf(b,tr(a,"Mystery %u/%u - %s","Mystere %u/%u - %s"), a->rosaryMode==ROS_DECADE?1u:decade+1, a->rosaryMode==ROS_DECADE?1u:5u, mystery_set_name(a->state.language,a->rosarySet));
            line(a,4,b,0);
        }else if(part==RP_HAIL){
            char beads[12];
            sprintf(b,tr(a,"Decade %u/%u    Hail Mary %u/10","Dizaine %u/%u    Ave Maria %u/10"), a->rosaryMode==ROS_DECADE?1u:decade+1, a->rosaryMode==ROS_DECADE?1u:5u, count);
            line(a,4,b,0);
            for(i=0;i<10;i++) beads[i]=i<count?'*':'o'; beads[10]=0; line(a,5,beads,0);
        }else if(part==RP_OPEN_HAIL){ sprintf(b,tr(a,"Opening Hail Mary %u/3","Ave Maria d'ouverture %u/3"), count); line(a,4,b,0); }
        for(i=0;i<9;i++){ char slice[96]; cell_slice(text,a->rosaryPage*360+i*40,40,slice,sizeof(slice)); line(a,6+(int)i,slice,0); }
        if(pages>1){ sprintf(b,tr(a,"Page %u/%u  Enter: continue","Page %u/%u  Entree : continuer"), a->rosaryPage+1, pages); line(a,15,b,0); }
        else line(a,15,tr(a,"Enter: next prayer   <- previous","Entree : suite       <- precedent"),0);
        line(a,16,tr(a,"Esc: leave session","Echap : quitter la seance"),0);
    }else if(a->screen==SCR_ROSARY_COMPLETE){
        line(a,3,tr(a,"ROSARY COMPLETED","ROSAIRE TERMINE"),1);
        line(a,6,tr(a,"The prayer session is complete.","La seance de priere est terminee."),0);
        line(a,9,tr(a,"Remain for a moment in God's peace.","Demeurez un instant dans la paix de Dieu."),0);
        line(a,13,tr(a,"Enter: return to LPS Companion","Entree : retour a LPS Companion"),0);
    }else if(a->screen==SCR_FINISH){
        line(a,3,tr(a,"CLOSE THE DAY?","TERMINER LE JOUR ?"),0);
        sprintf(b,tr(a,"Activities: %u","Activites : %u"), count_flags(a->state.today.flags)); line(a,6,b,0);
        sprintf(b,tr(a,"Variety bonus: %u XP","Bonus variete : %u XP"), count_flags(a->state.today.flags)>=3?10u:0u); line(a,8,b,0);
        sprintf(b,"Total : +%lu XP", (unsigned long)points(&a->state.today)); line(a,10,b,0);
        line(a,13,tr(a,"Enter: save and advance","Entree : enregistrer et avancer"),0);
        line(a,15,tr(a,"An empty day has no penalty.","Une journee vide ne coute rien."),0);
    }else{
        int row=8;
        line(a,3,tr(a,"DAY SAVED","JOUR ENREGISTRE"),0);
        sprintf(b,"+%lu XP",(unsigned long)a->earned); line(a,5,b,0);
        for(i=0;i<6;i++) if(a->unlocked&(1u<<i)){ line(a,row++,tr(a,"NEW KEEPSAKE","NOUVEAU SOUVENIR"),0); line(a,row++,tr(a,GIFTS_EN[i],GIFTS_FR[i]),0); }
        line(a,15,tr(a,"Enter: start the new day","Entree : commencer le nouveau jour"),0);
    }

    statusRow = a->screen==SCR_HOME?18:((a->screen==SCR_TIMER_RUNNING||a->screen==SCR_TIMER_COMPLETE)?16:17);
    if(a->missingStorage) a->hw->alert(a->hw,statusRow,0,a->message);
    else if(a->message[0]) line(a,statusRow,a->message,0);
    if(a->hw->battery_low && a->hw->battery_low(a->hw))
        a->hw->alert(a->hw,18,0,tr(a,"[!] Low battery!","[!] Batterie faible !"));
    if(a->screen!=SCR_TIMER_RUNNING && a->screen!=SCR_TIMER_COMPLETE)
        line(a,19,tr(a,"Up/Down  Enter:OK  Esc:Back","Haut/Bas  Entree:OK  Echap:Retour"),1);
    a->hw->present(a->hw);
}

int app_flush_save(App *app){ return flush_save(app); }
int app_is_dirty(const App *app){ return app->dirty; }
