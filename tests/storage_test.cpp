// Exercise the SAME A/B adapter compiled into the Pico firmware.
#define main pico_firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
unsigned char fontStub[4+224*12]{};
extern "C" {
unsigned char* MainFont=fontStub;
void draw_bitmap_spi(int,int,int,int,int,int,int,unsigned char*){}
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
    std::remove("SAVE_A.BIN");std::remove("SAVE_B.BIN");
    PicoCalc disk;disk.init();App initial(disk);initial.start();
    initial.key(Left);assert(initial.viewedDay().number==1);
    assert(mockWrites==0); // Empty history does not create files.
    State state;
    for(unsigned n=1;n<=40;++n){
        state.history[state.next]=sample(n);state.next=(state.next+1)%HistoryLength;
        if(state.count<HistoryLength)++state.count;
        state.today=sample(n+1);state.xp=n*10;
        Blob b=encode(state);assert(disk.save(b.bytes.data(),b.size));
    }
    Record a=readRecord(0),b=readRecord(1);assert(a.status==Record::Valid&&b.status==Record::Valid);
    auto writes=mockWrites;
    {
        PicoCalc reboot;reboot.init();App app(reboot);app.start();checkDay(app.viewedDay(),41);
        for(unsigned n=40;n>=10;--n){
            app.key(Left);checkDay(app.viewedDay(),n);
            // Attempt edits in every archived screen; no writes or XP changes.
            app.key('1');app.key('1');app.key(Enter);app.key(Right);app.key(Escape);
            app.key('2');app.key('X');app.key(Backspace);app.key(Enter);
            app.key('3');app.key('9');app.key(Backspace);app.key(Enter);
            app.key('4');assert(app.currentScreen()==Screen::Home);
            checkDay(app.viewedDay(),n);assert(app.data().xp==400);
        }
        app.key(Left);checkDay(app.viewedDay(),10); // Oldest retained boundary.
        app.key(Right);checkDay(app.viewedDay(),41); // Direct return, not one day forward.
        app.key(Right);checkDay(app.viewedDay(),41);
        assert(mockWrites==writes);
        assert(readRecord(0).bytes==a.bytes&&readRecord(1).bytes==b.bytes);
        app.key('1');app.key('1');app.key(Escape);assert(mockWrites==writes+1);
        assert(app.data().today.flags==(sample(41).flags^1));
    }
    // Verify another restart keeps edited today AND the correct old history.
    {PicoCalc reboot;reboot.init();App app(reboot);app.start();
     assert(app.data().today.flags==(sample(41).flags^1));app.key(Left);checkDay(app.viewedDay(),40);}
    a=readRecord(0);b=readRecord(1);
    const unsigned latest=a.generation>b.generation?0:1;
    const char* names[]={"SAVE_A.BIN","SAVE_B.BIN"};
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
    std::remove("SAVE_A.BIN");std::remove("SAVE_B.BIN");
    std::puts("PASS: real A/B adapter with host files: 40 saves, restart, 31-day wrap, read-only browsing, current-day edits, damaged/missing newest slot and protected I/O failure.");
}
