#ifndef NEQO_CLIENT_APP_H
#define NEQO_CLIENT_APP_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

#include <cstdint>
#include <vector>

struct NeqoBridgeConn;

namespace ns3
{
namespace neqo_ns3
{


class NeqoClientApp : public Application
{
  public:
    static TypeId GetTypeId();
    NeqoClientApp();
    ~NeqoClientApp() override;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;
    void HandleRead(Ptr<Socket> socket);
    void Drive();
    void ProcessEvents();
    void TrySend();

    Address m_remote;
    uint64_t m_maxBytes{1000000};

    Ptr<Socket> m_socket;
    NeqoBridgeConn* m_conn{nullptr};
    EventId m_timer;
    uint32_t m_localIp{0};
    uint16_t m_localPort{0};

    std::vector<uint8_t> m_payload;
    bool m_established{false};
    bool m_opened{false};
    bool m_finSent{false};
    bool m_done{false};
    uint64_t m_streamId{0};
    uint64_t m_sent{0};
    int64_t m_startNs{0};
};

} // namespace neqo_ns3
} // namespace ns3

#endif