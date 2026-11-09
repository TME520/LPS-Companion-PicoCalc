// Hardware adapter for original PicoCalc / RP2040, standalone BOOTSEL firmware.
// Application stays in one file; include it once here (do not also link it).
#include "lps_companion.cpp"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
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
// LPS-113-CHG06: the save is JSON in two alternating files. The binary files
// of v1.12 and earlier are only read, to migrate them, and never modified.
constexpr const char* paths[]={"0:/LPS/SAVE_A.JSN","0:/LPS/SAVE_B.JSN"};
constexpr const char* legacyPaths[]={"0:/LPS/SAVE_A.BIN","0:/LPS/SAVE_B.BIN"};
constexpr size_t RecordSize=lps::WireSize+16;
constexpr size_t RecordSizeV5=lps::WireV5+16;
constexpr size_t RecordSizeV4=lps::WireV4+16,RecordSizeV3=lps::WireV3+16,RecordSizeV2=lps::WireV2+16,RecordSizeV1=lps::WireV1+16;
uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
struct Record {
    std::array<uint8_t,RecordSize> bytes{};
    uint32_t generation=0;
    size_t payloadSize=0;
    enum Status { Missing, Valid, Damaged, IOError } status=Missing;
};
Record readLegacyRecord(unsigned slot,lps::State& into){
    Record r;FIL f;FRESULT result=f_open(&f,legacyPaths[slot],FA_READ);
    if(result==FR_NO_FILE||result==FR_NO_PATH)return r;
    if(result!=FR_OK){r.status=Record::IOError;return r;}
    const size_t recordSize=f_size(&f);
    bool rightSize=recordSize==RecordSize||recordSize==RecordSizeV5||recordSize==RecordSizeV4||recordSize==RecordSizeV3||recordSize==RecordSizeV2||recordSize==RecordSizeV1;UINT n=0;
    result=rightSize?f_read(&f,r.bytes.data(),recordSize,&n):FR_OK;
    FRESULT closed=f_close(&f);
    if(result!=FR_OK||closed!=FR_OK){r.status=Record::IOError;return r;}
    if(!rightSize||n!=recordSize||get32(r.bytes.data())!=0x3152504c||get32(r.bytes.data()+8)!=recordSize-16||get32(r.bytes.data()+recordSize-4)!=lps::crc32(r.bytes.data(),recordSize-4)||!lps::decode(r.bytes.data()+12,recordSize-16,into)){
        r.status=Record::Damaged;return r;
    }
    r.generation=get32(r.bytes.data()+4);r.payloadSize=recordSize-16;r.status=Record::Valid;return r;
}
// FatFs streams for the JSON writer/reader: no whole-file buffer.
struct FileSink final:lps::SaveSink {
    FIL& file;explicit FileSink(FIL& f):file(f){}
    bool write(const char* data,size_t size)override{UINT n=0;return f_write(&file,data,UINT(size),&n)==FR_OK&&n==size;}
};
struct FileSource final:lps::SaveSource {
    FIL& file;explicit FileSource(FIL& f):file(f){}
    long read(char* data,size_t capacity)override{UINT n=0;return f_read(&file,data,UINT(capacity),&n)==FR_OK?long(n):-1;}
};
struct Slot {
    enum Status { Missing, Valid, Damaged, Unsupported, IOError } status=Missing;
    uint32_t generation=0,checksum=0;
};
// Parse one JSON slot into `into` (overwritten even when the file is invalid).
Slot readSlot(unsigned slot,lps::State& into){
    Slot s;FIL f;FRESULT result=f_open(&f,paths[slot],FA_READ);
    if(result==FR_NO_FILE||result==FR_NO_PATH)return s;
    if(result!=FR_OK){s.status=Slot::IOError;return s;}
    FileSource source(f);lps::SaveInfo info;
    const auto check=lps::readSave(source,into,info);
    const FRESULT closed=f_close(&f);
    if(check==lps::SaveCheck::IOError||closed!=FR_OK){s.status=Slot::IOError;return s;}
    if(check==lps::SaveCheck::Unsupported){s.status=Slot::Unsupported;return s;}
    if(check!=lps::SaveCheck::Valid){s.status=Slot::Damaged;return s;}
    s.status=Slot::Valid;s.generation=info.generation;s.checksum=info.checksum;return s;
}
class PicoCalc final:public lps::Platform {
    std::array<std::array<char,40>,20> frame{},previous{};
    std::array<bool,20> reverse{},oldReverse{};
    std::array<bool,20> alertRow{},oldAlertRow{};
    std::array<std::array<bool,40>,20> bold{},oldBold{};
    bool first=true,mounted=false,writeFault=false;
    bool lowBattery=false;
    int active=-1;
    uint32_t generation=0;
    // Parse target for slot checks, BIN migration checks and write read-back
    // (one State, static with the adapter, so not on the stack).
    lps::State scratch{};
    // A full year of the compact CSV is below 16 KiB.  Keeping this buffer in
    // the hardware adapter avoids a large stack allocation on the RP2040.
    std::array<char,16384> csv{};
    std::array<char,6> largeValue{};bool largeRed=false,hadLarge=false;
    int foreground=0xe4eed5,background=0x18291c,selected=0xbad99f;
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
        // Pico VSYS is available on ADC3 through the board's 1/3 divider.
        adc_init();adc_gpio_init(29);
        if(sd_init_driver())mounted=f_mount(&sd_get_by_num(0)->fatfs,"0:",1)==FR_OK;
    }
    void clear()override{largeValue.fill(0);for(auto& row:frame)row.fill(' ');reverse.fill(false);alertRow.fill(false);for(auto& row:bold)row.fill(false);}
    void setPalette(lps::Palette palette)override{
        const int oldForeground=foreground,oldBackground=background,oldSelected=selected;
        switch(palette){
        case lps::Palette::Red: foreground=0xf4ddd2;background=0x2a1515;selected=0xd78778;break;
        case lps::Palette::Green: foreground=0xe4eed5;background=0x18291c;selected=0xbad99f;break;
        case lps::Palette::Blue: foreground=0xdce9f5;background=0x152332;selected=0x8ebde3;break;
        case lps::Palette::Orange: foreground=0xffe5ba;background=0x302016;selected=0xf3ad62;break;
        case lps::Palette::Pink: foreground=0xffe7f1;background=0x31202c;selected=0xeeb2d0;break;
        }
        // A changed palette must repaint every line, even where its text did not change.
        if(foreground!=oldForeground||background!=oldBackground||selected!=oldSelected)first=true;
    }
    void text(int x,int y,const char* s,bool rev)override{
        int row=y/16,col=x/8;if(row<0||row>=20||col<0||col>=40)return;
        reverse[row]=rev;while(*s&&col<40)frame[row][col++]=*s++;
    }
    void emphasize(int x,int y,int length)override{
        const int row=y/16,start=x/8;if(row<0||row>=20)return;
        for(int col=start;col<start+length&&col<40;++col)if(col>=0)bold[row][col]=true;
    }
    void alert(int x,int y,const char* s)override{
        int row=y/16,col=x/8;if(row<0||row>=20||col<0||col>=40)return;
        alertRow[row]=true;while(*s&&col<40)frame[row][col++]=*s++;
    }
    bool batteryBelow20()override{
        adc_select_input(3);uint32_t total=0;
        for(unsigned i=0;i<16;++i)total+=adc_read();
        const uint32_t millivolts=(total/16)*9900u/4095u;
        // Approximate 20% threshold, with hysteresis to prevent flicker.
        if(millivolts<3480)lowBattery=true;
        else if(millivolts>3550)lowBattery=false;
        return lowBattery;
    }
    void largeText(const char* value,bool red)override{
        std::snprintf(largeValue.data(),largeValue.size(),"%s",value);largeRed=red;
    }
    void present()override{
        if(hadLarge||largeValue[0])first=true;
        // Upstream font is 8x12. Pad two rows above/below to make 8x16 cells.
        // One 640-byte monochrome line buffer, not a 307,200-byte framebuffer.
        std::array<unsigned char,640> bitmap{};
        for(int row=0;row<20;++row){
            if(!first&&frame[row]==previous[row]&&reverse[row]==oldReverse[row]&&alertRow[row]==oldAlertRow[row]&&bold[row]==oldBold[row])continue;
            bitmap.fill(0);
            for(int col=0;col<40;++col){unsigned c=static_cast<unsigned char>(frame[row][col]);
                // The upstream extended font is NOT Latin-1. Compose French
                // accents over its ASCII bases in the padded 8x16 cell.
                unsigned accent=0;bool upper=false;
                switch(c){
                case 0xc0:c='A';accent=2;upper=true;break;
                case 0xe9:c='e';accent=1;break;case 0xe8:c='e';accent=2;break;
                case 0xea:c='e';accent=3;break;case 0xe0:c='a';accent=2;break;
                case 0xe2:c='a';accent=3;break;case 0xee:c='i';accent=3;break;
                case 0xf4:c='o';accent=3;break;case 0xf9:c='u';accent=2;break;
                case 0xfb:c='u';accent=3;break;case 0xe7:c='c';accent=4;break;
                case 0xc9:c='E';accent=1;upper=true;break;case 0xc8:c='E';accent=2;upper=true;break;
                }
                if(c<32||c>126)c=32;
                for(int y=0;y<12;++y){const auto glyph=MainFont[4+(c-32)*12+y];
                    bitmap[(y+2)*40+col]=(alertRow[row]||bold[row][col])?static_cast<unsigned char>(glyph|(glyph>>1)):glyph;}
                const unsigned top=upper?0:3;
                if(accent==1){bitmap[top*40+col]=0x08;bitmap[(top+1)*40+col]=0x10;}
                if(accent==2){bitmap[top*40+col]=0x10;bitmap[(top+1)*40+col]=0x08;}
                if(accent==3){bitmap[top*40+col]=0x10;bitmap[(top+1)*40+col]=0x28;}
                if(accent==4){bitmap[12*40+col]=0x10;bitmap[13*40+col]=0x20;}
            }
            const int fg=alertRow[row]?0xf05252:(reverse[row]?background:foreground),bg=reverse[row]?selected:background;
            draw_bitmap_spi(0,row*16,320,16,1,fg,bg,bitmap.data());
        }
        if(largeValue[0]){
            // 4x nearest-neighbour font: 160x64, centred; only 1280 bytes.
            std::array<unsigned char,1280> largeBitmap{};
            for(int c=0;c<5;++c)for(int y=0;y<12;++y){
                const unsigned glyph=MainFont[4+(unsigned(largeValue[c])-32)*12+y];
                for(int x=0;x<8;++x)if(glyph&(0x80u>>x))
                    for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx){
                        const int px=c*32+x*4+dx,py=(y+2)*4+dy;
                        largeBitmap[py*20+px/8]|=static_cast<unsigned char>(0x80u>>(px%8));
                    }
            }
            draw_bitmap_spi(80,112,160,64,1,largeRed?0xf05252:foreground,background,largeBitmap.data());
        }
        hadLarge=largeValue[0]!=0;
        previous=frame;oldReverse=reverse;oldAlertRow=alertRow;oldBold=bold;first=false;
    }
    lps::LoadResult load(lps::State& out)override{
        if(!mounted)return lps::LoadResult::NoStorage;
        const Slot a=readSlot(0,scratch),b=readSlot(1,scratch);
        // A newer schema is protected like an unreadable card: never overwritten.
        if(a.status==Slot::IOError||b.status==Slot::IOError||a.status==Slot::Unsupported||b.status==Slot::Unsupported)return lps::LoadResult::Error;
        if(a.status==Slot::Valid||b.status==Slot::Valid){
            active=b.status==Slot::Valid&&(a.status!=Slot::Valid||b.generation>a.generation)?1:0;
            // scratch holds B (parsed last); re-read A if A is the newest.
            if(active==0){const Slot again=readSlot(0,scratch);if(again.status!=Slot::Valid||again.checksum!=a.checksum)return lps::LoadResult::Error;}
            generation=active?b.generation:a.generation;out=scratch;return lps::LoadResult::Ok;
        }
        // Both JSON slots damaged: a JSON save existed, so the BIN files are
        // older data. Protect everything rather than fall back silently.
        if(a.status==Slot::Damaged&&b.status==Slot::Damaged)return lps::LoadResult::Error;
        return loadLegacy(out,a.status==Slot::Damaged||b.status==Slot::Damaged);
    }
    // Migration: no JSON save yet (or only an interrupted first JSON write).
    // Load SAVE_A/B.BIN formats 1-6; the next save writes SAVE_A.JSN.
    lps::LoadResult loadLegacy(lps::State& out,bool damagedJson){
        Record a=readLegacyRecord(0,scratch),b=readLegacyRecord(1,scratch);
        if(a.status==Record::IOError||b.status==Record::IOError)return lps::LoadResult::Error;
        if(a.status!=Record::Valid&&b.status!=Record::Valid){
            return a.status==Record::Missing&&b.status==Record::Missing&&!damagedJson?lps::LoadResult::Missing:lps::LoadResult::Error;
        }
        const auto& best=b.status==Record::Valid&&(a.status!=Record::Valid||b.generation>a.generation)?b:a;
        if(!lps::decode(best.bytes.data()+12,best.payloadSize,out))return lps::LoadResult::Error;
        active=-1;generation=best.generation;return lps::LoadResult::Ok;
    }
    bool save(const lps::State& state)override{
        if(!mounted||writeFault||generation==UINT32_MAX)return false;
        FRESULT result=f_mkdir("0:/LPS");if(result!=FR_OK&&result!=FR_EXIST)return false;
        const unsigned slot=active==0?1:0;
        FIL f;result=f_open(&f,paths[slot],FA_WRITE|FA_CREATE_ALWAYS);if(result!=FR_OK)return false;
        // Streamed through a 256-byte buffer: no whole-file copy in RAM.
        FileSink sink(f);uint32_t checksum=0;
        const bool written=lps::writeSave(state,generation+1,sink,checksum);
        const auto synced=f_sync(&f),closed=f_close(&f);
        // A/B preserves the active slot. If a late write/sync/readback fails,
        // outcome may be ambiguous: block further writes until restart/recovery.
        if(!written||synced!=FR_OK||closed!=FR_OK){writeFault=true;return false;}
        const Slot check=readSlot(slot,scratch);
        if(check.status!=Slot::Valid||check.generation!=generation+1||check.checksum!=checksum){writeFault=true;return false;}
        active=int(slot);++generation;return true;
    }
    bool exportIcs(const lps::Day& day,uint32_t,uint32_t,lps::Language language,const lps::State& state)override{
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
        bool first=true;lps::eachExportedActivity(state,day,language,[&](const char* name){
            if(!first)ok=ok&&put(f,", ");
            ok=ok&&escaped(f,name);first=false;});
        if(first)ok=ok&&put(f,"none");
        // RFC 5545 TEXT line breaks are a single backslash followed by N. Do
        // not run that escape through escaped(), or it becomes a literal \\N.
        // LPS-112-CHG03: the XP line is kept, empty, so ICS structure stays compatible.
        ok=ok&&put(f,"\\NXP:");
        if(day.expected>=0){ok=ok&&put(f,"\\N");std::snprintf(line,sizeof line,"%s: %ld.%03ld kg",language==lps::Language::English?"Expected weight":"Poids attendu",long(day.expected/1000),long(day.expected%1000));ok=ok&&escaped(f,line);}
        if(day.actual>=0){ok=ok&&put(f,"\\N");std::snprintf(line,sizeof line,"%s: %ld.%03ld kg",language==lps::Language::English?"Actual weight":"Poids effectif",long(day.actual/1000),long(day.actual%1000));ok=ok&&escaped(f,line);}
        if(day.note[0]){ok=ok&&put(f,"\\NNote: ");ok=ok&&escaped(f,day.note.data());}
        ok=ok&&put(f,"\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        const auto sync=f_sync(&f),close=f_close(&f);
        if(!(ok&&sync==FR_OK&&close==FR_OK)){f_unlink(path);return false;}
        return true;
    }
    bool exportWeightCsv(const lps::Day& day)override{
        if(!mounted||!lps::validDate(day.date))return false;
        FRESULT r=f_mkdir("0:/LPS");if(r!=FR_OK&&r!=FR_EXIST)return false;
        r=f_mkdir("0:/LPS/WEIGHTS");if(r!=FR_OK&&r!=FR_EXIST)return false;
        char path[40],date[16],expected[16]{},actual[16]{},line[64];
        std::snprintf(path,sizeof path,"0:/LPS/WEIGHTS/%04u.csv",day.date.year);
        std::snprintf(date,sizeof date,"%04u-%02u-%02u",day.date.year,day.date.month,day.date.day);
        FIL input;r=f_open(&input,path,FA_READ);
        size_t used=0;
        if(r==FR_OK){
            const size_t size=f_size(&input);UINT got=0;
            if(size>=csv.size()||f_read(&input,csv.data(),size,&got)!=FR_OK||got!=size||f_close(&input)!=FR_OK)return false;
            used=size;
            // A retry after an ICS or A/B failure sees the existing dated row.
            for(size_t i=0;i+11<=used;++i)if((i==0||csv[i-1]=='\n')&&std::memcmp(csv.data()+i,date,10)==0&&csv[i+10]==',')return true;
        }else if(r!=FR_NO_FILE&&r!=FR_NO_PATH)return false;
        if(day.expected>=0)std::snprintf(expected,sizeof expected,"%ld.%03ld",long(day.expected/1000),long(day.expected%1000));
        if(day.actual>=0)std::snprintf(actual,sizeof actual,"%ld.%03ld",long(day.actual/1000),long(day.actual%1000));
        const int written=std::snprintf(line,sizeof line,"%s,%s,%s\r\n",date,expected,actual);
        constexpr char header[]="date,expected_kg,measured_kg\r\n";
        if(written<=0||size_t(written)>=sizeof line)return false;
        // Append rather than rewrite: a write error must never discard a
        // previously recorded year.  FatFs positions FA_OPEN_APPEND at EOF.
        FIL output;r=f_open(&output,path,FA_WRITE|FA_OPEN_APPEND);if(r!=FR_OK)return false;
        bool ok=true;
        if(!used)ok=put(output,header);
        ok=ok&&put(output,line);
        const auto sync=f_sync(&output),close=f_close(&output);
        if(!(ok&&sync==FR_OK&&close==FR_OK))return false;
        return true;
    }
};
int keymap(int c){
    // ClockworkPi Code/picocalc_kbd_tester/keyboard_define.h
    switch(c){case 0xb5:return lps::Up;case 0xb6:return lps::Down;case 0xb4:return lps::Left;case 0xb7:return lps::Right;
    case 0xb1:case 27:return lps::Escape;case 10:case 13:return lps::Enter;case 8:case 127:return lps::Backspace;
    case 0xd4:return lps::DeleteKey;case 0x81:return lps::F1;case 0x82:return lps::F2;case 0x83:return lps::F3;case 0x84:return lps::F4;case 0x85:return lps::F5;
    default:return (c>=32&&c<=126)||c==9?c:-1;}
}
}
int main(){
    static PicoCalc hardware;
    hardware.init();
    static lps::App app(hardware);
    app.start();
    while(true){app.tick(time_us_64()/1000);int k=keymap(read_i2c_kbd());if(k>=0)app.key(k);sleep_ms(5);}
}
