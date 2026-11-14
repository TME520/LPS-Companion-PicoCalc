// LPS-114-CHG01: Meal Planner (hidden SPACE mode, fixed deck of 16 cards).
#include "../src/lps_companion.cpp"
#include "save_helpers.h"
#include <cassert>
#include <string>
#include <vector>
using namespace lps;
class Display final:public Platform {
public:
    lps_test::JsonStore stored{};bool exists=false,failSave=false;unsigned saves=0;
    std::array<std::array<char,41>,20> rows{};
    MealCardView card{};bool hasCard=false;
    void clear()override{for(auto& row:rows)row.fill(0);hasCard=false;}
    void text(int x,int y,const char* text,bool)override{
        assert(x==0&&y>=0&&y%16==0&&y+16<=320);
        assert(std::strlen(text)<=40);
        std::snprintf(rows[y/16].data(),41,"%s",text);
    }
    void alert(int x,int y,const char* value)override{this->text(x,y,value,true);}
    void mealCard(const MealCardView& v)override{card=v;hasCard=true;}
    void present()override{}
    void setPalette(Palette)override{}
    LoadResult load(State& state)override{if(!exists)return LoadResult::Missing;return stored.read(state);}
    bool save(const State& state)override{if(failSave||!stored.write(state))return false;exists=true;++saves;return true;}
    bool exportIcs(const Day&,uint32_t,uint32_t,Language,const State&)override{return true;}
    bool exportWeightCsv(const Day&)override{return true;}
    bool has(int row,const char* utf8)const{char cells[100];displayText(utf8,cells,sizeof cells);return std::strstr(rows[row].data(),cells)!=nullptr;}
    bool has(const char* utf8)const{for(int r=0;r<20;++r)if(has(r,utf8))return true;return false;}
};
// Records the card drawing into a 320x320 frame.
struct Frame final:Canvas {
    static constexpr uint32_t Unset=0xff000000u;
    std::vector<uint32_t> px=std::vector<uint32_t>(320*320,Unset);
    std::vector<std::string> labels;
    void set(int x,int y,uint32_t c){assert(x>=0&&y>=0&&x<320&&y<320);px[std::size_t(y*320+x)]=c;}
    void fill(int x,int y,int w,int h,uint32_t rgb)override{for(int j=0;j<h;++j)for(int i=0;i<w;++i)set(x+i,y+j,rgb);}
    void pixels(int x,int y,int w,const uint32_t* rgb)override{for(int i=0;i<w;++i)set(x+i,y,rgb[i]);}
    void label(int x,int y,const char* latin1,uint32_t,uint32_t bg)override{
        const int n=int(std::strlen(latin1));
        assert(x>=MealCardX+4&&x+n*8<=MealCardX+MealCardW-4);
        for(int j=0;j<16;++j)for(int i=0;i<n*8;++i)set(x+i,y+j,bg);
        labels.push_back(latin1);
    }
    uint32_t at(int x,int y)const{return px[std::size_t(y*320+x)];}
};
bool grey(uint32_t c){return (c>>16)==((c>>8)&255)&&((c>>8)&255)==(c&255);}
void choose(App& app,Language language){app.key('6');app.key('1');app.key(language==Language::French?'2':'1');app.key('3');}
std::vector<unsigned> order(const State& s){return std::vector<unsigned>(s.meals.order.begin(),s.meals.order.begin()+s.meals.playedCount);}
int main(){
    // The deck: 16 cards in three rows, in the agreed order, duplicates separate.
    {
        static_assert(MealCardCount==16,"16 cards");
        unsigned perRow[3]{};for(const MealCard& c:MealDeck)++perRow[unsigned(c.row)];
        assert(perRow[0]==6&&perRow[1]==8&&perRow[2]==2);
        for(unsigned r=0;r<3;++r)for(unsigned i=MealRowFirst[r];i<MealRowFirst[r+1];++i)assert(unsigned(MealDeck[i].row)==r);
        assert(!std::strcmp(MealDeck[0].en,"Coffee (Latte)")&&!std::strcmp(MealDeck[1].en,"Coffee (Black)")&&!std::strcmp(MealDeck[2].en,"Coffee (Black)"));
        assert(!std::strcmp(MealDeck[4].fr,"Thé à la menthe")&&!std::strcmp(MealDeck[6].en,"Light Cheese Wrap")&&!std::strcmp(MealDeck[7].en,"Light Cheese Wrap"));
        assert(!std::strcmp(MealDeck[13].en,"Soup")&&!std::strcmp(MealDeck[14].en,"Blank")&&!std::strcmp(MealDeck[15].fr,"Blank"));
        // Every label fits inside the card (22 cells) and uses glyphs the LCD can draw.
        for(const MealCard& c:MealDeck)for(const char* t:{c.en,c.fr,c.enPortion,c.frPortion})assert(displayWidth(t)<=22);
        for(const MealCard& c:MealDeck)assert(c.sprite<meal_sprites::Count);
    }
    // SPACE opens the hidden mode from Home; SPACE and ESC go back.
    {
        Display d;App app(d);app.start();
        assert(app.currentScreen()==Screen::Home&&!d.has("Meal")&&!d.has("MEAL")&&!d.hasCard); // no visible menu entry
        app.key(' ');assert(app.currentScreen()==Screen::MealPlanner&&d.hasCard&&d.card.card==0&&d.card.position==0);
        assert(d.has(3,"MEAL PLANNER")&&d.has(4,"Drinks 1/6")&&d.has(4,"16/16 left")&&d.has(19,"Enter:Play")&&d.has(19,"Space/Esc:Back"));
        app.key(' ');assert(app.currentScreen()==Screen::Home&&!d.hasCard);
        app.key(' ');app.key(Escape);assert(app.currentScreen()==Screen::Home);
        // Home keys still work as before after a visit: digits, arrows, Enter.
        app.key('1');assert(app.currentScreen()==Screen::Activities);app.key(Escape);
        app.key(Down);app.key(Down);app.key(Enter);assert(app.currentScreen()==Screen::Weight);app.key(Escape);
        app.key('9');assert(app.currentScreen()==Screen::Finish);app.key(Escape);
        // SPACE elsewhere keeps its meaning (text entry, other screens).
        app.key('3');app.key('n');app.key('a');app.key(' ');app.key('b');assert(app.currentScreen()==Screen::TaskEdit);
        app.key(Enter);assert(!std::strcmp(app.data().tasks[0].name.data(),"a b"));app.key(Escape);
        app.key('1');app.key(' ');assert(app.currentScreen()==Screen::Activities);app.key(Escape);
        assert(d.saves==1); // only the Kanban task was saved; visiting the planner writes nothing
    }
    // Navigation: Left/Right wrap inside a row; Up/Down wrap between rows and keep the column.
    {
        Display d;App app(d);app.start();app.key(' ');
        app.key(Left);assert(d.card.card==5&&d.has(4,"Drinks 6/6"));
        app.key(Right);assert(d.card.card==0);
        for(int i=0;i<4;++i)app.key(Right);assert(d.card.card==4);
        app.key(Down);assert(d.card.card==10&&d.has(4,"Food 5/8"));
        app.key(Down);assert(d.card.card==15&&d.has(4,"Blank 2/2")); // column clamped
        app.key(Down);assert(d.card.card==1&&d.has(4,"Drinks 2/6")); // wraps to the first row
        app.key(Up);assert(d.card.card==15);
        app.key(Up);assert(d.card.card==7);
        app.key(Up);assert(d.card.card==1);
        app.key(Up);app.key(Right);app.key(Right);assert(d.card.card==15);
        app.key(Down);app.key(Down);app.key(Left);assert(d.card.card==6&&d.has(4,"Food 1/8"));
        app.key(Left);assert(d.card.card==13);
        assert(d.saves==0);
    }
    // ENTER plays (greyed, counted, saved at once); ENTER again puts the card back.
    {
        Display d;App app(d);app.start();app.key(' ');
        app.key(Enter);assert(d.card.position==1&&d.saves==1&&order(app.data())==std::vector<unsigned>{0});
        assert(d.has(3,"PLAYED #1")&&d.has(4,"15/16 left")&&d.has(19,"Enter:Put back"));
        app.key(Right);app.key(Enter);app.key(Down);app.key(Enter);app.key(Down);app.key(Enter);
        assert(order(app.data())==(std::vector<unsigned>{0,1,7,15}));assert(d.has(4,"12/16 left")&&d.has(3,"PLAYED #4"));
        // Put back the second card played: later plays move up.
        app.key(Up);app.key(Up);assert(d.card.card==1&&d.card.position==2);
        app.key(Enter);assert(d.card.position==0&&order(app.data())==(std::vector<unsigned>{0,7,15})&&d.has(4,"13/16 left")&&!d.has(3,"PLAYED"));
        app.key(Enter);assert(d.card.position==4&&order(app.data())==(std::vector<unsigned>{0,7,15,1}));
        // Both coffees (Black) are separate cards.
        app.key(Right);app.key(Enter);assert(order(app.data())==(std::vector<unsigned>{0,7,15,1,2}));
        // Reopening lands on the first card still in the deck.
        app.key(Escape);app.key(' ');assert(d.card.card==3&&d.card.position==0);
        // All 16 played: 0/16 left; reopening shows the first card.
        for(unsigned i=0;i<MealCardCount;++i){app.key(Escape);app.key(' ');if(!d.card.position)app.key(Enter);}
        assert(app.data().meals.playedCount==16&&d.has(4,"0/16 left"));
        app.key(Escape);app.key(' ');assert(d.card.card==0&&d.card.position==1);
        // French labels.
        app.key(Escape);choose(app,Language::French);app.key(' ');
        assert(d.card.language==Language::French&&d.has(3,"PLANIFICATEUR DE REPAS")&&d.has(3,"JOUÉE #1")&&d.has(4,"Boissons 1/6")&&d.has(4,"0/16 restantes"));
        assert(d.has(19,"Entrée:Remettre")&&d.has(17,"Haut/Bas"));
        app.key(Down);assert(d.has(4,"Nourriture 1/8"));
    }
    // Persistence: the deck and its order survive a restart; the JSON holds the order.
    {
        Display d;
        {App app(d);app.start();app.key(' ');app.key(Down);app.key(Right);app.key(Right);app.key(Enter);app.key(Up);app.key(Enter);app.key(Left);app.key(Enter);}
        assert(d.stored.text().find("\"meals\":{\"played\":[8,2,1]}")!=std::string::npos);
        {State s;assert(d.stored.check(s)==SaveCheck::Valid&&order(s)==(std::vector<unsigned>{8,2,1}));}
        App again(d);again.start();assert(order(again.data())==(std::vector<unsigned>{8,2,1}));
        again.key(' ');assert(d.card.card==0);
        // A v1.13 (schema 2) save without "meals" loads with a full deck.
        std::string json=d.stored.text();const auto at=json.find(",\n\"meals\"");assert(at!=std::string::npos);
        json.erase(at,json.find("}",at)+1-at);assert(json.find("meals")==std::string::npos);
        d.stored.assign(json);App old(d);old.start();assert(old.data().meals.playedCount==0&&old.currentScreen()==Screen::Home);
        old.key(' ');old.key(Enter);assert(order(old.data())==std::vector<unsigned>{0});
        // Unknown keys inside "meals" are skipped; bad indices or duplicates are damage.
        auto variant=[&](const char* meals){
            std::string j=d.stored.text();const auto m=j.find("\"meals\":");const auto end=j.find("}",j.find("]",m))+1;
            j.replace(m,end-m,meals);lps_test::JsonStore probe;probe.assign(j);State s;return std::make_pair(probe.check(s),order(s));
        };
        assert(variant("\"meals\":{\"later\":[1,{\"x\":2}],\"played\":[3,1]}")==std::make_pair(SaveCheck::Valid,std::vector<unsigned>{3,1}));
        assert(variant("\"meals\":{}").first==SaveCheck::Valid);
        assert(variant("\"meals\":{\"played\":[16]}").first==SaveCheck::Damaged);
        assert(variant("\"meals\":{\"played\":[-1]}").first==SaveCheck::Damaged);
        assert(variant("\"meals\":{\"played\":[2,2]}").first==SaveCheck::Damaged);
        assert(variant("\"meals\":{\"played\":[\"2\"]}").first==SaveCheck::Damaged);
        assert(variant("\"meals\":{\"played\":[1.5]}").first==SaveCheck::Damaged);
        assert(variant("\"meals\":[]").first==SaveCheck::Damaged);
        assert(variant("\"meals\":{\"played\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}").first==SaveCheck::Valid);
        assert(SaveSchemaVersion==2);
    }
    // A failed save rolls the play back; a damaged save blocks plays; archive view refuses SPACE.
    {
        Display d;App app(d);app.start();app.key(' ');d.failSave=true;
        app.key(Enter);assert(app.data().meals.playedCount==0&&d.card.position==0&&d.has(18,"Save failed"));
        d.failSave=false;app.key(Enter);assert(app.data().meals.playedCount==1);
        // Close a day (needs a date) so an archive exists, then browse it.
        app.key(Escape);app.key('9');app.key(Enter);assert(app.currentScreen()==Screen::Reward);app.key(Enter);
        app.key(Left);app.key(' ');assert(app.currentScreen()==Screen::Home&&d.has("Archived day: read-only"));
        app.key(Right);app.key(' ');assert(app.currentScreen()==Screen::MealPlanner);
        Display broken;broken.exists=true;broken.stored.assign("{\"format\":\"lps-companion\"",false);
        App blocked(broken);blocked.start();blocked.key(' ');blocked.key(Enter);
        assert(blocked.data().meals.playedCount==0&&broken.has("writes blocked"));
    }
    // New day, new deck: closing the day deals 16 fresh cards; history is unchanged by the deck.
    {
        Display d;App app(d);app.start();app.key(' ');app.key(Enter);app.key(Right);app.key(Enter);
        assert(app.data().meals.playedCount==2);
        app.key(Escape);app.key('9');app.key(Enter);assert(app.currentScreen()==Screen::Reward);
        assert(app.data().meals.playedCount==0&&app.data().count==1);
        {State s;assert(d.stored.check(s)==SaveCheck::Valid&&s.meals.playedCount==0);}
        app.key(Enter);app.key(' ');assert(d.has(4,"16/16 left")&&d.card.card==0);
        // A failed close keeps today's deck.
        app.key(Enter);app.key(Escape);app.key('9');d.failSave=true;app.key(Enter);
        assert(app.data().meals.playedCount==1);d.failSave=false;
    }
    // Drawing: a coloured card with its sprite; played cards entirely grey; nothing outside the card.
    {
        Frame live;drawMealCard(live,MealCardView{11,0,Language::English});
        Frame played;drawMealCard(played,MealCardView{11,3,Language::English});
        Frame fr;drawMealCard(fr,MealCardView{4,0,Language::French});
        assert(live.labels==(std::vector<std::string>{"Food","Apple","1 medium"}));
        char cells[32];displayText("Thé à la menthe",cells,sizeof cells);
        assert(fr.labels[0]=="Boissons"&&fr.labels[1]==cells);
        for(int y=0;y<320;++y)for(int x=0;x<320;++x){
            const bool inside=x>=MealCardX&&x<MealCardX+MealCardW&&y>=MealCardY&&y<MealCardY+MealCardH;
            if(!inside)assert(live.at(x,y)==Frame::Unset&&played.at(x,y)==Frame::Unset);
            else if(played.at(x,y)!=Frame::Unset)assert(grey(played.at(x,y)));
        }
        assert(live.at(MealCardX,MealCardY)==Frame::Unset); // rounded corner
        assert(live.at(MealCardX+1,MealCardY+MealCardH/2)==MealRowColours[1]);
        assert(fr.at(MealCardX+1,MealCardY+MealCardH/2)==MealRowColours[0]);
        assert(live.at(MealCardX+6,MealCardY+MealCardH-12)==MealCardBody);
        // The apple sprite brings red into the card centre; the played one is grey.
        unsigned red=0;for(int y=MealCardY+24;y<MealCardY+152;++y)for(int x=MealCardX+32;x<MealCardX+160;++x)if(live.at(x,y)==meal_sprites::Palette[6])++red;
        assert(red>1000);
        assert(played.at(MealCardX+1,MealCardY+MealCardH/2)==mealGrey(MealRowColours[1]));
        // Sprites are 4 bits per pixel in flash: 512 bytes each.
        static_assert(sizeof meal_sprites::Pixels==meal_sprites::Count*512,"4bpp 32x32");
    }
    // Memory: the deck adds 17 bytes to State.
    static_assert(sizeof(MealDeckState)==17,"compact deck state");
    std::printf("State: %zu bytes, sprites: %zu bytes\n",sizeof(State),sizeof meal_sprites::Pixels+sizeof meal_sprites::Palette);
    std::puts("PASS: Meal Planner: hidden SPACE entry/exit, home keys intact, row/card wrap, play/put back with order, greyed card, counter, FR labels, save/restore, v2 save without deck, invalid deck data, failed-save rollback, read-only archive, new deck at day close and card drawing.");
}
