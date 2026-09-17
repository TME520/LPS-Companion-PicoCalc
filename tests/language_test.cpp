#include "../src/lps_companion.cpp"
#include <cassert>
#include <string>
using namespace lps;
class Display final:public Platform {
public:
    std::array<std::array<char,41>,20> rows{};
    Blob stored{};bool exists=false,fail=false,failExport=false;unsigned writes=0,exports=0;
    void clear()override{for(auto& row:rows)row.fill(0);}
    void text(int x,int y,const char* text,bool)override{
        assert(x==0&&y>=0&&y%16==0&&y+16<=320);
        assert(std::strlen(text)<=40); // Fail on untranslated byte widths or clipped hints.
        for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p)
            assert((*p>=32&&*p<=126)||*p==0xe9||*p==0xe8||*p==0xe0||*p==0xf4||*p==0xfb||*p==0xe7||*p==0xc9);
        std::snprintf(rows[y/16].data(),41,"%s",text);
    }
    bool has(int row,const char* utf8)const{
        char cells[100];displayText(utf8,cells,sizeof cells);
        return std::strstr(rows[row].data(),cells)!=nullptr;
    }
    void present()override{}
    LoadResult load(uint8_t* b,std::size_t cap,std::size_t& n)override{
        if(!exists)return LoadResult::Missing;
        assert(stored.size<=cap);n=stored.size;std::memcpy(b,stored.bytes.data(),n);return LoadResult::Ok;
    }
    bool save(const uint8_t* b,std::size_t n)override{
        if(fail)return false;
        ++writes;exists=true;stored.size=n;std::memcpy(stored.bytes.data(),b,n);return true;
    }
    bool exportIcs(const Day&,uint32_t,uint32_t,Language)override{if(failExport)return false;++exports;return true;}
};
void choose(App& app,Language language){app.key('5');app.key('1');app.key(language==Language::French?'2':'1');}
void checkMenus(Display& d,App& a,bool fr){
    assert(d.has(6,"0  Date"));
    assert(d.has(8,fr?"1  Activités":"1  Activities"));
    assert(d.has(10,fr?"2  Bloc notes":"2  Notepad"));
    assert(d.has(12,fr?"3  Suivi du poids":"3  Weight tracker"));
    assert(d.has(14,fr?"4  Terminer le jour":"4  Close the day"));
    assert(d.has(16,"5  Config"));
    a.key('0');assert(d.has(3,"DATE"));assert(d.has(6,"2026-09-17"));
    assert(d.has(9,fr?"Calendrier":"Calendar"));a.key(Escape);
    a.key('1');assert(d.has(3,fr?"ACTIVITÉS":"ACTIVITIES"));assert(d.has(7,fr?"Régime":"Diet"));
    a.key(Right);assert(d.has(13,fr?"Congé":"Day off"));a.key(Right);a.key(Right);a.key(Escape);
    a.key('2');assert(d.has(3,fr?"BLOC NOTES":"NOTEPAD"));a.key(Escape);
    a.key('3');assert(d.has(3,fr?"SUIVI DU POIDS":"WEIGHT TRACKER"));
    assert(d.has(6,fr?"Attendu":"Expected"));assert(d.has(9,fr?"Effectif":"Actual"));
    a.key('0');a.key(Enter);assert(d.has(17,fr?"Poids invalide":"Invalid weight"));a.key(Escape);
    a.key('4');assert(d.has(3,fr?"TERMINER LE JOUR":"CLOSE THE DAY"));a.key(Escape);
    a.key('5');assert(d.has(6,fr?"1  Langue":"1  Language"));assert(d.has(9,fr?"2  Enregistrer":"2  Save"));
    a.key('1');assert(d.has(9,"Français"));a.key(Escape);a.key(Escape);
}
int main(){
    Display d;App a(d);a.start();assert(a.data().language==Language::English);checkMenus(d,a,false);
    a.key(Up);a.key(Enter);assert(a.currentScreen()==Screen::Config);
    a.key(Enter);a.key(Down);a.key(Escape);assert(d.has(6,"English"));
    a.key(Escape);assert(a.data().language==Language::English&&d.writes==0);
    choose(a,Language::French);assert(d.has(6,"Français"));assert(a.data().language==Language::English&&d.writes==0);
    a.key(Escape);assert(a.data().language==Language::English); // Discard before Save.
    choose(a,Language::French);d.fail=true;a.key('2');assert(a.currentScreen()==Screen::Config);
    assert(a.data().language==Language::English&&d.writes==0&&d.has(17,"Save failed"));
    d.fail=false;a.key('2');assert(a.currentScreen()==Screen::Home&&d.writes==1);
    assert(a.data().language==Language::French&&d.has(17,"Configuration enregistrée"));
    checkMenus(d,a,true);
    App reboot(d);reboot.start();assert(reboot.data().language==Language::French);checkMenus(d,reboot,true);
    // Proper names and user text are invariant; save settings never changes XP/days.
    reboot.key('2');for(char c:std::string("My French lesson"))reboot.key(c);reboot.key(Enter);
    reboot.key('1');reboot.key('1');reboot.key('2');reboot.key('3');reboot.key(Escape);
    choose(reboot,Language::English);reboot.key('2');
    assert(reboot.data().xp==0&&reboot.data().today.flags==7&&reboot.data().today.number==1);
    assert(std::strcmp(reboot.data().today.note.data(),"My French lesson")==0);
    reboot.key('4');reboot.key(Enter);assert(d.has(3,"DAY SAVED")&&d.has(8,"NEW KEEPSAKE"));
    assert(d.has(9,"Pocket notebook"));reboot.key(Enter);
    choose(reboot,Language::French);reboot.key('2');
    reboot.key(Left);reboot.key('2');assert(d.has(5,"My French lesson"));reboot.key(Escape);
    auto saved=d.stored;auto writes=d.writes;
    reboot.key('4');assert(d.has(17,"lecture seule"));reboot.key('5');assert(reboot.currentScreen()==Screen::Home);
    assert(d.writes==writes&&d.stored.bytes==saved.bytes);
    reboot.key(Right);reboot.key('1');d.fail=true;reboot.key('1');assert(d.has(17,"Échec sauvegarde"));
    d.fail=false;reboot.key('1');reboot.key('2');reboot.key('3');reboot.key(Escape);reboot.key('4');reboot.key(Enter);
    assert(d.has(3,"JOUR ENREGISTRÉ")&&d.has(9,"Tasse de thé"));reboot.key(Enter);
    assert(reboot.data().language==Language::French);
    // Reject an invalid language even if an attacker/error recomputed the CRC.
    auto malformed=encode(reboot.data());malformed.bytes[12]=2;
    auto crc=crc32(malformed.bytes.data(),malformed.size-4);
    for(unsigned i=0;i<4;++i)malformed.bytes[malformed.size-4+i]=uint8_t(crc>>(8*i));
    State decoded;assert(!decode(malformed.bytes.data(),malformed.size,decoded));
    d.stored=malformed;App corrupt(d);corrupt.start();assert(d.has(17,"Read error"));
    choose(corrupt,Language::French);corrupt.key('2');assert(d.has(17,"writes blocked"));
    std::puts("PASS: EN/FR menus, all screen widths, accents, save/cancel/failure, reboot, unchanged user data, history, rewards and invalid-language protection.");
}
