#include "../src/lps_companion.cpp"
#include <cassert>
#include <string>
struct TimerDisplay: lps::Platform {
 std::string value;bool red=false;unsigned writes=0;
 void clear()override{} void text(int,int,const char*,bool)override{} void alert(int,int,const char*)override{}
 void present()override{} void setPalette(lps::Palette)override{}
 void largeText(const char* v,bool r)override{value=v;red=r;}
 lps::LoadResult load(uint8_t*,size_t,size_t&)override{return lps::LoadResult::Missing;}
 bool save(const uint8_t*,size_t)override{++writes;return true;}
 bool exportIcs(const lps::Day&,uint32_t,uint32_t,lps::Language)override{return true;}
 bool exportWeightCsv(const lps::Day&)override{return true;}
};
int main(){using namespace lps;
 for(unsigned option=0;option<3;++option){TimerDisplay d;App a(d);a.start();a.tick(1000);a.key('9');
 assert(a.currentScreen()==Screen::TimerSelect);for(unsigned i=0;i<option;++i)a.key(Down);a.key(Enter);
 const unsigned duration[]={300000,900000,1800000};const char* initial[]={"05:00","15:00","30:00"};
 assert(d.value==initial[option]&&!d.red);a.tick(1000+duration[option]-60000);assert(d.value=="01:00"&&!d.red);
 a.tick(1000+duration[option]-59000);assert(d.value=="00:59"&&d.red);a.key(Enter);
 a.tick(1000+duration[option]+10000);assert(d.value=="00:59");a.key(Enter);
 a.tick(1000+duration[option]+69000);assert(d.value=="00:00"&&d.red&&a.currentScreen()==Screen::TimerComplete);
 a.key(Enter);assert(a.currentScreen()==Screen::TimerSelect);a.key(Enter);a.key(Escape);a.tick(9999999);
 assert(a.currentScreen()==Screen::Home&&d.writes==0);a.key(Up);a.key(Enter);assert(a.currentScreen()==Screen::Finish);
 }
 std::puts("PASS: timer durations, 00:59 red threshold, pause/resume, expiry, cancellation, no save writes and close-day navigation.");
}
