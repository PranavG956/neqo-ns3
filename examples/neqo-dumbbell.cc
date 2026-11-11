/*
 * Dumbbell: --nFlows client/server pairs share one bottleneck link.
 *
 *  client0 --\                                    /-- server0
 *  client1 ----- R1 ===== bottleneck ===== R2 ----- server1
 *  clientN --/                                    \-- serverN
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/neqo-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <sstream>
#include <vector>

using namespace ns3;
using namespace ns3::neqo_ns3;

int
main(int argc, char* argv[])
{
    uint32_t nFlows = 2;
    std::string accessRate = "100Mbps";
    std::string accessDelay = "5ms";
    std::string bottleneckRate = "10Mbps";
    std::string bottleneckDelay = "20ms";
    uint32_t queuePackets = 100;
    uint64_t maxBytes = 5000000;
    double startGap = 0.0;
    double simTime = 30.0;
    bool enablePcap = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nFlows", "Number of client/server pairs", nFlows);
    cmd.AddValue("accessRate", "Access link rate", accessRate);
    cmd.AddValue("accessDelay", "Access link one-way delay", accessDelay);
    cmd.AddValue("bottleneckRate", "Bottleneck link rate", bottleneckRate);
    cmd.AddValue("bottleneckDelay", "Bottleneck one-way delay", bottleneckDelay);
    cmd.AddValue("queuePackets", "Bottleneck FIFO size in packets", queuePackets);
    cmd.AddValue("maxBytes", "Bytes each client sends", maxBytes);
    cmd.AddValue("startGap", "Seconds between the start of consecutive flows", startGap);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("pcap", "Capture the two bottleneck devices", enablePcap);
    cmd.Parse(argc, argv);

    NodeContainer senders;
    NodeContainer receivers;
    NodeContainer routers;
    senders.Create(nFlows);
    receivers.Create(nFlows);
    routers.Create(2);

    InternetStackHelper internet;
    internet.Install(senders);
    internet.Install(receivers);
    internet.Install(routers);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue(accessRate));
    access.SetChannelAttribute("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckRate));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    // Keep the device queue tiny so the FIFO installed below is the real buffer.
    bottleneck.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("1p"));

    Ipv4AddressHelper ipv4;
    NetDeviceContainer bn = bottleneck.Install(routers.Get(0), routers.Get(1));
    ipv4.SetBase("10.0.0.0", "255.255.255.0");
    ipv4.Assign(bn);

    std::vector<Ipv4Address> serverIps(nFlows);
    for (uint32_t i = 0; i < nFlows; i++)
    {
        NetDeviceContainer left = access.Install(senders.Get(i), routers.Get(0));
        std::ostringstream a;
        a << "10.1." << i + 1 << ".0";
        ipv4.SetBase(a.str().c_str(), "255.255.255.0");
        ipv4.Assign(left);

        NetDeviceContainer right = access.Install(routers.Get(1), receivers.Get(i));
        std::ostringstream b;
        b << "10.2." << i + 1 << ".0";
        ipv4.SetBase(b.str().c_str(), "255.255.255.0");
        Ipv4InterfaceContainer rif = ipv4.Assign(right);
        serverIps[i] = rif.GetAddress(1);
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Explicit FIFO on the bottleneck (replaces any default queue disc).
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc",
                         "MaxSize",
                         StringValue(std::to_string(queuePackets) + "p"));
    tch.Uninstall(bn);
    tch.Install(bn);

    for (uint32_t i = 0; i < nFlows; i++)
    {
        InetSocketAddress serverAddr(serverIps[i], 4433);

        NeqoServerHelper server(serverAddr);
        ApplicationContainer serverApps = server.Install(receivers.Get(i));
        serverApps.Start(Seconds(0.0));
        serverApps.Stop(Seconds(simTime));

        NeqoClientHelper client(serverAddr);
        client.SetAttribute("MaxBytes", UintegerValue(maxBytes));
        ApplicationContainer clientApps = client.Install(senders.Get(i));
        clientApps.Start(Seconds(1.0 + i * startGap));
        clientApps.Stop(Seconds(simTime));
    }

    if (enablePcap)
    {
        bottleneck.EnablePcap("neqo-dumbbell-bottleneck", bn, true);
    }

    Time rtt = (Time(accessDelay) * 2 + Time(bottleneckDelay)) * 2;
    std::cout << "dumbbell: " << nFlows << " flows, bottleneck " << bottleneckRate << " / "
              << bottleneckDelay << ", queue " << queuePackets << " pkts, base RTT "
              << rtt.GetMilliSeconds() << " ms (clients are nodes 0.." << nFlows - 1
              << ", servers " << nFlows << ".." << 2 * nFlows - 1 << ")" << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}