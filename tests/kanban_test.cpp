#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <string>
using namespace lps;

class BoardDisplay final:public Platform {
public:
    lps_test::JsonStore stored{};bool exists=false,fail=false;unsigned writes=0;
    std::array<std::array<char,41>,20> rows{};
    void clear()override{for(auto& row:rows)row.fill(0);}
    void text(int x,int y,const char* value,bool)override{assert(x==0&&y%16==0);std::snprintf(rows[y/16].data(),41,"%s",value);}
    void alert(int x,int y,const char* value)override{text(x,y,value,true);}
    void present()override{}
    void setPalette(Palette)override{}
    LoadResult load(State& state)override{
        if(!exists)return LoadResult::Missing;
        return stored.read(state);
    }
    bool save(const State& state)override{
        if(fail||!stored.write(state))return false;
        ++writes;exists=true;return true;
    }
    bool exportIcs(const Day&,uint32_t,uint32_t,Language)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(int row,const char* value)const{return std::strstr(rows[row].data(),value)!=nullptr;}
};

void add(App& app,const char* name){app.key('N');assert(app.currentScreen()==Screen::TaskEdit);for(char c:std::string(name))app.key(c);app.key(Enter);assert(app.currentScreen()==Screen::Kanban);}

int main(){
    BoardDisplay display;App app(display);app.start();app.key('3');
    assert(app.currentScreen()==Screen::Kanban&&display.has(3,"TODO")&&display.has(6,"No tasks"));
    add(app,"Alpha");add(app,"Beta");add(app,"Gamma");
    assert(app.data().taskCount==3&&std::strcmp(app.data().tasks[2].name.data(),"Gamma")==0);
    app.key(F4);assert(std::strcmp(app.data().tasks[1].name.data(),"Gamma")==0); // Up one.
    app.key(Up);app.key(F4);assert(std::strcmp(app.data().tasks[0].name.data(),"Gamma")==0&&std::strcmp(app.data().tasks[1].name.data(),"Beta")==0&&std::strcmp(app.data().tasks[2].name.data(),"Alpha")==0); // Top wraps to bottom.
    app.key(F5);assert(std::strcmp(app.data().tasks[0].name.data(),"Alpha")==0&&std::strcmp(app.data().tasks[1].name.data(),"Gamma")==0&&std::strcmp(app.data().tasks[2].name.data(),"Beta")==0); // Bottom wraps to top.
    app.key(F2);assert(app.data().tasks[2].status==TaskStatus::Doing&&display.has(3,"DOING"));
    app.key(F3);assert(app.data().tasks[2].status==TaskStatus::Done&&display.has(3,"DONE"));
    app.key(Left);assert(display.has(3,"DOING"));app.key(Left);assert(display.has(3,"TODO"));
    app.key(Left);assert(display.has(3,"DONE"));app.key(Right);assert(display.has(3,"TODO"));
    app.key(Enter);assert(app.currentScreen()==Screen::TaskEdit);
    for(unsigned i=0;i<5;++i){app.key(Backspace);}for(char c:std::string("Delta")){app.key(c);}app.key(Enter);
    assert(std::strcmp(app.data().tasks[0].name.data(),"Delta")==0);
    app.key(DeleteKey);assert(app.data().taskCount==2);
    const auto before=app.data();display.fail=true;app.key('N');app.key('X');app.key(Enter);
    assert(app.currentScreen()==Screen::TaskEdit&&app.data().taskCount==before.taskCount);
    display.fail=false;app.key(Enter);assert(app.currentScreen()==Screen::Kanban&&app.data().taskCount==3);
    app.key(DeleteKey);assert(app.data().taskCount==2);app.key(Escape);
    App reboot(display);reboot.start();assert(reboot.data().taskCount==2);
    assert(reboot.data().tasks[1].status==TaskStatus::Done&&std::strcmp(reboot.data().tasks[1].name.data(),"Alpha")==0);
    State legacy;Blob old=lps_test::encodeLegacyV6(legacy);State decoded;assert(decode(old.bytes.data(),old.size,decoded)&&decoded.taskCount==0);
    std::puts("PASS: Kanban create/edit/delete, circular navigation/reordering, state moves, rollback and persistence.");
}
