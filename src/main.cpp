// Hardware adapter for original PicoCalc / RP2040, standalone BOOTSEL firmware.
// Application stays in one file; include it once here (do not also link it).
#include "lps_companion.cpp"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
extern "C" {
#include "i2ckbd.h"
#include "lcdspi.h"
#include "sd_card.h"
#include "hw_config.h"
#include "ff.h"
extern unsigned char *MainFont;
void draw_bitmap_spi(int,int,int,int,int,int,int,unsigned char*);
}
namespace {
constexpr const char* paths[]={"0:/LPS/SAVE_A.BIN","0:/LPS/SAVE_B.BIN"};
constexpr size_t RecordSize=lps::WireSize+16;
static_assert(RecordSize==3584,"Seven SD sectors per record");
void put32(uint8_t* p,uint32_t x){for(int i=0;i<4;++i){p[i]=uint8_t(x);x>>=8;}}
uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
struct Record {
    std::array<uint8_t,RecordSize> bytes{};
    uint32_t generation=0;
    enum Status { Missing, Valid, Damaged, IOError } status=Missing;
};
Record readRecord(unsigned slot){
    Record r;FIL f;FRESULT result=f_open(&f,paths[slot],FA_READ);
    if(result==FR_NO_FILE||result==FR_NO_PATH)return r;
    if(result!=FR_OK){r.status=Record::IOError;return r;}
    bool rightSize=f_size(&f)==RecordSize;UINT n=0;
    result=rightSize?f_read(&f,r.bytes.data(),RecordSize,&n):FR_OK;
    FRESULT closed=f_close(&f);
    if(result!=FR_OK||closed!=FR_OK){r.status=Record::IOError;return r;}
    lps::State candidate;
    if(!rightSize||n!=RecordSize||get32(r.bytes.data())!=0x3152504c||get32(r.bytes.data()+8)!=lps::WireSize||get32(r.bytes.data()+RecordSize-4)!=lps::crc32(r.bytes.data(),RecordSize-4)||!lps::decode(r.bytes.data()+12,lps::WireSize,candidate)){
        r.status=Record::Damaged;return r;
    }
    r.generation=get32(r.bytes.data()+4);r.status=Record::Valid;return r;
}
class PicoCalc final:public lps::Platform {
    std::array<std::array<char,40>,20> frame{},previous{};
    std::array<bool,20> reverse{},oldReverse{};
    bool first=true,mounted=false,writeFault=false;
    int active=-1;
    uint32_t generation=0;
public:
    void init(){
        set_sys_clock_khz(133000,true);
        init_i2c_kbd();lcd_init();
        // Keep unused PSRAM deselected; no PSRAM or multicore required.
        gpio_init(20);gpio_set_dir(20,GPIO_OUT);gpio_put(20,1);
        if(sd_init_driver())mounted=f_mount(&sd_get_by_num(0)->fatfs,"0:",1)==FR_OK;
    }
    void clear()override{for(auto& row:frame)row.fill(' ');reverse.fill(false);}
    void text(int x,int y,const char* s,bool rev)override{
        int row=y/16,col=x/8;if(row<0||row>=20||col<0||col>=40)return;
        reverse[row]=rev;while(*s&&col<40)frame[row][col++]=*s++;
    }
    void present()override{
        // Upstream font is 8x12. Pad two rows above/below to make 8x16 cells.
        // One 640-byte monochrome line buffer, not a 307,200-byte framebuffer.
        std::array<unsigned char,640> bitmap{};
        for(int row=0;row<20;++row){
            if(!first&&frame[row]==previous[row]&&reverse[row]==oldReverse[row])continue;
            bitmap.fill(0);
            for(int col=0;col<40;++col){unsigned c=static_cast<unsigned char>(frame[row][col]);if(c==0xe9){
                    // Accent plus the upstream lowercase e (extended font is not Latin-1).
                    for(int y=0;y<12;++y)bitmap[(y+2)*40+col]=MainFont[4+('e'-32)*12+y];
                    bitmap[3*40+col]=0x08;bitmap[4*40+col]=0x10;
                    continue;
                }
                if(c<32||c>126)c=32;
                for(int y=0;y<12;++y)bitmap[(y+2)*40+col]=MainFont[4+(c-32)*12+y];
            }
            const int fg=reverse[row]?0x18291c:0xe4eed5,bg=reverse[row]?0xbad99f:0x18291c;
            draw_bitmap_spi(0,row*16,320,16,1,fg,bg,bitmap.data());
        }
        previous=frame;oldReverse=reverse;first=false;
    }
    lps::LoadResult load(uint8_t* bytes,size_t cap,size_t& size)override{
        if(!mounted||cap<lps::WireSize)return lps::LoadResult::Error;
        Record a=readRecord(0),b=readRecord(1);
        if(a.status==Record::IOError||b.status==Record::IOError)return lps::LoadResult::Error;
        if(a.status!=Record::Valid&&b.status!=Record::Valid){
            return a.status==Record::Missing&&b.status==Record::Missing?lps::LoadResult::Missing:lps::LoadResult::Error;
        }
        active=b.status==Record::Valid&&(a.status!=Record::Valid||b.generation>a.generation)?1:0;
        const auto& best=active?b:a;generation=best.generation;
        size=lps::WireSize;std::memcpy(bytes,best.bytes.data()+12,size);return lps::LoadResult::Ok;
    }
    bool save(const uint8_t* bytes,size_t size)override{
        if(!mounted||writeFault||size!=lps::WireSize||generation==UINT32_MAX)return false;
        FRESULT result=f_mkdir("0:/LPS");if(result!=FR_OK&&result!=FR_EXIST)return false;
        const unsigned slot=active==0?1:0;
        std::array<uint8_t,RecordSize> record{};
        put32(record.data(),0x3152504c);put32(record.data()+4,generation+1);put32(record.data()+8,uint32_t(size));
        std::memcpy(record.data()+12,bytes,size);put32(record.data()+RecordSize-4,lps::crc32(record.data(),RecordSize-4));
        FIL f;result=f_open(&f,paths[slot],FA_WRITE|FA_CREATE_ALWAYS);if(result!=FR_OK)return false;
        UINT n=0;result=f_write(&f,record.data(),RecordSize,&n);
        const auto synced=f_sync(&f),closed=f_close(&f);
        // A/B preserves the active slot. If a late write/sync/readback fails,
        // outcome may be ambiguous: block further writes until restart/recovery.
        if(result!=FR_OK||n!=RecordSize||synced!=FR_OK||closed!=FR_OK){writeFault=true;return false;}
        const Record check=readRecord(slot);
        if(check.status!=Record::Valid||check.bytes!=record){writeFault=true;return false;}
        active=int(slot);++generation;return true;
    }
};
int keymap(int c){
    // ClockworkPi Code/picocalc_kbd_tester/keyboard_define.h
    switch(c){case 0xb5:return lps::Up;case 0xb6:return lps::Down;case 0xb4:return lps::Left;case 0xb7:return lps::Right;
    case 0xb1:case 27:return lps::Escape;case 10:case 13:return lps::Enter;case 8:case 127:return lps::Backspace;
    default:return (c>=32&&c<=126)||c==9?c:-1;}
}
}
int main(){
    static PicoCalc hardware;
    hardware.init();
    static lps::App app(hardware);
    app.start();
    while(true){int k=keymap(read_i2c_kbd());if(k>=0)app.key(k);sleep_ms(5);}
}
