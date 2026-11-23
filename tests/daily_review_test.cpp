#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace lps;
class ReviewDisplay final:public Platform {
public:
    lps_test::JsonStore stored{};
    std::array<std::array<char,100>,20> rows{};
    void clear()override{for(auto& row:rows)row.fill(0);}
    void text(int,int y,const char* value,bool)override{assert(y>=0&&y%16==0&&y<320);std::snprintf(rows[y/16].data(),100,"%s",value);}
    void alert(int,int,const char*)override{}
    void present()override{}
    void setPalette(Palette)override{}
    LoadResult load(State& state)override{return stored.read(state);}
    bool save(const State&)override{return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(const char* value)const{char cells[100];displayText(value,cells,sizeof cells);for(const auto& r:rows)if(std::strstr(r.data(),cells))return true;return false;}
};
static void expectFull(Date d,Language language,const char* want){
    char t[64];fullDateText(d,language,t,sizeof t);
    if(std::strcmp(t,want)!=0){std::fprintf(stderr,"fullDateText got \"%s\" want \"%s\"\n",t,want);assert(0);}
    assert(displayWidth(t)<=40);
}
int main(){
    // LPSC116-FEAT02-CHG03: wording matches DOS full_date_text, Home keeps longDateText.
    expectFull({2026,11,16},Language::English,"Monday 16 November 2026 - Week 47");
    expectFull({2026,11,16},Language::French,"Lundi 16 novembre 2026 - Semaine 47");
    expectFull({2026,2,14},Language::French,"Samedi 14 février 2026 - Semaine 7");
    expectFull({2026,8,15},Language::French,"Samedi 15 août 2026 - Semaine 33");
    assert(isoWeek({2026,12,31})==53);
    expectFull({2026,12,31},Language::English,"Thursday 31 December 2026 - Week 53");
    expectFull({2026,12,31},Language::French,"Jeudi 31 décembre 2026 - Semaine 53");
    assert(isoWeek({2027,1,1})==53);
    expectFull({2027,1,1},Language::English,"Friday 1 January 2027 - Week 53");
    expectFull({2027,1,1},Language::French,"Vendredi 1 janvier 2027 - Semaine 53");
    assert(isoWeek({2020,12,31})==53);
    expectFull({2020,12,31},Language::English,"Thursday 31 December 2020 - Week 53");
    assert(isoWeek({2021,1,3})==53);
    expectFull({2021,1,3},Language::English,"Sunday 3 January 2021 - Week 53");
    expectFull({2021,1,3},Language::French,"Dimanche 3 janvier 2021 - Semaine 53");
    assert(isoWeek({2024,12,30})==1);
    expectFull({2024,12,30},Language::English,"Monday 30 December 2024 - Week 1");
    expectFull({2024,12,30},Language::French,"Lundi 30 décembre 2024 - Semaine 1");
    assert(isoWeek({2026,9,30})==40);
    expectFull({2026,9,30},Language::French,"Mercredi 30 septembre 2026 - Semaine 40");
    expectFull({2026,9,30},Language::English,"Wednesday 30 September 2026 - Week 40");
    {char t[64];fullDateText({2026,9,30},Language::French,t,sizeof t);assert(displayWidth(t)==39);}
    // Every valid day in 2000-2099 fits in 40 columns, both languages.
    for(unsigned y=2000;y<=2099;++y)for(uint8_t m=1;m<=12;++m){
        const uint8_t last=uint8_t(daysInMonth(uint16_t(y),m));
        for(uint8_t day=1;day<=last;++day){
            Date d{uint16_t(y),m,day};char en[64],fr[64];
            fullDateText(d,Language::English,en,sizeof en);
            fullDateText(d,Language::French,fr,sizeof fr);
            assert(displayWidth(en)<=40&&displayWidth(fr)<=40);
            const unsigned week=isoWeek(d);assert(week>=1&&week<=53);
        }
    }
    ReviewDisplay d;State s;s.today.date={2026,9,23};s.today.actual=117700;s.today.flags=(1u<<0)|(1u<<2);
    s.taskCount=3;s.tasks[0].status=TaskStatus::Todo;std::snprintf(s.tasks[0].name.data(),33,"Buy supplies");
    s.tasks[1].status=TaskStatus::Doing;std::snprintf(s.tasks[1].name.data(),33,"Write notes");
    s.tasks[2].status=TaskStatus::Done;std::snprintf(s.tasks[2].name.data(),33,"Hidden done task");
    d.stored=lps_test::encodeJson(s);App a(d);a.start();
    assert(d.has("Wednesday 23 September 2026 (week 39)"));
    a.key('5');assert(a.currentScreen()==Screen::DailyReview);
    assert(d.has("Wednesday 23 September 2026 - Week 39"));
    assert(!d.has("(week 39)"));
    assert(d.has("2026-09-23")&&d.has("117.700 kg")&&d.has("Walk")&&d.has("Bible"));
    assert(d.has("TODO (1)")&&d.has("DOING (1)"));assert(!d.has("Hidden done task"));
    a.key(Escape);assert(a.currentScreen()==Screen::Home);
    assert(d.has("Wednesday 23 September 2026 (week 39)"));
    assert(!d.has("- Week 39"));
    s.language=Language::French;s.today.flags=0;s.today.actual=-1;
    s.taskCount=24;for(unsigned i=0;i<24;++i){s.tasks[i].status=i%2?TaskStatus::Doing:TaskStatus::Todo;std::snprintf(s.tasks[i].name.data(),33,"Tache %02u",i);}
    d.stored=lps_test::encodeJson(s);App fr(d);fr.start();
    assert(d.has("Mercredi 23 Septembre 2026 (semaine 39)"));
    fr.key('5');assert(d.has("REVUE JOURNALIÈRE")&&d.has("Poids effectif : -- kg")&&d.has("Aucune sélectionnée"));
    assert(d.has("Mercredi 23 septembre 2026 - Semaine 39"));
    assert(!d.has("(semaine 39)"));
    for(unsigned i=0;i<40;++i)fr.key(Down);
    assert(d.has("Tache 23")&&!d.has("FINI"));
    for(unsigned i=0;i<12;++i)fr.key(Up);
    assert(d.has("EN COURS (12)"));
    for(unsigned i=0;i<12;++i)fr.key(Down);
    fr.key(Up);assert(!d.has("Tache 23"));fr.key(Escape);assert(fr.currentScreen()==Screen::Home);
    assert(d.has("Mercredi 23 Septembre 2026 (semaine 39)"));
    assert(!d.has("- Semaine 39"));
    std::puts("PASS: daily review date, activities, weight, TODO/DOING, empty states, French, scrolling and full date with ISO week.");
}
