#include "remote_server.h"

#include "cadence/engine.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

namespace cadence {

namespace {

constexpr int kMaxMessageBytes = 2 * 1024 * 1024;
constexpr int kMaxOutboxMessages = 256;
constexpr int kMaxAuthFailures = 5;
constexpr int kTickMs = 20;

// ---------------------------------------------------------------------------
// SHA-1 (RFC 3174). JUCE ships SHA-256 but the WebSocket handshake needs SHA-1.
// ---------------------------------------------------------------------------

struct Sha1
{
    std::uint32_t h[5] { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu,
                         0x10325476u, 0xC3D2E1F0u };
    std::uint64_t bitLength = 0;
    std::uint8_t block[64] {};
    std::size_t blockSize = 0;

    static std::uint32_t rotl(std::uint32_t v, int n)
    {
        return (v << n) | (v >> (32 - n));
    }

    void processBlock(const std::uint8_t* p)
    {
        std::uint32_t w[80] {};
        for (int i = 0; i < 16; ++i)
            w[i] = ((std::uint32_t) p[i * 4] << 24)
                   | ((std::uint32_t) p[i * 4 + 1] << 16)
                   | ((std::uint32_t) p[i * 4 + 2] << 8)
                   | (std::uint32_t) p[i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f = 0, k = 0;
            if (i < 20)      { f = (b & c) | ((~b) & d);       k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                  k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                  k = 0xCA62C1D6u; }

            const auto temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const void* data, std::size_t size)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bitLength += (std::uint64_t) size * 8u;
        while (size > 0)
        {
            const auto take = std::min<std::size_t>(size, 64 - blockSize);
            std::memcpy(block + blockSize, p, take);
            blockSize += take;
            p += take;
            size -= take;
            if (blockSize == 64)
            {
                processBlock(block);
                blockSize = 0;
            }
        }
    }

    std::array<std::uint8_t, 20> finish()
    {
        const std::uint64_t bits = bitLength;
        const std::uint8_t pad = 0x80;
        update(&pad, 1);
        const std::uint8_t zero = 0;
        while (blockSize != 56)
            update(&zero, 1);
        std::uint8_t len[8] {};
        for (int i = 0; i < 8; ++i)
            len[7 - i] = (std::uint8_t) (bits >> (i * 8));
        update(len, 8);

        std::array<std::uint8_t, 20> out {};
        for (int i = 0; i < 5; ++i)
        {
            out[(std::size_t) i * 4] = (std::uint8_t) (h[i] >> 24);
            out[(std::size_t) i * 4 + 1] = (std::uint8_t) (h[i] >> 16);
            out[(std::size_t) i * 4 + 2] = (std::uint8_t) (h[i] >> 8);
            out[(std::size_t) i * 4 + 3] = (std::uint8_t) h[i];
        }
        return out;
    }
};

std::array<std::uint8_t, 20> sha1(const void* data, std::size_t size)
{
    Sha1 sha;
    sha.update(data, size);
    return sha.finish();
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

juce::var makeObject()
{
    return juce::var(new juce::DynamicObject());
}

juce::var getProp(const juce::var& value, const char* key)
{
    if (auto* object = value.getDynamicObject())
        return object->getProperty(juce::Identifier(key));
    return {};
}

bool hasProp(const juce::var& value, const char* key)
{
    return ! getProp(value, key).isVoid();
}

bool getBool(const juce::var& value, const char* key, bool fallback = false)
{
    const auto v = getProp(value, key);
    if (v.isVoid())
        return fallback;
    if (v.isBool())
        return (bool) v;
    if (v.isInt() || v.isInt64() || v.isDouble())
        return (double) v != 0.0;
    return fallback;
}

double getDouble(const juce::var& value, const char* key, double fallback = 0.0)
{
    const auto v = getProp(value, key);
    if (v.isVoid())
        return fallback;
    if (v.isDouble() || v.isInt() || v.isInt64() || v.isBool())
        return (double) v;
    return fallback;
}

int getInt(const juce::var& value, const char* key, int fallback = 0)
{
    const auto v = getProp(value, key);
    if (v.isVoid())
        return fallback;
    if (v.isInt() || v.isInt64() || v.isDouble() || v.isBool())
        return (int) v;
    return fallback;
}

juce::int64 getInt64(const juce::var& value, const char* key,
                     juce::int64 fallback = -1)
{
    const auto v = getProp(value, key);
    if (v.isVoid())
        return fallback;
    if (v.isInt() || v.isInt64() || v.isDouble())
        return (juce::int64) v;
    return fallback;
}

juce::String getString(const juce::var& value, const char* key,
                       const juce::String& fallback = {})
{
    const auto v = getProp(value, key);
    if (v.isVoid())
        return fallback;
    return v.toString();
}

void put(juce::var& target, const char* key, const juce::var& value)
{
    if (auto* object = target.getDynamicObject())
        object->setProperty(juce::Identifier(key), value);
}

juce::var okResponse(const juce::var& id, const juce::var& result)
{
    auto response = makeObject();
    put(response, "v", 1);
    if (! id.isVoid())
        put(response, "id", id);
    put(response, "ok", true);
    put(response, "result", result);
    return response;
}

juce::var errorResponse(const juce::var& id, const juce::String& code,
                        const juce::String& message)
{
    auto response = makeObject();
    put(response, "v", 1);
    if (! id.isVoid())
        put(response, "id", id);
    put(response, "ok", false);
    auto error = makeObject();
    put(error, "code", code);
    put(error, "message", message);
    put(response, "error", error);
    return response;
}

juce::var eventMessage(const juce::String& name, const juce::var& data)
{
    auto message = makeObject();
    put(message, "v", 1);
    put(message, "event", name);
    put(message, "data", data);
    return message;
}

juce::var curveToVar(const AutomationCurvePtr& curve)
{
    juce::Array<juce::var> points;
    if (curve != nullptr)
        for (const auto& point : curve->points)
        {
            auto item = makeObject();
            put(item, "t", point.timeSeconds);
            put(item, "v", point.value);
            points.add(item);
        }
    return juce::var(points);
}

std::vector<AutomationPoint> pointsFromRequest(const juce::var& request)
{
    std::vector<AutomationPoint> points;
    if (auto* array = getProp(request, "points").getArray())
        for (const auto& item : *array)
        {
            AutomationPoint point;
            point.timeSeconds = getDouble(item, "t", getDouble(item, "time", 0.0));
            point.value = getDouble(item, "v", getDouble(item, "value", 0.0));
            points.push_back(point);
        }
    return points;
}

// ---------------------------------------------------------------------------
// WebSocket framing
// ---------------------------------------------------------------------------

std::string encodeFrame(std::uint8_t opcode, const void* payload,
                        std::size_t size)
{
    std::string frame;
    frame.push_back((char) (0x80 | opcode));
    if (size < 126)
    {
        frame.push_back((char) size);
    }
    else if (size <= 0xFFFF)
    {
        frame.push_back((char) 126);
        frame.push_back((char) ((size >> 8) & 0xFF));
        frame.push_back((char) (size & 0xFF));
    }
    else
    {
        frame.push_back((char) 127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((char) ((size >> (i * 8)) & 0xFF));
    }
    if (size > 0 && payload != nullptr)
        frame.append(static_cast<const char*>(payload), size);
    return frame;
}

juce::String lowerCase(juce::String value)
{
    return value.trim().toLowerCase();
}

} // namespace

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

struct RemoteServer::Client : public std::enable_shared_from_this<Client>
{
    int id = 0;
    std::unique_ptr<juce::StreamingSocket> socket;
    std::thread thread;

    std::atomic<bool> closing { false };
    std::atomic<bool> authenticated { false };
    std::atomic<bool> wantsMeters { true };
    std::atomic<int> metersHz { 20 };
    std::atomic<std::int64_t> lastMetersMs { 0 };
    std::atomic<std::int64_t> lastSeenMs { 0 };
    int authFailures = 0; // client thread only
    juce::String name;

    // WebSocket fragmentation state (client thread only).
    std::uint8_t fragmentOpcode = 0;
    std::string fragmentData;

    std::mutex outMutex;
    std::deque<std::string> outbox;
    std::atomic<bool> outOverflow { false };

    bool writeAll(const char* data, std::size_t size)
    {
        if (socket == nullptr)
            return false;
        std::size_t offset = 0;
        while (offset < size)
        {
            if (closing.load(std::memory_order_relaxed))
                return false;
            const int written = socket->write(
                data + offset, (int) std::min<std::size_t>(size - offset,
                                                          1024 * 1024));
            if (written <= 0)
                return false;
            offset += (std::size_t) written;
        }
        return true;
    }

    void sendRaw(std::string frame)
    {
        std::scoped_lock lock(outMutex);
        if (outbox.size() >= (std::size_t) kMaxOutboxMessages)
        {
            outOverflow.store(true, std::memory_order_release);
            return;
        }
        outbox.push_back(std::move(frame));
    }

    void sendFrame(std::uint8_t opcode, const void* payload, std::size_t size)
    {
        sendRaw(encodeFrame(opcode, payload, size));
    }

    void sendJson(const juce::var& message)
    {
        const auto text = juce::JSON::toString(message, false).toStdString();
        sendRaw(encodeFrame(0x1, text.data(), text.size()));
    }

    void sendText(const juce::String& text)
    {
        const auto utf8 = text.toStdString();
        sendRaw(encodeFrame(0x1, utf8.data(), utf8.size()));
    }

    bool drainOutbox()
    {
        std::deque<std::string> pending;
        {
            std::scoped_lock lock(outMutex);
            if (outbox.empty())
                return true;
            pending.swap(outbox);
        }
        for (const auto& frame : pending)
            if (! writeAll(frame.data(), frame.size()))
                return false;
        return true;
    }

    bool processIncoming(RemoteServer& server, std::vector<std::uint8_t>& buffer)
    {
        std::size_t offset = 0;
        while (buffer.size() - offset >= 2)
        {
            const auto* header = buffer.data() + offset;
            const bool fin = (header[0] & 0x80) != 0;
            const auto opcode = (std::uint8_t) (header[0] & 0x0F);
            const bool masked = (header[1] & 0x80) != 0;
            std::uint64_t length = (std::uint64_t) (header[1] & 0x7F);
            std::size_t headerSize = 2;

            if (length == 126)
            {
                if (buffer.size() - offset < 4)
                    break;
                length = ((std::uint64_t) header[2] << 8) | header[3];
                headerSize = 4;
            }
            else if (length == 127)
            {
                if (buffer.size() - offset < 10)
                    break;
                length = 0;
                for (int i = 0; i < 8; ++i)
                    length = (length << 8) | header[2 + (std::size_t) i];
                headerSize = 10;
            }

            if (! masked)
            {
                // Clients MUST mask every frame (RFC 6455 §5.1).
                sendFrame(0x8, "\x03\xEA", 2); // 1002 protocol error
                return false;
            }
            if (length > (std::uint64_t) kMaxMessageBytes)
            {
                sendFrame(0x8, "\x03\xF1", 2); // 1009 message too big
                return false;
            }
            if (buffer.size() - offset < headerSize + 4 + (std::size_t) length)
                break;

            const auto* mask = header + headerSize;
            const auto* payload = mask + 4;
            std::vector<std::uint8_t> data((std::size_t) length);
            for (std::size_t i = 0; i < data.size(); ++i)
                data[i] = (std::uint8_t) (payload[i] ^ mask[i % 4]);

            offset += headerSize + 4 + (std::size_t) length;

            if (opcode >= 0x8)
            {
                if (! fin || length > 125)
                    return false;
                if (opcode == 0x8) // close
                {
                    sendFrame(0x8, data.data(), data.size());
                    return false;
                }
                if (opcode == 0x9) // ping
                    sendFrame(0xA, data.data(), data.size());
                continue;
            }

            if (opcode == 0x0) // continuation
            {
                if (fragmentOpcode == 0)
                    return false;
                fragmentData.append((const char*) data.data(), data.size());
                if (fragmentData.size() > (std::size_t) kMaxMessageBytes)
                    return false;
                if (fin)
                {
                    const bool wasText = fragmentOpcode == 0x1;
                    const auto message = fragmentData;
                    fragmentData.clear();
                    fragmentOpcode = 0;
                    if (wasText && ! handleText(server, message))
                        return false;
                }
            }
            else if (opcode == 0x1) // text
            {
                if (fragmentOpcode != 0)
                    return false;
                const std::string text((const char*) data.data(), data.size());
                if (fin)
                {
                    if (! handleText(server, text))
                        return false;
                }
                else
                {
                    fragmentOpcode = opcode;
                    fragmentData = text;
                }
            }
            else if (opcode == 0x2) // binary
            {
                // The protocol is JSON text only. Accumulate and discard so a
                // fragmented binary frame cannot desynchronise the stream.
                if (! fin && fragmentOpcode == 0)
                {
                    fragmentOpcode = opcode;
                    fragmentData.assign((const char*) data.data(), data.size());
                }
                else if (fin)
                {
                    fragmentOpcode = 0;
                    fragmentData.clear();
                }
            }
            else
            {
                return false;
            }
        }

        if (offset > 0)
            buffer.erase(buffer.begin(), buffer.begin() + (std::ptrdiff_t) offset);
        return true;
    }

    bool handleText(RemoteServer& server, const std::string& text)
    {
        const auto parsed = juce::JSON::parse(
            juce::String::fromUTF8(text.data(), (int) text.size()));
        if (parsed.isVoid())
        {
            sendJson(errorResponse({}, "bad_json", "Message is not valid JSON"));
            return true;
        }

        const auto id = getProp(parsed, "id");
        const auto command = getString(parsed, "cmd");
        if (command.isEmpty())
        {
            sendJson(errorResponse(id, "bad_request", "Missing cmd"));
            return true;
        }

        if (command == "hello")
        {
            const auto supplied = getString(parsed, "token");
            if (supplied != server.token_)
            {
                ++authFailures;
                sendJson(errorResponse(id, "auth_failed",
                                       "Invalid pairing code"));
                if (authFailures >= kMaxAuthFailures)
                    closing.store(true, std::memory_order_release);
                return true;
            }
            name = getString(getProp(parsed, "client"), "name", name);
            authFailures = 0;
            authenticated.store(true, std::memory_order_release);
            sendJson(okResponse(id, server.buildWelcome(shared_from_this())));
            return true;
        }

        if (command == "ping")
        {
            auto result = makeObject();
            put(result, "pong", true);
            put(result, "time", (double) juce::Time::getMillisecondCounter());
            sendJson(okResponse(id, result));
            return true;
        }

        if (! authenticated.load(std::memory_order_acquire))
        {
            sendJson(errorResponse(id, "auth_required",
                                   "Send hello with the pairing code first"));
            return true;
        }

        if (command == "state.get")
        {
            auto result = makeObject();
            put(result, "state", server.buildState());
            put(result, "transport", server.buildTransport());
            sendJson(okResponse(id, result));
            return true;
        }

        if (command == "transport.get")
        {
            sendJson(okResponse(id, server.buildTransport()));
            return true;
        }

        if (command == "subscribe")
        {
            if (hasProp(parsed, "meters"))
                wantsMeters.store(getBool(parsed, "meters", true),
                                  std::memory_order_release);
            if (hasProp(parsed, "metersHz"))
                metersHz.store(juce::jlimit(1, 30, getInt(parsed, "metersHz", 20)),
                               std::memory_order_release);
            auto result = makeObject();
            put(result, "meters", wantsMeters.load());
            put(result, "metersHz", metersHz.load());
            sendJson(okResponse(id, result));
            return true;
        }

        // Everything else mutates the document or asks the plugin host: it
        // must run on the engine's message thread.
        server.enqueueCommand(shared_from_this(), parsed);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

struct RemoteServer::Command
{
    std::weak_ptr<Client> client;
    juce::var request;
};

// ---------------------------------------------------------------------------
// RemoteServer
// ---------------------------------------------------------------------------

RemoteServer::RemoteServer(Engine& engine)
    : engine_(engine)
{
}

RemoteServer::~RemoteServer()
{
    stop();
}

juce::String RemoteServer::lastError() const
{
    const std::scoped_lock lock(errorMutex_);
    return lastError_;
}

void RemoteServer::setLastError(const juce::String& message)
{
    const std::scoped_lock lock(errorMutex_);
    lastError_ = message;
}

juce::Result RemoteServer::start(int port, const juce::String& token)
{
    stop();

    const auto requestedToken = token.trim();
    token_ = requestedToken.isNotEmpty()
                 ? requestedToken
                 : juce::String(juce::Random::getSystemRandom().nextInt(1000000))
                       .paddedLeft('0', 6);
    if (token_.length() > 64)
        token_ = token_.substring(0, 64);
    setLastError({});

    stopping_.store(false, std::memory_order_release);
    alive_.store(true, std::memory_order_release);
    port_.store(0, std::memory_order_release);

    if (! listener_.createListener(port, "0.0.0.0"))
    {
        const auto message = "No se pudo abrir el puerto " + juce::String(port)
                             + " para el control remoto";
        setLastError(message);
        alive_.store(false, std::memory_order_release);
        return juce::Result::fail(message);
    }

    const int boundPort = listener_.getBoundPort();
    if (boundPort <= 0)
    {
        listener_.close();
        const auto message = "El sistema no asignó un puerto de control remoto";
        setLastError(message);
        alive_.store(false, std::memory_order_release);
        return juce::Result::fail(message);
    }

    port_.store(boundPort, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    lastProjectRevision_ = engine_.projectRevision();
    lastProjectSentMs_ = 0;
    lastTransportSentMs_ = 0;
    lastMetersSentMs_ = 0;
    lastPlaying_ = false;
    lastRecording_ = false;
    lastPosition_ = -1.0;

    acceptThread_ = std::thread([this] { acceptLoop(); });
    tickThread_ = std::thread([this] { tickLoop(); });
    return juce::Result::ok();
}

void RemoteServer::stop()
{
    stopping_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    alive_.store(false, std::memory_order_release);

    listener_.close();

    if (acceptThread_.joinable()
        && acceptThread_.get_id() != std::this_thread::get_id())
        acceptThread_.join();
    if (tickThread_.joinable()
        && tickThread_.get_id() != std::this_thread::get_id())
        tickThread_.join();

    std::vector<std::shared_ptr<Client>> clients;
    {
        const std::scoped_lock lock(clientsMutex_);
        clients.swap(clients_);
    }
    for (auto& client : clients)
    {
        client->closing.store(true, std::memory_order_release);
        if (client->socket != nullptr)
            client->socket->close();
    }
    for (auto& client : clients)
        if (client->thread.joinable()
            && client->thread.get_id() != std::this_thread::get_id())
            client->thread.join();

    clientCount_.store(0, std::memory_order_release);
    {
        const std::scoped_lock lock(commandsMutex_);
        commands_.clear();
        pumpPosted_ = false;
    }
}

void RemoteServer::acceptLoop()
{
    while (! stopping_.load(std::memory_order_acquire))
    {
        if (listener_.waitUntilReady(true, 200) != 1)
            continue;

        std::unique_ptr<juce::StreamingSocket> socket(
            listener_.waitForNextConnection());
        if (socket == nullptr)
            continue;

        auto client = std::make_shared<Client>();
        client->id = nextClientId_++;
        client->socket = std::move(socket);
        client->lastSeenMs.store((std::int64_t) juce::Time::getMillisecondCounter(),
                                 std::memory_order_release);

        {
            const std::scoped_lock lock(clientsMutex_);
            clients_.push_back(client);
        }
        clientCount_.fetch_add(1, std::memory_order_acq_rel);

        client->thread = std::thread([this, client] { handleClient(client); });
    }
}

void RemoteServer::handleClient(const std::shared_ptr<Client>& client)
{
    if (! performHandshake(*client))
    {
        client->closing.store(true, std::memory_order_release);
        if (client->socket != nullptr)
            client->socket->close();
        return;
    }

    std::vector<std::uint8_t> pending;
    auto lastPing = (std::int64_t) juce::Time::getMillisecondCounter();

    while (! client->closing.load(std::memory_order_acquire)
           && ! stopping_.load(std::memory_order_acquire))
    {
        if (client->outOverflow.load(std::memory_order_acquire))
            break;
        if (! client->drainOutbox())
            break;

        const int ready = client->socket->waitUntilReady(true, 25);
        if (ready < 0)
            break;
        if (ready == 1)
        {
            std::uint8_t buffer[8192];
            const int read = client->socket->read(buffer, (int) sizeof(buffer), false);
            if (read <= 0)
                break;
            pending.insert(pending.end(), buffer, buffer + read);
            client->lastSeenMs.store(
                (std::int64_t) juce::Time::getMillisecondCounter(),
                std::memory_order_release);
            if (! client->processIncoming(*this, pending))
                break;
        }

        const auto now = (std::int64_t) juce::Time::getMillisecondCounter();
        if (now - lastPing > 20000)
        {
            client->sendFrame(0x9, nullptr, 0);
            lastPing = now;
        }
        if (now - client->lastSeenMs.load(std::memory_order_acquire) > 120000)
            break;
    }

    client->closing.store(true, std::memory_order_release);
    if (client->socket != nullptr)
        client->socket->close();
}

bool RemoteServer::performHandshake(Client& client)
{
    if (client.socket == nullptr)
        return false;

    std::string request;
    const auto deadline = juce::Time::getMillisecondCounter() + 5000;
    while (request.find("\r\n\r\n") == std::string::npos)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        if (client.socket->waitUntilReady(true, 100) != 1)
            continue;
        char buffer[1024];
        const int read = client.socket->read(buffer, (int) sizeof(buffer), false);
        if (read <= 0)
            return false;
        request.append(buffer, (std::size_t) read);
        if (request.size() > 16384)
            return false;
    }

    std::istringstream stream(request);
    std::string requestLine;
    std::getline(stream, requestLine);
    if (! requestLine.empty() && requestLine.back() == '\r')
        requestLine.pop_back();

    std::string method = "GET";
    {
        const auto firstSpace = requestLine.find(' ');
        if (firstSpace != std::string::npos)
            method = requestLine.substr(0, firstSpace);
    }

    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(stream, line))
    {
        if (! line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        auto key = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        std::transform(key.begin(), key.end(), key.begin(),
                       [] (unsigned char c) { return (char) std::tolower(c); });
        const auto first = value.find_first_not_of(" \t");
        value = first == std::string::npos ? std::string() : value.substr(first);
        headers[key] = value;
    }

    const auto upgrade = headers.find("upgrade");
    const auto keyIt = headers.find("sec-websocket-key");
    const bool wantsWebSocket = upgrade != headers.end()
                                && lowerCase(upgrade->second.c_str())
                                       .contains("websocket")
                                && keyIt != headers.end();

    if (wantsWebSocket)
    {
        const std::string magic = keyIt->second
                                  + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        const auto hash = sha1(magic.data(), magic.size());
        const auto accept = juce::Base64::toBase64(hash.data(), hash.size())
                                .toStdString();
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        return client.writeAll(response.data(), response.size());
    }

    // Plain HTTP: a tiny discovery/health document. No project data and no
    // control surface, so it is safe to answer without the pairing code.
    auto info = makeObject();
    put(info, "app", "EZMusicProduction");
    put(info, "protocol", kProtocolVersion);
    put(info, "transport", "websocket+json");
    put(info, "port", port_.load(std::memory_order_acquire));
    put(info, "requiresToken", true);
    const auto body = juce::JSON::toString(info, false).toStdString();

    std::string response;
    if (method == "OPTIONS")
    {
        response = "HTTP/1.1 204 No Content\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Access-Control-Allow-Headers: content-type\r\n"
                   "Connection: close\r\n\r\n";
    }
    else
    {
        response = "HTTP/1.1 200 OK\r\n"
                   "Content-Type: application/json; charset=utf-8\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n"
                   "Cache-Control: no-store\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Connection: close\r\n\r\n" + body;
    }
    client.writeAll(response.data(), response.size());
    return false;
}

void RemoteServer::enqueueCommand(const std::shared_ptr<Client>& client,
                                  const juce::var& request)
{
    if (! alive_.load(std::memory_order_acquire))
        return;

    auto command = std::make_shared<Command>();
    command->client = client;
    command->request = request;

    bool post = false;
    {
        const std::scoped_lock lock(commandsMutex_);
        commands_.push_back(std::move(command));
        if (! pumpPosted_)
        {
            pumpPosted_ = true;
            post = true;
        }
    }

    if (post)
    {
        auto self = self_.lock();
        if (self != nullptr)
        {
            juce::MessageManager::callAsync([self] {
                if (self->alive_.load(std::memory_order_acquire))
                    self->drainCommands();
            });
        }
    }
}

int RemoteServer::pumpCommands()
{
    auto* messageManager = juce::MessageManager::getInstance();
    if (messageManager == nullptr || ! messageManager->isThisTheMessageThread())
        return 0;
    return drainCommands();
}

int RemoteServer::drainCommands()
{
    if (! alive_.load(std::memory_order_acquire))
        return 0;

    std::deque<std::shared_ptr<Command>> pending;
    {
        const std::scoped_lock lock(commandsMutex_);
        pending.swap(commands_);
        pumpPosted_ = false;
    }

    int count = 0;
    for (const auto& command : pending)
    {
        if (! alive_.load(std::memory_order_acquire))
            break;
        executeCommand(*command);
        ++count;
    }
    return count;
}

void RemoteServer::executeCommand(const Command& command)
{
    auto client = command.client.lock();
    if (client == nullptr || client->closing.load(std::memory_order_acquire))
        return;

    const auto& request = command.request;
    const auto id = getProp(request, "id");
    const auto cmd = getString(request, "cmd");

    auto reply = [&] (const juce::var& result) {
        client->sendJson(okResponse(id, result));
    };
    auto fail = [&] (const juce::String& code, const juce::String& message) {
        client->sendJson(errorResponse(id, code, message));
    };
    auto engineError = [&] {
        return juce::String::fromUTF8(engine_.lastErrorUtf8());
    };
    auto revisionResult = [&] {
        auto result = makeObject();
        put(result, "revision", (juce::int64) engine_.projectRevision());
        return result;
    };
    auto ensureAudio = [&] () -> bool {
        if (engine_.isInitialised())
            return true;
        if (engine_.initialise().failed())
        {
            fail("audio_device", engineError());
            return false;
        }
        return true;
    };

    if (cmd == "transport.play")
    {
        if (! ensureAudio())
            return;
        engine_.play();
        auto result = makeObject();
        put(result, "playing", true);
        reply(result);
        return;
    }
    if (cmd == "transport.pause")
    {
        engine_.pause();
        auto result = makeObject();
        put(result, "playing", false);
        reply(result);
        return;
    }
    if (cmd == "transport.stop")
    {
        engine_.stop();
        auto result = makeObject();
        put(result, "playing", false);
        put(result, "position", 0.0);
        reply(result);
        return;
    }
    if (cmd == "transport.seek")
    {
        const double seconds = std::max(
            0.0, getDouble(request, "seconds",
                           getDouble(request, "position", 0.0)));
        engine_.seek(seconds);
        auto result = makeObject();
        put(result, "position", seconds);
        reply(result);
        return;
    }
    if (cmd == "transport.record.start")
    {
        if (! ensureAudio())
            return;
        const auto result = engine_.startRecord();
        if (result.failed())
        {
            fail("record_failed", engineError());
            return;
        }
        auto payload = makeObject();
        put(payload, "recording", true);
        put(payload, "recordStart", engine_.recordStartSeconds());
        reply(payload);
        return;
    }
    if (cmd == "transport.record.stop")
    {
        engine_.stopRecord();
        auto payload = makeObject();
        put(payload, "recording", false);
        put(payload, "position", engine_.positionSeconds());
        reply(payload);
        return;
    }
    if (cmd == "transport.metronome")
    {
        engine_.setMetronome(getBool(request, "enabled", true));
        auto payload = makeObject();
        put(payload, "metronome", engine_.metronomeEnabled());
        reply(payload);
        return;
    }
    if (cmd == "transport.tempo")
    {
        const auto result = engine_.setTempo(getDouble(request, "bpm", 120.0));
        if (result.failed())
        {
            fail("invalid_argument", engineError());
            return;
        }
        auto payload = makeObject();
        put(payload, "tempoBpm", engine_.tempoBpm());
        put(payload, "revision", (juce::int64) engine_.projectRevision());
        reply(payload);
        return;
    }
    if (cmd == "transport.timeSig")
    {
        const auto result = engine_.setTimeSignature(
            getInt(request, "numerator", 4), getInt(request, "denominator", 4));
        if (result.failed())
        {
            fail("invalid_argument", engineError());
            return;
        }
        auto payload = makeObject();
        put(payload, "numerator", engine_.timeSigNumerator());
        put(payload, "denominator", engine_.timeSigDenominator());
        put(payload, "revision", (juce::int64) engine_.projectRevision());
        reply(payload);
        return;
    }
    if (cmd == "project.undo")
    {
        const bool changed = engine_.undo();
        auto payload = revisionResult();
        put(payload, "changed", changed);
        reply(payload);
        return;
    }
    if (cmd == "project.redo")
    {
        const bool changed = engine_.redo();
        auto payload = revisionResult();
        put(payload, "changed", changed);
        reply(payload);
        return;
    }
    if (cmd == "mixer.setTrack")
    {
        const auto trackId = getInt64(request, "trackId", -1);
        if (trackId < 0)
        {
            fail("invalid_argument", "trackId is required");
            return;
        }
        TrackMixUpdate update;
        if (hasProp(request, "volume"))
            update.volume = (float) getDouble(request, "volume", 1.0);
        if (hasProp(request, "pan"))
            update.pan = (float) getDouble(request, "pan", 0.0);
        if (hasProp(request, "mute"))
            update.mute = getBool(request, "mute", false);
        if (hasProp(request, "solo"))
            update.solo = getBool(request, "solo", false);
        if (hasProp(request, "name"))
            update.name = getString(request, "name");
        if (hasProp(request, "colorIndex"))
            update.colorIndex = getInt(request, "colorIndex", 0);

        const bool recordUndo = getBool(request, "undo", true);
        const auto mixResult = engine_.setTrackMix(trackId, update, recordUndo);
        if (mixResult.failed())
        {
            fail("not_found", engineError());
            return;
        }
        if (hasProp(request, "armed"))
        {
            const auto result = engine_.setTrackArmed(
                trackId, getBool(request, "armed", false));
            if (result.failed())
            {
                fail("not_found", engineError());
                return;
            }
        }
        if (hasProp(request, "inputChannel"))
        {
            const auto result = engine_.setTrackInputChannel(
                trackId, getInt(request, "inputChannel", 0));
            if (result.failed())
            {
                fail("not_found", engineError());
                return;
            }
        }
        auto payload = revisionResult();
        put(payload, "trackId", (juce::int64) trackId);
        reply(payload);
        return;
    }
    if (cmd == "mixer.setMaster")
    {
        const auto result = engine_.setMasterGainEx(
            (float) getDouble(request, "gain", 1.0),
            getBool(request, "undo", true));
        if (result.failed())
        {
            fail("invalid_argument", engineError());
            return;
        }
        auto payload = revisionResult();
        put(payload, "gain", (double) engine_.currentProject()->masterGain);
        reply(payload);
        return;
    }
    if (cmd == "track.add")
    {
        const auto typeString = getString(request, "type", "audio");
        const auto type = typeString.containsIgnoreCase("instrument")
                              || getInt(request, "type", 0) == 1
                          ? Track::Type::Instrument : Track::Type::Audio;
        const auto trackId = engine_.addTrack(
            getString(request, "name", "Track"), getInt(request, "colorIndex", 0),
            type);
        if (trackId < 0)
        {
            fail("invalid_argument", engineError());
            return;
        }
        auto payload = revisionResult();
        put(payload, "trackId", (juce::int64) trackId);
        reply(payload);
        return;
    }
    if (cmd == "track.remove")
    {
        const auto result = engine_.removeTrack(getInt64(request, "trackId", -1));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "clip.move")
    {
        const auto result = engine_.moveClipToTrack(
            getInt64(request, "clipId", -1),
            getInt64(request, "trackId", -1),
            std::max(0.0, getDouble(request, "start", 0.0)));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "clip.remove")
    {
        const auto result = engine_.removeClip(getInt64(request, "clipId", -1));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "clip.split")
    {
        const auto result = engine_.splitClipAt(
            getInt64(request, "clipId", -1),
            std::max(0.0, getDouble(request, "seconds", 0.0)));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "clip.bounds")
    {
        const auto result = engine_.setClipBounds(
            getInt64(request, "clipId", -1),
            getDouble(request, "start", 0.0),
            getDouble(request, "offset", 0.0),
            getDouble(request, "length", 0.0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "clip.fades")
    {
        const auto result = engine_.setClipFades(
            getInt64(request, "clipId", -1),
            getDouble(request, "fadeIn", 0.0),
            getDouble(request, "fadeOut", 0.0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "clip.gain")
    {
        const auto result = engine_.setClipGain(
            getInt64(request, "clipId", -1),
            (float) getDouble(request, "gain", 1.0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "plugins.list")
    {
        juce::Array<juce::var> plugins;
        const int count = engine_.pluginCount();
        for (int i = 0; i < count; ++i)
        {
            auto item = makeObject();
            put(item, "index", i);
            put(item, "name", juce::String::fromUTF8(engine_.pluginNameAt(i)));
            put(item, "isInstrument", engine_.pluginIsInstrumentAt(i));
            put(item, "format", juce::String::fromUTF8(engine_.pluginFormatAt(i)));
            plugins.add(item);
        }
        reply(juce::var(plugins));
        return;
    }
    if (cmd == "fx.params")
    {
        const auto trackId = getInt64(request, "trackId", -1);
        const int slotKind = getInt(request, "slotKind", 0);
        const int slotIndex = getInt(request, "slotIndex", 0);
        const int count = engine_.pluginParamCount(trackId, slotKind, slotIndex);
        juce::Array<juce::var> params;
        for (int i = 0; i < count; ++i)
        {
            auto item = makeObject();
            put(item, "index", i);
            put(item, "name", juce::String::fromUTF8(
                engine_.pluginParamName(trackId, slotKind, slotIndex, i)));
            put(item, "value", (double) engine_.pluginParamValue(
                trackId, slotKind, slotIndex, i));
            put(item, "text", juce::String::fromUTF8(
                engine_.pluginParamText(trackId, slotKind, slotIndex, i)));
            params.add(item);
        }
        auto result = makeObject();
        put(result, "slotKind", slotKind);
        put(result, "slotIndex", slotIndex);
        put(result, "params", juce::var(params));
        reply(result);
        return;
    }
    if (cmd == "fx.setParam")
    {
        const auto trackId = getInt64(request, "trackId", -1);
        const int slotKind = getInt(request, "slotKind", 0);
        const int slotIndex = getInt(request, "slotIndex", 0);
        const int paramIndex = getInt(request, "paramIndex", -1);
        const auto result = engine_.setPluginParamValue(
            trackId, slotKind, slotIndex, paramIndex,
            (float) getDouble(request, "value", 0.0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        auto payload = makeObject();
        put(payload, "value", (double) engine_.pluginParamValue(
            trackId, slotKind, slotIndex, paramIndex));
        put(payload, "text", juce::String::fromUTF8(
            engine_.pluginParamText(trackId, slotKind, slotIndex, paramIndex)));
        reply(payload);
        return;
    }
    if (cmd == "fx.setBypass")
    {
        const auto result = engine_.setTrackFxBypass(
            getInt64(request, "trackId", -1), getInt(request, "slotIndex", 0),
            getBool(request, "bypass", true));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "fx.remove")
    {
        const auto result = engine_.removeTrackFx(
            getInt64(request, "trackId", -1), getInt(request, "slotIndex", 0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "fx.add")
    {
        const auto result = engine_.addTrackFx(
            getInt64(request, "trackId", -1), getInt(request, "pluginIndex", -1));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "fx.master.add")
    {
        const auto result = engine_.addMasterFx(getInt(request, "pluginIndex", -1));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "fx.master.remove")
    {
        const auto result = engine_.removeMasterFx(getInt(request, "slotIndex", 0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "fx.master.setBypass")
    {
        const auto result = engine_.setMasterFxBypass(
            getInt(request, "slotIndex", 0), getBool(request, "bypass", true));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "automation.set")
    {
        const auto result = engine_.setTrackLaneAutomation(
            getInt64(request, "trackId", -1), getInt(request, "lane", 0),
            pointsFromRequest(request));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "automation.param.set")
    {
        const auto result = engine_.setTrackParamAutomation(
            getInt64(request, "trackId", -1), getInt(request, "slotKind", 0),
            getInt(request, "slotIndex", 0), getInt(request, "paramIndex", -1),
            getString(request, "name", "Param"), pointsFromRequest(request));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "automation.param.remove")
    {
        const auto result = engine_.removeTrackParamLane(
            getInt64(request, "trackId", -1), getInt(request, "laneIndex", -1));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }
    if (cmd == "automation.writeVolume")
    {
        const auto result = engine_.setTrackVolumeAutomationPointAt(
            getInt64(request, "trackId", -1),
            getDouble(request, "time", 0.0),
            getDouble(request, "value", 0.0));
        if (result.failed())
        {
            fail("not_found", engineError());
            return;
        }
        reply(revisionResult());
        return;
    }

    fail("unknown_command", "Unknown command: " + cmd);
}

juce::var RemoteServer::buildState() const
{
    auto root = makeObject();
    put(root, "revision", (juce::int64) engine_.projectRevision());
    put(root, "canUndo", engine_.canUndo());
    put(root, "canRedo", engine_.canRedo());

    const auto project = engine_.currentProject();
    if (project == nullptr)
    {
        put(root, "name", "Untitled");
        put(root, "sampleRate", 48000.0);
        put(root, "tempoBpm", 120.0);
        put(root, "timeSigNum", 4);
        put(root, "timeSigDen", 4);
        put(root, "duration", 0.0);
        put(root, "masterGain", 1.0);
        put(root, "masterFx", juce::var(juce::Array<juce::var>()));
        put(root, "tracks", juce::var(juce::Array<juce::var>()));
        return root;
    }

    put(root, "name", project->name);
    put(root, "sampleRate", project->sampleRate);
    put(root, "tempoBpm", project->tempoBpm);
    put(root, "timeSigNum", project->timeSigNumerator);
    put(root, "timeSigDen", project->timeSigDenominator);
    put(root, "duration", project->durationSeconds());
    put(root, "masterGain", (double) project->masterGain);
    put(root, "inputQuantize", project->inputQuantizeSeconds);

    juce::Array<juce::var> masterFx;
    for (int i = 0; i < (int) project->masterFx.size(); ++i)
    {
        const auto& slot = project->masterFx[(std::size_t) i];
        auto item = makeObject();
        put(item, "index", i);
        put(item, "name", slot.name);
        put(item, "bypass", slot.bypass);
        masterFx.add(item);
    }
    put(root, "masterFx", juce::var(masterFx));

    juce::Array<juce::var> tracks;
    for (const auto& track : project->tracks)
    {
        auto item = makeObject();
        put(item, "id", (juce::int64) track.id);
        put(item, "type", track.type == Track::Type::Audio
                              ? juce::var("audio") : juce::var("instrument"));
        put(item, "name", track.name);
        put(item, "colorIndex", track.colorIndex);
        put(item, "volume", (double) track.volume);
        put(item, "pan", (double) track.pan);
        put(item, "mute", track.mute);
        put(item, "solo", track.solo);
        put(item, "armed", track.armed);
        put(item, "inputChannel", track.inputChannel);
        put(item, "instrument", track.instrument.pluginUid.isNotEmpty()
                                   ? juce::var(track.instrument.name)
                                   : juce::var(""));
        put(item, "midiFx", track.midiFx.pluginUid.isNotEmpty()
                                ? juce::var(track.midiFx.name) : juce::var(""));

        juce::Array<juce::var> fx;
        for (int i = 0; i < (int) track.fx.size(); ++i)
        {
            const auto& slot = track.fx[(std::size_t) i];
            auto fxItem = makeObject();
            put(fxItem, "index", i);
            put(fxItem, "name", slot.name);
            put(fxItem, "bypass", slot.bypass);
            fx.add(fxItem);
        }
        put(item, "fx", juce::var(fx));

        auto automation = makeObject();
        put(automation, "volume", curveToVar(track.volumeAutomation));
        put(automation, "pan", curveToVar(track.panAutomation));
        put(automation, "sustain", curveToVar(track.sustainAutomation));

        juce::Array<juce::var> paramLanes;
        for (int i = 0; i < (int) track.paramLanes.size(); ++i)
        {
            const auto& lane = track.paramLanes[(std::size_t) i];
            auto laneItem = makeObject();
            put(laneItem, "index", i);
            put(laneItem, "slotKind", lane.slotKind);
            put(laneItem, "slotIndex", lane.slotIndex);
            put(laneItem, "paramIndex", lane.paramIndex);
            put(laneItem, "name", lane.name);
            put(laneItem, "points", curveToVar(lane.curve));
            paramLanes.add(laneItem);
        }
        put(automation, "params", juce::var(paramLanes));
        put(item, "automation", automation);

        juce::Array<juce::var> clips;
        for (const auto& clip : track.clips)
        {
            auto clipItem = makeObject();
            put(clipItem, "id", (juce::int64) clip.id);
            put(clipItem, "kind", clip.isMidi() ? "midi" : "audio");
            put(clipItem, "start", clip.startSeconds);
            put(clipItem, "length", clip.lengthSeconds);
            put(clipItem, "offset", clip.sourceOffsetSeconds);
            put(clipItem, "gain", (double) clip.gain);
            put(clipItem, "fadeIn", (double) clip.fadeInSeconds);
            put(clipItem, "fadeOut", (double) clip.fadeOutSeconds);
            put(clipItem, "muted", clip.muted);
            if (clip.isMidi() && clip.midi != nullptr)
            {
                juce::Array<juce::var> notes;
                for (const auto& note : clip.midi->notes)
                {
                    auto noteItem = makeObject();
                    put(noteItem, "pitch", note.pitch);
                    put(noteItem, "velocity", (double) note.velocity);
                    put(noteItem, "start", note.startSeconds);
                    put(noteItem, "length", note.lengthSeconds);
                    if (std::abs(note.origStartSeconds - note.startSeconds) > 1e-9)
                        put(noteItem, "origStart", note.origStartSeconds);
                    notes.add(noteItem);
                }
                put(clipItem, "notes", juce::var(notes));
            }
            clips.add(clipItem);
        }
        put(item, "clips", juce::var(clips));
        tracks.add(item);
    }
    put(root, "tracks", juce::var(tracks));
    return root;
}

juce::var RemoteServer::buildTransport() const
{
    auto root = makeObject();
    put(root, "playing", engine_.isPlaying());
    put(root, "recording", engine_.isRecording());
    put(root, "position", engine_.positionSeconds());
    put(root, "duration", engine_.durationSeconds());
    put(root, "recordStart", engine_.recordStartSeconds());
    put(root, "metronome", engine_.metronomeEnabled());
    put(root, "tempoBpm", engine_.tempoBpm());
    put(root, "timeSigNum", engine_.timeSigNumerator());
    put(root, "timeSigDen", engine_.timeSigDenominator());
    put(root, "revision", (juce::int64) engine_.projectRevision());
    return root;
}

juce::var RemoteServer::buildMeters() const
{
    auto root = makeObject();
    juce::Array<juce::var> tracks;
    if (const auto project = engine_.currentProject())
    {
        int index = 0;
        for (const auto& track : project->tracks)
        {
            auto item = makeObject();
            put(item, "id", (juce::int64) track.id);
            put(item, "peak", (double) engine_.remoteMeterTrackPeak(index++));
            tracks.add(item);
        }
    }
    put(root, "tracks", juce::var(tracks));
    juce::Array<juce::var> master;
    master.add((double) engine_.remoteMeterMasterPeak(0));
    master.add((double) engine_.remoteMeterMasterPeak(1));
    put(root, "master", juce::var(master));
    return root;
}

juce::var RemoteServer::buildWelcome(const std::shared_ptr<Client>& client) const
{
    auto root = makeObject();
    put(root, "protocol", kProtocolVersion);
    put(root, "clientId", client->id);

    auto server = makeObject();
    put(server, "app", "EZMusicProduction");
    put(server, "port", port_.load(std::memory_order_acquire));
    put(server, "requiresToken", true);
    put(server, "protocol", kProtocolVersion);
    if (const auto project = engine_.currentProject())
        put(server, "projectName", project->name);
    juce::Array<juce::var> capabilities;
    for (const char* capability : { "transport", "timeline", "mixer", "fx",
                                    "automation", "meters", "plugins" })
        capabilities.add(capability);
    put(server, "capabilities", juce::var(capabilities));
    put(root, "server", server);

    put(root, "state", buildState());
    put(root, "transport", buildTransport());
    return root;
}

void RemoteServer::broadcast(const juce::String& json, bool metersOnly)
{
    std::vector<std::shared_ptr<Client>> clients;
    {
        const std::scoped_lock lock(clientsMutex_);
        clients = clients_;
    }
    for (const auto& client : clients)
    {
        if (client->closing.load(std::memory_order_acquire)
            || ! client->authenticated.load(std::memory_order_acquire))
            continue;
        if (metersOnly
            && ! client->wantsMeters.load(std::memory_order_acquire))
            continue;
        if (metersOnly)
        {
            // metersHz is per client: the tick builds at 20 Hz, but a slow
            // client only receives every Nth sample.
            const int hz = juce::jlimit(
                1, 30, client->metersHz.load(std::memory_order_acquire));
            const auto now = (std::int64_t) juce::Time::getMillisecondCounter();
            const auto last = client->lastMetersMs.load(
                std::memory_order_acquire);
            if (last != 0 && now - last < 1000 / hz)
                continue;
            client->lastMetersMs.store(now, std::memory_order_release);
        }
        client->sendText(json);
    }
}

void RemoteServer::tickLoop()
{
    while (! stopping_.load(std::memory_order_acquire))
    {
        const auto now = (std::int64_t) juce::Time::getMillisecondCounter();

        reapClients();

        const auto revision = engine_.projectRevision();
        if (revision != lastProjectRevision_ && now - lastProjectSentMs_ >= 120)
        {
            const auto message = eventMessage("project", buildState());
            broadcast(juce::JSON::toString(message, false), false);
            lastProjectRevision_ = revision;
            lastProjectSentMs_ = now;
        }

        const bool playing = engine_.isPlaying();
        const bool recording = engine_.isRecording();
        const double position = engine_.positionSeconds();
        const bool transportChanged = playing != lastPlaying_
                                      || recording != lastRecording_
                                      || std::abs(position - lastPosition_) > 1e-4;
        const bool transportDue = (playing || recording)
                                      ? now - lastTransportSentMs_ >= 100
                                      : now - lastTransportSentMs_ >= 500;
        if (transportChanged || transportDue)
        {
            const auto message = eventMessage("transport", buildTransport());
            broadcast(juce::JSON::toString(message, false), false);
            lastPlaying_ = playing;
            lastRecording_ = recording;
            lastPosition_ = position;
            lastTransportSentMs_ = now;
        }

        if (now - lastMetersSentMs_ >= 50)
        {
            bool anyMeters = false;
            {
                const std::scoped_lock lock(clientsMutex_);
                for (const auto& client : clients_)
                    anyMeters = anyMeters
                                || (client->authenticated.load(std::memory_order_acquire)
                                    && client->wantsMeters.load(std::memory_order_acquire));
            }
            if (anyMeters)
            {
                const auto message = eventMessage("meters", buildMeters());
                broadcast(juce::JSON::toString(message, false), true);
            }
            lastMetersSentMs_ = now;
        }

        juce::Thread::sleep(kTickMs);
    }
}

void RemoteServer::reapClients()
{
    std::vector<std::shared_ptr<Client>> finished;
    {
        const std::scoped_lock lock(clientsMutex_);
        auto it = clients_.begin();
        while (it != clients_.end())
        {
            if ((*it)->closing.load(std::memory_order_acquire))
            {
                finished.push_back(*it);
                it = clients_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    for (auto& client : finished)
    {
        if (client->thread.joinable()
            && client->thread.get_id() != std::this_thread::get_id())
            client->thread.join();
        clientCount_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

} // namespace cadence
