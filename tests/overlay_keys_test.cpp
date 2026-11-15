// LPS-115-CHG02: TAB (Config) and SPACE (Meal Planner) from any screen of today.
#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <cstring>
#include <string>
using namespace lps;
class Display final:public Platform {
public:
    lps_test::JsonStore stored{};bool exists=false,failMealCsv=false;unsigned saves=0,mealExports=0;MealDeckState lastDeck{};
    std::array<std::array<char,41>,20> rows{};std::array<bool,20> rev{};
    void clear()override{for(auto& r:rows)r.fill(0);rev.fill(false);}
    void text(int x,int y,const char* s,bool reverse)override{
        assert(x==0&&y>=0&&y%16==0&&y+16<=320&&std::strlen(s)<=40);std::snprintf(rows[y/16].data(),41,"%s",s);rev[y/16]=reverse;
    }
    void largeText(const char* value,bool)override{std::snprintf(rows[8].data(),41,"%s",value);}
    void alert(int x,int y,const char* s)override{text(x,y,s,true);}
    void present()override{}void setPalette(Palette)override{}
    LoadResult load(State& state)override{if(!exists)return LoadResult::Missing;return stored.read(state);}
    bool save(const State& state)override{if(!stored.write(state))return false;exists=true;++saves;return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool exportMealCsv(const Date&,const MealDeckState& m)override{++mealExports;lastDeck=m;return !failMealCsv;}
    bool has(int row,const char* utf8)const{char cells[100];displayText(utf8,cells,sizeof cells);return std::strstr(rows[row].data(),cells)!=nullptr;}
    bool has(const char* utf8)const{for(int r=0;r<20;++r)if(has(r,utf8))return true;return false;}
    std::string frame()const{std::string f;for(int r=0;r<20;++r){f+=rows[r].data();f+=rev[r]?"#\n":"\n";}return f;}
};
State base(){
    State s;s.today.date={2026,11,15};
    const char* names[]={"alpha","beta","gamma","delta"};
    for(unsigned i=0;i<4;++i){s.tasks[i].status=i<2?TaskStatus::Todo:TaskStatus::Doing;std::snprintf(s.tasks[i].name.data(),s.tasks[i].name.size(),"%s",names[i]);}
    s.taskCount=4;
    s.reminders[0].from={2026,11,10};s.reminders[0].duration=30;std::snprintf(s.reminders[0].text.data(),s.reminders[0].text.size(),"Water plants");
    s.reminders[1].from={2026,11,1};s.reminders[1].duration=60;std::snprintf(s.reminders[1].text.data(),s.reminders[1].text.size(),"Call bank");
    s.reminderCount=2;
    return s;
}
// A fresh app on today's home screen (the reminder popup is dismissed).
struct Fixture {
    Display d;App a;
    Fixture():a(d){d.stored=lps_test::encodeJson(base());d.exists=true;a.start();assert(a.currentScreen()==Screen::ReminderPopup);a.key(Escape);assert(a.currentScreen()==Screen::Home);}
};
// TAB and SPACE each open their overlay and close back to the identical frame
// (same screen, cursor, page, column, scroll), by the same key or by Esc.
void roundTrips(Fixture& f,Screen s){
    App& a=f.a;Display& d=f.d;assert(a.currentScreen()==s);const std::string before=d.frame();const unsigned exports=d.mealExports,saves=d.saves;
    a.key(Tab);assert(a.currentScreen()==Screen::Config&&d.has(3,"CONFIGURATION"));a.key(Tab);assert(a.currentScreen()==s&&d.frame()==before);
    a.key(Tab);a.key(Escape);assert(a.currentScreen()==s&&d.frame()==before);
    a.key(Tab);a.key('1');a.key(Tab);assert(a.currentScreen()==s&&d.frame()==before); // from a sub-screen, TAB closes Config entirely
    a.key(' ');assert(a.currentScreen()==Screen::MealPlanner&&d.has(3,"MEAL PLANNER"));a.key(' ');assert(a.currentScreen()==s&&d.frame()==before);
    a.key(' ');a.key(Escape);assert(a.currentScreen()==s&&d.frame()==before);
    assert(d.mealExports==exports+2&&d.saves==saves); // every exit writes the CSV; nothing is saved
}
int main(){
    // 1. Menus, lists and views: TAB/SPACE open the overlay and return to the same place.
    {Fixture f;App& a=f.a;Display& d=f.d;
        a.key(Down);a.key(Down);a.key(Down);roundTrips(f,Screen::Home);assert(d.rev[9]&&d.has(9,"3  Kanban"));
        a.key(Enter);a.key(Right);a.key(Down);assert(d.has("delta"));roundTrips(f,Screen::Kanban); // Doing column, second task
        a.key(Escape);a.key('1');a.key(Right);a.key(Down);a.key(Down);roundTrips(f,Screen::Activities);assert(d.has("2/3"));
        a.key('3');assert(a.data().today.flags==(ActivityFlags(1u)<<12)); // keys still act on page 2 afterwards
        a.key(Escape);a.key('5');a.key(Down);roundTrips(f,Screen::DailyReview);
        a.key(Escape);a.key('6');a.key(Down);roundTrips(f,Screen::Reminders);
        a.key(Enter);assert(a.currentScreen()==Screen::ReminderEdit&&d.has("Call bank"));a.key(Escape);a.key(Escape);
        a.key('7');a.key(Down);roundTrips(f,Screen::TimerSelect);
        a.key(Escape);a.key('4');a.key(Down);roundTrips(f,Screen::RosaryMode);a.key(Up);a.key(Enter);
        a.key(Down);a.key(Down);roundTrips(f,Screen::RosarySet);a.key(Enter);
        a.key(Down);roundTrips(f,Screen::RosaryDecade);a.key(Enter);
        a.key(Right);a.key(Right);a.key(Right);roundTrips(f,Screen::RosaryPrayer);
        for(int i=0;i<200&&a.currentScreen()==Screen::RosaryPrayer;++i)a.key(Right);
        roundTrips(f,Screen::RosaryComplete);a.key(Enter);
        a.key('8');a.key(Enter);roundTrips(f,Screen::Reward);a.key(Enter);assert(a.currentScreen()==Screen::Home);
    }
    // 2. Text entry: SPACE stays a character, TAB does not leave the editor.
    {Fixture f;App& a=f.a;Display& d=f.d;
        a.key('3');a.key('n');a.key('a');a.key(' ');a.key(Tab);a.key('b');assert(a.currentScreen()==Screen::TaskEdit);a.key(Enter);
        assert(!std::strcmp(a.data().tasks[4].name.data(),"a b"));
        a.key(Escape);a.key('1');a.key('n');a.key('x');a.key(' ');a.key(Tab);a.key('y');assert(a.currentScreen()==Screen::ActivityEdit);a.key(Enter);
        assert(!std::strcmp(a.data().activities[22].english.data(),"x y"));
        a.key(Escape);a.key('6');a.key('n');a.key(Down);a.key(Down);a.key('p');a.key(' ');a.key(Tab);a.key('q');assert(a.currentScreen()==Screen::ReminderEdit&&d.has("p q"));
        a.key(Escape);a.key(Escape);
        a.key('0');a.key(' ');a.key(Tab);assert(a.currentScreen()==Screen::Date&&d.has("2026-11-15"));a.key(Escape);
        // LPS-115-CHG02b: on Weight (numeric entry) TAB opens Config and returns
        // with the field and the digits typed so far; SPACE stays ignored.
        a.key('2');a.key('1');a.key(Down);a.key('2');a.key('.');assert(d.rev[9]&&d.has(9,"Actual   : 2."));
        const std::string typed=d.frame();
        a.key(' ');assert(a.currentScreen()==Screen::Weight&&d.frame()==typed);
        a.key(Tab);assert(a.currentScreen()==Screen::Config);a.key(Tab);assert(a.currentScreen()==Screen::Weight&&d.frame()==typed);
        a.key(Tab);a.key('1');a.key(Escape);a.key(Escape);assert(a.currentScreen()==Screen::Weight&&d.frame()==typed);
        a.key(Tab);a.key('2');a.key(Tab);assert(d.frame()==typed);
        a.key('5');a.key(Enter);assert(a.currentScreen()==Screen::Home&&a.data().today.expected==1000&&a.data().today.actual==2500);
        // Saving a language from Weight keeps the drafts too.
        a.key('2');for(int i=0;i<8;++i)a.key(Backspace);a.key('9');
        a.key(Tab);a.key('1');a.key('2');a.key('3');assert(a.currentScreen()==Screen::Weight&&d.has(6,"Attendu  : 9")&&d.rev[6]&&d.has("Configuration enregistrée"));
        a.key(Enter);assert(a.data().today.expected==9000&&a.data().today.actual==2500);
        a.key(Tab);a.key('1');a.key('1');a.key('3');
        assert(d.mealExports==0);
    }
    // 3. Confirmations: TAB/SPACE change nothing; the reminder popup is dismissed by any key.
    {Fixture f;App& a=f.a;Display& d=f.d;
        a.key('1');a.key(DeleteKey);assert(d.has("Delete"));a.key(Tab);a.key(' ');assert(a.currentScreen()==Screen::Activities&&d.has("Delete"));
        a.key(Escape);assert(!d.has("Delete")&&a.data().activityCount==22);a.key(Escape);
        a.key('8');a.key(Tab);a.key(' ');assert(a.currentScreen()==Screen::Finish);a.key(Escape);
        App popup(d);popup.start();assert(popup.currentScreen()==Screen::ReminderPopup);popup.key(Tab);assert(popup.currentScreen()==Screen::Home);
        App popup2(d);popup2.start();popup2.key(' ');assert(popup2.currentScreen()==Screen::Home);
        assert(d.mealExports==0);
    }
    // 4. Cross-open: TAB from the planner opens Config, which closes back to the
    // planner (card kept); SPACE is ignored in Config; Esc goes up one level.
    {Fixture f;App& a=f.a;Display& d=f.d;
        a.key('3');a.key(Right);a.key(Down);const std::string kanban=d.frame();
        a.key(' ');a.key(Down);a.key(Right);const std::string planner=d.frame();assert(d.has("Food 2/8"));
        a.key(Tab);assert(a.currentScreen()==Screen::Config);
        a.key(' ');assert(a.currentScreen()==Screen::Config);a.key('2');a.key(Down);a.key(' ');assert(a.currentScreen()==Screen::Colors);
        a.key(Escape);assert(a.currentScreen()==Screen::Config);a.key(Escape);assert(a.currentScreen()==Screen::MealPlanner&&d.frame()==planner);
        a.key(Tab);a.key(Tab);assert(d.frame()==planner);
        assert(d.mealExports==0); // Config inside the planner does not leave it
        a.key(Enter);assert(a.data().meals.playedCount==1);a.key(Escape);assert(a.currentScreen()==Screen::Kanban&&d.frame()==kanban);
        assert(d.mealExports==1&&d.lastDeck.playedCount==1);
        // Saving Config returns to the screen it was opened from, in the new language.
        a.key(Tab);a.key('1');a.key('2');a.key('3');assert(a.currentScreen()==Screen::Kanban&&a.data().language==Language::French&&d.has("Configuration enregistrée")&&d.has("delta"));
        // A CSV failure is reported on the screen the planner returns to.
        d.failMealCsv=true;a.key(' ');a.key(' ');assert(a.currentScreen()==Screen::Kanban&&d.has("Export CSV repas échoué")&&d.has("delta"));
        // A Rosary page past the end of the new language's text is clamped on return.
        d.failMealCsv=false;a.key(Escape);a.key('4');a.key(Enter);a.key(Enter);a.key(Enter);
        for(int i=0;i<6;++i)a.key(Right);
        a.key(Tab);a.key('1');a.key('1');a.key('3');assert(a.currentScreen()==Screen::RosaryPrayer&&a.data().language==Language::English);
        a.key(Left);a.key(Right);assert(a.currentScreen()==Screen::RosaryPrayer);
    }
    // 5. Archived days keep the read-only message.
    {Display d;State s=base();s.reminderCount=0;s.history[0]=s.today;s.history[0].date={2026,11,14};s.next=1;s.count=1;
        d.stored=lps_test::encodeJson(s);d.exists=true;App a(d);a.start();
        a.key(Left);a.key(Tab);assert(a.currentScreen()==Screen::Home&&d.has("Archived day: read-only"));
        a.key(Down);a.key(' ');assert(a.currentScreen()==Screen::Home&&d.has("Archived day: read-only"));
        a.key('1');assert(a.currentScreen()==Screen::Activities);a.key(Tab);assert(a.currentScreen()==Screen::Activities&&d.has("Archived day: read-only"));
        a.key(Escape);a.key('2');assert(a.currentScreen()==Screen::Weight);a.key(Tab);assert(a.currentScreen()==Screen::Weight&&d.has("Archived day: read-only"));
        a.key(Escape);a.key('1');
        a.key(' ');assert(a.currentScreen()==Screen::Activities&&d.mealExports==0);
    }
    // 6. The timer keeps counting behind Config and the planner, and its alert takes over.
    {Fixture f;App& a=f.a;Display& d=f.d;
        a.tick(1000);a.key('7');a.key('1');assert(d.has(8,"05:00"));
        a.key(Tab);a.tick(61000);assert(a.currentScreen()==Screen::Config);a.key(Tab);assert(a.currentScreen()==Screen::TimerRunning&&d.has(8,"04:00"));
        a.key(' ');a.key(Tab);a.tick(121000);a.key(Escape);a.tick(122000);a.key(' ');assert(a.currentScreen()==Screen::TimerRunning&&d.has(8,"02:59"));
        a.key(Enter);a.key(Tab);a.tick(500000);a.key(Tab);assert(d.has(8,"02:59")); // paused stays paused
        a.key(Enter);a.key(' ');a.key(Enter);a.tick(678999);assert(a.currentScreen()==Screen::MealPlanner);
        a.tick(679000);assert(a.currentScreen()==Screen::TimerComplete&&d.has("TIMER FINISHED!")&&d.mealExports==2&&d.lastDeck.playedCount==1);
        // Expiry inside Config drops the pending choice; inside Config over the planner it also writes the CSV.
        a.key('1');a.key(Tab);a.key('1');a.key('2');a.tick(679000+300000);
        assert(a.currentScreen()==Screen::TimerComplete&&a.data().language==Language::English&&d.saves==1);
        a.key(Tab);assert(a.currentScreen()==Screen::Config&&d.has("English"));a.key(Tab);
        a.key('1');a.key(' ');a.key(Tab);a.tick(979000+300000);assert(a.currentScreen()==Screen::TimerComplete&&d.mealExports==3);
        a.key(Enter);assert(a.currentScreen()==Screen::TimerSelect);
        // Esc on the running timer still cancels it: no alert later.
        a.key('1');a.key(Escape);a.tick(9999999);assert(a.currentScreen()==Screen::Home);
        // Reminders are unaffected: the popup still comes at start-up.
        App later(d);later.start();assert(later.currentScreen()==Screen::ReminderPopup&&d.has("Water plants"));
    }
    std::puts("PASS: TAB/SPACE from menus, lists, views, Timer, Rosary and Reward with identical return; text editors keep SPACE/TAB; Weight: TAB opens Config keeping field and digits, SPACE ignored; confirmations and reminder popup untouched; planner/Config cross-open and one-level Esc; CSV on every planner exit; archive read-only; timer runs, pauses and alerts behind overlays; reminders at start-up.");
}
