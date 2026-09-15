/*
 LPS Companion - first native C++ implementation, 2026-09-15.
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
 Hardware behaviour still needs verification on a physical PicoCalc.
 The portable application remains independent of the Pico SDK; desktop and
 built-in tests below remain available for checking the UI/state machine.

 Behaviour:
 - Nine daily checkboxes; 10 XP each, +10 for >=3 selections (prototype rules).
 - No Journal menu. "Pour demain" is the renamed collectible screen.
 - Expected/actual weights are OPTIONAL daily inputs, stored as integer grams.
   Comma or dot accepted, up to three decimals. No generated weight-loss target.
 - Note: 96 printable ASCII characters; accents need a font/input extension.
 - Days are sequential, NOT calendar dates. RTC/calendar integration is pending.
 - Current day autosaves on checkbox changes and explicit form validation.
 - Closing a day banks XP once, unlocks souvenirs and advances the day.
 - The last 31 closed days are retained in a ring buffer (no history UI).
 - Persistent format: versioned little-endian bytes + CRC32, no raw struct dumps.
 - Missing storage starts a new profile. Corruption/I/O errors block writes;
   they never silently overwrite a damaged existing save.
 - Failed saves roll back RAM state; UI reports it. A late SD error can leave
   commit status uncertain; the adapter then blocks writes until restart.
   A/B recovery chooses the highest-generation intact record. No dynamic allocation in
   application code. SD adapter must provide atomic/durable replacement.

 Desktop commands are line-based: enter/up/down/left/right/esc/back, digits 1-9,
 text YOUR TEXT, quit. To edit a field, use back, or enter an empty field as-is.
 Desktop save is lps_companion.sav in the current working directory.
*/
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace lps {
constexpr int Width=320, Height=320, Columns=40, Rows=20;
constexpr std::size_t NoteLength=96, HistoryLength=31, BlobCapacity=4096;
enum Key { Enter=13, Backspace=8, Escape=27, Up=1000, Down, Left, Right };
enum class LoadResult { Missing, Ok, Error };
struct Platform {
    virtual ~Platform() = default;
    virtual void clear()=0;
    // Coordinates in pixels. reverse=true fills all 40 cells on that row.
    virtual void text(int x,int y,const char* ascii,bool reverse)=0;
    virtual void present()=0;
    // Return Error for truncated/oversized/inaccessible saves, Missing only if absent.
    virtual LoadResult load(uint8_t* bytes,std::size_t capacity,std::size_t& size)=0;
    // On false, RAM is rolled back; an ambiguous late commit MUST block further
    // writes until restart. The prior valid record must be retained. On true, replacement must be
    // recoverable after restart. FatFs: use validated A/B slots with generations,
    // f_sync and startup recovery, or an equivalent transactional scheme.
    virtual bool save(const uint8_t* bytes,std::size_t size)=0;
};
constexpr const char* Activities[]={"Marche","Repas","Bible","Messe","Travail","Projet","Sieste","Gurumed","Maladie"};
struct Gift { const char* name; uint32_t xp; };
constexpr Gift Gifts[]={{"Carnet de poche",30},{"Tasse de the",70},{"Boussole",120},{"Radio de poche",180},{"Mini-ordinateur",250},{"Lanterne",330}};
struct Day {
    uint32_t number=1;
    uint16_t flags=0;
    int32_t expected=-1, actual=-1; // grams; -1 is missing
    std::array<char,NoteLength+1> note{};
};
struct State {
    uint32_t xp=0;
    Day today{};
    std::array<Day,HistoryLength> history{};
    uint8_t next=0, count=0;
};
unsigned count(uint16_t flags) { unsigned n=0;for(;flags;flags>>=1)n+=flags&1;return n; }
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
uint32_t crc32(const uint8_t* p,std::size_t n) {
    uint32_t c=0xffffffffu;
    while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}
    return ~c;
}
struct Blob { std::array<uint8_t,BlobCapacity> bytes{};std::size_t size=0; };
// 4 magic + 2 version + 4 xp + 2 ring indices + 32 * 111 day bytes + 4 CRC.
constexpr std::size_t WireSize=3568;
Blob encode(const State& s) {
    Blob b;
    auto put=[&](uint32_t v,unsigned n){while(n--){b.bytes[b.size++]=uint8_t(v);v>>=8;}};
    put(0x3153504c,4);put(1,2);put(s.xp,4);put(s.next,1);put(s.count,1);
    auto day=[&](const Day& d){put(d.number,4);put(d.flags,2);put(d.expected<0?0xffffffffu:uint32_t(d.expected),4);put(d.actual<0?0xffffffffu:uint32_t(d.actual),4);for(char c:d.note)put(uint8_t(c),1);};
    day(s.today);for(const Day& d:s.history)day(d);
    const auto crc=crc32(b.bytes.data(),b.size);put(crc,4);return b;
}
bool decode(const uint8_t* bytes,std::size_t size,State& out) {
    if(size!=WireSize)return false;
    std::size_t pos=size-4;
    auto get=[&](unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(bytes[pos++])<<(i*8);return v;};
    if(get(4)!=crc32(bytes,size-4))return false;
    pos=0;if(get(4)!=0x3153504c||get(2)!=1)return false;
    State s;s.xp=get(4);s.next=uint8_t(get(1));s.count=uint8_t(get(1));
    bool valid=s.next<HistoryLength&&s.count<=HistoryLength;
    auto day=[&](Day& d){d.number=get(4);d.flags=uint16_t(get(2));
        auto w=[&](){uint32_t v=get(4);if(v==0xffffffffu)return int32_t(-1);if(v==0||v>999999){valid=false;return int32_t(-1);}return int32_t(v);};
        d.expected=w();d.actual=w();for(char& c:d.note)c=char(get(1));
        valid=valid&&d.number>0&&(d.flags&~0x1ffu)==0&&d.note.back()==0;
        bool ended=false;for(char c:d.note){if(!c)ended=true;else if(!ended&&(c<32||c>126))valid=false;}
    };
    day(s.today);for(Day& d:s.history)day(d);
    if(!valid)return false;
    out=s;return true;
}
enum class Screen { Home, Activities, Note, Weight, Tomorrow, Finish, Reward };
class App {
    Platform& hw;
    State state{};
    Screen screen=Screen::Home;
    unsigned selection=0,page=0,gift=0,field=0;
    bool blocked=false;
    std::array<char,NoteLength+1> draft{};
    std::array<std::array<char,8>,2> weights{};
    std::array<char,41> message{};
    uint32_t earned=0;
    uint8_t unlocked=0;
    void say(const char* text){std::snprintf(message.data(),message.size(),"%s",text);}
    void line(int row,const char* text,bool reverse=false){hw.text(0,row*16,text,reverse);}
    void wrap(int row,const char* text,unsigned limit){
        for(unsigned i=0;*text&&i<limit;++i){char b[41]{};std::size_t n=std::strlen(text);if(n>40)n=40;std::memcpy(b,text,n);line(row+int(i),b);text+=n;}
    }
    bool commit(const State& candidate){
        if(blocked){say("Sauvegarde illisible : ecriture bloquee");return false;}
        Blob b=encode(candidate);
        if(!hw.save(b.bytes.data(),b.size)){say("Echec sauvegarde : redemarrer");return false;}
        state=candidate;return true;
    }
    void go(Screen s){screen=s;selection=0;message.fill(0);
        if(s==Screen::Note)draft=state.today.note;
        if(s==Screen::Weight){field=0;for(unsigned i=0;i<2;++i){int32_t v=i?state.today.actual:state.today.expected;weights[i].fill(0);if(v>=0)weightText(v,weights[i].data(),weights[i].size());}}
    }
    template<std::size_t N> void edit(std::array<char,N>& value,int key){
        std::size_t len=std::strlen(value.data());
        if(key==Backspace||key==127){if(len)value[len-1]=0;}
        else if(key>=32&&key<=126&&len<N-1){value[len]=char(key);value[len+1]=0;}
    }
public:
    explicit App(Platform& platform):hw(platform){}
    const State& data()const{return state;}
    Screen currentScreen()const{return screen;}
    void start(){Blob b;auto r=hw.load(b.bytes.data(),b.bytes.size(),b.size);
        if(r==LoadResult::Error||(r==LoadResult::Ok&&!decode(b.bytes.data(),b.size,state))){blocked=true;say("Erreur lecture : sauvegarde protegee");}
        render();
    }
    void key(int k){
        if(k==Escape){go(Screen::Home);render();return;}
        message.fill(0);
        if(screen==Screen::Home){
            if(k==Up)selection=(selection+4)%5;
            if(k==Down)selection=(selection+1)%5;
            if(k>='1'&&k<='5'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){constexpr Screen screens[]={Screen::Activities,Screen::Note,Screen::Weight,Screen::Tomorrow,Screen::Finish};go(screens[selection]);}
        }else if(screen==Screen::Activities){
            unsigned rows=page?4:5;
            if(k==Left||k==Right){page=1-page;selection=0;}
            else if(k==Up)selection=(selection+rows-1)%rows;
            else if(k==Down)selection=(selection+1)%rows;
            else if(k==Enter||(k>='1'&&k<='9')){
                unsigned id=k==Enter?page*5+selection:unsigned(k-'1');State next=state;next.today.flags^=uint16_t(1u<<id);
                if(commit(next)){page=id/5;selection=id%5;}
            }
        }else if(screen==Screen::Note){
            if(k==Enter){State next=state;next.today.note=draft;if(commit(next))go(Screen::Home);}
            else edit(draft,k);
        }else if(screen==Screen::Weight){
            if(k==Up||k==Down||k==9)field=1-field;
            else if(k==Enter){State next=state;
                if(!parseWeight(weights[0].data(),next.today.expected)||!parseWeight(weights[1].data(),next.today.actual))say("Poids invalide (exemple : 110,500)");
                else if(commit(next))go(Screen::Home);
            }else if((k>='0'&&k<='9')||k=='.'||k==','||k==Backspace||k==127)edit(weights[field],k);
        }else if(screen==Screen::Tomorrow){
            if(k==Right||k==Down)gift=(gift+1)%6;
            if(k==Left||k==Up)gift=(gift+5)%6;
        }else if(screen==Screen::Finish){
            if(k==Enter){uint32_t gain=points(state.today);
                if(state.today.number==std::numeric_limits<uint32_t>::max()||state.xp>std::numeric_limits<uint32_t>::max()-gain)say("Limite du compteur atteinte");
                else {State next=state;next.history[next.next]=next.today;next.next=uint8_t((next.next+1)%HistoryLength);if(next.count<HistoryLength)++next.count;
                    next.xp+=gain;next.today=Day{};next.today.number=state.today.number+1;
                    uint8_t won=0;for(unsigned i=0;i<6;++i)if(state.xp<Gifts[i].xp&&next.xp>=Gifts[i].xp)won|=uint8_t(1u<<i);
                    if(commit(next)){earned=gain;unlocked=won;go(Screen::Reward);}
                }
            }
        }else if(screen==Screen::Reward&&k==Enter)go(Screen::Home);
        render();
    }
    void render(){
        hw.clear();char b[80];
        std::snprintf(b,sizeof b,"LPS COMPANION                 J%lu",static_cast<unsigned long>(state.today.number));line(0,b,true);
        std::snprintf(b,sizeof b,"%lu XP acquis",static_cast<unsigned long>(state.xp));line(1,b);
        if(screen==Screen::Home){
            line(3,"AUJOURD'HUI");std::snprintf(b,sizeof b,"%u/9 activites - %lu XP a valider",count(state.today.flags),static_cast<unsigned long>(points(state.today)));line(4,b);
            constexpr const char* menu[]={"1  Activites","2  Note du jour","3  Poids du jour","4  Pour demain","5  Terminer le jour"};
            for(unsigned i=0;i<5;++i)line(6+int(i)*2,menu[i],i==selection);
        }else if(screen==Screen::Activities){
            std::snprintf(b,sizeof b,"ACTIVITES                       %u/2",page+1);line(3,b);
            unsigned end=page?9:5;for(unsigned i=page*5;i<end;++i){std::snprintf(b,sizeof b,"%u  %-24s [%c]",i+1,Activities[i],state.today.flags&(1u<<i)?'x':' ');line(5+int(i-page*5)*2,b,i-page*5==selection);}
            line(16,"1-9 cocher / decocher - <- -> pages");
        }else if(screen==Screen::Note){
            line(3,"NOTE DU JOUR");wrap(5,draft.data(),3);std::snprintf(b,sizeof b,"%zu/96 caracteres",std::strlen(draft.data()));line(10,b);line(14,"Entree : valider / Echap : annuler");
        }else if(screen==Screen::Weight){
            line(3,"POIDS DU JOUR (kg)");std::snprintf(b,sizeof b,"Attendu  : %s",weights[0].data());line(6,b,field==0);std::snprintf(b,sizeof b,"Effectif : %s",weights[1].data());line(9,b,field==1);
            int32_t a=0,c=0;bool valid=parseWeight(weights[0].data(),a)&&parseWeight(weights[1].data(),c)&&a>=0&&c>=0;
            if(valid){int32_t d=c-a,abs=d<0?-d:d;std::snprintf(b,sizeof b,"Ecart : %c%ld.%03ld kg",d<0?'-':'+',long(abs/1000),long(abs%1000));}else std::snprintf(b,sizeof b,"Ecart : --");line(12,b);
            line(14,"Vide = non renseigne");line(15,"Haut/Bas : champ - Entree : valider");
        }else if(screen==Screen::Tomorrow){
            line(3,"POUR DEMAIN");std::snprintf(b,sizeof b,"Souvenir %u/6",gift+1);line(5,b);line(8,Gifts[gift].name);
            if(state.xp>=Gifts[gift].xp)line(11,"OBTENU");else{std::snprintf(b,sizeof b,"Encore %lu XP",static_cast<unsigned long>(Gifts[gift].xp-state.xp));line(11,b);}
            line(15,"<- Precedent              Suivant ->");
        }else if(screen==Screen::Finish){
            line(3,"TERMINER LE JOUR ?");std::snprintf(b,sizeof b,"Activites : %u",count(state.today.flags));line(6,b);std::snprintf(b,sizeof b,"Bonus variete : %u XP",count(state.today.flags)>=3?10:0);line(8,b);std::snprintf(b,sizeof b,"Total : +%lu XP",static_cast<unsigned long>(points(state.today)));line(10,b);line(13,"Entree : enregistrer et avancer");line(15,"Une journee vide ne coute rien.");
        }else{
            line(3,"JOUR ENREGISTRE");std::snprintf(b,sizeof b,"+%lu XP",static_cast<unsigned long>(earned));line(5,b);int row=8;
            for(unsigned i=0;i<6;++i)if(unlocked&(1u<<i)){line(row++,"NOUVEAU SOUVENIR");line(row++,Gifts[i].name);}
            line(15,"Entree : commencer le nouveau jour");
        }
        line(17,message.data());
        line(19,"Haut/Bas  Entree:OK  Echap:Retour",true);hw.present();
    }
};
} // namespace lps

#if defined(LPS_DESKTOP)
#include <cerrno>
#include <string>
#include <iostream>
#include <unistd.h>
class Terminal final:public lps::Platform {
    std::array<std::array<char,41>,20> cells{};
    std::array<bool,20> inverse{};
public:
    void clear()override{for(auto& r:cells){r.fill(' ');r[40]=0;}inverse.fill(false);}
    void text(int x,int y,const char* s,bool reverse)override{
        int row=y/16,col=x/8;if(row<0||row>=20||col<0||col>=40)return;
        inverse[row]=reverse;while(*s&&col<40)cells[row][col++]=*s++;
    }
    void present()override{
        std::printf("\033[2J\033[H");for(unsigned i=0;i<20;++i)std::printf("%s%s\033[0m\n",inverse[i]?"\033[7m":"",cells[i].data());
        std::printf("\nCommands: 1-9, up/down/left/right, enter, esc, back,\ntext YOUR TEXT, quit\n> ");std::fflush(stdout);
    }
    lps::LoadResult load(uint8_t* b,std::size_t cap,std::size_t& n)override{
        FILE* f=std::fopen("lps_companion.sav","rb");if(!f)return errno==ENOENT?lps::LoadResult::Missing:lps::LoadResult::Error;
        n=std::fread(b,1,cap,f);int extra=std::fgetc(f);bool ok=!std::ferror(f)&&extra==EOF;std::fclose(f);return ok?lps::LoadResult::Ok:lps::LoadResult::Error;
    }
    bool save(const uint8_t* b,std::size_t n)override{
        FILE* f=std::fopen("lps_companion.sav.tmp","wb");if(!f)return false;
        bool ok=std::fwrite(b,1,n,f)==n;
        if(std::fflush(f)!=0)ok=false;
        if(::fsync(::fileno(f))!=0)ok=false;
        if(std::fclose(f)!=0)ok=false;
        if(!ok){std::remove("lps_companion.sav.tmp");return false;}
        return std::rename("lps_companion.sav.tmp","lps_companion.sav")==0;
    }
};
int main(){Terminal terminal;lps::App app(terminal);app.start();std::string s;
    while(std::getline(std::cin,s)){
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
class Memory final:public lps::Platform {
public:
    lps::Blob stored{};bool exists=false,fail=false;
    void clear()override{}
    void text(int x,int y,const char*,bool)override{assert(x>=0&&x<320&&y>=0&&y+16<=320);}
    void present()override{}
    lps::LoadResult load(uint8_t* b,std::size_t cap,std::size_t& n)override{if(!exists)return lps::LoadResult::Missing;if(stored.size>cap)return lps::LoadResult::Error;n=stored.size;std::memcpy(b,stored.bytes.data(),n);return lps::LoadResult::Ok;}
    bool save(const uint8_t* b,std::size_t n)override{if(fail)return false;stored.size=n;std::memcpy(stored.bytes.data(),b,n);exists=true;return true;}
};
int main(){
    using namespace lps;int32_t g=0;
    assert(parseWeight("110,500",g)&&g==110500);assert(parseWeight("0.001",g)&&g==1);
    assert(parseWeight("",g)&&g==-1);assert(!parseWeight("0",g));assert(!parseWeight("-1",g));assert(!parseWeight("1.2345",g));assert(!parseWeight("1.",g));assert(!parseWeight("nan",g));
    Memory mem;App a(mem);a.start();a.key('1');a.key('1');a.key('2');a.key('3');assert(points(a.data().today)==40);
    a.key('3');assert(points(a.data().today)==20);a.key('9');assert(a.data().today.flags&(1u<<8));
    State before=a.data();mem.fail=true;a.key('4');assert(a.data().today.flags==before.today.flags);mem.fail=false;
    a.key(Escape);a.key('3');for(char c:std::array<char,5>{'1','1','0',',','5'})a.key(c);a.key(Down);for(char c:std::array<char,5>{'1','1','1','.','2'})a.key(c);a.key(Enter);assert(a.data().today.expected==110500&&a.data().today.actual==111200);
    a.key('2');a.key('O');a.key('K');a.key(Enter);assert(std::strcmp(a.data().today.note.data(),"OK")==0);
    a.key('5');a.key(Enter);assert(a.data().xp==40&&a.data().today.number==2&&a.data().today.flags==0);assert(a.data().history[0].actual==111200);a.key(Enter);assert(a.data().xp==40);
    App restored(mem);restored.start();assert(restored.data().xp==40&&restored.data().today.number==2);
    assert(mem.stored.size==WireSize);State decoded;assert(decode(mem.stored.bytes.data(),mem.stored.size,decoded));assert(!decode(mem.stored.bytes.data(),10,decoded));
    for(int i=0;i<40;++i){restored.key('5');restored.key(Enter);restored.key(Enter);}assert(restored.data().count==31&&restored.data().today.number==42);
    mem.stored.bytes[20]^=1;App corrupt(mem);corrupt.start();auto original=mem.stored;corrupt.key('1');corrupt.key('1');assert(corrupt.data().today.flags==0);assert(mem.stored.bytes==original.bytes);
    std::puts("PASS: weights, toggles, bonus, rollback, notes, closure, restore, ring history, CRC, corrupt-save protection and render bounds.");
}
#endif
