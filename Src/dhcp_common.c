#include "dhcp_common.h"
#include "tcpip.h"

uint8_t dhcp_role;


network_settings_t network_settings;

client_options_t client_options; //Options from sent by server to the client


struct udp_pcb *dhcp_pcb;
struct pbuf *dhcp_pbuf;
struct dhcp_msg *dhcp_out;
uint8_t *out_options_ptr;

uint8_t initDHCPpcb(uint16_t port, udp_recv_fn dhcp_recv) {
	dhcp_role = (port == DHCP_SERVER_PORT) ? DHCP_SERVER : DHCP_CLIENT;

	LOCK_TCPIP_CORE();
	if (dhcp_pcb == NULL)
		dhcp_pcb = udp_new();

	if (dhcp_pcb == NULL)
		goto ret_error;

	if (udp_bind(dhcp_pcb, IP_ADDR_ANY, port) != ERR_OK)
		goto ret_error;

	udp_recv(dhcp_pcb, dhcp_recv, NULL);

	dhcp_pbuf = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcp_msg), PBUF_RAM);
	if (dhcp_pbuf == NULL)
		goto ret_error;

ret_success:
	UNLOCK_TCPIP_CORE();
	return 0;

ret_error:
	UNLOCK_TCPIP_CORE();
	return 1;
}


inline void fillMessage(uint8_t field, void *field_value) {
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
		break;
		case DHCP_FLD_XID:
			dhcp_out->xid = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_SECS:
		break;
		case DHCP_FLD_FLAGS:
			dhcp_out->flags = *((uint16_t*)field_value);
		break;
		case DHCP_FLD_CIADDR:
			dhcp_out->ciaddr.addr = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_YIADDR:
			dhcp_out->yiaddr.addr = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_SIADDR:
			dhcp_out->siaddr.addr = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_GIADDR:
		break;
		case DHCP_FLD_CHADDR:
			memcpy(dhcp_out->chaddr, (uint8_t*)field_value, MAC_ADDR_LEN);
		break;
		case DHCP_FLD_SNAME:
		break;
		case DHCP_FLD_FILE:
		break;
		case DHCP_FLD_COOKIE:
			dhcp_out->cookie = htonl((uint32_t)DHCP_MAGIC_COOKIE);
		break;
		case DHCP_FLD_OPTIONS:
		break;
		default: break;
	}
}

inline uint8_t fillOption(uint8_t offset, uint8_t opt_code, uint8_t *opt_val) {
	out_options_ptr = dhcp_out->options;
	uint8_t *options = out_options_ptr + offset;
	uint8_t cnt = 0;

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
