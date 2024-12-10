# How to use ?

Enable the RNG module.\
Set the pointer to your netwotk interface (dhcp_netif) in *dhcp_common.c*.\
Add the following macro definitions to lwiopts.h:
- #define LWIP_UDP  1
- #define LWIP_IP_ACCEPT_UDP_PORT(x)  (1)

The last define is needed to allow communication through the UDP reserved ports for DHCP.The LWIP_DHCP macro should be defined to 1 only if you are planning to use the lwip DHCP client. Otherwise, make sure it is not defined or is defined as 0.

To launch the server, call dhcpServerStart. Your project need the files *dhcp_server.h/c, dhcp_common.h/c*.

To launch the client, call dhcpClientStart and pass 1 as the value for the first parameter.This means that the client is autonomous. Your project need the files *dhcp_client.h/c, dhcp_common.h/c*.

When the host can be either a client or a server, call dhcpRoleResolverStart. The host will first try to reach a server, and after a certain number of attempts, it will set itself as a server.

## Available settings
- DHCP_TRIES_RND_MIN, DHCP_TRIES_RND_MAX: When managed, the client will try to configure it self N times (DHCP_TRIES_RND_MIN < N < DHCP_TRIES_RND_MAX). These macros are located in *dhcp_role_resolver.c*.
- DHCP_RESPONSE_TIMEOUT_MS: The time slice between client status checks. The macro is located in *dhcp_client.h*.
- DHCP_TRY_CNT: The number of times the client try to make requests when in REQUESTING state and SELECTING state. The later is valid when the client is autonomous. The macro is located in *dhcp_client.c*.
- DHCP_SERVER_MAX_CLIENTS: Number of client the server can handle. The macro is located in *dhcp_server.c*.
- DHCP_NETWORK_IP, DHCP_NETWORK_NETMASK, DHCP_NETWORK_GATEWAY, LEASE_TIME: DHCP server network settings provided to clients. These macros are located in *dhcp_server.c*.