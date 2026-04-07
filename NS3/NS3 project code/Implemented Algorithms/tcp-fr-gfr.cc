
#include "tcp-fr-gfr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("TcpFrGfr");
NS_OBJECT_ENSURE_REGISTERED(TcpFrGfr);

TypeId
TcpFrGfr::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::TcpFrGfr")
                            .SetParent<TcpNewReno>()
                            .SetGroupName("Internet")
                            .AddConstructor<TcpFrGfr>()
                            .AddAttribute("RecoveryFactorA",
                                          "GFR-TCP recovery factor a. Use 1 for few flows, 2 for many flows.",
                                          UintegerValue(1),
                                          MakeUintegerAccessor(&TcpFrGfr::m_a),
                                          MakeUintegerChecker<uint32_t>(1));
    return tid;
}

TcpFrGfr::TcpFrGfr()
    : TcpNewReno()
    , m_alpha(0.8)           // Paper e bolce
    , m_a(1)                 // Paper e bolce a=1 for few flows, a=2 for many flows
    , m_BWE(0.0)             // Will be updated on first ACK
    , m_laststacktime(Seconds(0))
    , m_minRTT(Time::Max())
    , m_avoidance()
    , m_firstPacket(true)

{
    NS_LOG_FUNCTION(this);
}

TcpFrGfr::TcpFrGfr(const TcpFrGfr& other)
    : TcpNewReno(other)
    , m_alpha(other.m_alpha)
    , m_a(other.m_a)
    , m_BWE(other.m_BWE)
    , m_laststacktime(other.m_laststacktime)
    , m_minRTT(other.m_minRTT)
    , m_avoidance()  // Don't copy event
    , m_firstPacket(other.m_firstPacket)

{
    NS_LOG_FUNCTION(this);
}

TcpFrGfr::~TcpFrGfr()
{
    NS_LOG_FUNCTION(this);
    m_avoidance.Cancel();
}

std::string
TcpFrGfr::GetName() const
{
    return "TcpFrGfr";
}

// void 
// TcpFrGfr::UpdateAverageSegmentSize(uint32_t newSegmentSize)
// {
//     m_segmentHistory.push_back(newSegmentSize);
    
//     // If we exceed n segments, remove the oldest one
//     if (m_segmentHistory.size() > m_n)
//     {
//         m_segmentHistory.pop_front();
//     }
    
//     // Calculate the new average
//     double sum = 0;
//     for (uint32_t size : m_segmentHistory)
//     {
//         sum += size;
//     }
    
//     m_avgSegmentSize = sum / m_segmentHistory.size();
// }




void
TcpFrGfr::PktsAcked(Ptr<TcpSocketState> tcb,uint32_t segmentsAcked, const Time& rtt)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);

    Time now = Simulator::Now();

    if (m_firstPacket)
    {
        m_laststacktime = now;
        m_firstPacket = false;
    }
    else if (segmentsAcked > 0)
    {
        double timeDelta = (now - m_laststacktime).GetSeconds();
        if (timeDelta > 0)
        {
            double ackedBytes = static_cast<double>(segmentsAcked) * tcb->m_segmentSize;
            double sampleBwe  = (ackedBytes * 8.0) / timeDelta; // bps

            if (m_BWE <= 0.0)
            {
                m_BWE = sampleBwe;
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
TcpFrGfr::CongestionStateSet(Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_FUNCTION(this << tcb << newState);

    // Cancel periodic timer when entering loss/recovery states
    if (newState == TcpSocketState::CA_LOSS || 
        newState == TcpSocketState::CA_RECOVERY)
    {
        m_avoidance.Cancel();//jeno old scheduled update interfere na kore
    }

    if (newState == TcpSocketState::CA_LOSS)//timeout
    {
        // ssthresh = (BWE * RTTmin) / a
        // CWIN = 1 (segment)

        if (m_BWE > 0 && m_minRTT != Time::Max())
        {
            uint32_t newSsThresh = CalculateBWEBasedSsThresh();
            tcb->m_ssThresh = std::max(newSsThresh, 2 * tcb->m_segmentSize);
            
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
TcpFrGfr::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        // Slow Start phase - use NewReno
        TcpNewReno::SlowStart(tcb, segmentsAcked);
    }
    else
    {
        // Congestion Avoidance phase - use  NewReno linear increase
        TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);

        const double bweRttProduct = (m_BWE * m_minRTT.GetSeconds()) / 8.0;
        if (!m_avoidance.IsPending() && m_BWE > 0 && m_minRTT != Time::Max() &&
            tcb->m_cWnd > tcb->m_ssThresh && tcb->m_cWnd < bweRttProduct)
        {
            NS_LOG_INFO("Starting GFR-TCP periodic timer (500ms interval)");
            m_avoidance = Simulator::Schedule(MilliSeconds(500),
                                             &TcpFrGfr::CongestionAvoidanceEvent,
                                             this,
                                             tcb);
        }
    }
}

uint32_t
TcpFrGfr::GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << state << bytesInFlight);

    if (m_BWE > 0 && m_minRTT != Time::Max())
    {
        uint32_t newSsThresh = CalculateBWEBasedSsThresh();
        return std::max(newSsThresh, 2 * state->m_segmentSize);
    }
    else
    {
        // Fallback to standard TCP behavior (cwnd/2)
        uint32_t newSsThresh = std::max(2 * state->m_segmentSize, bytesInFlight / 2);
        return newSsThresh;
    }
}

Ptr<TcpCongestionOps>
TcpFrGfr::Fork()
{
    return CopyObject<TcpFrGfr>(this);
}

void
TcpFrGfr::CongestionAvoidanceEvent(Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this << tcb);

    // GFR-TCP Algorithm 
    //  If (CWIN > ssthresh) AND (CWIN < BWE*RTT_min)
    // then ssthresh += (BWE*RTT_min - ssthresh)/2;

    if (m_BWE > 0 && m_minRTT != Time::Max())
    {
        double bweRttProduct = (m_BWE * m_minRTT.GetSeconds()) / 8.0; 
        if ((tcb->m_cWnd > tcb->m_ssThresh) && (tcb->m_cWnd < bweRttProduct))
        {
            uint32_t oldSsThresh = tcb->m_ssThresh;
            tcb->m_ssThresh += (bweRttProduct - tcb->m_ssThresh) / 2;
            
            NS_LOG_INFO("GFR-TCP: Updated ssthresh from " << oldSsThresh 
                       << " to " << tcb->m_ssThresh << " bytes");
        }
        else
        {
            NS_LOG_DEBUG("GFR-TCP condition not met: cwnd=" << tcb->m_cWnd 
                        << ", ssthresh=" << tcb->m_ssThresh 
                        << ", BWE*RTT=" << bweRttProduct);
        }

        // Keep the periodic timer alive only while the GFR condition can still help.
        if (tcb->m_cWnd > tcb->m_ssThresh && tcb->m_cWnd < bweRttProduct)
        {
            m_avoidance = Simulator::Schedule(MilliSeconds(500),
                                             &TcpFrGfr::CongestionAvoidanceEvent,
                                             this,
                                             tcb);
        }
    }

}

uint32_t
TcpFrGfr::CalculateBWEBasedSsThresh() const
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
