/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005, 2009 INRIA
 * Copyright (c) 2009 MIRKO BANCHI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 * Author: Mirko Banchi <mk.banchi@gmail.com>
 */
#ifndef WIFI_MAC_QUEUE_RED_H
#define WIFI_MAC_QUEUE_RED_H

#include <list>
#include <utility>
#include "ns3/packet.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include "wifi-mac-header.h"
#include "ns3/traced-callback.h"
#include "wifi-mac-queue.h"

#include "ns3/boolean.h"
#include "ns3/data-rate.h"

namespace ns3 {

class TraceContainer;
class UniformRandomVariable;

class WifiMacParameters;
class QosBlockedDestinations;

/**
 * \ingroup wifi
 *
 * This queue implements the timeout procedure described in IEEE
 * Std. 802.11-2007, section 9.9.1.6, paragraph 6.
 *
 * When a packet is received by the MAC, to be sent to the PHY,
 * it is queued in the internal queue after being tagged by the
 * current time.
 *
 * When a packet is dequeued, the queue checks its timestamp
 * to verify whether or not it should be dropped. If
 * dot11EDCATableMSDULifetime has elapsed, it is dropped.
 * Otherwise, it is returned to the caller.
 */
class WifiMacQueueRed : public WifiMacQueue
{
public:
  void MetodoSimples();
  static TypeId GetTypeId (void);

  WifiMacQueueRed ();
  ~WifiMacQueueRed ();

  typedef struct
  {
    // Early probability drops
    uint32_t unforcedDrop;
    // Forced drops, qavg > max threshold
    uint32_t forcedDrop;
    // Drops due to queue limits
    uint32_t qLimDrop;
  } Stats;

  /* 
   * \brief Drop types
   *
   */
  enum
  {
    DTYPE_NONE,        // Ok, no drop
    DTYPE_FORCED,      // A "forced" drop
    DTYPE_UNFORCED,    // An "unforced" (random) drop
  };

  void Enqueue (Ptr<const Packet> packet, const WifiMacHeader &hdr);
  Ptr<const Packet> Dequeue (WifiMacHeader *hdr);

  // ...
  void InitializeParams (void);
  // Compute the average queue size
  double Estimator (uint32_t nQueued, uint32_t m, double qAvg, double qW);
  // Check if packet p needs to be dropped due to probability mark
  uint32_t DropEarly (Ptr<const Packet> p, uint32_t qSize);
  // Returns a probability using these function parameters for the DropEarly funtion
  double CalculatePNew (double qAvg, double maxTh, bool gentle, double vA,
                        double vB, double vC, double vD, double maxP);
  // Returns a probability using these function parameters for the DropEarly funtion
  double ModifyP (double p, uint32_t count, uint32_t countBytes,
                  uint32_t meanPktSize, bool wait, uint32_t size);

  enum QueueMode
  {
    QUEUE_MODE_PACKETS,     /**< Use number of packets for maximum queue size */
    QUEUE_MODE_BYTES,       /**< Use number of bytes for maximum queue size */
  };

  /*
   * \brief Get the encapsulation mode of this queue.
   * Get the encapsulation mode of this queue
   *
   * \returns The encapsulation mode of this queue.
   */
  WifiMacQueueRed::QueueMode GetMode (void);

  bool m_hasRedStarted;
  Stats m_stats;

  // ** Variables supplied by user
  // Bytes or packets?
  QueueMode m_mode;
  // Avg pkt size
  uint32_t m_meanPktSize;
  // Avg pkt size used during idle times
  uint32_t m_idlePktSize;
  // True for waiting between dropped packets
  bool m_isWait;
  // True to increases dropping prob. slowly when ave queue exceeds maxthresh
  bool m_isGentle;
  // Min avg length threshold (bytes)
  double m_minTh;
  // Max avg length threshold (bytes), should be >= 2*minTh
  double m_maxTh;
  // Queue limit in bytes / packets
  uint32_t m_queueLimit;
  // Queue weight given to cur q size sample
  double m_qW;
  // The max probability of dropping a packet
  double m_lInterm;
  // Ns-1 compatibility
  bool m_isNs1Compat;
  // Link bandwidth
  DataRate m_linkBandwidth;
  // Link delay
  Time m_linkDelay;

  // ** Variables maintained by RED
  // Prob. of packet drop before "count"
  double m_vProb1;
  // v_prob = v_a * v_ave + v_b
  double m_vA;
  double m_vB;
  // Used for "gentle" mode
  double m_vC;
  // Used for "gentle" mode
  double m_vD;
  // Current max_p
  double m_curMaxP;
  // Prob. of packet drop
  double m_vProb;
  // # of bytes since last drop
  uint32_t m_countBytes;
  // 0 when average queue first exceeds thresh
  uint32_t m_old;
  // 0/1 idle status
  uint32_t m_idle;
  // packet time constant in packets/second
  double m_ptc;
  // Average queue length
  double m_qAvg;
  // number of packets since last random number generation
  uint32_t m_count;
  /*
   * 0 for default RED
   * 1 experimental (see red-queue.cc)
   * 2 experimental (see red-queue.cc)
   * 3 use Idle packet size in the ptc
   */
  uint32_t m_cautious;
  // Start of current idle period
  Time m_idleTime;

  Ptr<UniformRandomVariable> m_uv;

  // just to calculate avgpktsize
  int m_totalEnqueue;
  double m_totalBytes;
};

} // namespace ns3

#endif /* WIFI_MAC_QUEUE_RED_H */
