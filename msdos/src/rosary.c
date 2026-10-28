#include "lps.h"
#include <string.h>

enum RosaryPart {
    RP_SIGN, RP_CREED, RP_OPEN_OF, RP_OPEN_HAIL, RP_GLORY, RP_MYSTERY,
    RP_SCRIPTURE, RP_MEDITATION, RP_SILENCE, RP_OUR_FATHER, RP_HAIL,
    RP_FATIMA, RP_HHQ, RP_CLOSING, RP_END_SIGN
};
typedef struct { int part; unsigned decade, count; } RosaryView;

static const char *tr(int lang, const char *en, const char *fr){ return lang==LANG_FR?fr:en; }

const char *mystery_set_name(int lang, int set){
    static const char *en[]={"Joyful","Luminous","Sorrowful","Glorious"};
    static const char *fr[]={"Joyeux","Lumineux","Douloureux","Glorieux"};
    return tr(lang,en[set],fr[set]);
}
const char *mystery_name(int lang, int set, unsigned decade){
    static const char *en[4][5]={
        {"The Annunciation","The Visitation","The Nativity","The Presentation","Finding Jesus in the Temple"},
        {"The Baptism of Jesus","The Wedding at Cana","Proclamation of the Kingdom","The Transfiguration","Institution of the Eucharist"},
        {"The Agony in the Garden","The Scourging at the Pillar","The Crowning with Thorns","The Carrying of the Cross","The Crucifixion"},
        {"The Resurrection","The Ascension","The Descent of the Holy Spirit","The Assumption of Mary","The Coronation of Mary"}};
    static const char *fr[4][5]={
        {"L'Annonciation","La Visitation","La Nativite","La Presentation","Jesus retrouve au Temple"},
        {"Le Bapteme de Jesus","Les Noces de Cana","L'annonce du Royaume","La Transfiguration","L'institution de l'Eucharistie"},
        {"L'Agonie au Jardin","La Flagellation","Le Couronnement d'epines","Le Portement de la Croix","La Crucifixion"},
        {"La Resurrection","L'Ascension","La Pentecote","L'Assomption de Marie","Le Couronnement de Marie"}};
    return tr(lang,en[set][decade],fr[set][decade]);
}
static const char *scripture(int lang, int set, unsigned decade){
    static const char *en[4][5]={
        {"Luke 1:38 - Let it be to me according to your word.","Luke 1:42 - Blessed are you among women.","Luke 2:7 - She gave birth to her firstborn son.","Luke 2:30 - My eyes have seen your salvation.","Luke 2:49 - I must be in my Father's house."},
        {"Matthew 3:17 - This is my beloved Son.","John 2:5 - Do whatever he tells you.","Mark 1:15 - Repent and believe in the Gospel.","Matthew 17:2 - His face shone like the sun.","Luke 22:19 - This is my body, given for you."},
        {"Luke 22:42 - Not my will, but yours be done.","John 19:1 - Pilate took Jesus and had him scourged.","Matthew 27:29 - They placed a crown of thorns on his head.","Luke 23:26 - They laid the cross on Simon.","Luke 23:46 - Father, into your hands I commend my spirit."},
        {"Matthew 28:6 - He is not here; he has risen.","Acts 1:9 - He was lifted up before their eyes.","Acts 2:4 - They were all filled with the Holy Spirit.","Luke 1:49 - The Almighty has done great things for me.","Revelation 12:1 - A woman clothed with the sun."}};
    static const char *fr[4][5]={
        {"Luc 1,38 - Qu'il me soit fait selon ta parole.","Luc 1,42 - Tu es benie entre les femmes.","Luc 2,7 - Elle mit au monde son fils premier-ne.","Luc 2,30 - Mes yeux ont vu ton salut.","Luc 2,49 - Je dois etre chez mon Pere."},
        {"Matthieu 3,17 - Celui-ci est mon Fils bien-aime.","Jean 2,5 - Faites tout ce qu'il vous dira.","Marc 1,15 - Convertissez-vous et croyez a l'Evangile.","Matthieu 17,2 - Son visage devint brillant comme le soleil.","Luc 22,19 - Ceci est mon corps donne pour vous."},
        {"Luc 22,42 - Non pas ma volonte, mais la tienne.","Jean 19,1 - Pilate fit flageller Jesus.","Matthieu 27,29 - Ils poserent sur sa tete une couronne d'epines.","Luc 23,26 - Ils chargerent Simon de la croix.","Luc 23,46 - Pere, entre tes mains je remets mon esprit."},
        {"Matthieu 28,6 - Il n'est pas ici, il est ressuscite.","Actes 1,9 - Il s'eleva sous leurs yeux.","Actes 2,4 - Tous furent remplis de l'Esprit Saint.","Luc 1,49 - Le Puissant fit pour moi des merveilles.","Apocalypse 12,1 - Une femme vetue du soleil."}};
    return tr(lang,en[set][decade],fr[set][decade]);
}
static const char *meditation(int lang, int set, unsigned decade){
    static const char *en[4][5]={
        {"Ask for Mary's trust: receive God's call without fear.","Carry Christ toward another person with humble joy.","Welcome Jesus in poverty, simplicity and gratitude.","Offer to God what is most precious, without possessing it.","Seek Christ patiently whenever he seems absent."},
        {"Remember your baptism and live today as a beloved child of God.","Entrust the ordinary needs of life to Christ and obey him.","Let Christ rule first within your own heart.","Ask to see Christ's light behind present difficulties.","Receive Christ's self-gift and learn to give yourself."},
        {"Bring anguish to the Father and choose his will in trust.","Pray for endurance when suffering is undeserved.","Reject pride and contemplate the quiet kingship of Christ.","Carry today's burden beside Jesus, one step at a time.","Remain at the Cross and receive the mercy flowing from it."},
        {"Let the risen Christ awaken hope where life seems closed.","Lift your heart toward heaven while serving faithfully on earth.","Ask the Holy Spirit for courage, wisdom and charity.","Entrust your whole life to God as Mary did.","Contemplate the dignity promised to a life united with God."}};
    static const char *fr[4][5]={
        {"Demandez la confiance de Marie : accueillir l'appel de Dieu sans peur.","Portez le Christ vers autrui avec une joie humble.","Accueillez Jesus dans la pauvrete, la simplicite et la gratitude.","Offrez a Dieu ce qui est precieux sans chercher a le posseder.","Cherchez patiemment le Christ lorsqu'il semble absent."},
        {"Souvenez-vous de votre bapteme et vivez en enfant aime de Dieu.","Confiez au Christ les besoins ordinaires et faites ce qu'il dit.","Laissez d'abord le Christ regner dans votre propre coeur.","Demandez a voir la lumiere du Christ derriere les difficultes.","Recevez le don du Christ et apprenez a vous donner."},
        {"Portez votre angoisse au Pere et choisissez sa volonte avec confiance.","Demandez la force d'endurer une souffrance immeritee.","Rejetez l'orgueil et contemplez la royaute silencieuse du Christ.","Portez le fardeau du jour aupres de Jesus, pas apres pas.","Demeurez au pied de la Croix et recevez sa misericorde."},
        {"Que le Christ ressuscite reveille l'esperance la ou tout semble ferme.","Elevez votre coeur vers le ciel en servant fidelement sur terre.","Demandez a l'Esprit Saint courage, sagesse et charite.","Confiez toute votre vie a Dieu comme Marie.","Contemplez la dignite promise a une vie unie a Dieu."}};
    return tr(lang,en[set][decade],fr[set][decade]);
}

RosaryView rosary_view(int mode, unsigned rosaryDecade, unsigned step){
    RosaryView v; unsigned local, extras, block, decade, pos, p;
    memset(&v,0,sizeof(v));
    if(mode==ROS_DECADE){
        if(step==0){ v.part=RP_SIGN; v.decade=rosaryDecade; }
        else if(step==1){ v.part=RP_MYSTERY; v.decade=rosaryDecade; }
        else if(step==2){ v.part=RP_OUR_FATHER; v.decade=rosaryDecade; }
        else if(step<13){ v.part=RP_HAIL; v.decade=rosaryDecade; v.count=step-2; }
        else if(step==13){ v.part=RP_GLORY; v.decade=rosaryDecade; }
        else if(step==14){ v.part=RP_FATIMA; v.decade=rosaryDecade; }
        else if(step==15){ v.part=RP_CLOSING; v.decade=rosaryDecade; }
        else { v.part=RP_END_SIGN; v.decade=rosaryDecade; }
        return v;
    }
    if(step==0){ v.part=RP_SIGN; return v; }
    if(step==1){ v.part=RP_CREED; return v; }
    if(step==2){ v.part=RP_OPEN_OF; return v; }
    if(step>=3&&step<=5){ v.part=RP_OPEN_HAIL; v.count=step-2; return v; }
    if(step==6){ v.part=RP_GLORY; return v; }
    local=step-7;
    extras = mode==ROS_FULL?0: mode==ROS_GUIDED?2:3;
    block=14+extras;
    if(local<5*block){
        decade=local/block; pos=local%block;
        if(pos==0){ v.part=RP_MYSTERY; v.decade=decade; return v; }
        p=pos-1;
        if(extras){ if(p==0){ v.part=RP_SCRIPTURE; v.decade=decade; return v; }
            if(p==1){ v.part=RP_MEDITATION; v.decade=decade; return v; }
            if(extras==3&&p==2){ v.part=RP_SILENCE; v.decade=decade; return v; }
            p-=extras; }
        if(p==0){ v.part=RP_OUR_FATHER; v.decade=decade; return v; }
        if(p<=10){ v.part=RP_HAIL; v.decade=decade; v.count=p; return v; }
        if(p==11){ v.part=RP_GLORY; v.decade=decade; return v; }
        v.part=RP_FATIMA; v.decade=decade; return v;
    }
    local-=5*block;
    if(local==0){ v.part=RP_HHQ; v.decade=4; return v; }
    if(local==1){ v.part=RP_CLOSING; v.decade=4; return v; }
    v.part=RP_END_SIGN; v.decade=4; return v;
}

unsigned rosary_steps(int mode){
    unsigned extras;
    if(mode==ROS_DECADE) return 17;
    extras = mode==ROS_FULL?0: mode==ROS_GUIDED?2:3;
    return 7+5*(14+extras)+3;
}

const char *rosary_text(int lang, int mode, int set, unsigned rosaryDecade, unsigned step){
    RosaryView v=rosary_view(mode,rosaryDecade,step);
    (void)mode;
    switch(v.part){
    case RP_SIGN: case RP_END_SIGN:
        return tr(lang,"In the name of the Father, and of the Son, and of the Holy Spirit. Amen.","Au nom du Pere, du Fils et du Saint-Esprit. Amen.");
    case RP_CREED:
        return tr(lang,
            "I believe in God, the Father almighty, Creator of heaven and earth, and in Jesus Christ, his only Son, our Lord, who was conceived by the Holy Spirit, born of the Virgin Mary, suffered under Pontius Pilate, was crucified, died and was buried; he descended into hell; on the third day he rose again from the dead; he ascended into heaven, and is seated at the right hand of God the Father almighty; from there he will come to judge the living and the dead. I believe in the Holy Spirit, the holy catholic Church, the communion of saints, the forgiveness of sins, the resurrection of the body, and life everlasting. Amen.",
            "Je crois en Dieu, le Pere tout-puissant, createur du ciel et de la terre. Et en Jesus-Christ, son Fils unique, notre Seigneur, qui a ete concu du Saint-Esprit, est ne de la Vierge Marie, a souffert sous Ponce Pilate, a ete crucifie, est mort et a ete enseveli, est descendu aux enfers. Le troisieme jour est ressuscite des morts, est monte aux cieux, est assis a la droite de Dieu le Pere tout-puissant, d'ou il viendra juger les vivants et les morts. Je crois en l'Esprit Saint, a la sainte Eglise catholique, a la communion des saints, a la remission des peches, a la resurrection de la chair, a la vie eternelle. Amen.");
    case RP_OPEN_OF: case RP_OUR_FATHER:
        return tr(lang,
            "Our Father, who art in heaven, hallowed be thy name; thy kingdom come; thy will be done on earth as it is in heaven. Give us this day our daily bread; and forgive us our trespasses, as we forgive those who trespass against us; and lead us not into temptation, but deliver us from evil. Amen.",
            "Notre Pere, qui es aux cieux, que ton nom soit sanctifie, que ton regne vienne, que ta volonte soit faite sur la terre comme au ciel. Donne-nous aujourd'hui notre pain de ce jour. Pardonne-nous nos offenses, comme nous pardonnons aussi a ceux qui nous ont offenses. Et ne nous laisse pas entrer en tentation, mais delivre-nous du Mal. Amen.");
    case RP_OPEN_HAIL: case RP_HAIL:
        return tr(lang,
            "Hail Mary, full of grace, the Lord is with thee. Blessed art thou among women, and blessed is the fruit of thy womb, Jesus. Holy Mary, Mother of God, pray for us sinners, now and at the hour of our death. Amen.",
            "Je vous salue, Marie, pleine de grace ; le Seigneur est avec vous. Vous etes benie entre toutes les femmes, et Jesus, le fruit de vos entrailles, est beni. Sainte Marie, Mere de Dieu, priez pour nous pauvres pecheurs, maintenant et a l'heure de notre mort. Amen.");
    case RP_GLORY:
        return tr(lang,
            "Glory be to the Father, and to the Son, and to the Holy Spirit, as it was in the beginning, is now, and ever shall be, world without end. Amen.",
            "Gloire au Pere, au Fils et au Saint-Esprit, comme il etait au commencement, maintenant et toujours, pour les siecles des siecles. Amen.");
    case RP_MYSTERY: return mystery_name(lang,set,v.decade);
    case RP_SCRIPTURE: return scripture(lang,set,v.decade);
    case RP_MEDITATION: return meditation(lang,set,v.decade);
    case RP_SILENCE:
        return tr(lang,"Remain in silence. Place this mystery, your intentions and your whole attention before God. Press Enter when you are ready to continue.","Demeurez en silence. Placez ce mystere, vos intentions et toute votre attention devant Dieu. Appuyez sur Entree lorsque vous etes pret a continuer.");
    case RP_FATIMA:
        return tr(lang,"O my Jesus, forgive us our sins, save us from the fires of hell, lead all souls to heaven, especially those most in need of thy mercy. Amen.","O mon Jesus, pardonnez-nous nos peches, preservez-nous du feu de l'enfer et conduisez au ciel toutes les ames, surtout celles qui ont le plus besoin de votre misericorde. Amen.");
    case RP_HHQ:
        return tr(lang,"Hail, holy Queen, Mother of mercy, our life, our sweetness and our hope. To thee do we cry, poor banished children of Eve. To thee do we send up our sighs, mourning and weeping in this valley of tears. Turn then, most gracious advocate, thine eyes of mercy toward us, and after this our exile show unto us the blessed fruit of thy womb, Jesus. O clement, O loving, O sweet Virgin Mary. Pray for us, O holy Mother of God, that we may be made worthy of the promises of Christ.","Salut, o Reine, Mere de misericorde, notre vie, notre douceur et notre esperance, salut. Enfants d'Eve exiles, nous crions vers vous. Vers vous nous soupirons, gemissant et pleurant dans cette vallee de larmes. O vous, notre avocate, tournez vers nous vos regards misericordieux. Et apres cet exil, montrez-nous Jesus, le fruit beni de vos entrailles. O clemente, o misericordieuse, o douce Vierge Marie. Priez pour nous, sainte Mere de Dieu, afin que nous devenions dignes des promesses du Christ.");
    case RP_CLOSING:
        return tr(lang,"O God, whose only-begotten Son, by his life, death and resurrection, has purchased for us the rewards of eternal life: grant that, meditating upon these mysteries of the holy Rosary, we may imitate what they contain and obtain what they promise, through Christ our Lord. Amen.","O Dieu, dont le Fils unique, par sa vie, sa mort et sa resurrection, nous a acquis les recompenses de la vie eternelle, accordez-nous, en meditant ces mysteres du tres saint Rosaire, d'imiter ce qu'ils contiennent et d'obtenir ce qu'ils promettent, par le Christ notre Seigneur. Amen.");
    default: return "";
    }
}

const char *rosary_part_name(int lang, int mode, unsigned rosaryDecade, unsigned step){
    RosaryView v=rosary_view(mode,rosaryDecade,step);
    switch(v.part){
    case RP_SIGN: case RP_END_SIGN: return tr(lang,"SIGN OF THE CROSS","SIGNE DE CROIX");
    case RP_CREED: return tr(lang,"APOSTLES' CREED","SYMBOLE DES APOTRES");
    case RP_OPEN_OF: case RP_OUR_FATHER: return tr(lang,"OUR FATHER","NOTRE PERE");
    case RP_OPEN_HAIL: case RP_HAIL: return tr(lang,"HAIL MARY","JE VOUS SALUE MARIE");
    case RP_GLORY: return tr(lang,"GLORY BE","GLOIRE AU PERE");
    case RP_MYSTERY: return tr(lang,"MYSTERY","MYSTERE");
    case RP_SCRIPTURE: return tr(lang,"SCRIPTURE","ECRITURE");
    case RP_MEDITATION: return tr(lang,"MEDITATION","MEDITATION");
    case RP_SILENCE: return tr(lang,"CONTEMPLATIVE SILENCE","SILENCE CONTEMPLATIF");
    case RP_FATIMA: return tr(lang,"FATIMA PRAYER","PRIERE DE FATIMA");
    case RP_HHQ: return tr(lang,"HAIL, HOLY QUEEN","SALUT, O REINE");
    case RP_CLOSING: return tr(lang,"CLOSING PRAYER","PRIERE FINALE");
    default: return "";
    }
}

int rosary_part(int mode, unsigned rosaryDecade, unsigned step){
    return rosary_view(mode,rosaryDecade,step).part;
}
unsigned rosary_view_decade(int mode, unsigned rosaryDecade, unsigned step){
    return rosary_view(mode,rosaryDecade,step).decade;
}
unsigned rosary_view_count(int mode, unsigned rosaryDecade, unsigned step){
    return rosary_view(mode,rosaryDecade,step).count;
}

/* expose part enums to app via macros in app */
enum { _RP_SIGN=RP_SIGN,_RP_CREED=RP_CREED,_RP_OPEN_OF=RP_OPEN_OF,_RP_OPEN_HAIL=RP_OPEN_HAIL,
_RP_GLORY=RP_GLORY,_RP_MYSTERY=RP_MYSTERY,_RP_SCRIPTURE=RP_SCRIPTURE,_RP_MEDITATION=RP_MEDITATION,
_RP_SILENCE=RP_SILENCE,_RP_OUR_FATHER=RP_OUR_FATHER,_RP_HAIL=RP_HAIL,_RP_FATIMA=RP_FATIMA,
_RP_HHQ=RP_HHQ,_RP_CLOSING=RP_CLOSING,_RP_END_SIGN=RP_END_SIGN };
