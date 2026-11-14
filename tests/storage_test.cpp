// Exercise the SAME A/B adapter compiled into the Pico firmware.
#define main pico_firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <string>
// Whole file content, to prove read-only paths leave both slots byte-for-byte.
std::string fileBytes(const char* name){
    std::string out;FILE* f=std::fopen(name,"rb");if(!f)return out;
    char b[512];size_t n;while((n=std::fread(b,1,sizeof b,f))>0)out.append(b,n);std::fclose(f);return out;
}
lps::State probe;
Slot slot(unsigned n){return readSlot(n,probe);}
unsigned char fontStub[4+224*12]{};
extern "C" {
unsigned char* MainFont=fontStub;
void draw_bitmap_spi(int,int,int,int,int,int,int,unsigned char*){}
void draw_rect_spi(int,int,int,int,int){}
void draw_buffer_spi(int,int,int,int,unsigned char*){}
}
lps::Day sample(unsigned n){
    lps::Day d;d.number=n;d.flags=uint16_t(n%2048);
    d.expected=100000+int(n);d.actual=101000+int(n);
    std::snprintf(d.note.data(),d.note.size(),"Distinct note for day %u",n);return d;
}
void checkDay(const lps::Day& d,unsigned n){
    const auto expected=sample(n);
    assert(d.number==n&&d.flags==expected.flags&&d.expected==expected.expected&&d.actual==expected.actual&&d.note==expected.note);
}
int main(){
    using namespace lps;
    assert(keymap(0x81)==F1&&keymap(0x82)==F2&&keymap(0x83)==F3&&keymap(0x84)==F4&&keymap(0x85)==F5&&keymap(0xd4)==DeleteKey);
    std::remove("SAVE_A.JSN");std::remove("SAVE_B.JSN");
    PicoCalc disk;disk.init();App initial(disk);initial.start();
    initial.key(Left);assert(initial.viewedDay().number==1);
    assert(mockWrites==0); // Empty history does not create files.
    State state;
    for(unsigned n=1;n<=40;++n){
        state.history[state.next]=sample(n);state.next=(state.next+1)%HistoryLength;
        if(state.count<HistoryLength)++state.count;
        state.today=sample(n+1);state.xp=n*10;
        assert(disk.save(state));
    }
    Slot a=slot(0),b=slot(1);assert(a.status==Slot::Valid&&b.status==Slot::Valid);
    const std::string fileA=fileBytes("SAVE_A.JSN"),fileB=fileBytes("SAVE_B.JSN");
    auto writes=mockWrites;
    {
        PicoCalc reboot;reboot.init();App app(reboot);app.start();checkDay(app.viewedDay(),41);
        for(unsigned n=40;n>=10;--n){
            app.key(Left);checkDay(app.viewedDay(),n);
            // Attempt edits in every archived screen; no writes or XP changes.
            app.key('1');app.key('1');app.key(Enter);app.key(Right);app.key(Escape);
            app.key('2');app.key('9');app.key(Backspace);app.key(Enter);
            app.key('3');assert(app.currentScreen()==Screen::Home);
            checkDay(app.viewedDay(),n);assert(app.data().xp==400);
        }
        app.key(Left);checkDay(app.viewedDay(),10); // Oldest retained boundary.
        app.key(Right);checkDay(app.viewedDay(),41); // Direct return, not one day forward.
        app.key(Right);checkDay(app.viewedDay(),41);
        assert(mockWrites==writes);
        assert(fileBytes("SAVE_A.JSN")==fileA&&fileBytes("SAVE_B.JSN")==fileB);
        app.key('1');app.key('1');app.key(Escape);assert(mockWrites>writes); // JSON is streamed in several f_write calls.
        assert(app.data().today.flags==(sample(41).flags^1));
    }
    // Verify another restart keeps edited today AND the correct old history.
    {PicoCalc reboot;reboot.init();App app(reboot);app.start();
     assert(app.data().today.flags==(sample(41).flags^1));app.key(Left);checkDay(app.viewedDay(),40);}
    a=slot(0);b=slot(1);
    const unsigned latest=a.generation>b.generation?0:1;
    const char* names[]={"SAVE_A.JSN","SAVE_B.JSN"};
    FILE* f=std::fopen(names[latest],"wb");assert(f);std::fputs("interrupted",f);std::fclose(f);
    {PicoCalc reboot;reboot.init();App app(reboot);app.start();
     checkDay(app.viewedDay(),41);app.key(Left);checkDay(app.viewedDay(),40);}
    // Same fallback when the newest slot is missing, regardless of A/B order.
    std::remove(names[latest]);
    {PicoCalc reboot;reboot.init();App app(reboot);app.start();checkDay(app.viewedDay(),41);}
    // Both slots corrupt: protected failure, no fabricated historical entries.
    f=std::fopen(names[1-latest],"wb");assert(f);std::fputs("bad",f);std::fclose(f);
    writes=mockWrites;
    {PicoCalc reboot;reboot.init();App app(reboot);app.start();app.key(Left);
     assert(app.viewedDay().number==1);app.key('1');app.key('1');assert(mockWrites==writes);}
    mockReadError=true;
    {PicoCalc reboot;reboot.init();App app(reboot);app.start();app.key('1');app.key('1');assert(mockWrites==writes);}
    mockReadError=false;
    std::remove("SAVE_A.JSN");std::remove("SAVE_B.JSN");
    // Date + ICS: this uses PicoCalc::exportIcs itself and the host FatFs stubs.
    std::remove("2028-02-29_LPS-Companion.ics");std::remove("2028.csv");
    PicoCalc datedDisk;datedDisk.init();App dated(datedDisk);dated.start();
    // Replace default date using the exact Date screen, including fixed-width edit.
    dated.key('0');for(int i=0;i<10;++i)dated.key(Backspace);for(char c:std::string("2028-02-29"))dated.key(c);dated.key(Enter);
    dated.key('1');dated.key('1');dated.key('2');dated.key(Escape);
    dated.key('8');dated.key(Escape);dated.key(Up);dated.key(Enter);dated.key(Enter);assert(dated.data().today.date.year==2028&&dated.data().today.date.month==3&&dated.data().today.date.day==1);
    FILE* ics=std::fopen("2028-02-29_LPS-Companion.ics","rb");assert(ics);char content[1024]{};const auto used=std::fread(content,1,sizeof(content)-1,ics);assert(std::fclose(ics)==0&&used>0);
    assert(std::strstr(content,"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"));
    assert(std::strstr(content,"DTSTART;VALUE=DATE:20280229"));assert(std::strstr(content,"DTEND;VALUE=DATE:20280301"));
    assert(std::strstr(content,"Activities: Walk, Diet\\NXP:"));
    // One yearly CSV, built by the same close-day path. A repeat is safe.
    FILE* csv=std::fopen("2028.csv","rb");assert(csv);std::memset(content,0,sizeof content);std::fread(content,1,sizeof(content)-1,csv);assert(std::fclose(csv)==0);
    assert(std::strstr(content,"date,expected_kg,measured_kg\r\n2028-02-29,,\r\n"));
    Day weightDay;weightDay.date={2028,3,2};weightDay.expected=110000;weightDay.actual=109500;
    assert(datedDisk.exportWeightCsv(weightDay));assert(datedDisk.exportWeightCsv(weightDay));
    csv=std::fopen("2028.csv","rb");assert(csv);std::memset(content,0,sizeof content);std::fread(content,1,sizeof(content)-1,csv);assert(std::fclose(csv)==0);
    assert(std::strstr(content,"2028-03-02,110.000,109.500\r\n"));
    assert(std::strstr(std::strstr(content,"2028-03-02,")+1,"2028-03-02,")==nullptr);
    const auto generationAfterLeap=slot(0).generation>slot(1).generation?slot(0).generation:slot(1).generation;
    // Invalid dates and export failure never advance the day or A/B generation.
    dated.key('0');for(int i=0;i<10;++i)dated.key(Backspace);for(char c:std::string("2027-02-29"))dated.key(c);dated.key(Enter);assert(dated.data().today.date.year==2028);
    dated.key(Escape);dated.key('8');dated.key(Escape);dated.key(Up);dated.key(Enter);std::remove("2028-03-01_LPS-Companion.ics");
    mockWriteError=true;dated.key(Enter);assert(dated.data().today.date.day==1);FILE* missing=std::fopen("2028-03-01_LPS-Companion.ics","rb");assert(!missing);
    mockWriteError=false;dated.key(Enter);assert(dated.data().today.date.day==2);
    const auto generationAfterRetry=slot(0).generation>slot(1).generation?slot(0).generation:slot(1).generation;
    assert(generationAfterRetry==generationAfterLeap+1);
    // ICS escaping plus French activity labels use the same PicoCalc adapter.
    Day french;french.date={2028,3,2};french.flags=(1u<<1)|(1u<<9);french.expected=110000;french.actual=109500;
    std::strcpy(french.note.data(),"comma, semi; slash\\");
    assert(datedDisk.exportIcs(french,60,40,Language::French,State{}));
    ics=std::fopen("2028-03-02_LPS-Companion.ics","rb");assert(ics);std::memset(content,0,sizeof content);std::fread(content,1,sizeof(content)-1,ics);assert(std::fclose(ics)==0);
    assert(std::strstr(content,"Activités: Régime, Congé\\NXP:\\NPoids attendu: 110.000 kg\\NPoids effectif: 109.500 kg\\NNote: comma\\, semi\\; slash\\\\"));
    // LPS-113-CHG08: new activities appear by name, same ICS format.
    Day added;added.date={2028,3,3};added.flags=1u|(ActivityFlags(1u)<<16)|(ActivityFlags(1u)<<21);
    assert(datedDisk.exportIcs(added,0,0,Language::English,State{}));
    ics=std::fopen("2028-03-03_LPS-Companion.ics","rb");assert(ics);std::memset(content,0,sizeof content);std::fread(content,1,sizeof(content)-1,ics);assert(std::fclose(ics)==0);
    assert(std::strstr(content,"DESCRIPTION:Activities: Walk, Sport, Meditation\\NXP:\r\nEND:VEVENT"));
    added.flags=(ActivityFlags(1u)<<17)|(ActivityFlags(1u)<<18)|(ActivityFlags(1u)<<19)|(ActivityFlags(1u)<<20);
    assert(datedDisk.exportIcs(added,0,0,Language::French,State{}));
    ics=std::fopen("2028-03-03_LPS-Companion.ics","rb");assert(ics);std::memset(content,0,sizeof content);std::fread(content,1,sizeof(content)-1,ics);assert(std::fclose(ics)==0);
    assert(std::strstr(content,"DESCRIPTION:Activités: Cuisine, Ménage, Famille, Amis\\NXP:\r\nEND:VEVENT"));
    std::remove("2028-03-03_LPS-Companion.ics");
    std::remove("2028-02-29_LPS-Companion.ics");std::remove("2028-03-01_LPS-Companion.ics");std::remove("2028-03-02_LPS-Companion.ics");std::remove("2028.csv");std::remove("SAVE_A.JSN");std::remove("SAVE_B.JSN");
    std::puts("PASS: real A/B adapter with host files: history, date, annual CSV, ICS, retries and failure handling.");
}
