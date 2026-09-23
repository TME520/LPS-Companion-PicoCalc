#include "../src/lps_companion.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace lps;
class ReviewDisplay final:public Platform {
public:
    Blob stored{};
    std::array<std::array<char,100>,20> rows{};
    void clear()override{for(auto& row:rows)row.fill(0);}
    void text(int,int y,const char* value,bool)override{assert(y>=0&&y%16==0&&y<320);std::snprintf(rows[y/16].data(),100,"%s",value);}
    void alert(int,int,const char*)override{}
    void present()override{}
    void setPalette(Palette)override{}
    LoadResult load(uint8_t* data,std::size_t capacity,std::size_t& size)override{assert(capacity>=stored.size);std::memcpy(data,stored.bytes.data(),stored.size);size=stored.size;return LoadResult::Ok;}
    bool save(const uint8_t*,std::size_t)override{return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(const char* value)const{char cells[100];displayText(value,cells,sizeof cells);for(const auto& r:rows)if(std::strstr(r.data(),cells))return true;return false;}
};
int main(){
    ReviewDisplay d;State s;s.today.date={2026,9,23};s.today.actual=117700;s.today.flags=(1u<<0)|(1u<<2);
    s.taskCount=3;s.tasks[0].status=TaskStatus::Todo;std::snprintf(s.tasks[0].name.data(),33,"Buy supplies");
    s.tasks[1].status=TaskStatus::Doing;std::snprintf(s.tasks[1].name.data(),33,"Write notes");
    s.tasks[2].status=TaskStatus::Done;std::snprintf(s.tasks[2].name.data(),33,"Hidden done task");
    d.stored=encode(s);App a(d);a.start();a.key('6');assert(a.currentScreen()==Screen::DailyReview);
    assert(d.has("2026-09-23")&&d.has("117.700 kg")&&d.has("Walk")&&d.has("Bible"));
    assert(d.has("TODO (1)")&&d.has("DOING (1)"));assert(!d.has("Hidden done task"));
    a.key(Escape);assert(a.currentScreen()==Screen::Home);
    s.language=Language::French;s.today.flags=0;s.today.actual=-1;
    s.taskCount=24;for(unsigned i=0;i<24;++i){s.tasks[i].status=i%2?TaskStatus::Doing:TaskStatus::Todo;std::snprintf(s.tasks[i].name.data(),33,"Tache %02u",i);}
    d.stored=encode(s);App fr(d);fr.start();fr.key('6');assert(d.has("REVUE JOURNALIÈRE")&&d.has("Poids effectif : -- kg")&&d.has("Aucune sélectionnée"));
    for(unsigned i=0;i<40;++i)fr.key(Down);
    assert(d.has("Tache 23")&&!d.has("FINI"));
    for(unsigned i=0;i<12;++i)fr.key(Up);
    assert(d.has("EN COURS (12)"));
    for(unsigned i=0;i<12;++i)fr.key(Down);
    fr.key(Up);assert(!d.has("Tache 23"));fr.key(Escape);assert(fr.currentScreen()==Screen::Home);
    std::puts("PASS: daily review date, activities, weight, TODO/DOING, empty states, French and scrolling.");
}
