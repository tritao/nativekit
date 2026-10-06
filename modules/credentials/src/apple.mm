#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include "backend.hpp"

#include <cstring>

namespace {

int32_t report_status(OSStatus status, const char *operation) {
    if (status == errSecItemNotFound) {
        nkc::set_error("credential was not found");
        return NK_ERROR_NOT_FOUND;
    }
    if (status == errSecAuthFailed || status == errSecInteractionNotAllowed ||
        status == errSecUserCanceled) {
        nkc::set_error("the operating-system credential store denied access");
        return NK_ERROR_PERMISSION_DENIED;
    }
    nkc::set_error(operation);
    return NK_ERROR_UNKNOWN;
}

NSDictionary *base_query(const nkc::Request &request) {
    NSString *service = [[NSString alloc] initWithBytes:request.service
                                                  length:std::strlen(request.service)
                                                encoding:NSUTF8StringEncoding];
    NSString *account = [[NSString alloc] initWithBytes:request.account
                                                  length:std::strlen(request.account)
                                                encoding:NSUTF8StringEncoding];
    if (!service || !account)
        return nil;
    return @{
        (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService : service,
        (__bridge id)kSecAttrAccount : account,
    };
}

} // namespace

namespace nkc {

int32_t set(const Request &request, const uint8_t *secret, uint32_t secret_size) {
    @autoreleasepool {
        NSDictionary *query = base_query(request);
        if (!query) {
            set_error("could not encode the credential name as UTF-8");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        NSData *data = [NSData dataWithBytes:secret length:secret_size];
        NSDictionary *values = @{
            (__bridge id)kSecValueData : data,
            (__bridge id)kSecAttrAccessible : (__bridge id)kSecAttrAccessibleAfterFirstUnlock,
        };
        OSStatus status = SecItemAdd((__bridge CFDictionaryRef)[query
            dictionaryByAddingEntriesFromDictionary:values], nullptr);
        if (status == errSecDuplicateItem)
            status = SecItemUpdate((__bridge CFDictionaryRef)query,
                                   (__bridge CFDictionaryRef)values);
        if (status != errSecSuccess)
            return report_status(status, "the operating-system credential store could not save the credential");
        return NK_OK;
    }
}

int32_t get(const Request &request, uint8_t *secret, uint32_t capacity,
            uint32_t *out_secret_size) {
    @autoreleasepool {
        NSDictionary *query = base_query(request);
        if (!query) {
            set_error("could not encode the credential name as UTF-8");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        NSMutableDictionary *read_query = [query mutableCopy];
        read_query[(__bridge id)kSecReturnData] = @YES;
        read_query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)read_query, &result);
        if (status != errSecSuccess)
            return report_status(status, "the operating-system credential store could not read the credential");
        NSData *data = CFBridgingRelease(result);
        const auto size = static_cast<uint32_t>(data.length);
        *out_secret_size = size;
        if (size > NK_CREDENTIALS_MAX_SECRET_BYTES) {
            set_error("stored credential exceeds the portable 2048-byte limit");
            return NK_ERROR_PAYLOAD_TOO_LARGE;
        }
        if (!secret || capacity < size) {
            set_error("credential output buffer is too small");
            return NK_ERROR_BUFFER_TOO_SMALL;
        }
        std::memcpy(secret, data.bytes, size);
        return NK_OK;
    }
}

int32_t erase(const Request &request) {
    @autoreleasepool {
        NSDictionary *query = base_query(request);
        if (!query) {
            set_error("could not encode the credential name as UTF-8");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
        if (status != errSecSuccess)
            return report_status(status, "the operating-system credential store could not delete the credential");
        return NK_OK;
    }
}

} // namespace nkc
