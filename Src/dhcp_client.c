#include "dhcp_common.h"
#include "dhcp_client.h"
#include "rng.h"


#define DHCP_TASK_STACK_SIZE		1024
#define DHCP_TASK_DELAY_VAL_MS 		10

#define DHCP_DISCOVER_TRIES_MIN		5
#define DHCP_DISCOVER_TRIES_MAX		10
#define DHCP_SERVER_TIMEOUT			2000


enum client_states {
	INIT = 0,
	SELECTING,
	REQUESTING,
	BOUND,
	RENEWING,
	REBINDING,
	REBOOTING,
	INIT_REBOOT
};

extern TaskHandle_t	t_dhcp_role_resolver;

extern RNG_HandleTypeDef hrng;
extern struct netif gnetif;


//Defined in dhcp_common.c
extern const uint8_t *in_options_ptr;
extern network_settings_t network_settings;
extern client_options_t client_options;
extern struct dhcp_msg dhcp_in, dhcp_out;
extern int dhcp_in_len, dhcp_out_len;
extern uint8_t dhcp_role;
extern struct sockaddr_in remote_sa, local_sa;
extern socklen_t remote_sa_len;
extern int socket_fd;
//*****************************************//

static uint8_t client_can_be_server;
static uint8_t discover_tries_cnt;
static uint8_t client_state;
static uint32_t transaction_id;
static uint8_t listen;
static uint32_t renew_timeout, rebind_timeout, lease_timeout, server_timeout;
static TaskHandle_t t_dhcp_client;

inline static uint8_t isServerTimeout(void) {
	if (server_timeout >= DHCP_SERVER_TIMEOUT)
		return 1;

	return 0;
}

uint8_t getDiscoverCnt(void) {
	return discover_tries_cnt;
}

inline static void resetDiscoverCnt(void) {
	uint32_t random = 0;
    HAL_RNG_GenerateRandomNumber(&hrng, &random);
    discover_tries_cnt = (uint8_t)(random & 0x000000FF);
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
			opt_ofst += fillOption(opt_ofst, DHCP_OPTION_REQUESTED_IP, &dhcp_in.yiaddr.addr);
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

	remote_sa.sin_addr.s_addr = ip;
	sent_len = send_dhcp();

	return sent_len;
}

inline static void updateTimeout(void) {
	renew_timeout += DHCP_TASK_DELAY_VAL_MS;
	rebind_timeout += DHCP_TASK_DELAY_VAL_MS;
	lease_timeout += DHCP_TASK_DELAY_VAL_MS;
}

inline static void clearTimeout(void) {
	renew_timeout = 0;
	rebind_timeout = 0;
	lease_timeout = 0;
}

inline static uint8_t forUs(void) {
	if (dhcp_in.xid != transaction_id || memcmp(dhcp_in.chaddr, network_settings.mac_addr, MAC_ADDR_LEN) != 0)
		return 0;

	return 1;
}

inline static void parseOptions(void) {
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

	if (dhcp_in_len > 0 && forUs())
		parseOptions();

	switch (client_state) {
		case INIT:
			if (discover_tries_cnt > 0) {
				transaction_id += 1;
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_DISCOVER);
				client_state = SELECTING;
				listen = 1;
				discover_tries_cnt--;
			} else {
				if (client_can_be_server) {
					deinitDHCPSocket();
					vTaskResume(t_dhcp_role_resolver);
					vTaskDelete(NULL);
				} else {
					resetDiscoverCnt();
				}
			}
		break;
		case SELECTING:
			if (dhcp_in_len < 0) {
				if (isServerTimeout()) {
					client_state = INIT;
					listen = 0;
				}
				break;
			}

			if (client_options.msg_type != DHCP_OFFER) {
				client_state = INIT;
				listen = 0;
				break;
			}

			sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
			client_state = REQUESTING;
			listen = 1;
		break;
		case REQUESTING:
			if (dhcp_in_len < 0) {
				if (isServerTimeout()) {
					client_state = INIT;
					listen = 0;
				}
				break;
			}

			if (client_options.msg_type != DHCP_ACK) {
				client_state = INIT;
				listen = 0;
				break;
			}

			clearTimeout();
//			autoip_stop(&gnetif);
			network_settings.gw_addr = 0;
			network_settings.ip_addr = dhcp_in.yiaddr.addr;
			network_settings.netmask = client_options.subnet_mask;
			netif_set_addr(&gnetif, &network_settings.ip_addr, &network_settings.netmask, &network_settings.gw_addr);
			resetDiscoverCnt();
			listen = 0;
			client_state = BOUND;
		break;
		case BOUND:
			updateTimeout();
			if (renew_timeout >= client_options.renewal_time) {
				transaction_id += 1;
				sendMessage(client_options.server_ip, DHCP_REQUEST);
				client_state = RENEWING;
				listen = 1;
			}
		break;
		case RENEWING:
			if (dhcp_in_len > 0) {
				if (client_options.msg_type != DHCP_ACK) {
					memset(&client_options, 0, sizeof(client_options_t));
					client_state = INIT;
				} else {
					clearTimeout();
					client_state = BOUND;
				}

				listen = 0;
			} else {
				updateTimeout();
				if (rebind_timeout >= client_options.rebinding_time) {
					transaction_id += 1;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_REQUEST);
					client_state = REBINDING;
					listen = 1;
				}
			}
		break;
		case REBINDING:
			if (dhcp_in_len > 0) {
				if (client_options.msg_type != DHCP_ACK) {
					memset(&client_options, 0, sizeof(client_options_t));
					client_state = INIT;
				} else {
					clearTimeout();
					client_state = BOUND;
				}

				listen = 0;
			} else {
				updateTimeout();
				if (lease_timeout >= client_options.lease_time) {
					memset(&client_options, 0, sizeof(client_options_t));
					client_state = INIT;
				}
			}
		break;
		case REBOOTING:

		break;
		case INIT_REBOOT:

		break;
	}

}

static void dhcp_client_task(void const *args) {

    for (;;) {
    	if (listen == 1) {
    		server_timeout += DHCP_TASK_DELAY_VAL_MS;
			receive_dhcp();
			if (dhcp_in_len > 0)
				server_timeout = 0;
    	}

		stateManager();

		vTaskDelay(DHCP_TASK_DELAY_VAL_MS);
    }
}

void dhcp_client_init(void) {
	dhcp_role = DHCP_CLIENT;
	initDHCPSocket(dhcp_role);
    client_can_be_server = CLIENT_CAN_BE_SERVER;
    client_state = INIT;
    resetDiscoverCnt();

	//constant fields
	fillMessage(DHCP_FLD_OP, NULL);
	fillMessage(DHCP_FLD_HTYPE, NULL);
	fillMessage(DHCP_FLD_HLEN, NULL);
	fillMessage(DHCP_FLD_COOKIE, NULL);
	memcpy(network_settings.mac_addr, gnetif.hwaddr, MAC_ADDR_LEN);

    xTaskCreate(dhcp_client_task, "dhcp_client", DHCP_TASK_STACK_SIZE, NULL, 0, &t_dhcp_client);
}
