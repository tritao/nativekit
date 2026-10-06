#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include "backend.hpp"

#include <cstring>
#include <string>

namespace {

bool widen(const char *utf8, std::wstring &wide) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
    if (count <= 0)
        return false;
    wide.resize(static_cast<size_t>(count));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide.data(), count) <= 0)
        return false;
    wide.pop_back();
    return true;
}

bool target_name(const nkc::Request &request, std::wstring &target) {
    std::wstring service;
    std::wstring account;
    if (!widen(request.service, service) || !widen(request.account, account))
        return false;
    target = L"NativeKit/v1/" + std::to_wstring(service.size()) + L":" + service + account;
    return true;
}

int32_t status_error(DWORD status, const char *operation) {
    if (status == ERROR_NOT_FOUND) {
        nkc::set_error("credential was not found");
        return NK_ERROR_NOT_FOUND;
    }
    if (status == ERROR_ACCESS_DENIED) {
        nkc::set_error("the operating-system credential store denied access");
        return NK_ERROR_PERMISSION_DENIED;
    }
    nkc::set_error(operation);
    return NK_ERROR_UNKNOWN;
}

} // namespace

namespace nkc {

int32_t set(const Request &request, const uint8_t *secret, uint32_t secret_size) {
    std::wstring target;
    if (!target_name(request, target)) {
        set_error("could not encode the credential name as UTF-16");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = secret_size;
    credential.CredentialBlob = const_cast<LPBYTE>(secret);
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0))
        return status_error(GetLastError(), "the operating-system credential store could not save the credential");
    return NK_OK;
}

int32_t get(const Request &request, uint8_t *secret, uint32_t capacity,
            uint32_t *out_secret_size) {
    std::wstring target;
    if (!target_name(request, target)) {
        set_error("could not encode the credential name as UTF-16");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return status_error(GetLastError(), "the operating-system credential store could not read the credential");
    const auto size = credential->CredentialBlobSize;
    *out_secret_size = size;
    if (size > NK_CREDENTIALS_MAX_SECRET_BYTES) {
        CredFree(credential);
        set_error("stored credential exceeds the portable 2048-byte limit");
        return NK_ERROR_PAYLOAD_TOO_LARGE;
    }
    if (!secret || capacity < size) {
        CredFree(credential);
        set_error("credential output buffer is too small");
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(secret, credential->CredentialBlob, size);
    CredFree(credential);
    return NK_OK;
}

int32_t erase(const Request &request) {
    std::wstring target;
    if (!target_name(request, target)) {
        set_error("could not encode the credential name as UTF-16");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
        return status_error(GetLastError(), "the operating-system credential store could not delete the credential");
    return NK_OK;
}

} // namespace nkc
