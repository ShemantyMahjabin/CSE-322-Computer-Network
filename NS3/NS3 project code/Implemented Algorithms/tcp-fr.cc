
#include "tcp-fr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TcpFr");
NS_OBJECT_ENSURE_REGISTERED(TcpFr);

TypeId
TcpFr::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::TcpFr")
                            .SetParent<TcpNewReno>()
                            .SetGroupName("Internet")
                            .AddConstructor<TcpFr>()
                            .AddAttribute("RecoveryFactorA",
                                          "FR-TCP recovery factor a. Use 1 for few flows, 2 for many flows.",
                                          UintegerValue(1),
                                          MakeUintegerAccessor(&TcpFr::m_a),
                                          MakeUintegerChecker<uint32_t>(1));
    return tid;
}

TcpFr::TcpFr()
    : TcpNewReno()
    , m_alpha(0.8)           // Paper e bolce
    , m_a(1)                 // Paper e bolce a=1 for few flows, a=2 for many flows
    , m_BWE(0.0)             // Will be updated on first ACK
    , m_laststacktime(Seconds(0))
    , m_minRTT(Time::Max())
    , m_firstPacket(true)
{
    NS_LOG_FUNCTION(this);
}

TcpFr::TcpFr(const TcpFr& other)
    : TcpNewReno(other)
    , m_alpha(other.m_alpha)
    , m_a(other.m_a)
    , m_BWE(other.m_BWE)
    , m_laststacktime(other.m_laststacktime)
    , m_minRTT(other.m_minRTT)
    , m_firstPacket(other.m_firstPacket)
{
    NS_LOG_FUNCTION(this);
}

TcpFr::~TcpFr()
{
    NS_LOG_FUNCTION(this);
}

std::string
TcpFr::GetName() const
{
    return "TcpFr";
}



void
TcpFr::PktsAcked(Ptr<TcpSocketState> tcb,uint32_t segmentsAcked, const Time& rtt)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);

    Time now = Simulator::Now();

    if (m_firstPacket)
    {
        m_laststacktime = now;
        m_firstPacket = false;
    }
    else if (segmentsAcked > 0) // ignore dupACK-only updates
    {
        double timeDelta = (now - m_laststacktime).GetSeconds();
        if (timeDelta > 0)
        {
            double ackedBytes = static_cast<double>(segmentsAcked) * tcb->m_segmentSize;
            double sampleBwe  = (ackedBytes * 8.0) / timeDelta; // bps

            if (m_BWE <= 0.0)
            {
                m_BWE = sampleBwe; // fast time r jonne 
            }
            else
            {
                m_BWE = m_alpha * m_BWE + (1.0 - m_alpha) * sampleBwe;
            }
        }
    }

    m_laststacktime = now;
    if (!rtt.IsZero() && rtt < m_minRTT)
    {
        m_minRTT = rtt;
    }
}



void
TcpFr::CongestionStateSet(Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_FUNCTION(this << tcb << newState);


    if (newState == TcpSocketState::CA_LOSS)// Timeout
    {
        // ssthresh = (BWE * RTTmin) / a
        // CWIN = 1 (segment)

        if (m_BWE > 0 && m_minRTT != Time::Max())
        {
            uint32_t newSsThresh = CalculateBWEBasedSsThresh();
            tcb->m_ssThresh = std::max(newSsThresh, 2 * tcb->m_segmentSize);//Tcp r niyom ssthresh minimum 2 segment size hote hobe
            
            NS_LOG_INFO("Set ssthresh = " << tcb->m_ssThresh << " bytes "
                       << "(BWE=" << m_BWE << " bps, "
                       << "minRTT=" << m_minRTT.GetSeconds() << " s, "
                       << "a=" << m_a << ")");
        }
        tcb->m_cWnd = tcb->m_segmentSize;
    }
    else if (newState == TcpSocketState::CA_RECOVERY)// Triple duplicate ACKs 
    {
        // ssthresh = (BWE * RTTmin) / a
        // CWIN = ssthresh
        
        if (m_BWE > 0 && m_minRTT != Time::Max())
        {
            uint32_t newSsThresh = CalculateBWEBasedSsThresh();
            tcb->m_ssThresh = std::max(newSsThresh, 2 * tcb->m_segmentSize);
            tcb->m_cWnd = tcb->m_ssThresh;
            
            NS_LOG_INFO("Set ssthresh = cwnd = " << tcb->m_ssThresh << " bytes "
                       << "(BWE=" << m_BWE << " bps, "
                       << "minRTT=" << m_minRTT.GetSeconds() << " s, "
                       << "a=" << m_a << ")");
        }
    }
}

void
TcpFr::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        // Slow Start phase - NewReno r jemon omon e thakbe
        NS_LOG_INFO("Slow Start: cwnd = " << tcb->m_cWnd 
                   << ", ssthresh = " << tcb->m_ssThresh);
        TcpNewReno::SlowStart(tcb, segmentsAcked);
    }
    else
    {
        // Congestion Avoidance phase -  NewReno 
        NS_LOG_INFO("Congestion Avoidance: cwnd = " << tcb->m_cWnd 
                   << ", ssthresh = " << tcb->m_ssThresh);
        TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);

        // if (!m_avoidance.IsRunning() && m_BWE > 0 && m_minRTT != Time::Max())
        // {
        //     NS_LOG_INFO("Starting GFR-TCP periodic timer (500ms interval)");
        //     m_avoidance = Simulator::Schedule(MilliSeconds(500),
        //                                      &TcpFrGfr::CongestionAvoidanceEvent,
        //                                      this,
        //                                      tcb);
        // }
    }
}

uint32_t
TcpFr::GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << state << bytesInFlight);

    if (m_BWE > 0 && m_minRTT != Time::Max())
    {
        uint32_t newSsThresh = CalculateBWEBasedSsThresh();
        NS_LOG_INFO("GetSsThresh: Calculated from BWE = " << newSsThresh << " bytes");
        return std::max(newSsThresh, 2 * state->m_segmentSize);
    }
    else
    {
        // Fallback to standard TCP behavior (cwnd/2)
        uint32_t newSsThresh = std::max(2 * state->m_segmentSize, bytesInFlight / 2);
        NS_LOG_INFO("GetSsThresh: Using fallback = " << newSsThresh << " bytes");
        return newSsThresh;
    }
}

Ptr<TcpCongestionOps>
TcpFr::Fork()
{
    return CopyObject<TcpFr>(this);
}



uint32_t
TcpFr::CalculateBWEBasedSsThresh() const
{
    // ssthresh = (BWE * RTTmin) / a
    
    double ssthreshBits = m_BWE * m_minRTT.GetSeconds();
    double ssthreshBytes = ssthreshBits / 8.0;  
    uint32_t result = static_cast<uint32_t>(ssthreshBytes / m_a);
    
    NS_LOG_DEBUG("CalculateBWEBasedSsThresh: "
                << "BWE=" << m_BWE << " bps, "
                << "RTTmin=" << m_minRTT.GetSeconds() << " s, "
                << "a=" << m_a << ", "
                << "result=" << result << " bytes");
    
    return result;
}

} 
