#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::energy;

NS_LOG_COMPONENT_DEFINE("WifiMobileChecklist");

namespace
{

struct ExperimentConfig
{
    std::string mobilityMode{"mobile"};
    std::string tcpVariant{"TcpFr"};
    std::string variantLabel;
    uint32_t nNodes{20};
    uint32_t nFlows{10};
    uint32_t packetsPerSecond{100};
    uint32_t packetSizeBytes{512};
    double nodeSpeedMps{10.0};
    double pauseTimeSeconds{0.0};
    double txRangeMeters{250.0};
    double areaScale{3.0};
    double simulationTimeSeconds{40.0};
    double appStartSeconds{5.0};
    double stopMarginSeconds{2.0};
    double initialEnergyJ{10000.0};
    double supplyVoltageV{3.0};
    double jitterThresholdMs{3.0};
    uint32_t run{1};
    std::string phyMode{"ErpOfdmRate24Mbps"};
    std::string summaryFile;
};

struct FlowSpec
{
    uint32_t sourceNode;
    uint32_t destinationNode;
    uint16_t port;
};

struct AggregateFlowStats
{
    uint64_t txBytes{0};
    uint64_t rxBytes{0};
    uint64_t txPackets{0};
    uint64_t rxPackets{0};
    uint64_t lostPackets{0};
    uint64_t timesForwarded{0};
    Time delaySum{Seconds(0)};
    Time jitterSum{Seconds(0)};
};

Ptr<PositionAllocator>
CreateRandomPositionAllocator(double fieldSideMeters)//random node position allocator create korbe
{
    std::ostringstream fieldSideStream;
    fieldSideStream << std::fixed << std::setprecision(6) << fieldSideMeters;
    const std::string fieldSide = fieldSideStream.str();

    ObjectFactory positionFactory;
    positionFactory.SetTypeId("ns3::RandomRectanglePositionAllocator");
    positionFactory.Set("X",StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +fieldSide + "]"));
    positionFactory.Set("Y",StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +fieldSide + "]"));
    return positionFactory.Create()->GetObject<PositionAllocator>();
}

void
InstallMobility(const NodeContainer& nodes, const ExperimentConfig& cfg, double fieldSideMeters)
{
    MobilityHelper mobility;
    Ptr<PositionAllocator> positionAllocator = CreateRandomPositionAllocator(fieldSideMeters);
    mobility.SetPositionAllocator(positionAllocator);

    std::ostringstream nodeSpeedStream;
    nodeSpeedStream << std::fixed << std::setprecision(6) << cfg.nodeSpeedMps;
    const std::string nodeSpeed = nodeSpeedStream.str();

    std::ostringstream pauseTimeStream;
    pauseTimeStream << std::fixed << std::setprecision(6) << cfg.pauseTimeSeconds;
    const std::string pauseTime = pauseTimeStream.str();

    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel","Speed", StringValue("ns3::ConstantRandomVariable[Constant=" + nodeSpeed + "]"),"Pause", StringValue("ns3::ConstantRandomVariable[Constant=" +pauseTime + "]"),"PositionAllocator", PointerValue(positionAllocator));

    mobility.Install(nodes);
}

std::vector<FlowSpec>
BuildFlowSpecs(uint32_t nNodes, uint32_t nFlows, uint16_t basePort)//random unique flow pair banay
{
    Ptr<UniformRandomVariable> nodeRv = CreateObject<UniformRandomVariable>();
    std::set<uint64_t> usedPairs;
    std::vector<FlowSpec> flows;
    flows.reserve(nFlows);
    while (flows.size() < nFlows)
    {
        const uint32_t source = nodeRv->GetInteger(0, nNodes - 1);
        const uint32_t destination = nodeRv->GetInteger(0, nNodes - 1);
        if (source == destination)
        {
            continue;
        }
        const uint64_t key = (static_cast<uint64_t>(source) << 32) | destination;
        if (!usedPairs.insert(key).second)
        {
            continue;
        }
        flows.push_back(
            {
                source,
                destination,
                static_cast<uint16_t>(basePort + flows.size()),// proti flow alada port
            });
    }
    return flows;
}

AggregateFlowStats
CollectFlowStats(Ptr<FlowMonitor> monitor,
                 Ptr<Ipv4FlowClassifier> classifier,
                 uint16_t basePort,
                 uint32_t nFlows)
{
    AggregateFlowStats aggregate;

    for (const auto& [flowId, stats] : monitor->GetFlowStats())
    {
        const auto tuple = classifier->FindFlow(flowId);
        if (tuple.protocol != 6)// Only count TCP flows
        {
            continue;
        }
        if (tuple.destinationPort < basePort || tuple.destinationPort >= basePort + nFlows)// Only count flows in the expected port range
        {
            continue;
        }

        aggregate.txBytes += stats.txBytes;
        aggregate.rxBytes += stats.rxBytes;
        aggregate.txPackets += stats.txPackets;
        aggregate.rxPackets += stats.rxPackets;
        aggregate.lostPackets += stats.lostPackets;
        aggregate.timesForwarded += stats.timesForwarded;
        aggregate.delaySum += stats.delaySum;
        aggregate.jitterSum += stats.jitterSum;
    }

    return aggregate;
}

double
SumConsumedEnergy(const EnergySourceContainer& energySources)
{
    double totalEnergyConsumedJ = 0.0;
    for (uint32_t i = 0; i < energySources.GetN(); ++i)
    {
        Ptr<EnergySource> source = energySources.Get(i);
        source->UpdateEnergySource();
        totalEnergyConsumedJ += source->GetInitialEnergy() - source->GetRemainingEnergy();
    }
    return totalEnergyConsumedJ;
}
} 

int
main(int argc, char* argv[])
{
    ExperimentConfig cfg;
    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "TCP variant TypeId or short name", cfg.tcpVariant);
    cmd.AddValue("nNodes", "Number of Wi-Fi nodes", cfg.nNodes);
    cmd.AddValue("nFlows", "Number of TCP flows", cfg.nFlows);
    cmd.AddValue("packetsPerSecond","Packets per second mapped to offered application load per flow",cfg.packetsPerSecond);
    cmd.AddValue("nodeSpeed", "Node speed in m/s for mobile runs", cfg.nodeSpeedMps);
    cmd.AddValue("simulationTime", "Total simulation time in seconds", cfg.simulationTimeSeconds);
    cmd.Parse(argc, argv);

    const double appStopSeconds = cfg.simulationTimeSeconds - cfg.stopMarginSeconds;
    if (cfg.variantLabel.empty())
    {
        cfg.variantLabel = cfg.tcpVariant;
    }

    const double fieldSideMeters = cfg.areaScale * cfg.txRangeMeters;
    const uint16_t basePort = 9000;
    const uint64_t flowBitRate =
        static_cast<uint64_t>(cfg.packetSizeBytes * 8.0 * cfg.packetsPerSecond);
    const std::string tcpTypeName = (cfg.tcpVariant.rfind("ns3::", 0) == 0) ? cfg.tcpVariant : "ns3::" + cfg.tcpVariant;
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(tcpTypeName, &tcpTid),"TCP TypeId " << tcpTypeName << " not found");
    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(cfg.run);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(tcpTid));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(cfg.packetSizeBytes));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(false));
    Config::SetDefault("ns3::TcpFrGfrJitter::JitterThreshold",TimeValue(MilliSeconds(cfg.jitterThresholdMs)));
    Config::SetDefault("ns3::WifiRemoteStationManager::FragmentationThreshold",StringValue("2200"));//pktsize threshold r niche thakle mac frame vangbe na
    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue("2200"));//pktsize threshold r niche thakle RTS/CTS use hobe na
    Config::SetDefault("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue(cfg.phyMode));// shob node same phy mode use korbe

    NodeContainer nodes;
    nodes.Create(cfg.nNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue(cfg.phyMode),
                                 "ControlMode",
                                 StringValue(cfg.phyMode));

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel",
                                   "MaxRange",
                                   DoubleValue(cfg.txRangeMeters));

    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(wifiChannel.Create());

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);

    InstallMobility(nodes, cfg, fieldSideMeters);

    AodvHelper aodv;
    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

    BasicEnergySourceHelper basicEnergyHelper;
    basicEnergyHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(cfg.initialEnergyJ));
    basicEnergyHelper.Set("BasicEnergySupplyVoltageV", DoubleValue(cfg.supplyVoltageV));
    basicEnergyHelper.Set("PeriodicEnergyUpdateInterval", TimeValue(Seconds(1.0)));
    EnergySourceContainer energySources = basicEnergyHelper.Install(nodes);

    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Install(devices, energySources);

    const std::vector<FlowSpec> flows = BuildFlowSpecs(cfg.nNodes, cfg.nFlows, basePort);
    for (uint32_t i = 0; i < flows.size(); ++i)
    {
        const FlowSpec& flow = flows[i];

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), flow.port));
        ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(flow.destinationNode));
        sinkApps.Start(Seconds(0.0));
        sinkApps.Stop(Seconds(cfg.simulationTimeSeconds));

        OnOffHelper sourceHelper("ns3::TcpSocketFactory",
                                 InetSocketAddress(interfaces.GetAddress(flow.destinationNode),
                                                   flow.port));
        sourceHelper.SetAttribute("PacketSize", UintegerValue(cfg.packetSizeBytes));
        sourceHelper.SetAttribute("DataRate", DataRateValue(DataRate(flowBitRate)));
        sourceHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        sourceHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer sourceApps = sourceHelper.Install(nodes.Get(flow.sourceNode));
        const double flowStartOffsetSeconds = 0.005 * i;
        sourceApps.Start(Seconds(cfg.appStartSeconds + flowStartOffsetSeconds));
        sourceApps.Stop(Seconds(appStopSeconds));
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(cfg.simulationTimeSeconds));
    Simulator::Run();

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    const AggregateFlowStats flowStats = CollectFlowStats(monitor, classifier, basePort, cfg.nFlows);
    const double activeDurationSeconds = appStopSeconds - cfg.appStartSeconds;
    const double throughputMbps =activeDurationSeconds > 0.0 ? (flowStats.rxBytes * 8.0 / activeDurationSeconds / 1e6) : 0.0;
    const double meanDelayMs = flowStats.rxPackets > 0 ? (flowStats.delaySum.GetSeconds() / static_cast<double>(flowStats.rxPackets) * 1000.0) : 0.0;
    const double pdrPct =
        flowStats.txPackets > 0
            ? (100.0 * static_cast<double>(flowStats.rxPackets) /
               static_cast<double>(flowStats.txPackets))
            : 0.0;
    const double dropRatioPct =
        flowStats.txPackets > 0
            ? (100.0 * static_cast<double>(flowStats.lostPackets) /
               static_cast<double>(flowStats.txPackets))
            : 0.0;
    const double totalEnergyConsumedJ = SumConsumedEnergy(energySources);
    const double meanEnergyPerNodeJ =
        cfg.nNodes > 0 ? totalEnergyConsumedJ / static_cast<double>(cfg.nNodes) : 0.0;



    std::cout << std::fixed << std::setprecision(3);
    std::cout << "variant=" << cfg.variantLabel << " mobility_mode=" << cfg.mobilityMode
              << " n_nodes=" << cfg.nNodes << " n_flows=" << cfg.nFlows
              << " packets_per_second=" << cfg.packetsPerSecond
              << " node_speed_mps=" << cfg.nodeSpeedMps << " throughput_mbps=" << throughputMbps
              << " mean_delay_ms=" << meanDelayMs << " pdr_pct=" << pdrPct
              << " drop_ratio_pct=" << dropRatioPct
              << " total_energy_j=" << totalEnergyConsumedJ << std::endl;

    Simulator::Destroy();
    return 0;
}
