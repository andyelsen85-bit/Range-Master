#pragma once
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cctype>
inline constexpr unsigned FAT_NAME_VERSION=2;

// The mounted volume has LFN disabled. Never append a second extension.
// Also used for directory entries: reject unknown names, never delete them.
inline bool fat_name(char *out,size_t capacity,const char *base,const char *extension=nullptr,
                     const char *root="/fatfs") {
    if(!out||!capacity)return false;
    out[0]=0;
    const char *leaf=strrchr(base,'/');leaf=leaf?leaf+1:base;
    const char *dot=strchr(leaf,'.');
    size_t length=dot?(size_t)(dot-leaf):strlen(leaf);
    const char *ext=extension?extension:(dot?dot+1:"");
    if(*ext=='.')++ext;
    size_t elen=strlen(ext);
    auto legal=[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='_'||c=='-';};
    if(length<1||length>8||elen>3||(dot&&strchr(dot+1,'.'))) {errno=EINVAL;return false;}
    for(size_t i=0;i<length;i++)if(!legal((unsigned char)leaf[i])){errno=EINVAL;return false;}
    for(size_t i=0;i<elen;i++)if(!legal((unsigned char)ext[i])){errno=EINVAL;return false;}
    char shortname[13]={};
    for(size_t i=0;i<length;i++)shortname[i]=(char)toupper((unsigned char)leaf[i]);
    if(elen) {
        shortname[length++]='.';
        for(size_t i=0;i<elen;i++)shortname[length+i]=(char)toupper((unsigned char)ext[i]);
    }
    int count=snprintf(out,capacity,"%s/%s",root,shortname);
    if(count<0||(size_t)count>=capacity){out[0]=0;errno=ENAMETOOLONG;return false;}
    return true;
}
