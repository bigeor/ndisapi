# NDISAPI

NDISAPI is a user-mode interface library for interacting with the [Windows Packet Filter](https://www.ntkernel.com/windows-packet-filter/) driver. It provides an easy-to-use, safe, and efficient interface to efficiently filter (inspect and modify) raw network packets at the NDIS level of the network stack with minimal impact on network activity.

Windows Packet Filter (WinpkFilter) is a high-performance, lightweight packet filtering framework for Windows, enabling developers to efficiently inspect, modify, and control raw network packets at the NDIS level. With user-friendly APIs and support for various Windows versions, WinpkFilter simplifies network packet manipulation without requiring kernel-mode programming expertise.

## Features

- Enumerate network adapters
- Query and set network adapter properties
- Capture and analyze packets
- Filter and modify packets
- Send raw packets

## Homepage

https://www.ntkernel.com/windows-packet-filter/

## Documentation

https://www.ntkernel.com/docs/windows-packet-filter-documentation/

## Library projects

* **ndisapi.dll** - native Win32 DLL wrapper (x86/x64/ARM64)
* **ndisapi.lib** - native Win32 static library wrapper (x86/x64/ARM64)
* **ndisapi.net** - .NET C++/CLI mixed class library (x86/x64/ARM64)
* **ndisapi.vs2012** - Visual Studio 2012 project for native Win32 DLL wrapper (x86/x64). Provides support for the Windows XP/2003.
* **ndisapi.vc6** - Visual C++ 6.0 project for native Win32 DLL wrapper. Provides support for the legacy Windows versions prior Windows XP/2003.

## Examples

### Build prerequisites 

* Install [vcpkg](https://vcpkg.io/en/getting-started.html)  
* Use vcpkg to Install Microsoft GSL library
```bash
vcpkg install ms-gsl:x86-windows ms-gsl:x64-windows ms-gsl:arm64-windows ms-gsl:x86-windows-static ms-gsl:x64-windows-static ms-gsl:arm64-windows-static
```
* Use vcpkg to Install PcapPlusPlus library
```bash
vcpkg install pcapplusplus:x86-windows pcapplusplus:x64-windows
```
* Use vcpkg to Install Hyperscan library  
```bash
vcpkg install hyperscan:x86-windows hyperscan:x64-windows
```
* Use vcpkg to Install llhttp library  
```bash
vcpkg install llhttp:x86-windows llhttp:x64-windows
```

### C++ examples 

* **capture** - native C++ sample that intercepts packets for the specified network interface and saves them into a PCAP file, which can be opened and analyzed with WireShark.
* **dns_proxy** - native C++ sample that redirects DNS protocol through a transparent UDP proxy.
* **dnstrace** - native C++ sample that intercepts DNS responses and decodes their content to the console. It can be configured to link NDISAPI statically or dynamically.
* **ethernet_bridge** - native C++ sample that implements bridging between wired and wireless networks. For more information, visit [Bridging Networks with Windows Packet Filter](https://www.ntkernel.com/bridging-networks-with-windows-packet-filter/)
* **ipv6_parser** - native C++ sample that intercepts IPv6 packets, matches them to the originating process (using IP Helper API), and parses protocol headers.
* **sni_inspector** - native C++ sample that intercepts network packets and extracts SNI from HTTPS packets and Host from HTTP packets.
* **socksify** - native C++ sample that redirects selected TCP connections through a SOCKS5 proxy. 
* **udp2tcp** - native C++ sample that acts as a UDP to TCP (and vice versa) converter.
* **rebind** - native C++ sample that rebinds outgoing TCP/UDP connections for the specified application from the default network interface to a different one.
* **pcapplusplus** - native C++ sample that utilizes the [PcapPlusPlus](https://pcapplusplus.github.io/) library to parse intercepted network packets and extract the Server Name Indication (SNI) from HTTPS packets. Additionally, it performs Transport Layer Security (TLS) fingerprinting to identify the specific version of TLS being used.
* **hyperscan** - native C++ sample that utilizes the [Hyperscan](https://github.com/intel/hyperscan) library to parse intercepted network packets to detect HTTP protocol sessions, and [llhttp](https://github.com/nodejs/llhttp) to parse the HTTP protocol of the detected sessions.

### C# examples

* **TestDotNet** - C# sample that demonstrates the NDISAPI usage in several filtering scenarios. It is available in x86, x64 and ARM64 configurations. The AnyCPU configuration is not available due to the C++/CLI nature of ndisapi.net wrapper. The project references [PacketDotNet](https://github.com/chmorgan/packetnet) for dumping network packet headers.
