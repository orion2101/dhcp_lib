#include "rng.h"
#include "tcpip.h"
#include "dhcp_common.h"


extern RNG_HandleTypeDef hrng;
extern struct netif gnetif;

static uint8_t dhcp_role;
static struct dhcp_msg *dhcp_out;
static uint8_t dhcp_out_buff[DHCP_OUT_BUFF_LEN];

struct udp_pcb *dhcp_pcb;
struct pbuf *dhcp_pbuf;


uint32_t dhcpGenerateUint32(void) {
	uint32_t result = 0;
	HAL_RNG_GenerateRandomNumber(&hrng, &result);
	return result;
}

void dhcpClearOptions(void) {
	memset(dhcp_out->options, 0, DHCP_OPTIONS_LEN);
}

uint8_t dhcpInit(uint16_t port, udp_recv_fn dhcp_recv) {
	memset(dhcp_out_buff, 0, DHCP_OUT_BUFF_LEN);
	//Allocating transmit buffer
	if (dhcp_pbuf == NULL) {
		if ( (dhcp_pbuf = pbuf_alloc_reference((void *)dhcp_out_buff, DHCP_OUT_BUFF_LEN, PBUF_ROM)) == NULL )
			goto ret_error;
	}

	dhcp_role = (port == DHCP_SERVER_PORT) ? DHCP_SERVER : DHCP_CLIENT;

	LOCK_TCPIP_CORE();
	if (dhcp_pcb == NULL) {
		if ( (dhcp_pcb = udp_new()) == NULL ) {
			pbuf_free(dhcp_pbuf);
			dhcp_pbuf = NULL;
			goto ret_error;
		}
	}

	if (udp_bind(dhcp_pcb, IP_ADDR_ANY, port) != ERR_OK) {
		udp_remove(dhcp_pcb);
		pbuf_free(dhcp_pbuf);
		dhcp_pcb = NULL;
		dhcp_pbuf = NULL;
		goto ret_error;
	}

	udp_bind_netif(dhcp_pcb, &gnetif);
	ip_set_option(dhcp_pcb, SOF_BROADCAST);
	udp_recv(dhcp_pcb, dhcp_recv, NULL);

ret_success:
	UNLOCK_TCPIP_CORE();
	return 0;

ret_error:
	UNLOCK_TCPIP_CORE();
	return 1;
}

void dhcpDeinit(void) {
	LOCK_TCPIP_CORE();
	udp_remove(dhcp_pcb);
	pbuf_free(dhcp_pbuf);
	dhcp_pcb = NULL;
	dhcp_pbuf = NULL;
	UNLOCK_TCPIP_CORE();
}

inline void dhcpFillMessage(uint8_t field, void *value) {
	dhcp_out = dhcp_pbuf->payload;

	switch (field) {
		case DHCP_FLD_OP:
			dhcp_out->op = (dhcp_role == DHCP_CLIENT) ? DHCP_BOOTREQUEST : DHCP_BOOTREPLY;
		break;
		case DHCP_FLD_HTYPE:
			dhcp_out->htype = 0x01;
		break;
		case DHCP_FLD_HLEN:
			dhcp_out->hlen = (uint8_t)MAC_ADDR_LEN;
		break;
		case DHCP_FLD_HOPS:
			dhcp_out->hops = *((uint8_t*)value);
		break;
		case DHCP_FLD_XID:
			dhcp_out->xid = *((uint32_t*)value);
		break;
		case DHCP_FLD_SECS:
			dhcp_out->secs = *((uint16_t*)value);
		break;
		case DHCP_FLD_FLAGS:
			dhcp_out->flags = *((uint16_t*)value);
		break;
		case DHCP_FLD_CIADDR:
			dhcp_out->ciaddr.addr = *((uint32_t*)value);
		break;
		case DHCP_FLD_YIADDR:
			dhcp_out->yiaddr.addr = *((uint32_t*)value);
		break;
		case DHCP_FLD_SIADDR:
			dhcp_out->siaddr.addr = *((uint32_t*)value);
		break;
		case DHCP_FLD_GIADDR:
			dhcp_out->giaddr.addr = *((uint32_t*)value);
		break;
		case DHCP_FLD_CHADDR:
			memcpy(dhcp_out->chaddr, (uint8_t*)value, MAC_ADDR_LEN);
		break;
		case DHCP_FLD_SNAME:
			//TODO
		break;
		case DHCP_FLD_FILE:
			//TODO
		break;
		case DHCP_FLD_COOKIE:
			dhcp_out->cookie = htonl((uint32_t)DHCP_MAGIC_COOKIE);
		break;
		case DHCP_FLD_OPTIONS:
			//fillOption function
		break;
		default: break;
	}
}

inline uint8_t dhcpFillOption(uint8_t offset, uint8_t opt_code, uint8_t *opt_val) {
	uint8_t *options = dhcp_out->options + offset;
	static uint8_t cnt = 0;

	switch (opt_code) {
		case DHCP_OPTION_MESSAGE_TYPE:
			*options = DHCP_OPTION_MESSAGE_TYPE;
			*(options + 1) = DHCP_OPTION_MESSAGE_TYPE_LEN;
			*(options + 2) = *opt_val;
			cnt = 3;
		break;
		case DHCP_OPTION_END:
			*options = DHCP_OPTION_END;
			cnt = 1;
		break;
		case DHCP_OPTION_SUBNET_MASK:
			*options = DHCP_OPTION_SUBNET_MASK;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		case DHCP_OPTION_SERVER_ID:
			*options = DHCP_OPTION_SERVER_ID;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		case DHCP_OPTION_BROADCAST:
			*options = DHCP_OPTION_BROADCAST;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		case DHCP_OPTION_T1:
			*options = DHCP_OPTION_T1;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		case DHCP_OPTION_T2:
			*options = DHCP_OPTION_T2;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		case DHCP_OPTION_LEASE_TIME:
			*options = DHCP_OPTION_LEASE_TIME;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		case DHCP_OPTION_REQUESTED_IP:
			*options = DHCP_OPTION_REQUESTED_IP;
			*(options + 1) = 4;
			memcpy(options+2, opt_val, 4);
			cnt = 6;
		break;
		default: break;
	}

	return cnt;
}
