// LPS-115-CHG03: Config > Activities, the show/hide list of home menu entries.
#include "../src/lps_companion.cpp"
#include <cassert>
#include <cstring>
#include <string>
using namespace lps;
class Display final:public Platform {
public:
    bool fail=false;unsigned saves=0;State saved{};
    std::array<std::array<char,41>,20> rows{};std::array<bool,20> rev{};
    void clear()override{for(auto& r:rows)r.fill(0);rev.fill(false);}
    void text(int x,int y,const char* s,bool reverse)override{
        assert(x==0&&y>=0&&y%16==0&&y+16<=320&&std::strlen(s)<=40);std::snprintf(rows[y/16].data(),41,"%s",s);rev[y/16]=reverse;
    }
    void alert(int x,int y,const char* s)override{text(x,y,s,true);}
    void largeText(const char* v,bool)override{std::snprintf(rows[8].data(),41,"%s",v);}
    void present()override{}void setPalette(Palette)override{}
    LoadResult load(State&)override{return LoadResult::Missing;}
    bool save(const State& s)override{if(fail)return false;saved=s;++saves;return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(int row,const char* utf8)const{char cells[100];displayText(utf8,cells,sizeof cells);return std::strstr(rows[row].data(),cells)!=nullptr;}
    bool has(const char* utf8)const{for(int r=0;r<20;++r)if(has(r,utf8))return true;return false;}
};
int main(){
    static_assert(HomeLocked==0x101&&HomeAll==0x1ff,"Date and Close the day are locked");
    Display d;App a(d);a.start();
    a.key(Tab);assert(d.has(8,"3  Activities: 9/9")&&d.has(9,"4  Save"));
    a.key('3');assert(a.currentScreen()==Screen::HomeEntries&&d.rev[6]);
    for(int r=6;r<15;++r)assert(d.has(r,"[x] "));
    // Locked entries: Enter only explains; Up wraps to Close the day.
    a.key(Enter);assert(d.has(17,"Always shown")&&d.has(6,"[x] Date"));
    a.key(Up);assert(d.rev[14]&&d.has(14,"(locked)"));a.key(Enter);assert(d.has(17,"Always shown")&&d.has(14,"[x] Close the day"));
    // Toggling is pending until Save, like Language.
    a.key(Down);a.key(Down);assert(d.rev[7]);a.key(Enter);assert(d.has(7,"[ ] Activities")&&!d.has(17,"Always"));
    a.key(Down);a.key(Down);a.key(Enter);a.key(Enter);assert(d.has(9,"[x] Kanban")); // twice = unchanged
    a.key(' ');assert(a.currentScreen()==Screen::HomeEntries); // SPACE does nothing in Config
    assert(a.data().hiddenHome==0&&d.saves==0);
    a.key(Escape);assert(a.currentScreen()==Screen::Config&&d.has(8,"3  Activities: 8/9"));
    a.key('4');assert(a.currentScreen()==Screen::Home&&d.has("Configuration saved")&&d.saves==1);
    assert(a.data().hiddenHome==0x002&&d.saved.hiddenHome==0x002);
    assert(d.has(7,"1  Activities")&&d.has(14,"8  Close the day")); // the home menu is filtered in CHG04, not yet
    // Reopening starts from the saved choice; Tab drops a pending change, from the sub-screen too.
    a.key('3');const std::string kanban(d.rows[3].data());
    a.key(Tab);a.key('3');assert(d.has(7,"[ ] Activities"));a.key(Down);a.key(Enter);assert(d.has(7,"[x] Activities"));
    a.key(Tab);assert(a.currentScreen()==Screen::Kanban&&d.rows[3].data()==kanban&&a.data().hiddenHome==0x002);
    a.key(Tab);a.key('3');assert(d.has(7,"[ ] Activities"));a.key(Escape);a.key(Escape);assert(a.currentScreen()==Screen::Kanban);
    // Hide everything that can be hidden; save together with a language change.
    a.key(Tab);a.key('3');for(unsigned i=0;i<9;++i){a.key(Down);a.key(Enter);} // Activities toggles back on, the rest off
    assert(d.has(7,"[x] Activities")&&d.has(8,"[ ] Weight tracker")&&d.has(13,"[ ] Timer")&&d.has(6,"[x] Date")&&d.has(14,"[x] Close the day"));
    a.key(Escape);a.key('1');a.key('2');assert(d.has(8,"3  Activities: 3/9")); /* labels switch after Save */a.key('4');
    assert(a.data().language==Language::French&&a.data().hiddenHome==0x0fc&&a.currentScreen()==Screen::Kanban);
    a.key(Tab);a.key('3');a.key(Down);for(unsigned i=0;i<5;++i){a.key(Down);a.key(Enter);}
    a.key(Escape);assert(d.has(8,"3  Activités: 8/9"));
    // A failed save keeps the pending choice and the old state.
    d.fail=true;a.key('4');assert(a.currentScreen()==Screen::Config&&a.data().hiddenHome==0x0fc&&d.has(8,"8/9"));
    d.fail=false;a.key('4');assert(a.data().hiddenHome==0x080&&a.currentScreen()==Screen::Kanban); // only Timer hidden
    // The timer finishing over the sub-screen drops the pending choice.
    a.key(Escape);a.tick(1000);a.key('7');a.key('1');a.key(Tab);a.key('3');a.key(Down);a.key(Enter);
    a.tick(1000+300000);assert(a.currentScreen()==Screen::TimerComplete&&a.data().hiddenHome==0x080);
    a.key(Tab);assert(d.has(8,"8/9"));
    std::puts("PASS: Config Activities entry and count, 9 checkboxes, locked Date/Close the day, pending toggles with Save, Esc one level, Tab and timer drop pending, Space ignored, failed save, language saved together, home menu unchanged until CHG04.");
}
