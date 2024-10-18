#ifndef DHCP_COMMON_H
#define DHCP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/prot/dhcp.h"
//#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "lwip/api.h"

#define DHCP_SERVER_PORT			67
#define DHCP_CLIENT_PORT			68
#define BROADCAST_FLAG				0x8000
#define MAC_ADDR_LEN				6
#define CLIENT_CAN_BE_SERVER 		1


enum dhcp_role {
	DHCP_CLIENT = 0,
	DHCP_SERVER
};

enum dhcp_msg_fields {
	DHCP_FLD_OP = 0,
	DHCP_FLD_HTYPE,
	DHCP_FLD_HLEN,
	DHCP_FLD_HOPS,
	DHCP_FLD_XID,
	DHCP_FLD_SECS,
	DHCP_FLD_FLAGS,
	DHCP_FLD_CIADDR,
	DHCP_FLD_YIADDR,
	DHCP_FLD_SIADDR,
	DHCP_FLD_GIADDR,
	DHCP_FLD_CHADDR,
	DHCP_FLD_SNAME,
	DHCP_FLD_FILE,
	DHCP_FLD_COOKIE,
	DHCP_FLD_OPTIONS
};

typedef struct {
	uint8_t msg_type;
	uint32_t subnet_mask;
	uint32_t renewal_time;
	uint32_t rebinding_time;
	uint32_t lease_time;
	uint32_t server_ip;
} client_options_t;

typedef struct {
	uint32_t ip_addr;
	uint32_t gw_addr;
	uint32_t netmask;
	uint8_t mac_addr[MAC_ADDR_LEN];
} network_settings_t;


uint8_t initDHCPSocket(uint8_t type);
uint8_t deinitDHCPSocket(void);
void fillMessage(uint8_t field, void *field_value);
uint8_t fillOption(uint8_t offset, uint8_t opt_code, uint8_t *opt_val);
void receive_dhcp(void);
int send_dhcp(void);
uint8_t initDHCPpcb(uint16_t port, udp_recv_fn dhcp_recv);

#ifdef __cplusplus
}
#endif

#endif
