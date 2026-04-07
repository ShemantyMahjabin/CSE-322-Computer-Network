#ifndef TCP_FR_GFR_H
#define TCP_FR_GFR_H

#include "tcp-congestion-ops.h"
#include "tcp-socket-base.h"
#include "ns3/event-id.h"

namespace ns3 {

class TcpSocketState;


class TcpFrGfr : public TcpNewReno
{
private:
    double m_alpha;              
    uint32_t m_a;               
    double m_BWE;                
    Time m_laststacktime;        
    Time m_minRTT;               
    EventId m_avoidance;         
    bool m_firstPacket;         

public:
    static TypeId GetTypeId(void);
    TcpFrGfr();
    TcpFrGfr(const TcpFrGfr& other);
    virtual ~TcpFrGfr();
    virtual std::string GetName() const override;
    virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,  const Time& rtt) override;
    virtual void CongestionStateSet(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState) override;
    virtual void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    virtual uint32_t GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight) override;
    virtual Ptr<TcpCongestionOps> Fork() override;

private:
    void CongestionAvoidanceEvent(Ptr<TcpSocketState> tcb); //500ms por por call kore check kora j ssthresh increase kora uchit kina GFR-TCP algorithm onujayi
    uint32_t CalculateBWEBasedSsThresh() const;
};

}

#endif 