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

#define min(x, y) (x <= y ? x : y)
#define max(x, y) (x >= y ? x : y)

FuzzyVar::FuzzyVar(std::string _nome, int32_t a, int32_t b)
{
	nome = _nome;
	limits[0] = a;
	limits[1] = b;
}

void FuzzyVar::add_set(std::string name, int32_t a, int32_t b, int32_t c)
{
	std::array<int32_t, 3> points = { a, b, c };

	sets.insert({name, points});
}

double FuzzyVar::activation(std::string set, double value)
{
	std::array<int32_t, 3> p;
	double ret;

	if (value < limits[0] || value > limits[1])
		// Out of scale
		return 0.0;

	p = sets[set];
	if (value < p[0] || value > p[2])
		// Out of the set
		return 0.0;

	if (value < p[1])
		ret = (value-p[0])/(p[1]-p[0]);
	else
		ret = (value-p[2])/(p[1]-p[2]);

	return ret;
}

std::vector<std::string> FuzzyVar::get_sets(void)
{
	std::vector<std::string> keys;

	for (auto &n: sets)
		keys.push_back(n.first);

	return keys;
}

double FuzzyVar::get_limit_min(void) const
{
	return limits[0];
}

double FuzzyVar::get_limit_max(void) const
{
	return limits[1];
}


FuzzyVarIn::FuzzyVarIn(std::string _nome, int32_t a, int32_t b):
	FuzzyVar(_nome, a, b)
{
}


FuzzyVarOut::FuzzyVarOut(std::string _name, int32_t a, int32_t b):
	FuzzyVar(_name, a, b)
{
}

void FuzzyVarOut::add_set(std::string name, int32_t a, int32_t b, int32_t c)
{
	FuzzyVar::add_set(name, a, b, c);
	u_max.insert({name, 0.0});
}

double FuzzyVarOut::activation(std::string set, double value)
{
	double p;

	p = FuzzyVar::activation(set, value);
	// Implication rule
	p = min(get_u_max(set), p);

	return p;
}

void FuzzyVarOut::reset(std::string set)
{
	set_u_max(set, 0.0);
}

void FuzzyVarOut::reset(void)
{
	for (auto &n: sets)
		reset(n.first);
}

double FuzzyVarOut::get_u_max(std::string set)
{
	return u_max[set];
}

void FuzzyVarOut::set_u_max(std::string set, double u)
{
	u_max[set] = u;
}


Fuzzy::Fuzzy()
{
}

void Fuzzy::set_vars(FuzzyVarIn *in1, FuzzyVarIn *in2, FuzzyVarOut *_out)
{
	in[0] = in1;
	in[1] = in2;
	out = _out;
}

void Fuzzy::add_rule(std::string in1, std::string in2, std::string out)
{
	std::array<std::string, 3> rule = {in1, in2, out};
	rules.push_back(rule);
}

double Fuzzy::op_And(double x1, double x2)
{
	return min(x1, x2);
}

double Fuzzy::op_Agg(double x1, double x2)
{
	return max(x1, x2);
}

double Fuzzy::eval(double x1, double x2)
{
	x2 *= 1000000.0; // It comes in seconds, we need it in us
	out->reset();

	for (auto &rule: rules) {
		double _x1, _x2, r, u_max;

		_x1 = in[0]->activation(rule[0], x1);
		_x2 = in[1]->activation(rule[1], x2);
		r = op_And(_x1, _x2);
#if 0
		std::cout << rule[0] << " " << x1 << " " << _x1 << " "
			<< rule[1] << " " << x2 << " " << _x2 << " "
			<< rule[2] << " " << r << " "
			<< "\n";
#endif

		u_max = out->get_u_max(rule[2]);
		out->set_u_max(rule[2], op_Agg(r, u_max));
	}

	double weight;
	double a = 0;
	double STEP = 1; // não pode ser maior que 1
	double i;

	for (i = out->get_limit_min(); i < out->get_limit_max(); i += STEP) {
		double u = 0, u_;

		for (auto &set: out->get_sets()) {
			u_ = out->activation(set, i);
			u = op_Agg(u, u_);
		}

		weight += u*i;
		a += u;
	}

	weight = weight/a;
	weight = max(weight, out->get_limit_min());
	weight = min(weight, out->get_limit_max());
	weight /= 1000000.0;

	return weight;
}



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

  FuzzyVarIn *in1 = new FuzzyVarIn("Drops", 0, 100000);
  FuzzyVarIn *in2 = new FuzzyVarIn("Delay", 0, 5000000); // 1s em us
  FuzzyVarOut *out = new FuzzyVarOut("Interval", 10, 1000000); // 1s em us

  in1->add_set("Pequena perda", -1000, 0, 1000);
  in1->add_set("Média perda", 1000, 2000, 3000);
  in1->add_set("Alta perda", 3000, 100000, 150000);

  in2->add_set("Pequeno delay", -1000, 35, 1000); // max = 1ms
  in2->add_set("Médio delay", 1000, 50000, 100000);
  in2->add_set("Médio alto delay", 80000, 200000, 350000);
  in2->add_set("Alto delay", 300000, 5000000, 6000000); // max = 1.5s

  out->add_set("Taxa super alta", -100, 50, 500);
  out->add_set("Taxa alta", 250, 5000, 10000);
  out->add_set("Taxa média", 5000, 100000, 250000);
  out->add_set("Taxa baixa", 200000, 500000, 700000);
  out->add_set("Taxa super baixa", 600000, 800000, 1100000);

  fuzzy.set_vars(in1, in2, out);

  fuzzy.add_rule("Pequena perda", "Pequeno delay",    "Taxa super alta");
  fuzzy.add_rule("Pequena perda", "Médio delay",      "Taxa alta");
  fuzzy.add_rule("Pequena perda", "Médio alto delay", "Taxa média");
  fuzzy.add_rule("Pequena perda", "Alto delay",       "Taxa baixa");
  fuzzy.add_rule("Média perda",   "Pequeno delay",    "Taxa alta");
  fuzzy.add_rule("Média perda",   "Médio delay",      "Taxa média");
  fuzzy.add_rule("Média perda",   "Médio alto delay", "Taxa baixa");
  fuzzy.add_rule("Média perda",   "Alto delay",       "Taxa super baixa");
  fuzzy.add_rule("Alta perda",    "Pequeno delay",    "Taxa média");
  fuzzy.add_rule("Alta perda",    "Médio delay",      "Taxa baixa");
  fuzzy.add_rule("Alta perda",    "Médio alto delay", "Taxa super baixa");
  fuzzy.add_rule("Alta perda",    "Alto delay",       "Taxa super baixa");
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

		if (stopped || (m_count && m_sent >= m_count))
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
#if 1
	double interval = fuzzy.eval(drops, delay);
	std::cout << "Fuzzy: drops:" << drops << " "
		<< " delay:" << delay
		<< " old interval:" << m_interval.GetSeconds()
		<< " new interval:" << interval << "\n";
	SetAttribute("Interval", TimeValue(Seconds(interval)));
#else
	if (!drops) {
		SetAttribute("Interval", TimeValue(Seconds(m_interval.GetSeconds()*0.75)));
		std::cout << "Speeding up! " << m_interval << "\n";
	} else {
		SetAttribute("Interval", TimeValue(Seconds(m_interval.GetSeconds()*2)));
		std::cout << "Slowing down! " << m_interval << "\n";
	}
#endif
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

  if (!m_count || m_sent < m_count)
    {
      m_sendEvent = Simulator::Schedule (m_interval, &UdpClient::Send, this);
    }
}

} // Namespace ns3
