/*
 LPS Companion - v1.8, 2026-09-22.
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
 v1.2 confirmed on the user's PicoCalc; v1.8 needs device testing.
 The portable application remains independent of the Pico SDK; desktop and
 built-in tests below remain available for checking the UI/state machine.

 Behaviour:
 - English default; French selectable in Config, applied on explicit Save.
 - PAL1 red, PAL2 green (default), and PAL3 blue are previewed live and saved on Enter.
 - Format 5 adds a persistent 24-task Kanban; formats 1-4 import safely.
 - Sixteen daily checkboxes; 10 XP each, +10 for >=3 selections (prototype rules).
 - No Journal or collectible browser; new souvenirs still appear after closing a day.
 - Expected/actual weights are OPTIONAL daily inputs, stored as integer grams.
   Comma or dot accepted, up to three decimals. No generated weight-loss target.
 - Note: 96 printable ASCII characters; accents need a font/input extension.
 - Dates are manually set, then progress through the built-in 2000-2099 Gregorian calendar.
 - Closing a dated day produces a portable ICS export on the SD card.
 - Current day autosaves on checkbox changes and explicit form validation.
 - Closing a day banks XP once, unlocks souvenirs and advances the day.
 - The last 31 closed days are retained in a ring buffer, browsable read-only from Home.
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
enum class Language : uint8_t { English, French };
enum class Palette : uint8_t { Red, Green, Blue };
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
constexpr int Width=320, Height=320, Columns=40, Rows=20;
constexpr std::size_t NoteLength=96, HistoryLength=31, TaskNameLength=32, TaskCapacity=24, BlobCapacity=6144;
enum Key { Enter=13, Backspace=8, Escape=27, Up=1000, Down, Left, Right, DeleteKey, F1, F2, F3, F4, F5 };
enum class LoadResult { Missing, Ok, NoStorage, Error };
struct Day;
struct Platform {
    virtual ~Platform() = default;
    virtual void clear()=0;
    // Coordinates in pixels. reverse=true fills all 40 cells on that row.
    virtual void text(int x,int y,const char* text,bool reverse)=0;
    // A hardware-visible error line (bold red on PicoCalc). Used for faults
    // which need immediate attention rather than normal status text.
    virtual void alert(int x,int y,const char* text)=0;
    // Hardware may override this. Host/test platforms report a healthy battery.
    virtual bool batteryBelow20(){return false;}
    virtual void present()=0;
    // Apply the selected theme before drawing. The desktop implementation may ignore it.
    virtual void setPalette(Palette)=0;
    // Return Error for truncated/oversized/inaccessible saves, Missing only if absent.
    virtual LoadResult load(uint8_t* bytes,std::size_t capacity,std::size_t& size)=0;
    // On false, RAM is rolled back; an ambiguous late commit MUST block further
    // writes until restart. The prior valid record must be retained. On true, replacement must be
    // recoverable after restart. FatFs: use validated A/B slots with generations,
    // f_sync and startup recovery, or an equivalent transactional scheme.
    virtual bool save(const uint8_t* bytes,std::size_t size)=0;
    // Export first, then save the A/B state. Retrying replaces the same named
    // export if the snapshot write later fails; no duplicate calendar event.
    virtual bool exportIcs(const Day& day,uint32_t totalXp,uint32_t dayXp,Language language)=0;
    // The yearly weight journal is exported before the state commit as well.
    // Implementations must treat a repeated date as already exported.
    virtual bool exportWeightCsv(const Day& day)=0;
};
constexpr const char* Activities[]={"Marche","Régime","Bible","Messe","Travail","Projet","Sieste","Gurumed","Maladie","Congé","Weekend","Sortie","Jeu","Docteur","Lecture","Shopping"};
constexpr const char* EnglishActivities[]={"Walk","Diet","Bible","Mass","Work","Project","Nap","Gurumed","Illness","Day off","Weekend","Outing","Gaming","Doctor","Reading","Shopping"};
constexpr unsigned ActivityCount=sizeof Activities/sizeof Activities[0];
constexpr unsigned PageSize=10, PageCount=(ActivityCount+PageSize-1)/PageSize;
static_assert(ActivityCount<=16,"Activity flags require more than 16 bits");
constexpr uint16_t ActivityMask=(1u<<ActivityCount)-1;
struct Gift { const char* name; uint32_t xp; };
constexpr Gift Gifts[]={{"Carnet de poche",30},{"Tasse de thé",70},{"Boussole",120},{"Radio de poche",180},{"Mini-ordinateur",250},{"Lanterne",330}};
constexpr const char* EnglishGifts[]={"Pocket notebook","Cup of tea","Compass","Pocket radio","Mini computer","Lantern"};
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
    uint16_t flags=0;
    int32_t expected=-1, actual=-1; // grams; -1 is missing
    std::array<char,NoteLength+1> note{};
};
enum class TaskStatus : uint8_t { Todo, Doing, Done };
struct Task {
    TaskStatus status=TaskStatus::Todo;
    std::array<char,TaskNameLength+1> name{};
};
struct State {
    Language language=Language::English;
    Palette palette=Palette::Green;
    uint32_t xp=0;
    Day today{};
    std::array<Day,HistoryLength> history{};
    uint8_t next=0, count=0;
    uint8_t taskCount=0;
    std::array<Task,TaskCapacity> tasks{};
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
// Format 1: v1 base. Format 2: adds language. Format 3: adds dates.
// Format 4: adds palette after language. Format 5: adds the persistent Kanban.
constexpr std::size_t WireV1=3568, WireV2=WireV1+1, WireV3=WireV2+32*4, WireV4=WireV3+1;
constexpr std::size_t WireSize=WireV4+1+TaskCapacity*(1+TaskNameLength+1);
Blob encode(const State& s) {
    Blob b;
    auto put=[&](uint32_t v,unsigned n){while(n--){b.bytes[b.size++]=uint8_t(v);v>>=8;}};
    put(0x3153504c,4);put(5,2);put(s.xp,4);put(s.next,1);put(s.count,1);put(uint8_t(s.language),1);put(uint8_t(s.palette),1);
    auto day=[&](const Day& d){put(d.date.year,2);put(d.date.month,1);put(d.date.day,1);put(d.number,4);put(d.flags,2);put(d.expected<0?0xffffffffu:uint32_t(d.expected),4);put(d.actual<0?0xffffffffu:uint32_t(d.actual),4);for(char c:d.note)put(uint8_t(c),1);};
    day(s.today);for(const Day& d:s.history)day(d);
    put(s.taskCount,1);for(const Task& task:s.tasks){put(uint8_t(task.status),1);for(char c:task.name)put(uint8_t(c),1);}
    const auto crc=crc32(b.bytes.data(),b.size);put(crc,4);return b;
}
bool decode(const uint8_t* bytes,std::size_t size,State& out) {
    if(size!=WireSize&&size!=WireV4&&size!=WireV3&&size!=WireV2&&size!=WireV1)return false;
    std::size_t pos=size-4;
    auto get=[&](unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(bytes[pos++])<<(i*8);return v;};
    if(get(4)!=crc32(bytes,size-4))return false;
    pos=0;if(get(4)!=0x3153504c)return false;
    const auto version=get(2);
    if(!((version==1&&size==WireV1)||(version==2&&size==WireV2)||(version==3&&size==WireV3)||(version==4&&size==WireV4)||(version==5&&size==WireSize)))return false;
    State s;s.xp=get(4);s.next=uint8_t(get(1));s.count=uint8_t(get(1));
    if(version>=2){const auto language=get(1);if(language>1)return false;s.language=Language(language);}
    if(version>=4){const auto palette=get(1);if(palette>2)return false;s.palette=Palette(palette);}
    bool valid=s.next<HistoryLength&&s.count<=HistoryLength;
    auto day=[&](Day& d){d.date={};if(version>=3){d.date.year=uint16_t(get(2));d.date.month=uint8_t(get(1));d.date.day=uint8_t(get(1));}
        d.number=get(4);d.flags=uint16_t(get(2));
        auto w=[&](){uint32_t v=get(4);if(v==0xffffffffu)return int32_t(-1);if(v==0||v>999999){valid=false;return int32_t(-1);}return int32_t(v);};
        d.expected=w();d.actual=w();for(char& c:d.note)c=char(get(1));
        valid=valid&&d.number>0&&(d.flags&~ActivityMask)==0&&d.note.back()==0&&(d.date.year==0||validDate(d.date));
        bool ended=false;for(char c:d.note){if(!c)ended=true;else if(!ended&&(c<32||c>126))valid=false;}
    };
    day(s.today);for(Day& d:s.history)day(d);
    if(version>=5){
        s.taskCount=uint8_t(get(1));valid=valid&&s.taskCount<=TaskCapacity;
        for(unsigned i=0;i<TaskCapacity;++i){
            const auto status=get(1);if(status>2)valid=false;s.tasks[i].status=TaskStatus(status);
            for(char& c:s.tasks[i].name)c=char(get(1));
            valid=valid&&s.tasks[i].name.back()==0;
            bool ended=false;for(char c:s.tasks[i].name){if(!c)ended=true;else if(!ended&&(c<32||c>126))valid=false;}
            if(i<s.taskCount&&s.tasks[i].name[0]==0)valid=false;
        }
    }
    if(!valid)return false;
    out=s;return true;
}
enum class Screen { Home, Date, Activities, Note, Weight, Kanban, TaskEdit, Finish, Reward, Config, Language, Colors,
                    RosaryMode, RosarySet, RosaryDecade, RosaryPrayer, RosaryComplete };
class App {
    Platform& hw;
    State state{};
    Screen screen=Screen::Home;
    unsigned selection=0,page=0,field=0,daysBack=0;
    bool blocked=false;
    Language pendingLanguage=Language::English;
    Palette pendingPalette=Palette::Green;
    std::array<char,11> dateDraft{};
    std::array<char,NoteLength+1> draft{};
    std::array<char,TaskNameLength+1> taskDraft{};
    std::array<std::array<char,8>,2> weights{};
    std::array<char,100> message{};
    uint32_t earned=0;
    uint8_t unlocked=0;
    bool missingStorage=false;
    TaskStatus kanbanColumn=TaskStatus::Todo;
    unsigned editedTask=TaskCapacity;
    RosaryMode rosaryMode=RosaryMode::Decade;
    MysterySet rosarySet=MysterySet::Joyful;
    unsigned rosaryDecade=0,rosaryStep=0,rosaryPage=0;
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
    void line(int row,const char* text,bool reverse=false){char cells[100];displayText(text,cells,sizeof cells);hw.text(0,row*16,cells,reverse);}
    void wrap(int row,const char* text,unsigned limit){
        for(unsigned i=0;*text&&i<limit;++i){char b[41]{};std::size_t n=std::strlen(text);if(n>40)n=40;std::memcpy(b,text,n);line(row+int(i),b);text+=n;}
    }
    bool commit(const State& candidate){
        if(daysBack){readOnly();return false;}
        if(blocked){say(tr("Unreadable save: writes blocked","Sauvegarde illisible : écriture bloquée"));return false;}
        Blob b=encode(candidate);
        if(!hw.save(b.bytes.data(),b.size)){say(tr("Save failed: restart device","Échec sauvegarde : redémarrer"));return false;}
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
    void beginTaskEdit(unsigned index){
        editedTask=index;taskDraft.fill(0);if(index<TaskCapacity)taskDraft=state.tasks[index].name;go(Screen::TaskEdit);
    }
    void moveTaskTo(TaskStatus target){
        const unsigned n=tasksIn(kanbanColumn),index=n?taskAt(kanbanColumn,selection):TaskCapacity;
        if(index>=state.taskCount)return;
        if(state.tasks[index].status==target){say(tr("Task is already in this column","Tâche déjà dans cette colonne"));return;}
        State next=state;Task moved=next.tasks[index];moved.status=target;
        for(unsigned i=index;i+1<next.taskCount;++i)next.tasks[i]=next.tasks[i+1];
        next.tasks[next.taskCount-1]=moved;
        if(commit(next)){kanbanColumn=target;selection=tasksIn(target)-1;say(tr("Task moved","Tâche déplacée"));}
    }
    void reorderTask(bool downward){
        const unsigned n=tasksIn(kanbanColumn);if(n<2)return;
        std::array<unsigned,TaskCapacity> indices{};for(unsigned i=0;i<n;++i)indices[i]=taskAt(kanbanColumn,i);
        State next=state;unsigned destination=selection;
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
        if(s==Screen::Language)selection=unsigned(pendingLanguage);
        if(s==Screen::Colors)selection=unsigned(pendingPalette);
        if(s==Screen::Note)draft=viewedDay().note;
        if(s==Screen::Weight){field=0;for(unsigned i=0;i<2;++i){int32_t v=i?viewedDay().actual:viewedDay().expected;weights[i].fill(0);if(v>=0)weightText(v,weights[i].data(),weights[i].size());}}
        if(s==Screen::Date){dateDraft.fill(0);Date d=validDate(state.today.date)?state.today.date:DefaultDate;dateText(d,dateDraft.data(),dateDraft.size());}
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
    const Day& viewedDay()const{
        // next is the slot AFTER the most recently closed day. Never index by
        // day number: physical slots wrap every 31 closures.
        return daysBack?state.history[(state.next+HistoryLength-daysBack)%HistoryLength]:state.today;
    }
    void start(){Blob b;auto r=hw.load(b.bytes.data(),b.bytes.size(),b.size);
        // The language preference is on the SD card too, so a cold start with no
        // card deliberately uses the English default rather than guessing.
        if(r==LoadResult::NoStorage){blocked=true;missingStorage=true;say("[!] Missing SD card!");}
        else if(r==LoadResult::Error||(r==LoadResult::Ok&&!decode(b.bytes.data(),b.size,state))){blocked=true;say(tr("Read error: save protected","Erreur lecture : sauvegarde protégée"));}
        render();
    }
    void key(int k){
        if(k==Escape){
            Screen target=Screen::Home;
            if(screen==Screen::Language||screen==Screen::Colors)target=Screen::Config;
            else if(screen==Screen::TaskEdit)target=Screen::Kanban;
            else if(screen==Screen::RosarySet)target=Screen::RosaryMode;
            else if(screen==Screen::RosaryDecade)target=Screen::RosarySet;
            go(target);render();return;
        }
        message.fill(0);
        if(screen==Screen::Home){
            if(k==Left){
                if(daysBack<state.count){++daysBack;selection=0;page=0;}
                else say(state.count?tr("Oldest saved day","Plus ancien jour conservé"):tr("No archived days","Aucun jour archivé"));
            }
            if(k==Right){daysBack=0;selection=0;page=0;}
            const unsigned menuCount=daysBack?3:8;
            if(k==Up)selection=(selection+menuCount-1)%menuCount;
            if(k==Down)selection=(selection+1)%menuCount;
            if(!daysBack&&k=='0'){selection=0;k=Enter;}
            else if(daysBack&&k>='1'&&k<='3'){selection=unsigned(k-'1');k=Enter;}
            else if(!daysBack&&k>='1'&&k<='7'){selection=unsigned(k-'0');k=Enter;}
            if((k=='0'||(k>='4'&&k<='7'))&&daysBack)readOnly();
            if(k==Enter){
                constexpr Screen currentScreens[]={Screen::Date,Screen::Activities,Screen::Note,Screen::Weight,Screen::Kanban,Screen::Config,Screen::Finish,Screen::RosaryMode};
                constexpr Screen archiveScreens[]={Screen::Activities,Screen::Note,Screen::Weight};
                const Screen target=daysBack?archiveScreens[selection]:currentScreens[selection];
                if(target==Screen::Kanban)kanbanColumn=TaskStatus::Todo;
                if(target==Screen::Config){pendingLanguage=state.language;pendingPalette=state.palette;}
                go(target);}
        }else if(screen==Screen::Date){
            if(k==Enter){State next=state;Date d{};
                if(!parseDate(dateDraft.data(),d))say(tr("Invalid date: YYYY-MM-DD, 2000-2099","Date invalide : AAAA-MM-JJ, 2000-2099"));
                else {next.today.date=d;if(commit(next))go(Screen::Home);}
            }else if((k>='0'&&k<='9')||k=='-'||k==Backspace||k==127)edit(dateDraft,k);
        }else if(screen==Screen::Activities){
            unsigned remaining=ActivityCount-page*PageSize;
            unsigned rows=remaining<PageSize?remaining:PageSize;
            if(k==Left||k==Right){page=(page+(k==Right?1:PageCount-1))%PageCount;selection=0;}
            else if(k==Up)selection=(selection+rows-1)%rows;
            else if(k==Down)selection=(selection+1)%rows;
            else if(daysBack&&(k==Enter||(k>='1'&&k<='9')))readOnly();
            else if(k==Enter||(k>='1'&&k<='9')){
                unsigned id=k==Enter?page*PageSize+selection:unsigned(k-'1');State next=state;next.today.flags^=uint16_t(1u<<id);
                if(commit(next)){page=id/PageSize;selection=id%PageSize;}
            }
        }else if(screen==Screen::Note){
            if(daysBack){if(k==Enter)go(Screen::Home);else readOnly();}
            else if(k==Enter){State next=state;next.today.note=draft;if(commit(next))go(Screen::Home);}
            else edit(draft,k);
        }else if(screen==Screen::Weight){
            if(daysBack){if(k==Enter)go(Screen::Home);else readOnly();}
            else if(k==Up||k==Down||k==9)field=1-field;
            else if(k==Enter){State next=state;
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
                const unsigned index=taskAt(kanbanColumn,selection);State next=state;
                for(unsigned i=index;i+1<next.taskCount;++i)next.tasks[i]=next.tasks[i+1];
                next.tasks[--next.taskCount]=Task{};
                if(commit(next)&&selection>=tasksIn(kanbanColumn)&&selection) --selection;
            }else if(k>=F1&&k<=F3&&n)moveTaskTo(TaskStatus(unsigned(k-F1)));
            else if(k==F4&&n)reorderTask(false);
            else if(k==F5&&n)reorderTask(true);
        }else if(screen==Screen::TaskEdit){
            if(k==Enter){
                if(!taskDraft[0])say(tr("Task name cannot be empty","Le nom ne peut pas être vide"));
                else {State next=state;
                    unsigned destination=editedTask;
                    if(editedTask<TaskCapacity)next.tasks[editedTask].name=taskDraft;
                    else if(next.taskCount>=TaskCapacity){say(tr("Kanban is full (24 tasks)","Kanban complet (24 tâches)"));render();return;}
                    else {destination=next.taskCount;next.tasks[next.taskCount].status=kanbanColumn;next.tasks[next.taskCount].name=taskDraft;++next.taskCount;}
                    if(commit(next)){editedTask=destination;selection=taskPosition(kanbanColumn,editedTask);screen=Screen::Kanban;message.fill(0);}
                }
            }else edit(taskDraft,k);
        }else if(screen==Screen::Config){
            if(k==Up)selection=(selection+2)%3;
            if(k==Down)selection=(selection+1)%3;
            if(k>='1'&&k<='3'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){
                if(selection==0)go(Screen::Language);
                else if(selection==1)go(Screen::Colors);
                else {State next=state;next.language=pendingLanguage;
                    if(commit(next)){go(Screen::Home);say(tr("Configuration saved","Configuration enregistrée"));}}
            }
        }else if(screen==Screen::Language){
            if(k==Up||k==Down)selection=1-selection;
            if(k=='1'||k=='2'){selection=unsigned(k-'1');k=Enter;}
            if(k==Enter){pendingLanguage=Language(selection);go(Screen::Config);}
        }else if(screen==Screen::Colors){
            if(k==Up)selection=(selection+2)%3;
            if(k==Down)selection=(selection+1)%3;
            if(k>='1'&&k<='3')selection=unsigned(k-'1');
            pendingPalette=Palette(selection);
            if(k==Enter){State next=state;next.palette=pendingPalette;
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
                else {State next=state;next.history[next.next]=next.today;next.next=uint8_t((next.next+1)%HistoryLength);if(next.count<HistoryLength)++next.count;
                    Date tomorrow=state.today.date;if(!nextDate(tomorrow)) {say(tr("Calendar ends on 2099-12-31","Le calendrier s'arrête au 2099-12-31"));render();return;}
                    // The export is deliberately written before the A/B commit.
                    // A retry overwrites the same dated file if the snapshot fails.
                    if(!hw.exportWeightCsv(state.today)){say(tr("Weight CSV export failed; day remains open","Export CSV poids échoué ; journée reste ouverte"));render();return;}
                    if(!hw.exportIcs(state.today,state.xp+gain,gain,state.language)){say(tr("ICS export failed; day remains open","Export ICS échoué ; journée reste ouverte"));render();return;}
                    next.xp+=gain;next.today=Day{};next.today.number=state.today.number+1;next.today.date=tomorrow;
                    uint8_t won=0;for(unsigned i=0;i<6;++i)if(state.xp<Gifts[i].xp&&next.xp>=Gifts[i].xp)won|=uint8_t(1u<<i);
                    if(commit(next)){earned=gain;unlocked=won;go(Screen::Reward);}
                }
            }
        }else if(screen==Screen::Reward&&k==Enter)go(Screen::Home);
        render();
    }
    void render(){
        hw.setPalette(displayedPalette());hw.clear();char b[128];const Day& day=viewedDay();char date[11]{};
        if(validDate(day.date))dateText(day.date,date,sizeof date);else std::snprintf(date,sizeof date,"%s%lu",tr("D","J"),static_cast<unsigned long>(day.number));
        std::snprintf(b,sizeof b,"LPS COMPANION v1.8            %s",date);line(0,b,true);
        std::snprintf(b,sizeof b,tr("%lu XP earned","%lu XP acquis"),static_cast<unsigned long>(state.xp));line(1,b);
        if(daysBack)line(2,tr("ARCHIVED DAY - READ ONLY","JOUR ARCHIVÉ - LECTURE SEULE"));
        if(screen==Screen::Home){
            line(3,daysBack?tr("CLOSED DAY","JOUR TERMINÉ"):tr("TODAY","AUJOURD'HUI"));
            std::snprintf(b,sizeof b,tr("%u/%u activities - %lu XP %s","%u/%u activités - %lu XP %s"),count(day.flags),ActivityCount,static_cast<unsigned long>(points(day)),daysBack?tr("banked","validés"):tr("pending","à valider"));line(4,b);
            const char* menu[]={"0  Date",tr("1  Activities","1  Activités"),tr("2  Notepad","2  Bloc notes"),tr("3  Weight tracker","3  Suivi du poids"),"4  Kanban","5  Config",tr("6  Close the day","6  Terminer le jour"),tr("7  Christian Rosary","7  Rosaire chrétien")};
            const unsigned first=daysBack?1:0, total=daysBack?3:8;
            for(unsigned i=0;i<total;++i)line(6+int(i),menu[first+i],i==selection);
            line(16,tr("<- Previous day   -> Current day","<- Jour précédent   -> Jour actuel"));
        }else if(screen==Screen::Date){
            line(3,"DATE");line(6,dateDraft.data(),true);
            line(9,tr("Calendar: 2000-01-01 to 2099-12-31","Calendrier : 2000-01-01 au 2099-12-31"));
            line(12,tr("Enter: save / Esc: cancel","Entrée : valider / Échap : annuler"));
            line(14,tr("Next day is suggested after closing.","Jour suivant proposé après clôture."));
        }else if(screen==Screen::Activities){
            std::snprintf(b,sizeof b,tr("ACTIVITIES                      %u/%u","ACTIVITÉS                       %u/%u"),page+1,PageCount);line(3,b);
            unsigned end=(page+1)*PageSize;if(end>ActivityCount)end=ActivityCount;
            for(unsigned i=page*PageSize;i<end;++i){
                // Pad by display cells, not UTF-8 bytes, to align checkboxes.
                char label[32];displayText(tr(EnglishActivities[i],Activities[i]),label,sizeof label);
                std::snprintf(b,sizeof b,"%2u  %-24s [%c]",i+1,label,day.flags&(1u<<i)?'x':' ');
                hw.text(0,(5+int(i-page*PageSize))*16,b,i-page*PageSize==selection);
            }
            line(15,daysBack?tr("<- -> pages   Esc: day menu","<- -> pages   Échap : menu du jour"):tr("Enter: toggle  1-9: keys  <- -> pages","Entrée: cocher  1-9: racc.  <- -> pages"));
        }else if(screen==Screen::Note){
            line(3,tr("NOTEPAD","BLOC NOTES"));wrap(5,draft.data(),3);std::snprintf(b,sizeof b,tr("%zu/96 characters","%zu/96 caractères"),std::strlen(draft.data()));line(10,b);
            line(14,daysBack?tr("Enter / Esc: day menu","Entrée / Échap : menu du jour"):tr("Enter: save / Esc: cancel","Entrée : valider / Échap : annuler"));
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
            line(11,tr("Enter: save color","Entrée : enregistrer couleur"));
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
            line(3,tr("CLOSE THE DAY?","TERMINER LE JOUR ?"));std::snprintf(b,sizeof b,tr("Activities: %u","Activités : %u"),count(state.today.flags));line(6,b);std::snprintf(b,sizeof b,tr("Variety bonus: %u XP","Bonus variété : %u XP"),count(state.today.flags)>=3?10:0);line(8,b);std::snprintf(b,sizeof b,"Total : +%lu XP",static_cast<unsigned long>(points(state.today)));line(10,b);line(13,tr("Enter: save and advance","Entrée : enregistrer et avancer"));line(15,tr("An empty day has no penalty.","Une journée vide ne coûte rien."));
        }else{
            line(3,tr("DAY SAVED","JOUR ENREGISTRÉ"));std::snprintf(b,sizeof b,"+%lu XP",static_cast<unsigned long>(earned));line(5,b);int row=8;
            for(unsigned i=0;i<6;++i)if(unlocked&(1u<<i)){line(row++,tr("NEW KEEPSAKE","NOUVEAU SOUVENIR"));line(row++,tr(EnglishGifts[i],Gifts[i].name));}
            line(15,tr("Enter: start the new day","Entrée : commencer le nouveau jour"));
        }
        if(missingStorage)hw.alert(0,17*16,message.data());else line(17,message.data());
        if(hw.batteryBelow20())hw.alert(0,18*16,tr("[!] Low battery!","[!] Batterie faible !"));
        line(19,tr("Up/Down  Enter:OK  Esc:Back","Haut/Bas  Entrée:OK  Échap:Retour"),true);hw.present();
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
    bool exportIcs(const lps::Day& day,uint32_t totalXp,uint32_t dayXp,lps::Language)override{
        // Oversized date buffers keep strict host builds independent of range inference.
        char name[48],ymd[13],tomorrow[13];lps::dateText(day.date,name,sizeof name);
        std::snprintf(ymd,sizeof ymd,"%04u%02u%02u",day.date.year,day.date.month,day.date.day);
        auto next=day.date;if(!lps::nextDate(next))return false;std::snprintf(tomorrow,sizeof tomorrow,"%04u%02u%02u",next.year,next.month,next.day);
        std::snprintf(name,sizeof name,"%04u-%02u-%02u_LPS-Companion.ics",day.date.year,day.date.month,day.date.day);
        FILE* f=std::fopen(name,"wb");if(!f)return false;
        int result=std::fprintf(f,"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//LPS Companion//EN\r\nBEGIN:VEVENT\r\nUID:lps-companion-%s@desktop\r\nDTSTART;VALUE=DATE:%s\r\nDTEND;VALUE=DATE:%s\r\nSUMMARY:LPS Companion\r\nDESCRIPTION:XP: +%lu (total %lu)\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",ymd,ymd,tomorrow,static_cast<unsigned long>(dayXp),static_cast<unsigned long>(totalXp));
        return result>0&&std::fclose(f)==0;
    }
    bool exportWeightCsv(const lps::Day&)override{return true;}
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
    lps::Blob stored{};bool exists=false,fail=false,failExport=false,failWeightCsv=false,noStorage=false,lowBattery=false;unsigned exports=0,weightExports=0;
    std::array<std::array<char,41>,20> rows{};
    std::array<bool,20> alerts{};
    lps::Palette palette=lps::Palette::Green;
    void setPalette(lps::Palette p)override{palette=p;}
    void alert(int x,int y,const char* text)override{alerts[y/16]=true;this->text(x,y,text,true);}
    bool batteryBelow20()override{return lowBattery;}
    void clear()override{}
    void text(int x,int y,const char* text,bool)override{assert(x>=0&&x<320&&y>=0&&y+16<=320);std::snprintf(rows[y/16].data(),41,"%s",text);}
    void present()override{}
    lps::LoadResult load(uint8_t* b,std::size_t cap,std::size_t& n)override{if(noStorage)return lps::LoadResult::NoStorage;if(!exists)return lps::LoadResult::Missing;if(stored.size>cap)return lps::LoadResult::Error;n=stored.size;std::memcpy(b,stored.bytes.data(),n);return lps::LoadResult::Ok;}
    bool save(const uint8_t* b,std::size_t n)override{if(fail)return false;stored.size=n;std::memcpy(stored.bytes.data(),b,n);exists=true;return true;}
    bool exportIcs(const lps::Day&,uint32_t,uint32_t,lps::Language)override{if(failExport)return false;++exports;return true;}
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
    historyState.next=1;historyState.count=1;history.stored=encode(historyState);history.exists=true;
    App browsing(history);browsing.start();browsing.key(Left);
    assert(browsing.viewedDay().number==1);
    browsing.key('2');assert(std::strcmp(history.rows[5].data(),"Yesterday only")==0);
    browsing.key(Escape);browsing.key('3');
    assert(std::strstr(history.rows[6].data(),"110.500"));
    assert(std::strstr(history.rows[9].data(),"111.200"));
    assert(std::strstr(history.rows[12].data(),"+0.700"));
    browsing.key(Escape);browsing.key('1');browsing.key(Right);
    assert(std::strstr(history.rows[5].data(),"Weekend")&&std::strstr(history.rows[5].data(),"[x]"));
    browsing.key(Escape);browsing.key(Right);browsing.key('3');
    assert(std::strstr(history.rows[6].data(),"120.000"));
    Memory pages;App nav(pages);nav.start();nav.key('1');
    for(int i=0;i<9;++i)nav.key(Down);
    nav.key(Enter);
    assert(nav.data().today.flags==(1u<<9));
    nav.key(Right);nav.key(Enter);
    assert(nav.data().today.flags==((1u<<9)|(1u<<10)));
    App reload(pages);reload.start();assert(reload.data().today.flags==nav.data().today.flags);
    nav.key('1');assert(nav.data().today.flags&1);
    nav.key(Right);nav.key(Enter);assert(!(nav.data().today.flags&(1u<<10)));
    nav.key(Escape);nav.key('4');assert(nav.currentScreen()==Screen::Kanban);nav.key(Escape);
    nav.key('6');assert(nav.currentScreen()==Screen::Finish);
    nav.key(Escape);nav.key('5');assert(nav.currentScreen()==Screen::Config);nav.key('2');assert(nav.currentScreen()==Screen::Colors);
    assert(pages.palette==Palette::Green);nav.key(Down);assert(pages.palette==Palette::Blue);nav.key(Escape);assert(pages.palette==Palette::Green);
    nav.key('2');nav.key('1');nav.key(Enter);assert(nav.currentScreen()==Screen::Home&&nav.data().palette==Palette::Red&&pages.palette==Palette::Red);
    App paletteReload(pages);paletteReload.start();assert(paletteReload.data().palette==Palette::Red);
    State old;old.today.flags=0x1ff;old.today.expected=110500;old.xp=70;
    Blob legacy=encode(old);State migrated;assert(decode(legacy.bytes.data(),legacy.size,migrated));
    assert(migrated.today.flags==0x1ff&&migrated.today.expected==110500&&migrated.xp==70);
    // A real v1.4 payload has language then dates but no palette byte.
    Blob v3;v3.size=WireV3;std::memcpy(v3.bytes.data(),legacy.bytes.data(),13);v3.bytes[4]=3;
    std::memcpy(v3.bytes.data()+13,legacy.bytes.data()+14,WireV3-17);
    auto v3crc=crc32(v3.bytes.data(),v3.size-4);for(unsigned i=0;i<4;++i)v3.bytes[v3.size-4+i]=uint8_t(v3crc>>(8*i));
    assert(decode(v3.bytes.data(),v3.size,migrated)&&migrated.palette==Palette::Green&&migrated.today.flags==old.today.flags);
    Blob v4;v4.size=WireV4;std::memcpy(v4.bytes.data(),legacy.bytes.data(),WireV4-4);v4.bytes[4]=4;
    auto v4crc=crc32(v4.bytes.data(),v4.size-4);for(unsigned i=0;i<4;++i)v4.bytes[v4.size-4+i]=uint8_t(v4crc>>(8*i));
    assert(decode(v4.bytes.data(),v4.size,migrated)&&migrated.taskCount==0&&migrated.palette==Palette::Green);
    Memory missing;missing.noStorage=true;App noCard(missing);noCard.start();assert(std::strstr(missing.rows[17].data(),"Missing SD card"));
    Memory battery;battery.lowBattery=true;App lowPower(battery);lowPower.start();assert(battery.alerts[18]&&std::strstr(battery.rows[18].data(),"Low battery"));
    Memory mem;App a(mem);a.start();a.key('1');a.key('1');a.key('2');a.key('3');assert(points(a.data().today)==40);
    a.key('3');assert(points(a.data().today)==20);a.key('9');assert(a.data().today.flags&(1u<<8));
    State before=a.data();mem.fail=true;a.key('4');assert(a.data().today.flags==before.today.flags);mem.fail=false;
    a.key(Escape);a.key('3');for(char c:std::array<char,5>{'1','1','0',',','5'})a.key(c);a.key(Down);for(char c:std::array<char,5>{'1','1','1','.','2'})a.key(c);a.key(Enter);assert(a.data().today.expected==110500&&a.data().today.actual==111200);
    a.key('2');a.key('O');a.key('K');a.key(Enter);assert(std::strcmp(a.data().today.note.data(),"OK")==0);
    a.key('6');a.key(Enter);assert(a.data().xp==40&&a.data().today.number==2&&a.data().today.flags==0);assert(a.data().history[0].actual==111200);a.key(Enter);assert(a.data().xp==40);
    App restored(mem);restored.start();assert(restored.data().xp==40&&restored.data().today.number==2);
    assert(mem.stored.size==WireSize);State decoded;assert(decode(mem.stored.bytes.data(),mem.stored.size,decoded));assert(!decode(mem.stored.bytes.data(),10,decoded));
    for(int i=0;i<40;++i){restored.key('6');restored.key(Enter);restored.key(Enter);}assert(restored.data().count==31&&restored.data().today.number==42);
    mem.stored.bytes[20]^=1;App corrupt(mem);corrupt.start();auto original=mem.stored;corrupt.key('1');corrupt.key('1');assert(corrupt.data().today.flags==0);assert(mem.stored.bytes==original.bytes);
    std::puts("PASS: weights, toggles, bonus, rollback, notes, closure, restore, ring history, CRC, palette preview/persistence, corrupt-save protection and render bounds.");
}
#endif
