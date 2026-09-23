#include "../src/lps_companion.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace lps;
class Display final:public Platform{
public:
 Blob stored{};bool exists=false,fail=false;unsigned writes=0;std::array<std::array<char,100>,20> rows{};
 void clear()override{for(auto& r:rows)r.fill(0);}
 void text(int,int y,const char* s,bool)override{assert(y>=0&&y<320&&y%16==0);std::snprintf(rows[y/16].data(),100,"%s",s);}
 void alert(int,int,const char*)override{}void present()override{}void setPalette(Palette)override{}
 LoadResult load(uint8_t* b,std::size_t cap,std::size_t& n)override{if(!exists)return LoadResult::Missing;assert(stored.size<=cap);n=stored.size;std::memcpy(b,stored.bytes.data(),n);return LoadResult::Ok;}
 bool save(const uint8_t* b,std::size_t n)override{if(fail)return false;assert(n<=stored.bytes.size());std::memcpy(stored.bytes.data(),b,n);stored.size=n;exists=true;++writes;return true;}
 bool exportIcs(const Day&,uint32_t,uint32_t,Language)override{return true;}bool exportWeightCsv(const Day&)override{return true;}
 bool has(const char* s)const{char cell[100];displayText(s,cell,sizeof cell);for(const auto& r:rows)if(std::strstr(r.data(),cell))return true;return false;}
};
static void create(App& a,const char* message){a.key('8');a.key('n');a.key(Down);a.key(Down);for(const char* c=message;*c;++c)a.key(*c);a.key(Enter);assert(a.currentScreen()==Screen::Reminders);a.key(Escape);}
int main(){
 Display d;State s;s.today.date={2028,2,29};d.stored=encode(s);d.exists=true;
 App a(d);a.start();assert(a.currentScreen()==Screen::Home);create(a,"Pay rent");
 assert(a.data().reminderCount==1);App reboot(d);reboot.start();assert(reboot.currentScreen()==Screen::ReminderPopup&&d.has("Pay rent"));
 reboot.key('x');assert(reboot.currentScreen()==Screen::Home);App again(d);again.start();assert(again.currentScreen()==Screen::ReminderPopup);
 again.key(Escape);create(again,"Call doctor");create(again,"Order parts");
 again.key('8');again.key('n');assert(again.currentScreen()==Screen::Reminders&&d.has("Limit: 3"));again.key(Escape);
 // Edits are persisted, and the last active day is inclusive across leap day.
 State future=again.data();future.reminders[0].duration=2;future.reminders[1].duration=4;future.reminders[2].duration=4;future.today.date={2028,3,1};d.stored=encode(future);
 App last(d);last.start();assert(d.has("Pay rent"));last.key(Escape);
 future.today.date={2028,3,2};d.stored=encode(future);unsigned before=d.writes;
 App expired(d);expired.start();assert(d.writes==before+1&&expired.data().reminderCount==2&&!d.has("Pay rent"));
 expired.key(Escape);expired.key('8');expired.key('n');assert(expired.currentScreen()==Screen::ReminderEdit);
 expired.key(Escape);expired.key(Escape);
 App persisted(d);persisted.start();assert(persisted.data().reminderCount==2);
 // If persistence fails, do not silently delete an expired reminder from RAM.
 State stale=persisted.data();stale.reminders[0].from={2028,1,1};d.stored=encode(stale);d.fail=true;
 App failed(d);failed.start();assert(failed.data().reminderCount==2&&d.has("Save failed"));
 // Decode the previous v5 layout with no reminders.
 Blob legacy=d.stored;legacy.size=WireV5;legacy.bytes[4]=5;auto crc=crc32(legacy.bytes.data(),legacy.size-4);
 for(unsigned i=0;i<4;++i)legacy.bytes[legacy.size-4+i]=uint8_t(crc>>(i*8));
 State migrated;assert(decode(legacy.bytes.data(),legacy.size,migrated)&&migrated.reminderCount==0);
 std::puts("PASS: three reminders, repeat startup popup, leap-day expiry, durable deletion, failed save, v5 migration.");
}
