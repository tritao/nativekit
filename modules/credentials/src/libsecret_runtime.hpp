#ifndef NATIVEKIT_CREDENTIALS_LIBSECRET_RUNTIME_HPP
#define NATIVEKIT_CREDENTIALS_LIBSECRET_RUNTIME_HPP

#include <cstddef>
#include <cstdint>

/*
 * The part of libsecret's (and GLib's) C ABI that the Linux backend uses,
 * declared here so the module builds without libsecret's development files and
 * loads libsecret-1.so.0 on first use. A system without it reports the
 * credential store as unsupported. The layouts and values match libsecret's
 * and GLib's headers; the libsecret_abi test compares them wherever those
 * headers are installed.
 */

namespace nkc::secret {

using Boolean = int;
using Quark = std::uint32_t;
using SignedSize = std::ptrdiff_t;

struct HashTable;
struct Cancellable;
struct Value;

/** GError */
struct Error {
    Quark domain;
    int code;
    char *message;
};

/** SecretSchemaAttribute */
struct SchemaAttribute {
    const char *name;
    int type;
};

/** SecretSchema */
struct Schema {
    const char *name;
    int flags;
    SchemaAttribute attributes[32];
    int reserved;
    void *reserved1;
    void *reserved2;
    void *reserved3;
    void *reserved4;
    void *reserved5;
    void *reserved6;
    void *reserved7;
};

constexpr int schema_none = 0;                        /* SECRET_SCHEMA_NONE */
constexpr int schema_attribute_string = 0;            /* SECRET_SCHEMA_ATTRIBUTE_STRING */
constexpr int error_is_locked = 2;                    /* SECRET_ERROR_IS_LOCKED */
constexpr int io_error_permission_denied = 14;        /* G_IO_ERROR_PERMISSION_DENIED */
constexpr const char *collection_default = "default"; /* SECRET_COLLECTION_DEFAULT */

/** Entry points resolved from libsecret-1.so.0 and the GLib it links. */
struct Api {
    Boolean (*secret_password_storev_binary_sync)(const Schema *, HashTable *, const char *,
                                                  const char *, Value *, Cancellable *, Error **);
    Value *(*secret_password_lookupv_binary_sync)(const Schema *, HashTable *, Cancellable *,
                                                  Error **);
    Boolean (*secret_password_clearv_sync)(const Schema *, HashTable *, Cancellable *, Error **);
    Value *(*secret_value_new)(const char *, SignedSize, const char *);
    const char *(*secret_value_get)(Value *, std::size_t *);
    void (*secret_value_unref)(void *);
    Quark (*secret_error_get_quark)();
    Quark (*g_io_error_quark)();
    HashTable *(*g_hash_table_new_full)(unsigned int (*)(const void *),
                                        Boolean (*)(const void *, const void *), void (*)(void *),
                                        void (*)(void *));
    Boolean (*g_hash_table_insert)(HashTable *, void *, void *);
    void (*g_hash_table_unref)(HashTable *);
    unsigned int (*g_str_hash)(const void *);
    Boolean (*g_str_equal)(const void *, const void *);
    char *(*g_strdup)(const char *);
    void (*g_free)(void *);
    void (*g_error_free)(Error *);
};

} // namespace nkc::secret

#endif
