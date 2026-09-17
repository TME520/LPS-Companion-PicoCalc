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
constexpr size_t RecordSizeV2=lps::WireV2+16,RecordSizeV1=lps::WireV1+16;
void put32(uint8_t* p,uint32_t x){for(int i=0;i<4;++i){p[i]=uint8_t(x);x>>=8;}}
uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
struct Record {
    std::array<uint8_t,RecordSize> bytes{};
    uint32_t generation=0;
    size_t payloadSize=0;
    enum Status { Missing, Valid, Damaged, IOError } status=Missing;
};
Record readRecord(unsigned slot){
    Record r;FIL f;FRESULT result=f_open(&f,paths[slot],FA_READ);
    if(result==FR_NO_FILE||result==FR_NO_PATH)return r;
    if(result!=FR_OK){r.status=Record::IOError;return r;}
    const size_t recordSize=f_size(&f);
    bool rightSize=recordSize==RecordSize||recordSize==RecordSizeV2||recordSize==RecordSizeV1;UINT n=0;
    result=rightSize?f_read(&f,r.bytes.data(),recordSize,&n):FR_OK;
    FRESULT closed=f_close(&f);
    if(result!=FR_OK||closed!=FR_OK){r.status=Record::IOError;return r;}
    lps::State candidate;
    if(!rightSize||n!=recordSize||get32(r.bytes.data())!=0x3152504c||get32(r.bytes.data()+8)!=recordSize-16||get32(r.bytes.data()+recordSize-4)!=lps::crc32(r.bytes.data(),recordSize-4)||!lps::decode(r.bytes.data()+12,recordSize-16,candidate)){
        r.status=Record::Damaged;return r;
    }
    r.generation=get32(r.bytes.data()+4);r.payloadSize=recordSize-16;r.status=Record::Valid;return r;
}
class PicoCalc final:public lps::Platform {
    std::array<std::array<char,40>,20> frame{},previous{};
    std::array<bool,20> reverse{},oldReverse{};
    bool first=true,mounted=false,writeFault=false;
    int active=-1;
    uint32_t generation=0;
    static bool put(FIL& f,const char* text){UINT written=0;const auto n=std::strlen(text);return f_write(&f,text,n,&written)==FR_OK&&written==n;}
    static bool escaped(FIL& f,const char* text){
        char b[3]{};
        for(;*text;++text){char c=*text;
            if(c=='\\'||c==';'||c==','){b[0]='\\';b[1]=c;b[2]=0;}
            else if(c=='\n'){std::strcpy(b,"\\n");}
            else {b[0]=c;b[1]=0;}
            if(!put(f,b))return false;
        }return true;
    }
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
            for(int col=0;col<40;++col){unsigned c=static_cast<unsigned char>(frame[row][col]);
                // The upstream extended font is NOT Latin-1. Compose French
                // accents over its ASCII bases in the padded 8x16 cell.
                unsigned accent=0;bool upper=false;
                switch(c){
                case 0xe9:c='e';accent=1;break;case 0xe8:c='e';accent=2;break;
                case 0xea:c='e';accent=3;break;case 0xe0:c='a';accent=2;break;
                case 0xe2:c='a';accent=3;break;case 0xee:c='i';accent=3;break;
                case 0xf4:c='o';accent=3;break;case 0xf9:c='u';accent=2;break;
                case 0xfb:c='u';accent=3;break;case 0xe7:c='c';accent=4;break;
                case 0xc9:c='E';accent=1;upper=true;break;
                }
                if(c<32||c>126)c=32;
                for(int y=0;y<12;++y)bitmap[(y+2)*40+col]=MainFont[4+(c-32)*12+y];
                const unsigned top=upper?0:3;
                if(accent==1){bitmap[top*40+col]=0x08;bitmap[(top+1)*40+col]=0x10;}
                if(accent==2){bitmap[top*40+col]=0x10;bitmap[(top+1)*40+col]=0x08;}
                if(accent==3){bitmap[top*40+col]=0x10;bitmap[(top+1)*40+col]=0x28;}
                if(accent==4){bitmap[12*40+col]=0x10;bitmap[13*40+col]=0x20;}
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
        size=best.payloadSize;std::memcpy(bytes,best.bytes.data()+12,size);return lps::LoadResult::Ok;
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
    bool exportIcs(const lps::Day& day,uint32_t totalXp,uint32_t dayXp,lps::Language language)override{
        if(!mounted||!lps::validDate(day.date))return false;
        FRESULT r=f_mkdir("0:/LPS");if(r!=FR_OK&&r!=FR_EXIST)return false;
        r=f_mkdir("0:/LPS/EXPORT");if(r!=FR_OK&&r!=FR_EXIST)return false;
        char path[64],ymd[13],next[13],line[160];
        std::snprintf(ymd,sizeof ymd,"%04u%02u%02u",day.date.year,day.date.month,day.date.day);
        auto tomorrow=day.date;if(!lps::nextDate(tomorrow))return false;
        std::snprintf(next,sizeof next,"%04u%02u%02u",tomorrow.year,tomorrow.month,tomorrow.day);
        std::snprintf(path,sizeof path,"0:/LPS/EXPORT/%04u-%02u-%02u_LPS-Companion.ics",day.date.year,day.date.month,day.date.day);
        FIL f;r=f_open(&f,path,FA_WRITE|FA_CREATE_ALWAYS);if(r!=FR_OK)return false;
        bool ok=put(f,"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//LPS Companion//EN\r\nCALSCALE:GREGORIAN\r\nBEGIN:VEVENT\r\n");
        std::snprintf(line,sizeof line,"UID:lps-companion-%s@picocalc\r\nDTSTART;VALUE=DATE:%s\r\nDTEND;VALUE=DATE:%s\r\n",ymd,ymd,next);ok=ok&&put(f,line);
        ok=ok&&put(f,"SUMMARY:LPS Companion\r\nDESCRIPTION:");
        const char* names=language==lps::Language::English?"Activities: ":"Activités: ";ok=ok&&escaped(f,names);
        bool first=true;for(unsigned i=0;i<lps::ActivityCount;++i)if(day.flags&(1u<<i)){
            if(!first)ok=ok&&put(f,", ");
            ok=ok&&escaped(f,language==lps::Language::English?lps::EnglishActivities[i]:lps::Activities[i]);first=false;}
        if(first)ok=ok&&put(f,"none");
        std::snprintf(line,sizeof line,"\\nXP: +%lu (total %lu)",static_cast<unsigned long>(dayXp),static_cast<unsigned long>(totalXp));ok=ok&&put(f,line);
        if(day.expected>=0){std::snprintf(line,sizeof line,"\\n%s: %ld.%03ld kg",language==lps::Language::English?"Expected weight":"Poids attendu",long(day.expected/1000),long(day.expected%1000));ok=ok&&escaped(f,line);}
        if(day.actual>=0){std::snprintf(line,sizeof line,"\\n%s: %ld.%03ld kg",language==lps::Language::English?"Actual weight":"Poids effectif",long(day.actual/1000),long(day.actual%1000));ok=ok&&escaped(f,line);}
        if(day.note[0]){ok=ok&&put(f,"\\nNote: ");ok=ok&&escaped(f,day.note.data());}
        ok=ok&&put(f,"\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        const auto sync=f_sync(&f),close=f_close(&f);
        if(!(ok&&sync==FR_OK&&close==FR_OK)){f_unlink(path);return false;}
        return true;
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
