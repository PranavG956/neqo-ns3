#include "neqo-server-app.h"

#include "neqo_ns3_bridge.h"

#include "ns3/address-utils.h"
#include "ns3/inet-socket-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

#include <iostream>
#include <vector>

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("NeqoServerApp");
namespace neqo_ns3
{
NS_OBJECT_ENSURE_REGISTERED(NeqoServerApp);

TypeId
NeqoServerApp::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::neqo_ns3::NeqoServerApp")
            .SetParent<Application>()
            .SetGroupName("Neqo")
            .AddConstructor<NeqoServerApp>()
            .AddAttribute("ListenAddress",
                          "InetSocketAddress to listen on (must be a concrete IPv4 address)",
                          AddressValue(),
                          MakeAddressAccessor(&NeqoServerApp::m_listen),
                          MakeAddressChecker());
    return tid;
}

NeqoServerApp::NeqoServerApp() = default;
NeqoServerApp::~NeqoServerApp() = default;

void
NeqoServerApp::DoDispose()
{
    m_socket = nullptr;
    Application::DoDispose();
}

void
NeqoServerApp::StartApplication()
{
    m_conn = neqo_bridge_server_new();
    NS_ABORT_MSG_IF(!m_conn, "neqo_bridge_server_new failed");

    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    NS_ABORT_MSG_IF(m_socket->Bind(m_listen) != 0, "server: bind failed");
    m_socket->SetRecvCallback(MakeCallback(&NeqoServerApp::HandleRead, this));
    NS_LOG_INFO("neqo server listening");
}

void
NeqoServerApp::StopApplication()
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
NeqoServerApp::HandleRead(Ptr<Socket> socket)
{
    Address from;
    while (Ptr<Packet> p = socket->RecvFrom(from))
    {
        if (!m_conn || p->GetSize() == 0)
        {
            continue;
        }
        InetSocketAddress src = InetSocketAddress::ConvertFrom(from);
        InetSocketAddress me = InetSocketAddress::ConvertFrom(m_listen);
        std::vector<uint8_t> buf(p->GetSize());
        p->CopyData(buf.data(), buf.size());
        int rc = neqo_bridge_process_input(m_conn,
                                           buf.data(),
                                           buf.size(),
                                           src.GetIpv4().Get(),
                                           src.GetPort(),
                                           me.GetIpv4().Get(),
                                           me.GetPort(),
                                           Simulator::Now().GetNanoSeconds());
        if (rc != 0)
        {
            NS_LOG_WARN("neqo server: process_input returned " << rc);
        }
    }
    Drive();
}

void
NeqoServerApp::ProcessEvents()
{
    NeqoEvent ev;
    while (neqo_bridge_next_event(m_conn, &ev) == 1)
    {
        if (ev.tag != NEQO_EV_STREAM_READABLE || m_finished.count(ev.stream_id))
        {
            continue;
        }
        std::vector<uint8_t> rbuf(65536);
        for (;;)
        {
            int fin = 0;
            int64_t n = neqo_bridge_stream_recv(m_conn, ev.stream_id, rbuf.data(), rbuf.size(), &fin);
            if (n < 0)
            {
                break; // stream already finished; not fatal
            }
            m_rx[ev.stream_id] += n;
            if (fin == 1)
            {
                m_finished.insert(ev.stream_id);
                std::cout << "[server n" << GetNode()->GetId() << "] stream " << ev.stream_id << ": received "
                          << m_rx[ev.stream_id] << " bytes (FIN) at t="
                          << Simulator::Now().GetMilliSeconds() << " ms" << std::endl;
                const uint8_t reply[2] = {'O', 'K'};
                neqo_bridge_stream_send(m_conn, ev.stream_id, reply, sizeof(reply));
                neqo_bridge_stream_close_send(m_conn, ev.stream_id);
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
NeqoServerApp::Drive()
{
    if (!m_conn)
    {
        return;
    }
    m_timer.Cancel();
    const uint64_t now = Simulator::Now().GetNanoSeconds();

    if (!m_reported && neqo_bridge_state(m_conn) == NEQO_STATE_CONFIRMED)
    {
        m_reported = true;
        std::cout << "[server n" << GetNode()->GetId() << "] handshake confirmed at t=" << Simulator::Now().GetMilliSeconds()
                  << " ms" << std::endl;
    }

    ProcessEvents();

    std::vector<uint8_t> buf(2048);
    for (int i = 0; i < 10000; i++)
    {
        NeqoOutput out{};
        int rc = neqo_bridge_process_output(m_conn, now, buf.data(), buf.size(), &out);
        if (rc != 0)
        {
            NS_LOG_WARN("neqo server: process_output returned " << rc);
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
            m_timer = Simulator::Schedule(NanoSeconds(out.callback_ns), &NeqoServerApp::Drive, this);
        }
        break;
    }

    if (!m_closeReported && neqo_bridge_state(m_conn) == NEQO_STATE_CLOSED)
    {
        m_closeReported = true;
        std::cout << "[server n" << GetNode()->GetId() << "] connection closed at t=" << Simulator::Now().GetMilliSeconds()
                  << " ms" << std::endl;
    }
}

} // namespace neqo_ns3
} // namespace ns3