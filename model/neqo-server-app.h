#ifndef NEQO_SERVER_APP_H
#define NEQO_SERVER_APP_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include <map>
#include <set>

struct NeqoBridgeConn;

namespace ns3
{
namespace neqo_ns3
{

class NeqoServerApp : public Application
{
  public:
    static TypeId GetTypeId();
    NeqoServerApp();
    ~NeqoServerApp() override;

  protected:
    void DoDispose() override;

  private:
    void StartApplication() override;
    void StopApplication() override;
    void HandleRead(Ptr<Socket> socket);
    void Drive();
    void ProcessEvents();

    Address m_listen;
    Ptr<Socket> m_socket;
    NeqoBridgeConn* m_conn{nullptr};
    EventId m_timer;
    bool m_reported{false};
    bool m_closeReported{false};
    std::map<uint64_t, uint64_t> m_rx; // stream id -> bytes received
    std::set<uint64_t> m_finished;
};

} // namespace neqo_ns3
} // namespace ns3

#endif