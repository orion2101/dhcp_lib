#include "dhcp_common.h"
#include "dhcp_server.h"
#include "ethernet.h"


#define DHCP_SERVER_MAX_CLIENTS		5

//network alignment is big-endian
#define DHCP_NETWORK_IP 			LWIP_MAKEU32(192, 168, 0, 1)
#define DHCP_NETWORK_NETMASK		LWIP_MAKEU32(255, 255, 255, 0)
#define DHCP_NETWORK_GATEWAY		LWIP_MAKEU32(0, 0, 0, 0)

//DHCP_LEASE_TIME > DHCP_REBIND_TIME > DHCP_RENEW_TIME
#define DHCP_LEASE_TIME				0x96000000UL //150
#define DHCP_REBIND_TIME			0x78000000UL //120
#define DHCP_RENEW_TIME				0x3C000000UL //60


typedef enum {
	FREE,
	OFFERED,
	USED
} Binding_state_t;

static struct {
	Binding_state_t state;
	uint32_t 		ip_addr;
	uint8_t			mac_addr[MAC_ADDR_LEN];
} dhcp_addr_pool[DHCP_SERVER_MAX_CLIENTS];

static struct {
	uint32_t ip_addr;
	uint32_t gw_addr;
	uint32_t netmask;
	uint8_t mac_addr[MAC_ADDR_LEN];
} dhcp_server_info;

static struct {
	uint8_t msg_type;
	uint32_t requested_ip;
	uint32_t server_ip;
} server_options;

extern struct netif gnetif;

//Defined in dhcp_common.c
extern struct udp_pcb *dhcp_pcb;
extern struct pbuf *dhcp_pbuf;
extern uint8_t dhcp_in_buff[DHCP_OUT_BUFF_LEN];
//*****************************************//

static uint32_t network_ip, network_ip_min, network_broadcast;
static uint8_t addr_cnt = DHCP_SERVER_MAX_CLIENTS;
static uint32_t offer_ip = 0;
static uint16_t dhcp_in_len, dhcp_out_len;
static struct dhcp_msg *dhcp_in;
static TaskHandle_t t_dhcp_server;
static SemaphoreHandle_t s_dhcp_in_buff;


inline static int8_t getPoolItemByIP(uint32_t ip_addr) {
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (dhcp_addr_pool[ix].ip_addr == ip_addr)
			return ix;
	}

	return -1;
}

inline static uint8_t getFreeIp(void) {
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (dhcp_addr_pool[ix].state != USED)
			return ix;
	}

	return -1;
}

inline static int8_t isClient(uint8_t *mac_addr) {
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (memcmp(dhcp_addr_pool[ix].mac_addr, mac_addr, (size_t)MAC_ADDR_LEN) == 0)
			return ix;
	}

	return -1;
}

inline static int sendMessage(uint32_t ip, uint8_t message_type) {
	uint8_t opt_ofst = 0;
	int sent_len = 0;
	dhcp_out_len = 0;

	fillMessage(DHCP_FLD_XID, &dhcp_in->xid);
	fillMessage(DHCP_FLD_FLAGS, (ip == IP4_ADDR_BROADCAST->addr) ? &(uint16_t){htons(BROADCAST_FLAG)} : &(uint16_t){0});
	fillMessage(DHCP_FLD_YIADDR, &offer_ip);
	fillMessage(DHCP_FLD_CHADDR, dhcp_in->chaddr);
	opt_ofst += fillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);
	opt_ofst += fillOption(opt_ofst, DHCP_OPTION_SERVER_ID, &dhcp_server_info.ip_addr);

	if (message_type != DHCP_NAK) {
		opt_ofst += fillOption(opt_ofst, DHCP_OPTION_T1, &(uint32_t){DHCP_RENEW_TIME});
		opt_ofst += fillOption(opt_ofst, DHCP_OPTION_T2, &(uint32_t){DHCP_REBIND_TIME});
		opt_ofst += fillOption(opt_ofst, DHCP_OPTION_LEASE_TIME, &(uint32_t){DHCP_LEASE_TIME});
		opt_ofst += fillOption(opt_ofst, DHCP_OPTION_SUBNET_MASK, &dhcp_server_info.netmask);
	}

	opt_ofst += fillOption(opt_ofst, DHCP_OPTION_END, NULL);
	dhcp_out_len = DHCP_OPTIONS_OFS + opt_ofst;

	udp_sendto(dhcp_pcb, dhcp_pbuf, (ip_addr_t*)&ip, DHCP_CLIENT_PORT);
//	pbuf_remove_header(dhcp_pbuf, SIZEOF_ETH_HDR + IP_HLEN + UDP_HLEN);

	return sent_len;
}

inline static void parseOptions(void) {
	uint8_t *in_options_ptr = dhcp_in->options;
	uint16_t length = dhcp_in_len - DHCP_OPTIONS_OFS;
	uint16_t ofst = 0, opt_len = 0;
	uint8_t *opt_data = NULL;
	uint8_t opt_code = *in_options_ptr;

	while (ofst < length && opt_code != DHCP_OPTION_END) {
		opt_len = *(in_options_ptr + ofst + 1);
		ofst += 2;
		opt_data = in_options_ptr+ofst;

		switch (opt_code) {
			case DHCP_OPTION_MESSAGE_TYPE:
				server_options.msg_type = *opt_data;
			break;
			case DHCP_OPTION_SERVER_ID:
				memcpy(&server_options.server_ip, opt_data, opt_len);
			break;
			case DHCP_OPTION_REQUESTED_IP:
				memcpy(&server_options.requested_ip, opt_data, opt_len);
			break;
			default: break;
		}

		ofst += opt_len;
		opt_code = *(in_options_ptr + ofst);
	}
}

inline static void handleMessage(void) {
	int8_t pool_item = -1;
	offer_ip = 0;

	parseOptions();

	switch (server_options.msg_type) {
		case DHCP_DISCOVER:
			pool_item = getPoolItemByIP(server_options.requested_ip);

			//The requested IP was found in the pool
			if (pool_item >= 0) {
				uint8_t addr_state = dhcp_addr_pool[pool_item].state;

				//The requested IP is free or client is the registered owner
				if ( (addr_state == FREE) || (memcmp(dhcp_addr_pool[pool_item].mac_addr, dhcp_in->chaddr, MAC_ADDR_LEN) == 0) ) {
					dhcp_addr_pool[pool_item].state = OFFERED;
					offer_ip = dhcp_addr_pool[pool_item].ip_addr;
				}
			}
			else {
				if ((pool_item = getFreeIp()) != -1) {
					dhcp_addr_pool[pool_item].state = OFFERED;
					offer_ip = dhcp_addr_pool[pool_item].ip_addr;
				}
				// No free address
				else break;
			}

			//If no IP was offered on previous check and free addresses are available
//			if ( (offer_ip == 0) && (addr_cnt > 0) ) {
//				pool_item = getFreeIp();
//				dhcp_addr_pool[pool_item].state = OFFERED;
//				offer_ip = dhcp_addr_pool[pool_item].ip_addr;
//
//			} else if (addr_cnt == 0) break;

			sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_OFFER);

		break;
		case DHCP_REQUEST:
			//Client renewing it's lease time or validating its configuration
			if (dhcp_in->ciaddr.addr != 0) {
				pool_item = getPoolItemByIP(dhcp_in->ciaddr.addr);

				//IP not found in the pool or client is not the owner
				if ( (pool_item < 0) || (memcmp(dhcp_addr_pool[pool_item].mac_addr, dhcp_in->chaddr, MAC_ADDR_LEN) != 0)) {
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_NAK);
				} else {
					offer_ip = dhcp_addr_pool[pool_item].ip_addr;
					dhcp_addr_pool[pool_item].state = USED;

					//BROADCAST_FLAG -> Client restarts and tries to validate his configuration
					//UNICAST_FLAG -> Client tries to renew it's lease time
//					sendMessage((dhcp_in->flags == htons(BROADCAST_FLAG)) ? IP4_ADDR_BROADCAST->addr : remote_sa.sin_addr.s_addr, DHCP_ACK);
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_ACK);
				}

			//Request after an offer
			} else {
				pool_item = getPoolItemByIP(server_options.requested_ip);

				if (pool_item < 0 || dhcp_addr_pool[pool_item].state == USED) {
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_NAK);
				} else {
					offer_ip = dhcp_addr_pool[pool_item].ip_addr;
					memcpy(dhcp_addr_pool[pool_item].mac_addr, dhcp_in->chaddr, MAC_ADDR_LEN);
					dhcp_addr_pool[pool_item].state = USED;
					addr_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_ACK);
				}
			}

		break;
		case DHCP_RELEASE:
			pool_item = isClient(dhcp_in->chaddr);
			if (pool_item >= 0) {
				dhcp_addr_pool[pool_item].state = FREE;
				addr_cnt++;
			}
		break;
		case DHCP_DECLINE:
			//Client informs us that the offered address is in use by another host
		break;
		case DHCP_INFORM:
			//Client tries to request other parameters
		break;
		default: break;
	}
}

inline static void dhcpServerInit(void) {
	dhcp_server_info.ip_addr = DHCP_NETWORK_IP;
	dhcp_server_info.netmask = DHCP_NETWORK_NETMASK;
	dhcp_server_info.gw_addr = DHCP_NETWORK_GATEWAY;

	network_ip = dhcp_server_info.ip_addr & dhcp_server_info.netmask;
	network_ip_min = network_ip + 1;
	network_broadcast = network_ip | ~dhcp_server_info.netmask;

	uint32_t ip_addr_init = network_ip_min;

	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (ip_addr_init == dhcp_server_info.ip_addr)
			ip_addr_init++;

		dhcp_addr_pool[ix].ip_addr = htonl(ip_addr_init);
		ip_addr_init++;
	}

	dhcp_server_info.ip_addr = htonl(dhcp_server_info.ip_addr);
	dhcp_server_info.netmask = htonl(dhcp_server_info.netmask);
	dhcp_server_info.gw_addr = htonl(dhcp_server_info.gw_addr);
	netif_set_addr(&gnetif, (ip4_addr_t*)&dhcp_server_info.ip_addr, (ip4_addr_t*)&dhcp_server_info.netmask, (ip4_addr_t*)&dhcp_server_info.gw_addr);

	//Constant fields
	fillMessage(DHCP_FLD_OP, NULL);
	fillMessage(DHCP_FLD_HTYPE, NULL);
	fillMessage(DHCP_FLD_HLEN, NULL);
	fillMessage(DHCP_FLD_COOKIE, NULL);
}

void dhcpServerReceive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
	dhcp_in = (struct dhcp_msg*)p->payload;
	dhcp_in_len = p->tot_len;
//	if (xSemaphoreTake(s_dhcp_in_buff, pdMS_TO_TICKS(50)) == pdTRUE) {
//		pbuf_copy_partial(p, (void*)dhcp_in_buff, dhcp_in_len, 0);
//		xSemaphoreGive(s_dhcp_in_buff);
//	}

	handleMessage();

	pbuf_free(p);
}

static void task_dhcpServer(void *args) {
	if ( (s_dhcp_in_buff = xSemaphoreCreateBinary()) == NULL)
		vTaskDelete(NULL);

	xSemaphoreGive(s_dhcp_in_buff);

	for (;;) {
		xSemaphoreTake(s_dhcp_in_buff, portMAX_DELAY);
		dhcp_in = (struct dhcp_msg*)dhcp_in_buff;
		handleMessage();
		xSemaphoreGive(s_dhcp_in_buff);

		vTaskDelay(2000);
	}
}

void dhcpServerStart(void) {
	initDHCP(DHCP_SERVER_PORT, dhcpServerReceive);
	dhcpServerInit();
//	if (xTaskCreate(task_dhcpServer, "task_dhcpServer", 1024, NULL, 0, &t_dhcp_server) != pdPASS)
//		return;
}
