// LPS-115-CHG03/04/05: Config > Activities, the filtered home menu and its save key.
#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <cstring>
#include <string>
#include <initializer_list>
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
// LPS-115-CHG05: a platform with a real JSON save, for restarts.
class Disk final:public Platform {
public:
    lps_test::JsonStore stored{};bool exists=false,fail=false;unsigned saves=0;
    std::array<std::array<char,41>,20> rows{};std::array<bool,20> rev{};
    void clear()override{for(auto& r:rows)r.fill(0);rev.fill(false);}
    void text(int x,int y,const char* s,bool reverse)override{assert(x==0&&std::strlen(s)<=40);std::snprintf(rows[y/16].data(),41,"%s",s);rev[y/16]=reverse;}
    void alert(int x,int y,const char* s)override{text(x,y,s,true);}
    void present()override{}void setPalette(Palette)override{}
    LoadResult load(State& state)override{if(!exists)return LoadResult::Missing;return stored.read(state);}
    bool save(const State& state)override{if(fail||!stored.write(state))return false;exists=true;++saves;return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(int row,const char* utf8)const{char cells[100];displayText(utf8,cells,sizeof cells);return std::strstr(rows[row].data(),cells)!=nullptr;}
};
// The JSON with "homeHidden" replaced (or removed when value is null), re-signed.
std::string withHidden(const std::string& json,const char* value){
    std::string j=json;const auto at=j.find(",\n\"homeHidden\":");assert(at!=std::string::npos);
    const auto end=j.find("]",at)+1;j.replace(at,end-at,value?std::string(",\n\"homeHidden\":")+value:std::string());
    lps_test::JsonStore s;s.assign(j);return s.text();
}
std::pair<SaveCheck,uint16_t> load(const std::string& json,const char* value){
    lps_test::JsonStore probe;probe.assign(withHidden(json,value));State s;const SaveCheck c=probe.check(s);return {c,s.hiddenHome};
}
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
    assert(d.has(7,"1  Weight tracker")&&d.has(13,"7  Close the day")&&!d.rows[14][0]); // LPS-115-CHG04: filtered
    // Reopening starts from the saved choice; Tab drops a pending change, from the sub-screen too.
    a.key('2');const std::string kanban(d.rows[3].data());
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
    // Timer hidden: 7 is now Close the day and 8 does nothing.
    a.key(Escape);assert(d.has(12,"6  Rappels")&&d.has(13,"7  Terminer le jour"));a.key('8');assert(a.currentScreen()==Screen::Home);
    a.key('7');assert(a.currentScreen()==Screen::Finish);a.key(Escape);
    a.key(Tab);a.key('3');a.key(Up);a.key(Up);a.key(Enter);a.key(Escape);a.key('4');assert(a.data().hiddenHome==0&&a.currentScreen()==Screen::Home);
    // The timer finishing over the sub-screen drops the pending choice.
    a.tick(1000);a.key('7');a.key('1');a.key(Tab);a.key('3');a.key(Down);a.key(Enter);
    a.tick(1000+300000);assert(a.currentScreen()==Screen::TimerComplete&&a.data().hiddenHome==0);
    a.key(Tab);assert(d.has(8,"9/9"));
    // LPS-115-CHG04: the home menu shows only the checked entries.
    {Display d;App a(d);a.start();
        auto hide=[&](std::initializer_list<unsigned> entries){ // toggle entries (HomeScreens indices) and save
            a.key(Tab);a.key('3');for(unsigned e:entries){for(unsigned i=0;i<e;++i)a.key(Down);a.key(Enter);for(unsigned i=0;i<e;++i)a.key(Up);}
            a.key(Escape);a.key('4');};
        a.key('1');a.key('1');a.key(Escape); // tick an activity first: hidden entries keep their data
        hide({1,5}); // Activities and Daily review
        const char* expected[]={"0  Date","1  Weight tracker","2  Kanban","3  Christian Rosary","4  Reminders","5  Timer","6  Close the day"};
        for(int i=0;i<7;++i)assert(!std::strcmp(d.rows[6+i].data(),expected[i]));
        assert(!d.rows[13][0]&&d.has(17,"<- Previous day")&&d.has(19,"Tab:Config")&&d.has(4,"1/22 activities")&&d.rev[6]);
        // Digits map to positions; keys past the end do nothing.
        a.key('1');assert(a.currentScreen()==Screen::Weight);a.key(Escape);
        a.key('5');assert(a.currentScreen()==Screen::TimerSelect);a.key(Escape);
        a.key('6');assert(a.currentScreen()==Screen::Finish);a.key(Escape);
        a.key('7');a.key('8');a.key('9');assert(a.currentScreen()==Screen::Home&&d.rev[6]);
        // Up/Down move over visible entries only and wrap.
        a.key(Up);assert(d.rev[12]);a.key(Down);assert(d.rev[6]);a.key(Down);a.key(Enter);assert(a.currentScreen()==Screen::Weight);a.key(Escape);
        // Cursor kept on the same entry after a Config save...
        a.key(Down);a.key(Down);assert(d.rev[8]&&d.has(8,"2  Kanban"));
        a.key(Tab);a.key('3');a.key(Down);a.key(Down);a.key(Enter);a.key(Escape);a.key('4'); // hide Weight tracker
        assert(d.rev[7]&&d.has(7,"1  Kanban")&&d.has(11,"5  Close the day"));
        // ...or moved to the next visible entry when its own entry is hidden.
        hide({3});assert(d.rev[7]&&d.has(7,"1  Christian Rosary"));a.key(Enter);assert(a.currentScreen()==Screen::RosaryMode);a.key(Escape); // Esc from a screen returns to Home on Date
        a.key(Down);a.key(Down);a.key(Down);assert(d.has(9,"3  Timer")&&d.rev[9]);
        hide({7});assert(d.has(9,"3  Close the day")&&d.rev[9]); // hiding the last hideable entry lands on Close the day
        hide({1,2,3,5,7});assert(d.rev[14]&&d.has(13,"7  Timer")&&d.has(14,"8  Close the day")); // all shown again, cursor kept on Close the day
        // Meal Planner return point: the entry hidden while in Config over the planner.
        a.key(Up);a.key(Up);assert(d.has(12,"6  Reminders")&&d.rev[12]);
        a.key(' ');a.key(Tab);a.key('3');for(int i=0;i<6;++i)a.key(Down);a.key(Enter);a.key(Escape);a.key('4');
        assert(a.currentScreen()==Screen::MealPlanner);a.key(' ');assert(a.currentScreen()==Screen::Home&&d.rev[12]&&d.has(12,"6  Timer"));
        // Archive day menu unchanged; hidden entries keep working behind the scenes.
        hide({1,2});assert(d.has(6,"0  Date")&&d.has(7,"1  Kanban"));
        a.key('0');a.key(Enter);a.key(Escape); // keep the date
        a.key(Up);a.key(Enter);assert(a.currentScreen()==Screen::Finish);a.key(Enter);assert(a.currentScreen()==Screen::Reward);a.key(Enter);
        a.key(Left);assert(d.has(6,"1  Activities")&&d.has(7,"2  Weight tracker")&&d.has(8,"3  Daily review")&&!d.rows[9][0]);
        a.key('1');assert(a.currentScreen()==Screen::Activities&&d.has("[x]"));a.key(Escape);a.key(Right);
        assert(d.has(7,"1  Kanban"));
        // The timer keeps running while its entry is hidden.
        assert(d.has(10,"4  Timer"));a.tick(1000);a.key('4');assert(a.currentScreen()==Screen::TimerSelect);a.key('1');
        hide({7});assert(a.currentScreen()==Screen::TimerRunning);a.tick(1000+300000);assert(a.currentScreen()==Screen::TimerComplete);
        a.key(Escape);assert(a.currentScreen()==Screen::Home&&d.has(10,"4  Close the day")&&!d.has("Timer"));
    }
    // LPS-115-CHG05: the choice is saved as "homeHidden" with stable ids, after "meals", crc32 last.
    {Disk d;
        {App a(d);a.start();a.key('0');a.key(Enter); // first save: nothing hidden
         assert(d.stored.text().find("\"meals\":{\"played\":[]},\n\"homeHidden\":[]},\n\"crc32\":")!=std::string::npos);
         a.key(Tab);a.key('3');a.key(Down);a.key(Enter);for(int i=0;i<4;++i)a.key(Down);a.key(Enter);a.key(Escape);a.key('4');}
        const std::string json=d.stored.text();
        assert(json.find("\"homeHidden\":[\"activities\",\"daily_review\"]},\n\"crc32\":\"")!=std::string::npos);
        assert(json.find("\"version\":2,")!=std::string::npos);
        {State s;assert(d.stored.check(s)==SaveCheck::Valid&&s.hiddenHome==0x022);}
        // Restart keeps the filtered menu.
        {App b(d);b.start();assert(b.data().hiddenHome==0x022);
         const char* expected[]={"0  Date","1  Weight tracker","2  Kanban","3  Christian Rosary","4  Reminders","5  Timer","6  Close the day"};
         for(int i=0;i<7;++i)assert(!std::strcmp(d.rows[6+i].data(),expected[i]));assert(!d.rows[13][0]);
         // A failed save keeps the pending choice, the old state and the old file.
         b.key(Tab);b.key('3');b.key(Down);b.key(Enter);b.key(Escape);assert(d.has(8,"3  Activities: 8/9"));
         d.fail=true;b.key('4');assert(b.currentScreen()==Screen::Config&&b.data().hiddenHome==0x022&&d.has(8,"8/9")&&d.stored.text()==json);
         d.fail=false;b.key('4');assert(b.data().hiddenHome==0x020&&d.stored.text().find("\"homeHidden\":[\"daily_review\"]")!=std::string::npos);}
        {App c(d);c.start();assert(c.data().hiddenHome==0x020&&d.has(7,"1  Activities")&&d.has(11,"5  Reminders"));}
        // v1.14 save (key absent): everything shown.
        {lps_test::JsonStore old;old.assign(withHidden(json,nullptr));assert(old.text().find("homeHidden")==std::string::npos);
         Disk v114;v114.stored=old;v114.exists=true;App a(v114);a.start();assert(a.data().hiddenHome==0&&v114.has(14,"8  Close the day"));}
        // Ids: all of them, unknown ones ignored, locked ones never hidden, duplicates harmless.
        assert(load(json,"[\"activities\",\"weight\",\"kanban\",\"rosary\",\"daily_review\",\"reminders\",\"timer\"]")==std::make_pair(SaveCheck::Valid,uint16_t(0x0fe)));
        assert(load(json,"[\"activities\",\"sauna\",\"DAILY_REVIEW\",\"\",\"timer\",\"timer\"]")==std::make_pair(SaveCheck::Valid,uint16_t(0x082)));
        assert(load(json,(std::string("[\"")+std::string(300,'x')+"\",\"kanban\"]").c_str())==std::make_pair(SaveCheck::Valid,uint16_t(0x008)));
        assert(load(json,"[\"date\",\"close_day\",\"reminders\"]")==std::make_pair(SaveCheck::Valid,uint16_t(0x040)));
        assert(load(json,"[]")==std::make_pair(SaveCheck::Valid,uint16_t(0)));
        // Wrong shapes are damage, like the rest of the save.
        assert(load(json,"\"activities\"").first==SaveCheck::Damaged);
        assert(load(json,"{}").first==SaveCheck::Damaged);
        assert(load(json,"[1]").first==SaveCheck::Damaged);
        assert(load(json,"[null]").first==SaveCheck::Damaged);
        assert(load(json,"[[\"kanban\"]]").first==SaveCheck::Damaged);
        // A locked bit set in RAM is never written.
        {State s;s.hiddenHome=0x1ff;lps_test::JsonStore w=lps_test::encodeJson(s);
         assert(w.text().find("\"homeHidden\":[\"activities\",\"weight\",\"kanban\",\"rosary\",\"daily_review\",\"reminders\",\"timer\"]")!=std::string::npos);
         State back;assert(w.check(back)==SaveCheck::Valid&&back.hiddenHome==0x0fe);}
    }
    std::puts("PASS: Config Activities entry and count, 9 checkboxes, locked Date/Close the day, pending toggles with Save, Esc one level, Tab and timer drop pending, Space ignored, failed save, language saved together, home menu filtered and renumbered, digits/arrows on visible entries, cursor kept or moved to the next entry, Meal Planner return, archive menu and hidden-entry data unchanged, timer behind a hidden entry; homeHidden saved by id after meals with crc32 last, restart keeps the filtered menu, v1.14 save shows all, unknown/overlong ids ignored, locked ids never hidden, wrong shapes damaged, failed save keeps pending and file.");
}
