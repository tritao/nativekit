#include "transport/secure_websocket.hpp"

#include "android/nativekit_android_internal.hpp"
#include "core/thread_name.hpp"

#include <jni.h>

#include <atomic>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <string_view>
#include <thread>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * The platform TLS stack is Java's: a javax.net.ssl socket verifies the server
 * against the system trust store (and the app's network security config), and
 * HttpsURLConnection's default host name verifier checks the certificate's
 * names. Two pump threads move bytes between the TLS socket's streams and a
 * local socket pair whose other end the transport polls like a TCP socket;
 * NativeKit's own WebSocket handshake and framing run on top, as for ws://.
 */

namespace nk::transport {
namespace {

constexpr jint pump_chunk = 16 * 1024;

/** Attaches the calling thread to the VM for the scope's lifetime. */
class JniScope final {
  public:
    JniScope() noexcept : env_(nk::backend::android_jni_attach(&attached_)) {}
    ~JniScope() { nk::backend::android_jni_detach(attached_); }
    JniScope(const JniScope &) = delete;
    JniScope &operator=(const JniScope &) = delete;

    JNIEnv *env() const noexcept { return env_; }

  private:
    bool attached_ = false;
    JNIEnv *env_ = nullptr;
};

bool contains(std::string_view value, std::string_view needle) noexcept {
    return value.find(needle) != std::string_view::npos;
}

/** Clears a pending Java exception and maps its class to a transport error; NK_OK if none. */
nk_result take_exception(JNIEnv *env) noexcept {
    if (!env->ExceptionCheck())
        return NK_OK;
    jthrowable throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    nk_result result = NK_TRANSPORT_ERROR_CONNECTION;
    if (!throwable)
        return result;
    jclass type = env->GetObjectClass(throwable);
    jclass class_class = env->FindClass("java/lang/Class");
    jmethodID get_name =
        class_class ? env->GetMethodID(class_class, "getName", "()Ljava/lang/String;") : nullptr;
    jstring name = get_name && type && !env->ExceptionCheck()
                       ? static_cast<jstring>(env->CallObjectMethod(type, get_name))
                       : nullptr;
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (name) {
        if (const char *chars = env->GetStringUTFChars(name, nullptr)) {
            const std::string_view text(chars);
            if (contains(text, "UnknownHost"))
                result = NK_TRANSPORT_ERROR_DNS;
            else if (contains(text, "Timeout"))
                result = NK_TRANSPORT_ERROR_TIMEOUT;
            env->ReleaseStringUTFChars(name, chars);
        }
        env->DeleteLocalRef(name);
    }
    if (class_class)
        env->DeleteLocalRef(class_class);
    if (type)
        env->DeleteLocalRef(type);
    env->DeleteLocalRef(throwable);
    return result;
}

void close_java_socket(JNIEnv *env, jobject socket) noexcept {
    if (!socket)
        return;
    jclass type = env->GetObjectClass(socket);
    jmethodID close = type ? env->GetMethodID(type, "close", "()V") : nullptr;
    if (close)
        env->CallVoidMethod(socket, close);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    if (type)
        env->DeleteLocalRef(type);
}

/** Opens and verifies the TLS socket; returns a local reference to it or null with *out_result. */
jobject connect_tls(JNIEnv *env, const SecureEndpoint &endpoint, nk_result *out_result) noexcept {
    *out_result = NK_TRANSPORT_ERROR_CONNECTION;
    jclass socket_class = env->FindClass("java/net/Socket");
    jclass address_class = env->FindClass("java/net/InetSocketAddress");
    jclass factory_class = env->FindClass("javax/net/ssl/SSLSocketFactory");
    jclass https_class = env->FindClass("javax/net/ssl/HttpsURLConnection");
    jclass verifier_class = env->FindClass("javax/net/ssl/HostnameVerifier");
    jclass ssl_socket_class = env->FindClass("javax/net/ssl/SSLSocket");
    jobject plain = nullptr;
    jobject secure = nullptr;
    jobject result = nullptr;
    jstring host = nullptr;
    jobject address = nullptr;
    jobject factory = nullptr;
    jobject session = nullptr;
    jobject verifier = nullptr;
    const jint timeout =
        endpoint.timeout_ms > 0x7fffffffu ? 0x7fffffff : static_cast<jint>(endpoint.timeout_ms);
    do {
        if (env->ExceptionCheck() || !socket_class || !address_class || !factory_class ||
            !https_class || !verifier_class || !ssl_socket_class)
            break;
        const jmethodID socket_new = env->GetMethodID(socket_class, "<init>", "()V");
        const jmethodID socket_connect =
            env->GetMethodID(socket_class, "connect", "(Ljava/net/SocketAddress;I)V");
        const jmethodID socket_timeout = env->GetMethodID(socket_class, "setSoTimeout", "(I)V");
        const jmethodID address_new =
            env->GetMethodID(address_class, "<init>", "(Ljava/lang/String;I)V");
        const jmethodID factory_default =
            env->GetStaticMethodID(factory_class, "getDefault", "()Ljavax/net/SocketFactory;");
        const jmethodID factory_layer =
            env->GetMethodID(factory_class, "createSocket",
                             "(Ljava/net/Socket;Ljava/lang/String;IZ)Ljava/net/Socket;");
        const jmethodID handshake = env->GetMethodID(ssl_socket_class, "startHandshake", "()V");
        const jmethodID get_session =
            env->GetMethodID(ssl_socket_class, "getSession", "()Ljavax/net/ssl/SSLSession;");
        const jmethodID default_verifier = env->GetStaticMethodID(
            https_class, "getDefaultHostnameVerifier", "()Ljavax/net/ssl/HostnameVerifier;");
        const jmethodID verify = env->GetMethodID(
            verifier_class, "verify", "(Ljava/lang/String;Ljavax/net/ssl/SSLSession;)Z");
        if (env->ExceptionCheck() || !socket_new || !socket_connect || !socket_timeout ||
            !address_new || !factory_default || !factory_layer || !handshake || !get_session ||
            !default_verifier || !verify)
            break;
        host = env->NewStringUTF(endpoint.host.c_str());
        if (!host || env->ExceptionCheck())
            break;
        plain = env->NewObject(socket_class, socket_new);
        address = plain ? env->NewObject(address_class, address_new, host,
                                         static_cast<jint>(endpoint.port))
                        : nullptr;
        if (!address || env->ExceptionCheck())
            break;
        env->CallVoidMethod(plain, socket_connect, address, timeout);
        if (env->ExceptionCheck())
            break;
        env->CallVoidMethod(plain, socket_timeout, timeout);
        if (env->ExceptionCheck())
            break;
        factory = env->CallStaticObjectMethod(factory_class, factory_default);
        if (!factory || env->ExceptionCheck())
            break;
        /* Layering on the connected socket with the host name sets SNI; closing the TLS socket
           closes the plain one. */
        secure = env->CallObjectMethod(factory, factory_layer, plain, host,
                                       static_cast<jint>(endpoint.port), JNI_TRUE);
        if (!secure || env->ExceptionCheck())
            break;
        env->CallVoidMethod(secure, handshake);
        if (env->ExceptionCheck())
            break;
        session = env->CallObjectMethod(secure, get_session);
        if (!session || env->ExceptionCheck())
            break;
        verifier = env->CallStaticObjectMethod(https_class, default_verifier);
        if (!verifier || env->ExceptionCheck())
            break;
        if (!env->CallBooleanMethod(verifier, verify, host, session) || env->ExceptionCheck())
            break;
        /* The handshake timeout no longer applies: reads wait until data or close. */
        env->CallVoidMethod(secure, socket_timeout, 0);
        if (env->ExceptionCheck())
            break;
        result = secure;
        secure = nullptr;
        *out_result = NK_OK;
    } while (false);
    if (*out_result != NK_OK) {
        const auto exception = take_exception(env);
        if (exception != NK_OK)
            *out_result = exception;
        close_java_socket(env, secure ? secure : plain);
    }
    for (jobject local :
         {static_cast<jobject>(socket_class), static_cast<jobject>(address_class),
          static_cast<jobject>(factory_class), static_cast<jobject>(https_class),
          static_cast<jobject>(verifier_class), static_cast<jobject>(ssl_socket_class), plain,
          secure, static_cast<jobject>(host), address, factory, session, verifier})
        if (local)
            env->DeleteLocalRef(local);
    return result;
}

class JavaTlsStream final : public TlsStream {
  public:
    JavaTlsStream(jobject socket, jobject input, jobject output, int local) noexcept
        : socket_(socket), input_(input), output_(output), local_(local) {}

    ~JavaTlsStream() override {
        /* Closing the TLS socket ends a pump blocked in Java; shutting the pair down ends one
           blocked on the transport's side. */
        JniScope scope;
        JNIEnv *env = scope.env();
        ::shutdown(local_, SHUT_RDWR);
        if (env)
            close_java_socket(env, socket_);
        if (writer_.joinable())
            writer_.join();
        if (reader_.joinable())
            reader_.join();
        ::close(local_);
        if (env) {
            env->DeleteGlobalRef(input_);
            env->DeleteGlobalRef(output_);
            env->DeleteGlobalRef(socket_);
        }
    }

    bool start() noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
        reader_ = std::thread([this] { read_tls(); });
        writer_ = std::thread([this] { write_tls(); });
        return true;
#else
        try {
            reader_ = std::thread([this] { read_tls(); });
            writer_ = std::thread([this] { write_tls(); });
            return true;
        } catch (...) {
            ::shutdown(local_, SHUT_RDWR);
            return false;
        }
#endif
    }

  private:
    bool send_local(const jbyte *bytes, std::size_t size) noexcept {
        std::size_t offset = 0;
        while (offset < size) {
            const auto sent = ::send(local_, bytes + offset, size - offset, MSG_NOSIGNAL);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
            } else if (sent < 0 && errno != EINTR) {
                return false;
            }
        }
        return true;
    }

    /** TLS input to the transport. */
    void read_tls() noexcept {
        nk::core::name_current_thread("nk.tls.read");
        JniScope scope;
        JNIEnv *env = scope.env();
        jclass type = env ? env->FindClass("java/io/InputStream") : nullptr;
        jmethodID read = type ? env->GetMethodID(type, "read", "([BII)I") : nullptr;
        jbyteArray buffer = read ? env->NewByteArray(pump_chunk) : nullptr;
        jbyte bytes[pump_chunk];
        while (buffer && !env->ExceptionCheck()) {
            const jint count = env->CallIntMethod(input_, read, buffer, 0, pump_chunk);
            if (env->ExceptionCheck() || count < 0)
                break;
            env->GetByteArrayRegion(buffer, 0, count, bytes);
            if (!send_local(bytes, static_cast<std::size_t>(count)))
                break;
        }
        if (env && env->ExceptionCheck())
            env->ExceptionClear();
        /* End of the TLS stream reaches the transport as end of its socket. */
        ::shutdown(local_, SHUT_WR);
        if (buffer)
            env->DeleteLocalRef(buffer);
        if (type)
            env->DeleteLocalRef(type);
    }

    /** Transport output to TLS. */
    void write_tls() noexcept {
        nk::core::name_current_thread("nk.tls.write");
        JniScope scope;
        JNIEnv *env = scope.env();
        jclass type = env ? env->FindClass("java/io/OutputStream") : nullptr;
        jmethodID write = type ? env->GetMethodID(type, "write", "([BII)V") : nullptr;
        jmethodID flush = type ? env->GetMethodID(type, "flush", "()V") : nullptr;
        jbyteArray buffer = write && flush ? env->NewByteArray(pump_chunk) : nullptr;
        jbyte bytes[pump_chunk];
        while (buffer && !env->ExceptionCheck()) {
            const auto received = ::recv(local_, bytes, sizeof(bytes), 0);
            if (received < 0 && errno == EINTR)
                continue;
            if (received <= 0)
                break;
            const auto count = static_cast<jint>(received);
            env->SetByteArrayRegion(buffer, 0, count, bytes);
            env->CallVoidMethod(output_, write, buffer, 0, count);
            if (!env->ExceptionCheck())
                env->CallVoidMethod(output_, flush);
        }
        if (env) {
            if (env->ExceptionCheck())
                env->ExceptionClear();
            /* Closing the TLS socket ends the reader's blocking read. */
            close_java_socket(env, socket_);
        }
        ::shutdown(local_, SHUT_RDWR);
        if (buffer)
            env->DeleteLocalRef(buffer);
        if (type)
            env->DeleteLocalRef(type);
    }

    jobject socket_;
    jobject input_;
    jobject output_;
    int local_;
    std::thread reader_;
    std::thread writer_;
};

bool make_local_pair(int (&pair)[2]) noexcept {
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
        return false;
    /* The transport's end polls; the pump threads block on theirs. */
    const int flags = fcntl(pair[0], F_GETFL, 0);
    if (flags < 0 || fcntl(pair[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        ::close(pair[0]);
        ::close(pair[1]);
        return false;
    }
    return true;
}

} // namespace

bool secure_client_available() noexcept {
    JniScope scope;
    return scope.env() != nullptr;
}

nk_result open_tls_stream(const SecureEndpoint &endpoint, const std::atomic<bool> &stopping,
                          std::unique_ptr<TlsStream> &out_stream, int &out_socket) noexcept {
    JniScope scope;
    JNIEnv *env = scope.env();
    if (!env)
        return NK_ERROR_UNSUPPORTED;
    nk_result result = NK_TRANSPORT_ERROR_CONNECTION;
    jobject socket = connect_tls(env, endpoint, &result);
    if (!socket)
        return stopping.load(std::memory_order_acquire) ? NK_TRANSPORT_ERROR_CANCELED : result;
    jclass type = env->GetObjectClass(socket);
    jmethodID get_input =
        type ? env->GetMethodID(type, "getInputStream", "()Ljava/io/InputStream;") : nullptr;
    jmethodID get_output =
        type ? env->GetMethodID(type, "getOutputStream", "()Ljava/io/OutputStream;") : nullptr;
    jobject input = get_input ? env->CallObjectMethod(socket, get_input) : nullptr;
    jobject output =
        get_output && !env->ExceptionCheck() ? env->CallObjectMethod(socket, get_output) : nullptr;
    jobject socket_global = nullptr;
    jobject input_global = nullptr;
    jobject output_global = nullptr;
    int pair[2]{-1, -1};
    bool ready = !env->ExceptionCheck() && input && output;
    if (ready) {
        socket_global = env->NewGlobalRef(socket);
        input_global = env->NewGlobalRef(input);
        output_global = env->NewGlobalRef(output);
        ready = socket_global && input_global && output_global && make_local_pair(pair);
    }
    if (env->ExceptionCheck())
        env->ExceptionClear();
    for (jobject local : {static_cast<jobject>(type), input, output})
        if (local)
            env->DeleteLocalRef(local);
    std::unique_ptr<JavaTlsStream> stream;
    if (ready)
        stream.reset(new (std::nothrow)
                         JavaTlsStream(socket_global, input_global, output_global, pair[1]));
    if (!stream) {
        if (ready) {
            ::close(pair[0]);
            ::close(pair[1]);
        }
        close_java_socket(env, socket);
        for (jobject global : {socket_global, input_global, output_global})
            if (global)
                env->DeleteGlobalRef(global);
        env->DeleteLocalRef(socket);
        return ready ? NK_ERROR_OUT_OF_MEMORY : NK_TRANSPORT_ERROR_CONNECTION;
    }
    env->DeleteLocalRef(socket);
    if (!stream->start()) {
        ::close(pair[0]);
        return NK_ERROR_OUT_OF_MEMORY;
    }
    out_stream = std::move(stream);
    out_socket = pair[0];
    return NK_OK;
}

} // namespace nk::transport
