#include "neqo-link-test.h"

#include "neqo_ns3_bridge.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace ns3
{

bool
NeqoRunInProcessHandshake()
{
    const uint32_t cIp = 0x0a000002;
    const uint32_t sIp = 0x0a000001;
    const uint16_t cPort = 5000;
    const uint16_t sPort = 4433;

    NeqoBridgeConn* c = neqo_bridge_client_new(cIp, cPort, sIp, sPort, 0);
    NeqoBridgeConn* s = neqo_bridge_server_new();
    if (!c || !s)
    {
        std::cout << "neqo: could not create connections" << std::endl;
        return false;
    }

    std::vector<uint8_t> buf(2048);
    uint64_t now = 0;
    int datagrams = 0;
    for (int i = 0; i < 5000; i++)
    {
        for (int dir = 0; dir < 2; dir++)
        {
            NeqoBridgeConn* from = dir == 0 ? c : s;
            NeqoBridgeConn* to = dir == 0 ? s : c;
            uint32_t fIp = dir == 0 ? cIp : sIp;
            uint32_t tIp = dir == 0 ? sIp : cIp;
            uint16_t fPort = dir == 0 ? cPort : sPort;
            uint16_t tPort = dir == 0 ? sPort : cPort;
            for (;;)
            {
                NeqoOutput out{};
                if (neqo_bridge_process_output(from, now, buf.data(), buf.size(), &out) != 0)
                {
                    return false;
                }
                if (out.tag != NEQO_OUT_DATAGRAM)
                {
                    break;
                }
                datagrams++;
                neqo_bridge_process_input(to, buf.data(), out.len, fIp, fPort, tIp, tPort, now);
            }
        }
        now += 1000000;
        if (neqo_bridge_state(c) == NEQO_STATE_CONFIRMED &&
            neqo_bridge_state(s) == NEQO_STATE_CONFIRMED)
        {
            break;
        }
    }

    bool ok = neqo_bridge_state(c) == NEQO_STATE_CONFIRMED &&
              neqo_bridge_state(s) == NEQO_STATE_CONFIRMED;
    std::cout << (ok ? "neqo handshake OK, datagrams: " : "neqo handshake FAILED, datagrams: ")
              << datagrams << std::endl;
    neqo_bridge_free(c);
    neqo_bridge_free(s);
    return ok;
}

} // namespace ns3
