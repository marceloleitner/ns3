/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007,2008,2009 INRIA, UDCAST
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
 * Author: Amine Ismail <amine.ismail@sophia.inria.fr>
 *                      <amine.ismail@udcast.com>
 */
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "udp-client.h"
#include "seq-ts-header.h"
#include <cstdlib>
#include <cstdio>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("UdpClient");

NS_OBJECT_ENSURE_REGISTERED (UdpClient);

TypeId
UdpClient::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::UdpClient")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<UdpClient> ()
    .AddAttribute ("MaxPackets",
                   "The maximum number of packets the application will send",
                   UintegerValue (100),
                   MakeUintegerAccessor (&UdpClient::m_count),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Interval",
                   "The time to wait between packets", TimeValue (Seconds (1.0)),
                   MakeTimeAccessor (&UdpClient::m_interval),
                   MakeTimeChecker ())
    .AddAttribute ("RemoteAddress",
                   "The destination Address of the outbound packets",
                   AddressValue (),
                   MakeAddressAccessor (&UdpClient::m_peerAddress),
                   MakeAddressChecker ())
    .AddAttribute ("RemotePort", "The destination port of the outbound packets",
                   UintegerValue (100),
                   MakeUintegerAccessor (&UdpClient::m_peerPort),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("PacketSize",
                   "Size of packets generated. The minimum packet size is 12 bytes which is the size of the header carrying the sequence number and the time stamp.",
                   UintegerValue (1024),
                   MakeUintegerAccessor (&UdpClient::m_size),
                   MakeUintegerChecker<uint32_t> (12,1500))
    .AddAttribute ("Fuzzy",
                   "Enable Fuzzy congestion control.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&UdpClient::enable_fuzzy),
		   MakeBooleanChecker ())
    .AddAttribute ("DelayTolerance",
		   "How much delay we can have in order to improve throughput",
		   DoubleValue (0.5),
		   MakeDoubleAccessor (&UdpClient::delay_tolerance),
		   MakeDoubleChecker<double>(0.0, 5.0))
    .AddTraceSource ("Drops", "Drops in the last second",
		     MakeTraceSourceAccessor (&UdpClient::m_ReportDrops),
		     "ns3::UdpClient::DropCallback")
    .AddTraceSource ("Delay", "Delay in the last second",
		     MakeTraceSourceAccessor (&UdpClient::m_ReportDelay),
		     "ns3::UdpClient::DelayCallback")
  ;
  return tid;
}

UdpClient::UdpClient ()
{
  NS_LOG_FUNCTION (this);
  m_sent = 0;
  m_socket = 0;
  m_sendEvent = EventId ();
  stopped = false;
}

UdpClient::~UdpClient ()
{
  NS_LOG_FUNCTION (this);
}

void
UdpClient::SetRemote (Address ip, uint16_t port)
{
  NS_LOG_FUNCTION (this << ip << port);
  m_peerAddress = ip;
  m_peerPort = port;
}

void
UdpClient::SetRemote (Address addr)
{
  NS_LOG_FUNCTION (this << addr);
  m_peerAddress = addr;
}

void
UdpClient::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
UdpClient::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      if (Ipv4Address::IsMatchingType(m_peerAddress) == true)
        {
          if (m_socket->Bind () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (InetSocketAddress (Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
      else if (Ipv6Address::IsMatchingType(m_peerAddress) == true)
        {
          if (m_socket->Bind6 () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (Inet6SocketAddress (Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
        }
      else if (InetSocketAddress::IsMatchingType (m_peerAddress) == true)
        {
          if (m_socket->Bind () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (m_peerAddress);
        }
      else if (Inet6SocketAddress::IsMatchingType (m_peerAddress) == true)
        {
          if (m_socket->Bind6 () == -1)
            {
              NS_FATAL_ERROR ("Failed to bind socket");
            }
          m_socket->Connect (m_peerAddress);
        }
      else
        {
          NS_ASSERT_MSG (false, "Incompatible address type: " << m_peerAddress);
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&UdpClient::HandleRead, this));
  m_socket->SetAllowBroadcast (true);
  m_sendEvent = Simulator::Schedule (Seconds (0.0), &UdpClient::Send, this);
}

void
UdpClient::HandleRead (Ptr<Socket> socket)
{
	Ptr<Packet> packet;
	Address from;

	while ((packet = socket->RecvFrom (from))) {
		if (packet->GetSize () <= 0)
			continue;

		if (stopped || m_sent >= m_count)
			continue;

		HandlePacket(packet, from);
	}
}

void
UdpClient::HandlePacket (Ptr<Packet> packet, Address &from)
{
	InetSocketAddress transport = InetSocketAddress::ConvertFrom (m_peerAddress);
	uint16_t port = transport.GetPort ();

	SeqTsHeader seqTs;
	packet->RemoveHeader (seqTs);
	drops = seqTs.GetSeq();
	delay = seqTs.GetTs().GetSeconds();

	m_ReportDrops(drops_old, drops);
	m_ReportDelay(delay_old, delay);

	if (delay_old < delay_min || delay_min == 0.0)
		delay_min = delay_old;

	std::cout << Simulator::Now().GetSeconds() << " " <<
		"Got a read from " << port << ": " <<
		"drops:" << drops << " " <<
		"delay:" << delay << " " <<
		"delay_min:" << delay_min << " " <<
		"\n";

	if (enable_fuzzy)
		adjust_rate(drops, delay);

	drops_old = drops;
	delay_old = delay;
}

void
UdpClient::adjust_rate(uint32_t drops, double delay)
{
	/* TODO: FUZZY.
	 * Aqui é onde faremos o processamento fuzzy para
	 * computar o novo intervalo entre as mensagens, baseado
	 * na latência que está sendo medida e na quantidade de
	 * pacotes perdidos.
	 * Temos disponível:
	 *   drops_old: quantidade descartada no intervalo anterior
	 *   drops: quantidade descartada no último intervalo
	 *   delay_old: delay médio no intervalo anterior
	 *   delay: delay médio no último intervalo
	 *   delay_min: delay mínimo medido até então
	 *   delay_tolerance: limite de delay permitido para sustentar
	 *      aumento de largura de banda em uso.
	 * Saídas:
	 *   interval: se deve aumentar ou diminuir o intervalo entre
	 *      pacotes, e em que proporção.
	 * Nota que:
	 *   quando drops passar a ser != 0, delay será levemente maior
	 *   que o permitido naquele momento. Pode servir de
	 *   referencial, assim como o delay e delay_old.
	 */
	if (!drops) {
		SetAttribute("Interval", TimeValue(Seconds(m_interval*0.75)));
		std::cout << "Speeding up! " << m_interval << "\n";
	} else {
		SetAttribute("Interval", TimeValue(Seconds(m_interval*2)));
		std::cout << "Slowing down! " << m_interval << "\n";
	}
}

void
UdpClient::StopApplication (void)
{
  NS_LOG_FUNCTION (this);
  Simulator::Cancel (m_sendEvent);
  stopped = true;
}

void
UdpClient::Send (void)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_sendEvent.IsExpired ());
  SeqTsHeader seqTs;
  seqTs.SetSeq (m_sent);
  Ptr<Packet> p = Create<Packet> (m_size-(8+4)); // 8+4 : the size of the seqTs header
  p->AddHeader (seqTs);

  std::stringstream peerAddressStringStream;
  if (Ipv4Address::IsMatchingType (m_peerAddress))
    {
      peerAddressStringStream << Ipv4Address::ConvertFrom (m_peerAddress);
    }
  else if (Ipv6Address::IsMatchingType (m_peerAddress))
    {
      peerAddressStringStream << Ipv6Address::ConvertFrom (m_peerAddress);
    }

  if ((m_socket->Send (p)) >= 0)
    {
      ++m_sent;
      NS_LOG_INFO ("TraceDelay TX " << m_size << " bytes to "
                                    << peerAddressStringStream.str () << " Uid: "
                                    << p->GetUid () << " Time: "
                                    << (Simulator::Now ()).GetSeconds ());

    }
  else
    {
      NS_LOG_INFO ("Error while sending " << m_size << " bytes to "
                                          << peerAddressStringStream.str ());
    }

  if (m_sent < m_count)
    {
      m_sendEvent = Simulator::Schedule (m_interval, &UdpClient::Send, this);
    }
}

} // Namespace ns3
