#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/error-model.h"
#include <fstream>
#include <vector>
#include <string>
#include <limits>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpLossySim");

class MyApp : public Application
{
  public:
    MyApp();
    virtual ~MyApp();
    static TypeId GetTypeId(void);
    void Configure(Ptr<Socket> socket,
                   Address address,
                   uint32_t packetSize,
                   uint32_t nPackets,
                   DataRate dataRate);

  private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    void SendPacket(void);
    void ScheduleTx(void);

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_pktSize;
    uint32_t m_nPackets;
    DataRate m_dataRate;
    EventId m_sendEvent;
    bool m_running;
    uint32_t m_pktsSent;
};

MyApp::MyApp()
    : m_socket(0), m_pktSize(0), m_nPackets(0), m_dataRate(0), m_running(false), m_pktsSent(0)
{
}

MyApp::~MyApp() { m_socket = 0; }

TypeId
MyApp::GetTypeId(void)
{
    static TypeId tid = TypeId("MyApp")
                            .SetParent<Application>()
                            .SetGroupName("Tutorial")
                            .AddConstructor<MyApp>();
    return tid;
}

void
MyApp::Configure(Ptr<Socket> socket,Address address, uint32_t packetSize, uint32_t nPackets,DataRate dataRate)
{
    m_socket = socket;
    m_peer = address;
    m_pktSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;
}

void
MyApp::StartApplication(void)
{
    m_running = true;
    m_pktsSent = 0;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}

void
MyApp::StopApplication(void)
{
    m_running = false;
    if (m_sendEvent.IsPending())
        Simulator::Cancel(m_sendEvent);
    if (m_socket)
        m_socket->Close();
}

void
MyApp::SendPacket(void)
{
    Ptr<Packet> pkt = Create<Packet>(m_pktSize);
    m_socket->Send(pkt);
    m_pktsSent++;
    if (m_pktsSent < m_nPackets)
        ScheduleTx();
}

void
MyApp::ScheduleTx(void)
{
    if (m_running)
    {
        Time tNext(Seconds(m_pktSize * 8 / static_cast<double>(m_dataRate.GetBitRate())));
        m_sendEvent = Simulator::Schedule(tNext, &MyApp::SendPacket, this);
    }
}

// Paper: P(loss event) = 1e-3, average burst length n = 3 
// p_start = P(loss_event) / avg_burst = 1e-3 / 3 ≈ 3.33e-4
Ptr<BurstErrorModel>
CreateBurstErrorModel()
{
    Ptr<BurstErrorModel> em = CreateObject<BurstErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(3.33e-4));
    Ptr<ExponentialRandomVariable> burstSizeRv = CreateObject<ExponentialRandomVariable>();
    burstSizeRv->SetAttribute("Mean", DoubleValue(3.0));
    burstSizeRv->SetAttribute("Bound", DoubleValue(20.0)); // burst limit 20 packets
    em->SetAttribute("BurstSize", PointerValue(burstSizeRv));
    return em;
}

//fig 8 throughput vs time r jonne
static uint64_t g_rxBytes = 0;
static uint64_t g_lastRxBytes = 0;
static Ptr<OutputStreamWrapper> g_throughputStream;// Measure throughput every g_measureInterval seconds
static double g_measureInterval = 1.0; // seconds

void
RxCallback(Ptr<const Packet> packet, const Address& addr)
{
    g_rxBytes += packet->GetSize();//add received packet size 
}

void
MeasureThroughput()
{
    double throughput = ((g_rxBytes - g_lastRxBytes) * 8.0) / g_measureInterval / 1e6; // Mb/s
    *g_throughputStream->GetStream()
        << Simulator::Now().GetSeconds() << "\t" << throughput << std::endl;
    g_lastRxBytes = g_rxBytes;
    Simulator::Schedule(Seconds(g_measureInterval), &MeasureThroughput);
}


int
main(int argc, char* argv[])
{
    uint32_t numConnections = 10; 
    std::string tcpVariant = "ns3::TcpNewReno";
    std::string bottleneckBW = "45Mbps";
    std::string linkDelay = "250ms";// one-way → 500ms RTT
    double simulationTime = 200.0;
    uint32_t payloadSize = 1024;
    bool enableBurstError = true;
    bool enableSack = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numConnections", "Number of TCP flows", numConnections);
    cmd.AddValue("tcpVariant", "TCP variant string ", tcpVariant);
    cmd.AddValue("bandwidth", "Bottleneck bandwidth ", bottleneckBW);
    cmd.AddValue("delay", "One-way link delay ", linkDelay);
    cmd.AddValue("simTime", "Simulation time in seconds", simulationTime);
    cmd.AddValue("enableSack", "Enable TCP SACK option", enableSack);
    cmd.AddValue("enableBurstError", "Use burst error model (vs simple rate-based)", enableBurstError);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue(tcpVariant));
    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",TypeIdValue(TypeId::LookupByName("ns3::TcpClassicRecovery")));
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(payloadSize));
    Config::SetDefault("ns3::TcpSocketBase::Sack", BooleanValue(enableSack));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(1));

    // Dumbbell-like topology:
    // N senders -> left gateway -> (shared lossy bottleneck) -> right gateway -> receiver.
    NodeContainer senders;
    senders.Create(numConnections);

    NodeContainer gateways;
    gateways.Create(2); // [0]=left, [1]=right

    NodeContainer receiver;
    receiver.Create(1);

    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    accessLink.SetChannelAttribute("Delay", StringValue("1ms"));

    // Bottleneck link
    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBW));
    bottleneck.SetChannelAttribute("Delay", StringValue(linkDelay));

    InternetStackHelper stack;
    stack.Install(senders);
    stack.Install(gateways);
    stack.Install(receiver);

    // Sender access links: sender_i <-> left gateway
    Ipv4AddressHelper ipv4;
    std::vector<Ipv4InterfaceContainer> senderIfaces(numConnections);
    std::vector<Ipv4Address> senderIps(numConnections);

    for (uint32_t i = 0; i < numConnections; i++)
    {
        NodeContainer accessPair(senders.Get(i), gateways.Get(0));
        NetDeviceContainer accessDevices = accessLink.Install(accessPair);
        std::ostringstream base;
        base << "10.1." << (i + 1) << ".0";
        ipv4.SetBase(base.str().c_str(), "255.255.255.0");
        senderIfaces[i] = ipv4.Assign(accessDevices);
        senderIps[i] = senderIfaces[i].GetAddress(0);
    }

    // Shared bottleneck: left gateway <-> right gateway
    NodeContainer bottleneckPair(gateways.Get(0), gateways.Get(1));
    NetDeviceContainer bottleneckDevices = bottleneck.Install(bottleneckPair);
    ipv4.SetBase("10.2.0.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIfaces = ipv4.Assign(bottleneckDevices);

    // Attaching burst/rateerrormodel on left -> right
    if (enableBurstError)
    {
        Ptr<BurstErrorModel> em = CreateBurstErrorModel();
        bottleneckDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }
    else
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorRate", DoubleValue(1e-3));
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        bottleneckDevices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    // Receiver access: right gateway <-> receiver
    NodeContainer receiverPair(gateways.Get(1), receiver.Get(0));
    NetDeviceContainer receiverDevices = accessLink.Install(receiverPair);
    ipv4.SetBase("10.3.0.0", "255.255.255.0");
    Ipv4InterfaceContainer receiverIfaces = ipv4.Assign(receiverDevices);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t basePort = 9000;
    AsciiTraceHelper ascii;
    // Throughput-vs-time output (for Fig 8)
    g_throughputStream = ascii.CreateFileStream("scratch/randomlosses" + "-throughput-vs-time.txt");
    *g_throughputStream->GetStream() << "# Time(s)\tThroughput(Mb/s)" << std::endl;
    Ptr<OutputStreamWrapper> cwndStream = ascii.CreateFileStream("scratch/randomlosses" + "-cwnd.txt");
    *cwndStream->GetStream() << "# Time(s)\tOldCwnd\tNewCwnd" << std::endl;
    Ptr<OutputStreamWrapper> ssthreshStream = ascii.CreateFileStream("scratch/randomlosses" + "-ssthresh.txt");
    *ssthreshStream->GetStream() << "# Time(s)\tOldSSThresh\tNewSSThresh" << std::endl;

    // Sink apps
    uint32_t maxPackets = std::numeric_limits<uint32_t>::max();
    DataRate appRate("1Gbps"); 

    for (uint32_t i = 0; i < numConnections; i++)
    {
        uint16_t port = basePort + i;
        Address sinkAddr(InetSocketAddress(receiverIfaces.GetAddress(1), port));

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sinkApp = sinkHelper.Install(receiver.Get(0));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simulationTime));

        // flow 0 r jonne Rx callback and cwnd/ssthresh tracing
        if (i == 0)
        {
            Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApp.Get(0));
            sink->TraceConnectWithoutContext("Rx", MakeCallback(&RxCallback));
        }
        Ptr<Socket> sock = Socket::CreateSocket(senders.Get(i), TcpSocketFactory::GetTypeId());
        if (i == 0)
        {
            sock->TraceConnectWithoutContext( "CongestionWindow", MakeBoundCallback( +[](Ptr<OutputStreamWrapper> s, uint32_t o, uint32_t n) {*s->GetStream() << Simulator::Now().GetSeconds() << "\t" << o << "\t" << n << std::endl;},cwndStream));
            sock->TraceConnectWithoutContext("SlowStartThreshold",MakeBoundCallback(+[](Ptr<OutputStreamWrapper> s, uint32_t o, uint32_t n) {*s->GetStream() << Simulator::Now().GetSeconds() << "\t" << o << "\t" << n << std::endl;},ssthreshStream));
        }
        Ptr<MyApp> app = CreateObject<MyApp>();
        app->Configure(sock, sinkAddr, payloadSize, maxPackets, appRate);
        senders.Get(i)->AddApplication(app);
        double startTime = 1.0 + i * 0.01;//shob flow jeno same time e start na kore
        app->SetStartTime(Seconds(startTime));
        app->SetStopTime(Seconds(simulationTime - 0.1));
    }
    Simulator::Schedule(Seconds(1.0 + g_measureInterval), &MeasureThroughput);

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double totalGoodput = 0.0;
    uint32_t dataFlows = 0;
    for (auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        bool isData = false;
        for (uint32_t i = 0; i < numConnections; i++)
        {
            if (t.sourceAddress == senderIps[i])// Only count sender to receiver (data) flows, not ACKs
            {
                isData = true;
                break;
            }
        }
        if (!isData)
            continue;

        double dur = kv.second.timeLastRxPacket.GetSeconds() - kv.second.timeFirstTxPacket.GetSeconds();
        double gput = (dur > 0) ? (kv.second.rxBytes * 8.0 / dur / 1e6) : 0.0; // Mb/s
        totalGoodput += gput;
        dataFlows++;
    }

    double avgGoodput = (dataFlows > 0) ? totalGoodput / dataFlows : 0.0;

    std::ofstream res("scratch/randomlosses" + "-results.txt");
    res << "tcpVariant=" << tcpVariant << "\n"
        << "numConnections=" << numConnections << "\n"
        << "bandwidth=" << bottleneckBW << "\n"
        << "delay=" << linkDelay << "\n"
        << "simTime=" << simulationTime << "\n"
        << "sack=" << (enableSack ? "1" : "0") << "\n"
        << "avgGoodput_Mbps=" << avgGoodput << "\n"
        << "totalGoodput_Mbps=" << totalGoodput << "\n";
    res.close();
    Simulator::Destroy();
    return 0;
}
