#pragma once
/**
 * @file net.hpp
 * @brief Réseau (SDL3_net) : sockets TCP/UDP, résolution d'adresses.
 */
#include <SDL3_net/SDL_net.h>
#include <span>
#include <vector>

#include <core/core.hpp>

namespace sdl3 {

/// SDL3_net version number compiled into the library (NET_Version).
[[nodiscard]] inline int NetVersion() { return NET_Version(); }

/// Simulate hostname-resolution failures (0-100%), for testing resilience.
inline void SimulateAddressResolutionLoss(int percentLoss) { NET_SimulateAddressResolutionLoss(percentLoss); }

// ============================================================================
// NetContext — RAII NET_Init / NET_Quit
// ============================================================================

/// Contexte réseau (RAII sur NET_Init / NET_Quit).
class NetContext {
    bool m_owns = false;

public:
    NetContext() = default;
    ~NetContext();

    NetContext(const NetContext &) = delete;
    NetContext &operator=(const NetContext &) = delete;
    NetContext(NetContext &&o) noexcept : m_owns(o.m_owns) { o.m_owns = false; }
    NetContext &operator=(NetContext &&o) noexcept {
        if (this != &o) {
            if (m_owns)
                NET_Quit();
            m_owns = o.m_owns;
            o.m_owns = false;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_owns; }

    [[nodiscard]] static Result<NetContext, Error> Create();
};

// ============================================================================
// IpAddress — reference-counted NET_Address (hostname resolution)
// ============================================================================

/// Adresse réseau à comptage de références (résolution de nom d'hôte).
class IpAddress : public Wrapper<NET_Address, NET_UnrefAddress> {
public:
    using Wrapper::Wrapper;

    [[nodiscard]] static Result<IpAddress, StringView> Resolve(const String &host);

    /// Block until the hostname is resolved.
    /// timeout_ms <= 0 means wait indefinitely.
    bool Wait(int timeoutMs = -1);

    [[nodiscard]] String ToString() const;

    /// Nouvelle référence sur une adresse existante (NET_RefAddress) : le
    /// wrapper rendu la libère, l'appelant garde la sienne.
    [[nodiscard]] static IpAddress Share(NET_Address *addr) { return IpAddress(addr ? NET_RefAddress(addr) : nullptr); }

    /// Copie partagée (même NET_Address, compteur de références incrémenté).
    [[nodiscard]] IpAddress Clone() const { return Share(mHandle); }

    /// NET_SUCCESS (résolue), NET_WAITING (en cours) ou NET_FAILURE.
    [[nodiscard]] NET_Status Status() const noexcept { return mHandle ? NET_GetAddressStatus(mHandle) : NET_FAILURE; }

    /// Octets bruts (4 pour IPv4, 16 pour IPv6) ; vide si non résolue.
    [[nodiscard]] std::vector<uint8_t> Bytes() const;

    [[nodiscard]] bool IsIpv4() const { return Bytes().size() == 4; }

    /// Même adresse (NET_CompareAddresses) — pas forcément le même objet.
    [[nodiscard]] bool SameAs(const IpAddress &other) const noexcept {
        return mHandle && other.mHandle && NET_CompareAddresses(mHandle, other.mHandle) == 0;
    }

    /// Adresses des interfaces de cette machine (déjà résolues), boucle
    /// locale comprise.
    [[nodiscard]] static std::vector<IpAddress> LocalAddresses();
};

// ============================================================================
// TcpSocket — RAII stream (TCP) client socket
// ============================================================================

/// Socket client TCP (flux, RAII sur NET_StreamSocket).
class TcpSocket : public Wrapper<NET_StreamSocket, NET_DestroyStreamSocket> {
public:
    using Wrapper::Wrapper;

    /// Connect to a resolved address on the given port.
    [[nodiscard]] static Result<TcpSocket, StringView> Connect(IpAddress &addr, uint16_t port);

    /// Block until connected. Returns true on success.
    bool WaitConnected(int timeoutMs = -1);

    /// Send raw bytes. Returns true on success.
    bool Send(const void *data, int len) { return mHandle && NET_WriteToStreamSocket(mHandle, data, len); }
    template <typename T> bool Send(std::span<const T> data) { return Send(data.data(), int(data.size_bytes())); }

    /// Receive raw bytes. Returns bytes read (0 = no data yet) or -1 on error.
    int Receive(void *data, int len) { return mHandle ? NET_ReadFromStreamSocket(mHandle, data, len) : -1; }
    template <typename T> int Receive(std::span<T> data) { return Receive(data.data(), int(data.size_bytes())); }

    /// Sans bloquer : NET_SUCCESS (connecté), NET_WAITING (connexion en
    /// cours) ou NET_FAILURE (refusée, injoignable).
    [[nodiscard]] NET_Status Status() const noexcept { return mHandle ? NET_GetConnectionStatus(mHandle) : NET_FAILURE; }

    /// Octets écrits mais pas encore partis vers le réseau (-1 : erreur).
    /// Sert à réguler un envoi volumineux sans gonfler la mémoire.
    [[nodiscard]] int PendingWrites() const noexcept { return mHandle ? NET_GetStreamSocketPendingWrites(mHandle) : -1; }

    /// Attend que les écritures en attente soient parties ; rend ce qui reste
    /// (0 : tout est parti, -1 : erreur).
    int WaitDrained(int timeoutMs = -1) { return mHandle ? NET_WaitUntilStreamSocketDrained(mHandle, timeoutMs) : -1; }

    /// Adresse de l'autre extrémité.
    [[nodiscard]] IpAddress RemoteAddress() const { return IpAddress(mHandle ? NET_GetStreamSocketAddress(mHandle) : nullptr); }
};

// ============================================================================
// TcpServer — RAII server socket (accepts TCP connections)
// ============================================================================

/// Serveur TCP (accepte des connexions, RAII sur NET_Server).
class TcpServer : public Wrapper<NET_Server, NET_DestroyServer> {
public:
    using Wrapper::Wrapper;

    /// Listen on port (all interfaces if addr is nullptr).
    [[nodiscard]] static Result<TcpServer, StringView> Listen(uint16_t port, IpAddress *addr = nullptr);

    /// Non-blocking accept — returns NONE if no client is waiting.
    [[nodiscard]] Option<TcpSocket> Accept();
};

// ============================================================================
// UdpSocket — RAII datagram (UDP) socket
// ============================================================================

struct ReceivedDatagram {
    String senderAddr;
    uint16_t port = 0;
    std::vector<uint8_t> data;
    IpAddress sender; ///< adresse de l'expéditeur, réutilisable pour lui répondre
};

/// Options d'ouverture d'un socket UDP (propriétés NET_PROP_DATAGRAM_SOCKET_*).
struct UdpOptions {
    /// Autorise l'envoi en diffusion (SO_BROADCAST en IPv4, groupe ff02::1 en
    /// IPv6) — nécessaire pour Broadcast() et l'envoi vers x.x.x.255.
    bool allowBroadcast = false;
    /// Plusieurs sockets sur le même port (plusieurs instances sur une même
    /// machine reçoivent alors toutes les diffusions).
    bool reuseAddress = true;
};

/// Socket UDP (datagrammes, RAII sur NET_DatagramSocket).
class UdpSocket : public Wrapper<NET_DatagramSocket, NET_DestroyDatagramSocket> {
public:
    using Wrapper::Wrapper;

    /// Open a UDP socket, optionally bound to a local port (0 = any).
    [[nodiscard]] static Result<UdpSocket, StringView> Open(uint16_t localPort = 0, IpAddress *addr = nullptr);
    [[nodiscard]] static Result<UdpSocket, StringView> Open(uint16_t localPort, const UdpOptions &options,
                                                            IpAddress *addr = nullptr);

    bool Send(IpAddress &dest, uint16_t port, const void *data, int len);
    template <typename T> bool Send(IpAddress &dest, uint16_t port, std::span<const T> data) {
        return Send(dest, port, data.data(), int(data.size_bytes()));
    }

    /// Diffusion sur le réseau local (adresse NULL de NET_SendDatagram) ; le
    /// socket doit avoir été ouvert avec `allowBroadcast`.
    bool Broadcast(uint16_t port, const void *data, int len) {
        return mHandle && NET_SendDatagram(mHandle, nullptr, port, data, len);
    }
    template <typename T> bool Broadcast(uint16_t port, std::span<const T> data) {
        return Broadcast(port, data.data(), int(data.size_bytes()));
    }

    /// Attend qu'un datagramme arrive (0 : ne fait que sonder) ; true s'il y
    /// en a au moins un à lire.
    bool WaitInput(int timeoutMs) {
        void *socks[] = {mHandle};
        return mHandle && NET_WaitUntilInputAvailable(socks, 1, timeoutMs) > 0;
    }

    /// Non-blocking receive — returns NONE if no datagram is available.
    [[nodiscard]] Option<ReceivedDatagram> Receive();
};

/// Simulate TCP packet loss on a stream socket (0-100%), for testing.
inline void SimulateStreamPacketLoss(TcpSocket &sock, int percentLoss) {
	NET_SimulateStreamPacketLoss(sock.Get(), percentLoss);
}

/// Simulate UDP packet loss on a datagram socket (0-100%), for testing.
inline void SimulateDatagramPacketLoss(UdpSocket &sock, int percentLoss) {
	NET_SimulateDatagramPacketLoss(sock.Get(), percentLoss);
}

} // namespace sdl3
