/* HELLO.EXE — Book 8088 / 8086 BIOS smoke test. No VRAM, no float, no dirs. */
#include <dos.h>

static void puts_bios(const char *s){
    union REGS r;
    while(*s){
        if(*s=='\n'){
            r.h.ah=0x0E; r.h.al='\r'; r.h.bl=0x07; int86(0x10,&r,&r);
            r.h.ah=0x0E; r.h.al='\n'; r.h.bl=0x07; int86(0x10,&r,&r);
        }else{
            r.h.ah=0x0E; r.h.al=(unsigned char)*s; r.h.bl=0x07; int86(0x10,&r,&r);
        }
        ++s;
    }
}

int main(void){
    union REGS r;
    unsigned equip;
    unsigned mode;
    puts_bios("\nLPS COMPANION - HELLO\n");
    puts_bios("If you see this, the EXE runs on this 8088.\n");
    r.h.ah=0x0F; int86(0x10,&r,&r); mode=r.h.al;
    equip = *(unsigned far *)MK_FP(0x40, 0x10);
    {
        char buf[64];
        /* tiny itoa for mode/equip */
        puts_bios("BIOS video mode: ");
        buf[0]=(char)('0'+((mode/16)&15)); if(buf[0]>'9') buf[0]=(char)(buf[0]-'0'+'A'-10);
        /* simpler: print hex nibble */
        {
            static const char H[]="0123456789ABCDEF";
            char m[8];
            m[0]=H[(mode>>4)&15]; m[1]=H[mode&15]; m[2]='\n'; m[3]=0;
            puts_bios(m);
        }
        puts_bios("Equipment word: ");
        {
            static const char H[]="0123456789ABCDEF";
            char m[8];
            m[0]=H[(equip>>12)&15]; m[1]=H[(equip>>8)&15];
            m[2]=H[(equip>>4)&15]; m[3]=H[equip&15]; m[4]='\n'; m[5]=0;
            puts_bios(m);
        }
        if(((equip>>4)&3)==3) puts_bios("Video: MDA/mono (B000)\n");
        else puts_bios("Video: CGA/colour (B800)\n");
    }
    puts_bios("Press any key to quit.\n");
    r.h.ah=0x00; int86(0x16,&r,&r);
    return 0;
}
