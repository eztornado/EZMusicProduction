#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cadence {

class Engine;

// LAN remote-control server: WebSocket + JSON.
//
// One accept thread creates a Client per connection; each client has one
// thread that reads frames, writes queued messages and answers WebSocket
// control frames. A tick thread pushes transport/project/meter events.
//
// Commands that mutate the document are queued and executed on the engine's
// message thread. They are drained either by JUCE's async queue (when the host
// pumps it, as the macOS app does) or by Engine::pumpRemoteCommands() from the
// UI frame ticker. This dual path keeps the server usable in headless tests.
//
// Protocol and message catalogue: docs/design/remote-control.md.
class RemoteServer
{
public:
    static constexpr int kDefaultPort = 47823;
    static constexpr int kProtocolVersion = 1;

    explicit RemoteServer(Engine& engine);
    ~RemoteServer();

    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    // Starts (or restarts) the listener. port 0 picks an ephemeral port.
    // Empty token generates a 6-digit pairing code.
    juce::Result start(int port, const juce::String& token);
    void stop();

    // Binds the shared owner so queued callAsync callbacks can keep the
    // server alive while they wait for the message thread.
    void bindSelf(const std::shared_ptr<RemoteServer>& self) { self_ = self; }

    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    int port() const { return port_.load(std::memory_order_acquire); }
    int clientCount() const { return clientCount_.load(std::memory_order_acquire); }
    const juce::String& token() const { return token_; }
    juce::String lastError() const;

    // Drains queued commands on the caller's thread (must be the message
    // thread). Returns how many commands were executed.
    int pumpCommands();

private:
    struct Client;
    struct Command;

    void acceptLoop();
    void tickLoop();
    void handleClient(const std::shared_ptr<Client>& client);
    bool performHandshake(Client& client);
    void enqueueCommand(const std::shared_ptr<Client>& client,
                        const juce::var& request);
    void executeCommand(const Command& command);
    int drainCommands();
    void broadcast(const juce::String& json, bool metersOnly);
    void reapClients();
    void setLastError(const juce::String& message);

    juce::var buildState() const;
    juce::var buildTransport() const;
    juce::var buildMeters() const;
    juce::var buildWelcome(const std::shared_ptr<Client>& client) const;

    Engine& engine_;
    std::weak_ptr<RemoteServer> self_;

    juce::StreamingSocket listener_;
    std::thread acceptThread_;
    std::thread tickThread_;

    std::atomic<bool> running_ { false };
    std::atomic<bool> stopping_ { false };
    std::atomic<bool> alive_ { true };
    std::atomic<int> port_ { 0 };
    std::atomic<int> clientCount_ { 0 };

    juce::String token_;
    mutable std::mutex errorMutex_;
    juce::String lastError_;

    mutable std::mutex clientsMutex_;
    std::vector<std::shared_ptr<Client>> clients_;
    int nextClientId_ = 1; // accept thread only

    std::mutex commandsMutex_;
    std::deque<std::shared_ptr<Command>> commands_;

    // Guarded by commandsMutex_: prevents a burst of callAsync posts.
    bool pumpPosted_ = false;

    // Tick-thread state.
    std::uint64_t lastProjectRevision_ = 0;
    std::int64_t lastProjectSentMs_ = 0;
    std::int64_t lastTransportSentMs_ = 0;
    std::int64_t lastMetersSentMs_ = 0;
    bool lastPlaying_ = false;
    bool lastRecording_ = false;
    double lastPosition_ = -1.0;
};

} // namespace cadence
