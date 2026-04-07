#include "tcp-fr-gfr-jitter.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "tcp-socket-state.h"
#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TcpFrGfrJitter");
NS_OBJECT_ENSURE_REGISTERED(TcpFrGfrJitter);

TypeId
TcpFrGfrJitter::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TcpFrGfrJitter")
            .SetParent<TcpNewReno>()
            .SetGroupName("Internet")
            .AddConstructor<TcpFrGfrJitter>()
            .AddAttribute("RecoveryFactorA",
                          "FR/GFR recovery factor a. Use 1 for few flows, 2 for many flows.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&TcpFrGfrJitter::m_a),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("JitterThreshold",
                          "Disable the GFR boost when |RTT - minRTT| exceeds this threshold.",
                          TimeValue(MilliSeconds(3)),
                          MakeTimeAccessor(&TcpFrGfrJitter::m_jitterThreshold),
                          MakeTimeChecker())
            .AddAttribute("JitterSmoothingGain",
                          "EWMA gain for the queueing-delay estimate used by the safety switch.",
                          DoubleValue(0.2),
                          MakeDoubleAccessor(&TcpFrGfrJitter::m_jitterSmoothingGain),
                          MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

TcpFrGfrJitter::TcpFrGfrJitter()
    : TcpNewReno(),
      m_alpha(0.8),
      m_a(1),
      m_bwe(0.0),
      m_lastAckTime(Seconds(0)),
      m_minRtt(Time::Max()),
      m_avoidance(),
      m_firstPacket(true),
      m_jitterThreshold(MilliSeconds(3)),
      m_jitterSmoothingGain(0.2),
      m_smoothedJitterSeconds(0.0),
      m_hasJitterSample(false)
{
    NS_LOG_FUNCTION(this);
}

TcpFrGfrJitter::TcpFrGfrJitter(const TcpFrGfrJitter& other)
    : TcpNewReno(other),
      m_alpha(other.m_alpha),
      m_a(other.m_a),
      m_bwe(other.m_bwe),
      m_lastAckTime(other.m_lastAckTime),
      m_minRtt(other.m_minRtt),
      m_avoidance(),
      m_firstPacket(other.m_firstPacket),
      m_jitterThreshold(other.m_jitterThreshold),
      m_jitterSmoothingGain(other.m_jitterSmoothingGain),
      m_smoothedJitterSeconds(other.m_smoothedJitterSeconds),
      m_hasJitterSample(other.m_hasJitterSample)
{
    NS_LOG_FUNCTION(this);
}

TcpFrGfrJitter::~TcpFrGfrJitter()
{
    NS_LOG_FUNCTION(this);
    m_avoidance.Cancel();
}

std::string
TcpFrGfrJitter::GetName() const
{
    return "TcpFrGfrJitter";
}

void
TcpFrGfrJitter::PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked << rtt);

    const Time now = Simulator::Now();

    if (m_firstPacket)
    {
        m_lastAckTime = now;
        m_firstPacket = false;
    }
    else if (segmentsAcked > 0)
    {
        const double timeDelta = (now - m_lastAckTime).GetSeconds();
        if (timeDelta > 0.0)
        {
            const double ackedBytes = static_cast<double>(segmentsAcked) * tcb->m_segmentSize;
            const double sampleBwe = (ackedBytes * 8.0) / timeDelta;
            if (m_bwe <= 0.0)
            {
                m_bwe = sampleBwe;
            }
            else
            {
                m_bwe = m_alpha * m_bwe + (1.0 - m_alpha) * sampleBwe;
            }
        }
    }

    m_lastAckTime = now;

    if (!rtt.IsZero())
    {
        if (rtt < m_minRtt)
        {
            m_minRtt = rtt;
        }

        if (m_minRtt != Time::Max())
        {
            const double currentJitterSeconds = std::fabs((rtt - m_minRtt).GetSeconds());// congestion hole RTT - minRTT = queueing delay bere jay ,emon case e gfr boost off 
            if (!m_hasJitterSample)
            {
                m_smoothedJitterSeconds = currentJitterSeconds;// Initialize with the first sample
                m_hasJitterSample = true;
            }
            else
            {
                m_smoothedJitterSeconds =
                    (1.0 - m_jitterSmoothingGain) * m_smoothedJitterSeconds +
                    m_jitterSmoothingGain * currentJitterSeconds;//ager ta k 80% weight dibe, notun ta k 20% weight dibe
            }
        }
    }
}

void
TcpFrGfrJitter::CongestionStateSet(Ptr<TcpSocketState> tcb,
                                   TcpSocketState::TcpCongState_t newState)
{
    NS_LOG_FUNCTION(this << tcb << newState);

    if (newState == TcpSocketState::CA_LOSS || newState == TcpSocketState::CA_RECOVERY)
    {
        m_avoidance.Cancel();
    }

    if (newState == TcpSocketState::CA_LOSS)
    {
        if (m_bwe > 0.0 && m_minRtt != Time::Max())
        {
            const uint32_t newSsThresh = CalculateBWEBasedSsThresh();
            tcb->m_ssThresh = std::max(newSsThresh, 2 * tcb->m_segmentSize);
        }
        tcb->m_cWnd = tcb->m_segmentSize;
    }
    else if (newState == TcpSocketState::CA_RECOVERY)
    {
        if (m_bwe > 0.0 && m_minRtt != Time::Max())
        {
            const uint32_t newSsThresh = CalculateBWEBasedSsThresh();
            tcb->m_ssThresh = std::max(newSsThresh, 2 * tcb->m_segmentSize);
            tcb->m_cWnd = tcb->m_ssThresh;
        }
    }
}

void
TcpFrGfrJitter::IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
    NS_LOG_FUNCTION(this << tcb << segmentsAcked);

    if (tcb->m_cWnd < tcb->m_ssThresh)
    {
        TcpNewReno::SlowStart(tcb, segmentsAcked);
    }
    else
    {
        TcpNewReno::CongestionAvoidance(tcb, segmentsAcked);

        const double bweRttProduct = (m_bwe * m_minRtt.GetSeconds()) / 8.0;
        if (!m_avoidance.IsPending() && m_bwe > 0.0 && m_minRtt != Time::Max() &&
            tcb->m_cWnd > tcb->m_ssThresh && tcb->m_cWnd < bweRttProduct && AllowGfrBoost())
        {
            m_avoidance = Simulator::Schedule(MilliSeconds(500),
                                              &TcpFrGfrJitter::CongestionAvoidanceEvent,
                                              this,
                                              tcb);
        }
    }
}

uint32_t
TcpFrGfrJitter::GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight)
{
    NS_LOG_FUNCTION(this << state << bytesInFlight);

    if (m_bwe > 0.0 && m_minRtt != Time::Max())
    {
        const uint32_t newSsThresh = CalculateBWEBasedSsThresh();
        return std::max(newSsThresh, 2 * state->m_segmentSize);
    }

    return std::max(2 * state->m_segmentSize, bytesInFlight / 2);
}

Ptr<TcpCongestionOps>
TcpFrGfrJitter::Fork()
{
    return CopyObject<TcpFrGfrJitter>(this);
}

void
TcpFrGfrJitter::CongestionAvoidanceEvent(Ptr<TcpSocketState> tcb)
{
    NS_LOG_FUNCTION(this << tcb);

    if (m_bwe <= 0.0 || m_minRtt == Time::Max())
    {
        return;
    }

    const double bweRttProduct = (m_bwe * m_minRtt.GetSeconds()) / 8.0;
    const bool gfrWindow = (tcb->m_cWnd > tcb->m_ssThresh) && (tcb->m_cWnd < bweRttProduct);

    if (gfrWindow && AllowGfrBoost())
    {
        const uint32_t oldSsThresh = tcb->m_ssThresh;
        tcb->m_ssThresh += static_cast<uint32_t>((bweRttProduct - tcb->m_ssThresh) / 2.0);
        NS_LOG_INFO("Jitter-aware GFR raised ssthresh from "
                    << oldSsThresh << " to " << tcb->m_ssThresh << " bytes");
    }
    else if (gfrWindow)
    {
        NS_LOG_INFO("Jitter-aware GFR suppressed boost at "
                    << Simulator::Now().GetSeconds() << " s because smoothed jitter="
                    << (m_smoothedJitterSeconds * 1000.0) << " ms exceeds threshold="
                    << m_jitterThreshold.GetMilliSeconds() << " ms");
    }

    if (gfrWindow && AllowGfrBoost())
    {
        m_avoidance = Simulator::Schedule(MilliSeconds(500),
                                          &TcpFrGfrJitter::CongestionAvoidanceEvent,
                                          this,
                                          tcb);
    }
}

uint32_t
TcpFrGfrJitter::CalculateBWEBasedSsThresh() const
{
    const double ssthreshBits = m_bwe * m_minRtt.GetSeconds();
    const double ssthreshBytes = ssthreshBits / 8.0;
    return static_cast<uint32_t>(ssthreshBytes / m_a);
}

bool
TcpFrGfrJitter::AllowGfrBoost() const
{
    if (!m_hasJitterSample)
    {
        return true;// No jitter data yet, allow boost by default
    }

    return Seconds(m_smoothedJitterSeconds) <= m_jitterThreshold;//smothed jitter tai sudden spike e sathe sathe block na hoye , 1/2 sample por 
}

} 
