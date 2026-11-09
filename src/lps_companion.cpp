/*
 LPS Companion - v1.12, 2026-11-08.
 Screen: 320x320, 8x16 bitmap font, 40 columns x 20 rows.

 Desktop build (Fedora/Linux):
   g++ -std=c++17 -Wall -Wextra -Wpedantic -DLPS_DESKTOP lps_companion.cpp -o lps_companion
   ./lps_companion
 Tests:
   g++ -std=c++17 -Wall -Wextra -Wpedantic -DLPS_TEST lps_companion.cpp -o lps_test
   ./lps_test

 PICO INTEGRATION
 In this package src/main.cpp supplies the LCD, keyboard and SD implementation.
 Run bash build.sh from the project root to create build/lps_companion.uf2.
 Target: original RP2040 PicoCalc, standalone BOOTSEL firmware.
 v1.2 confirmed on the user's PicoCalc; v1.12 needs device testing.
 The portable application remains independent of the Pico SDK; desktop and
 built-in tests below remain available for checking the UI/state machine.

 Behaviour:
 - English default; French selectable in Config, applied on explicit Save.
 - PAL1 red, PAL2 green (default), and PAL3 blue are previewed live and saved on Enter.
 - Format 5 adds a persistent 24-task Kanban; formats 1-4 import safely.
 - 22 daily checkboxes (v1.13, LPS-113-CHG08); 10 XP each, +10 for >=3 selections (prototype rules).
 - No Journal or collectible browser; new souvenirs still appear after closing a day.
 - Expected/actual weights are OPTIONAL daily inputs, stored as integer grams.
   Comma or dot accepted, up to three decimals. No generated weight-loss target.
 - Note: 96 printable ASCII characters; accents need a font/input extension.
 - Dates are manually set, then progress through the built-in 2000-2099 Gregorian calendar.
 - Closing a dated day produces a portable ICS export on the SD card.
 - Current day autosaves on checkbox changes and explicit form validation.
 - Closing a day advances it; XP is still banked in the save for compatibility but never shown (v1.12).
 - The last 31 closed days are retained in a ring buffer, browsable read-only from Home.
 - Persistent format (v1.13, LPS-113-CHG06): JSON SAVE_A.JSN/SAVE_B.JSN, streamed with
   a hand-written token parser and CRC-32; older binary saves are read once to migrate.
 - Missing storage starts a new profile. Corruption/I/O errors block writes;
   they never silently overwrite a damaged existing save.
 - Failed saves roll back RAM state; UI reports it. A late SD error can leave
   commit status uncertain; the adapter then blocks writes until restart.
   A/B recovery chooses the highest-generation intact record. No dynamic allocation in
   application code. SD adapter must provide atomic/durable replacement.

 Desktop commands are line-based: enter/up/down/left/right/esc/back, digits 1-9,
 text YOUR TEXT, quit. To edit a field, use back, or enter an empty field as-is.
 Desktop save is lps_companion.jsn in the current working directory
 (an older lps_companion.sav is read once and left untouched).
*/
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace lps {
enum class Language : uint8_t { English, French };
enum class Palette : uint8_t { Red, Green, Blue, Orange, Pink };
enum class RosaryMode : uint8_t { Decade, Full, Guided, Contemplative };
enum class MysterySet : uint8_t { Joyful, Luminous, Sorrowful, Glorious };
// UI text is UTF-8 in source, converted to single-byte Latin-1 display cells.
// Notes remain printable ASCII; user-authored text is never translated.
void displayText(const char* utf8,char* out,std::size_t capacity){
    std::size_t n=0;
    while(*utf8&&n+1<capacity){
        unsigned c=static_cast<unsigned char>(*utf8++);
        if((c==0xc2||c==0xc3)&&((static_cast<unsigned char>(*utf8)&0xc0)==0x80))
            c=((c&0x1f)<<6)|(static_cast<unsigned char>(*utf8++)&0x3f);
        out[n++]=char(c);
    }
    out[n]=0;
}
unsigned displayWidth(const char* utf8){
    unsigned n=0;
    while(*utf8){
        unsigned c=static_cast<unsigned char>(*utf8++);
        if((c==0xc2||c==0xc3)&&*utf8)++utf8;
        ++n;
    }
    return n;
}
constexpr int Width=320, Height=320, Columns=40, Rows=20;
constexpr std::size_t NoteLength=96, HistoryLength=31, TaskNameLength=32, TaskCapacity=24, ReminderCapacity=3, ReminderTextLength=32, BlobCapacity=6144;
enum Key { Enter=13, Backspace=8, Escape=27, Up=1000, Down, Left, Right, DeleteKey, F1, F2, F3, F4, F5 };
enum class LoadResult { Missing, Ok, NoStorage, Error };
struct Day;
struct State;
struct Platform {
    virtual ~Platform() = default;
    virtual void clear()=0;
    // Coordinates in pixels. reverse=true fills all 40 cells on that row.
    virtual void text(int x,int y,const char* text,bool reverse)=0;
    // Emphasize just the cells containing a keyboard key in helper text.
    virtual void emphasize(int,int,int){}
    // A hardware-visible error line (bold red on PicoCalc). Used for faults
    // which need immediate attention rather than normal status text.
    virtual void alert(int x,int y,const char* text)=0;
    // Hardware may override this. Host/test platforms report a healthy battery.
    virtual bool batteryBelow20(){return false;}
    virtual void largeText(const char* value,bool red){if(red)alert(80,128,value);else text(80,128,value,false);}
    virtual void present()=0;
    // Apply the selected theme before drawing. The desktop implementation may ignore it.
    virtual void setPalette(Palette)=0;
    // Fill state from the newest valid save. Return Error for damaged, unsupported
    // or inaccessible saves (state may then hold partial data), Missing only if absent.
    virtual LoadResult load(State& state)=0;
    // On false, RAM is rolled back; an ambiguous late commit MUST block further
    // writes until restart. The prior valid record must be retained. On true, replacement must be
    // recoverable after restart. FatFs: use validated A/B slots with generations,
    // f_sync and startup recovery, or an equivalent transactional scheme.
    virtual bool save(const State& state)=0;
    // Export first, then save the A/B state. Retrying replaces the same named
    // export if the snapshot write later fails; no duplicate calendar event.
    virtual bool exportIcs(const Day& day,uint32_t totalXp,uint32_t dayXp,Language language,const State& state)=0;
    // The yearly weight journal is exported before the state commit as well.
    // Implementations must treat a repeated date as already exported.
    virtual bool exportWeightCsv(const Day& day)=0;
};
// LPS-113-CHG08: default list of 22 activities. Order matters: the first 16
// keep the bit positions of v1.12 binary saves; JSON saves store the French names.
constexpr const char* Activities[]={"Marche","Régime","Bible","Messe","Travail","Projet","Sieste","Gurumed","Maladie","Congé","Weekend","Sortie","Jeu","Docteur","Lecture","Shopping",
                                    "Sport","Cuisine","Ménage","Famille","Amis","Méditation"};
constexpr const char* EnglishActivities[]={"Walk","Diet","Bible","Mass","Work","Project","Nap","Gurumed","Illness","Day off","Weekend","Outing","Gaming","Doctor","Reading","Shopping",
                                           "Sport","Cooking","Chores","Family","Friends","Meditation"};
constexpr unsigned DefaultActivityCount=sizeof Activities/sizeof Activities[0];
static_assert(DefaultActivityCount==sizeof EnglishActivities/sizeof EnglishActivities[0],"Every activity needs an English name");
// Day flags hold one bit per current-list index; 30 is the design capacity (3 pages of 10).
// LPS-113-CHG07: the list itself is user-defined and stored in the JSON save.
using ActivityFlags=uint32_t;
constexpr unsigned ActivityCapacity=30;
constexpr unsigned ActivityNameLength=24; // display cells and stored UTF-8 bytes
static_assert(DefaultActivityCount<=ActivityCapacity&&ActivityCapacity<=32,"Activity flags are 32 bits");
constexpr unsigned PageSize=10;
constexpr ActivityFlags ActivityMask=ActivityFlags((uint64_t(1)<<DefaultActivityCount)-1);
constexpr unsigned LegacyActivityCount=16; // Binary saves (formats 1-6) hold 16 bits.
static_assert(LegacyActivityCount<=DefaultActivityCount,"Legacy bits must keep their activity");
struct Activity {
    uint16_t id=0;
    std::array<char,ActivityNameLength+1> french{};
    std::array<char,ActivityNameLength+1> english{};
};
unsigned activityPages(unsigned n){return n?(n+PageSize-1)/PageSize:1;}
struct Date { uint16_t year=0;uint8_t month=0,day=0; };
constexpr Date FirstCalendarDate{2000,1,1}, LastCalendarDate{2099,12,31}, DefaultDate{2026,9,17};
bool leap(uint16_t year){return year%4==0&&(year%100!=0||year%400==0);}
unsigned daysInMonth(uint16_t year,uint8_t month){
    constexpr uint8_t normal[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return month>=1&&month<=12?(month==2&&leap(year)?29:normal[month-1]):0;
}
bool validDate(Date d){return d.year>=FirstCalendarDate.year&&d.year<=LastCalendarDate.year&&d.month>=1&&d.month<=12&&d.day>=1&&d.day<=daysInMonth(d.year,d.month);}
bool nextDate(Date& d){
    if(!validDate(d)||(d.year==LastCalendarDate.year&&d.month==12&&d.day==31))return false;
    if(++d.day<=daysInMonth(d.year,d.month))return true;
    d.day=1;if(++d.month<=12)return true;d.month=1;++d.year;return true;
}
void dateText(Date d,char* out,std::size_t n){std::snprintf(out,n,"%04u-%02u-%02u",d.year,d.month,d.day);}
bool parseDate(const char* text,Date& d){
    if(std::strlen(text)!=10||text[4]!='-'||text[7]!='-')return false;
    for(unsigned i=0;i<10;++i)if(i!=4&&i!=7&&(text[i]<'0'||text[i]>'9'))return false;
    d.year=uint16_t((text[0]-'0')*1000+(text[1]-'0')*100+(text[2]-'0')*10+text[3]-'0');
    d.month=uint8_t((text[5]-'0')*10+text[6]-'0');d.day=uint8_t((text[8]-'0')*10+text[9]-'0');return validDate(d);
}
struct Day {
    Date date=DefaultDate; // Old-format decoder explicitly clears this.
    uint32_t number=1;
    ActivityFlags flags=0;
    int32_t expected=-1, actual=-1; // grams; -1 is missing
    std::array<char,NoteLength+1> note{};
    // Frozen ticked names (French identity name, or the only language if the other is empty).
    // History keeps these when the list is later renamed or deleted (LPS-113-CHG07).
    uint8_t namedCount=0;
    std::array<std::array<char,ActivityNameLength+1>,ActivityCapacity> names{};
};
enum class TaskStatus : uint8_t { Todo, Doing, Done };
struct Task {
    TaskStatus status=TaskStatus::Todo;
    std::array<char,TaskNameLength+1> name{};
};
struct Reminder { Date from{};uint16_t duration=1;std::array<char,ReminderTextLength+1> text{}; };
struct State {
    Language language=Language::English;
    Palette palette=Palette::Green;
    uint32_t xp=0;
    Day today{};
    std::array<Day,HistoryLength> history{};
    uint8_t next=0, count=0;
    uint8_t taskCount=0;
    std::array<Task,TaskCapacity> tasks{};
    uint8_t reminderCount=0;
    std::array<Reminder,ReminderCapacity> reminders{};
    uint8_t activityCount=0;
    uint16_t nextActivityId=1;
    std::array<Activity,ActivityCapacity> activities{};
    State();
};
const char* storedName(const Activity& a){return a.french[0]?a.french.data():a.english.data();}
const char* displayName(const Activity& a,Language language){
    if(language==Language::French)return a.french[0]?a.french.data():a.english.data();
    return a.english[0]?a.english.data():a.french.data();
}
const char* languageName(const Activity& a,Language language){
    return language==Language::French?a.french.data():a.english.data();
}
unsigned indexByStoredName(const State& s,const char* name){
    if(!name||!*name)return ActivityCapacity;
    for(unsigned i=0;i<s.activityCount;++i)if(!std::strcmp(storedName(s.activities[i]),name))return i;
    return ActivityCapacity;
}
void copyActivityName(std::array<char,ActivityNameLength+1>& dest,const char* src){
    dest.fill(0);if(src)std::snprintf(dest.data(),dest.size(),"%s",src);
}
void fillDefaultActivities(State& s){
    s.activityCount=uint8_t(DefaultActivityCount);s.nextActivityId=uint16_t(DefaultActivityCount+1);
    for(Activity& a:s.activities)a=Activity{};
    for(unsigned i=0;i<DefaultActivityCount;++i){
        s.activities[i].id=uint16_t(i+1);
        copyActivityName(s.activities[i].french,Activities[i]);
        copyActivityName(s.activities[i].english,EnglishActivities[i]);
    }
}
State::State(){fillDefaultActivities(*this);}
void resolveDayFlags(State& s,Day& d){
    if(!d.namedCount)return;
    d.flags=0;
    for(unsigned i=0;i<d.namedCount;++i){
        const unsigned idx=indexByStoredName(s,d.names[i].data());
        if(idx<s.activityCount)d.flags|=ActivityFlags(1u)<<idx;
    }
}
void resolveAllDayFlags(State& s){
    resolveDayFlags(s,s.today);
    for(unsigned i=0;i<s.count;++i)resolveDayFlags(s,s.history[i]);
}
void freezeDayNames(const State& s,Day& d){
    d.namedCount=0;
    for(unsigned i=0;i<s.activityCount&&d.namedCount<ActivityCapacity;++i)if(d.flags&(ActivityFlags(1u)<<i)){
        copyActivityName(d.names[d.namedCount],storedName(s.activities[i]));++d.namedCount;
    }
    for(unsigned i=d.namedCount;i<ActivityCapacity;++i)d.names[i].fill(0);
}
void refreshActivityFlags(State& s){
    // Today's ticks follow the live flags; freeze them under the new names
    // first so a French rename or a session-only tick is not lost when
    // history is rematched from its own frozen names.
    freezeDayNames(s,s.today);
    resolveAllDayFlags(s);
}
void swapActivities(State& s,unsigned a,unsigned b){
    if(a>=s.activityCount||b>=s.activityCount||a==b)return;
    const Activity keep=s.activities[a];s.activities[a]=s.activities[b];s.activities[b]=keep;
    const ActivityFlags bitA=ActivityFlags(1u)<<a,bitB=ActivityFlags(1u)<<b;
    const bool hadA=s.today.flags&bitA,hadB=s.today.flags&bitB;
    s.today.flags&=~(bitA|bitB);
    if(hadA)s.today.flags|=bitB;if(hadB)s.today.flags|=bitA;
}
void rotateActivity(State& s,bool firstToEnd){
    if(s.activityCount<2)return;
    const unsigned n=s.activityCount;
    if(firstToEnd){
        const Activity keep=s.activities[0];
        for(unsigned i=0;i+1<n;++i)s.activities[i]=s.activities[i+1];
        s.activities[n-1]=keep;
        const bool had=s.today.flags&1u;
        s.today.flags>>=1;
        if(had)s.today.flags|=ActivityFlags(1u)<<(n-1);
        s.today.flags&=n>=32?~ActivityFlags(0):(ActivityFlags(1u)<<n)-1;
    }else{
        const Activity keep=s.activities[n-1];
        for(unsigned i=n-1;i>0;--i)s.activities[i]=s.activities[i-1];
        s.activities[0]=keep;
        const ActivityFlags lastBit=ActivityFlags(1u)<<(n-1);
        const bool had=s.today.flags&lastBit;
        s.today.flags=((s.today.flags&(lastBit-1))<<1)|(had?ActivityFlags(1u):0);
    }
}
void trimName(char* text){
    char* start=text;while(*start==' ')++start;
    std::size_t n=std::strlen(start);while(n&&start[n-1]==' ')--n;
    start[n]=0;if(start!=text)std::memmove(text,start,n+1);
}
bool namesEqualIgnoreCase(const char* a,const char* b){
    auto ascii=[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);};
    auto c3=[](unsigned char c){return c>=0x80&&c<=0x9e&&c!=0x97?char(c+0x20):char(c);};
    while(*a&&*b){
        unsigned char ca=static_cast<unsigned char>(*a++),cb=static_cast<unsigned char>(*b++);
        if(ca==0xc3&&cb==0xc3&&*a&&*b){if(c3(static_cast<unsigned char>(*a++))!=c3(static_cast<unsigned char>(*b++)))return false;}
        else if(ascii(ca)!=ascii(cb))return false;
    }
    return!*a&&!*b;
}
bool duplicateActivityName(const State& s,const char* name,unsigned except,Language language){
    for(unsigned i=0;i<s.activityCount;++i)
        if(i!=except&&namesEqualIgnoreCase(displayName(s.activities[i],language),name))return true;
    return false;
}
bool validActivityName(const char* name){
    if(!name||!*name||std::strlen(name)>ActivityNameLength||displayWidth(name)>ActivityNameLength)return false;
    for(const char* p=name;*p;++p){const unsigned char c=static_cast<unsigned char>(*p);if(c<32||c==127)return false;}
    return true;
}
template<class F> void eachExportedActivity(const State& s,const Day& d,Language language,F&& fn){
    if(d.namedCount){
        for(unsigned i=0;i<d.namedCount;++i){
            const unsigned idx=indexByStoredName(s,d.names[i].data());
            fn(idx<s.activityCount?displayName(s.activities[idx],language):d.names[i].data());
        }
    }else{
        for(unsigned i=0;i<s.activityCount;++i)if(d.flags&(ActivityFlags(1u)<<i))fn(displayName(s.activities[i],language));
    }
}

unsigned count(ActivityFlags flags) { unsigned n=0;for(;flags;flags>>=1)n+=flags&1;return n; }
uint32_t points(const Day& d) { unsigned n=count(d.flags);return n*10+(n>=3?10:0); }
bool parseWeight(const char* s,int32_t& grams) {
    if(!*s){grams=-1;return true;}
    uint32_t whole=0,fraction=0;unsigned digits=0,decimals=0;
    while(*s>='0'&&*s<='9'){if(++digits>3)return false;whole=whole*10+unsigned(*s++-'0');}
    if(!digits)return false;
    if(*s=='.'||*s==','){
        ++s;while(*s>='0'&&*s<='9'){if(++decimals>3)return false;fraction=fraction*10+unsigned(*s++-'0');}
        if(!decimals)return false;
    }
    if(*s)return false;
    for(;decimals<3;++decimals)fraction*=10;
    grams=int32_t(whole*1000+fraction);return grams>0;
}
void weightText(int32_t grams,char* out,std::size_t n) {
    if(grams<0)std::snprintf(out,n,"--");
    else std::snprintf(out,n,"%ld.%03ld",long(grams/1000),long(grams%1000));
}
uint32_t crc32Update(uint32_t c,uint8_t byte) {
    c^=byte;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));
    return c;
}
uint32_t crc32(const uint8_t* p,std::size_t n) {
    uint32_t c=0xffffffffu;
    while(n--)c=crc32Update(c,*p++);
    return ~c;
}
struct Blob { std::array<uint8_t,BlobCapacity> bytes{};std::size_t size=0; };
// Legacy binary saves (SAVE_A/B.BIN, up to v1.12) are only READ now, to
// migrate them to JSON (LPS-113-CHG06); nothing writes this format any more.
// Format 1: v1 base. Format 2: adds language. Format 3: adds dates.
// Format 4: palette. Format 5: Kanban. Format 6: three dated reminders.
constexpr std::size_t WireV1=3568, WireV2=WireV1+1, WireV3=WireV2+32*4, WireV4=WireV3+1;
constexpr std::size_t WireV5=WireV4+1+TaskCapacity*(1+TaskNameLength+1);
constexpr std::size_t WireSize=WireV5+1+ReminderCapacity*(2+1+1+2+ReminderTextLength+1);
void resetState(State& s);
bool decode(const uint8_t* bytes,std::size_t size,State& out) {
    if(size!=WireSize&&size!=WireV5&&size!=WireV4&&size!=WireV3&&size!=WireV2&&size!=WireV1)return false;
    std::size_t pos=size-4;
    auto get=[&](unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(bytes[pos++])<<(i*8);return v;};
    if(get(4)!=crc32(bytes,size-4))return false;
    pos=0;if(get(4)!=0x3153504c)return false;
    const auto version=get(2);
    if(!((version==1&&size==WireV1)||(version==2&&size==WireV2)||(version==3&&size==WireV3)||(version==4&&size==WireV4)||(version==5&&size==WireV5)||(version==6&&size==WireSize)))return false;
    resetState(out);
    out.xp=get(4);out.next=uint8_t(get(1));out.count=uint8_t(get(1));
    if(version>=2){const auto language=get(1);if(language>1)return false;out.language=Language(language);}
    if(version>=4){const auto palette=get(1);if(palette>4)return false;out.palette=Palette(palette);}
    bool valid=out.next<HistoryLength&&out.count<=HistoryLength;
    auto day=[&](Day& d){d.date={};if(version>=3){d.date.year=uint16_t(get(2));d.date.month=uint8_t(get(1));d.date.day=uint8_t(get(1));}
        d.number=get(4);d.flags=ActivityFlags(get(2)); // Bits 0-15: the first 16 activities, unchanged.
        auto w=[&](){uint32_t v=get(4);if(v==0xffffffffu)return int32_t(-1);if(v==0||v>999999){valid=false;return int32_t(-1);}return int32_t(v);};
        d.expected=w();d.actual=w();for(char& c:d.note)c=char(get(1));
        valid=valid&&d.number>0&&(d.flags&~ActivityMask)==0&&d.note.back()==0&&(d.date.year==0||validDate(d.date));
        bool ended=false;for(char c:d.note){if(!c)ended=true;else if(!ended&&(c<32||c>126))valid=false;}
    };
    day(out.today);for(Day& d:out.history)day(d);
    if(version>=5){
        out.taskCount=uint8_t(get(1));valid=valid&&out.taskCount<=TaskCapacity;
        for(unsigned i=0;i<TaskCapacity;++i){
            const auto status=get(1);if(status>2)valid=false;out.tasks[i].status=TaskStatus(status);
            for(char& c:out.tasks[i].name)c=char(get(1));
            valid=valid&&out.tasks[i].name.back()==0;
            bool ended=false;for(char c:out.tasks[i].name){if(!c)ended=true;else if(!ended&&(c<32||c>126))valid=false;}
            if(i<out.taskCount&&out.tasks[i].name[0]==0)valid=false;
        }
    }
    if(version>=6){
        out.reminderCount=uint8_t(get(1));valid=valid&&out.reminderCount<=ReminderCapacity;
        for(unsigned i=0;i<ReminderCapacity;++i){
            Reminder& r=out.reminders[i];r.from.year=uint16_t(get(2));r.from.month=uint8_t(get(1));r.from.day=uint8_t(get(1));r.duration=uint16_t(get(2));
            for(char& c:r.text)c=char(get(1));
            valid=valid&&r.text.back()==0;
            bool ended=false;for(char c:r.text){if(!c)ended=true;else if(!ended&&(c<32||c>126))valid=false;}
            if(i<out.reminderCount)valid=valid&&validDate(r.from)&&r.duration>=1&&r.duration<=365&&r.text[0]!=0;
        }
    }
    if(!valid)return false;
    return true;
}
// ---------------------------------------------------------------------------
// LPS-113-CHG06: JSON save files (SAVE_A.JSN / SAVE_B.JSN).
// The save is one JSON object, written and read as a stream: no object tree,
// no dynamic allocation, only small fixed buffers, so the same code can move
// to a 640 KB MS-DOS machine. Schema 2 (CHG07) adds the editable catalog;
// schema 1 files still load:
//   {"format":"lps-companion","version":2,"generation":N,
//    "state":{"language":"en","palette":"green","xp":0,
//             "activities":[{"id":1,"fr":"Marche","en":"Walk"},...],
//             "today":{DAY},"history":[DAY,...],"kanban":[TASK,...],"reminders":[REMINDER,...]},
//    "crc32":"xxxxxxxx"}
//   DAY      {"date":"YYYY-MM-DD"|null,"number":N,"activities":["Marche",...],
//             "expected":grams|null,"actual":grams|null,"note":"..."}
//   TASK     {"status":"todo"|"doing"|"done","name":"..."}
//   REMINDER {"from":"YYYY-MM-DD","days":N,"text":"..."}
// "crc32" is the CRC-32 (IEEE, lowercase hex) of every byte before the
// "crc32" key and must be the last member. History is stored oldest first.
// Each day stores ticked identity names (French if set, otherwise English).
// Names that no longer match the catalog are kept as frozen extras.
// A v1 file with no catalog receives the default list of 22. Unknown keys
// are skipped; a newer schema version is refused and blocks writes.
constexpr uint32_t SaveSchemaVersion=2;
constexpr std::size_t SaveTextCapacity=128, SaveIoBuffer=256, SaveMaxBytes=65536;
struct SaveSink { virtual ~SaveSink()=default; virtual bool write(const char* data,std::size_t size)=0; };
// read() returns the number of bytes read, 0 at the end, or a negative value on an I/O error.
struct SaveSource { virtual ~SaveSource()=default; virtual long read(char* data,std::size_t capacity)=0; };
enum class SaveCheck { Valid, Damaged, Unsupported, IOError };
struct SaveInfo { uint32_t generation=0, checksum=0; };
constexpr const char* PaletteNames[]={"red","green","blue","orange","pink"};
constexpr const char* StatusNames[]={"todo","doing","done"};
constexpr const char* LanguageNames[]={"en","fr"};
class JsonWriter {
    SaveSink& sink;
    std::array<char,SaveIoBuffer> buffer{};
    std::size_t used=0;
    uint32_t crc=0xffffffffu;
    bool ok=true;
public:
    explicit JsonWriter(SaveSink& s):sink(s){}
    void raw(const char* p,std::size_t n){
        while(n--){const char c=*p++;crc=crc32Update(crc,uint8_t(c));buffer[used++]=c;if(used==buffer.size())flush();}
    }
    void raw(const char* s){raw(s,std::strlen(s));}
    void flush(){if(used&&ok)ok=sink.write(buffer.data(),used);used=0;}
    // UTF-8 passes through; quotes, backslashes and control characters are escaped.
    void text(const char* s){
        raw("\"",1);
        for(;*s;++s){
            const unsigned char c=static_cast<unsigned char>(*s);
            if(c=='"')raw("\\\"");else if(c=='\\')raw("\\\\");
            else if(c=='\n')raw("\\n");else if(c=='\r')raw("\\r");else if(c=='\t')raw("\\t");
            else if(c<0x20){char e[8];std::snprintf(e,sizeof e,"\\u%04x",unsigned(c));raw(e);}
            else raw(s,1);
        }
        raw("\"",1);
    }
    void key(const char* k){text(k);raw(":",1);}
    void number(uint32_t v){char b[12];std::snprintf(b,sizeof b,"%lu",static_cast<unsigned long>(v));raw(b);}
    void weight(int32_t grams){if(grams<0)raw("null");else number(uint32_t(grams));}
    void date(Date d){if(d.year==0){raw("null");return;}char b[16];dateText(d,b,sizeof b);text(b);}
    uint32_t checksum()const{return ~crc;}
    bool good()const{return ok;}
};
void writeDay(JsonWriter& w,const State& s,const Day& d){
    w.raw("{");w.key("date");w.date(d.date);w.raw(",");w.key("number");w.number(d.number);
    w.raw(",");w.key("activities");w.raw("[");
    bool first=true;
    auto emit=[&](const char* name){if(!first)w.raw(",");w.text(name);first=false;};
    for(unsigned i=0;i<s.activityCount;++i)if(d.flags&(ActivityFlags(1u)<<i))emit(storedName(s.activities[i]));
    for(unsigned i=0;i<d.namedCount;++i)if(indexByStoredName(s,d.names[i].data())>=s.activityCount)emit(d.names[i].data());
    w.raw("],");w.key("expected");w.weight(d.expected);w.raw(",");w.key("actual");w.weight(d.actual);
    w.raw(",");w.key("note");w.text(d.note.data());w.raw("}");
}
// Streams the whole save to sink through one small buffer. checksum receives
// the CRC-32 written in the file, so the adapter can verify its read-back.
bool writeSave(const State& s,uint32_t generation,SaveSink& sink,uint32_t& checksum){
    JsonWriter w(sink);
    w.raw("{");w.key("format");w.text("lps-companion");w.raw(",");w.key("version");w.number(SaveSchemaVersion);
    w.raw(",");w.key("generation");w.number(generation);w.raw(",\n");
    w.key("state");w.raw("{");w.key("language");w.text(LanguageNames[unsigned(s.language)]);
    w.raw(",");w.key("palette");w.text(PaletteNames[unsigned(s.palette)]);w.raw(",");w.key("xp");w.number(s.xp);
    w.raw(",\n");w.key("activities");w.raw("[");
    for(unsigned i=0;i<s.activityCount;++i){
        if(i)w.raw(",");
        w.raw("\n{");w.key("id");w.number(s.activities[i].id);
        w.raw(",");w.key("fr");w.text(s.activities[i].french.data());
        w.raw(",");w.key("en");w.text(s.activities[i].english.data());w.raw("}");
    }
    w.raw("],\n");w.key("today");writeDay(w,s,s.today);
    w.raw(",\n");w.key("history");w.raw("[");
    for(unsigned i=0;i<s.count;++i){
        if(i)w.raw(",");
        w.raw("\n");writeDay(w,s,s.history[(s.next+HistoryLength-s.count+i)%HistoryLength]);
    }
    w.raw("],\n");w.key("kanban");w.raw("[");
    for(unsigned i=0;i<s.taskCount;++i){
        if(i)w.raw(",");
        w.raw("\n{");w.key("status");w.text(StatusNames[unsigned(s.tasks[i].status)]);
        w.raw(",");w.key("name");w.text(s.tasks[i].name.data());w.raw("}");
    }
    w.raw("],\n");w.key("reminders");w.raw("[");
    for(unsigned i=0;i<s.reminderCount;++i){
        const Reminder& r=s.reminders[i];
        if(i)w.raw(",");
        w.raw("\n{");w.key("from");w.date(r.from);w.raw(",");w.key("days");w.number(r.duration);
        w.raw(",");w.key("text");w.text(r.text.data());w.raw("}");
    }
    w.raw("]},\n");
    checksum=w.checksum();
    char hex[12];std::snprintf(hex,sizeof hex,"%08lx",static_cast<unsigned long>(checksum));
    w.key("crc32");w.text(hex);w.raw("}\n");w.flush();
    return w.good();
}
enum class Token : uint8_t { End, Error, ObjectBegin, ObjectEnd, ArrayBegin, ArrayEnd, Colon, Comma, String, Number, True, False, Null };
// Pull tokenizer: each next() reads just enough bytes for one token from a
// 256-byte buffer. Strings land in a fixed 128-byte buffer (longer ones set
// overflow, which only matters if the value is needed). A running CRC-32
// covers every byte consumed.
class JsonReader {
    SaveSource& source;
    std::array<char,SaveIoBuffer> buffer{};
    std::size_t position=0, length=0, consumed=0;
    bool finished=false;
    uint32_t crc=0xffffffffu;
    int peek(){
        if(position==length){
            if(finished)return -1;
            const long n=source.read(buffer.data(),buffer.size());
            if(n<0)ioError=true;
            if(n<=0||std::size_t(n)>buffer.size()){finished=true;return -1;}
            length=std::size_t(n);position=0;
        }
        return static_cast<unsigned char>(buffer[position]);
    }
    int get(){
        const int c=peek();
        if(c>=0){++position;crc=crc32Update(crc,uint8_t(c));if(++consumed>SaveMaxBytes)tooLong=true;}
        return c;
    }
    void append(uint32_t c){if(textLength<SaveTextCapacity)text[textLength++]=char(c);else overflow=true;}
    void appendUtf8(uint32_t cp){
        if(cp<0x80)append(cp);
        else if(cp<0x800){append(0xc0|(cp>>6));append(0x80|(cp&0x3f));}
        else if(cp<0x10000){append(0xe0|(cp>>12));append(0x80|((cp>>6)&0x3f));append(0x80|(cp&0x3f));}
        else {append(0xf0|(cp>>18));append(0x80|((cp>>12)&0x3f));append(0x80|((cp>>6)&0x3f));append(0x80|(cp&0x3f));}
    }
    bool hex4(uint32_t& v){
        v=0;
        for(int i=0;i<4;++i){
            const int c=get();v<<=4;
            if(c>='0'&&c<='9')v|=uint32_t(c-'0');else if(c>='a'&&c<='f')v|=uint32_t(c-'a'+10);
            else if(c>='A'&&c<='F')v|=uint32_t(c-'A'+10);else return false;
        }
        return true;
    }
    Token string(){
        textLength=0;overflow=false;
        for(;;){
            int c=get();
            if(c<0x20){text[textLength]=0;return Token::Error;} // end of data or raw control character
            if(c=='"'){text[textLength]=0;return Token::String;}
            if(c!='\\'){append(uint32_t(c));continue;}
            c=get();
            switch(c){
            case '"':case '\\':case '/':append(uint32_t(c));break;
            case 'b':append(8);break;case 'f':append(12);break;case 'n':append(10);break;
            case 'r':append(13);break;case 't':append(9);break;
            case 'u':{
                uint32_t cp=0;if(!hex4(cp))return Token::Error;
                if(cp>=0xd800&&cp<=0xdbff){
                    uint32_t low=0;
                    if(get()!='\\'||get()!='u'||!hex4(low)||low<0xdc00||low>0xdfff)return Token::Error;
                    cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                }else if((cp>=0xdc00&&cp<=0xdfff)||cp==0)return Token::Error;
                appendUtf8(cp);break;
            }
            default:return Token::Error;
            }
        }
    }
    bool digit(int c)const{return c>='0'&&c<='9';}
    Token numberToken(int c){
        number=0;integer=true;
        const bool negative=c=='-';
        if(negative){if(!digit(peek()))return Token::Error;c=get();}
        for(;;){
            if(number<100000000000000)number=number*10+(c-'0');else integer=false;
            if(!digit(peek()))break;
            c=get();
        }
        if(peek()=='.'){get();integer=false;if(!digit(peek()))return Token::Error;while(digit(peek()))get();}
        if(peek()=='e'||peek()=='E'){
            get();integer=false;if(peek()=='+'||peek()=='-')get();
            if(!digit(peek()))return Token::Error;
            while(digit(peek()))get();
        }
        if(negative)number=-number;
        return Token::Number;
    }
    Token literal(const char* rest,Token token){for(;*rest;++rest)if(get()!=*rest)return Token::Error;return token;}
public:
    bool ioError=false, tooLong=false;
    std::array<char,SaveTextCapacity+1> text{};
    std::size_t textLength=0;
    bool overflow=false;
    int64_t number=0;
    bool integer=false;
    // CRC state before the current token, and before the last object key.
    uint32_t tokenCrc=0, keyCrc=0;
    explicit JsonReader(SaveSource& s):source(s){}
    Token next(){
        int c;
        do{tokenCrc=crc;c=get();}while(c==' '||c=='\t'||c=='\n'||c=='\r');
        if(tooLong)return Token::Error;
        switch(c){
        case -1:return ioError?Token::Error:Token::End;
        case '{':return Token::ObjectBegin;case '}':return Token::ObjectEnd;
        case '[':return Token::ArrayBegin;case ']':return Token::ArrayEnd;
        case ':':return Token::Colon;case ',':return Token::Comma;
        case '"':return string();
        case 't':return literal("rue",Token::True);
        case 'f':return literal("alse",Token::False);
        case 'n':return literal("ull",Token::Null);
        default:return c=='-'||digit(c)?numberToken(c):Token::Error;
        }
    }
    // Skip one whole value (unknown key). Nesting is checked up to 32 levels.
    bool skip(Token first){
        if(first==Token::String||first==Token::Number||first==Token::True||first==Token::False||first==Token::Null)return true;
        if(first!=Token::ObjectBegin&&first!=Token::ArrayBegin)return false;
        uint32_t objects=first==Token::ObjectBegin?1u:0u;unsigned depth=1;
        while(depth){
            const Token t=next();
            if(t==Token::Error||t==Token::End)return false;
            if(t==Token::ObjectBegin||t==Token::ArrayBegin){
                if(depth==32)return false;
                objects=(objects<<1)|(t==Token::ObjectBegin?1u:0u);++depth;
            }else if(t==Token::ObjectEnd||t==Token::ArrayEnd){
                if(((objects&1u)!=0)!=(t==Token::ObjectEnd))return false;
                objects>>=1;--depth;
            }
        }
        return true;
    }
};
// Calls member(key) for each member of an object whose '{' was just read.
// member must consume exactly one value. Keys longer than 23 bytes read as "".
template<class F> bool members(JsonReader& r,F&& member){
    Token t=r.next();
    if(t==Token::ObjectEnd)return true;
    for(;;){
        if(t!=Token::String)return false;
        char key[24]{};
        if(!r.overflow&&r.textLength<sizeof key)std::memcpy(key,r.text.data(),r.textLength);
        r.keyCrc=r.tokenCrc;
        if(r.next()!=Token::Colon||!member(static_cast<const char*>(key)))return false;
        t=r.next();
        if(t==Token::ObjectEnd)return true;
        if(t!=Token::Comma)return false;
        t=r.next();
    }
}
// Calls element(firstToken) for each element of an array whose '[' was just read.
template<class F> bool elements(JsonReader& r,F&& element){
    Token t=r.next();
    if(t==Token::ArrayEnd)return true;
    for(;;){
        if(!element(t))return false;
        t=r.next();
        if(t==Token::ArrayEnd)return true;
        if(t!=Token::Comma)return false;
        t=r.next();
    }
}
bool readNumber(JsonReader& r,int64_t min,int64_t max,int64_t& v){
    if(r.next()!=Token::Number||!r.integer||r.number<min||r.number>max)return false;
    v=r.number;return true;
}
// Stored text: no control characters; UTF-8 bytes are kept as they are.
template<std::size_t N> bool readText(JsonReader& r,std::array<char,N>& out,bool required){
    if(r.next()!=Token::String||r.overflow||r.textLength>=N||(required&&!r.textLength))return false;
    for(std::size_t i=0;i<r.textLength;++i){const unsigned char c=static_cast<unsigned char>(r.text[i]);if(c<32||c==127)return false;}
    out.fill(0);std::memcpy(out.data(),r.text.data(),r.textLength);return true;
}
bool readDate(JsonReader& r,Date& d,bool allowNull){
    const Token t=r.next();
    if(t==Token::Null&&allowNull){d={};return true;}
    return t==Token::String&&!r.overflow&&parseDate(r.text.data(),d);
}
bool readWeight(JsonReader& r,int32_t& grams){
    const Token t=r.next();
    if(t==Token::Null){grams=-1;return true;}
    if(t!=Token::Number||!r.integer||r.number<1||r.number>999999)return false;
    grams=int32_t(r.number);return true;
}
bool readChoice(JsonReader& r,const char* const* names,unsigned count,unsigned& out){
    if(r.next()!=Token::String||r.overflow)return false;
    for(unsigned i=0;i<count;++i)if(!std::strcmp(r.text.data(),names[i])){out=i;return true;}
    return false;
}
bool readDay(JsonReader& r,Token first,Day& d){
    if(first!=Token::ObjectBegin)return false;
    d=Day{};d.date={};
    return members(r,[&](const char* key){
        if(!std::strcmp(key,"date"))return readDate(r,d.date,true);
        if(!std::strcmp(key,"number")){int64_t v=0;if(!readNumber(r,1,UINT32_MAX,v))return false;d.number=uint32_t(v);return true;}
        if(!std::strcmp(key,"activities")){
            if(r.next()!=Token::ArrayBegin)return false;
            d.flags=0;d.namedCount=0;
            return elements(r,[&](Token t){
                if(t!=Token::String||r.overflow||r.textLength>ActivityNameLength||d.namedCount>=ActivityCapacity)return false;
                copyActivityName(d.names[d.namedCount],r.text.data());++d.namedCount;return true;
            });
        }
        if(!std::strcmp(key,"expected"))return readWeight(r,d.expected);
        if(!std::strcmp(key,"actual"))return readWeight(r,d.actual);
        if(!std::strcmp(key,"note"))return readText(r,d.note,false);
        return r.skip(r.next());
    });
}
void resetState(State& s){
    s.language=Language::English;s.palette=Palette::Green;s.xp=0;s.today=Day{};
    for(Day& d:s.history)d=Day{};
    s.next=0;s.count=0;s.taskCount=0;
    for(Task& t:s.tasks)t=Task{};
    s.reminderCount=0;
    for(Reminder& r:s.reminders)r=Reminder{};
    fillDefaultActivities(s);
}
bool readState(JsonReader& r,State& s){
    if(r.next()!=Token::ObjectBegin)return false;
    if(!members(r,[&](const char* key){
        unsigned choice=0;
        if(!std::strcmp(key,"language")){if(!readChoice(r,LanguageNames,2,choice))return false;s.language=Language(choice);return true;}
        if(!std::strcmp(key,"palette")){if(!readChoice(r,PaletteNames,5,choice))return false;s.palette=Palette(choice);return true;}
        if(!std::strcmp(key,"xp")){int64_t v=0;if(!readNumber(r,0,UINT32_MAX,v))return false;s.xp=uint32_t(v);return true;}
        if(!std::strcmp(key,"activities")){
            if(r.next()!=Token::ArrayBegin)return false;
            for(Activity& a:s.activities)a=Activity{};
            s.activityCount=0;s.nextActivityId=1;
            return elements(r,[&](Token t){
                if(t!=Token::ObjectBegin||s.activityCount>=ActivityCapacity)return false;
                Activity& activity=s.activities[s.activityCount];activity=Activity{};
                const bool ok=members(r,[&](const char* field){
                    int64_t v=0;
                    if(!std::strcmp(field,"id")){if(!readNumber(r,1,65535,v))return false;activity.id=uint16_t(v);return true;}
                    if(!std::strcmp(field,"fr"))return readText(r,activity.french,false);
                    if(!std::strcmp(field,"en"))return readText(r,activity.english,false);
                    return r.skip(r.next());
                });
                if(!ok||(!activity.french[0]&&!activity.english[0]))return false;
                if(displayWidth(activity.french.data())>ActivityNameLength||displayWidth(activity.english.data())>ActivityNameLength)return false;
                if(!activity.id)activity.id=s.nextActivityId;
                for(unsigned i=0;i<s.activityCount;++i)if(s.activities[i].id==activity.id)return false;
                if(activity.id>=s.nextActivityId)s.nextActivityId=uint16_t(activity.id+1);
                ++s.activityCount;return true;
            });
        }
        if(!std::strcmp(key,"today"))return readDay(r,r.next(),s.today);
        if(!std::strcmp(key,"history")){
            if(r.next()!=Token::ArrayBegin)return false;
            s.count=0;
            const bool ok=elements(r,[&](Token t){
                if(s.count>=HistoryLength||!readDay(r,t,s.history[s.count]))return false;
                ++s.count;return true;
            });
            s.next=uint8_t(s.count%HistoryLength);return ok;
        }
        if(!std::strcmp(key,"kanban")){
            if(r.next()!=Token::ArrayBegin)return false;
            s.taskCount=0;
            return elements(r,[&](Token t){
                if(t!=Token::ObjectBegin||s.taskCount>=TaskCapacity)return false;
                Task& task=s.tasks[s.taskCount];task=Task{};bool named=false;
                const bool ok=members(r,[&](const char* field){
                    unsigned status=0;
                    if(!std::strcmp(field,"status")){if(!readChoice(r,StatusNames,3,status))return false;task.status=TaskStatus(status);return true;}
                    if(!std::strcmp(field,"name"))return named=readText(r,task.name,true);
                    return r.skip(r.next());
                });
                if(!ok||!named)return false;
                ++s.taskCount;return true;
            });
        }
        if(!std::strcmp(key,"reminders")){
            if(r.next()!=Token::ArrayBegin)return false;
            s.reminderCount=0;
            return elements(r,[&](Token t){
                if(t!=Token::ObjectBegin||s.reminderCount>=ReminderCapacity)return false;
                Reminder& reminder=s.reminders[s.reminderCount];reminder=Reminder{};bool dated=false,texted=false;
                const bool ok=members(r,[&](const char* field){
                    if(!std::strcmp(field,"from"))return dated=readDate(r,reminder.from,false);
                    if(!std::strcmp(field,"days")){int64_t v=0;if(!readNumber(r,1,365,v))return false;reminder.duration=uint16_t(v);return true;}
                    if(!std::strcmp(field,"text"))return texted=readText(r,reminder.text,true);
                    return r.skip(r.next());
                });
                if(!ok||!dated||!texted)return false;
                ++s.reminderCount;return true;
            });
        }
        return r.skip(r.next());
    }))return false;
    resolveAllDayFlags(s);return true;
}
// Streams one save file into out (reset first; partial data on failure).
SaveCheck readSave(SaveSource& source,State& out,SaveInfo& info){
    resetState(out);info=SaveInfo{};
    JsonReader r(source);
    bool formatOk=false,versionSeen=false,unsupported=false,generationSeen=false,stateSeen=false,crcSeen=false;
    uint32_t stored=0,computed=0;
    const bool parsed=r.next()==Token::ObjectBegin&&members(r,[&](const char* key){
        if(crcSeen)return false; // crc32 must be the last member: nothing after it is unchecked.
        int64_t v=0;
        if(!std::strcmp(key,"format")){
            if(r.next()!=Token::String)return false;
            formatOk=!r.overflow&&!std::strcmp(r.text.data(),"lps-companion");return formatOk;
        }
        if(!std::strcmp(key,"version")){
            if(!readNumber(r,1,UINT32_MAX,v))return false;
            versionSeen=true;unsupported=uint64_t(v)>SaveSchemaVersion;return true;
        }
        if(!std::strcmp(key,"generation")){
            if(!readNumber(r,0,UINT32_MAX,v))return false;
            generationSeen=true;info.generation=uint32_t(v);return true;
        }
        if(!std::strcmp(key,"state")){
            if(!formatOk||!versionSeen||stateSeen)return false;
            stateSeen=true;
            return unsupported?r.skip(r.next()):readState(r,out);
        }
        if(!std::strcmp(key,"crc32")){
            computed=~r.keyCrc;
            if(r.next()!=Token::String||r.textLength!=8)return false;
            for(unsigned i=0;i<8;++i){
                const char c=r.text[i];stored<<=4;
                if(c>='0'&&c<='9')stored|=uint32_t(c-'0');else if(c>='a'&&c<='f')stored|=uint32_t(c-'a'+10);else return false;
            }
            crcSeen=true;return true;
        }
        return r.skip(r.next());
    })&&r.next()==Token::End;
    if(r.ioError)return SaveCheck::IOError;
    if(!parsed||!crcSeen||stored!=computed||!formatOk||!versionSeen||!generationSeen||!stateSeen)return SaveCheck::Damaged;
    info.checksum=stored;
    return unsupported?SaveCheck::Unsupported:SaveCheck::Valid;
}
enum class Screen { TimerSelect, TimerRunning, TimerComplete, Home, ReminderPopup, Reminders, ReminderEdit, Date, Activities, ActivityEdit, Weight, Kanban, TaskEdit, DailyReview, Finish, Reward, Config, Language, Colors,
                    RosaryMode, RosarySet, RosaryDecade, RosaryPrayer, RosaryComplete };
class App {
    Platform& hw;
    State state{};
    // Mutation scratch: stage() copies state here, handlers edit the copy,
    // commit() saves it then copies it back. Distinct from PicoCalc::scratch
    // (load parse / save verify). stage() is not re-entered: commit() and
    // Platform::save do not call stage().
    State draft{};
    Screen screen=Screen::Home;
    unsigned selection=0,page=0,field=0,daysBack=0;
    bool blocked=false;
    Language pendingLanguage=Language::English;
    Palette pendingPalette=Palette::Green;
    std::array<char,11> dateDraft{};
    std::array<char,TaskNameLength+1> taskDraft{};
    std::array<std::array<char,8>,2> weights{};
    std::array<char,100> message{};
    bool missingStorage=false;
    TaskStatus kanbanColumn=TaskStatus::Todo;
    unsigned reviewOffset=0;
    unsigned editingReminder=ReminderCapacity;
    unsigned reminderField=0;
    std::array<char,11> reminderDate{};
    std::array<char,4> reminderDuration{};
    std::array<char,ReminderTextLength+1> reminderText{};
    unsigned editedTask=TaskCapacity;
    unsigned editingActivity=ActivityCapacity;
    std::array<char,ActivityNameLength+1> activityDraft{};
    bool confirmingDelete=false;
    RosaryMode rosaryMode=RosaryMode::Decade;
    MysterySet rosarySet=MysterySet::Joyful;
    unsigned rosaryDecade=0,rosaryStep=0,rosaryPage=0;
    uint64_t timerNow=0,timerLast=0,timerRemaining=0;
    bool timerPaused=false;
    unsigned timerSeconds()const{return unsigned((timerRemaining+999)/1000);}
    void startTimer(unsigned option){constexpr unsigned minutes[]={5,30,60};timerRemaining=uint64_t(minutes[option])*60000;timerLast=timerNow;timerPaused=false;go(Screen::TimerRunning);}
    unsigned extraCount(const Day& d)const{
        if(!daysBack)return 0;
        unsigned n=0;for(unsigned i=0;i<d.namedCount;++i)if(indexByStoredName(state,d.names[i].data())>=state.activityCount)++n;
        return n;
    }
    unsigned visibleActivities()const{return state.activityCount+extraCount(viewedDay());}
    unsigned currentPages()const{return activityPages(visibleActivities());}
    const char* extraLabel(const Day& d,unsigned extraIndex)const{
        unsigned seen=0;
        for(unsigned i=0;i<d.namedCount;++i)if(indexByStoredName(state,d.names[i].data())>=state.activityCount){
            if(seen==extraIndex)return d.names[i].data();++seen;
        }
        return "";
    }
    void beginActivityEdit(unsigned index){
        editingActivity=index;activityDraft.fill(0);
        if(index<state.activityCount)copyActivityName(activityDraft,displayName(state.activities[index],state.language));
        screen=Screen::ActivityEdit;message.fill(0);
    }
    void clampActivityCursor(){
        const unsigned n=visibleActivities(),pages=currentPages();
        if(page>=pages)page=pages-1;
        const unsigned remaining=n>page*PageSize?n-page*PageSize:0;
        const unsigned rows=remaining<PageSize?remaining:PageSize;
        if(rows&&selection>=rows)selection=rows-1;
        if(!rows)selection=0;
    }
    const char* tr(const char* en,const char* fr)const{return state.language==Language::French?fr:en;}
    Palette displayedPalette()const{return screen==Screen::Colors?pendingPalette:state.palette;}
    void say(const char* text){std::snprintf(message.data(),message.size(),"%s",text);}
    void readOnly(){say(tr("Archived day: read-only","Jour archivé : lecture seule"));}
    enum class RosaryPart { Sign, Creed, OpeningOurFather, OpeningHail, Glory, Mystery, Scripture, Meditation, Silence,
                            OurFather, Hail, Fatima, HailHolyQueen, Closing, EndSign };
    struct RosaryView { RosaryPart part=RosaryPart::Sign;unsigned decade=0,count=0; };
    static unsigned weekday(Date d){
        // Gregorian calendar, 0=Sunday. Dates in LPS are restricted to 2000-2099.
        static constexpr unsigned offsets[]={0,3,2,5,0,3,5,1,4,6,2,4};
        unsigned y=d.year;if(d.month<3)--y;
        return (y+y/4-y/100+y/400+offsets[d.month-1]+d.day)%7;
    }
    static unsigned isoWeek(Date d){
        // ISO 8601: weeks start on Monday; week 1 contains the year's first Thursday.
        auto jan1=[](unsigned y){unsigned p=y-1;return (p+1+p/4-p/100+p/400)%7;}; // 0=Sunday
        auto weeks=[&](unsigned y){unsigned j=jan1(y);return j==4||(leap(uint16_t(y))&&j==3)?53u:52u;};
        unsigned ordinal=d.day;for(unsigned m=1;m<d.month;++m)ordinal+=daysInMonth(d.year,uint8_t(m));
        const unsigned iso=(weekday(d)+6)%7+1;const int week=(int(ordinal)-int(iso)+10)/7;
        if(week<1)return weeks(d.year-1u);
        return unsigned(week)>weeks(d.year)?1u:unsigned(week);
    }
    void longDateText(Date d,char* out,std::size_t n)const{
        static constexpr const char* enDays[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        static constexpr const char* frDays[]={"Dimanche","Lundi","Mardi","Mercredi","Jeudi","Vendredi","Samedi"};
        static constexpr const char* enMonths[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
        static constexpr const char* frMonths[]={"Janvier","Février","Mars","Avril","Mai","Juin","Juillet","Août","Septembre","Octobre","Novembre","Décembre"};
        const bool fr=state.language==Language::French;const unsigned w=weekday(d);
        std::snprintf(out,n,fr?"%s %u %s %u (semaine %u)":"%s %u %s %u (week %u)",fr?frDays[w]:enDays[w],unsigned(d.day),fr?frMonths[d.month-1]:enMonths[d.month-1],unsigned(d.year),isoWeek(d));
    }
    MysterySet suggestedMysteries()const{
        if(!validDate(state.today.date))return MysterySet::Joyful;
        switch(weekday(state.today.date)){
            case 1:case 6:return MysterySet::Joyful;
            case 2:case 5:return MysterySet::Sorrowful;
            case 4:return MysterySet::Luminous;
            default:return MysterySet::Glorious;
        }
    }
    const char* mysterySetName(MysterySet set)const{
        static constexpr const char* en[]={"Joyful","Luminous","Sorrowful","Glorious"};
        static constexpr const char* fr[]={"Joyeux","Lumineux","Douloureux","Glorieux"};
        return state.language==Language::French?fr[unsigned(set)]:en[unsigned(set)];
    }
    const char* mysteryName(MysterySet set,unsigned decade)const{
        static constexpr const char* en[4][5]={
            {"The Annunciation","The Visitation","The Nativity","The Presentation","Finding Jesus in the Temple"},
            {"The Baptism of Jesus","The Wedding at Cana","Proclamation of the Kingdom","The Transfiguration","Institution of the Eucharist"},
            {"The Agony in the Garden","The Scourging at the Pillar","The Crowning with Thorns","The Carrying of the Cross","The Crucifixion"},
            {"The Resurrection","The Ascension","The Descent of the Holy Spirit","The Assumption of Mary","The Coronation of Mary"}};
        static constexpr const char* fr[4][5]={
            {"L'Annonciation","La Visitation","La Nativité","La Présentation","Jésus retrouvé au Temple"},
            {"Le Baptême de Jésus","Les Noces de Cana","L'annonce du Royaume","La Transfiguration","L'institution de l'Eucharistie"},
            {"L'Agonie au Jardin","La Flagellation","Le Couronnement d'épines","Le Portement de la Croix","La Crucifixion"},
            {"La Résurrection","L'Ascension","La Pentecôte","L'Assomption de Marie","Le Couronnement de Marie"}};
        return state.language==Language::French?fr[unsigned(set)][decade]:en[unsigned(set)][decade];
    }
    const char* scripture(MysterySet set,unsigned decade)const{
        static constexpr const char* en[4][5]={
            {"Luke 1:38 - Let it be to me according to your word.","Luke 1:42 - Blessed are you among women.","Luke 2:7 - She gave birth to her firstborn son.","Luke 2:30 - My eyes have seen your salvation.","Luke 2:49 - I must be in my Father's house."},
            {"Matthew 3:17 - This is my beloved Son.","John 2:5 - Do whatever he tells you.","Mark 1:15 - Repent and believe in the Gospel.","Matthew 17:2 - His face shone like the sun.","Luke 22:19 - This is my body, given for you."},
            {"Luke 22:42 - Not my will, but yours be done.","John 19:1 - Pilate took Jesus and had him scourged.","Matthew 27:29 - They placed a crown of thorns on his head.","Luke 23:26 - They laid the cross on Simon.","Luke 23:46 - Father, into your hands I commend my spirit."},
            {"Matthew 28:6 - He is not here; he has risen.","Acts 1:9 - He was lifted up before their eyes.","Acts 2:4 - They were all filled with the Holy Spirit.","Luke 1:49 - The Almighty has done great things for me.","Revelation 12:1 - A woman clothed with the sun."}};
        static constexpr const char* fr[4][5]={
            {"Luc 1,38 - Qu'il me soit fait selon ta parole.","Luc 1,42 - Tu es bénie entre les femmes.","Luc 2,7 - Elle mit au monde son fils premier-né.","Luc 2,30 - Mes yeux ont vu ton salut.","Luc 2,49 - Je dois être chez mon Père."},
            {"Matthieu 3,17 - Celui-ci est mon Fils bien-aimé.","Jean 2,5 - Faites tout ce qu'il vous dira.","Marc 1,15 - Convertissez-vous et croyez à l'Évangile.","Matthieu 17,2 - Son visage devint brillant comme le soleil.","Luc 22,19 - Ceci est mon corps donné pour vous."},
            {"Luc 22,42 - Non pas ma volonté, mais la tienne.","Jean 19,1 - Pilate fit flageller Jésus.","Matthieu 27,29 - Ils posèrent sur sa tête une couronne d'épines.","Luc 23,26 - Ils chargèrent Simon de la croix.","Luc 23,46 - Père, entre tes mains je remets mon esprit."},
            {"Matthieu 28,6 - Il n'est pas ici, il est ressuscité.","Actes 1,9 - Il s'éleva sous leurs yeux.","Actes 2,4 - Tous furent remplis de l'Esprit Saint.","Luc 1,49 - Le Puissant fit pour moi des merveilles.","Apocalypse 12,1 - Une femme vêtue du soleil."}};
        return state.language==Language::French?fr[unsigned(set)][decade]:en[unsigned(set)][decade];
    }
    const char* meditation(MysterySet set,unsigned decade)const{
        static constexpr const char* en[4][5]={
            {"Ask for Mary's trust: receive God's call without fear.","Carry Christ toward another person with humble joy.","Welcome Jesus in poverty, simplicity and gratitude.","Offer to God what is most precious, without possessing it.","Seek Christ patiently whenever he seems absent."},
            {"Remember your baptism and live today as a beloved child of God.","Entrust the ordinary needs of life to Christ and obey him.","Let Christ rule first within your own heart.","Ask to see Christ's light behind present difficulties.","Receive Christ's self-gift and learn to give yourself."},
            {"Bring anguish to the Father and choose his will in trust.","Pray for endurance when suffering is undeserved.","Reject pride and contemplate the quiet kingship of Christ.","Carry today's burden beside Jesus, one step at a time.","Remain at the Cross and receive the mercy flowing from it."},
            {"Let the risen Christ awaken hope where life seems closed.","Lift your heart toward heaven while serving faithfully on earth.","Ask the Holy Spirit for courage, wisdom and charity.","Entrust your whole life to God as Mary did.","Contemplate the dignity promised to a life united with God."}};
        static constexpr const char* fr[4][5]={
            {"Demandez la confiance de Marie : accueillir l'appel de Dieu sans peur.","Portez le Christ vers autrui avec une joie humble.","Accueillez Jésus dans la pauvreté, la simplicité et la gratitude.","Offrez à Dieu ce qui est précieux sans chercher à le posséder.","Cherchez patiemment le Christ lorsqu'il semble absent."},
            {"Souvenez-vous de votre baptême et vivez en enfant aimé de Dieu.","Confiez au Christ les besoins ordinaires et faites ce qu'il dit.","Laissez d'abord le Christ régner dans votre propre coeur.","Demandez à voir la lumière du Christ derrière les difficultés.","Recevez le don du Christ et apprenez à vous donner."},
            {"Portez votre angoisse au Père et choisissez sa volonté avec confiance.","Demandez la force d'endurer une souffrance imméritée.","Rejetez l'orgueil et contemplez la royauté silencieuse du Christ.","Portez le fardeau du jour auprès de Jésus, pas après pas.","Demeurez au pied de la Croix et recevez sa miséricorde."},
            {"Que le Christ ressuscité réveille l'espérance là où tout semble fermé.","Élevez votre coeur vers le ciel en servant fidèlement sur terre.","Demandez à l'Esprit Saint courage, sagesse et charité.","Confiez toute votre vie à Dieu comme Marie.","Contemplez la dignité promise à une vie unie à Dieu."}};
        return state.language==Language::French?fr[unsigned(set)][decade]:en[unsigned(set)][decade];
    }
    RosaryView rosaryView(unsigned step)const{
        if(rosaryMode==RosaryMode::Decade){
            if(step==0)return {RosaryPart::Sign,rosaryDecade,0};
            if(step==1)return {RosaryPart::Mystery,rosaryDecade,0};
            if(step==2)return {RosaryPart::OurFather,rosaryDecade,0};
            if(step<13)return {RosaryPart::Hail,rosaryDecade,step-2};
            if(step==13)return {RosaryPart::Glory,rosaryDecade,0};
            if(step==14)return {RosaryPart::Fatima,rosaryDecade,0};
            if(step==15)return {RosaryPart::Closing,rosaryDecade,0};
            return {RosaryPart::EndSign,rosaryDecade,0};
        }
        if(step==0)return {RosaryPart::Sign,0,0};
        if(step==1)return {RosaryPart::Creed,0,0};
        if(step==2)return {RosaryPart::OpeningOurFather,0,0};
        if(step>=3&&step<=5)return {RosaryPart::OpeningHail,0,step-2};
        if(step==6)return {RosaryPart::Glory,0,0};
        unsigned local=step-7;
        const unsigned extras=rosaryMode==RosaryMode::Full?0:rosaryMode==RosaryMode::Guided?2:3;
        const unsigned block=14+extras;
        if(local<5*block){
            const unsigned decade=local/block,pos=local%block;
            if(pos==0)return {RosaryPart::Mystery,decade,0};
            unsigned p=pos-1;
            if(extras){if(p==0)return {RosaryPart::Scripture,decade,0};if(p==1)return {RosaryPart::Meditation,decade,0};if(extras==3&&p==2)return {RosaryPart::Silence,decade,0};p-=extras;}
            if(p==0)return {RosaryPart::OurFather,decade,0};
            if(p<=10)return {RosaryPart::Hail,decade,p};
            if(p==11)return {RosaryPart::Glory,decade,0};
            return {RosaryPart::Fatima,decade,0};
        }
        local-=5*block;
        if(local==0)return {RosaryPart::HailHolyQueen,4,0};
        if(local==1)return {RosaryPart::Closing,4,0};
        return {RosaryPart::EndSign,4,0};
    }
    unsigned rosarySteps()const{
        if(rosaryMode==RosaryMode::Decade)return 17;
        const unsigned extras=rosaryMode==RosaryMode::Full?0:rosaryMode==RosaryMode::Guided?2:3;
        return 7+5*(14+extras)+3;
    }
    const char* rosaryText(RosaryView v)const{
        switch(v.part){
            case RosaryPart::Sign:return tr("In the name of the Father, and of the Son, and of the Holy Spirit. Amen.","Au nom du Père, du Fils et du Saint-Esprit. Amen.");
            case RosaryPart::Creed:return tr("I believe in God, the Father almighty, Creator of heaven and earth, and in Jesus Christ, his only Son, our Lord, who was conceived by the Holy Spirit, born of the Virgin Mary, suffered under Pontius Pilate, was crucified, died and was buried; he descended into hell; on the third day he rose again from the dead; he ascended into heaven, and is seated at the right hand of God the Father almighty; from there he will come to judge the living and the dead. I believe in the Holy Spirit, the holy catholic Church, the communion of saints, the forgiveness of sins, the resurrection of the body, and life everlasting. Amen.","Je crois en Dieu, le Père tout-puissant, créateur du ciel et de la terre. Et en Jésus-Christ, son Fils unique, notre Seigneur, qui a été conçu du Saint-Esprit, est né de la Vierge Marie, a souffert sous Ponce Pilate, a été crucifié, est mort et a été enseveli, est descendu aux enfers. Le troisième jour est ressuscité des morts, est monté aux cieux, est assis à la droite de Dieu le Père tout-puissant, d'où il viendra juger les vivants et les morts. Je crois en l'Esprit Saint, à la sainte Église catholique, à la communion des saints, à la rémission des péchés, à la résurrection de la chair, à la vie éternelle. Amen.");
            case RosaryPart::OpeningOurFather:case RosaryPart::OurFather:return tr("Our Father, who art in heaven, hallowed be thy name; thy kingdom come; thy will be done on earth as it is in heaven. Give us this day our daily bread; and forgive us our trespasses, as we forgive those who trespass against us; and lead us not into temptation, but deliver us from evil. Amen.","Notre Père, qui es aux cieux, que ton nom soit sanctifié, que ton règne vienne, que ta volonté soit faite sur la terre comme au ciel. Donne-nous aujourd'hui notre pain de ce jour. Pardonne-nous nos offenses, comme nous pardonnons aussi à ceux qui nous ont offensés. Et ne nous laisse pas entrer en tentation, mais délivre-nous du Mal. Amen.");
            case RosaryPart::OpeningHail:case RosaryPart::Hail:return tr("Hail Mary, full of grace, the Lord is with thee. Blessed art thou among women, and blessed is the fruit of thy womb, Jesus. Holy Mary, Mother of God, pray for us sinners, now and at the hour of our death. Amen.","Je vous salue, Marie, pleine de grâce ; le Seigneur est avec vous. Vous êtes bénie entre toutes les femmes, et Jésus, le fruit de vos entrailles, est béni. Sainte Marie, Mère de Dieu, priez pour nous pauvres pécheurs, maintenant et à l'heure de notre mort. Amen.");
            case RosaryPart::Glory:return tr("Glory be to the Father, and to the Son, and to the Holy Spirit, as it was in the beginning, is now, and ever shall be, world without end. Amen.","Gloire au Père, au Fils et au Saint-Esprit, comme il était au commencement, maintenant et toujours, pour les siècles des siècles. Amen.");
            case RosaryPart::Mystery:return mysteryName(rosarySet,v.decade);
            case RosaryPart::Scripture:return scripture(rosarySet,v.decade);
            case RosaryPart::Meditation:return meditation(rosarySet,v.decade);
            case RosaryPart::Silence:return tr("Remain in silence. Place this mystery, your intentions and your whole attention before God. Press Enter when you are ready to continue.","Demeurez en silence. Placez ce mystère, vos intentions et toute votre attention devant Dieu. Appuyez sur Entrée lorsque vous êtes prêt à continuer.");
            case RosaryPart::Fatima:return tr("O my Jesus, forgive us our sins, save us from the fires of hell, lead all souls to heaven, especially those most in need of thy mercy. Amen.","Ô mon Jésus, pardonnez-nous nos péchés, préservez-nous du feu de l'enfer et conduisez au ciel toutes les âmes, surtout celles qui ont le plus besoin de votre miséricorde. Amen.");
            case RosaryPart::HailHolyQueen:return tr("Hail, holy Queen, Mother of mercy, our life, our sweetness and our hope. To thee do we cry, poor banished children of Eve. To thee do we send up our sighs, mourning and weeping in this valley of tears. Turn then, most gracious advocate, thine eyes of mercy toward us, and after this our exile show unto us the blessed fruit of thy womb, Jesus. O clement, O loving, O sweet Virgin Mary. Pray for us, O holy Mother of God, that we may be made worthy of the promises of Christ.","Salut, ô Reine, Mère de miséricorde, notre vie, notre douceur et notre espérance, salut. Enfants d'Ève exilés, nous crions vers vous. Vers vous nous soupirons, gémissant et pleurant dans cette vallée de larmes. Ô vous, notre avocate, tournez vers nous vos regards miséricordieux. Et après cet exil, montrez-nous Jésus, le fruit béni de vos entrailles. Ô clémente, ô miséricordieuse, ô douce Vierge Marie. Priez pour nous, sainte Mère de Dieu, afin que nous devenions dignes des promesses du Christ.");
            case RosaryPart::Closing:return tr("O God, whose only-begotten Son, by his life, death and resurrection, has purchased for us the rewards of eternal life: grant that, meditating upon these mysteries of the holy Rosary, we may imitate what they contain and obtain what they promise, through Christ our Lord. Amen.","Ô Dieu, dont le Fils unique, par sa vie, sa mort et sa résurrection, nous a acquis les récompenses de la vie éternelle, accordez-nous, en méditant ces mystères du très saint Rosaire, d'imiter ce qu'ils contiennent et d'obtenir ce qu'ils promettent, par le Christ notre Seigneur. Amen.");
            case RosaryPart::EndSign:return tr("In the name of the Father, and of the Son, and of the Holy Spirit. Amen.","Au nom du Père, du Fils et du Saint-Esprit. Amen.");
        }
        return "";
    }
    const char* rosaryPartName(RosaryView v)const{
        switch(v.part){
            case RosaryPart::Sign:case RosaryPart::EndSign:return tr("SIGN OF THE CROSS","SIGNE DE CROIX");
            case RosaryPart::Creed:return tr("APOSTLES' CREED","SYMBOLE DES APÔTRES");
            case RosaryPart::OpeningOurFather:case RosaryPart::OurFather:return tr("OUR FATHER","NOTRE PÈRE");
            case RosaryPart::OpeningHail:case RosaryPart::Hail:return tr("HAIL MARY","JE VOUS SALUE MARIE");
            case RosaryPart::Glory:return tr("GLORY BE","GLOIRE AU PÈRE");
            case RosaryPart::Mystery:return tr("MYSTERY","MYSTÈRE");
            case RosaryPart::Scripture:return tr("SCRIPTURE","ÉCRITURE");
            case RosaryPart::Meditation:return tr("MEDITATION","MÉDITATION");
            case RosaryPart::Silence:return tr("CONTEMPLATIVE SILENCE","SILENCE CONTEMPLATIF");
            case RosaryPart::Fatima:return tr("FATIMA PRAYER","PRIÈRE DE FATIMA");
            case RosaryPart::HailHolyQueen:return tr("HAIL, HOLY QUEEN","SALUT, Ô REINE");
            case RosaryPart::Closing:return tr("CLOSING PRAYER","PRIÈRE FINALE");
        }
        return "";
    }
    static unsigned displayCells(const char* text){unsigned n=0;while(*text){unsigned c=static_cast<unsigned char>(*text++);if((c==0xc2||c==0xc3)&&*text)++text;++n;}return n;}
    static void cellSlice(const char* text,unsigned first,unsigned count,char* out,std::size_t capacity){
        while(*text&&first){unsigned c=static_cast<unsigned char>(*text++);if((c==0xc2||c==0xc3)&&*text)++text;--first;}
        std::size_t n=0;while(*text&&count&&n+2<capacity){unsigned c=static_cast<unsigned char>(*text++);out[n++]=char(c);if((c==0xc2||c==0xc3)&&*text)out[n++]=*text++;--count;}out[n]=0;
    }
    void line(int row,const char* text,bool reverse=false){
        char cells[100];displayText(text,cells,sizeof cells);hw.text(0,row*16,cells,reverse);
        constexpr const char* keys[]={"Enter","Esc","Delete","Del","Backspace","Up","Down","Left","Right",
            "Entrée","Échap","Suppr","Haut","Bas","F1","F2","F3","F4","F5","N","E","<-","->","1-9","1-9,0"};
        for(unsigned col=0;cells[col]&&col<40;){
            bool matched=false;
            for(const char* key:keys){char latin[24];displayText(key,latin,sizeof latin);const unsigned len=unsigned(std::strlen(latin));
                if(std::strncmp(cells+col,latin,len)==0&&(!col||!((cells[col-1]>='A'&&cells[col-1]<='Z')||(cells[col-1]>='a'&&cells[col-1]<='z')))
                    &&!(cells[col+len]>='a'&&cells[col+len]<='z')){
                    hw.emphasize(int(col*8),row*16,int(len));col+=len;matched=true;break;
                }
            }
            if(!matched)++col;
        }
    }
    void wrap(int row,const char* text,unsigned limit){
        for(unsigned i=0;*text&&i<limit;++i){char b[41]{};std::size_t n=std::strlen(text);if(n>40)n=40;std::memcpy(b,text,n);line(row+int(i),b);text+=n;}
    }
    State& stage(){draft=state;return draft;}
    bool commit(const State& candidate){
        if(daysBack){readOnly();return false;}
        if(blocked){say(tr("Unreadable save: writes blocked","Sauvegarde illisible : écriture bloquée"));return false;}
        if(!hw.save(candidate)){say(tr("Save failed: restart device","Échec sauvegarde : redémarrer"));return false;}
        state=candidate;return true;
    }
    unsigned tasksIn(TaskStatus status)const{
        unsigned n=0;for(unsigned i=0;i<state.taskCount;++i)if(state.tasks[i].status==status)++n;return n;
    }
    unsigned taskAt(TaskStatus status,unsigned position)const{
        for(unsigned i=0;i<state.taskCount;++i)if(state.tasks[i].status==status&&position--==0)return i;
        return TaskCapacity;
    }
    unsigned taskPosition(TaskStatus status,unsigned index)const{
        unsigned position=0;for(unsigned i=0;i<index&&i<state.taskCount;++i)if(state.tasks[i].status==status)++position;return position;
    }
    const char* taskStatusName(TaskStatus status)const{
        constexpr const char* en[]={"TODO","DOING","DONE"};constexpr const char* fr[]={"À FAIRE","EN COURS","FINI"};
        return state.language==Language::French?fr[unsigned(status)]:en[unsigned(status)];
    }
    unsigned reviewRows()const{
        return 3+count(state.today.flags)+tasksIn(TaskStatus::Todo)+tasksIn(TaskStatus::Doing)
            +(count(state.today.flags)==0)+(tasksIn(TaskStatus::Todo)==0)+(tasksIn(TaskStatus::Doing)==0);
    }
    void reviewLine(unsigned index,char* out,std::size_t size)const{
        const unsigned activities=count(state.today.flags);
        if(index==0){std::snprintf(out,size,tr("ACTIVITIES (%u)","ACTIVITÉS (%u)"),activities);return;}
        --index;
        for(unsigned i=0;i<state.activityCount;++i)if(state.today.flags&(ActivityFlags(1u)<<i)){
            if(index--==0){std::snprintf(out,size,"  %s",displayName(state.activities[i],state.language));return;}
        }
        if(!activities){if(index--==0){std::snprintf(out,size,"  %s",tr("None selected","Aucune sélectionnée"));return;}}
        for(TaskStatus status:{TaskStatus::Todo,TaskStatus::Doing}){
            const unsigned n=tasksIn(status);
            if(index--==0){std::snprintf(out,size,"%s (%u)",taskStatusName(status),n);return;}
            for(unsigned i=0;i<state.taskCount;++i)if(state.tasks[i].status==status){
                if(index--==0){std::snprintf(out,size,"  %s",state.tasks[i].name.data());return;}
            }
            if(!n&&index--==0){std::snprintf(out,size,"  %s",tr("None","Aucune"));return;}
        }
        out[0]=0;
    }
    static uint32_t dayIndex(Date d){
        uint32_t n=0;for(unsigned y=2000;y<d.year;++y)n+=leap(uint16_t(y))?366:365;
        for(unsigned m=1;m<d.month;++m)n+=daysInMonth(d.year,uint8_t(m));
        return n+d.day;
    }
    bool expired(const Reminder& r)const{
        return validDate(state.today.date)&&dayIndex(state.today.date)>=dayIndex(r.from)+r.duration;
    }
    bool active(const Reminder& r)const{
        return validDate(state.today.date)&&dayIndex(state.today.date)>=dayIndex(r.from)&&!expired(r);
    }
    unsigned activeReminders()const{unsigned n=0;for(unsigned i=0;i<state.reminderCount;++i)if(active(state.reminders[i]))++n;return n;}
    void pruneReminders(){
        State& next=stage();unsigned kept=0;
        for(unsigned i=0;i<state.reminderCount;++i)if(!expired(state.reminders[i]))next.reminders[kept++]=state.reminders[i];
        if(kept!=state.reminderCount){for(unsigned i=kept;i<ReminderCapacity;++i)next.reminders[i]=Reminder{};next.reminderCount=uint8_t(kept);commit(next);}
    }
    void beginReminderEdit(unsigned index){
        editingReminder=index;reminderField=0;reminderDate.fill(0);reminderDuration.fill(0);reminderText.fill(0);
        const Reminder r=index<state.reminderCount?state.reminders[index]:Reminder{state.today.date,1,{}};
        if(validDate(r.from))dateText(r.from,reminderDate.data(),reminderDate.size());
        std::snprintf(reminderDuration.data(),reminderDuration.size(),"%u",r.duration);
        reminderText=r.text;go(Screen::ReminderEdit);
    }
    void beginTaskEdit(unsigned index){
        editedTask=index;taskDraft.fill(0);if(index<TaskCapacity)taskDraft=state.tasks[index].name;go(Screen::TaskEdit);
    }
    void moveTaskTo(TaskStatus target){
        const unsigned n=tasksIn(kanbanColumn),index=n?taskAt(kanbanColumn,selection):TaskCapacity;
        if(index>=state.taskCount)return;
        if(state.tasks[index].status==target){say(tr("Task is already in this column","Tâche déjà dans cette colonne"));return;}
        State& next=stage();Task moved=next.tasks[index];moved.status=target;
        for(unsigned i=index;i+1<next.taskCount;++i)next.tasks[i]=next.tasks[i+1];
        next.tasks[next.taskCount-1]=moved;
        if(commit(next)){kanbanColumn=target;selection=tasksIn(target)-1;say(tr("Task moved","Tâche déplacée"));}
    }
    void reorderTask(bool downward){
        const unsigned n=tasksIn(kanbanColumn);if(n<2)return;
        std::array<unsigned,TaskCapacity> indices{};for(unsigned i=0;i<n;++i)indices[i]=taskAt(kanbanColumn,i);
        State& next=stage();unsigned destination=selection;
        if(!downward&&selection==0){
            const Task moved=next.tasks[indices[0]];for(unsigned i=0;i+1<n;++i)next.tasks[indices[i]]=next.tasks[indices[i+1]];
            next.tasks[indices[n-1]]=moved;destination=n-1;
        }else if(downward&&selection==n-1){
            const Task moved=next.tasks[indices[n-1]];for(unsigned i=n-1;i>0;--i)next.tasks[indices[i]]=next.tasks[indices[i-1]];
            next.tasks[indices[0]]=moved;destination=0;
        }else {
            destination=downward?selection+1:selection-1;const Task moved=next.tasks[indices[selection]];
            next.tasks[indices[selection]]=next.tasks[indices[destination]];next.tasks[indices[destination]]=moved;
        }
        if(commit(next))selection=destination;
    }
    void go(Screen s){screen=s;selection=0;message.fill(0);
        if(s==Screen::DailyReview)reviewOffset=0;
        if(s==Screen::Language)selection=unsigned(pendingLanguage);
        if(s==Screen::Colors)selection=unsigned(pendingPalette);
        if(s==Screen::Weight){field=0;for(unsigned i=0;i<2;++i){int32_t v=i?viewedDay().actual:viewedDay().expected;weights[i].fill(0);if(v>=0)weightText(v,weights[i].data(),weights[i].size());}}
        if(s==Screen::Date){dateDraft.fill(0);Date d=validDate(state.today.date)?state.today.date:DefaultDate;dateText(d,dateDraft.data(),dateDraft.size());}
        if(s==Screen::Activities)page=0; // LPS-113-CHG08b: always open on page 1, first row.
        confirmingDelete=false;
    }
    template<std::size_t N> void edit(std::array<char,N>& value,int key){
        std::size_t len=std::strlen(value.data());
        if(key==Backspace||key==127){if(len)value[len-1]=0;}
        else if(key>=32&&key<=126&&len<N-1){value[len]=char(key);value[len+1]=0;}
    }
public:
    explicit App(Platform& platform):hw(platform){}
    void tick(uint64_t milliseconds){
        timerNow=milliseconds;
        if(screen!=Screen::TimerRunning)return;
        const unsigned before=timerSeconds();
        if(!timerPaused){const uint64_t elapsed=timerNow-timerLast;timerRemaining=elapsed>=timerRemaining?0:timerRemaining-elapsed;}
        timerLast=timerNow;
        if(!timerRemaining){go(Screen::TimerComplete);render();}
        else if(before!=timerSeconds())render();
    }
    const State& data()const{return state;}
    Screen currentScreen()const{return screen;}
    const Day& viewedDay()const{
        // next is the slot AFTER the most recently closed day. Never index by
        // day number: physical slots wrap every 31 closures.
        return daysBack?state.history[(state.next+HistoryLength-daysBack)%HistoryLength]:state.today;
    }
    void start(){const auto r=hw.load(state);
        if(r!=LoadResult::Ok)resetState(state); // Never show a half-read save.
        // The language preference is on the SD card too, so a cold start with no
        // card deliberately uses the English default rather than guessing.
        if(r==LoadResult::NoStorage){blocked=true;missingStorage=true;say("[!] Missing SD card!");}
        else if(r==LoadResult::Error){blocked=true;say(tr("Read error: save protected","Erreur lecture : sauvegarde protégée"));}
        if(!blocked){pruneReminders();if(activeReminders())screen=Screen::ReminderPopup;}
        render();
    }
    void key(int k){
        if(k==Escape){
            if(screen==Screen::Activities&&confirmingDelete){confirmingDelete=false;message.fill(0);render();return;}
            if(screen==Screen::ActivityEdit){screen=Screen::Activities;activityDraft.fill(0);message.fill(0);render();return;}
            Screen target=Screen::Home;
            if(screen==Screen::Language||screen==Screen::Colors)target=Screen::Config;
            else if(screen==Screen::ReminderEdit)target=Screen::Reminders;
            else if(screen==Screen::TaskEdit)target=Screen::Kanban;
            else if(screen==Screen::RosarySet)target=Screen::RosaryMode;
            else if(screen==Screen::RosaryDecade)target=Screen::RosarySet;
            go(target);render();return;
        }
        message.fill(0);
        if(screen==Screen::ReminderPopup){go(Screen::Home);
        }else if(screen==Screen::Home){
            if(k==Left){
                if(daysBack<state.count){++daysBack;selection=0;page=0;}
                else say(state.count?tr("Oldest saved day","Plus ancien jour conservé"):tr("No archived days","Aucun jour archivé"));
            }
            if(k==Right){daysBack=0;selection=0;page=0;}
            const unsigned menuCount=daysBack?2:10;
            if(k==Up)selection=(selection+menuCount-1)%menuCount;
            if(k==Down)selection=(selection+1)%menuCount;
            if(!daysBack&&k=='0'){selection=0;k=Enter;}
            else if(daysBack&&k>='1'&&k<='2'){selection=unsigned(k-'1');k=Enter;}
            else if(!daysBack&&k>='1'&&k<='9'){selection=unsigned(k-'0');k=Enter;}
            if((k=='0'||(k>='3'&&k<='9'))&&daysBack)readOnly();
            if(k==Enter){
                constexpr Screen currentScreens[]={Screen::Date,Screen::Activities,Screen::Weight,Screen::Kanban,Screen::RosaryMode,Screen::DailyReview,Screen::Config,Screen::Reminders,Screen::TimerSelect,Screen::Finish};
                constexpr Screen archiveScreens[]={Screen::Activities,Screen::Weight};
                const Screen target=daysBack?archiveScreens[selection]:currentScreens[selection];
                if(target==Screen::Kanban)kanbanColumn=TaskStatus::Todo;
                if(target==Screen::Reminders)pruneReminders();
                if(target==Screen::Config){pendingLanguage=state.language;pendingPalette=state.palette;}
                go(target);}
        }else if(screen==Screen::TimerSelect){
            if(k==Up)selection=(selection+2)%3;
            if(k==Down)selection=(selection+1)%3;
            if(k>='1'&&k<='3'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter)startTimer(selection);
        }else if(screen==Screen::TimerRunning){
            if(k==Enter){timerPaused=!timerPaused;timerLast=timerNow;}
        }else if(screen==Screen::TimerComplete){
            if(k>='1'&&k<='3')startTimer(unsigned(k-'1'));
            else if(k==Enter)go(Screen::TimerSelect);
        }else if(screen==Screen::Reminders){
            if(k==Up&&state.reminderCount)selection=(selection+state.reminderCount-1)%state.reminderCount;
            else if(k==Down&&state.reminderCount)selection=(selection+1)%state.reminderCount;
            else if(k=='n'||k=='N'){
                if(state.reminderCount>=ReminderCapacity)say(tr("Limit: 3 reminders","Limite : 3 rappels"));
                else beginReminderEdit(ReminderCapacity);
            }else if(k==Enter&&state.reminderCount)beginReminderEdit(selection);
            else if(k==DeleteKey&&state.reminderCount){State& next=stage();
                for(unsigned i=selection;i+1<next.reminderCount;++i)next.reminders[i]=next.reminders[i+1];
                next.reminders[--next.reminderCount]=Reminder{};
                if(commit(next)&&selection>=state.reminderCount&&selection)--selection;
            }
        }else if(screen==Screen::ReminderEdit){
            if(k==Up&&reminderField) --reminderField;
            else if(k==Down&&reminderField<2)++reminderField;
            else if(k==Enter){
                Date from{};unsigned duration=0;
                for(char c:reminderDuration)if(c){if(c<'0'||c>'9'){duration=366;break;}duration=duration*10+unsigned(c-'0');}
                if(!parseDate(reminderDate.data(),from)||duration<1||duration>365||!reminderText[0])say(tr("Check date, 1-365 days and text","Vérifiez date, 1-365 jours et texte"));
                else if(dayIndex(from)+duration<=dayIndex(state.today.date))say(tr("Reminder already expired","Rappel déjà expiré"));
                else {State& next=stage();const unsigned index=editingReminder<ReminderCapacity?editingReminder:next.reminderCount;
                    next.reminders[index].from=from;next.reminders[index].duration=uint16_t(duration);next.reminders[index].text=reminderText;
                    if(index==next.reminderCount)++next.reminderCount;
                    if(commit(next))go(Screen::Reminders);
                }
            }else if(k==Backspace||k==127||(k>=32&&k<=126)){
                if(reminderField==0){if((k>='0'&&k<='9')||k=='-'||k==Backspace||k==127)edit(reminderDate,k);}
                else if(reminderField==1){if((k>='0'&&k<='9')||k==Backspace||k==127)edit(reminderDuration,k);}
                else edit(reminderText,k);
            }
        }else if(screen==Screen::Date){
            if(k==Enter){State& next=stage();Date d{};
                if(!parseDate(dateDraft.data(),d))say(tr("Invalid date: YYYY-MM-DD, 2000-2099","Date invalide : AAAA-MM-JJ, 2000-2099"));
                else {next.today.date=d;if(commit(next))go(Screen::Home);}
            }else if((k>='0'&&k<='9')||k=='-'||k==Backspace||k==127)edit(dateDraft,k);
        }else if(screen==Screen::Activities){
            const unsigned total=visibleActivities();
            unsigned remaining=total>page*PageSize?total-page*PageSize:0;
            unsigned rows=remaining<PageSize?remaining:PageSize;
            const unsigned pages=currentPages();
            if(confirmingDelete){
                if(k==Enter){
                    const unsigned id=page*PageSize+selection;
                    if(id<state.activityCount){
                        State& next=stage();ActivityFlags flags=0;
                        for(unsigned i=0;i<next.activityCount;++i)if(i!=id&&(next.today.flags&(ActivityFlags(1u)<<i)))
                            flags|=ActivityFlags(1u)<<(i<id?i:i-1);
                        next.today.flags=flags;
                        for(unsigned i=id;i+1<next.activityCount;++i)next.activities[i]=next.activities[i+1];
                        next.activities[--next.activityCount]=Activity{};
                        refreshActivityFlags(next);
                        if(commit(next)){confirmingDelete=false;clampActivityCursor();}
                    }
                }
            }else if(k==Left||k==Right){page=(page+(k==Right?1:pages-1))%pages;selection=0;}
            else if(rows&&k==Up)selection=(selection+rows-1)%rows;
            else if(rows&&k==Down)selection=(selection+1)%rows;
            else if(daysBack&&(k==Enter||k=='0'||(k>='1'&&k<='9')||k=='e'||k=='E'||k=='n'||k=='N'||k==DeleteKey||k==F4||k==F5))readOnly();
            else if(k==Enter||k=='0'||(k>='1'&&k<='9')){
                unsigned row=k==Enter?selection:(k=='0'?9:unsigned(k-'1'));
                if(row>=rows)return;
                unsigned id=page*PageSize+row;
                if(id>=state.activityCount)return; // Orphan archive rows are display-only.
                State& next=stage();next.today.flags^=ActivityFlags(1u)<<id;
                if(commit(next))selection=row;
            }else if((k=='n'||k=='N')){
                if(state.activityCount>=ActivityCapacity)say(tr("Limit: 30 activities","Limite : 30 activités"));
                else beginActivityEdit(ActivityCapacity);
            }else if((k=='e'||k=='E')&&rows&&page*PageSize+selection<state.activityCount)beginActivityEdit(page*PageSize+selection);
            else if(k==DeleteKey&&rows&&page*PageSize+selection<state.activityCount)confirmingDelete=true;
            else if((k==F4||k==F5)&&rows){
                const unsigned id=page*PageSize+selection;
                if(id<state.activityCount&&state.activityCount>=2){
                    State& next=stage();unsigned other;
                    if(k==F4&&id==0){rotateActivity(next,true);other=next.activityCount-1;}
                    else if(k==F5&&id+1==state.activityCount){rotateActivity(next,false);other=0;}
                    else {other=k==F4?id-1:id+1;swapActivities(next,id,other);}
                    refreshActivityFlags(next);
                    if(commit(next)){selection=other%PageSize;page=other/PageSize;}
                }
            }
        }else if(screen==Screen::ActivityEdit){
            if(k==Enter){
                trimName(activityDraft.data());
                if(!activityDraft[0]){screen=Screen::Activities;activityDraft.fill(0);} // Empty = cancel.
                else if(!validActivityName(activityDraft.data()))say(tr("Name is too long","Nom trop long"));
                else if(duplicateActivityName(state,activityDraft.data(),editingActivity,state.language))say(tr("That name already exists","Ce nom existe déjà"));
                else {State& next=stage();unsigned destination=editingActivity;
                    if(editingActivity>=ActivityCapacity){
                        if(next.activityCount>=ActivityCapacity){say(tr("Limit: 30 activities","Limite : 30 activités"));render();return;}
                        destination=next.activityCount;next.activities[destination].id=next.nextActivityId++;
                        ++next.activityCount;
                    }
                    if(next.language==Language::French)copyActivityName(next.activities[destination].french,activityDraft.data());
                    else copyActivityName(next.activities[destination].english,activityDraft.data());
                    refreshActivityFlags(next);
                    if(commit(next)){editingActivity=destination;page=destination/PageSize;selection=destination%PageSize;screen=Screen::Activities;activityDraft.fill(0);message.fill(0);}
                }
            }else edit(activityDraft,k);
        }else if(screen==Screen::Weight){
            if(daysBack){if(k==Enter)go(Screen::Home);else readOnly();}
            else if(k==Up||k==Down||k==9)field=1-field;
            else if(k==Enter){State& next=stage();
                if(!parseWeight(weights[0].data(),next.today.expected)||!parseWeight(weights[1].data(),next.today.actual))say(tr("Invalid weight (example: 110.500)","Poids invalide (exemple : 110,500)"));
                else if(commit(next))go(Screen::Home);
            }else if((k>='0'&&k<='9')||k=='.'||k==','||k==Backspace||k==127)edit(weights[field],k);
        }else if(screen==Screen::Kanban){
            unsigned n=tasksIn(kanbanColumn);if(n&&selection>=n)selection=n-1;
            if(k==Left||k==Right){
                const unsigned column=unsigned(kanbanColumn);kanbanColumn=TaskStatus((column+(k==Right?1:2))%3);selection=0;
            }else if(k==Up&&n)selection=(selection+n-1)%n;
            else if(k==Down&&n)selection=(selection+1)%n;
            else if(k=='n'||k=='N'){
                if(state.taskCount>=TaskCapacity)say(tr("Kanban is full (24 tasks)","Kanban complet (24 tâches)"));
                else beginTaskEdit(TaskCapacity);
            }else if(k==Enter&&n)beginTaskEdit(taskAt(kanbanColumn,selection));
            else if(k==DeleteKey&&n){
                const unsigned index=taskAt(kanbanColumn,selection);State& next=stage();
                for(unsigned i=index;i+1<next.taskCount;++i)next.tasks[i]=next.tasks[i+1];
                next.tasks[--next.taskCount]=Task{};
                if(commit(next)&&selection>=tasksIn(kanbanColumn)&&selection) --selection;
            }else if(k>=F1&&k<=F3&&n)moveTaskTo(TaskStatus(unsigned(k-F1)));
            else if(k==F4&&n)reorderTask(false);
            else if(k==F5&&n)reorderTask(true);
        }else if(screen==Screen::TaskEdit){
            if(k==Enter){
                if(!taskDraft[0])say(tr("Task name cannot be empty","Le nom ne peut pas être vide"));
                else {State& next=stage();
                    unsigned destination=editedTask;
                    if(editedTask<TaskCapacity)next.tasks[editedTask].name=taskDraft;
                    else if(next.taskCount>=TaskCapacity){say(tr("Kanban is full (24 tasks)","Kanban complet (24 tâches)"));render();return;}
                    else {destination=next.taskCount;next.tasks[next.taskCount].status=kanbanColumn;next.tasks[next.taskCount].name=taskDraft;++next.taskCount;}
                    if(commit(next)){editedTask=destination;selection=taskPosition(kanbanColumn,editedTask);screen=Screen::Kanban;message.fill(0);}
                }
            }else edit(taskDraft,k);
        }else if(screen==Screen::DailyReview){
            const unsigned maximum=reviewRows()>8?reviewRows()-8:0;
            if(k==Down&&reviewOffset<maximum)++reviewOffset;
            else if(k==Up&&reviewOffset)--reviewOffset;
            else if(k==Right)reviewOffset=(reviewOffset+8<maximum)?reviewOffset+8:maximum;
            else if(k==Left)reviewOffset=reviewOffset>8?reviewOffset-8:0;
        }else if(screen==Screen::Config){
            if(k==Up)selection=(selection+2)%3;
            if(k==Down)selection=(selection+1)%3;
            if(k>='1'&&k<='3'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){
                if(selection==0)go(Screen::Language);
                else if(selection==1)go(Screen::Colors);
                else {State& next=stage();next.language=pendingLanguage;
                    if(commit(next)){go(Screen::Home);say(tr("Configuration saved","Configuration enregistrée"));}}
            }
        }else if(screen==Screen::Language){
            if(k==Up||k==Down)selection=1-selection;
            if(k=='1'||k=='2'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){pendingLanguage=Language(selection);go(Screen::Config);}
        }else if(screen==Screen::Colors){
            if(k==Up)selection=(selection+4)%5;
            if(k==Down)selection=(selection+1)%5;
            if(k>='1'&&k<='5')selection=unsigned(k-'1');
            pendingPalette=Palette(selection);
            if(k==Enter){State& next=stage();next.palette=pendingPalette;
                if(commit(next)){go(Screen::Home);say(tr("Color saved","Couleur enregistrée"));}}
        }else if(screen==Screen::RosaryMode){
            if(k==Up)selection=(selection+3)%4;
            if(k==Down)selection=(selection+1)%4;
            if(k>='1'&&k<='4'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){rosaryMode=RosaryMode(selection);go(Screen::RosarySet);}
        }else if(screen==Screen::RosarySet){
            if(k==Up)selection=(selection+4)%5;
            if(k==Down)selection=(selection+1)%5;
            if(k>='1'&&k<='5'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){rosarySet=selection?MysterySet(selection-1):suggestedMysteries();
                if(rosaryMode==RosaryMode::Decade)go(Screen::RosaryDecade);
                else {rosaryStep=rosaryPage=0;go(Screen::RosaryPrayer);}}
        }else if(screen==Screen::RosaryDecade){
            if(k==Up)selection=(selection+4)%5;
            if(k==Down)selection=(selection+1)%5;
            if(k>='1'&&k<='5'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){rosaryDecade=selection;rosaryStep=rosaryPage=0;go(Screen::RosaryPrayer);}
        }else if(screen==Screen::RosaryPrayer){
            const char* text=rosaryText(rosaryView(rosaryStep));const unsigned pages=(displayCells(text)+359)/360;
            if(k==Left){if(rosaryPage)--rosaryPage;else if(rosaryStep){--rosaryStep;const char* prior=rosaryText(rosaryView(rosaryStep));rosaryPage=(displayCells(prior)+359)/360-1;}}
            else if(k==Right||k==Enter){if(rosaryPage+1<pages)++rosaryPage;else if(rosaryStep+1<rosarySteps()){++rosaryStep;rosaryPage=0;}else go(Screen::RosaryComplete);}
        }else if(screen==Screen::RosaryComplete){
            if(k==Enter)go(Screen::Home);
        }else if(screen==Screen::Finish){
            if(k==Enter){uint32_t gain=points(state.today);
                if(state.today.number==std::numeric_limits<uint32_t>::max()||state.xp>std::numeric_limits<uint32_t>::max()-gain)say(tr("Counter limit reached","Limite du compteur atteinte"));
                else if(!validDate(state.today.date))say(tr("Set Date before closing the day","Renseignez Date avant de terminer le jour"));
                else {State& next=stage();const unsigned slot=next.next;next.history[slot]=next.today;freezeDayNames(next,next.history[slot]);
                    next.next=uint8_t((next.next+1)%HistoryLength);if(next.count<HistoryLength)++next.count;
                    Date tomorrow=state.today.date;if(!nextDate(tomorrow)) {say(tr("Calendar ends on 2099-12-31","Le calendrier s'arrête au 2099-12-31"));render();return;}
                    // The export is deliberately written before the A/B commit.
                    // A retry overwrites the same dated file if the snapshot fails.
                    if(!hw.exportWeightCsv(state.today)){say(tr("Weight CSV export failed; day remains open","Export CSV poids échoué ; journée reste ouverte"));render();return;}
                    if(!hw.exportIcs(next.history[slot],state.xp+gain,gain,state.language,next)){say(tr("ICS export failed; day remains open","Export ICS échoué ; journée reste ouverte"));render();return;}
                    next.xp+=gain;next.today=Day{};next.today.number=state.today.number+1;next.today.date=tomorrow;
                    // LPS-112-CHG03: XP is still banked in the save for format compatibility, never shown.
                    if(commit(next))go(Screen::Reward);
                }
            }
        }else if(screen==Screen::Reward&&k==Enter)go(Screen::Home);
        render();
    }
    void render(){
        hw.setPalette(displayedPalette());hw.clear();char b[128];const Day& day=viewedDay();char date[11]{};
        if(validDate(day.date))dateText(day.date,date,sizeof date);else std::snprintf(date,sizeof date,"%s%lu",tr("D","J"),static_cast<unsigned long>(day.number));
        std::snprintf(b,sizeof b,"LPS COMPANION v1.12    %s",date);line(0,b,true);
        if(validDate(day.date)){longDateText(day.date,b,sizeof b);line(1,b);}
        if(daysBack)line(2,tr("ARCHIVED DAY - READ ONLY","JOUR ARCHIVÉ - LECTURE SEULE"));
        if(screen==Screen::Home){
            line(3,daysBack?tr("CLOSED DAY","JOUR TERMINÉ"):tr("TODAY","AUJOURD'HUI"));
            const unsigned marked=daysBack&&day.namedCount?day.namedCount:count(day.flags);
            std::snprintf(b,sizeof b,tr("%u/%u activities","%u/%u activités"),marked,state.activityCount);line(4,b);
            const char* menu[]={"0  Date",tr("1  Activities","1  Activités"),tr("2  Weight tracker","2  Suivi du poids"),"3  Kanban",tr("4  Christian Rosary","4  Rosaire chrétien"),tr("5  Daily review","5  Revue journalière"),"6  Config",tr("7  Reminders","7  Rappels"),tr("8  Timer","8  Minuteur"),tr("9  Close the day","9  Terminer le jour")};
            const unsigned first=daysBack?1:0, total=daysBack?2:10;
            for(unsigned i=0;i<total;++i)line(6+int(i),menu[first+i],i==selection);
            line(17,tr("<- Previous day   -> Current day","<- Jour précédent   -> Jour actuel"));
        }else if(screen==Screen::TimerSelect){
            line(3,tr("TIMER","MINUTEUR"));
            const char* options[]={"1  5 minutes","2  30 minutes","3  60 minutes"};
            for(unsigned i=0;i<3;++i)line(6+int(i)*2,options[i],selection==i);
        }else if(screen==Screen::TimerRunning||screen==Screen::TimerComplete){
            line(3,tr("TIMER","MINUTEUR"));
            const unsigned seconds=timerSeconds();std::snprintf(b,sizeof b,"%02u:%02u",seconds/60,seconds%60);
            hw.largeText(b,seconds<60);
            if(screen==Screen::TimerComplete)line(14,tr("TIMER FINISHED!","MINUTEUR TERMINÉ !"));
            else if(timerPaused)line(14,tr("PAUSED","EN PAUSE"));
            line(17,screen==Screen::TimerComplete?tr("1-3: Restart  Enter: Menu  Esc: Back","1-3: Relancer  Enter: Menu  Esc: Retour"):
                (timerPaused?tr("Enter: Resume   Esc: Cancel","Enter: Reprendre   Esc: Annuler"):tr("Enter: Pause   Esc: Cancel","Enter: Pause   Esc: Annuler")));
        }else if(screen==Screen::ReminderPopup){
            line(3,tr("REMINDERS","RAPPELS"));
            unsigned row=6;for(unsigned i=0;i<state.reminderCount;++i)if(active(state.reminders[i])){
                std::snprintf(b,sizeof b,"%u. %s",row-5,state.reminders[i].text.data());line(int(row),b);row+=2;
            }
            line(15,tr("Press any key to continue","Appuyez sur une touche"));
        }else if(screen==Screen::Reminders){
            line(3,tr("REMINDERS (max 3)","RAPPELS (max 3)"));
            if(!state.reminderCount)line(6,tr("No reminders","Aucun rappel"));
            for(unsigned i=0;i<state.reminderCount;++i){char from[11];dateText(state.reminders[i].from,from,sizeof from);
                std::snprintf(b,sizeof b,"%s  %u %s",from,state.reminders[i].duration,tr("days","jours"));line(6+int(i*3),b,i==selection);
                line(7+int(i*3),state.reminders[i].text.data());
            }
            line(16,tr("N New  Enter Edit  Del Delete","N Nouveau Entrée Modifier Suppr Effacer"));
        }else if(screen==Screen::ReminderEdit){
            line(3,tr("EDIT REMINDER","MODIFIER LE RAPPEL"));
            std::snprintf(b,sizeof b,tr("From: %s","Début : %s"),reminderDate.data());line(6,b,reminderField==0);
            std::snprintf(b,sizeof b,tr("Days: %s","Jours : %s"),reminderDuration.data());line(8,b,reminderField==1);
            line(10,tr("Text:","Texte :"));line(11,reminderText.data(),reminderField==2);
            line(15,tr("Up/Down fields  Enter save","Haut/Bas champs  Entrée valider"));
        }else if(screen==Screen::Date){
            line(3,"DATE");line(6,dateDraft.data(),true);
            line(9,tr("Calendar: 2000-01-01 to 2099-12-31","Calendrier : 2000-01-01 au 2099-12-31"));
            line(12,tr("Enter: save / Esc: cancel","Entrée : valider / Échap : annuler"));
            line(14,tr("Next day is suggested after closing.","Jour suivant proposé après clôture."));
        }else if(screen==Screen::Activities){
            const unsigned total=visibleActivities(),pages=currentPages();
            std::snprintf(b,sizeof b,tr("ACTIVITIES                      %u/%u","ACTIVITÉS                       %u/%u"),page+1,pages);line(3,b);
            if(!total)line(6,tr("N: add an activity","N : ajouter une activité"));
            unsigned end=(page+1)*PageSize;if(end>total)end=total;
            for(unsigned i=page*PageSize;i<end;++i){
                char label[32];
                const bool catalog=i<state.activityCount;
                displayText(catalog?displayName(state.activities[i],state.language):extraLabel(day,i-state.activityCount),label,sizeof label);
                const unsigned row=i-page*PageSize;
                const char key=row==9?'0':char('1'+row);
                const bool checked=catalog?(day.flags&(ActivityFlags(1u)<<i)):true;
                std::snprintf(b,sizeof b,"%c  %-24s [%c]",key,label,checked?'x':' ');
                hw.text(0,(5+int(row))*16,b,row==selection);
            }
            if(confirmingDelete&&page*PageSize+selection<state.activityCount){
                char shown[32];displayText(displayName(state.activities[page*PageSize+selection],state.language),shown,sizeof shown);
                std::snprintf(b,sizeof b,tr("Delete %s?","Supprimer %s ?"),shown);line(15,b);
                line(16,tr("Enter: yes   Esc: no","Entrée : oui   Échap : non"));
            }else if(daysBack)line(15,tr("<- -> pages   Esc: day menu","<- -> pages   Échap : menu du jour"));
            else {
                line(15,tr("1-9,0 Tick  E Ren  N New  Del","1-9,0 Cocher  E Renom  N Nouv  Suppr"));
                line(16,tr("F4/F5 Move  <- -> Pages","F4/F5 Déplacer  <- -> Pages"));
            }
        }else if(screen==Screen::ActivityEdit){
            line(3,editingActivity<ActivityCapacity?tr("RENAME ACTIVITY","RENOMMER L'ACTIVITÉ"):tr("NEW ACTIVITY","NOUVELLE ACTIVITÉ"));
            line(6,activityDraft.data(),true);
            std::snprintf(b,sizeof b,tr("%u/24 characters","%u/24 caractères"),displayWidth(activityDraft.data()));line(9,b);
            line(13,tr("Enter: save  (empty cancels)","Entrée : enregistrer  (vide = annuler)"));
            line(15,tr("Backspace: erase  Esc: cancel","Retour : effacer  Échap : annuler"));
        }else if(screen==Screen::Weight){
            line(3,tr("WEIGHT TRACKER (kg)","SUIVI DU POIDS (kg)"));std::snprintf(b,sizeof b,tr("Expected : %s","Attendu  : %s"),weights[0].data());line(6,b,field==0);std::snprintf(b,sizeof b,tr("Actual   : %s","Effectif : %s"),weights[1].data());line(9,b,field==1);
            int32_t a=0,c=0;bool valid=parseWeight(weights[0].data(),a)&&parseWeight(weights[1].data(),c)&&a>=0&&c>=0;
            if(valid){int32_t d=c-a,abs=d<0?-d:d;std::snprintf(b,sizeof b,tr("Difference: %c%ld.%03ld kg","Écart : %c%ld.%03ld kg"),d<0?'-':'+',long(abs/1000),long(abs%1000));}else std::snprintf(b,sizeof b,"%s",tr("Difference: --","Écart : --"));line(12,b);
            line(14,tr("Blank = not recorded","Vide = non renseigné"));line(15,daysBack?tr("Enter / Esc: day menu","Entrée / Échap : menu du jour"):tr("Up/Down: field - Enter: save","Haut/Bas : champ - Entrée : valider"));
        }else if(screen==Screen::Kanban){
            const unsigned n=tasksIn(kanbanColumn);if(n&&selection>=n)selection=n-1;
            std::snprintf(b,sizeof b,"KANBAN  < %s >  (%u)",taskStatusName(kanbanColumn),n);line(3,b);
            if(!n)line(6,tr("No tasks. Press N to create one.","Aucune tâche. Appuyez sur N."));
            else {
                constexpr unsigned visible=9;const unsigned first=selection>=visible?selection-visible+1:0;
                const unsigned end=(first+visible<n)?first+visible:n;
                for(unsigned position=first;position<end;++position){
                    const unsigned index=taskAt(kanbanColumn,position);
                    std::snprintf(b,sizeof b,"%2u  %-32s",position+1,state.tasks[index].name.data());
                    line(5+int(position-first),b,position==selection);
                }
            }
            line(14,tr("N New  Enter Edit  Del Delete","N Nouveau Entrée Modifier Suppr Effacer"));
            line(15,tr("F1 TODO  F2 DOING  F3 DONE","F1 À FAIRE F2 EN COURS F3 FINI"));
            line(16,tr("F4 Up F5 Down  <- -> Columns","F4 Monter F5 Descendre <- -> Colonnes"));
        }else if(screen==Screen::TaskEdit){
            line(3,editedTask<TaskCapacity?tr("EDIT TASK","MODIFIER LA TÂCHE"):tr("NEW TASK","NOUVELLE TÂCHE"));
            line(6,taskDraft.data(),true);
            std::snprintf(b,sizeof b,tr("%zu/32 characters","%zu/32 caractères"),std::strlen(taskDraft.data()));line(9,b);
            line(13,tr("Enter: save task","Entrée : enregistrer la tâche"));
            line(15,tr("Backspace: erase  Esc: cancel","Retour: effacer  Échap: annuler"));
        }else if(screen==Screen::DailyReview){
            std::snprintf(b,sizeof b,"%s  %s",tr("DAILY REVIEW","REVUE JOURNALIÈRE"),date);line(3,b);
            char weight[20];weightText(state.today.actual,weight,sizeof weight);
            std::snprintf(b,sizeof b,tr("Actual weight: %s kg","Poids effectif : %s kg"),weight);line(4,b);
            const unsigned total=reviewRows();
            for(unsigned i=0;i<8&&reviewOffset+i<total;++i){reviewLine(reviewOffset+i,b,sizeof b);line(7+int(i),b);}
            std::snprintf(b,sizeof b,tr("Up/Down scroll  %u-%u/%u  Esc back","Haut/Bas défiler  %u-%u/%u  Échap retour"),reviewOffset+1,(reviewOffset+8<total?reviewOffset+8:total),total);line(16,b);
        }else if(screen==Screen::Config){
            line(3,tr("CONFIGURATION","CONFIGURATION"));
            std::snprintf(b,sizeof b,"%s: %s",tr("1  Language","1  Langue"),pendingLanguage==Language::English?"English":"Français");line(6,b,selection==0);
            std::snprintf(b,sizeof b,"%s: PAL%u",tr("2  Colors","2  Couleurs"),unsigned(state.palette)+1);line(7,b,selection==1);
            line(8,tr("3  Save","3  Enregistrer"),selection==2);
            line(11,tr("Language: select, then Save.","Langue : choisir, puis enregistrer."));
            line(12,tr("Esc: discard changes","Échap : annuler les modifications"));
        }else if(screen==Screen::Language){
            line(3,tr("LANGUAGE","LANGUE"));
            line(6,"1  English",selection==0);line(7,"2  Français",selection==1);
            line(10,tr("Enter: select / Esc: back","Entrée : choisir / Échap : retour"));
            line(11,tr("Then Save in Configuration.","Puis enregistrer dans Configuration."));
        }else if(screen==Screen::Colors){
            line(3,tr("COLORS","COULEURS"));
            line(6,tr("1  PAL1  red","1  PAL1  rouge"),selection==0);
            line(7,tr("2  PAL2  green","2  PAL2  vert"),selection==1);
            line(8,tr("3  PAL3  blue","3  PAL3  bleu"),selection==2);
            line(9,"4  PAL4  Orange",selection==3);
            line(10,tr("5  PAL5  Pink","5  PAL5  Rose"),selection==4);
            line(13,tr("Enter: save color","Entrée : enregistrer couleur"));
        }else if(screen==Screen::RosaryMode){
            line(3,tr("CHOOSE ROSARY MODE","CHOISIR LE MODE DU ROSAIRE"));
            line(6,tr("1  One decade             3-5 min","1  Une dizaine            3-5 min"),selection==0);
            line(8,tr("2  Full Rosary          15-20 min","2  Rosaire complet      15-20 min"),selection==1);
            line(10,tr("3  Scripture + guidance 20-30 min","3  Écriture + méditation 20-30 min"),selection==2);
            line(12,tr("4  Slow contemplation   30-40 min","4  Contemplation lente  30-40 min"),selection==3);
            line(15,tr("Choose before every new session.","Choix demandé à chaque nouvelle séance."));
        }else if(screen==Screen::RosarySet){
            line(3,tr("CHOOSE THE MYSTERIES","CHOISIR LES MYSTÈRES"));
            std::snprintf(b,sizeof b,tr("1  Today: %s","1  Aujourd'hui : %s"),mysterySetName(suggestedMysteries()));line(6,b,selection==0);
            for(unsigned i=0;i<4;++i){std::snprintf(b,sizeof b,"%u  %s",i+2,mysterySetName(MysterySet(i)));line(7+int(i),b,selection==i+1);}
            line(14,tr("Today's cycle is selected by date.","Cycle du jour proposé automatiquement."));
        }else if(screen==Screen::RosaryDecade){
            std::snprintf(b,sizeof b,tr("ONE DECADE - %s MYSTERIES","UNE DIZAINE - MYSTÈRES %s"),mysterySetName(rosarySet));line(3,b);
            for(unsigned i=0;i<5;++i){std::snprintf(b,sizeof b,"%u  %s",i+1,mysteryName(rosarySet,i));line(6+int(i),b,selection==i);}
            line(14,tr("Choose the mystery to pray.","Choisissez le mystère à prier."));
        }else if(screen==Screen::RosaryPrayer){
            const RosaryView view=rosaryView(rosaryStep);const char* text=rosaryText(view);
            std::snprintf(b,sizeof b,"%s  %u/%u",rosaryPartName(view),rosaryStep+1,rosarySteps());line(3,b,true);
            if(view.part==RosaryPart::Mystery||view.part==RosaryPart::Scripture||view.part==RosaryPart::Meditation||view.part==RosaryPart::Silence){
                std::snprintf(b,sizeof b,tr("Mystery %u/%u - %s","Mystère %u/%u - %s"),rosaryMode==RosaryMode::Decade?1:view.decade+1,rosaryMode==RosaryMode::Decade?1:5,mysterySetName(rosarySet));line(4,b);
            }else if(view.part==RosaryPart::Hail){
                std::snprintf(b,sizeof b,tr("Decade %u/%u    Hail Mary %u/10","Dizaine %u/%u    Ave Maria %u/10"),rosaryMode==RosaryMode::Decade?1:view.decade+1,rosaryMode==RosaryMode::Decade?1:5,view.count);line(4,b);
                char beads[12];for(unsigned i=0;i<10;++i)beads[i]=i<view.count?'*':'o';beads[10]=0;line(5,beads);
            }else if(view.part==RosaryPart::OpeningHail){std::snprintf(b,sizeof b,tr("Opening Hail Mary %u/3","Ave Maria d'ouverture %u/3"),view.count);line(4,b);}
            const unsigned pages=(displayCells(text)+359)/360;
            for(unsigned i=0;i<9;++i){char slice[96];cellSlice(text,rosaryPage*360+i*40,40,slice,sizeof slice);line(6+int(i),slice);}
            if(pages>1){std::snprintf(b,sizeof b,tr("Page %u/%u  Enter: continue","Page %u/%u  Entrée : continuer"),rosaryPage+1,pages);line(15,b);}
            else line(15,tr("Enter: next prayer   <- previous","Entrée : suite       <- précédent"));
            line(16,tr("Esc: leave session","Échap : quitter la séance"));
        }else if(screen==Screen::RosaryComplete){
            line(3,tr("ROSARY COMPLETED","ROSAIRE TERMINÉ"),true);
            line(6,tr("The prayer session is complete.","La séance de prière est terminée."));
            line(9,tr("Remain for a moment in God's peace.","Demeurez un instant dans la paix de Dieu."));
            line(13,tr("Enter: return to LPS Companion","Entrée : retour à LPS Companion"));
        }else if(screen==Screen::Finish){
            line(3,tr("CLOSE THE DAY?","TERMINER LE JOUR ?"));std::snprintf(b,sizeof b,tr("Activities: %u","Activités : %u"),count(state.today.flags));line(6,b);line(13,tr("Enter: save and advance","Entrée : enregistrer et avancer"));line(15,tr("An empty day has no penalty.","Une journée vide ne coûte rien."));
        }else{
            line(3,tr("DAY SAVED","JOUR ENREGISTRÉ"));
            line(15,tr("Enter: start the new day","Entrée : commencer le nouveau jour"));
        }
        const int statusRow=screen==Screen::Home?18:((screen==Screen::TimerRunning||screen==Screen::TimerComplete)?16:17);
        if(missingStorage)hw.alert(0,statusRow*16,message.data());else if(message[0])line(statusRow,message.data());
        if(hw.batteryBelow20())hw.alert(0,18*16,tr("[!] Low battery!","[!] Batterie faible !"));
        if(screen!=Screen::TimerRunning&&screen!=Screen::TimerComplete)line(19,tr("Up/Down  Enter:OK  Esc:Back","Haut/Bas  Entrée:OK  Échap:Retour"),true);
        hw.present();
    }
};
} // namespace lps

#if defined(LPS_DESKTOP)
#include <cerrno>
#include <string>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <poll.h>
struct StdioSink final:lps::SaveSink {
    FILE* file;explicit StdioSink(FILE* f):file(f){}
    bool write(const char* data,std::size_t size)override{return std::fwrite(data,1,size,file)==size;}
};
struct StdioSource final:lps::SaveSource {
    FILE* file;explicit StdioSource(FILE* f):file(f){}
    long read(char* data,std::size_t capacity)override{const auto n=std::fread(data,1,capacity,file);return std::ferror(file)?-1:long(n);}
};
class Terminal final:public lps::Platform {
    std::array<std::array<char,41>,20> cells{};
    std::array<bool,20> inverse{};
    uint32_t generation=0;
public:
    void setPalette(lps::Palette)override{}
    void alert(int x,int y,const char* s)override{text(x,y,s,true);}
    void clear()override{for(auto& r:cells){r.fill(' ');r[40]=0;}inverse.fill(false);}
    void text(int x,int y,const char* s,bool reverse)override{
        int row=y/16,col=x/8;if(row<0||row>=20||col<0||col>=40)return;
        inverse[row]=reverse;while(*s&&col<40)cells[row][col++]=*s++;
    }
    void present()override{
        std::printf("\033[2J\033[H");for(unsigned i=0;i<20;++i){
            std::printf("%s",inverse[i]?"\033[7m":"");
            for(unsigned char c:cells[i]){if(!c)break;if(c>=128){std::putchar(0xc0|(c>>6));std::putchar(0x80|(c&0x3f));}else std::putchar(c);}
            std::printf("\033[0m\n");
        }
        std::printf("\nCommands: 1-9, up/down/left/right, enter, esc, back,\ntext YOUR TEXT, quit\n> ");std::fflush(stdout);
    }
    lps::LoadResult load(lps::State& state)override{
        FILE* f=std::fopen("lps_companion.jsn","rb");
        if(f){
            StdioSource source(f);lps::SaveInfo info;
            const bool ok=lps::readSave(source,state,info)==lps::SaveCheck::Valid;std::fclose(f);
            if(!ok)return lps::LoadResult::Error;
            generation=info.generation;return lps::LoadResult::Ok;
        }
        if(errno!=ENOENT)return lps::LoadResult::Error;
        // LPS-113-CHG06 migration: read the older binary save once, never modify it.
        f=std::fopen("lps_companion.sav","rb");if(!f)return errno==ENOENT?lps::LoadResult::Missing:lps::LoadResult::Error;
        lps::Blob b;b.size=std::fread(b.bytes.data(),1,b.bytes.size(),f);int extra=std::fgetc(f);bool ok=!std::ferror(f)&&extra==EOF;std::fclose(f);
        return ok&&lps::decode(b.bytes.data(),b.size,state)?lps::LoadResult::Ok:lps::LoadResult::Error;
    }
    bool save(const lps::State& state)override{
        if(generation==UINT32_MAX)return false;
        FILE* f=std::fopen("lps_companion.jsn.tmp","wb");if(!f)return false;
        StdioSink sink(f);uint32_t checksum=0;
        bool ok=lps::writeSave(state,generation+1,sink,checksum);
        if(std::fflush(f)!=0)ok=false;
        if(::fsync(::fileno(f))!=0)ok=false;
        if(std::fclose(f)!=0)ok=false;
        if(!ok){std::remove("lps_companion.jsn.tmp");return false;}
        if(std::rename("lps_companion.jsn.tmp","lps_companion.jsn")!=0)return false;
        ++generation;return true;
    }
    bool exportIcs(const lps::Day& day,uint32_t,uint32_t,lps::Language,const lps::State&)override{
        // Oversized date buffers keep strict host builds independent of range inference.
        char name[48],ymd[13],tomorrow[13];lps::dateText(day.date,name,sizeof name);
        std::snprintf(ymd,sizeof ymd,"%04u%02u%02u",day.date.year,day.date.month,day.date.day);
        auto next=day.date;if(!lps::nextDate(next))return false;std::snprintf(tomorrow,sizeof tomorrow,"%04u%02u%02u",next.year,next.month,next.day);
        std::snprintf(name,sizeof name,"%04u-%02u-%02u_LPS-Companion.ics",day.date.year,day.date.month,day.date.day);
        FILE* f=std::fopen(name,"wb");if(!f)return false;
        int result=std::fprintf(f,"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//LPS Companion//EN\r\nBEGIN:VEVENT\r\nUID:lps-companion-%s@desktop\r\nDTSTART;VALUE=DATE:%s\r\nDTEND;VALUE=DATE:%s\r\nSUMMARY:LPS Companion\r\nDESCRIPTION:XP:\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",ymd,ymd,tomorrow);
        return result>0&&std::fclose(f)==0;
    }
    bool exportWeightCsv(const lps::Day&)override{return true;}
};
int main(){Terminal terminal;lps::App app(terminal);app.start();std::string s;
    while(true){
        app.tick(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
        pollfd input{STDIN_FILENO,POLLIN,0};
        if(::poll(&input,1,100)<=0)continue;
        if(!std::getline(std::cin,s))break;
        if(s=="quit")break;
        if(s.rfind("text ",0)==0){for(unsigned char c:s.substr(5))app.key(c);}
        else if(s=="up")app.key(lps::Up);else if(s=="down")app.key(lps::Down);
        else if(s=="left")app.key(lps::Left);else if(s=="right")app.key(lps::Right);
        else if(s=="esc")app.key(lps::Escape);else if(s=="back")app.key(lps::Backspace);
        else if(s=="enter"||s.empty())app.key(lps::Enter);else if(s.size()==1)app.key(s[0]);
        else {std::cout<<"Unknown command.\n";app.render();}
    }
}
#elif defined(LPS_TEST)
#include <cassert>
#include "../tests/save_helpers.h"
class Memory final:public lps::Platform {
public:
    std::array<char,lps_test::MemoryCapacity> stored{};std::size_t size=0;uint32_t generation=0;bool exists=false,fail=false,failExport=false,failWeightCsv=false,noStorage=false,lowBattery=false;unsigned exports=0,weightExports=0;
    std::array<std::array<char,41>,20> rows{};
    std::array<bool,20> alerts{};
    lps::Palette palette=lps::Palette::Green;
    void setPalette(lps::Palette p)override{palette=p;}
    void alert(int x,int y,const char* text)override{alerts[y/16]=true;this->text(x,y,text,true);}
    bool batteryBelow20()override{return lowBattery;}
    void clear()override{}
    void text(int x,int y,const char* text,bool)override{assert(x>=0&&x<320&&y>=0&&y+16<=320);std::snprintf(rows[y/16].data(),41,"%s",text);}
    void present()override{}
    lps::LoadResult load(lps::State& state)override{
        if(noStorage)return lps::LoadResult::NoStorage;if(!exists)return lps::LoadResult::Missing;
        lps_test::MemorySource source(stored.data(),size);lps::SaveInfo info;
        if(lps::readSave(source,state,info)!=lps::SaveCheck::Valid)return lps::LoadResult::Error;
        generation=info.generation;return lps::LoadResult::Ok;
    }
    bool save(const lps::State& state)override{
        if(fail)return false;
        static std::array<char,lps_test::MemoryCapacity> scratch{};std::size_t n=0;
        lps_test::MemorySink sink(scratch,n);uint32_t checksum=0;
        if(!lps::writeSave(state,generation+1,sink,checksum))return false;
        stored=scratch;size=n;++generation;exists=true;return true;
    }
    void put(const lps::State& state){assert(save(state));}
    bool exportIcs(const lps::Day&,uint32_t,uint32_t,lps::Language,const lps::State&)override{if(failExport)return false;++exports;return true;}
    bool exportWeightCsv(const lps::Day&)override{if(failWeightCsv)return false;++weightExports;return true;}
};
int main(){
    using namespace lps;int32_t g=0;
    assert(parseWeight("110,500",g)&&g==110500);assert(parseWeight("0.001",g)&&g==1);
    assert(parseWeight("",g)&&g==-1);assert(!parseWeight("0",g));assert(!parseWeight("-1",g));assert(!parseWeight("1.2345",g));assert(!parseWeight("1.",g));assert(!parseWeight("nan",g));
    Date calendar{};assert(parseDate("2028-02-29",calendar)&&nextDate(calendar)&&calendar.month==3&&calendar.day==1);
    assert(parseDate("2027-12-31",calendar)&&nextDate(calendar)&&calendar.year==2028&&calendar.month==1&&calendar.day==1);
    assert(!parseDate("2027-02-29",calendar)&&!parseDate("1999-12-31",calendar)&&!parseDate("2100-01-01",calendar));
    calendar=LastCalendarDate;assert(!nextDate(calendar));
    Memory history;State historyState;historyState.today.number=2;
    historyState.today.flags=1;historyState.today.expected=120000;
    historyState.history[0].flags=1024;historyState.history[0].expected=110500;
    historyState.history[0].actual=111200;
    std::strcpy(historyState.history[0].note.data(),"Yesterday only");
    historyState.next=1;historyState.count=1;history.put(historyState);
    App browsing(history);browsing.start();browsing.key(Left);
    assert(browsing.viewedDay().number==1);
    // LPS-112-CHG05: the archived-day menu is 1 Activities, 2 Weight; no notepad.
    assert(std::strstr(history.rows[6].data(),"1  Activities")&&std::strstr(history.rows[7].data(),"2  Weight tracker"));
    for(const auto& row:history.rows)assert(!std::strstr(row.data(),"Notepad"));
    browsing.key('3');assert(browsing.currentScreen()==Screen::Home);
    browsing.key('2');
    assert(std::strstr(history.rows[6].data(),"110.500"));
    assert(std::strstr(history.rows[9].data(),"111.200"));
    assert(std::strstr(history.rows[12].data(),"+0.700"));
    browsing.key(Escape);browsing.key('1');browsing.key(Right);
    assert(std::strstr(history.rows[5].data(),"Weekend")&&std::strstr(history.rows[5].data(),"[x]"));
    browsing.key(Escape);browsing.key(Right);browsing.key('2');
    assert(std::strstr(history.rows[6].data(),"120.000"));
    Memory pages;App nav(pages);nav.start();nav.key('1');
    for(int i=0;i<9;++i)nav.key(Down);
    nav.key(Enter);
    assert(nav.data().today.flags==(1u<<9));
    nav.key(Right);nav.key(Enter);
    assert(nav.data().today.flags==((1u<<9)|(1u<<10)));
    App reload(pages);reload.start();assert(reload.data().today.flags==nav.data().today.flags);
    // LPS-113-CHG08b: leftover page is forgotten on re-entry; key 1 then means Walk again.
    nav.key(Escape);nav.key('1');
    nav.key('1');assert(nav.data().today.flags&1);
    nav.key(Right);nav.key(Enter);assert(!(nav.data().today.flags&(1u<<10)));
    nav.key(Escape);nav.key('3');assert(nav.currentScreen()==Screen::Kanban);nav.key(Escape);
    nav.key(Escape);nav.key(Up);nav.key(Enter);assert(nav.currentScreen()==Screen::Finish);
    nav.key(Escape);nav.key('9');assert(nav.currentScreen()==Screen::Finish);nav.key(Escape);nav.key('6');assert(nav.currentScreen()==Screen::Config);nav.key('2');assert(nav.currentScreen()==Screen::Colors);
    assert(pages.palette==Palette::Green);nav.key(Down);assert(pages.palette==Palette::Blue);nav.key(Escape);assert(pages.palette==Palette::Green);
    nav.key('2');nav.key('1');nav.key(Enter);assert(nav.currentScreen()==Screen::Home&&nav.data().palette==Palette::Red&&pages.palette==Palette::Red);
    App paletteReload(pages);paletteReload.start();assert(paletteReload.data().palette==Palette::Red);
    State old;old.today.flags=0x1ff;old.today.expected=110500;old.xp=70;
    Blob legacy=lps_test::encodeLegacyV6(old);State migrated;assert(decode(legacy.bytes.data(),legacy.size,migrated));
    assert(migrated.today.flags==0x1ff&&migrated.today.expected==110500&&migrated.xp==70);
    // A real v1.4 payload has language then dates but no palette byte.
    Blob v3;v3.size=WireV3;std::memcpy(v3.bytes.data(),legacy.bytes.data(),13);v3.bytes[4]=3;
    std::memcpy(v3.bytes.data()+13,legacy.bytes.data()+14,WireV3-17);
    auto v3crc=crc32(v3.bytes.data(),v3.size-4);for(unsigned i=0;i<4;++i)v3.bytes[v3.size-4+i]=uint8_t(v3crc>>(8*i));
    assert(decode(v3.bytes.data(),v3.size,migrated)&&migrated.palette==Palette::Green&&migrated.today.flags==old.today.flags);
    Blob v4;v4.size=WireV4;std::memcpy(v4.bytes.data(),legacy.bytes.data(),WireV4-4);v4.bytes[4]=4;
    auto v4crc=crc32(v4.bytes.data(),v4.size-4);for(unsigned i=0;i<4;++i)v4.bytes[v4.size-4+i]=uint8_t(v4crc>>(8*i));
    assert(decode(v4.bytes.data(),v4.size,migrated)&&migrated.taskCount==0&&migrated.palette==Palette::Green);
    Memory missing;missing.noStorage=true;App noCard(missing);noCard.start();assert(std::strstr(missing.rows[18].data(),"Missing SD card"));
    Memory battery;battery.lowBattery=true;App lowPower(battery);lowPower.start();assert(battery.alerts[18]&&std::strstr(battery.rows[18].data(),"Low battery"));
    Memory mem;App a(mem);a.start();a.key('1');a.key('1');a.key('2');a.key('3');assert(points(a.data().today)==40);
    a.key('3');assert(points(a.data().today)==20);a.key('9');assert(a.data().today.flags&(1u<<8));
    State before=a.data();mem.fail=true;a.key('4');assert(a.data().today.flags==before.today.flags);mem.fail=false;
    a.key(Escape);a.key('2');for(char c:std::array<char,5>{'1','1','0',',','5'})a.key(c);a.key(Down);for(char c:std::array<char,5>{'1','1','1','.','2'})a.key(c);a.key(Enter);assert(a.data().today.expected==110500&&a.data().today.actual==111200);
    for(const auto& row:mem.rows)assert(!std::strstr(row.data(),"Notepad"));assert(!a.data().today.note[0]);
    a.key(Up);a.key(Enter);a.key(Enter);assert(a.data().xp==40&&a.data().today.number==2&&a.data().today.flags==0);assert(a.data().history[0].actual==111200);a.key(Enter);assert(a.data().xp==40);
    App restored(mem);restored.start();assert(restored.data().xp==40&&restored.data().today.number==2);
    // LPS-113-CHG06: the stored save is JSON; a cut-off copy is rejected.
    {constexpr char head[]="{\"format\":\"lps-companion\",\"version\":2,";assert(std::memcmp(mem.stored.data(),head,sizeof head-1)==0);}
    {State decoded;SaveInfo info;lps_test::MemorySource whole(mem.stored.data(),mem.size);assert(readSave(whole,decoded,info)==SaveCheck::Valid&&decoded.xp==40&&decoded.today.number==2);
     lps_test::MemorySource cut(mem.stored.data(),10);assert(readSave(cut,decoded,info)==SaveCheck::Damaged);}
    for(int i=0;i<40;++i){restored.key(Escape);restored.key(Up);restored.key(Enter);restored.key(Enter);restored.key(Enter);}assert(restored.data().count==31&&restored.data().today.number==42);
    mem.stored[60]^=1;App corrupt(mem);corrupt.start();auto original=mem.stored;corrupt.key('1');corrupt.key('1');assert(corrupt.data().today.flags==0);assert(mem.stored==original);
    std::puts("PASS: weights, toggles, bonus, rollback, no notepad, renumbered menu, closure, restore, ring history, CRC, palette preview/persistence, corrupt-save protection and render bounds.");
}
#endif
