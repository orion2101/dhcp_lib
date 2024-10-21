# How to use ?

## Advice (When the host is a client or can be a client)
For random DHCP transaction IDs, include dhcp_common.c/h modules in your project and define the following in lwipopts.h:
    #define DHCP_CREATE_RAND_XID 0
    #define DHCP_GLOBAL_XID_HEADER "dhcp_common.h"
    #define DHCP_GLOBAL_XID ((uint32_t)generateUint32())

## host as client (custom client is not necessary, LWIP dhcp client is more complete)
- Define LWIP_DHCP as 1 in lwipopts.h
- For random DHCP transaction IDs look Advice. Turn on the RNG module

## host as server
- Include dhcp_common.c/h, dhcp_server.c/h files in your project
- Call dhcp_server_init() to start the server. The LWIP stack must be first initialized
- DHCP server settings (DHCP_SERVER_MAX_CLIENTS, DHCP_NETWORK_IP, DHCP_NETWORK_NETMASK, DHCP_NETWORK_GATEWAY, DHCP_LEASE_TIME, DHCP_REBIND_TIME, DHCP_RENEW_TIME) can be found and edit in dhcp_server.c

## host as client and maybe as server
- Turn on the RNG module
- Include dhcp_common.c/h, dhcp_server.c/h and dhcp_role_resolver.c/h files in your project
- Call dhcp_role_resolver() to start the host with autonomous DHCP role resolution. The LWIP stack must be first initialized