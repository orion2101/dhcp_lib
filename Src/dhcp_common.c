#include "dhcp_common.h"

uint8_t dhcp_role;

// Buffers
struct dhcp_msg dhcp_in, dhcp_out;
int dhcp_in_len, dhcp_out_len;

// socket objects
struct sockaddr_in remote_sa, local_sa;
socklen_t remote_sa_len;
int socket_fd;

network_settings_t network_settings;

client_options_t client_options; //Options from sent by server to the client
server_options_t server_options; //Options sent by client to server

const uint8_t *in_options_ptr = dhcp_in.options, *out_options_ptr = dhcp_out.options;

extern struct netif gnetif;

inline uint8_t initDHCPSocket(uint8_t type) {
	int res = 0;

	bzero(&local_sa, sizeof(local_sa));
	bzero(&remote_sa, sizeof(remote_sa));
	local_sa.sin_family = AF_INET;
	local_sa.sin_addr.s_addr = (type == DHCP_SERVER) ? gnetif.ip_addr.addr : INADDR_ANY;
	local_sa.sin_port = (type == DHCP_SERVER) ? htons((uint16_t)DHCP_SERVER_PORT)
												: htons((uint16_t)DHCP_CLIENT_PORT);

	if (type == DHCP_CLIENT) {
		remote_sa.sin_family = AF_INET;
		remote_sa.sin_addr.s_addr = IP4_ADDR_BROADCAST->addr;
		remote_sa.sin_port = htons((uint16_t)DHCP_SERVER_PORT);
	}

	socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_fd < 0) {
		return 0;
	}

	res = setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &(int){1}, sizeof(int));
//	res = setsockopt(socket_fd, SOL_SOCKET, SO_NO_CHECK, &(int){1}, sizeof(int));
	res = setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, "st", 6);


	if (bind(socket_fd, (struct sockaddr *) &local_sa, sizeof(local_sa)) < 0) {
		closesocket(socket_fd);
		return 0;
	}

	return 1;
}

inline uint8_t deinitDHCPSocket(void) {
	if (close(socket_fd) < 0)
		return 0;

	return 1;
}


inline void receive_dhcp(void) {
	remote_sa_len = sizeof(remote_sa);
	dhcp_in_len = recvfrom(socket_fd, &dhcp_in, sizeof(dhcp_in), MSG_DONTWAIT, (struct sockaddr *) &remote_sa, &remote_sa_len);
}

inline int send_dhcp(void) {
	return sendto(socket_fd, &dhcp_out, dhcp_out_len, 0, (struct sockaddr *) &remote_sa, sizeof(remote_sa));
}

inline void fillMessage(uint8_t field, void *field_value) {
	switch (field) {
		case DHCP_FLD_OP:
			dhcp_out.op = (dhcp_role == DHCP_CLIENT) ? DHCP_BOOTREQUEST : DHCP_BOOTREPLY;
		break;
		case DHCP_FLD_HTYPE:
			dhcp_out.htype = 0x01;
		break;
		case DHCP_FLD_HLEN:
			dhcp_out.hlen = (uint8_t)MAC_ADDR_LEN;
		break;
		case DHCP_FLD_HOPS:
		break;
		case DHCP_FLD_XID:
			dhcp_out.xid = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_SECS:
		break;
		case DHCP_FLD_FLAGS:
			dhcp_out.flags = *((uint16_t*)field_value);
		break;
		case DHCP_FLD_CIADDR:
			dhcp_out.ciaddr.addr = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_YIADDR:
			dhcp_out.yiaddr.addr = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_SIADDR:
			dhcp_out.siaddr.addr = *((uint32_t*)field_value);
		break;
		case DHCP_FLD_GIADDR:
		break;
		case DHCP_FLD_CHADDR:
			memcpy(dhcp_out.chaddr, (uint8_t*)field_value, MAC_ADDR_LEN);
		break;
		case DHCP_FLD_SNAME:
		break;
		case DHCP_FLD_FILE:
		break;
		case DHCP_FLD_COOKIE:
			dhcp_out.cookie = htonl((uint32_t)DHCP_MAGIC_COOKIE);
		break;
		case DHCP_FLD_OPTIONS:
		break;
		default: break;
	}
}

inline uint8_t fillOption(uint8_t offset, uint8_t opt_code, uint8_t *opt_val) {
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
