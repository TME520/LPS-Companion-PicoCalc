#define main pico_firmware_main
#include "../src/main.cpp"
#undef main
#include <cassert>
unsigned char fontStub[4+224*12]{};
extern "C" {
unsigned char* MainFont=fontStub;
void draw_bitmap_spi(int,int,int,int,int,int,int,unsigned char*){}
}
void writeLegacy(const char* name,const lps::Blob& payload,uint32_t generation){
    std::array<uint8_t,LegacyRecordSize> b{};
    put32(b.data(),0x3152504c);put32(b.data()+4,generation);put32(b.data()+8,payload.size);
    std::memcpy(b.data()+12,payload.bytes.data(),payload.size);
    put32(b.data()+b.size()-4,lps::crc32(b.data(),b.size()-4));
    FILE* f=std::fopen(name,"wb");assert(f);assert(std::fwrite(b.data(),1,b.size(),f)==b.size());assert(std::fclose(f)==0);
}
void checkData(const lps::App& a){
    assert(a.data().today.number==3&&a.data().today.flags==1025&&a.data().xp==90);
    assert(a.data().today.expected==110500&&a.data().today.actual==111250);
    assert(std::strcmp(a.data().today.note.data(),"Keep this original note")==0);
    assert(a.data().count==2&&a.data().next==2);
    assert(a.data().history[1].actual==111500&&a.data().history[1].flags==512);
}
int main(int argc,char** argv){
    using namespace lps;assert(argc==2);
    Blob legacy;FILE* f=std::fopen(argv[1],"rb");assert(f);
    legacy.size=std::fread(legacy.bytes.data(),1,legacy.bytes.size(),f);assert(std::fclose(f)==0);
    assert(legacy.size==LegacyWireSize);State decoded;
    assert(decode(legacy.bytes.data(),legacy.size,decoded)&&decoded.language==Language::English);
    const char* names[]={"SAVE_A.BIN","SAVE_B.BIN"};
    for(unsigned old=0;old<2;++old){
        std::remove(names[0]);std::remove(names[1]);
        writeLegacy(names[old],legacy,50);
        PicoCalc disk;disk.init();App app(disk);app.start();checkData(app);
        assert(app.data().language==Language::English);
        app.key(Left);assert(app.viewedDay().number==2&&app.viewedDay().actual==111500);
        app.key(Right);app.key('5');app.key('1');app.key('2');app.key('2');
        checkData(app);assert(app.data().language==Language::French);
        assert(readRecord(old).payloadSize==LegacyWireSize&&readRecord(1-old).payloadSize==WireSize);
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();checkData(a);assert(a.data().language==Language::French);}
        // Interrupted first v1.3 write: recover the actual original v1.2 snapshot.
        f=std::fopen(names[1-old],"wb");assert(f);std::fputs("partial",f);std::fclose(f);
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();checkData(a);
         assert(a.data().language==Language::English);
         a.key('5');a.key('1');a.key('2');a.key('2');
         a.key('2');a.key(Enter); // Second save upgrades the remaining legacy slot.
         assert(readRecord(0).payloadSize==WireSize&&readRecord(1).payloadSize==WireSize);}
        {PicoCalc reboot;reboot.init();App a(reboot);a.start();checkData(a);assert(a.data().language==Language::French);}
    }
    std::remove(names[0]);std::remove(names[1]);
    std::puts("PASS: authentic v1.2 fixture, mixed-format A/B saves in both orders, French persistence, interrupted-upgrade fallback and full history preservation.");
}
