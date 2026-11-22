#include "libsecret_runtime.hpp"

#include <gio/gio.h>
#include <libsecret/secret.h>

#include <cstddef>
#include <cstring>
#include <type_traits>

/* The Linux backend loads libsecret at run time from its own declarations; they must match. */

namespace secret = nkc::secret;

static_assert(sizeof(secret::Boolean) == sizeof(gboolean));
static_assert(sizeof(secret::Quark) == sizeof(GQuark));
static_assert(std::is_same_v<secret::SignedSize, gssize>);

static_assert(sizeof(secret::Error) == sizeof(GError));
static_assert(offsetof(secret::Error, domain) == offsetof(GError, domain));
static_assert(offsetof(secret::Error, code) == offsetof(GError, code));
static_assert(offsetof(secret::Error, message) == offsetof(GError, message));

static_assert(sizeof(secret::SchemaAttribute) == sizeof(SecretSchemaAttribute));
static_assert(offsetof(secret::SchemaAttribute, type) == offsetof(SecretSchemaAttribute, type));
static_assert(sizeof(secret::Schema) == sizeof(SecretSchema));
static_assert(offsetof(secret::Schema, flags) == offsetof(SecretSchema, flags));
static_assert(offsetof(secret::Schema, attributes) == offsetof(SecretSchema, attributes));
static_assert(offsetof(secret::Schema, reserved) == offsetof(SecretSchema, reserved));
static_assert(offsetof(secret::Schema, reserved7) == offsetof(SecretSchema, reserved7));
static_assert(sizeof(SecretSchemaFlags) == sizeof(int));
static_assert(sizeof(SecretSchemaAttributeType) == sizeof(int));

static_assert(secret::schema_none == SECRET_SCHEMA_NONE);
static_assert(secret::schema_attribute_string == SECRET_SCHEMA_ATTRIBUTE_STRING);
static_assert(secret::error_is_locked == SECRET_ERROR_IS_LOCKED);
static_assert(secret::io_error_permission_denied == G_IO_ERROR_PERMISSION_DENIED);

int main() {
    /* The only value the compiler cannot compare. */
    return std::strcmp(secret::collection_default, SECRET_COLLECTION_DEFAULT) == 0 ? 0 : 1;
}
