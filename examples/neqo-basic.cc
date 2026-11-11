// Node 0 (Client) ---- Node 1 (Router) ---- Node 2 (Server)
//            10 Mbps, 5 ms        10 Mbps, 5 ms
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/neqo-client-app.h"
#include "ns3/neqo-helper.h"
#include "ns3/neqo-server-app.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;
using namespace ns3::neqo_ns3;

int
main(int argc, char* argv[])
{
    bool enablePcap = true;
    CommandLine cmd;
    uint64_t maxBytes = 1000000;
    cmd.AddValue("maxBytes", "Bytes the client sends", maxBytes);
    cmd.AddValue("enablePcap", "Enable/disable PCAP capture", enablePcap);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(3);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));
    NetDeviceContainer clientRouter = p2p.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer routerServer = p2p.Install(nodes.Get(1), nodes.Get(2));
    if (enablePcap)
    {
        p2p.EnablePcapAll("neqo-basic", true);
    }

    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    ipv4.Assign(clientRouter);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer routerServerIf = ipv4.Assign(routerServer);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    InetSocketAddress serverAddr(routerServerIf.GetAddress(1), 4433);

    NeqoServerHelper server(serverAddr);
    ApplicationContainer serverApps = server.Install(nodes.Get(2));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(10.0));

    NeqoClientHelper client(serverAddr);
    client.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer clientApps = client.Install(nodes.Get(0));
    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(10.0));

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}