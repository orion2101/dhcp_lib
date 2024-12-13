#ifndef DHCP_COMMON_H
#define DHCP_COMMON_H


#include "lwip/prot/dhcp.h"
#include "lwip/udp.h"
#include "lwip/api.h"
#include "string.h"

#define DHCP_SERVER_PORT			67
#define DHCP_CLIENT_PORT			68
#define BROADCAST_FLAG				0x8000
#define MAC_ADDR_LEN				6

#define DHCP_OUT_BUFF_LEN	LWIP_MEM_ALIGN_SIZE(DHCP_OPTIONS_OFS + DHCP_OPTIONS_LEN)

extern struct netif *const dhcp_netif;

#ifdef __cplusplus
extern "C" {
#endif

enum dhcp_role {
	DHCP_CLIENT = 0,
	DHCP_SERVER
};

typedef struct {
	uint16_t length;
	uint32_t src;
	struct dhcp_msg input;
} DHCP_input_t;

typedef enum {
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
} DHCP_msg_fields_t;


uint8_t dhcpInit(uint16_t port, udp_recv_fn dhcp_recv);
void dhcpDeinit(void);
void dhcpFillMessage(DHCP_msg_fields_t field, void *field_value);
uint8_t dhcpFillOption(uint8_t offset, uint8_t opt_code, uint8_t *opt_val);
uint32_t dhcpGenerateUint32(void);
void dhcpClearOptions(void);

#ifdef __cplusplus
}
#endif

#endif
