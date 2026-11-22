#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

#include <fstream>
#include <iostream>

using namespace ns3;

std::ofstream file;

void CwndChange(uint32_t oldCwnd, uint32_t newCwnd)
{
    file << Simulator::Now().GetSeconds() << " "
         << newCwnd / 1000.0 << std::endl;
}

void StartTrace()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
        MakeCallback(&CwndChange));
}

int main(int argc, char *argv[])
{
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpLinuxReno::GetTypeId()));

    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(1000));

    NodeContainer nodes;
    nodes.Create(4);

    PointToPointHelper normalLink;
    normalLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    normalLink.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper slowLink;
    slowLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    slowLink.SetChannelAttribute("Delay", StringValue("20ms"));
    slowLink.SetQueue("ns3::DropTailQueue<Packet>",
                      "MaxSize", StringValue("50p"));

    NetDeviceContainer link1 =
        normalLink.Install(nodes.Get(0), nodes.Get(1));

    NetDeviceContainer link2 =
        slowLink.Install(nodes.Get(1), nodes.Get(2));

    NetDeviceContainer link3 =
        normalLink.Install(nodes.Get(2), nodes.Get(3));

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(link1);

    address.SetBase("10.1.2.0", "255.255.255.0");
    address.Assign(link2);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer endAddress = address.Assign(link3);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t port = 50000;

    PacketSinkHelper sink(
        "ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));

    ApplicationContainer sinkApp = sink.Install(nodes.Get(3));

    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(60));

    BulkSendHelper send(
        "ns3::TcpSocketFactory",
        InetSocketAddress(endAddress.GetAddress(1), port));

    send.SetAttribute("MaxBytes", UintegerValue(0));

    ApplicationContainer sendApp = send.Install(nodes.Get(0));

    sendApp.Start(Seconds(1));
    sendApp.Stop(Seconds(60));

    file.open("cwnd-reno.dat");

    Simulator::Schedule(Seconds(1.01), &StartTrace);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(60));
    Simulator::Run();

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    for (auto flow = stats.begin(); flow != stats.end(); flow++)
    {
        Ipv4FlowClassifier::FiveTuple info =
            classifier->FindFlow(flow->first);

        if (info.destinationPort == port)
        {
            double time =
                flow->second.timeLastRxPacket.GetSeconds() -
                flow->second.timeFirstTxPacket.GetSeconds();

            double throughput =
                flow->second.rxBytes * 8.0 / time / 1000000.0;

            std::cout << "Reno Results" << std::endl;
            std::cout << "Throughput: " << throughput << " Mbps" << std::endl;
            std::cout << "Bytes received: " << flow->second.rxBytes << std::endl;
            std::cout << "Packets lost: " << flow->second.lostPackets << std::endl;
        }
    }

    file.close();

    Simulator::Destroy();

    return 0;
}
