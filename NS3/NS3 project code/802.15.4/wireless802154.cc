#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"

#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::energy;

NS_LOG_COMPONENT_DEFINE("Wireless802154Static");

namespace
{

struct ExperimentConfig
{
    std::string tcpVariant{"TcpFr"};
    uint32_t nNodes{20};
    uint32_t nFlows{10};
    uint32_t packetsPerSecond{100};
    uint32_t coverageMultiplier{3};
    uint32_t packetSizeBytes{40};
    double txRangeMeters{40.0};
    double simulationTimeSeconds{20.0};
    double appStartSeconds{5.0};
    double stopMarginSeconds{2.0};
    double initialEnergyJ{1000.0};
    double supplyVoltageV{3.0};
    double jitterThresholdMs{3.0};
    uint32_t run{1};
    bool useMeshUnder{true};
    uint32_t meshUnderRadius{3};
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

std::vector<FlowSpec>
BuildFlowSpecs(uint32_t nNodes, uint32_t nFlows, uint16_t basePort)
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
                static_cast<uint16_t>(basePort + flows.size()),
            });
    }

    return flows;
}

AggregateFlowStats
CollectFlowStats(Ptr<FlowMonitor> monitor,
                 Ptr<Ipv6FlowClassifier> classifier,
                 uint16_t basePort,
                 uint32_t nFlows)
{
    AggregateFlowStats aggregate;

    for (const auto& [flowId, stats] : monitor->GetFlowStats())
    {
        const auto tuple = classifier->FindFlow(flowId);
        if (tuple.protocol != 6)
        {
            continue;
        }
        if (tuple.destinationPort < basePort || tuple.destinationPort >= basePort + nFlows)
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
CurrentForPhyState(lrwpan::PhyEnumeration state)
{
    constexpr double kTxCurrentA = 0.0174;
    constexpr double kRxCurrentA = 0.0188;
    constexpr double kIdleCurrentA = 0.000426;
    constexpr double kSleepCurrentA = 0.000020;

    switch (state)
    {
    case lrwpan::IEEE_802_15_4_PHY_BUSY_TX:
        return kTxCurrentA;
    case lrwpan::IEEE_802_15_4_PHY_BUSY_RX:
    case lrwpan::IEEE_802_15_4_PHY_RX_ON:
        return kRxCurrentA;
    case lrwpan::IEEE_802_15_4_PHY_TRX_OFF:
    case lrwpan::IEEE_802_15_4_PHY_FORCE_TRX_OFF:
        return kSleepCurrentA;
    case lrwpan::IEEE_802_15_4_PHY_BUSY:
    case lrwpan::IEEE_802_15_4_PHY_IDLE:
    case lrwpan::IEEE_802_15_4_PHY_TX_ON:
    case lrwpan::IEEE_802_15_4_PHY_SUCCESS:
    case lrwpan::IEEE_802_15_4_PHY_INVALID_PARAMETER:
    case lrwpan::IEEE_802_15_4_PHY_UNSUPPORTED_ATTRIBUTE:
    case lrwpan::IEEE_802_15_4_PHY_READ_ONLY:
    case lrwpan::IEEE_802_15_4_PHY_UNSPECIFIED:
    default:
        return kIdleCurrentA;
    }
}

void
InstallEnergyModels(const NodeContainer& nodes,
                    const NetDeviceContainer& lrwpanDevices,
                    const EnergySourceContainer& energySources)
{
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        Ptr<EnergySource> source = energySources.Get(i);
        Ptr<SimpleDeviceEnergyModel> model = CreateObject<SimpleDeviceEnergyModel>();
        model->SetNode(nodes.Get(i));
        model->SetEnergySource(source);
        source->AppendDeviceEnergyModel(model);
        model->SetCurrentA(CurrentForPhyState(lrwpan::IEEE_802_15_4_PHY_RX_ON));

        Ptr<lrwpan::LrWpanNetDevice> device = DynamicCast<lrwpan::LrWpanNetDevice>(lrwpanDevices.Get(i));
        device->GetPhy()->TraceConnectWithoutContext("TrxStateValue",
                                                     MakeBoundCallback(
                                                         +[](Ptr<SimpleDeviceEnergyModel> boundModel,
                                                             lrwpan::PhyEnumeration,
                                                             lrwpan::PhyEnumeration newState) {
                                                             boundModel->SetCurrentA(
                                                                 CurrentForPhyState(newState));
                                                         },
                                                         model));
    }
}

} 

int
main(int argc, char* argv[])
{
    ExperimentConfig cfg;
    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpVariant", "Optional TCP variant: ns3::TcpNewReno, ns3::TcpFr, ns3::TcpFrGfr, ns3::TcpFrGfrJitter", cfg.tcpVariant);
    cmd.AddValue("nNodes", "Number of LR-WPAN nodes", cfg.nNodes);
    cmd.AddValue("nFlows", "Number of TCP flows", cfg.nFlows);
    cmd.AddValue("packetsPerSecond",
                 "Packets per second mapped to offered application load per flow",
                 cfg.packetsPerSecond);
    cmd.AddValue("coverageMultiplier",
                 "Coverage side multiplier from the checklist: 1, 2, 3, 4, or 5",
                 cfg.coverageMultiplier);
    cmd.AddValue("simulationTime", "Total simulation time in seconds", cfg.simulationTimeSeconds);
    cmd.Parse(argc, argv);

    const double appStopSeconds = cfg.simulationTimeSeconds - cfg.stopMarginSeconds;

    const double fieldSideMeters =
        static_cast<double>(cfg.coverageMultiplier) * cfg.txRangeMeters;
    const uint16_t basePort = 9000;
    const uint64_t flowBitRate =
        static_cast<uint64_t>(cfg.packetSizeBytes * 8.0 * cfg.packetsPerSecond);
    const std::string tcpTypeName =
        cfg.tcpVariant.rfind("ns3::", 0) == 0 ? cfg.tcpVariant : "ns3::" + cfg.tcpVariant;
    TypeId tcpTid;
    NS_ABORT_MSG_UNLESS(TypeId::LookupByNameFailSafe(tcpTypeName, &tcpTid),
                        "TCP TypeId " << tcpTypeName << " not found");

    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(cfg.run);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(tcpTid));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(cfg.packetSizeBytes));
    Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(7));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(false));
    Config::SetDefault("ns3::TcpFrGfrJitter::JitterThreshold",
                       TimeValue(MilliSeconds(cfg.jitterThresholdMs)));

    NodeContainer nodes;
    nodes.Create(cfg.nNodes);
    std::ostringstream fieldSideStream;
    fieldSideStream << std::fixed << std::setprecision(6) << fieldSideMeters;
    const std::string fieldSide = fieldSideStream.str();

    ObjectFactory positionFactory;
    positionFactory.SetTypeId("ns3::RandomRectanglePositionAllocator");
    positionFactory.Set("X",
                        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                    fieldSide + "]"));
    positionFactory.Set("Y",
                        StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                    fieldSide + "]"));

    MobilityHelper mobility;
    mobility.SetPositionAllocator(positionFactory.Create()->GetObject<PositionAllocator>());
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    LrWpanHelper lrWpanHelper;
    lrWpanHelper.SetPropagationDelayModel("ns3::ConstantSpeedPropagationDelayModel");
    lrWpanHelper.AddPropagationLossModel("ns3::RangePropagationLossModel",
                                         "MaxRange",
                                         DoubleValue(cfg.txRangeMeters));
    NetDeviceContainer lrwpanDevices = lrWpanHelper.Install(nodes);
    lrWpanHelper.CreateAssociatedPan(lrwpanDevices, 0xBEEF);

    SixLowPanHelper sixLowPanHelper;
    sixLowPanHelper.SetDeviceAttribute("UseMeshUnder", BooleanValue(cfg.useMeshUnder));
    sixLowPanHelper.SetDeviceAttribute("MeshUnderRadius", UintegerValue(cfg.meshUnderRadius));
    NetDeviceContainer sixLowPanDevices = sixLowPanHelper.Install(lrwpanDevices);

    InternetStackHelper internet;
    internet.SetIpv4StackInstall(false);
    internet.Install(nodes);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:db8:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer interfaces = ipv6.Assign(sixLowPanDevices);

    BasicEnergySourceHelper basicEnergyHelper;
    basicEnergyHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(cfg.initialEnergyJ));
    basicEnergyHelper.Set("BasicEnergySupplyVoltageV", DoubleValue(cfg.supplyVoltageV));
    basicEnergyHelper.Set("PeriodicEnergyUpdateInterval", TimeValue(Seconds(1.0)));
    EnergySourceContainer energySources = basicEnergyHelper.Install(nodes);
    InstallEnergyModels(nodes, lrwpanDevices, energySources);

    const std::vector<FlowSpec> flows = BuildFlowSpecs(cfg.nNodes, cfg.nFlows, basePort);
    for (uint32_t i = 0; i < flows.size(); ++i)
    {
        const FlowSpec& flow = flows[i];

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    Inet6SocketAddress(Ipv6Address::GetAny(), flow.port));
        ApplicationContainer sinkApps = sinkHelper.Install(nodes.Get(flow.destinationNode));
        sinkApps.Start(Seconds(0.0));
        sinkApps.Stop(Seconds(cfg.simulationTimeSeconds));

        OnOffHelper sourceHelper(
            "ns3::TcpSocketFactory",
            Inet6SocketAddress(interfaces.GetAddress(flow.destinationNode, 1), flow.port));
        sourceHelper.SetAttribute("PacketSize", UintegerValue(cfg.packetSizeBytes));
        sourceHelper.SetAttribute("DataRate", DataRateValue(DataRate(flowBitRate)));
        sourceHelper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
        sourceHelper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));

        ApplicationContainer sourceApps = sourceHelper.Install(nodes.Get(flow.sourceNode));
        const double flowStartOffsetSeconds = 0.02 * i;
        sourceApps.Start(Seconds(cfg.appStartSeconds + flowStartOffsetSeconds));
        sourceApps.Stop(Seconds(appStopSeconds));
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    Simulator::Stop(Seconds(cfg.simulationTimeSeconds));
    Simulator::Run();

    monitor->CheckForLostPackets();

    Ptr<Ipv6FlowClassifier> classifier = DynamicCast<Ipv6FlowClassifier>(flowHelper.GetClassifier6());
    const AggregateFlowStats flowStats =
        CollectFlowStats(monitor, classifier, basePort, cfg.nFlows);

    const double activeDurationSeconds = appStopSeconds - cfg.appStartSeconds;
    const double throughputMbps =
        activeDurationSeconds > 0.0 ? (flowStats.rxBytes * 8.0 / activeDurationSeconds / 1e6) : 0.0;
    const double meanDelayMs =
        flowStats.rxPackets > 0
            ? (flowStats.delaySum.GetSeconds() / static_cast<double>(flowStats.rxPackets) * 1000.0)
            : 0.0;
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
    const double meanThroughputPerNodeMbps =
        cfg.nNodes > 0 ? throughputMbps / static_cast<double>(cfg.nNodes) : 0.0;
    double totalEnergyConsumedJ = 0.0;
    for (uint32_t i = 0; i < energySources.GetN(); ++i)
    {
        Ptr<EnergySource> source = energySources.Get(i);
        source->UpdateEnergySource();
        totalEnergyConsumedJ += source->GetInitialEnergy() - source->GetRemainingEnergy();
    }
    const double meanEnergyPerNodeJ =
        cfg.nNodes > 0 ? totalEnergyConsumedJ / static_cast<double>(cfg.nNodes) : 0.0;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "variant=" << cfg.tcpVariant << " mobility_mode=static"
              << " n_nodes=" << cfg.nNodes << " n_flows=" << cfg.nFlows
              << " packets_per_second=" << cfg.packetsPerSecond
              << " coverage_multiplier=" << cfg.coverageMultiplier
              << " field_side_m=" << fieldSideMeters
              << " tx_range_m=" << cfg.txRangeMeters
              << " use_mesh_under=" << (cfg.useMeshUnder ? 1 : 0)
              << " throughput_mbps=" << throughputMbps << " mean_delay_ms=" << meanDelayMs
              << " mean_throughput_per_node_mbps=" << meanThroughputPerNodeMbps
              << " pdr_pct=" << pdrPct << " drop_ratio_pct=" << dropRatioPct
              << " total_energy_j=" << totalEnergyConsumedJ
              << " mean_energy_per_node_j=" << meanEnergyPerNodeJ << std::endl;

    Simulator::Destroy();
    return 0;
}
