#include "neqo-client-app.h"

#include "neqo_ns3_bridge.h"

#include "ns3/address-utils.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <iostream>

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("NeqoClientApp");
namespace neqo_ns3
{
NS_OBJECT_ENSURE_REGISTERED(NeqoClientApp);

TypeId
NeqoClientApp::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::neqo_ns3::NeqoClientApp")
            .SetParent<Application>()
            .SetGroupName("Neqo")
            .AddConstructor<NeqoClientApp>()
            .AddAttribute("RemoteAddress",
                          "InetSocketAddress of the server",
                          AddressValue(),
                          MakeAddressAccessor(&NeqoClientApp::m_remote),
                          MakeAddressChecker())
            .AddAttribute("MaxBytes",
                          "Bytes to send on the stream",
                          UintegerValue(1000000),
                          MakeUintegerAccessor(&NeqoClientApp::m_maxBytes),
                          MakeUintegerChecker<uint64_t>());
    return tid;
}

NeqoClientApp::NeqoClientApp() = default;
NeqoClientApp::~NeqoClientApp() = default;

void
NeqoClientApp::DoDispose()
{
    m_socket = nullptr;
    Application::DoDispose();
}

void
NeqoClientApp::StartApplication()
{
    m_payload.assign(65536, 0x5a);
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    NS_ABORT_MSG_IF(m_socket->Bind() != 0, "client: bind failed");
    m_socket->SetRecvCallback(MakeCallback(&NeqoClientApp::HandleRead, this));

    Ptr<Ipv4> ipv4 = GetNode()->GetObject<Ipv4>();
    NS_ABORT_MSG_IF(!ipv4 || ipv4->GetNInterfaces() < 2, "client node has no IPv4 interface");
    m_localIp = ipv4->GetAddress(1, 0).GetLocal().Get();
    Address sockName;
    m_socket->GetSockName(sockName);
    m_localPort = InetSocketAddress::ConvertFrom(sockName).GetPort();

    InetSocketAddress remote = InetSocketAddress::ConvertFrom(m_remote);
    m_conn = neqo_bridge_client_new(m_localIp,
                                    m_localPort,
                                    remote.GetIpv4().Get(),
                                    remote.GetPort(),
                                    Simulator::Now().GetNanoSeconds());
    NS_ABORT_MSG_IF(!m_conn, "neqo_bridge_client_new failed");
    std::cout << "[client n" << GetNode()->GetId() << "] starting handshake at t=" << Simulator::Now().GetMilliSeconds()
              << " ms" << std::endl;
    Drive();
}

void
NeqoClientApp::StopApplication()
{
    m_timer.Cancel();
    if (m_socket)
    {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }
    if (m_conn)
    {
        neqo_bridge_free(m_conn);
        m_conn = nullptr;
    }
}

void
NeqoClientApp::HandleRead(Ptr<Socket> socket)
{
    Address from;
    while (Ptr<Packet> p = socket->RecvFrom(from))
    {
        if (!m_conn || p->GetSize() == 0)
        {
            continue;
        }
        InetSocketAddress src = InetSocketAddress::ConvertFrom(from);
        std::vector<uint8_t> buf(p->GetSize());
        p->CopyData(buf.data(), buf.size());
        int rc = neqo_bridge_process_input(m_conn,
                                           buf.data(),
                                           buf.size(),
                                           src.GetIpv4().Get(),
                                           src.GetPort(),
                                           m_localIp,
                                           m_localPort,
                                           Simulator::Now().GetNanoSeconds());
        if (rc != 0)
        {
            NS_LOG_WARN("neqo client: process_input returned " << rc);
        }
    }
    Drive();
}

void
NeqoClientApp::ProcessEvents()
{
    NeqoEvent ev;
    while (neqo_bridge_next_event(m_conn, &ev) == 1)
    {
        if (ev.tag != NEQO_EV_STREAM_READABLE || ev.stream_id != m_streamId || m_done)
        {
            continue;
        }
        std::vector<uint8_t> rbuf(4096);
        for (;;)
        {
            int fin = 0;
            int64_t n = neqo_bridge_stream_recv(m_conn, ev.stream_id, rbuf.data(), rbuf.size(), &fin);
            if (n < 0)
            {
                break;
            }
            if (fin == 1)
            {
                m_done = true;
                const int64_t nowNs = Simulator::Now().GetNanoSeconds();
                const double secs = (nowNs - m_startNs) / 1e9;
                std::cout << "[client n" << GetNode()->GetId() << "] transfer complete at t=" << Simulator::Now().GetMilliSeconds()
                          << " ms: " << m_sent << " bytes in " << secs * 1000 << " ms, goodput "
                          << (m_sent * 8.0 / secs / 1e6) << " Mbps" << std::endl;
                neqo_bridge_close(m_conn, nowNs, 0);
                break;
            }
            if (n == 0)
            {
                break;
            }
        }
    }
}

void
NeqoClientApp::TrySend()
{
    if (!m_opened || m_finSent || m_done)
    {
        return;
    }
    while (m_sent < m_maxBytes)
    {
        const size_t chunk = std::min<uint64_t>(m_payload.size(), m_maxBytes - m_sent);
        int64_t n = neqo_bridge_stream_send(m_conn, m_streamId, m_payload.data(), chunk);
        if (n < 0)
        {
            NS_LOG_WARN("neqo client: stream_send failed");
            return;
        }
        if (n == 0)
        {
            return; // flow control or send buffer full; retried on the next Drive()
        }
        m_sent += n;
    }
    if (neqo_bridge_stream_close_send(m_conn, m_streamId) == 0)
    {
        m_finSent = true;
        std::cout << "[client n" << GetNode()->GetId() << "] queued all " << m_sent << " bytes at t="
                  << Simulator::Now().GetMilliSeconds() << " ms" << std::endl;
    }
}

void
NeqoClientApp::Drive()
{
    if (!m_conn)
    {
        return;
    }
    m_timer.Cancel();
    const uint64_t now = Simulator::Now().GetNanoSeconds();

    if (!m_established && neqo_bridge_state(m_conn) == NEQO_STATE_CONFIRMED)
    {
        m_established = true;
        std::cout << "[client n" << GetNode()->GetId() << "] handshake confirmed at t=" << Simulator::Now().GetMilliSeconds()
                  << " ms" << std::endl;
        if (m_maxBytes > 0 && neqo_bridge_stream_create(m_conn, &m_streamId) == 0)
        {
            m_opened = true;
            m_startNs = now;
        }
    }

    ProcessEvents();
    TrySend();

    std::vector<uint8_t> buf(2048);
    for (int i = 0; i < 10000; i++)
    {
        NeqoOutput out{};
        int rc = neqo_bridge_process_output(m_conn, now, buf.data(), buf.size(), &out);
        if (rc != 0)
        {
            NS_LOG_WARN("neqo client: process_output returned " << rc);
            break;
        }
        if (out.tag == NEQO_OUT_DATAGRAM)
        {
            Ptr<Packet> pkt = Create<Packet>(buf.data(), out.len);
            m_socket->SendTo(pkt, 0, InetSocketAddress(Ipv4Address(out.dst_ip), out.dst_port));
            continue;
        }
        if (out.tag == NEQO_OUT_CALLBACK)
        {
            m_timer = Simulator::Schedule(NanoSeconds(out.callback_ns), &NeqoClientApp::Drive, this);
        }
        break;
    }
}

} // namespace neqo_ns3
} // namespace ns3