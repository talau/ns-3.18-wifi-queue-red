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

#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"

#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/llc-snap-header.h"
#include "ns3/flame-header.h"
#include "ns3/flame-protocol.h"
#include "ns3/ipv4-header.h"
#include "ns3/tcp-header.h"

#include "wifi-mac-queue-red.h"
#include "qos-blocked-destinations.h"

#include "ns3/trace-source-accessor.h"

#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/random-variable-stream.h"
#include "ns3/string.h"

NS_LOG_COMPONENT_DEFINE ("WifiMacQueueRed");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (WifiMacQueueRed);

TypeId WifiMacQueueRed::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::WifiMacQueueRed")
    .SetParent<WifiMacQueue> ()
    .AddConstructor<WifiMacQueueRed> ();

  return tid;
}

WifiMacQueueRed::WifiMacQueueRed ()
{
  std::cout << "WifiMacQueueREDDDDD created" << std::endl;

  m_size = 0;
  m_inAp = false;
  m_maxBytes = 100 * 1000; // incrivelmente com fila de 25 os resultados foram otimos
  //  m_maxBytes = 25 * 100; // for grid

  std::cout << "Queuesize " << m_maxBytes << std::endl;

  m_bytesInQueue = 0;
  m_totalDrops = 0;

  m_hasRedStarted = false;
  m_uv = CreateObject<UniformRandomVariable> ();

  //  m_totalEnqueue = 0;
  //  m_totalBytes = 0;
}

WifiMacQueueRed::~WifiMacQueueRed ()
{
  //  std::cout << "m_totalDrops = " << m_totalDrops << std::endl;
  //  std::cout << "(RED) Real avgPktSize = " << m_totalBytes / m_totalEnqueue << std::endl;
  Flush ();
}

void 
WifiMacQueueRed::MetodoSimples()
{
  std::cout << "metodo simples en red" << std::endl;
}

void
WifiMacQueueRed::Enqueue (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
  Cleanup ();

  //  std::cout << "Enqueue in RED" << std::endl;

  if (!m_hasRedStarted )
    {
      NS_LOG_INFO ("Initializing RED params.");
      InitializeParams ();
      m_hasRedStarted = true;
    }

  uint32_t nQueued = 0;

  if (GetMode () == QUEUE_MODE_BYTES)
    {
      NS_LOG_DEBUG ("Enqueue in bytes mode");
      nQueued = m_bytesInQueue;
    }
  else if (GetMode () == QUEUE_MODE_PACKETS)
    {
      NS_ABORT_MSG ("packet mode is off");
      NS_LOG_DEBUG ("Enqueue in packets mode");
      nQueued = m_queue.size ();
    }

  // simulate number of packets arrival during idle period
  uint32_t m = 0;

  if (m_idle == 1)
    {
      NS_LOG_DEBUG ("RED Queue is idle.");
      Time now = Simulator::Now ();

      if (m_cautious == 3)
        {
          double ptc = m_ptc * m_meanPktSize / m_idlePktSize;
          m = uint32_t (ptc * (now - m_idleTime).GetSeconds ());
        }
      else
        {
          m = uint32_t (m_ptc * (now - m_idleTime).GetSeconds ());
        }

      m_idle = 0;
    }

  m_qAvg = Estimator (nQueued, m + 1, m_qAvg, m_qW);

  NS_LOG_DEBUG ("\t bytesInQueue  " << m_bytesInQueue << "\tQavg " << m_qAvg);
  NS_LOG_DEBUG ("\t packetsInQueue  " << m_queue.size () << "\tQavg " << m_qAvg);

  m_count++;
  m_countBytes += packet->GetSize ();

  uint32_t dropType = DTYPE_NONE;
  if (m_qAvg >= m_minTh && nQueued > 1)
    {
      if ((!m_isGentle && m_qAvg >= m_maxTh) ||
          (m_isGentle && m_qAvg >= 2 * m_maxTh))
        {
          NS_LOG_DEBUG ("adding DROP FORCED MARK");
          dropType = DTYPE_FORCED;
        }
      else if (m_old == 0)
        {
          /* 
           * The average queue size has just crossed the
           * threshold from below to above "minthresh", or
           * from above "minthresh" with an empty queue to
           * above "minthresh" with a nonempty queue.
           */
          m_count = 1;
          m_countBytes = packet->GetSize ();
          m_old = 1;
        }
      else if (DropEarly (packet, nQueued))
        {
          NS_LOG_LOGIC ("DropEarly returns 1");
          dropType = DTYPE_UNFORCED;
        }
    }
  else 
    {
      // No packets are being dropped
      m_vProb = 0.0;
      m_old = 0;
    }

  if (nQueued >= m_queueLimit)
    {
      NS_LOG_DEBUG ("\t Dropping due to Queue Full " << nQueued);
      dropType = DTYPE_FORCED;
      m_stats.qLimDrop++;
    }

  if (dropType == DTYPE_UNFORCED)
    {
      NS_LOG_DEBUG ("\t Dropping due to Prob Mark " << m_qAvg);
      m_stats.unforcedDrop++;
      m_totalDrops++;
      m_wifiQueueDropTrace (packet);
      return;
    }
  else if (dropType == DTYPE_FORCED)
    {
      NS_LOG_DEBUG ("\t Dropping due to Hard Mark " << m_qAvg);
      m_stats.forcedDrop++;
      if (m_isNs1Compat)
        {
          m_count = 0;
          m_countBytes = 0;
        }
      m_totalDrops++;
      m_wifiQueueDropTrace (packet);
      return;
    }

  //  std::cout << "pkt size " << packet->GetSize() << std::endl;

  //  if (m_inAp)
  //    std::cout << "queue size: " << m_size << std::endl;
  Time now = Simulator::Now ();
  Ptr<Packet> aCopy = packet->Copy ();
  m_queue.push_back (Item (aCopy, hdr, now));
  m_size++;
  m_bytesInQueue += packet->GetSize();

  // just to calcule avgpktsize
  //  m_totalEnqueue++;
  //  m_totalBytes += packet->GetSize();
}

Ptr<const Packet>
WifiMacQueueRed::Dequeue (WifiMacHeader *hdr)
{
  Cleanup ();

  //  std::cout << "Dequeue in RED" << std::endl;

  if (m_queue.empty ())
    {
      NS_LOG_LOGIC ("Queue empty");
      m_idle = 1;
      m_idleTime = Simulator::Now ();

      return 0;
    }
  else
    {
      m_idle = 0;

      Item i = m_queue.front ();
      m_queue.pop_front ();
      m_size--;
      *hdr = i.hdr;
      m_bytesInQueue -= i.packet->GetSize();

      NS_LOG_LOGIC ("Popped " << i.packet);

      NS_LOG_LOGIC ("Number packets " << m_queue.size ());
      NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);
      return i.packet;
    }
}

/*
 * Note: if the link bandwidth changes in the course of the
 * simulation, the bandwidth-dependent RED parameters do not change.
 * This should be fixed, but it would require some extra parameters,
 * and didn't seem worth the trouble...


maxth = 60% of queue size
minth = maxth / 3

 */
void
WifiMacQueueRed::InitializeParams (void)
{
  NS_LOG_FUNCTION (this);

  ///////////////////////////////////////////  // Fixed params
  uint32_t tcp_segment_size = 1000;
  uint32_t bytes_queue_size = m_maxBytes; // default in wifi-mac-queue
  std::string backboneLinkDataRate = "6Mbps";
  std::string backboneLinkDelay = "0ms";

  //  uint32_t meanPktSize = tcp_segment_size;
  // avg in tests
  uint32_t meanPktSize = 530;
  std::string redLinkDataRate = backboneLinkDataRate;
  std::string redLinkDelay = backboneLinkDelay;


  //  Config::SetDefault ("ns3::RedQueue::Mode", StringValue("Bytes"));
  m_mode = QUEUE_MODE_BYTES;

  //  Config::SetDefault ("ns3::RedQueue::QueueLimit", UintegerValue (bytes_queue_size));
  m_queueLimit = bytes_queue_size;

  //  Config::SetDefault ("ns3::RedQueue::MeanPktSize", UintegerValue (meanPktSize));
  m_meanPktSize = meanPktSize;

  //  Config::SetDefault ("ns3::RedQueue::Wait", BooleanValue (true));
  m_isWait = true;

  //  Config::SetDefault ("ns3::RedQueue::Gentle", BooleanValue (true));
  m_isGentle = true;

  //  Config::SetDefault ("ns3::RedQueue::QW", DoubleValue (0.002));
  m_qW = 0.002;

  // 3 KB
  //  Config::SetDefault ("ns3::RedQueue::MinTh", DoubleValue (3 * 1024));
  ////  m_minTh = 3 * 1024;
  m_minTh = 20 * 1024;

  // 48.5 KB
  //  Config::SetDefault ("ns3::RedQueue::MaxTh", DoubleValue (48 * 1024 + 512));
  ////  m_maxTh = 48 * 1024 + 512;
  m_maxTh = 60 * 1024 + 512;

  //  Config::SetDefault ("ns3::RedQueue::LinkBandwidth", StringValue(redLinkDataRate));
  m_linkBandwidth = DataRate(redLinkDataRate);

  //Config::SetDefault ("ns3::RedQueue::LinkDelay", StringValue(redLinkDelay));
  m_linkDelay = Seconds(0);

  //  Config::SetDefault ("ns3::RedQueue::Ns1Compat", BooleanValue (true));
  m_isNs1Compat = true;

  std::cout << "linkbitrate = " << m_linkBandwidth.GetBitRate() << std::endl;
  /////////////////////////////////////////////////////////////////


  NS_ASSERT (m_minTh <= m_maxTh);
  m_stats.forcedDrop = 0;
  m_stats.unforcedDrop = 0;
  m_stats.qLimDrop = 0;

  m_cautious = 0;
  m_ptc = m_linkBandwidth.GetBitRate () / (8.0 * m_meanPktSize);

  m_qAvg = 0.0;
  m_count = 0;
  m_countBytes = 0;
  m_old = 0;
  m_idle = 1;

  double th_diff = (m_maxTh - m_minTh);
  if (th_diff == 0)
    {
      th_diff = 1.0; 
    }
  m_vA = 1.0 / th_diff;
  m_curMaxP = 1.0 / m_lInterm;
  m_vB = -m_minTh / th_diff;

  if (m_isGentle)
    {
      m_vC = (1.0 - m_curMaxP) / m_maxTh;
      m_vD = 2.0 * m_curMaxP - 1.0;
    }
  m_idleTime = NanoSeconds (0);

/*
 * If m_qW=0, set it to a reasonable value of 1-exp(-1/C)
 * This corresponds to choosing m_qW to be of that value for
 * which the packet time constant -1/ln(1-m)qW) per default RTT 
 * of 100ms is an order of magnitude more than the link capacity, C.
 *
 * If m_qW=-1, then the queue weight is set to be a function of
 * the bandwidth and the link propagation delay.  In particular, 
 * the default RTT is assumed to be three times the link delay and 
 * transmission delay, if this gives a default RTT greater than 100 ms. 
 *
 * If m_qW=-2, set it to a reasonable value of 1-exp(-10/C).
 */
  if (m_qW == 0.0)
    {
      m_qW = 1.0 - std::exp (-1.0 / m_ptc);
    }
  else if (m_qW == -1.0)
    {
      double rtt = 3.0 * (m_linkDelay.GetSeconds () + 1.0 / m_ptc);

      if (rtt < 0.1)
        {
          rtt = 0.1;
        }
      m_qW = 1.0 - std::exp (-1.0 / (10 * rtt * m_ptc));
    }
  else if (m_qW == -2.0)
    {
      m_qW = 1.0 - std::exp (-10.0 / m_ptc);
    }

  /// \todo implement adaptive RED

  NS_LOG_DEBUG ("\tm_delay " << m_linkDelay.GetSeconds () << "; m_isWait " 
                             << m_isWait << "; m_qW " << m_qW << "; m_ptc " << m_ptc
                             << "; m_minTh " << m_minTh << "; m_maxTh " << m_maxTh
                             << "; m_isGentle " << m_isGentle << "; th_diff " << th_diff
                             << "; lInterm " << m_lInterm << "; va " << m_vA <<  "; cur_max_p "
                             << m_curMaxP << "; v_b " << m_vB <<  "; m_vC "
                             << m_vC << "; m_vD " <<  m_vD);
}

// Compute the average queue size
double
WifiMacQueueRed::Estimator (uint32_t nQueued, uint32_t m, double qAvg, double qW)
{
  NS_LOG_FUNCTION (this << nQueued << m << qAvg << qW);
  double newAve;

  newAve = qAvg;
  while (--m >= 1)
    {
      newAve *= 1.0 - qW;
    }
  newAve *= 1.0 - qW;
  newAve += qW * nQueued;

  // implement adaptive RED

  return newAve;
}

// Check if packet p needs to be dropped due to probability mark
uint32_t
WifiMacQueueRed::DropEarly (Ptr<const Packet> p, uint32_t qSize)
{
  NS_LOG_FUNCTION (this << p << qSize);
  m_vProb1 = CalculatePNew (m_qAvg, m_maxTh, m_isGentle, m_vA, m_vB, m_vC, m_vD, m_curMaxP);
  m_vProb = ModifyP (m_vProb1, m_count, m_countBytes, m_meanPktSize, m_isWait, p->GetSize ());

  // Drop probability is computed, pick random number and act
  if (m_cautious == 1)
    {
      /*
       * Don't drop/mark if the instantaneous queue is much below the average.
       * For experimental purposes only.
       * pkts: the number of packets arriving in 50 ms
       */
      double pkts = m_ptc * 0.05;
      double fraction = std::pow ((1 - m_qW), pkts);

      if ((double) qSize < fraction * m_qAvg)
        {
          // Queue could have been empty for 0.05 seconds
          return 0;
        }
    }

  double u = m_uv->GetValue ();
  //  std::cout << "m_uv = " << u << std::endl;

  if (m_cautious == 2)
    {
      /*
       * Decrease the drop probability if the instantaneous
       * queue is much below the average.
       * For experimental purposes only.
       * pkts: the number of packets arriving in 50 ms
       */
      double pkts = m_ptc * 0.05;
      double fraction = std::pow ((1 - m_qW), pkts);
      double ratio = qSize / (fraction * m_qAvg);

      if (ratio < 1.0)
        {
          u *= 1.0 / ratio;
        }
    }

  if (u <= m_vProb)
    {
      NS_LOG_LOGIC ("u <= m_vProb; u " << u << "; m_vProb " << m_vProb);

      // DROP or MARK
      m_count = 0;
      m_countBytes = 0;
      /// \todo Implement set bit to mark

      return 1; // drop
    }

  return 0; // no drop/mark
}

// Returns a probability using these function parameters for the DropEarly funtion
double
WifiMacQueueRed::CalculatePNew (double qAvg, double maxTh, bool isGentle, double vA,
                         double vB, double vC, double vD, double maxP)
{
  NS_LOG_FUNCTION (this << qAvg << maxTh << isGentle << vA << vB << vC << vD << maxP);
  double p;

  if (isGentle && qAvg >= maxTh)
    {
      // p ranges from maxP to 1 as the average queue
      // Size ranges from maxTh to twice maxTh
      p = vC * qAvg + vD;
    }
  else if (!isGentle && qAvg >= maxTh)
    {
      /* 
       * OLD: p continues to range linearly above max_p as
       * the average queue size ranges above th_max.
       * NEW: p is set to 1.0
       */
      p = 1.0;
    }
  else
    {
      /*
       * p ranges from 0 to max_p as the average queue size ranges from
       * th_min to th_max
       */
      p = vA * qAvg + vB;
      p *= maxP;
    }

  if (p > 1.0)
    {
      p = 1.0;
    }

  return p;
}

// Returns a probability using these function parameters for the DropEarly funtion
double 
WifiMacQueueRed::ModifyP (double p, uint32_t count, uint32_t countBytes,
                   uint32_t meanPktSize, bool isWait, uint32_t size)
{
  NS_LOG_FUNCTION (this << p << count << countBytes << meanPktSize << isWait << size);
  double count1 = (double) count;

  if (GetMode () == QUEUE_MODE_BYTES)
    {
      count1 = (double) (countBytes / meanPktSize);
    }

  if (isWait)
    {
      if (count1 * p < 1.0)
        {
          p = 0.0;
        }
      else if (count1 * p < 2.0)
        {
          p /= (2.0 - count1 * p);
        }
      else
        {
          p = 1.0;
        }
    }
  else
    {
      if (count1 * p < 1.0)
        {
          p /= (1.0 - count1 * p);
        }
      else
        {
          p = 1.0;
        }
    }

  if ((GetMode () == QUEUE_MODE_BYTES) && (p < 1.0))
    {
      p = (p * size) / meanPktSize;
    }

  if (p > 1.0)
    {
      p = 1.0;
    }

  return p;
}

WifiMacQueueRed::QueueMode
WifiMacQueueRed::GetMode (void)
{
  NS_LOG_FUNCTION (this);
  return m_mode;
}

} // namespace ns3
