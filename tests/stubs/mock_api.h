#pragma once
// Host-only stand-ins: real adapter code uses ordinary files instead of FatFs.
#include <cstdio>
#include <cerrno>
#include <cstring>
using UINT=unsigned;
using FRESULT=int;
constexpr int FR_OK=0,FR_NO_FILE=1,FR_NO_PATH=2,FR_EXIST=3,FR_DISK_ERR=4;
constexpr int FA_READ=1,FA_WRITE=2,FA_CREATE_ALWAYS=4,GPIO_OUT=1;
struct FIL { FILE* file=nullptr; };
struct sd_card_t { int fatfs=0; };
inline bool mockReadError=false;
inline bool mockWriteError=false;
inline unsigned mockWrites=0;
inline FRESULT f_open(FIL* f,const char* path,int mode){
    if(mockReadError&&mode==FA_READ)return FR_DISK_ERR;
    const char* name=std::strrchr(path,'/');name=name?name+1:path;
    f->file=std::fopen(name,mode==FA_READ?"rb":"wb");
    return f->file?FR_OK:(errno==ENOENT?FR_NO_FILE:FR_DISK_ERR);
}
inline long f_size(FIL* f){std::fseek(f->file,0,SEEK_END);long n=std::ftell(f->file);std::rewind(f->file);return n;}
inline FRESULT f_read(FIL* f,void* b,UINT n,UINT* got){*got=std::fread(b,1,n,f->file);return std::ferror(f->file)?FR_DISK_ERR:FR_OK;}
inline FRESULT f_write(FIL* f,const void* b,UINT n,UINT* got){++mockWrites;if(mockWriteError){*got=0;return FR_DISK_ERR;}*got=std::fwrite(b,1,n,f->file);return *got==n?FR_OK:FR_DISK_ERR;}
inline FRESULT f_sync(FIL* f){return std::fflush(f->file)==0?FR_OK:FR_DISK_ERR;}
inline FRESULT f_close(FIL* f){return std::fclose(f->file)==0?FR_OK:FR_DISK_ERR;}
inline FRESULT f_unlink(const char* path){const char* name=std::strrchr(path,'/');name=name?name+1:path;return std::remove(name)==0?FR_OK:FR_DISK_ERR;}
inline FRESULT f_mkdir(const char*){return FR_EXIST;}
inline FRESULT f_mount(int*,const char*,int){return FR_OK;}
inline bool sd_init_driver(){return true;}
inline sd_card_t* sd_get_by_num(int){static sd_card_t card;return &card;}
inline void set_sys_clock_khz(int,bool){}
inline void init_i2c_kbd(){}
inline void lcd_init(){}
inline void gpio_init(int){}
inline void gpio_set_dir(int,int){}
inline void gpio_put(int,int){}
inline int read_i2c_kbd(){return -1;}
inline void sleep_ms(int){}
