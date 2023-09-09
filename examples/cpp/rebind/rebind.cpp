// udp2tcp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

/// @class rebind_router
/// @brief This class manages the rebind router functionality.
///
/// The rebind_router class is responsible for configuring and managing the routing
/// of network traffic between the default network adapter and the rebind network adapter.
/// It extends the iphelper::network_config_info class to gather information about the
/// network interfaces and settings.
///
/// @note Inherits from iphelper::network_config_info<rebind_router>
///
/// @member filter_ A unique_ptr to a dual_packet_filter object that manages the packet filtering.
/// @member default_adapter_handle_ A handle to the default network adapter (INVALID_HANDLE_VALUE if not initialized).
/// @member rebind_adapter_handle_ A handle to the rebind network adapter (INVALID_HANDLE_VALUE if not initialized).
/// @member app_name_ A string containing the name of the application.
/// @member rebind_src_hw_address_ A mac_address object representing the source hardware address of the rebind network adapter.
/// @member default_src_hw_address_ A mac_address object representing the source hardware address of the default network adapter.
/// @member rebind_gw_hw_address_ A mac_address object representing the hardware address of the gateway for the rebind network adapter.
/// @member rebind_src_ip_address_ An ip_address_v4 object representing the source IP address of the rebind network adapter.
/// @member default_src_ip_address_ An ip_address_v4 object representing the source IP address of the default network adapter.
/// @member file_stream_ A pcap_file_storage object for storing captured packets to a file named "capture.pcap".
class rebind_router: public iphelper::network_config_info<rebind_router>
{
	std::unique_ptr<ndisapi::dual_packet_filter> filter_;
	HANDLE default_adapter_handle_ = INVALID_HANDLE_VALUE;
	HANDLE rebind_adapter_handle_ = INVALID_HANDLE_VALUE;
	std::wstring app_name_;
	net::mac_address rebind_src_hw_address_{};
	net::mac_address default_src_hw_address_{};
	net::mac_address rebind_gw_hw_address_{};
	net::ip_address_v4 rebind_src_ip_address_{};
	net::ip_address_v4 default_src_ip_address_{};
	pcap::pcap_file_storage file_stream_{ "capture.pcap" };

	/// @brief Searches for an NDIS interface that matches the given network adapter information.
	///
	/// This method tries to find an NDIS interface in the list of available interfaces that
	/// corresponds to the provided network adapter information. If a matching interface is found,
	/// the method returns the index of the interface in the list.
	///
	/// @param info An iphelper::network_adapter_info object representing the network adapter to search for.
	///
	/// @return An std::optional<size_t> containing the index of the NDIS interface in the list if found,
	///         or an empty optional if not found.
	///
	/// @note This method is const and does not modify the rebind_router object.
	std::optional<size_t> get_ndis_interface_by_adapter_info(const iphelper::network_adapter_info& info) const
	{
		auto& ndis_adapters = filter_->get_interface_list();

		if (info.get_if_type() != IF_TYPE_PPP)
		{
			if (const auto it = std::find_if(ndis_adapters.begin(), ndis_adapters.end(),
				[&info](const auto& ndis_adapter)
				{
					return (std::string::npos != ndis_adapter->get_internal_name().
						find(info.get_adapter_name()));
				}); it != ndis_adapters.cend())
			{
				return { it - ndis_adapters.begin() };
			}
		}
		else
		{
			if (const auto it = std::find_if(ndis_adapters.begin(), ndis_adapters.end(),
				[&info](const auto& ndis_adapter)
				{
					if (auto wan_info = ndis_adapter->get_ras_links(); wan_info)
					{
						if (auto ras_it = std::find_if(
							wan_info->cbegin(), wan_info->cend(),
							[&info](auto& ras_link)
							{
								return info.has_address(
									ras_link.ip_address);
							}); ras_it != wan_info->cend())
						{
							return true;
						}
					}

					return false;
				}); it != ndis_adapters.cend())
			{
				return { it - ndis_adapters.begin() };
			}
		}

		return {};
	}

	/// @brief Resolves the process associated with a TCP connection for IPv4 addresses.
	///
	/// This method attempts to find the process responsible for a TCP connection, given
	/// the IP header and TCP header information. If the process is not found initially,
	/// the method tries to update the process lookup table and search again.
	///
	/// @param ip_header A pointer to the IP header structure of the packet.
	/// @param tcp_header A pointer to the TCP header structure of the packet.
	///
	/// @return A std::shared_ptr<iphelper::network_process> object representing the process
	///         associated with the TCP connection. If the process is not found, the shared_ptr
	///         will be empty (i.e., nullptr).
	static std::shared_ptr<iphelper::network_process> resolve_process_for_tcp (const iphdr* ip_header, const tcphdr* tcp_header)
	{
		auto process = iphelper::process_lookup<net::ip_address_v4>::get_process_helper().
			lookup_process_for_tcp<false>(net::ip_session<net::ip_address_v4>{
			ip_header->ip_src, ip_header->ip_dst, ntohs(tcp_header->th_sport),
				ntohs(tcp_header->th_dport)
		});

		if (!process)
		{
			iphelper::process_lookup<net::ip_address_v4>::get_process_helper().actualize(
				true, false);
			process = iphelper::process_lookup<net::ip_address_v4>::get_process_helper().
				lookup_process_for_tcp<true>(net::ip_session<net::ip_address_v4>{
				ip_header->ip_src, ip_header->ip_dst, ntohs(tcp_header->th_sport),
					ntohs(tcp_header->th_dport)
			});
		}

		return process;
	}

	/// @brief Resolves the process associated with a UDP connection for IPv4 addresses.
	///
	/// This method attempts to find the process responsible for a UDP connection, given
	/// the IP header and UDP header information. If the process is not found initially,
	/// the method tries to update the process lookup table and search again.
	///
	/// @param ip_header A pointer to the IP header structure of the packet.
	/// @param udp_header A pointer to the UDP header structure of the packet.
	///
	/// @return A std::shared_ptr<iphelper::network_process> object representing the process
	///         associated with the UDP connection. If the process is not found, the shared_ptr
	///         will be empty (i.e., nullptr).
	static std::shared_ptr<iphelper::network_process> resolve_process_for_udp(const iphdr* ip_header, const udphdr* udp_header)
	{
		auto process = iphelper::process_lookup<net::ip_address_v4>::get_process_helper().
			lookup_process_for_udp<false>(
				net::ip_endpoint<net::ip_address_v4>{
			ip_header->ip_src, ntohs(udp_header->th_sport)
		});

		if (!process)
		{
			iphelper::process_lookup<net::ip_address_v4>::get_process_helper().actualize(
				false, true);
			process = iphelper::process_lookup<net::ip_address_v4>::get_process_helper().
				lookup_process_for_udp<true>(
					net::ip_endpoint<net::ip_address_v4>{
				ip_header->ip_src, ntohs(udp_header->th_sport)
			});
		}

		return process;
	}

public:
	/// @brief Constructs an instance of the `rebind_router` class.
	///
	/// The constructor creates a `rebind_router` object, which sets up a packet filter
	/// to intercept incoming and outgoing packets. The filter is configured to check if
	/// the packet belongs to the specified application, and if so, modifies the packet's
	/// source IP and MAC addresses before routing it. The filtered packets are also saved
	/// to a file for further analysis.
	///
	/// This constructor initializes a `dual_packet_filter` object with custom lambdas
	/// for handling inbound and outbound packets. The lambdas implement the required
	/// logic for intercepting, modifying, and routing the packets as necessary.
	rebind_router()
	{
		filter_ = std::make_unique<ndisapi::dual_packet_filter>(
			nullptr,
			[this](HANDLE, INTERMEDIATE_BUFFER& buffer)
		{
			if (auto* const ethernet_header = reinterpret_cast<ether_header_ptr>(buffer.m_IBuffer); ntohs(
				ethernet_header->h_proto) == ETH_P_IP)
			{
				auto* const ip_header = reinterpret_cast<iphdr_ptr>(ethernet_header + 1);
				if(net::ip_address_v4(ip_header->ip_src) != default_src_ip_address_)
					return ndisapi::dual_packet_filter::packet_action::pass;

				if (ip_header->ip_p == IPPROTO_UDP)
				{
					const auto* const udp_header = reinterpret_cast<udphdr_ptr>(reinterpret_cast<PUCHAR>(ip_header) +
						sizeof(DWORD) * ip_header->ip_hl);

					if (const auto process = resolve_process_for_udp(ip_header, udp_header); 
						process->name.find(app_name_) != std::wstring::npos)
					{
						// Change source IP and MAC addresses
						ip_header->ip_src = rebind_src_ip_address_;
						memcpy(ethernet_header->h_source, rebind_src_hw_address_.data.data(), rebind_src_hw_address_.data.size());

						// Set MAC address of the default GW (LAN connections may not work properly)
						memcpy(ethernet_header->h_dest, rebind_gw_hw_address_.data.data(), rebind_gw_hw_address_.data.size());

						CNdisApi::RecalculateUDPChecksum(&buffer);
						CNdisApi::RecalculateIPChecksum(&buffer);

						file_stream_ << buffer;

						return ndisapi::dual_packet_filter::packet_action::route;
					}
				}
				else if (ip_header->ip_p == IPPROTO_TCP)
				{
					auto* const tcp_header = reinterpret_cast<const tcphdr*>(reinterpret_cast<const uint8_t*>(
						ip_header) +
						sizeof(DWORD) * ip_header->ip_hl);

					if (const auto process = resolve_process_for_tcp(ip_header, tcp_header);
						process->name.find(app_name_) != std::wstring::npos)
					{
						// Change source IP and MAC addresses
						ip_header->ip_src = rebind_src_ip_address_;
						memcpy(ethernet_header->h_source, rebind_src_hw_address_.data.data(), rebind_src_hw_address_.data.size());

						// Set MAC address of the default GW (LAN connections may not work properly)
						memcpy(ethernet_header->h_dest, rebind_gw_hw_address_.data.data(), rebind_gw_hw_address_.data.size());

						CNdisApi::RecalculateTCPChecksum(&buffer);
						CNdisApi::RecalculateIPChecksum(&buffer);

						file_stream_ << buffer;

						return ndisapi::dual_packet_filter::packet_action::route;
					}
				}
			}

			return ndisapi::dual_packet_filter::packet_action::pass;
		},
			[this](HANDLE, INTERMEDIATE_BUFFER& buffer)
		{
			if (auto* const ethernet_header = reinterpret_cast<ether_header_ptr>(buffer.m_IBuffer); ntohs(
				ethernet_header->h_proto) == ETH_P_IP)
			{
				auto* const ip_header = reinterpret_cast<iphdr_ptr>(ethernet_header + 1);
				if (net::ip_address_v4(ip_header->ip_dst) != rebind_src_ip_address_)
					return ndisapi::dual_packet_filter::packet_action::pass;

				// Change source IP and MAC addresses
				if (ip_header->ip_p == IPPROTO_UDP || ip_header->ip_p == IPPROTO_TCP)
				{
					ip_header->ip_dst = default_src_ip_address_;
					memcpy(ethernet_header->h_dest, default_src_hw_address_.data.data(), default_src_hw_address_.data.size());

					if (ip_header->ip_p == IPPROTO_UDP)
					{
						CNdisApi::RecalculateUDPChecksum(&buffer);
					}
					else if (ip_header->ip_p == IPPROTO_TCP)
					{
						CNdisApi::RecalculateTCPChecksum(&buffer);
					}

					CNdisApi::RecalculateIPChecksum(&buffer);

					file_stream_ << buffer;

					return ndisapi::dual_packet_filter::packet_action::route;
				}
			}

			return ndisapi::dual_packet_filter::packet_action::pass;
		},
			nullptr);
	}

	/// @brief Destructor for the `rebind_router` class.
	///
	/// Stops the router before the object is destroyed.
	~rebind_router()
	{
		stop();
	}

	/// @brief Deleted copy constructor for the `rebind_router` class.
	///
	/// This constructor is explicitly deleted to prevent copying of `rebind_router` objects.
	rebind_router(const rebind_router& other) = delete;

	/// @brief Deleted move constructor for the `rebind_router` class.
	///
	/// This constructor is explicitly deleted to prevent moving of `rebind_router` objects.
	rebind_router(rebind_router&& other) = delete;

	/// @brief Deleted copy assignment operator for the `rebind_router` class.
	///
	/// This operator is explicitly deleted to prevent copying of `rebind_router` objects.
	rebind_router& operator=(const rebind_router& other) = delete;

	/// @brief Deleted move assignment operator for the `rebind_router` class.
	///
	/// This operator is explicitly deleted to prevent moving of `rebind_router` objects.
	rebind_router& operator=(rebind_router&& other) = delete;

	/// @brief Checks if the driver is loaded.
	///
	/// @return `true` if the driver is loaded, `false` otherwise.
	///
	/// This method is used to determine if the packet filtering driver is properly loaded.
	[[nodiscard]] bool is_driver_loaded() const
	{
		return filter_->IsDriverLoaded();
	}

	/// @brief Sets the NDIS interfaces for the default and rebind network adapters based on adapter information.
	///
	/// @param default_adapter A reference to the `network_adapter_info` object for the default network adapter.
	/// @param rebind_adapter A reference to the `network_adapter_info` object for the rebind network adapter.
	/// @return `true` if the NDIS interfaces are successfully set, `false` otherwise.
	///
	/// This method sets the NDIS interfaces by finding the NDIS adapter corresponding to the provided
	/// `network_adapter_info` objects. It configures the default and rebind adapters, source hardware addresses,
	/// rebind gateway hardware address, rebind source IP address, and default source IP address.
	[[nodiscard]] bool set_ndis_interfaces_by_adapter_info(
		const iphelper::network_adapter_info& default_adapter, 
		const iphelper::network_adapter_info& rebind_adapter)
	{
		const auto ndis_default = get_ndis_interface_by_adapter_info(default_adapter);
		const auto ndis_rebind = get_ndis_interface_by_adapter_info(rebind_adapter);

		if (!ndis_default)
		{
			std::cout << "Failed to identify NDIS adapter for the default network interface.\n";
			return false;
		}

		if(!ndis_rebind)
		{
			std::cout << "Failed to identify NDIS adapter for the rebind network interface.\n";
			return false;
		}

		if(filter_->get_interface_list()[ndis_rebind.value()]->get_ndis_wan_type() != ndisapi::ndis_wan_type::ndis_wan_none)
		{
			std::cout << "For simplicity rebind to NDISWAN interfaces is not supported by this demo!\n";
			return false;
		}

		default_adapter_handle_ = filter_->get_interface_list()[ndis_default.value()]->get_adapter();
		rebind_adapter_handle_ = filter_->get_interface_list()[ndis_rebind.value()]->get_adapter();

		default_src_hw_address_ = filter_->get_interface_list()[ndis_default.value()]->get_hw_address();
		rebind_src_hw_address_ = filter_->get_interface_list()[ndis_rebind.value()]->get_hw_address();

		for (auto& gw :rebind_adapter.get_gateway_address_list())
		{
			if(gw.ss_family == AF_INET)
			{
				rebind_gw_hw_address_ = gw.hardware_address;
			}
		}

		for (auto& ip: rebind_adapter.get_unicast_address_list())
		{
			if(ip.ss_family == AF_INET)
			{
				rebind_src_ip_address_ = net::ip_address_v4(sockaddr_in(ip).sin_addr);
			}
		}

		for (auto& ip : default_adapter.get_unicast_address_list())
		{
			if (ip.ss_family == AF_INET)
			{
				default_src_ip_address_ = net::ip_address_v4(sockaddr_in(ip).sin_addr);
			}
		}

		return true;
	}

	/// @brief Sets the application name to match for IP rebind.
	///
	/// @param name A reference to the `std::wstring` containing the application name.
	///
	/// This method sets the application name for the rebind_router class. Packets from processes
	/// with a name that matches the given application name will be subject to IP rebind.
	void set_application_name(const std::wstring& name)
	{
		app_name_ = name;
	}

	/// @brief Starts the IP rebind process on the network interfaces.
	///
	/// @return Returns true if the IP rebind process started successfully on both interfaces, false otherwise.
	///
	/// This method starts filtering packets on both the default and rebind network interfaces. If the filtering fails to start on either
	/// interface, the method will print an error message and return false.
	[[nodiscard]] bool start() const
	{
		if(!filter_->start_filter(default_adapter_handle_, 0))
		{
			std::cout << "Failed to start filtering on default network interface!\n";
			return false;
		}
		if(!filter_->start_filter(rebind_adapter_handle_, 1))
		{
			std::cout << "Failed to start filtering on rebind network interface!\n";
			return false;
		}

		return true;
	}

	/// @brief Stops the IP rebind process on the network interfaces.
	///
	/// This method stops filtering packets on both the default and rebind network interfaces by calling the stop_filter() function for both interfaces.
	void stop() const
	{
		filter_->stop_filter(0);
		filter_->stop_filter(1);
	}

	/// @brief Prints the IP rebind parameters.
	///
	/// This method outputs the rebind parameters, including the application name, source MAC addresses of the rebind and default adapters, rebind adapter gateway MAC address, and source IP addresses of the rebind and default adapters.
	void print_parameters() const
	{
		std::cout << "\nRebind parameters:\n\n";
		std::wcout << "Application name: " << app_name_ << std::endl;
		std::cout << "Rebind adapter source MAC: " << rebind_src_hw_address_ << std::endl;
		std::cout << "Default adapter source MAC: " << default_src_hw_address_ << std::endl;
		std::cout << "Rebind adapter gateway MAC: " << rebind_gw_hw_address_ << std::endl;
		std::cout << "Rebind adapter source IP address: " << rebind_src_ip_address_ << std::endl;
		std::cout << "Default adapter source IP address: " << default_src_ip_address_ << std::endl;
		std::cout << "\n\n";
	}

	/**
	@brief Convert network adapter info to a formatted wstring.
	@param info The network adapter info to convert.
	@return std::wstring The formatted wstring.
	*/
	static std::wstring to_wstring(const iphelper::network_adapter_info& info)
	{
		std::wstringstream stream;

		stream << "\t" << info.get_friendly_name() <<
			"\t:\t" << info.get_description() << std::endl;

		for (auto& ip : info.get_unicast_address_list())
		{
			stream << "\t\t" << std::wstring(ip) << std::endl;
		}
		stream << "\t" << "Gateway:\n";
		for (auto& gw : info.get_gateway_address_list())
		{
			stream << "\t\t" << std::wstring(gw) << " : " << gw.hardware_address << std::endl;
		}

		return stream.str();
	}
};

int main()
{
	rebind_router rebind;

	if (rebind.is_driver_loaded())
	{
		std::cout << "WinpkFilter is loaded" << std::endl << std::endl;
	}
	else
	{
		std::cout << "WinpkFilter is not loaded" << std::endl << std::endl;
		return 1;
	}

	auto routable_adapters = rebind_router::get_routable_interfaces(net::ip_address_v4("1.1.1.1"));
	const auto default_adapter = rebind_router::get_best_interface(net::ip_address_v4("1.1.1.1"));

	if(!default_adapter)
	{
		std::cout << "IP address 1.1.1.1 is not reachable. System does not have Internet connection.\n";
		return 0;
	}

	std::cout << "Default Internet connected network interface:\n\n";

	std::wcout << rebind_router::to_wstring(default_adapter.value());

	if(routable_adapters.size() == 1)
	{
		std::cout << "System has only one Internet connected interface. Rebind is useless.\n";
		return 0;
	}

	// Remove the default network adapter from the list of alternative network interfaces
	routable_adapters.erase(
		std::remove_if(
			routable_adapters.begin(),
			routable_adapters.end(),
			[default_if = default_adapter.value()](auto&& a)
			{
				return default_if == a;
			}), routable_adapters.end());

	std::cout << "\nAlternative Internet connected network interfaces:" << std::endl << std::endl;
	size_t idx = 0;

	for (size_t i = 0; i < routable_adapters.size(); ++i)
	{
		std::wcout << i + 1<< ". " << rebind_router::to_wstring(routable_adapters[i]);
	}

	std::wstring app_name_w;

	std::cout << std::endl << "Application name to rebind: ";
	std::getline(std::wcin, app_name_w);

	rebind.set_application_name(app_name_w);

	if (routable_adapters.size() > 1)
	{
		std::cout << std::endl << "Select network interface to rebind: ";
		std::cin >> idx;
	}
	else
	{
		idx = 1;
	}

	if (idx > routable_adapters.size())
	{
		std::cout << "Wrong parameter was selected. Out of range." << std::endl;
		return 0;
	}

	if(const auto result = rebind.set_ndis_interfaces_by_adapter_info(
		default_adapter.value(),
		routable_adapters[idx - 1]); !result)
	{
		return 0;
	}

	rebind.print_parameters();

	if(!rebind.start())
	{
		return 0;
	}

	std::cout << "Press any key to stop filtering" << std::endl;

	std::ignore = _getch();

	std::cout << "Exiting..." << std::endl;

	return 0;
}

