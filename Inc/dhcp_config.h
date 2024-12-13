#ifndef DHCP_CONFIG_H
#define DHCP_CONFIG_H

/*
* dhcp_role_resolver
*/
#define DHCP_ROLE_RESOLVER_STACK_SZ		256
#define DHCP_ROLE_RESOLVER_DELAY_MS		2000 // Should be the same value as DHCP_RESPONSE_TIMEOUT_MS. Used to suspend dhcp_role_resolver task while the dhcp client tries to get a network configuration.
#define DHCP_TRIES_RND_MIN		        1
#define DHCP_TRIES_RND_MAX		        15

/*
* dhcp_server
*/
#define DHCP_SERVER_MAX_CLIENTS		    5
#define DHCP_INPUT_QUEUE_LEN		    DHCP_SERVER_MAX_CLIENTS

//network alignment is big-endian
#define DHCP_SERVER_IP 				    LWIP_MAKEU32(192, 168, 0, 1)
#define DHCP_NET_NETMASK			    LWIP_MAKEU32(255, 255, 255, 0)
#define DHCP_NET_GATEWAY			    LWIP_MAKEU32(0, 0, 0, 0)
#define DHCP_NET_ADDR				    (DHCP_SERVER_IP & DHCP_NET_NETMASK)
#define DHCP_NET_ADDR_MIN			    (DHCP_NET_ADDR + 1)
#define DHCP_NET_ADDR_BRD			    (DHCP_NET_ADDR | ~DHCP_NET_NETMASK)

//DHCP_LEASE_TIME > DHCP_REBIND_TIME > DHCP_RENEW_TIME
#define LEASE_TIME					    300UL	//seconds
#define DHCP_LEASE_TIME				    PP_HTONL(LEASE_TIME)
#define DHCP_REBIND_TIME			    PP_HTONL((LEASE_TIME*3)/4)
#define DHCP_RENEW_TIME				    PP_HTONL(LEASE_TIME/2)

/*
* dhcp_client
*/
#define DHCP_TRY_CNT				    5 // The number of times the client try to make requests
#define DHCP_CLIENT_STACK_SZ		    1024
#define DHCP_RESPONSE_TIMEOUT_MS		2000 //The time slice between client status checks

#endif /* DHCP_CONFIG_H */