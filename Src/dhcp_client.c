#include "projdefs.h"
#include "ethernet.h"
#include "dhcp_common.h"
#include "dhcp_client.h"
#include "dhcp_config.h"
#include "rng.h"


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


/* Defined in dhcp_common.c */
extern struct udp_pcb *dhcp_pcb;
extern struct pbuf *dhcp_pbuf;
/***********************************/

static uint16_t dhcp_in_len;
static struct dhcp_msg *dhcp_in;
static uint8_t standalone;
static uint8_t discover_try_cnt;
static uint8_t try_cnt = DHCP_TRY_CNT;
static uint8_t client_state;
static uint32_t transaction_id;
static uint32_t elapsed = 0;
static TaskHandle_t t_dhcp_client;
static SemaphoreHandle_t s_client_info;

DHCP_client_info dhcpClientGetInfo(void) {
	if (xSemaphoreTake(s_client_info, 0) != pdTRUE)
		return (DHCP_client_info){UNDEFINED, 1};

	DHCP_client_info info;
	info.state = client_state;
	info.discover_cnt = discover_try_cnt;
	xSemaphoreGive(s_client_info);

	return info;
}

inline static void sendMessage(uint32_t ip, uint8_t message_type) {
	uint8_t opt_ofst = 0;
//	int sent_len = 0;
//	dhcp_out_len = 0;

	dhcpFillMessage(DHCP_FLD_FLAGS, (ip == IP4_ADDR_BROADCAST->addr) ? &(uint16_t){htons(BROADCAST_FLAG)} : &(uint16_t){0});
	switch (message_type) {
		case DHCP_DISCOVER:
			dhcpFillMessage(DHCP_FLD_XID, &transaction_id);
			dhcpFillMessage(DHCP_FLD_CHADDR, &network_settings.mac_addr);
			opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);
		break;
		case DHCP_REQUEST:
			dhcpFillMessage(DHCP_FLD_XID, &transaction_id);
			dhcpFillMessage(DHCP_FLD_CHADDR, network_settings.mac_addr);
			opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);

			if (client_state == BOUND || client_state == RENEWING || client_state == REBINDING)
				dhcpFillMessage(DHCP_FLD_CIADDR, &network_settings.ip_addr);
			else {
				opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_REQUESTED_IP, (uint8_t*)&dhcp_in->yiaddr.addr);
				opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_SERVER_ID, (uint8_t*)&client_options.server_ip);
			}
		break;
		case DHCP_DECLINE:

		break;
		case DHCP_RELEASE:

		break;
		case DHCP_INFORM:

		break;
		default: break;
	}
	opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_END, NULL);
//	dhcp_out_len = DHCP_OPTIONS_OFS + opt_ofst;

	udp_sendto(dhcp_pcb, dhcp_pbuf, (ip_addr_t*)&ip, DHCP_SERVER_PORT);
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
	// Is the incoming message a response to our request ?
	if (dhcp_in->xid != transaction_id || memcmp(dhcp_in->chaddr, network_settings.mac_addr, MAC_ADDR_LEN) != 0)
		return;
	else
		parseOptions();

	switch (client_state) {
//		case INIT:
//
//		break;
		case SELECTING:
			if (client_options.msg_type == DHCP_OFFER)
				client_state = REQUESTING;
		break;
		case REQUESTING:
			if (client_options.msg_type != DHCP_ACK)
				client_state = SELECTING;
			else {
				network_settings.gw_addr = 0;
				network_settings.ip_addr = dhcp_in->yiaddr.addr;
				network_settings.netmask = client_options.subnet_mask;
				netif_set_addr(dhcp_netif, (ip4_addr_t*)&network_settings.ip_addr, (ip4_addr_t*)&network_settings.netmask, (ip4_addr_t*)&network_settings.gw_addr);
				elapsed = 0;
				client_state = BOUND;
			}
		break;
//		case BOUND:
//
//		break;
		case RENEWING:
			if (client_options.msg_type != DHCP_ACK) {
				memset(&client_options, 0, sizeof(client_options));
				dhcpFillMessage(DHCP_FLD_CIADDR, &(uint32_t){0});
				dhcpClearOptions();
				client_state = INIT;
			} else {
				elapsed = 0;
				client_state = BOUND;
			}
		break;
		case REBINDING:
			if (client_options.msg_type != DHCP_ACK) {
				memset(&client_options, 0, sizeof(client_options));
				dhcpFillMessage(DHCP_FLD_CIADDR, &(uint32_t){0});
				dhcpClearOptions();
				client_state = INIT;
			} else {
				elapsed = 0;
				client_state = BOUND;
			}
		break;
//		case REBOOTING:
//
//		break;
//		case INIT_REBOOT:
//
//		break;
		default: break;
	}
}

void task_dhcpClient(void *args) {
	for (;;) {
		elapsed += DHCP_RESPONSE_TIMEOUT_MS;

		if (xSemaphoreTake(s_client_info, 0) != pdTRUE)
			continue;

check_state:
		switch (client_state) {
			case INIT:
				if (discover_try_cnt == 0) {
					if (standalone)
						discover_try_cnt = DHCP_TRY_CNT;
					else
						break;
				}

				discover_try_cnt--;
				transaction_id = dhcpGenerateUint32();
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				client_state = SELECTING;
			break;
			case SELECTING:
				if (try_cnt > 0) {
					try_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				}
				else {
					client_state = INIT;
					goto check_state;
				}
			break;
			case REQUESTING:
				if (try_cnt > 0) {
					try_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
				}
				else {
					try_cnt = DHCP_TRY_CNT;
					transaction_id++;
					client_state = SELECTING;
					goto check_state;
				}
			break;
			case BOUND:
				if (elapsed >= client_options.renewal_time) {
					transaction_id++;
					client_state = RENEWING;
					goto check_state;
				}
			break;
			case RENEWING:
				if (elapsed >= client_options.rebinding_time) {
					transaction_id++;
					client_state = REBINDING;
					goto check_state;
				}
				else
					sendMessage(client_options.server_ip, DHCP_REQUEST);
			break;
			case REBINDING:
				if (elapsed >= client_options.lease_time) {
					memset(&client_options, 0, sizeof(client_options));
					dhcpFillMessage(DHCP_FLD_CIADDR, &(uint32_t){0});
					dhcpClearOptions();
					client_state = INIT;
					goto check_state;
				}
				else
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
			break;
			default: break;
		}

		xSemaphoreGive(s_client_info);
		vTaskDelay(pdMS_TO_TICKS(DHCP_RESPONSE_TIMEOUT_MS));
	}
}

void dhcpClientReceive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
	if (p->len != p->tot_len)
		goto return_;

	if (xSemaphoreTake(s_client_info, 0) == pdTRUE) {
		dhcp_in = (struct dhcp_msg*)p->payload;
		dhcp_in_len = p->tot_len;
		stateManager();
		xSemaphoreGive(s_client_info);
	}

return_:
	pbuf_free(p);
}

/**
 * @brief   Run the DHCP client.
 * @param	[in]	netif			 Pointer to the network interface on which the client should be run.
 * @param   [in]	is_standalone	 If the client autonomous set this value to 1. If the client is managed by an external thread, set this value to 0.
 * @param	[in]	discover_cnt	 Number of times the client must try to configure itself. This parameter is ignored if the client is managed (is_standalone == 0).
 */
void dhcpClientStart(uint8_t is_standalone, uint8_t discover_cnt) {
	client_state = INIT;
	standalone = is_standalone;
	discover_try_cnt = discover_cnt;
	memcpy(network_settings.mac_addr, dhcp_netif->hwaddr, MAC_ADDR_LEN);

	if ( (s_client_info = xSemaphoreCreateBinary()) == NULL )
		return;
	xSemaphoreGive(s_client_info);

	if (dhcpInit(DHCP_CLIENT_PORT, dhcpClientReceive) == 1) {
		vSemaphoreDelete(s_client_info);
		return;
	}

	//constant fields
	dhcpFillMessage(DHCP_FLD_OP, NULL);
	dhcpFillMessage(DHCP_FLD_HTYPE, NULL);
	dhcpFillMessage(DHCP_FLD_HLEN, NULL);
	dhcpFillMessage(DHCP_FLD_COOKIE, NULL);

	if (xTaskCreate(task_dhcpClient, "task_dhcpClient", DHCP_CLIENT_STACK_SZ, NULL, 0, &t_dhcp_client) != pdPASS) {
		dhcpDeinit();
		vSemaphoreDelete(s_client_info);
		return;
	}
}

void dhcpClientStop(void) {
	if (xSemaphoreTake(s_client_info, portMAX_DELAY) == pdTRUE) {
		vTaskSuspend(t_dhcp_client);
		dhcpDeinit();
		xSemaphoreGive(s_client_info);
		vSemaphoreDelete(s_client_info);
		vTaskDelete(t_dhcp_client);
	}
}
