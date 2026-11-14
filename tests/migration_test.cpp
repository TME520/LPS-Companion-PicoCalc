#define main pico_firmware_main
#include "../src/main.cpp"
#undef main
#include "save_helpers.h"
#include <cassert>
#include <string>
unsigned char fontStub[4+224*12]{};
extern "C" {
unsigned char* MainFont=fontStub;
void draw_bitmap_spi(int,int,int,int,int,int,int,unsigned char*){}
void draw_rect_spi(int,int,int,int,int){}
void draw_buffer_spi(int,int,int,int,unsigned char*){}
}
void put32(uint8_t* p,uint32_t x){for(int i=0;i<4;++i){p[i]=uint8_t(x);x>>=8;}}
// Wrap a legacy payload in the v1.12 A/B record envelope (SAVE_A/B.BIN).
void writeLegacy(const char* name,const lps::Blob& payload,uint32_t generation){
    std::array<uint8_t,RecordSize> b{};const auto recordSize=payload.size+16;
    assert(recordSize==RecordSizeV1||recordSize==RecordSizeV2||recordSize==RecordSize);
    put32(b.data(),0x3152504c);put32(b.data()+4,generation);put32(b.data()+8,payload.size);
    std::memcpy(b.data()+12,payload.bytes.data(),payload.size);
    put32(b.data()+recordSize-4,lps::crc32(b.data(),recordSize-4));
    FILE* f=std::fopen(name,"wb");assert(f);assert(std::fwrite(b.data(),1,recordSize,f)==recordSize);assert(std::fclose(f)==0);
}
std::string fileBytes(const char* name){
    std::string out;FILE* f=std::fopen(name,"rb");if(!f)return out;
    char b[512];size_t n;while((n=std::fread(b,1,sizeof b,f))>0)out.append(b,n);std::fclose(f);return out;
}
bool exists(const char* name){FILE* f=std::fopen(name,"rb");if(f)std::fclose(f);return f!=nullptr;}
void writeText(const char* name,const char* text){FILE* f=std::fopen(name,"wb");assert(f);std::fputs(text,f);std::fclose(f);}
lps::State probe;
Slot slot(unsigned n){return readSlot(n,probe);}
void checkData(const lps::App& a){
    assert(a.data().today.number==3&&a.data().today.flags==1025&&a.data().xp==90);
    assert(a.data().today.expected==110500&&a.data().today.actual==111250);
    assert(std::strcmp(a.data().today.note.data(),"Keep this original note")==0);
    assert(a.data().count==2&&a.data().next==2);
    assert(a.data().history[1].actual==111500&&a.data().history[1].flags==512);
}
const char* bins[]={"SAVE_A.BIN","SAVE_B.BIN"};
const char* jsns[]={"SAVE_A.JSN","SAVE_B.JSN"};
void clean(){for(unsigned i=0;i<2;++i){std::remove(bins[i]);std::remove(jsns[i]);}}
int main(int argc,char** argv){
    using namespace lps;assert(argc==2);
    Blob legacy;FILE* f=std::fopen(argv[1],"rb");assert(f);
    legacy.size=std::fread(legacy.bytes.data(),1,legacy.bytes.size(),f);assert(std::fclose(f)==0);
    assert(legacy.size==WireV1);State decoded;
    assert(decode(legacy.bytes.data(),legacy.size,decoded)&&decoded.language==Language::English);
    // Rebuild an authentic format-2 shape from the v1 payload: same day bytes,
    // exactly one language byte inserted between header and first day record.
    Blob v2;v2.size=WireV2;std::memcpy(v2.bytes.data(),legacy.bytes.data(),12);
    v2.bytes[4]=2;v2.bytes[12]=uint8_t(Language::French);
    std::memcpy(v2.bytes.data()+13,legacy.bytes.data()+12,WireV1-16);
    auto v2crc=crc32(v2.bytes.data(),v2.size-4);for(unsigned i=0;i<4;++i)v2.bytes[v2.size-4+i]=uint8_t(v2crc>>(8*i));
    assert(decode(v2.bytes.data(),v2.size,decoded)&&decoded.language==Language::French);
    // LPS-113-CHG06: an authentic v1.2 (format 1) BIN save, in either slot, migrates to JSON.
    for(unsigned old=0;old<2;++old){
        clean();
        writeLegacy(bins[old],legacy,50);const std::string original=fileBytes(bins[old]);
        PicoCalc disk;disk.init();App app(disk);app.start();checkData(app);
        assert(app.data().language==Language::English);
        app.key(Left);assert(app.viewedDay().number==2&&app.viewedDay().actual==111500);
        app.key(Right);app.key('6');app.key('1');app.key('2');app.key('3');
        checkData(app);assert(app.data().language==Language::French);
        // First save writes SAVE_A.JSN (generation continues); BIN files are untouched.
        assert(slot(0).status==Slot::Valid&&slot(0).generation==51&&slot(1).status==Slot::Missing);
        assert(fileBytes(bins[old])==original&&!exists(bins[1-old]));
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();checkData(a);assert(a.data().language==Language::French);
         a.key(Left);assert(a.viewedDay().number==2&&a.viewedDay().actual==111500);}
        // Interrupted first JSON write: fall back to the original BIN snapshot.
        writeText(jsns[0],"{\"format\":\"lps-comp");
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();checkData(a);
         assert(a.data().language==Language::English);
         a.key('6');a.key('1');a.key('2');a.key('3');
         a.key('6');a.key('3'); // Second save fills the other JSON slot.
         assert(slot(0).status==Slot::Valid&&slot(1).status==Slot::Valid);}
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();checkData(a);assert(a.data().language==Language::French);}
        assert(fileBytes(bins[old])==original);
        // Once JSON exists, a damaged pair is protected: no fallback to the stale BIN, no writes.
        writeText(jsns[0],"bad");writeText(jsns[1],"bad");
        {const auto writes=mockWrites;PicoCalc reboot;reboot.init();App a(reboot);a.start();
         assert(a.data().today.number==1);a.key('1');a.key('1');assert(mockWrites==writes);}
    }
    // A full v1.12 (format 6) save: every field, wrapped history, Kanban and reminders.
    {
        clean();
        State full=lps_test::fullState('m');
        // BIN formats hold 16 activity bits: they map to the first 16 names unchanged.
        full.today.flags&=0xffffu;for(Day& d:full.history)d.flags&=0xffffu;
        writeLegacy(bins[0],lps_test::encodeLegacyV6(full),7);
        State older=full;older.xp=1;writeLegacy(bins[1],lps_test::encodeLegacyV6(older),6);
        PicoCalc disk;disk.init();App app(disk);app.start();
        assert(lps_test::sameState(app.data(),full)); // Highest BIN generation wins.
        app.key('1');app.key('1'); // Untick Walk: a real save.
        State expected=full;expected.today.flags&=uint16_t(~1u);
        assert(slot(0).status==Slot::Valid&&slot(0).generation==8&&lps_test::sameState(probe,expected));
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();assert(lps_test::sameState(a.data(),expected));
         for(unsigned back=1;back<=HistoryLength;++back){a.key(Left);assert(lps_test::sameDay(a.viewedDay(),lps_test::archived(full,HistoryLength-back)));}}
        // An undamaged JSON pair wins over any BIN, newest generation first in both slot orders.
        app.key('1'); // Tick Walk again: generation 9 lands in SAVE_B.JSN.
        assert(slot(1).generation==9&&lps_test::sameState(probe,full));
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();assert(lps_test::sameState(a.data(),full));}
        // Swap the files: SAVE_A now holds generation 9, SAVE_B generation 8.
        const std::string a=fileBytes(jsns[0]),b=fileBytes(jsns[1]);
        f=std::fopen(jsns[0],"wb");std::fwrite(b.data(),1,b.size(),f);std::fclose(f);
        f=std::fopen(jsns[1],"wb");std::fwrite(a.data(),1,a.size(),f);std::fclose(f);
        {PicoCalc reboot;reboot.init();App x(reboot);x.start();assert(lps_test::sameState(x.data(),full));
         x.key('1');x.key('1');assert(slot(1).generation==10&&slot(0).generation==9);} // Next write goes to the older slot.
        // A newer schema in either slot blocks writes instead of being overwritten.
        std::string future=fileBytes(jsns[0]);
        lps_test::JsonStore j;const auto at=future.find("\"version\":2");future.replace(at,11,"\"version\":3");j.assign(future);
        f=std::fopen(jsns[0],"wb");std::fwrite(j.bytes.data(),1,j.size,f);std::fclose(f);
        {State s;assert(j.check(s)==SaveCheck::Unsupported);
         const auto writes=mockWrites;PicoCalc reboot;reboot.init();App x(reboot);x.start();x.key('1');x.key('1');assert(mockWrites==writes);}
    }
    clean();
    std::puts("PASS: v1.2 and v1.12 BIN saves migrate to JSON in both slot orders, BIN files untouched, interrupted first JSON write falls back to BIN, damaged JSON pair and newer schema protected, newest JSON generation wins.");
}
