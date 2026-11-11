#include "neqo_ns3_bridge.h"
#include <cstdint>
#include <cstdio>
#include <vector>

struct Peer {
    NeqoBridgeConn* conn;
    uint32_t ip;
    uint16_t port;
};

int main() {
    const uint32_t CLIENT_IP = 0x0a000002, SERVER_IP = 0x0a000001;  // 10.0.0.2 / 10.0.0.1
    const uint16_t CLIENT_PORT = 5000, SERVER_PORT = 4433;

    Peer client{neqo_bridge_client_new(CLIENT_IP, CLIENT_PORT, SERVER_IP, SERVER_PORT, 0),
                CLIENT_IP, CLIENT_PORT};
    Peer server{neqo_bridge_server_new(), SERVER_IP, SERVER_PORT};
    if (!client.conn || !server.conn) {
        std::printf("FAILED: could not create connections\n");
        return 1;
    }

    std::vector<uint8_t> buf(2048);
    uint64_t now = 0;
    int datagrams = 0;

    for (int i = 0; i < 5000; i++) {
        Peer* pairs[2][2] = {{&client, &server}, {&server, &client}};
        for (auto& p : pairs) {
            Peer* from = p[0];
            Peer* to = p[1];
            for (;;) {
                NeqoOutput out{};
                int rc = neqo_bridge_process_output(from->conn, now, buf.data(), buf.size(), &out);
                if (rc != 0) { std::printf("process_output failed: %d\n", rc); return 1; }
                if (out.tag != NEQO_OUT_DATAGRAM) break;
                datagrams++;
                rc = neqo_bridge_process_input(to->conn, buf.data(), out.len,
                                               from->ip, from->port, to->ip, to->port, now);
                if (rc != 0) { std::printf("process_input failed: %d\n", rc); return 1; }
            }
        }
        now += 1000000;  // 1 ms of simulated time
        if (neqo_bridge_state(client.conn) == NEQO_STATE_CONFIRMED &&
            neqo_bridge_state(server.conn) == NEQO_STATE_CONFIRMED)
            break;
    }

    bool ok = neqo_bridge_state(client.conn) == NEQO_STATE_CONFIRMED &&
              neqo_bridge_state(server.conn) == NEQO_STATE_CONFIRMED;
    std::printf("%s: %d datagrams, %llu ms simulated\n",
                ok ? "HANDSHAKE OK" : "HANDSHAKE FAILED", datagrams,
                (unsigned long long)(now / 1000000));
    neqo_bridge_free(client.conn);
    neqo_bridge_free(server.conn);
    return ok ? 0 : 1;
}
