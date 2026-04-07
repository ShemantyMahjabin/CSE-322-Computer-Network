#ifndef TCP_FR_GFR_JITTER_H
#define TCP_FR_GFR_JITTER_H

#include "ns3/event-id.h"
#include "tcp-congestion-ops.h"
#include "tcp-socket-base.h"

namespace ns3
{

class TcpSocketState;
class TcpFrGfrJitter : public TcpNewReno
{
  public:
    static TypeId GetTypeId();

    TcpFrGfrJitter();
    TcpFrGfrJitter(const TcpFrGfrJitter& other);
    ~TcpFrGfrJitter() override;

    std::string GetName() const override;
    void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt) override;
    void CongestionStateSet(Ptr<TcpSocketState> tcb,
                            TcpSocketState::TcpCongState_t newState) override;
    void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    uint32_t GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight) override;
    Ptr<TcpCongestionOps> Fork() override;

  private:
    void CongestionAvoidanceEvent(Ptr<TcpSocketState> tcb);
    uint32_t CalculateBWEBasedSsThresh() const;
    bool AllowGfrBoost() const;

    double m_alpha;
    uint32_t m_a;
    double m_bwe;
    Time m_lastAckTime;
    Time m_minRtt;
    EventId m_avoidance;
    bool m_firstPacket;
    Time m_jitterThreshold;
    double m_jitterSmoothingGain;
    double m_smoothedJitterSeconds;
    bool m_hasJitterSample;
};

} 

#endif 
