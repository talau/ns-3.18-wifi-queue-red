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

#include "wifi-mac-queue.h"
#include "qos-blocked-destinations.h"

#include "ns3/trace-source-accessor.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (WifiMacQueue);

WifiMacQueue::Item::Item (Ptr<Packet> packet,
                          const WifiMacHeader &hdr,
                          Time tstamp)
  : packet (packet),
    hdr (hdr),
    tstamp (tstamp)
{
}

TypeId
WifiMacQueue::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::WifiMacQueue")
    .SetParent<Object> ()
    .AddConstructor<WifiMacQueue> ()
    .AddAttribute ("MaxPacketNumber", "If a packet arrives when there are already this number of packets, it is dropped.",
                   UintegerValue (400),
                   MakeUintegerAccessor (&WifiMacQueue::m_maxSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxDelay", "If a packet stays longer than this delay in the queue, it is dropped.",
                   TimeValue (Seconds (10.0)),
                   MakeTimeAccessor (&WifiMacQueue::m_maxDelay),
                   MakeTimeChecker ())
    .AddTraceSource ("WifiQueueDrop",
                     "Drop in a wifi queue.",
                     MakeTraceSourceAccessor (&WifiMacQueue::m_wifiQueueDropTrace))
  ;
  return tid;
}

WifiMacQueue::WifiMacQueue ()
  : m_size (0)
{
  m_inAp = false;
  m_maxBytes = 100 * 1000;
  std::cout << "Queuesize " << m_maxBytes << std::endl;
  m_bytesInQueue = 0;
  m_totalDrops = 0;
}

WifiMacQueue::~WifiMacQueue ()
{
  //  std::cout << "m_totalDrops = " << m_totalDrops << std::endl;
  //  std::cout << "ecc_q = " << ecc_q << std::endl;
  Flush ();
}

// to update bytes in queue and call mm method
void
WifiMacQueue::DequeueProcess(Ptr<Packet> packet)
{
  //  if (InAp())
  //    std::cout << "InAp!" << std::endl;

// inAp() off for mesh
//  if (InAp())

  m_bytesInQueue -= packet->GetSize();
}

void
WifiMacQueue::SetMaxSize (uint32_t maxSize)
{
  m_maxSize = maxSize;
}

void
WifiMacQueue::SetMaxDelay (Time delay)
{
  m_maxDelay = delay;
}

uint32_t
WifiMacQueue::GetMaxSize (void) const
{
  return m_maxSize;
}

Time
WifiMacQueue::GetMaxDelay (void) const
{
  return m_maxDelay;
}

void
WifiMacQueue::Enqueue (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
  Cleanup ();

  //  std::cout << "enqueue in default wifi-mac-queue" << std::endl;

  //  std::cout << "pkt size " << packet->GetSize() << std::endl;

  if (InAp()) {
    //    std::cout << "queue size in ap " << m_bytesInQueue << std::endl;
  }

  if (m_bytesInQueue + packet->GetSize () >= m_maxBytes) {
    //    if (InAp())
    //      std::cout << "queue full in AP" << std::endl;
    m_wifiQueueDropTrace (packet);
    m_totalDrops++;
    return;
  }

  // if (m_size == m_maxSize)
  //   {
  //     if (m_inAp)
  //       std::cout << "queue in ap" << std::endl;
  //     NS_ABORT_MSG("queue full");
  //     return;
  //   }


  //  if (m_inAp)
  //    std::cout << "queue size: " << m_size << std::endl;
  Time now = Simulator::Now ();
  Ptr<Packet> aCopy = packet->Copy ();
  m_queue.push_back (Item (aCopy, hdr, now));
  m_size++;
  m_bytesInQueue += packet->GetSize();
}

void
WifiMacQueue::Cleanup (void)
{
  if (m_queue.empty ())
    {
      return;
    }

  Time now = Simulator::Now ();
  uint32_t n = 0;
  for (PacketQueueI i = m_queue.begin (); i != m_queue.end ();)
    {
      if (i->tstamp + m_maxDelay > now)
        {
          i++;
        }
      else
        {
          //          std::cout << "erase" << std::endl;
          m_bytesInQueue -= i->packet->GetSize();
          i = m_queue.erase (i);
          n++;
        }
    }
  //  if (n !=0)
  //    NS_ABORT_MSG("not used 1?");

  m_size -= n;
}

uint32_t
WifiMacQueue::GetBytesAvailable ()
{
  uint32_t max_available = m_maxBytes - m_bytesInQueue;
  //  uint32_t max_available = m_maxBytes - m_avgPktSize * m_size;

  if (max_available < 0 )
    max_available = 0;

  return max_available;
}

Ptr<const Packet>
WifiMacQueue::Dequeue (WifiMacHeader *hdr)
{
  Cleanup ();
  if (!m_queue.empty ())
    {
      Item i = m_queue.front ();
      m_queue.pop_front ();
      m_size--;
      *hdr = i.hdr;
      DequeueProcess(i.packet);
      return i.packet;
    }
  return 0;
}

Ptr<const Packet>
WifiMacQueue::Peek (WifiMacHeader *hdr)
{
  Cleanup ();
  NS_ABORT_MSG ("not used by RED");
  if (!m_queue.empty ())
    {
      Item i = m_queue.front ();
      *hdr = i.hdr;
      return i.packet;
    }
  return 0;
}

Ptr<const Packet>
WifiMacQueue::DequeueByTidAndAddress (WifiMacHeader *hdr, uint8_t tid,
                                      WifiMacHeader::AddressType type, Mac48Address dest)
{
  NS_ABORT_MSG("not used 2?");
  NS_ABORT_MSG ("not used by RED");

  Cleanup ();
  Ptr<const Packet> packet = 0;
  if (!m_queue.empty ())
    {
      PacketQueueI it;
      NS_ASSERT (type <= 4);
      for (it = m_queue.begin (); it != m_queue.end (); ++it)
        {
          if (it->hdr.IsQosData ())
            {
              if (GetAddressForPacket (type, it) == dest
                  && it->hdr.GetQosTid () == tid)
                {
                  packet = it->packet;
                  *hdr = it->hdr;
                  std::cout << "erase" << std::endl;
                  m_queue.erase (it);
                  m_size--;
                  break;
                }
            }
        }
    }
  return packet;
}

Ptr<const Packet>
WifiMacQueue::PeekByTidAndAddress (WifiMacHeader *hdr, uint8_t tid,
                                   WifiMacHeader::AddressType type, Mac48Address dest)
{
  Cleanup ();
  NS_ABORT_MSG ("not used by RED");
  if (!m_queue.empty ())
    {
      PacketQueueI it;
      NS_ASSERT (type <= 4);
      for (it = m_queue.begin (); it != m_queue.end (); ++it)
        {
          if (it->hdr.IsQosData ())
            {
              if (GetAddressForPacket (type, it) == dest
                  && it->hdr.GetQosTid () == tid)
                {
                  *hdr = it->hdr;
                  return it->packet;
                }
            }
        }
    }
  return 0;
}

bool
WifiMacQueue::IsEmpty (void)
{
  Cleanup ();
  return m_queue.empty ();
}

uint32_t
WifiMacQueue::GetSize (void)
{
  return m_size;
}

void
WifiMacQueue::Flush (void)
{
  m_bytesInQueue = 0;
  m_queue.erase (m_queue.begin (), m_queue.end ());
  m_size = 0;
}

Mac48Address
WifiMacQueue::GetAddressForPacket (enum WifiMacHeader::AddressType type, PacketQueueI it)
{
  if (type == WifiMacHeader::ADDR1)
    {
      return it->hdr.GetAddr1 ();
    }
  if (type == WifiMacHeader::ADDR2)
    {
      return it->hdr.GetAddr2 ();
    }
  if (type == WifiMacHeader::ADDR3)
    {
      return it->hdr.GetAddr3 ();
    }
  return 0;
}

bool
WifiMacQueue::Remove (Ptr<const Packet> packet)
{
  PacketQueueI it = m_queue.begin ();
  for (; it != m_queue.end (); it++)
    {
      if (it->packet == packet)
        {
          NS_ABORT_MSG("not used 3?");
          std::cout << "erase" << std::endl;
          m_queue.erase (it);
          m_size--;
          return true;
        }
    }
  return false;
}

void
WifiMacQueue::PushFront (Ptr<const Packet> packet, const WifiMacHeader &hdr)
{
    NS_ABORT_MSG("not used 4?");
  Cleanup ();
  if (m_size == m_maxSize)
    {
      return;
    }
  Time now = Simulator::Now ();
  Ptr<Packet> aCopy = packet->Copy ();
  m_queue.push_front (Item (aCopy, hdr, now));
  m_size++;
}

uint32_t
WifiMacQueue::GetNPacketsByTidAndAddress (uint8_t tid, WifiMacHeader::AddressType type,
                                          Mac48Address addr)
{
  Cleanup ();
  uint32_t nPackets = 0;
  if (!m_queue.empty ())
    {
      PacketQueueI it;
      NS_ASSERT (type <= 4);
      for (it = m_queue.begin (); it != m_queue.end (); it++)
        {
          if (GetAddressForPacket (type, it) == addr)
            {
              if (it->hdr.IsQosData () && it->hdr.GetQosTid () == tid)
                {
                  nPackets++;
                }
            }
        }
    }
  return nPackets;
}

Ptr<const Packet>
WifiMacQueue::DequeueFirstAvailable (WifiMacHeader *hdr, Time &timestamp,
                                     const QosBlockedDestinations *blockedPackets)
{
  Cleanup ();
  NS_ABORT_MSG ("not used by RED");

  Ptr<const Packet> packet = 0;
  for (PacketQueueI it = m_queue.begin (); it != m_queue.end (); it++)
    {
      if (!it->hdr.IsQosData ()
          || !blockedPackets->IsBlocked (it->hdr.GetAddr1 (), it->hdr.GetQosTid ()))
        {
          *hdr = it->hdr;
          timestamp = it->tstamp;
          packet = it->packet;
          DequeueProcess(it->packet);
          m_queue.erase (it);
          m_size--;
          return packet;
        }
    }
  return packet;
}

Ptr<const Packet>
WifiMacQueue::PeekFirstAvailable (WifiMacHeader *hdr, Time &timestamp,
                                  const QosBlockedDestinations *blockedPackets)
{
  Cleanup ();
  NS_ABORT_MSG ("not used by RED");

  for (PacketQueueI it = m_queue.begin (); it != m_queue.end (); it++)
    {
      if (!it->hdr.IsQosData ()
          || !blockedPackets->IsBlocked (it->hdr.GetAddr1 (), it->hdr.GetQosTid ()))
        {
          *hdr = it->hdr;
          timestamp = it->tstamp;
          return it->packet;
        }
    }
  return 0;
}

bool
WifiMacQueue::InAp() {
  return m_inAp;
}

// void 
// WifiMacQueue::MetodoSimples()
// {
//   std::cout << "metodo simples (droptail)" << std::endl;
// }

} // namespace ns3
