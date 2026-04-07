#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BurstyUdpTopology");



static void SetTcpVariant(const std::string& tcpVariant)
{
    if (tcpVariant == "Reno")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           StringValue("ns3::TcpNewReno"));
        Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(false));
    }
    else if (tcpVariant == "Sack")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           StringValue("ns3::TcpNewReno"));
        Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    }
    else if (tcpVariant == "FR")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           StringValue("ns3::TcpFr"));
        Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    }
    else if (tcpVariant == "GFR")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           StringValue("ns3::TcpFrGfr"));
        Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    }
    else if (tcpVariant == "JA-GFR")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           StringValue("ns3::TcpFrGfrJitter"));
        Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(true));
    }
    else
    {
        NS_FATAL_ERROR("Unknown TCP variant: " << tcpVariant
                       << ". Choose Reno|Sack|FR|GFR|JA-GFR");
    }
}

static std::vector<double>
ParseDoubleList(const std::string& csv)
{
    std::vector<double> values;
    std::stringstream ss(csv);
    std::string token;

    while (std::getline(ss, token, ','))
    {
        token.erase(std::remove_if(token.begin(),
                                   token.end(),
                                   [](unsigned char c) { return std::isspace(c) != 0; }),
                    token.end());
        if (token.empty())
        {
            continue;
        }

        try
        {
            values.push_back(std::stod(token));
        }
        catch (const std::exception&)
        {
            NS_FATAL_ERROR("Invalid numeric value in list: " << token);
        }
    }

    return values;
}

static void
ConfigureRecoveryFactor(uint32_t numConn)
{
    // The paper uses a=1 for few flows and a=2 for many flows.
    const uint32_t recoveryFactorA = (numConn >= 10) ? 2u : 1u;
    Config::SetDefault("ns3::TcpFr::RecoveryFactorA", UintegerValue(recoveryFactorA));
    Config::SetDefault("ns3::TcpFrGfr::RecoveryFactorA", UintegerValue(recoveryFactorA));
    Config::SetDefault("ns3::TcpFrGfrJitter::RecoveryFactorA", UintegerValue(recoveryFactorA));
}

static double BuildTopology(uint32_t           numConn,
                           double             bottleneckBw,
                           double             Rtt_s,
                           const std::string& tcpVariant,
                           double             simTime = 10.0)
{
    ConfigureRecoveryFactor(numConn);
    SetTcpVariant(tcpVariant);

    // ns-2 did not impose the small default socket-buffer ceiling that ns-3 has.
    // Keep the socket buffers fixed across scenarios so Fig. 3 and Fig. 4 vary
    // only the intended experimental parameter, while the bottleneck queue
    // still tracks the scenario BDP as described in the paper. Use the paper's
    // baseline scenario (45 Mb/s, 0.5 s RTT) BDP as the fixed socket-buffer size.
    uint32_t bdpBytes = static_cast<uint32_t>(bottleneckBw * 1e6 * Rtt_s / 8.0);
    const uint32_t fixedSocketBufBytes = 10u * 1024u * 1024u;
    uint32_t bufSize  = fixedSocketBufBytes;
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(bufSize));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(bufSize));

    Config::SetDefault("ns3::TcpSocket::SegmentSize",   UintegerValue(1448));//Matches 1500-byte MTU minus IP/TCP/Timestamp headers.
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",   UintegerValue(4));
    // paper's "fill the pipe" startup 
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold",
                       UintegerValue(bdpBytes));

    NS_LOG_UNCOND("[CONFIG] variant=" << tcpVariant
                  << "  SndBufSize=" << bufSize << " B"
                  << "  BDP=" << bdpBytes << " B"
                  << "  a=" << ((numConn >= 10) ? 2 : 1)
                  << "  bottleneckBw=" << bottleneckBw << " Mbps"
                  << "  Rtt=" << Rtt_s << " s");

    NodeContainer senders, receivers, routers, udpNodes;
    senders  .Create(numConn);
    receivers.Create(numConn);
    routers  .Create(2);       // R1, R2
    udpNodes .Create(2);       // udpSrc, udpSink

    Ptr<Node> R1 = routers.Get(0);
    Ptr<Node> R2 = routers.Get(1);

    InternetStackHelper stack;
    stack.Install(senders);
    stack.Install(receivers);
    stack.Install(routers);
    stack.Install(udpNodes);   

    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute ("DataRate", StringValue("1Gbps"));
    accessLink.SetChannelAttribute("Delay",    StringValue("0.001ms"));

    
    double   bandwidthDelayProduct_Bytes  = (bottleneckBw * 1e6 * Rtt_s) / 8.0;
    uint32_t queuePkts = std::max((uint32_t)(bandwidthDelayProduct_Bytes / 1500.0), (uint32_t)10);

    
    double propDelay_ms = (Rtt_s * 1000.0) / 2.0;

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute ("DataRate", StringValue(std::to_string(bottleneckBw) + "Mbps"));
    bottleneck.SetChannelAttribute("Delay",    StringValue(std::to_string(propDelay_ms) + "ms"));
    bottleneck.SetQueue("ns3::DropTailQueue",
                        "MaxSize",
                        QueueSizeValue(QueueSize(QueueSizeUnit::PACKETS, queuePkts)));

    
    std::vector<NetDeviceContainer> senderDevs(numConn);
    for (uint32_t i = 0; i < numConn; ++i)
        senderDevs[i] = accessLink.Install(senders.Get(i), R1);

    NetDeviceContainer bottleneckDevs = bottleneck.Install(R1, R2);

    std::vector<NetDeviceContainer> receiverDevs(numConn);
    for (uint32_t i = 0; i < numConn; ++i)
        receiverDevs[i] = accessLink.Install(R2, receivers.Get(i));

    
    NetDeviceContainer udpSrcDevs  = accessLink.Install(udpNodes.Get(0), R1);
    NetDeviceContainer udpSinkDevs = accessLink.Install(R2, udpNodes.Get(1));

    Ipv4AddressHelper ipv4;

    std::vector<Ipv4InterfaceContainer> senderIfaces(numConn);
    for (uint32_t i = 0; i < numConn; ++i)
    {
        std::ostringstream s;
        s << "10.1." << (i + 1) << ".0";
        ipv4.SetBase(s.str().c_str(), "255.255.255.252");
        senderIfaces[i] = ipv4.Assign(senderDevs[i]);
    }

    ipv4.SetBase("10.2.1.0", "255.255.255.252");
    ipv4.Assign(bottleneckDevs);

    std::vector<Ipv4InterfaceContainer> receiverIfaces(numConn);
    for (uint32_t i = 0; i < numConn; ++i)
    {
        std::ostringstream s;
        s << "10.3." << (i + 1) << ".0";
        ipv4.SetBase(s.str().c_str(), "255.255.255.252");
        receiverIfaces[i] = ipv4.Assign(receiverDevs[i]);
    }

    ipv4.SetBase("10.4.1.0", "255.255.255.252");
    ipv4.Assign(udpSrcDevs);

    ipv4.SetBase("10.5.1.0", "255.255.255.252");
    Ipv4InterfaceContainer udpSinkIfaces = ipv4.Assign(udpSinkDevs);
    Ipv4Address udpSinkAddr = udpSinkIfaces.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t tcpPortBase = 5000;

    std::vector<Ptr<PacketSink>> tcpSinks;
    tcpSinks.reserve(numConn);
    for (uint32_t i = 0; i < numConn; ++i)
    {
        uint16_t port = tcpPortBase + i;
        PacketSinkHelper sink("ns3::TcpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer a = sink.Install(receivers.Get(i));
        a.Start(Seconds(0.0));
        a.Stop (Seconds(simTime));

        Ptr<PacketSink> tcpSink = DynamicCast<PacketSink>(a.Get(0));
        NS_ABORT_MSG_IF(!tcpSink, "TCP sink app is not a PacketSink");
        tcpSinks.push_back(tcpSink);
    }

    // TCP bulk senders 
    for (uint32_t i = 0; i < numConn; ++i)
    {
        uint16_t port = tcpPortBase + i;
        BulkSendHelper bulk("ns3::TcpSocketFactory",
                             InetSocketAddress(receiverIfaces[i].GetAddress(1), port));
        bulk.SetAttribute("MaxBytes", UintegerValue(0));
        ApplicationContainer a = bulk.Install(senders.Get(i));
        a.Start(Seconds(1.0));
        a.Stop (Seconds(simTime - 1.0));
    }

    // On/Off UDP interference (90% BW, 20 s On / 20 s Off)
    uint16_t udpPort     = 9999;
    double   udpRateMbps = 0.9 * bottleneckBw;

    PacketSinkHelper udpSink("ns3::UdpSocketFactory",
                              InetSocketAddress(Ipv4Address::GetAny(), udpPort));
    ApplicationContainer udpSinkApp = udpSink.Install(udpNodes.Get(1));
    udpSinkApp.Start(Seconds(0.0));
    udpSinkApp.Stop (Seconds(simTime));

    OnOffHelper onoff("ns3::UdpSocketFactory",
                       InetSocketAddress(udpSinkAddr, udpPort));
    onoff.SetAttribute("DataRate", StringValue(std::to_string(udpRateMbps) + "Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1448));
    onoff.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=20]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=20]"));

    ApplicationContainer udpSrcApp = onoff.Install(udpNodes.Get(0));
    udpSrcApp.Start(Seconds(0.0));
    udpSrcApp.Stop (Seconds(simTime - 1.0));

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    double   totalGoodput = 0.0;
    uint32_t tcpFlows     = 0;
    const double tcpMeasurementDuration = std::max(simTime - 2.0 + (Rtt_s / 2.0), 1e-9);

    for (const auto& tcpSink : tcpSinks)
    {
        uint64_t rxBytes = tcpSink->GetTotalRx();
        if (rxBytes == 0)
        {
            continue;
        }

        totalGoodput += (rxBytes * 8.0) / (tcpMeasurementDuration * 1e6);
        ++tcpFlows;
    }

    Simulator::Destroy();

    return (tcpFlows > 0) ? (totalGoodput / tcpFlows) : 0.0;
}

int main(int argc, char* argv[])
{
    std::string experiment  = "connections";
    std::string tcpVariant  = "Reno";
    uint32_t    numConn     = 10;
    double      bottleneckBw= 45.0;
    double      Rtt   = 0.5;
    double      simTime     = 200.0;
    std::string connectionListArg;
    std::string bandwidthList;
    std::string rttListArg;
    std::string outputFile  = "result.csv";

    CommandLine cmd;
    cmd.AddValue("experiment",     "connections|bandwidth|rtt", experiment);
    cmd.AddValue("tcpVariant",     "Reno|Sack|FR|GFR|JA-GFR",   tcpVariant);
    cmd.AddValue("numConnections", "Number of TCP connections",  numConn);
    cmd.AddValue("bottleneckBw",   "Bottleneck BW in Mb/s",      bottleneckBw);
    cmd.AddValue("Rtt",      "Round-trip time in seconds",     Rtt);
    cmd.AddValue("simTime",        "Simulation time (s)",        simTime);
    cmd.AddValue("connectionList",
                 "Comma-separated connection counts for the connections sweep",
                 connectionListArg);
    cmd.AddValue("bandwidthList",
                 "Comma-separated bandwidth list for the bandwidth sweep",
                 bandwidthList);
    cmd.AddValue("rttList",
                 "Comma-separated RTT list for the RTT sweep",
                 rttListArg);
    cmd.AddValue("outputFile",     "Output CSV path",            outputFile);
    cmd.Parse(argc, argv);

    std::ofstream out(outputFile, std::ios::app);
    if (!out.is_open())
        NS_FATAL_ERROR("Cannot open: " << outputFile);

    NS_LOG_UNCOND("[START] experiment=" << experiment
                  << " variant=" << tcpVariant
                  << " numConn=" << numConn
                  << " bw=" << bottleneckBw << "Mbps"
                  << " rtt=" << Rtt << "s"
                  << " simTime=" << simTime << "s");

    if (experiment == "connections")
    {
        std::vector<uint32_t> connList = {1, 5, 10, 20, 30};
        if (!connectionListArg.empty())
        {
            std::vector<double> parsed = ParseDoubleList(connectionListArg);
            connList.clear();
            connList.reserve(parsed.size());
            for (double value : parsed)
            {
                NS_ABORT_MSG_IF(value <= 0.0, "connectionList values must be positive");
                connList.push_back(static_cast<uint32_t>(value));
            }
            NS_ABORT_MSG_IF(connList.empty(), "connectionList did not contain any usable values");
        }
        for (uint32_t n : connList)
        {
            RngSeedManager::SetSeed(42 + n);
            double goodput = BuildTopology(n, bottleneckBw, Rtt, tcpVariant, simTime);
            out << tcpVariant << "," << n << "," << goodput << "\n";
            out.flush();
            NS_LOG_UNCOND("[RESULT] Connections=" << n << " Goodput=" << goodput << " Mb/s");
        }
    }
    else if (experiment == "bandwidth")
    {
        std::vector<double> bwList = {1, 45, 150};
        if (!bandwidthList.empty())
        {
            bwList = ParseDoubleList(bandwidthList);
            NS_ABORT_MSG_IF(bwList.empty(), "bandwidthList did not contain any usable values");
        }
        for (double bw : bwList)
        {
            RngSeedManager::SetSeed(42);
            double goodput = BuildTopology(numConn, bw, Rtt, tcpVariant, simTime);
            out << tcpVariant << "," << bw << "," << goodput << "\n";
            out.flush();
            NS_LOG_UNCOND("[RESULT] BW=" << bw << "Mbps Goodput=" << goodput << " Mb/s");
        }
    }
    else if (experiment == "rtt")
    {
        std::vector<double> rttList = {0.01, 0.05, 0.1, 0.5, 1.0};
        if (!rttListArg.empty())
        {
            rttList = ParseDoubleList(rttListArg);
            NS_ABORT_MSG_IF(rttList.empty(), "rttList did not contain any usable values");
        }
        for (double rtt : rttList)
        {
            RngSeedManager::SetSeed(42);
            double goodput = BuildTopology(numConn, bottleneckBw, rtt, tcpVariant, simTime);
            out << tcpVariant << "," << rtt << "," << goodput << "\n";
            out.flush();
            NS_LOG_UNCOND("[RESULT] RTT=" << rtt << "s Goodput=" << goodput << " Mb/s");
        }
    }
    else
    {
        double goodput = BuildTopology(numConn, bottleneckBw, Rtt, tcpVariant, simTime);
        out << tcpVariant << "," << numConn << "," << bottleneckBw << ","
            << Rtt  << "," << goodput << "\n";
        NS_LOG_UNCOND("[RESULT] Goodput=" << goodput << " Mb/s");
    }

    out.close();
    return 0;
}
