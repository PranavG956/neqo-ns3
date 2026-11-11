/*
 * Chain with a bottleneck in the middle (ns-3 node ids 0..3):
 *
 *   n1 (client) ---- n2 ====== bottleneck ====== n3 ---- n4 (server)
 *        access           rate / delay / loss           access
 */
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/neqo-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;
using namespace ns3::neqo_ns3;

int
main(int argc, char* argv[])
{
    std::string accessRate = "100Mbps";
    std::string accessDelay = "2ms";
    std::string bottleneckRate = "10Mbps";
    std::string bottleneckDelay = "20ms";
    uint32_t queuePackets = 100;
    double loss = 0.0;
    uint64_t maxBytes = 1000000;
    double simTime = 30.0;
    uint32_t run = 1;
    bool enablePcap = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("accessRate", "Access link rate", accessRate);
    cmd.AddValue("accessDelay", "Access link one-way delay", accessDelay);
    cmd.AddValue("bottleneckRate", "Bottleneck link rate", bottleneckRate);
    cmd.AddValue("bottleneckDelay", "Bottleneck one-way delay", bottleneckDelay);
    cmd.AddValue("queuePackets", "Bottleneck FIFO size in packets", queuePackets);
    cmd.AddValue("loss", "Packet loss probability on the bottleneck (both directions)", loss);
    cmd.AddValue("maxBytes", "Bytes the client sends", maxBytes);
    cmd.AddValue("simTime", "Simulation time in seconds", simTime);
    cmd.AddValue("run", "ns-3 RNG run number (changes the loss pattern)", run);
    cmd.AddValue("pcap", "Capture every link", enablePcap);
    cmd.Parse(argc, argv);
    RngSeedManager::SetRun(run);

    NodeContainer n;
    n.Create(4);

    InternetStackHelper internet;
    internet.Install(n);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue(accessRate));
    access.SetChannelAttribute("Delay", StringValue(accessDelay));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckRate));
    bottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneck.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", StringValue("1p"));

    NetDeviceContainer d01 = access.Install(n.Get(0), n.Get(1));
    NetDeviceContainer d12 = bottleneck.Install(n.Get(1), n.Get(2));
    NetDeviceContainer d23 = access.Install(n.Get(2), n.Get(3));

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    ipv4.Assign(d01);
    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    ipv4.Assign(d12);
    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer if23 = ipv4.Assign(d23);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FifoQueueDisc",
                         "MaxSize",
                         StringValue(std::to_string(queuePackets) + "p"));
    tch.Uninstall(d12);
    tch.Install(d12);

    if (loss > 0)
    {
        for (uint32_t i = 0; i < 2; i++)
        {
            Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
            em->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
            em->SetAttribute("ErrorRate", DoubleValue(loss));
            d12.Get(i)->SetAttribute("ReceiveErrorModel", PointerValue(em));
        }
    }

    InetSocketAddress serverAddr(if23.GetAddress(1), 4433);

    NeqoServerHelper server(serverAddr);
    ApplicationContainer serverApps = server.Install(n.Get(3));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simTime));

    NeqoClientHelper client(serverAddr);
    client.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    ApplicationContainer clientApps = client.Install(n.Get(0));
    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(simTime));

    if (enablePcap)
    {
        access.EnablePcapAll("neqo-chain", true);
    }

    Time rtt = (Time(accessDelay) * 2 + Time(bottleneckDelay)) * 2;
    std::cout << "chain: bottleneck " << bottleneckRate << " / " << bottleneckDelay << ", loss "
              << loss << ", queue " << queuePackets << " pkts, base RTT " << rtt.GetMilliSeconds()
              << " ms" << std::endl;

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}