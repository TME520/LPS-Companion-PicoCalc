#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <string>
using namespace lps;
class Display final:public Platform {
public:
    Palette shown=Palette::Green;
    std::array<std::array<bool,40>,20> bold{};
    void setPalette(Palette palette)override{shown=palette;}
    void emphasize(int x,int y,int n)override{for(int i=0;i<n;++i)if(x/8+i<40)bold[y/16][x/8+i]=true;}
    std::array<std::array<char,41>,20> rows{};
    lps_test::JsonStore stored{};bool exists=false,fail=false,failExport=false,failWeightCsv=false;unsigned writes=0,exports=0,weightExports=0;
    void clear()override{for(auto& row:rows)row.fill(0);for(auto& row:bold)row.fill(false);}
    void text(int x,int y,const char* text,bool)override{
        assert(x==0&&y>=0&&y%16==0&&y+16<=320);
        assert(std::strlen(text)<=40); // Fail on untranslated byte widths or clipped hints.
        for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){
            const bool ok=(*p>=32&&*p<=126)||*p==0xe9||*p==0xe8||*p==0xea||*p==0xe0||*p==0xe2||*p==0xee||*p==0xf4||*p==0xf9||*p==0xfb||*p==0xe7||*p==0xc0||*p==0xc8||*p==0xc9;
            if(!ok)std::fprintf(stderr,"Unsupported byte %02x in: %s\n",*p,text);
            assert(ok);
        }
        std::snprintf(rows[y/16].data(),41,"%s",text);
    }
    void alert(int x,int y,const char* value)override{this->text(x,y,value,true);}
    bool anyXp()const{for(const auto& row:rows)if(std::strstr(row.data(),"XP"))return true;return false;}
    bool has(int row,const char* utf8)const{
        char cells[100];displayText(utf8,cells,sizeof cells);
        return std::strstr(rows[row].data(),cells)!=nullptr;
    }
    void present()override{}
    LoadResult load(State& state)override{
        if(!exists)return LoadResult::Missing;
        return stored.read(state);
    }
    bool save(const State& state)override{
        if(fail||!stored.write(state))return false;
        ++writes;exists=true;return true;
    }
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{if(failExport)return false;++exports;return true;}
    bool exportWeightCsv(const Day&)override{if(failWeightCsv)return false;++weightExports;return true;}
};
void choose(App& app,Language language){app.key('6');app.key('1');app.key(language==Language::French?'2':'1');}
void checkMenus(Display& d,App& a,bool fr){
    assert(d.has(6,"0  Date"));
    assert(d.has(7,fr?"1  Activités":"1  Activities"));
    assert(d.has(8,fr?"2  Suivi du poids":"2  Weight tracker"));
    assert(d.has(9,"3  Kanban"));
    assert(d.has(10,fr?"4  Rosaire chrétien":"4  Christian Rosary"));
    assert(d.has(11,fr?"5  Revue journalière":"5  Daily review"));
    assert(d.has(12,"6  Config"));
    assert(d.has(13,fr?"7  Rappels":"7  Reminders"));
    assert(d.has(14,fr?"8  Minuteur":"8  Timer"));
    assert(d.has(15,fr?"9  Terminer le jour":"9  Close the day"));
    // LPS-112-CHG05: the notepad is gone from the menu in both languages.
    for(const auto& row:d.rows){char cells[40];displayText(fr?"Bloc notes":"Notepad",cells,sizeof cells);assert(!std::strstr(row.data(),cells));}
    const char* footer=d.rows[19].data();char desired[24];displayText(fr?"Entrée":"Enter",desired,sizeof desired);const char* key=std::strstr(footer,desired);
    assert(key&&d.bold[19][unsigned(key-footer)]);
    if(fr){char back[24];displayText("Retour",back,sizeof back);const char* plain=std::strstr(footer,back);assert(plain);for(unsigned i=0;i<std::strlen(back);++i)assert(!d.bold[19][unsigned(plain-footer)+i]);}
    assert(d.has(17,fr?"Jour précédent":"Previous day"));
    assert(d.has(0,"v1.14    2026-09-17"));
    // LPS-112-CHG03: no XP anywhere; line 1 shows the long date and ISO week.
    assert(d.has(1,fr?"Jeudi 17 Septembre 2026 (semaine 38)":"Thursday 17 September 2026 (week 38)")&&!d.anyXp());
    a.key('0');assert(d.has(3,"DATE"));assert(d.has(6,"2026-09-17"));
    assert(d.has(9,fr?"Calendrier":"Calendar"));a.key(Escape);
    a.key('1');assert(d.has(3,fr?"ACTIVITÉS":"ACTIVITIES"));assert(d.has(6,fr?"Régime":"Diet"));
    assert(d.has(14,fr?"Congé":"Day off"));a.key(Right);assert(d.has(5,"Weekend"));assert(d.has(6,fr?"Sortie":"Outing"));assert(d.has(7,fr?"Jeu":"Gaming"));assert(d.has(8,fr?"Docteur":"Doctor"));assert(d.has(9,fr?"Lecture":"Reading"));assert(d.has(10,"Shopping"));a.key(Right);assert(d.has(6,fr?"2  Méditation":"2  Meditation"));a.key(Right);a.key(Escape);
    a.key('2');assert(d.has(3,fr?"SUIVI DU POIDS":"WEIGHT TRACKER"));
    assert(d.has(6,fr?"Attendu":"Expected"));assert(d.has(9,fr?"Effectif":"Actual"));
    a.key('0');a.key(Enter);assert(d.has(17,fr?"Poids invalide":"Invalid weight"));a.key(Escape);
    a.key('3');assert(a.currentScreen()==Screen::Kanban);assert(d.has(3,fr?"À FAIRE":"TODO"));a.key(Escape);
    a.key('8');assert(a.currentScreen()==Screen::TimerSelect);a.key(Escape);a.key(Up);a.key(Enter);assert(d.has(3,fr?"TERMINER LE JOUR":"CLOSE THE DAY"));a.key(Escape);
    a.key('9');assert(a.currentScreen()==Screen::Finish);a.key(Escape);
    a.key('4');assert(a.currentScreen()==Screen::RosaryMode);assert(d.has(3,fr?"MODE DU ROSAIRE":"ROSARY MODE"));
    a.key(Enter);a.key(Enter);assert(a.currentScreen()==Screen::RosaryDecade);a.key(Escape);a.key(Escape);a.key(Escape);
    a.key('6');assert(d.has(6,fr?"1  Langue":"1  Language"));assert(d.has(7,fr?"2  Couleurs":"2  Colors"));assert(d.has(8,fr?"3  Enregistrer":"3  Save"));
    a.key('2');assert(d.has(6,fr?"1  PAL1  rouge":"1  PAL1  red"));assert(d.has(7,fr?"2  PAL2  vert":"2  PAL2  green"));assert(d.has(8,fr?"3  PAL3  bleu":"3  PAL3  blue"));
    assert(d.has(9,"4  PAL4  Orange")&&d.has(10,fr?"5  PAL5  Rose":"5  PAL5  Pink"));a.key(Escape);
    a.key('1');assert(d.has(7,"Français"));a.key(Escape);a.key(Escape);
}
int main(){
    { Display activityDisplay;App activityApp(activityDisplay);activityApp.start();activityApp.key('1');activityApp.key(Right);
      for(unsigned i=0;i<5;++i){activityApp.key(Down);}activityApp.key(Enter);
      assert(activityApp.data().today.flags==uint16_t(1u<<15));assert(activityDisplay.has(10,"Shopping")); }
    // LPS-113-CHG08: 22 activities on three pages (10, 10, 2), in both languages.
    for(bool fr:{false,true}){
      Display dd;App app(dd);app.start();
      if(fr)choose(app,Language::French),app.key('3');
      assert(dd.has(4,fr?"0/22 activités":"0/22 activities"));
      app.key('1');assert(dd.has(3,"1/3")&&dd.has(5,fr?"1  Marche":"1  Walk")&&dd.has(14,fr?"0  Congé":"0  Day off"));
      // Same "1  Name" spacing as the home menu; every checkbox sits in the same column.
      {const char* row=dd.rows[5].data();assert(row[0]=='1'&&row[1]==' '&&row[2]==' ');
       const char* box=std::strchr(row,'[');assert(box&&box-row==28);
       for(int r=6;r<=14;++r)assert(dd.rows[r][28]=='[');}
      app.key(Right);assert(dd.has(3,"2/3")&&dd.has(5,"1  Weekend")&&dd.has(6,fr?"2  Sortie":"2  Outing")&&dd.has(11,fr?"7  Sport":"7  Sport"));
      assert(dd.has(12,fr?"8  Cuisine":"8  Cooking")&&dd.has(13,fr?"9  Ménage":"9  Chores")&&dd.has(14,fr?"0  Famille":"0  Family"));
      app.key(Right);assert(dd.has(3,"3/3")&&dd.has(5,fr?"1  Amis":"1  Friends")&&dd.has(6,fr?"2  Méditation":"2  Meditation"));
      for(int row=7;row<=14;++row)assert(dd.rows[row][0]==0); // Short last page: no stale rows.
      assert(dd.has(6,"[ ]"));
      app.key(Down);app.key(Down);app.key(Down);app.key(Enter); // Down wraps within the two rows: Méditation.
      assert(app.data().today.flags==(ActivityFlags(1u)<<21)&&dd.has(6,"[x]")&&dd.has(3,"3/3"));
      app.key(Right);assert(dd.has(3,"1/3"));app.key(Left);app.key(Left);assert(dd.has(3,"2/3"));
      app.key(Left);app.key(Left);assert(dd.has(3,"3/3")); // Left wraps from page 1 to page 3.
      app.key(Enter);assert(app.data().today.flags==((ActivityFlags(1u)<<21)|(ActivityFlags(1u)<<20)));
      app.key(Escape);assert(dd.has(4,fr?"2/22 activités":"2/22 activities"));
      // Reboot from the JSON save keeps the new activities; the review lists them.
      App again(dd);again.start();assert(again.data().today.flags==app.data().today.flags);
      again.key('5');{auto any=[&](const char* t){for(int r=0;r<20;++r)if(dd.has(r,t))return true;return false;};assert(any(fr?"  Amis":"  Friends")&&any(fr?"  Méditation":"  Meditation")&&any(fr?"ACTIVITÉS (2)":"ACTIVITIES (2)"));}again.key(Escape);
      // Close the day: Finish counts 2; the archived day shows Méditation ticked on page 3.
      again.key('9');assert(dd.has(6,fr?"Activités : 2":"Activities: 2"));again.key(Enter);again.key(Enter);
      assert(again.data().today.flags==0&&again.data().history[0].flags==app.data().today.flags);
      again.key(Left);assert(dd.has(4,fr?"2/22 activités":"2/22 activities"));
      again.key('1');again.key(Right);again.key(Right);assert(dd.has(6,fr?"2  Méditation":"2  Meditation")&&dd.has(6,"[x]"));
      again.key(Enter);assert(again.data().history[0].flags==app.data().today.flags); // Read-only.
    }
    // LPS-113-CHG08b: Activities always opens on page 1, first row; 1-9 and 0 act on the current page.
    for(bool fr:{false,true}){
      Display dd;App app(dd);app.start();
      if(fr)choose(app,Language::French),app.key('3');
      app.key('1');assert(dd.has(3,"1/3")&&dd.has(5,fr?"1  Marche":"1  Walk"));
      app.key('2'); // Tick Régime/Diet (row 2 of page 1).
      assert(app.data().today.flags==(1u<<1)&&dd.has(6,"[x]")&&dd.has(3,"1/3"));
      app.key(Right);app.key(Right);assert(dd.has(3,"3/3"));
      app.key('1');assert(app.data().today.flags==((1u<<1)|(ActivityFlags(1u)<<20))); // Amis / Friends.
      app.key('2');assert(app.data().today.flags==((1u<<1)|(ActivityFlags(1u)<<20)|(ActivityFlags(1u)<<21)));
      app.key('3');app.key('9');app.key('0'); // Past the last row of the short page: ignored.
      assert(app.data().today.flags==((1u<<1)|(ActivityFlags(1u)<<20)|(ActivityFlags(1u)<<21))&&dd.has(3,"3/3"));
      app.key(Escape);app.key('1'); // Re-enter from Home: page 1, first row, leftover page forgotten.
      assert(dd.has(3,"1/3")&&dd.has(5,fr?"1  Marche":"1  Walk")&&dd.has(5,"[ ]"));
      app.key(Enter);assert(app.data().today.flags&1u); // Cursor was on row 1 (Marche/Walk).
      app.key(Right);assert(dd.has(3,"2/3")&&dd.has(5,"1  Weekend"));
      app.key('1');assert(app.data().today.flags&(1u<<10)); // 1 on page 2 = Weekend (11), not Marche.
      app.key('0');assert(app.data().today.flags&(ActivityFlags(1u)<<19)); // 0 on page 2 = Famille / Family (20).
      app.key(Escape);app.key('9');app.key(Enter);app.key(Enter); // Close the day with those ticks.
      app.key(Left);app.key('1');assert(dd.has(3,"1/3")); // Archive view also opens on page 1.
      app.key(Right);app.key(Right);assert(dd.has(3,"3/3")&&dd.has(5,"[x]")&&dd.has(6,"[x]"));
      const auto before=app.data().history[0].flags;
      app.key('1');app.key('2');app.key('0');app.key(Enter); // Read-only: number keys and Enter do not change the archived day.
      assert(app.data().history[0].flags==before);
    }
    Display d;App a(d);a.start();assert(a.data().language==Language::English);checkMenus(d,a,false);
    a.key(Up);a.key(Up);a.key(Up);a.key(Up);a.key(Enter);assert(a.currentScreen()==Screen::Config);
    a.key(Enter);a.key(Down);a.key(Escape);assert(d.has(6,"English"));
    a.key(Escape);assert(a.data().language==Language::English&&d.writes==0);
    choose(a,Language::French);assert(d.has(6,"Français"));assert(a.data().language==Language::English&&d.writes==0);
    a.key(Escape);assert(a.data().language==Language::English); // Discard before Save.
    choose(a,Language::French);d.fail=true;a.key('3');assert(a.currentScreen()==Screen::Config);
    assert(a.data().language==Language::English&&d.writes==0&&d.has(17,"Save failed"));
    d.fail=false;a.key('3');assert(a.currentScreen()==Screen::Home&&d.writes==1);
    assert(a.data().language==Language::French&&d.has(18,"Configuration enregistrée"));
    checkMenus(d,a,true);
    a.key('6');a.key('2');a.key('4');assert(d.shown==Palette::Orange);a.key(Enter);
    assert(a.data().palette==Palette::Orange&&a.currentScreen()==Screen::Home);
    a.key('6');a.key('2');a.key('5');assert(d.shown==Palette::Pink);a.key(Enter);
    assert(a.data().palette==Palette::Pink);
    App colorsReload(d);colorsReload.start();assert(colorsReload.data().palette==Palette::Pink&&d.shown==Palette::Pink);
    App reboot(d);reboot.start();assert(reboot.data().language==Language::French);checkMenus(d,reboot,true);
    // Proper names and user text are invariant; save settings never changes XP/days.
    reboot.key('1');reboot.key('1');reboot.key('2');reboot.key('3');reboot.key(Escape);
    choose(reboot,Language::English);reboot.key('3');
    assert(reboot.data().xp==0&&reboot.data().today.flags==7&&reboot.data().today.number==1);
    assert(!reboot.data().today.note[0]);
    reboot.key('8');reboot.key(Escape);reboot.key(Up);reboot.key(Enter);assert(d.has(3,"CLOSE THE DAY?")&&d.has(6,"Activities: 3")&&!d.anyXp());
    reboot.key(Enter);assert(d.has(3,"DAY SAVED")&&!d.anyXp());
    assert(!d.has(8,"NEW KEEPSAKE")&&!d.has(9,"Pocket notebook"));reboot.key(Enter);
    assert(d.has(1,"Friday 18 September 2026 (week 38)")&&!d.anyXp());
    choose(reboot,Language::French);reboot.key('3');
    reboot.key(Left);assert(d.has(6,"1  Activités")&&d.has(7,"2  Suivi du poids"));reboot.key('2');assert(reboot.currentScreen()==Screen::Weight);reboot.key(Escape);
    auto saved=d.stored;auto writes=d.writes;
    reboot.key('4');assert(d.has(18,"lecture seule"));reboot.key('5');assert(reboot.currentScreen()==Screen::Home);
    assert(d.writes==writes&&d.stored.bytes==saved.bytes);
    reboot.key(Right);reboot.key('1');d.fail=true;reboot.key('1');assert(d.has(17,"Échec sauvegarde"));
    d.fail=false;reboot.key('1');reboot.key('2');reboot.key('3');reboot.key(Escape);reboot.key('8');reboot.key(Escape);reboot.key('9');reboot.key(Enter);
    assert(d.has(3,"JOUR ENREGISTRÉ")&&!d.anyXp()&&!d.has(9,"Tasse de thé"));reboot.key(Enter);
    assert(d.has(1,"Samedi 19 Septembre 2026 (semaine 38)"));
    assert(reboot.data().language==Language::French);
    // Reject an invalid language even if an attacker/error recomputed the CRC.
    // LPS-113-CHG06: same check on the JSON save, with a recomputed crc32.
    auto malformed=lps_test::encodeJson(reboot.data());std::string json=malformed.text();
    const auto at=json.find("\"language\":\"fr\"");assert(at!=std::string::npos);json.replace(at,15,"\"language\":\"xx\"");
    malformed.assign(json);
    State decoded;assert(malformed.check(decoded)==SaveCheck::Damaged);
    auto legacy=lps_test::encodeLegacyV6(reboot.data());legacy.bytes[12]=2;
    auto crc=crc32(legacy.bytes.data(),legacy.size-4);
    for(unsigned i=0;i<4;++i)legacy.bytes[legacy.size-4+i]=uint8_t(crc>>(8*i));
    assert(!decode(legacy.bytes.data(),legacy.size,decoded));
    d.stored=malformed;App corrupt(d);corrupt.start();assert(d.has(18,"Read error"));
    choose(corrupt,Language::French);corrupt.key('3');assert(d.has(17,"writes blocked"));
    std::puts("PASS: EN/FR menus, all screen widths, accents, save/cancel/failure, reboot, unchanged user data, history, rewards and invalid-language protection.");
}
