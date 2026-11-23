#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
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
    bool has(int row,const char* value)const{char cells[100];displayText(value,cells,sizeof cells);return std::strstr(rows[row].data(),cells)!=nullptr;}
    bool has(const char* value)const{char cells[100];displayText(value,cells,sizeof cells);for(const auto& r:rows)if(std::strstr(r.data(),cells))return true;return false;}
    bool mark(int row,char glyph)const{const char* r=rows[row].data();return std::strlen(r)>=40&&r[39]==glyph;}
};
static void expectFull(Date d,Language language,const char* want){
    char t[64];fullDateText(d,language,t,sizeof t);
    if(std::strcmp(t,want)!=0){std::fprintf(stderr,"fullDateText got \"%s\" want \"%s\"\n",t,want);assert(0);}
    assert(displayWidth(t)<=40);
}
static std::vector<std::string> linesOf(const State& s,const Day& day,bool archived){
    ReviewItem items[ReviewCapacity];
    const unsigned n=buildDailyReview(items,ReviewCapacity,s,day,archived);
    std::vector<std::string> out(n);
    for(unsigned i=0;i<n;++i){char b[128];formatDailyReviewLine(s,day,archived,items[i],b,sizeof b);out[i]=b;}
    return out;
}
static void expectLine(const std::vector<std::string>& lines,unsigned i,const char* want){
    if(i>=lines.size()||lines[i]!=want){
        std::fprintf(stderr,"review line %u got \"%s\" want \"%s\" (n=%zu)\n",i,
            i<lines.size()?lines[i].c_str():"<missing>",want,lines.size());
        assert(0);
    }
}
static void expectAbsent(const std::vector<std::string>& lines,const char* needle){
    for(const auto& l:lines)if(l.find(needle)!=std::string::npos){
        std::fprintf(stderr,"unexpected \"%s\" in \"%s\"\n",needle,l.c_str());assert(0);
    }
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

    // Minimal today: no weight, no focus, no meals played, no activities.
    {
        State s;s.today.date={2026,9,17};s.today.actual=-1;
        const auto lines=linesOf(s,s.today,false);
        assert(lines.size()==17);
        expectLine(lines,0,"Thursday 17 September 2026 - Week 38");
        expectLine(lines,1,"");
        expectLine(lines,2,"DAILY REVIEW");
        expectLine(lines,3,"Actual weight: -- Kg");
        expectLine(lines,4,"");
        expectLine(lines,5,"KANBAN");
        expectLine(lines,6,"No focused card");
        expectLine(lines,7,""); // CHG07c
        expectLine(lines,8,"MEAL PLANNER");
        expectLine(lines,9,"Drinks: 0/6");
        expectLine(lines,10,"Food: 0/8");
        expectLine(lines,11,"Blank: 0/2");
        expectLine(lines,12,"Next:");
        expectLine(lines,13,"Light Cheese Wrap (Coffee (Latte))");
        expectLine(lines,14,"");
        expectLine(lines,15,"ACTIVITIES");
        expectLine(lines,16,"No activity");
        expectAbsent(lines,"TODO");expectAbsent(lines,"DOING");expectAbsent(lines,"kg");
        s.language=Language::French;
        const auto fr=linesOf(s,s.today,false);
        assert(fr.size()==17);
        expectLine(fr,0,"Jeudi 17 septembre 2026 - Semaine 38");
        expectLine(fr,2,"REVUE JOURNALIÈRE");
        expectLine(fr,3,"Poids effectif : -- Kg");
        expectLine(fr,6,"Aucune carte en focus");
        expectLine(fr,8,"PLANIFICATEUR DE REPAS");
        expectLine(fr,9,"Boissons : 0/6");
        expectLine(fr,10,"Nourriture : 0/8");
        expectLine(fr,11,"Blank : 0/2");
        expectLine(fr,12,"Prochain :");
        expectLine(fr,13,"Wrap fromage léger (Café (Latte))");
        expectLine(fr,15,"ACTIVITÉS");
        expectLine(fr,16,"Aucune activité");
        assert(displayWidth(fr[13].c_str())<=40);
        assert(displayWidth("Wrap fromage léger (Thé à la menthe)")<=40);
    }

    // Typical today: weight, two focused cards, meals partly played, three activities.
    {
        State s;s.today.date={2026,9,17};s.today.actual=81200;
        s.today.flags=(1u<<0)|(1u<<2)|(1u<<16); // Walk, Bible, Sport
        s.taskCount=4;
        s.tasks[0].status=TaskStatus::Todo;std::snprintf(s.tasks[0].name.data(),33,"Write docs");s.tasks[0].focus=true;
        s.tasks[1].status=TaskStatus::Doing;std::snprintf(s.tasks[1].name.data(),33,"Fix bike");s.tasks[1].focus=true;
        s.tasks[2].status=TaskStatus::Todo;std::snprintf(s.tasks[2].name.data(),33,"Unfocused");s.tasks[2].focus=false;
        s.tasks[3].status=TaskStatus::Done;std::snprintf(s.tasks[3].name.data(),33,"Done focused");s.tasks[3].focus=true; // dropped on list
        toggleMealCard(s.meals,0);toggleMealCard(s.meals,1);toggleMealCard(s.meals,2); // first 3 drinks
        toggleMealCard(s.meals,7);toggleMealCard(s.meals,8);toggleMealCard(s.meals,9);toggleMealCard(s.meals,10); // 4 foods, first wrap remains
        const auto lines=linesOf(s,s.today,false);
        assert(lines.size()==20);
        expectLine(lines,3,"Actual weight: 81.200 Kg");
        expectLine(lines,5,"KANBAN");
        expectLine(lines,6,"  Write docs");
        expectLine(lines,7,"  Fix bike");
        expectLine(lines,8,""); // CHG07c after last card
        expectLine(lines,9,"MEAL PLANNER");
        expectLine(lines,10,"Drinks: 3/6");
        expectLine(lines,11,"Food: 4/8");
        expectLine(lines,12,"Blank: 0/2");
        expectLine(lines,13,"Next:");
        expectLine(lines,14,"Light Cheese Wrap (Tea)");
        expectLine(lines,16,"ACTIVITIES");
        expectLine(lines,17,"  Walk");
        expectLine(lines,18,"  Bible");
        expectLine(lines,19,"  Sport");
        expectAbsent(lines,"Unfocused");expectAbsent(lines,"Done focused");
        expectAbsent(lines,"TODO");expectAbsent(lines,"DOING");
        // Exhaust drinks, then food.
        toggleMealCard(s.meals,3);toggleMealCard(s.meals,4);toggleMealCard(s.meals,5);
        expectLine(linesOf(s,s.today,false),14,"Light Cheese Wrap (--)");
        toggleMealCard(s.meals,6);toggleMealCard(s.meals,11);toggleMealCard(s.meals,12);toggleMealCard(s.meals,13);
        const auto done=linesOf(s,s.today,false);
        expectLine(done,10,"Drinks: 6/6");expectLine(done,11,"Food: 8/8");expectLine(done,14,"-- (--)");
        s.language=Language::French;
        const auto fr=linesOf(s,s.today,false);
        expectLine(fr,3,"Poids effectif : 81.200 Kg");
        expectLine(fr,6,"  Write docs");
        expectLine(fr,9,"PLANIFICATEUR DE REPAS");
        expectLine(fr,10,"Boissons : 6/6");
        expectLine(fr,11,"Nourriture : 8/8");
        expectLine(fr,12,"Blank : 0/2");
        expectLine(fr,13,"Prochain :");
        expectLine(fr,14,"-- (--)");
        expectLine(fr,17,"  Marche");
        expectLine(fr,18,"  Bible");
        expectLine(fr,19,"  Sport");
    }

    // Archived day: no KANBAN, meal lines "--", stored activity names, viewed weight.
    // Not in the archive menu yet (CHG06); the list is tested through the builders.
    {
        State s;s.language=Language::French;s.today.date={2026,9,18};
        Day archived{};archived.date={2026,9,17};archived.actual=81200;archived.namedCount=3;
        std::snprintf(archived.names[0].data(),archived.names[0].size(),"%s",storedName(s.activities[0])); // Marche
        std::snprintf(archived.names[1].data(),archived.names[1].size(),"%s",storedName(s.activities[2])); // Bible
        std::snprintf(archived.names[2].data(),archived.names[2].size(),"Ancienne activité");
        s.taskCount=2;s.tasks[0].focus=true;std::snprintf(s.tasks[0].name.data(),33,"Write docs");
        toggleMealCard(s.meals,0);
        const auto lines=linesOf(s,archived,true);
        expectLine(lines,0,"Jeudi 17 septembre 2026 - Semaine 38");
        expectLine(lines,1,"JOUR ARCHIVÉ - LECTURE SEULE");
        expectLine(lines,2,"");
        expectLine(lines,3,"REVUE JOURNALIÈRE");
        expectLine(lines,4,"Poids effectif : 81.200 Kg");
        expectLine(lines,5,"");
        expectLine(lines,6,"PLANIFICATEUR DE REPAS");
        expectLine(lines,7,"Boissons : --");
        expectLine(lines,8,"Nourriture : --");
        expectLine(lines,9,"Blank : --");
        expectLine(lines,10,"Prochain :");
        expectLine(lines,11,"--");
        expectLine(lines,12,"");
        expectLine(lines,13,"ACTIVITÉS");
        expectLine(lines,14,"  Marche");
        expectLine(lines,15,"  Bible");
        expectLine(lines,16,"  Ancienne activité");
        expectAbsent(lines,"KANBAN");expectAbsent(lines,"Write docs");expectAbsent(lines,"0/6");
        assert(lines.size()==17);
        // Empty archived day.
        Day empty=archived;empty.namedCount=0;empty.actual=-1;
        const auto bare=linesOf(s,empty,true);
        expectLine(bare,4,"Poids effectif : -- Kg");
        expectLine(bare,14,"Aucune activité");
        expectAbsent(bare,"KANBAN");
        assert(bare.size()==15);
    }

    // Footer wording fits in 40 cells, including the longest plausible range.
    {
        char b[128];
        formatReviewFooter(Language::English,0,19,b,sizeof b);
        assert(!std::strcmp(b,"Up/Down: scroll  1-16/19  Esc back"));
        assert(displayWidth(b)==34);
        formatReviewFooter(Language::English,3,19,b,sizeof b);
        assert(!std::strcmp(b,"Up/Down: scroll  4-19/19  Esc back"));
        formatReviewFooter(Language::English,0,15,b,sizeof b);
        assert(!std::strcmp(b,"1-15/15  Esc back"));
        formatReviewFooter(Language::English,0,0,b,sizeof b);
        assert(!std::strcmp(b,"0-0/0  Esc back"));
        formatReviewFooter(Language::French,0,17,b,sizeof b);
        assert(!std::strcmp(b,"Haut/Bas : défiler 1-16/17 Échap retour"));
        assert(displayWidth(b)==39);
        formatReviewFooter(Language::French,64,80,b,sizeof b);
        assert(!std::strcmp(b,"Haut/Bas : défiler 65-80/80 Échap retour"));
        assert(displayWidth(b)==40);
        formatReviewFooter(Language::French,0,15,b,sizeof b);
        assert(!std::strcmp(b,"1-15/15  Échap retour"));
        for(unsigned total=0;total<=80;++total){
            const unsigned max=total>ReviewPage?total-ReviewPage:0;
            for(unsigned off:{0u,max/2,max}){
                formatReviewFooter(Language::English,off,total,b,sizeof b);assert(displayWidth(b)<=40);
                formatReviewFooter(Language::French,off,total,b,sizeof b);assert(displayWidth(b)<=40);
            }
        }
    }

    // On-screen: 16-line window, range footer on row 19, arrows, Home/End/Pg, overlays.
    {
        ReviewDisplay d;State s;s.today.date={2026,9,23};s.today.actual=117700;s.today.flags=(1u<<0)|(1u<<2);
        s.taskCount=3;s.tasks[0].status=TaskStatus::Todo;std::snprintf(s.tasks[0].name.data(),33,"Buy supplies");s.tasks[0].focus=true;
        s.tasks[1].status=TaskStatus::Doing;std::snprintf(s.tasks[1].name.data(),33,"Write notes");s.tasks[1].focus=true;
        s.tasks[2].status=TaskStatus::Done;std::snprintf(s.tasks[2].name.data(),33,"Hidden done task");s.tasks[2].focus=true;
        d.stored=lps_test::encodeJson(s);App a(d);a.start();
        assert(d.has("Wednesday 23 September 2026 (week 39)"));
        a.key('5');assert(a.currentScreen()==Screen::DailyReview);
        assert(a.reviewLineCount()==19&&a.reviewScroll()==0);
        assert(d.has(0,"v1.15    2026-09-23"));
        assert(d.has(1,"Wednesday 23 September 2026 - Week 39"));
        assert(!d.has("(week 39)"));
        assert(d.has("DAILY REVIEW")&&d.has("Actual weight: 117.700 Kg"));
        assert(d.has("KANBAN")&&d.has("  Buy supplies")&&d.has("  Write notes"));
        assert(d.has("MEAL PLANNER")&&d.has("Next:")&&d.has("Light Cheese Wrap (Coffee (Latte))"));
        assert(!d.has("ACTIVITIES")&&!d.has("  Walk")&&!d.has("Hidden done task")&&!d.has("TODO"));
        assert(!d.has("Enter:OK")); // range footer replaces the generic help
        assert(d.has(19,"Up/Down: scroll  1-16/19  Esc back"));
        assert(!d.mark(1,'\x18')&&d.mark(16,'\x19'));
        a.key(Down);assert(a.reviewScroll()==1);
        assert(d.has("ACTIVITIES")&&!d.has("  Walk"));
        assert(d.has(19,"Up/Down: scroll  2-17/19  Esc back"));
        assert(d.mark(1,'\x18')&&d.mark(16,'\x19')); // both arrows in the middle
        a.key(Tab);assert(a.currentScreen()==Screen::Config);
        a.key(Tab);assert(a.currentScreen()==Screen::DailyReview&&a.reviewScroll()==1);
        assert(d.has(19,"Up/Down: scroll  2-17/19  Esc back")&&d.mark(1,'\x18')&&d.mark(16,'\x19'));
        a.key(' ');assert(a.currentScreen()==Screen::MealPlanner);
        a.key(Escape);assert(a.currentScreen()==Screen::DailyReview&&a.reviewScroll()==1);
        a.key(End);assert(a.reviewScroll()==3);
        assert(d.has("  Walk")&&d.has("  Bible")&&d.has("ACTIVITIES"));
        assert(d.has(19,"Up/Down: scroll  4-19/19  Esc back"));
        assert(d.mark(1,'\x18')&&!d.mark(16,'\x19'));
        a.key(Down);assert(a.reviewScroll()==3); // no wrap
        a.key(Home);assert(a.reviewScroll()==0&&d.has(19,"Up/Down: scroll  1-16/19  Esc back"));
        a.key(Up);assert(a.reviewScroll()==0);
        a.key(PageDown);assert(a.reviewScroll()==3);
        a.key(PageUp);assert(a.reviewScroll()==0);
        a.key(Right);assert(a.reviewScroll()==3);
        a.key(Left);assert(a.reviewScroll()==0);
        a.key(Escape);assert(a.currentScreen()==Screen::Home);
        assert(d.has("Wednesday 23 September 2026 (week 39)"));
        assert(!d.has("- Week 39"));
        assert(d.has(19,"Up/Down  Enter:OK  Tab:Config")); // generic help back on Home
        // Home/End/PgUp/PgDn are scoped to the review: they do not move the menu or open Config.
        const unsigned sel=0;
        a.key(Home);a.key(End);a.key(PageUp);a.key(PageDown);
        assert(a.currentScreen()==Screen::Home&&d.has("Wednesday 23 September 2026 (week 39)"));
        (void)sel;
    }
    {
        ReviewDisplay d;State s;s.language=Language::French;s.today.date={2026,9,23};s.today.actual=-1;
        s.taskCount=16;
        for(unsigned i=0;i<16;++i){s.tasks[i].status=TaskStatus::Todo;s.tasks[i].focus=true;std::snprintf(s.tasks[i].name.data(),33,"Tache %02u",i);}
        d.stored=lps_test::encodeJson(s);App fr(d);fr.start();
        assert(d.has("Mercredi 23 Septembre 2026 (semaine 39)"));
        fr.key('5');
        assert(d.has("REVUE JOURNALIÈRE")&&d.has("Poids effectif : -- Kg"));
        assert(d.has("Mercredi 23 septembre 2026 - Semaine 39"));
        assert(!d.has("(semaine 39)"));
        assert(d.has("  Tache 00")&&!d.has("PLANIFICATEUR DE REPAS"));
        assert(fr.reviewLineCount()==32);
        assert(d.has(19,"Haut/Bas : défiler 1-16/32 Échap retour"));
        assert(!d.mark(1,'\x18')&&d.mark(16,'\x19'));
        fr.key(End);assert(fr.reviewScroll()==16);
        assert(d.has("Aucune activité")&&d.has("Prochain :")&&d.has("Wrap fromage léger (Café (Latte))"));
        assert(d.has("PLANIFICATEUR DE REPAS")&&d.has("  Tache 15"));
        assert(d.has(19,"Haut/Bas : défiler 17-32/32 Échap retour"));
        assert(d.mark(1,'\x18')&&!d.mark(16,'\x19'));
        assert(!d.has("FINI")&&!d.has("TODO")&&!d.has("Aucune sélectionnée")&&!d.has("Enter:OK"));
        fr.key(PageUp);assert(fr.reviewScroll()==0);
        fr.key(PageDown);assert(fr.reviewScroll()==16);
        fr.key(Home);assert(fr.reviewScroll()==0);
        fr.key(Escape);assert(fr.currentScreen()==Screen::Home);
        assert(d.has("Mercredi 23 Septembre 2026 (semaine 39)"));
        assert(!d.has("- Semaine 39"));
    }
    // Short footer when the list fits in 16 lines (archived empty day = 15).
    {
        char b[128];formatReviewFooter(Language::French,0,15,b,sizeof b);
        assert(!std::strcmp(b,"1-15/15  Échap retour")&&displayWidth(b)<=40);
        formatReviewFooter(Language::English,0,16,b,sizeof b);
        assert(!std::strcmp(b,"1-16/16  Esc back"));
    }
    std::printf("PASS: daily review layout, 16-line scroll, markers, range footer, Home/End/Pg, overlays, French. sizeof(State)=%zu\n",sizeof(State));
}
