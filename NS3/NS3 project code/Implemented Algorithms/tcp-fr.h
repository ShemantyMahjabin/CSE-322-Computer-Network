#ifndef TCP_FR_H
#define TCP_FR_H

#include "tcp-congestion-ops.h"
#include "tcp-socket-base.h"
#include "ns3/event-id.h"
#include <deque>

namespace ns3 {

class TcpSocketState;


class TcpFr : public TcpNewReno
{
private:
    double m_alpha;              
    uint32_t m_a;                
    double m_BWE;                
    Time m_laststacktime;        
    Time m_minRTT;             
    bool m_firstPacket; 

             

public:
    static TypeId GetTypeId(void);
    TcpFr();
    TcpFr(const TcpFr& other);
    virtual ~TcpFr();
    virtual std::string GetName() const override;// name of the congestion control algorithm
    virtual void PktsAcked(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,  const Time& rtt) override;
    virtual void CongestionStateSet(Ptr<TcpSocketState> tcb, const TcpSocketState::TcpCongState_t newState) override;
    virtual void IncreaseWindow(Ptr<TcpSocketState> tcb, uint32_t segmentsAcked) override;
    virtual uint32_t GetSsThresh(Ptr<const TcpSocketState> state, uint32_t bytesInFlight) override;
    virtual Ptr<TcpCongestionOps> Fork() override;//Copy the congestion control algorithm

private:
    uint32_t CalculateBWEBasedSsThresh() const;
};

}

#endif 