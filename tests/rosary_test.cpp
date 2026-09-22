#include "../src/lps_companion.cpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace lps;

class RosaryDisplay final:public Platform {
public:
    std::array<std::array<char,41>,20> rows{};
    void clear()override{for(auto& row:rows)row.fill(0);}
    void text(int x,int y,const char* value,bool)override{
        assert(x==0&&y>=0&&y%16==0&&y<320);if(std::strlen(value)>40)std::fprintf(stderr,"Too wide (%zu): %s\n",std::strlen(value),value);assert(std::strlen(value)<=40);
        std::snprintf(rows[y/16].data(),41,"%s",value);
    }
    void alert(int x,int y,const char* value)override{text(x,y,value,true);}
    void present()override{}
    void setPalette(Palette)override{}
    LoadResult load(uint8_t*,std::size_t,std::size_t&)override{return LoadResult::Missing;}
    bool save(const uint8_t*,std::size_t)override{return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(const char* utf8)const{
        char cells[256];displayText(utf8,cells,sizeof cells);
        for(const auto& row:rows)if(std::strstr(row.data(),cells))return true;
        return false;
    }
};

static void finish(App& app,unsigned limit=250){
    for(unsigned i=0;i<limit&&app.currentScreen()==Screen::RosaryPrayer;++i)app.key(Enter);
    assert(app.currentScreen()==Screen::RosaryComplete);
}

int main(){
    RosaryDisplay d;App app(d);app.start();
    app.key('7');assert(app.currentScreen()==Screen::RosaryMode);assert(d.has("One decade"));assert(d.has("30-40 min"));
    app.key(Enter);assert(app.currentScreen()==Screen::RosarySet);assert(d.has("Today: Luminous"));
    app.key(Enter);assert(app.currentScreen()==Screen::RosaryDecade);assert(d.has("The Baptism of Jesus"));
    app.key('1');assert(app.currentScreen()==Screen::RosaryPrayer);assert(d.has("SIGN OF THE CROSS"));
    finish(app);assert(d.has("ROSARY COMPLETED"));app.key(Enter);

    app.key('7');app.key('2');assert(app.currentScreen()==Screen::RosarySet);app.key(Enter);
    assert(app.currentScreen()==Screen::RosaryPrayer);finish(app);

    app.key(Enter);app.key('7');app.key('3');app.key(Enter);
    bool scripture=false,meditation=false;
    for(unsigned i=0;i<250&&app.currentScreen()==Screen::RosaryPrayer;++i){scripture=scripture||d.has("SCRIPTURE");meditation=meditation||d.has("MEDITATION");app.key(Enter);}
    assert(scripture&&meditation&&app.currentScreen()==Screen::RosaryComplete);

    app.key(Enter);app.key('7');app.key('4');app.key(Enter);
    bool silence=false;
    for(unsigned i=0;i<250&&app.currentScreen()==Screen::RosaryPrayer;++i){silence=silence||d.has("CONTEMPLATIVE SILENCE");app.key(Enter);}
    assert(silence&&app.currentScreen()==Screen::RosaryComplete);
    std::puts("PASS: four Rosary modes, Thursday suggestion, decade choice, guided content, pagination and completion.");
}
