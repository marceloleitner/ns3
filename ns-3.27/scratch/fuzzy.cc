/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/stats-module.h"

// Default Network Topology
//
// Number of wifi nodes can be increased up to 250
//
//   Wifi 10.1.3.0
//                 AP
//  *    *    *    *
//  |    |    |    |
// n5   n6   n7   n0
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("fuzzy");

int 
main (int argc, char *argv[])
{
  bool verbose = false;
  uint32_t nWifi = 5;
  bool tracing = false;
  uint32_t pktsize = 60;
  Time interval = Seconds(0.01);
  Time length = Seconds(100);
  int pktcount = 0;
  bool fuzzy = false;

  CommandLine cmd;
  cmd.AddValue ("nWifi", "Number of wifi STA devices", nWifi);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);
  cmd.AddValue ("tracing", "Enable pcap tracing", tracing);
  cmd.AddValue ("pktsize", "Packet size", pktsize);
  cmd.AddValue ("pktcount", "Packet count that a client will send", pktcount);
  cmd.AddValue ("interval", "Interval between packets", interval);
  cmd.AddValue ("length", "Simulation length", length);
  cmd.AddValue ("fuzzy", "Enable Fuzzy control", fuzzy);

  cmd.Parse (argc,argv);
  std::ostringstream _oprefix;
  _oprefix << "fuzzy"
		<< "-" << RngSeedManager::GetSeed()
		<< "-" << RngSeedManager::GetRun()
		<< "-" << pktsize
		<< "-" << interval
		<< "-" << nWifi
		<< "-" << fuzzy
		;
  std::string oprefix = _oprefix.str();


  // Check for valid number of csma or wifi nodes
  // 250 should be enough, otherwise IP addresses 
  // soon become an issue
  if (nWifi > 250)
    {
      std::cout << "Too many wifi nodes, no more than 250." << std::endl;
      return 1;
    }
  if (nWifi < 1)
    {
      std::cout << "Too few wifi nodes, must be at least 1." << std::endl;
      return 1;
    }

  if (verbose)
    {
      LogComponentEnable ("UdpServer", LOG_LEVEL_INFO);
    }

  NodeContainer wifiApNode;
  wifiApNode.Create (1);
  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (nWifi);

  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy = YansWifiPhyHelper::Default ();
  phy.SetChannel (channel.Create ());

  WifiHelper wifi;
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

  WifiMacHelper mac;
  Ssid ssid = Ssid ("ns-3-ssid");
  mac.SetType ("ns3::StaWifiMac",
               "Ssid", SsidValue (ssid),
               "ActiveProbing", BooleanValue (false));

  NetDeviceContainer staDevices;
  staDevices = wifi.Install (phy, mac, wifiStaNodes);

  mac.SetType ("ns3::ApWifiMac",
               "Ssid", SsidValue (ssid));

  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, mac, wifiApNode);

  MobilityHelper mobility;

  mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
                                 "MinX", DoubleValue (0.0),
                                 "MinY", DoubleValue (0.0),
                                 "DeltaX", DoubleValue (5.0),
                                 "DeltaY", DoubleValue (10.0),
                                 "GridWidth", UintegerValue (3),
                                 "LayoutType", StringValue ("RowFirst"));

  //mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
  //                           "Bounds", RectangleValue (Rectangle (-50, 50, -50, 50)));
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiStaNodes);

  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);

  InternetStackHelper stack;
  stack.Install (wifiApNode);
  stack.Install (wifiStaNodes);

  Ipv4AddressHelper address;

  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterfaces;
  apInterfaces = address.Assign (apDevices);
  address.Assign (staDevices);

  uint16_t sinkPort = 5002;
  ApplicationContainer sources;
  ApplicationContainer sinkApps;
  for (uint32_t i = 0; i < nWifi; i++) {
    ApplicationContainer tmp;

    Address sinkAddress (InetSocketAddress (apInterfaces.GetAddress (0), sinkPort+i));

    UdpClientHelper udpClientHelper (sinkAddress);
    udpClientHelper.SetAttribute("Interval", TimeValue(interval));
    udpClientHelper.SetAttribute("MaxPackets", UintegerValue(pktcount));
    udpClientHelper.SetAttribute("PacketSize", UintegerValue(pktsize));
    udpClientHelper.SetAttribute("Fuzzy", BooleanValue(fuzzy));
    tmp = udpClientHelper.Install (wifiStaNodes.Get (i));
    sources.Add (tmp);

/*
    Ptr<Socket> ns3TcpSocket = Socket::CreateSocket (wifiStaNodes.Get (i), UdpSocketFactory::GetTypeId ());
    Ptr<MyApp> app = CreateObject<MyApp> ();
    app->Setup (ns3TcpSocket, sinkAddress, pktsize, 0xffffffff, DataRate ("54Mbps"));
    wifiStaNodes.Get (i)->AddApplication (app);
    sources.Add(app);
*/
    UdpServerHelper udpServerHelper (sinkPort+i);
    tmp = udpServerHelper.Install (wifiApNode.Get (0));
    sinkApps.Add (tmp);
  }
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (length);
  sources.Get(0)->SetStartTime (Seconds (1.));
  sources.Get(0)->SetStopTime (Seconds (90.));
  if (nWifi > 1) {
    sources.Get(1)->SetStartTime (Seconds (5.));
    sources.Get(1)->SetStopTime (Seconds (80.));
  }
  if (nWifi > 2) {
    sources.Get(2)->SetStartTime (Seconds (10.));
    sources.Get(2)->SetStopTime (Seconds (70.));
  }
  if (nWifi > 3) {
    sources.Get(3)->SetStartTime (Seconds (15.));
    sources.Get(3)->SetStopTime (Seconds (60.));
  }
  if (nWifi > 4) {
    sources.Get(4)->SetStartTime (Seconds (20.));
    sources.Get(4)->SetStopTime (Seconds (50.));
  }

  /* If using more sources than 5 */
  for (uint32_t i = 5; i < nWifi; i++) {
    uint32_t start = 10. + i;
    if (start > 50)
	    start = 50;
    sources.Get(i)->SetStartTime (Seconds (start));
    sources.Get(i)->SetStopTime (Seconds (60.));
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  Simulator::Stop (length);

  std::ostringstream path;
#if 1
  /* Information probing */
  GnuplotHelper plotHelper;
  std::string probeType;
  std::string tracePath;
  std::string nodeName;

  /* Phy drops tracking over time */
  path.str("");
  path << oprefix << "-phydrops";
  plotHelper.ConfigurePlot(path.str(), "PhyTx drops vs time", "Time (s)", "PhyDrops (bytes)");
  probeType = "ns3::PacketProbe";

  for (uint32_t i = 0; i < nWifi; i++) {
    path.str("");
    path << "/NodeList/" <<
	  wifiStaNodes.Get(i)->GetId() <<
	  "/DeviceList/0/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxDrop";
	  //"/DeviceList/0/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxBegin";
	  //"/DeviceList/0/Mac/MacTxBackoff";
	  //"/DeviceList/0/Phy/PhyTxBegin";
    tracePath = path.str();
    path.str("");
    path << "n" << wifiStaNodes.Get(i)->GetId();
    nodeName = path.str();
    plotHelper.PlotProbe(probeType, tracePath, "OutputBytes", nodeName, GnuplotAggregator::KEY_BELOW);
  }

  /* Contenções over time */
  path.str("");
  path << oprefix << "-backoff";
  plotHelper.ConfigurePlot(path.str(), "Collisions vs time", "Time (s)", "Collision count");
  //probeType = "ns3::PacketProbe";
  probeType = "ns3::Uinteger32Probe";

  for (uint32_t i = 0; i < nWifi; i++) {
    path.str("");
    path << "/NodeList/" <<
	  wifiStaNodes.Get(i)->GetId() <<
	  "/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::RegularWifiMac/DcaTxop/Backoff";
	  //"/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::WifiMac/MacTxDrop";
	  //"/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::StaWifiMac/MacTxBackoff";
	  //"/DeviceList/0/Phy/PhyTxBegin";
    tracePath = path.str();
    path.str("");
    path << "n" << wifiStaNodes.Get(i)->GetId();
    nodeName = path.str();
    plotHelper.PlotProbe(probeType, tracePath, "Output", nodeName, GnuplotAggregator::KEY_BELOW);
  }

  /* Drops over time */
  path.str("");
  path << oprefix << "-udpdrops";
  plotHelper.ConfigurePlot(path.str(), "UDP drops vs time", "Time (s)", "UDP drops count");
  probeType = "ns3::Uinteger32Probe";

  for (uint32_t i = 0; i < nWifi; i++) {
    path.str("");
    path << "/NodeList/" <<
	  wifiStaNodes.Get(i)->GetId() <<
	  "/ApplicationList/0/$ns3::UdpClient/Drops";
    tracePath = path.str();
    path.str("");
    path << "n" << wifiStaNodes.Get(i)->GetId();
    nodeName = path.str();
    plotHelper.PlotProbe(probeType, tracePath, "Output", nodeName, GnuplotAggregator::KEY_BELOW);
  }

  /* Delay over time */
  path.str("");
  path << oprefix << "-udpdelay";
  plotHelper.ConfigurePlot(path.str(), "UDP delay vs time", "Time (s)", "UDP delay");
  probeType = "ns3::DoubleProbe";

  for (uint32_t i = 0; i < nWifi; i++) {
    path.str("");
    path << "/NodeList/" <<
	  wifiStaNodes.Get(i)->GetId() <<
	  "/ApplicationList/0/$ns3::UdpClient/Delay";
    tracePath = path.str();
    path.str("");
    path << "n" << wifiStaNodes.Get(i)->GetId();
    nodeName = path.str();
    plotHelper.PlotProbe(probeType, tracePath, "Output", nodeName, GnuplotAggregator::KEY_BELOW);
  }

  /* Amount of bytes sent over time */
  path.str("");
  path << oprefix << "-bytes";
  plotHelper.ConfigurePlot(path.str(), "txbytes vs time", "Time (s)", "tx (bytes)");
  probeType = "ns3::PacketProbe";
  //probeType = "ns3::Uinteger32Probe";

  for (uint32_t i = 0; i < nWifi; i++) {
    path.str("");
    path << "/NodeList/" <<
	  wifiStaNodes.Get(i)->GetId() <<
	  "/DeviceList/0/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyTxBegin";
	  //"/ApplicationList/0/TxBytes";
    tracePath = path.str();
    path.str("");
    path << "n" << wifiStaNodes.Get(i)->GetId();
    nodeName = path.str();
    plotHelper.PlotProbe(probeType, tracePath, "OutputBytes", nodeName, GnuplotAggregator::KEY_BELOW);
  }

  path.str("");
  path << oprefix << "-rxbytes";
  plotHelper.ConfigurePlot(path.str(), "rxbytes vs time", "Time (s)", "rx (bytes)");
  probeType = "ns3::PacketProbe";
  path.str("");
  path << "/NodeList/" <<
        wifiApNode.Get(0)->GetId() <<
//        "/DeviceList/0/$ns3::WifiNetDevice/Phy/$ns3::WifiPhy/PhyRxEnd";
        "/DeviceList/0/$ns3::WifiNetDevice/Mac/$ns3::WifiMac/MacRx";
        //"/ApplicationList/0/TxBytes";
  tracePath = path.str();
  path.str("");
  path << "n" << wifiApNode.Get(0)->GetId();
  nodeName = path.str();
  plotHelper.PlotProbe(probeType, tracePath, "OutputBytes", nodeName, GnuplotAggregator::KEY_BELOW);

#endif

  if (tracing == true)
    {
      path.str("");
      path << oprefix << "-1";
      phy.EnablePcap (path.str(), apDevices.Get(0));
#if 0
      path.str("");
      path << oprefix << "-2";
      phy.EnablePcap (path.str(), staDevices.Get(0));
#endif
    }

  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
