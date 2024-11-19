#ifndef DHCP_COMMON_H
#define DHCP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/prot/dhcp.h"
#include "lwip/udp.h"
#include "lwip/api.h"
#include "string.h"

#define DHCP_SERVER_PORT			67
#define DHCP_CLIENT_PORT			68
#define BROADCAST_FLAG				0x8000
#define MAC_ADDR_LEN				6

#define DHCP_OUT_BUFF_LEN	LWIP_MEM_ALIGN_SIZE(DHCP_OPTIONS_OFS + DHCP_OPTIONS_LEN)


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


void fillMessage(uint8_t field, void *field_value);
uint8_t fillOption(uint8_t offset, uint8_t opt_code, uint8_t *opt_val);
uint8_t initDHCP(uint16_t port, udp_recv_fn dhcp_recv);
void deinitDHCP(void);
uint32_t generateUint32(void);

#ifdef __cplusplus
}
#endif

#endif
