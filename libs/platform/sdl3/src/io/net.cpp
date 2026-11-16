// Définitions de sdl3/net.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <sdl3/sdl3.hpp>
#include "sdl3/io/net.hpp"

namespace sdl3 {

// ── NetContext ───────────────────────────────────────────────────────────────

NetContext::~NetContext() {
    if (m_owns)
        NET_Quit();
}

Result<NetContext, Error> NetContext::Create() {
    NetContext ctx;
    ctx.m_owns = NET_Init();
    if (!ctx)
        return Err(GetError());
    return Ok(std::move(ctx));
}

// ── IpAddress ────────────────────────────────────────────────────────────────

Result<IpAddress, StringView> IpAddress::Resolve(const String &host) {
    auto *addr = NET_ResolveHostname(host.c_str());
    if (!addr)
        return Err(GetError());
    return Ok(IpAddress(addr));
}

bool IpAddress::Wait(int timeoutMs) {
    if (!mHandle)
        return false;
    return NET_WaitUntilResolved(mHandle, timeoutMs) == NET_SUCCESS;
}

String IpAddress::ToString() const {
    if (!mHandle)
        return {};
    const char *s = NET_GetAddressString(mHandle);
    return String(s ? s : "");
}

std::vector<uint8_t> IpAddress::Bytes() const {
    std::vector<uint8_t> out;
    if (!mHandle || NET_GetAddressStatus(mHandle) != NET_SUCCESS)
        return out;
    int n = 0;
    const void *bytes = NET_GetAddressBytes(mHandle, &n);
    if (bytes && n > 0)
        out.assign(static_cast<const uint8_t *>(bytes), static_cast<const uint8_t *>(bytes) + n);
    return out;
}

std::vector<IpAddress> IpAddress::LocalAddresses() {
    std::vector<IpAddress> out;
    int count = 0;
    NET_Address **list = NET_GetLocalAddresses(&count);
    if (!list)
        return out;
    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        out.push_back(Share(list[i]));
    NET_FreeLocalAddresses(list); // libère la liste et ses références, pas les nôtres
    return out;
}

// ── TcpSocket ────────────────────────────────────────────────────────────────

Result<TcpSocket, StringView> TcpSocket::Connect(IpAddress &addr, uint16_t port) {
    auto *s = NET_CreateClient(addr.Get(), port, 0);
    if (!s)
        return Err(GetError());
    return Ok(TcpSocket(s));
}

bool TcpSocket::WaitConnected(int timeoutMs) {
    return mHandle && NET_WaitUntilConnected(mHandle, timeoutMs) == NET_SUCCESS;
}

// ── TcpServer ────────────────────────────────────────────────────────────────

Result<TcpServer, StringView> TcpServer::Listen(uint16_t port, IpAddress *addr) {
    auto *s = NET_CreateServer(addr ? addr->Get() : nullptr, port, 0);
    if (!s)
        return Err(GetError());
    return Ok(TcpServer(s));
}

Option<TcpSocket> TcpServer::Accept() {
    if (!mHandle)
        return NONE;
    NET_StreamSocket *client = nullptr;
    if (!NET_AcceptClient(mHandle, &client) || !client)
        return NONE;
    return Some(TcpSocket(client));
}

// ── UdpSocket ────────────────────────────────────────────────────────────────

Result<UdpSocket, StringView> UdpSocket::Open(uint16_t localPort, IpAddress *addr) {
    auto *s = NET_CreateDatagramSocket(addr ? addr->Get() : nullptr, localPort, 0);
    if (!s)
        return Err(GetError());
    return Ok(UdpSocket(s));
}

Result<UdpSocket, StringView> UdpSocket::Open(uint16_t localPort, const UdpOptions &options, IpAddress *addr) {
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props)
        return Err(GetError());
    SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_ALLOW_BROADCAST_BOOLEAN, options.allowBroadcast);
    SDL_SetBooleanProperty(props, NET_PROP_DATAGRAM_SOCKET_REUSEADDR_BOOLEAN, options.reuseAddress);
    auto *s = NET_CreateDatagramSocket(addr ? addr->Get() : nullptr, localPort, props);
    SDL_DestroyProperties(props);
    if (!s)
        return Err(GetError());
    return Ok(UdpSocket(s));
}

bool UdpSocket::Send(IpAddress &dest, uint16_t port, const void *data, int len) {
    return mHandle && NET_SendDatagram(mHandle, dest.Get(), port, data, len);
}

Option<ReceivedDatagram> UdpSocket::Receive() {
    if (!mHandle)
        return NONE;
    NET_Datagram *dgram = nullptr;
    if (!NET_ReceiveDatagram(mHandle, &dgram) || !dgram)
        return NONE;

    ReceivedDatagram result;
    result.port = dgram->port;
    result.data.assign(static_cast<const uint8_t *>(dgram->buf),
                       static_cast<const uint8_t *>(dgram->buf) + dgram->buflen);
    const char *addrStr = dgram->addr ? NET_GetAddressString(dgram->addr) : nullptr;
    result.senderAddr = String(addrStr ? addrStr : "");
    result.sender = IpAddress::Share(dgram->addr);
    NET_DestroyDatagram(dgram);
    return Some(std::move(result));
}

} // namespace sdl3
