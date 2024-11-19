#include "projdefs.h"
#include "ethernet.h"
#include "dhcp_common.h"
#include "dhcp_client.h"
#include "rng.h"


#define DHCP_SERVER_TIMEOUT		2000
#define DHCP_TRY_CNT			5


static struct {
	uint32_t ip_addr;
	uint32_t gw_addr;
	uint32_t netmask;
	uint8_t mac_addr[MAC_ADDR_LEN];
} network_settings;

static struct {
	uint8_t msg_type;
	uint32_t subnet_mask;
	uint32_t renewal_time;
	uint32_t rebinding_time;
	uint32_t lease_time;
	uint32_t server_ip;
} client_options;

extern RNG_HandleTypeDef hrng;
extern struct netif gnetif;

//Defined in dhcp_common.c
extern struct udp_pcb *dhcp_pcb;
extern struct pbuf *dhcp_pbuf;
//*****************************************//

static uint16_t dhcp_in_len, dhcp_out_len;
static struct dhcp_msg *dhcp_in;
static uint8_t standalone;
static uint8_t discover_try_cnt, try_cnt = DHCP_TRY_CNT;
static uint8_t client_state;
static uint32_t transaction_id;
static uint32_t elapsed = 0;
TaskHandle_t t_dhcp_client;

uint8_t dhcpClientGetState(void) {
	return client_state;
}

uint8_t dhcpClientGetDiscoveryTryCnt(void) {
	return discover_try_cnt;
}

inline static int sendMessage(uint32_t ip, uint8_t message_type) {
	uint8_t opt_ofst = 0;
	int sent_len = 0;
	dhcp_out_len = 0;

	fillMessage(DHCP_FLD_FLAGS, (ip == IP4_ADDR_BROADCAST->addr) ? &(uint16_t){htons(BROADCAST_FLAG)} : &(uint16_t){0});
	switch (message_type) {
		case DHCP_DISCOVER:
			fillMessage(DHCP_FLD_XID, &transaction_id);
			fillMessage(DHCP_FLD_CHADDR, &network_settings.mac_addr);
			opt_ofst += fillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);
		break;
		case DHCP_REQUEST:
			fillMessage(DHCP_FLD_XID, &transaction_id);
			fillMessage(DHCP_FLD_CHADDR, network_settings.mac_addr);
			opt_ofst += fillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);
			if (client_state == BOUND)
				fillMessage(DHCP_FLD_CIADDR, &network_settings.ip_addr);
			else {
			opt_ofst += fillOption(opt_ofst, DHCP_OPTION_REQUESTED_IP, &dhcp_in->yiaddr.addr);
			opt_ofst += fillOption(opt_ofst, DHCP_OPTION_SERVER_ID, &client_options.server_ip);
			}
		break;
		case DHCP_DECLINE:

		break;
		case DHCP_RELEASE:

		break;
		case DHCP_INFORM:

		break;
	}
	opt_ofst += fillOption(opt_ofst, DHCP_OPTION_END, NULL);
	dhcp_out_len = DHCP_OPTIONS_OFS + opt_ofst;

	udp_sendto(dhcp_pcb, dhcp_pbuf, &ip, DHCP_SERVER_PORT);
//	pbuf_remove_header(dhcp_pbuf, SIZEOF_ETH_HDR + IP_HLEN + UDP_HLEN);

	return sent_len;
}

inline static uint8_t forUs(void) {
	if (dhcp_in->xid != transaction_id || memcmp(dhcp_in->chaddr, network_settings.mac_addr, MAC_ADDR_LEN) != 0)
		return 0;

	return 1;
}

inline static void parseOptions(void) {
	uint8_t *in_options_ptr = dhcp_in->options;
	uint16_t length = dhcp_in_len - DHCP_OPTIONS_OFS;
	uint16_t ofst = 0;
	uint8_t opt_len = 0, *opt_data;
	uint8_t opt_code = *in_options_ptr;

	while (ofst < length && opt_code != DHCP_OPTION_END) {
		opt_len = *(in_options_ptr + ofst + 1);
		ofst += 2;
		opt_data = in_options_ptr+ofst;

		switch (opt_code) {
			case DHCP_OPTION_MESSAGE_TYPE:
				client_options.msg_type = *opt_data;
			break;
			case DHCP_OPTION_SUBNET_MASK:
				memcpy(&client_options.subnet_mask, opt_data, opt_len);
			break;
			case DHCP_OPTION_T1:
				memcpy(&client_options.renewal_time, opt_data, opt_len);
				client_options.renewal_time = ntohl(client_options.renewal_time) * 1000;
			break;
			case DHCP_OPTION_T2:
				memcpy(&client_options.rebinding_time, opt_data, opt_len);
				client_options.rebinding_time = ntohl(client_options.rebinding_time) * 1000;
			break;
			case DHCP_OPTION_LEASE_TIME:
				memcpy(&client_options.lease_time, opt_data, opt_len);
				client_options.lease_time = ntohl(client_options.lease_time) * 1000;
			break;
			case DHCP_OPTION_SERVER_ID:
				memcpy(&client_options.server_ip, opt_data, opt_len);
			break;
			default: break;
		}

		ofst += opt_len;
		opt_code = *(in_options_ptr + ofst);
	}
}

inline static void stateManager(void) {

	if (forUs())
		parseOptions();

	switch (client_state) {
		case INIT:

		break;
		case SELECTING:
			if (client_options.msg_type == DHCP_OFFER) {
				client_state = REQUESTING;
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
			}
		break;
		case REQUESTING:
			if (client_options.msg_type != DHCP_ACK) {
				client_state = SELECTING;
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				break;
			}

			network_settings.gw_addr = 0;
			network_settings.ip_addr = dhcp_in->yiaddr.addr;
			network_settings.netmask = client_options.subnet_mask;
			netif_set_addr(&gnetif, &network_settings.ip_addr, &network_settings.netmask, &network_settings.gw_addr);
			elapsed = 0;
			client_state = BOUND;
		break;
		case BOUND:

		break;
		case RENEWING:
			if (client_options.msg_type != DHCP_ACK) {
				memset(&client_options, 0, sizeof(client_options));
				discover_try_cnt = DHCP_TRY_CNT;
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				client_state = SELECTING;
			} else {
				elapsed = 0;
				client_state = BOUND;
			}
		break;
		case REBINDING:
			if (client_options.msg_type != DHCP_ACK) {
				memset(&client_options, 0, sizeof(client_options));
				discover_try_cnt = DHCP_TRY_CNT;
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				client_state = SELECTING;
			} else {
				elapsed = 0;
				client_state = BOUND;
			}
		break;
		case REBOOTING:

		break;
		case INIT_REBOOT:

		break;
	}
}

void task_dhcpClient(void *args) {
	for (;;) {
		elapsed += DHCP_SERVER_TIMEOUT;

		switch (client_state) {
			case SELECTING:
				if (discover_try_cnt > 0) {
					discover_try_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				} else {
					if (standalone) {
						transaction_id = generateUint32();
						discover_try_cnt = DHCP_TRY_CNT;
					}
				}
			break;
			case REQUESTING:
				if (try_cnt > 0) {
					try_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
				} else {
					try_cnt = DHCP_TRY_CNT;
					client_state = SELECTING;
				}
			break;
			case BOUND:
				if (elapsed >= client_options.renewal_time) {
					transaction_id += 1;
					sendMessage(client_options.server_ip, DHCP_REQUEST);
					client_state = RENEWING;
				}
			break;
			case RENEWING:
				if (elapsed >= client_options.rebinding_time) {
					transaction_id += 1;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
					client_state = REBINDING;
				} else {
					sendMessage(client_options.server_ip, DHCP_REQUEST);
				}
			break;
			case REBINDING:
				if (elapsed >= client_options.lease_time) {
					memset(&client_options, 0, sizeof(client_options));
					discover_try_cnt = DHCP_TRY_CNT;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
					client_state = SELECTING;
				} else {
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
				}
			break;
			default: break;
		}
		vTaskDelay(DHCP_SERVER_TIMEOUT);
	}
}

void dhcpClientReceive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
	vTaskSuspend(t_dhcp_client);

	dhcp_in = (struct dhcp_msg*)p->payload;
	dhcp_in_len = p->tot_len;
	stateManager();
	pbuf_free(p);

	vTaskResume(t_dhcp_client);
}

void dhcpClientStart(uint8_t is_standalone, uint8_t discover_cnt) {
	initDHCP(DHCP_CLIENT_PORT, dhcpClientReceive);
	standalone = is_standalone;
	discover_try_cnt = discover_cnt;

	//constant fields
	fillMessage(DHCP_FLD_OP, NULL);
	fillMessage(DHCP_FLD_HTYPE, NULL);
	fillMessage(DHCP_FLD_HLEN, NULL);
	fillMessage(DHCP_FLD_COOKIE, NULL);
	memcpy(network_settings.mac_addr, gnetif.hwaddr, MAC_ADDR_LEN);

	transaction_id = generateUint32();
	client_state = SELECTING;
	sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);

	if (xTaskCreate(task_dhcpClient, "task_dhcpClient", 1024, NULL, 0, &t_dhcp_client) != pdPASS)
		return;
}

void dhcpClientStop(void) {
	vTaskDelete(t_dhcp_client);
	deinitDHCP();
}
