#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <string>
#include <vector>
using namespace lps;
class Display final:public Platform {
public:
    lps_test::JsonStore stored{};bool exists=false;
    std::array<std::array<char,41>,20> rows{};
    void clear()override{for(auto& row:rows)row.fill(0);}
    void text(int x,int y,const char* text,bool)override{
        assert(x==0&&y>=0&&y%16==0&&y+16<=320);
        assert(std::strlen(text)<=40);
        std::snprintf(rows[y/16].data(),41,"%s",text);
    }
    void alert(int x,int y,const char* value)override{this->text(x,y,value,true);}
    void present()override{}
    void setPalette(Palette)override{}
    LoadResult load(State& state)override{if(!exists)return LoadResult::Missing;return stored.read(state);}
    bool save(const State& state)override{if(!stored.write(state))return false;exists=true;return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(int row,const char* utf8)const{char cells[100];displayText(utf8,cells,sizeof cells);return std::strstr(rows[row].data(),cells)!=nullptr;}
    bool has(const char* utf8)const{for(int r=0;r<20;++r)if(has(r,utf8))return true;return false;}
};
void choose(App& app,Language language){app.key('6');app.key('1');app.key(language==Language::French?'2':'1');app.key('3');}
void clearDraft(App& app,unsigned n){for(unsigned i=0;i<n;++i)app.key(Backspace);}
std::vector<std::string> exported(const State& s,const Day& d,Language language){
    std::vector<std::string> names;
    eachExportedActivity(s,d,language,[&](const char* name){names.push_back(name);});
    return names;
}
int main(){
    // Language-only rename and fallback display, English then French.
    {
        Display d;App app(d);app.start();
        app.key('1');app.key('e');assert(app.currentScreen()==Screen::ActivityEdit&&d.has(3,"RENAME ACTIVITY"));
        clearDraft(app,4);for(char c:std::string("Stroll"))app.key(c);app.key(Enter);
        assert(app.currentScreen()==Screen::Activities);
        assert(std::strcmp(app.data().activities[0].english.data(),"Stroll")==0);
        assert(std::strcmp(app.data().activities[0].french.data(),"Marche")==0);
        assert(d.has(5,"1  Stroll"));
        app.key(Escape);choose(app,Language::French);assert(app.data().language==Language::French);
        app.key('1');assert(d.has(5,"1  Marche")&&!d.has("Stroll"));
        app.key(Escape);choose(app,Language::English);
        app.key('1');assert(d.has(5,"1  Stroll"));
        // Empty Enter cancels; extra spaces trim; duplicate (case-insensitive) refused.
        app.key('e');clearDraft(app,10);app.key(Enter);assert(app.currentScreen()==Screen::Activities);
        app.key(Down);app.key('e');clearDraft(app,8);for(char c:std::string("  stroll  "))app.key(c);app.key(Enter);
        assert(d.has("That name already exists")&&std::strcmp(app.data().activities[1].english.data(),"Diet")==0);
        app.key(Escape);app.key('e');clearDraft(app,8);for(char c:std::string("  Solo  "))app.key(c);app.key(Enter);
        assert(std::strcmp(app.data().activities[1].english.data(),"Solo")==0);
        // Create in French only: the English screen shows the same name.
        app.key(Escape);choose(app,Language::French);
        app.key('1');app.key('n');assert(d.has(3,"NOUVELLE ACTIVITÉ"));
        for(char c:std::string("Piano"))app.key(c);app.key(Enter);
        assert(app.data().activityCount==23);
        assert(std::strcmp(app.data().activities[22].french.data(),"Piano")==0&&!app.data().activities[22].english[0]);
        assert(d.has("Piano"));
        app.key(Escape);choose(app,Language::English);
        app.key('1');app.key(Right);app.key(Right);assert(d.has("Piano")); // fallback, no '?' and no blank
        // Round-trip the custom list through the JSON save.
        App again(d);again.start();
        assert(again.data().activityCount==23);
        assert(std::strcmp(again.data().activities[0].english.data(),"Stroll")==0);
        assert(std::strcmp(again.data().activities[0].french.data(),"Marche")==0);
        assert(std::strcmp(again.data().activities[22].french.data(),"Piano")==0);
    }
    // Today's tick follows rename and move; delete asks for confirmation.
    {
        Display d;App app(d);app.start();
        app.key('1');app.key(Enter);assert(app.data().today.flags&1);
        app.key('e');clearDraft(app,4);for(char c:std::string("Stroll"))app.key(c);app.key(Enter);
        assert((app.data().today.flags&1)&&d.has(5,"1  Stroll")&&d.has(5,"[x]"));
        app.key(F5); // Swap with Diet; the tick moves with Stroll.
        assert(std::strcmp(app.data().activities[1].english.data(),"Stroll")==0);
        assert((app.data().today.flags&(1u<<1))&&!(app.data().today.flags&1));
        assert(d.has(6,"2  Stroll")&&d.has(6,"[x]"));
        app.key(F4);assert(std::strcmp(app.data().activities[0].english.data(),"Stroll")==0&&(app.data().today.flags&1));
        app.key(DeleteKey);assert(d.has("Delete Stroll?")&&d.has("Enter: yes"));
        app.key(Escape);assert(app.data().activityCount==22&&(app.data().today.flags&1)); // Esc cancels
        app.key(DeleteKey);app.key(Enter);
        assert(app.data().activityCount==21&&!(app.data().today.flags&1));
        assert(std::strcmp(app.data().activities[0].english.data(),"Diet")==0);
    }
    // After save/load, today's frozen names follow a French rename (tick stays).
    {
        Display d;App app(d);app.start();
        app.key('1');app.key(Enter);assert(app.data().today.flags&1);
        app.key(Escape);
        App loaded(d);loaded.start();
        assert(loaded.data().today.namedCount==1);
        assert(std::strcmp(loaded.data().today.names[0].data(),"Marche")==0);
        choose(loaded,Language::French);
        loaded.key('1');loaded.key('e');clearDraft(loaded,10);for(char c:std::string("Promenade"))loaded.key(c);loaded.key(Enter);
        assert(std::strcmp(loaded.data().activities[0].french.data(),"Promenade")==0);
        assert(std::strcmp(loaded.data().activities[0].english.data(),"Walk")==0);
        assert(loaded.data().today.flags&1);
        assert(d.has(5,"1  Promenade")&&d.has(5,"[x]"));
        assert(std::strcmp(loaded.data().today.names[0].data(),"Promenade")==0);
    }
    // History keeps frozen names after a rename and a delete; archive extras; ICS helper; read-only.
    {
        Display d;App app(d);app.start();
        app.key('0');for(int i=0;i<10;++i)app.key(Backspace);for(char c:std::string("2026-11-09"))app.key(c);app.key(Enter);
        app.key('1');app.key(Enter); // Walk / Marche
        app.key(Right);app.key(Down);app.key(Down);app.key(Down);app.key(Down);app.key(Down);app.key(Down);app.key(Enter); // Sport (page 2, key 7)
        assert(app.data().today.flags&((1u<<0)|(1u<<16)));
        app.key(Escape);app.key('9');app.key(Enter);app.key(Enter);
        assert(app.data().today.flags==0&&app.data().count==1);
        assert(app.data().history[0].namedCount==2);
        assert(std::strcmp(app.data().history[0].names[0].data(),"Marche")==0);
        assert(std::strcmp(app.data().history[0].names[1].data(),"Sport")==0);
        app.key(Left);assert(d.has(4,"2/22 activities"));
        app.key('1');assert(d.has(5,"1  Walk")&&d.has(5,"[x]"));
        app.key(Escape);app.key(Right); // Back to today before editing the list.
        // Rename Marche -> Promenade in French; delete Sport.
        choose(app,Language::French);
        app.key('1');app.key('e');clearDraft(app,10);for(char c:std::string("Promenade"))app.key(c);app.key(Enter);
        assert(std::strcmp(app.data().activities[0].french.data(),"Promenade")==0);
        app.key(Escape);app.key('1');app.key(Right);
        for(int i=0;i<6;++i)app.key(Down); // Sport is row 7 of page 2
        app.key(DeleteKey);app.key(Enter);
        assert(app.data().activityCount==21);
        app.key(Escape);app.key(Left); // archived closed day
        assert(d.has(4,"2/21 activités"));
        app.key('1');
        assert(d.has("Promenade")&&d.has("[ ]")); // renamed current item, not ticked on that past day
        app.key(Right);app.key(Right);
        // Orphan names listed after the catalog (21 items: page 3 starts at 20).
        assert(d.has("Marche")&&d.has("[x]"));
        assert(d.has("Sport")&&d.has("[x]"));
        const auto before=app.data().history[0].namedCount;
        app.key('e');app.key('n');app.key(DeleteKey);app.key(F4);app.key(F5);app.key(Enter);
        assert(app.data().history[0].namedCount==before); // read-only
        assert(d.has("lecture seule")||d.has("read-only")||d.has("lecture seule"));
        // ICS uses the current-language name when the activity still exists, else the frozen name.
        auto en=exported(app.data(),app.data().history[0],Language::English);
        auto fr=exported(app.data(),app.data().history[0],Language::French);
        assert(en.size()==2&&fr.size()==2);
        assert(en[0]=="Marche"&&fr[0]=="Marche"); // renamed: stored frozen name, not Promenade
        assert(en[1]=="Sport"&&fr[1]=="Sport");   // deleted: stored name
        Day live;live.flags=1;live.namedCount=0;
        auto liveEn=exported(app.data(),live,Language::English);
        assert(liveEn.size()==1&&liveEn[0]=="Walk"); // still exists: English name unchanged
    }
    // Limits, empty list, page clamp, v1 JSON and BIN migration.
    {
        Display d;App app(d);app.start();app.key('1');
        for(unsigned i=0;i<8;++i){
            app.key('n');char name[8];std::snprintf(name,sizeof name,"Extra%u",i);
            for(const char* p=name;*p;++p)app.key(*p);app.key(Enter);
        }
        assert(app.data().activityCount==30);
        app.key('n');assert(d.has("Limit: 30 activities"));assert(app.currentScreen()==Screen::Activities);
        // Delete until three pages collapse: 30 items, page 3 has 10. Delete page 3 entirely.
        assert(d.has(3,"3/3"));
        for(int i=0;i<10;++i){app.key(DeleteKey);app.key(Enter);}
        assert(app.data().activityCount==20&&d.has(3,"2/2")); // left page 3, now two pages
        while(app.data().activityCount){app.key(DeleteKey);app.key(Enter);}
        assert(app.data().activityCount==0&&d.has("N: add an activity"));
        app.key(Escape);assert(d.has(4,"0/0 activities"));
    }
    {
        State s;s.today.flags=1| (1u<<9);
        auto store=lps_test::encodeJson(s);
        std::string json=store.text();
        const auto catalog=json.find("\"activities\":[\n{");assert(catalog!=std::string::npos);
        const auto today=json.find("],\n\"today\"");assert(today!=std::string::npos&&today>catalog);
        json.erase(catalog,today+2-catalog); // drop the catalog, leave "today"
        const auto at=json.find("\"version\":2");assert(at!=std::string::npos);json.replace(at,11,"\"version\":1");
        store.assign(json);
        State back;assert(store.check(back)==SaveCheck::Valid);
        assert(back.activityCount==22&&back.today.flags==(1|(1u<<9)));
        assert(std::strcmp(back.activities[0].french.data(),"Marche")==0);
        State old;old.today.flags=1025;old.xp=90;
        Blob legacy=lps_test::encodeLegacyV6(old);State decoded;
        assert(decode(legacy.bytes.data(),legacy.size,decoded));
        assert(decoded.activityCount==22&&decoded.today.flags==1025);
        assert(std::strcmp(decoded.activities[16].english.data(),"Sport")==0);
    }
    std::puts("PASS: user-defined activities: rename/add/delete/move, language fallback, history freeze, ICS names, limits, v1/BIN defaults and archive read-only.");
}
