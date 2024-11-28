#include "dhcp_common.h"
#include "dhcp_server.h"
#include "ethernet.h"


#define DHCP_SERVER_MAX_CLIENTS		5
#define DHCP_INPUT_QUEUE_LEN		DHCP_SERVER_MAX_CLIENTS

//network alignment is big-endian
#define DHCP_SERVER_IP 				LWIP_MAKEU32(192, 168, 0, 1)
#define DHCP_NET_NETMASK			LWIP_MAKEU32(255, 255, 255, 0)
#define DHCP_NET_GATEWAY			LWIP_MAKEU32(0, 0, 0, 0)

//DHCP_LEASE_TIME > DHCP_REBIND_TIME > DHCP_RENEW_TIME
#define DHCP_LEASE_TIME				0x96000000UL //150
#define DHCP_REBIND_TIME			0x78000000UL //120
#define DHCP_RENEW_TIME				0x3C000000UL //60

#define DHCP_NET_ADDR				(DHCP_SERVER_IP & DHCP_NET_NETMASK)
#define DHCP_NET_ADDR_MIN			(DHCP_NET_ADDR + 1)
#define DHCP_NET_ADDR_BRD			(DHCP_NET_ADDR | ~DHCP_NET_NETMASK)


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

//static struct {
//	uint32_t ip_addr;
//	uint32_t gw_addr;
//	uint32_t netmask;
//	uint8_t mac_addr[MAC_ADDR_LEN];
//} dhcp_server_info;

static struct {
	uint8_t msg_type;
	uint32_t requested_ip;
	uint32_t server_ip;
} server_options;

extern struct netif gnetif;

//Defined in dhcp_common.c
extern struct udp_pcb *dhcp_pcb;
extern struct pbuf *dhcp_pbuf;
//*****************************************//

static uint8_t addr_cnt = DHCP_SERVER_MAX_CLIENTS;
static uint32_t offer_ip = 0;
static DHCP_input_t udb_cb_buff, dhcp_in_buff;
static TaskHandle_t t_dhcp_server;
static QueueHandle_t q_dhcp_input;



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

inline static void sendMessage(uint32_t ip, uint8_t message_type) {
	uint8_t opt_ofst = 0;
//	uint16_t dhcp_out_len = 0;

	dhcpFillMessage(DHCP_FLD_XID, &dhcp_in_buff.input.xid);
	dhcpFillMessage(DHCP_FLD_FLAGS, (ip == IP4_ADDR_BROADCAST->addr) ? &(uint16_t){htons(BROADCAST_FLAG)} : &(uint16_t){0});
	dhcpFillMessage(DHCP_FLD_YIADDR, &offer_ip);
	dhcpFillMessage(DHCP_FLD_CHADDR, &dhcp_in_buff.input.chaddr);
	opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);
	opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_SERVER_ID, &gnetif.ip_addr);

	if (message_type != DHCP_NAK) {
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_T1, &(uint32_t){DHCP_RENEW_TIME});
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_T2, &(uint32_t){DHCP_REBIND_TIME});
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_LEASE_TIME, &(uint32_t){DHCP_LEASE_TIME});
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_SUBNET_MASK, &gnetif.netmask);
	}

	opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_END, NULL);
//	dhcp_out_len = DHCP_OPTIONS_OFS + opt_ofst;

	udp_sendto(dhcp_pcb, dhcp_pbuf, (ip_addr_t*)&ip, DHCP_CLIENT_PORT);
//	pbuf_remove_header(dhcp_pbuf, SIZEOF_ETH_HDR + IP_HLEN + UDP_HLEN);
}

inline static void parseOptions(void) {
	uint8_t *in_options_ptr = dhcp_in_buff.input.options;
	uint16_t length = dhcp_in_buff.length - DHCP_OPTIONS_OFS;
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
			//The requested IP was found in the pool
			if ( (server_options.requested_ip != 0) && \
					(pool_item = getPoolItemByIP(server_options.requested_ip)) >= 0 ) {

					//The requested IP is free or client is the registered owner
					if ( (dhcp_addr_pool[pool_item].state == FREE) || (memcmp(dhcp_addr_pool[pool_item].mac_addr, dhcp_in_buff.input.chaddr, MAC_ADDR_LEN) == 0) ) {
						dhcp_addr_pool[pool_item].state = OFFERED;
						offer_ip = dhcp_addr_pool[pool_item].ip_addr;
					}
			}

			//If no IP was offered on previous check and free addresses are available
			if ( (offer_ip == 0) && (addr_cnt > 0) ) {
				pool_item = getFreeIp();
				dhcp_addr_pool[pool_item].state = OFFERED;
				offer_ip = dhcp_addr_pool[pool_item].ip_addr;
			}

			if (offer_ip != 0)
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_OFFER);

		break;
		case DHCP_REQUEST:
			//Client renewing it's lease time or validating its configuration
			if (dhcp_in_buff.input.ciaddr.addr != 0) {
				pool_item = getPoolItemByIP(dhcp_in_buff.input.ciaddr.addr);

				//IP not found in the pool or client is not the owner
				if ( (pool_item < 0) || (memcmp(dhcp_addr_pool[pool_item].mac_addr, dhcp_in_buff.input.chaddr, MAC_ADDR_LEN) != 0)) {
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_NAK);
				} else {
					offer_ip = dhcp_addr_pool[pool_item].ip_addr;
					dhcp_addr_pool[pool_item].state = USED;

					//BROADCAST_FLAG -> Client restarts and tries to validate his configuration
					//UNICAST_FLAG -> Client tries to renew it's lease time
					sendMessage((dhcp_in_buff.input.flags == htons(BROADCAST_FLAG)) ? IP4_ADDR_BROADCAST->addr : dhcp_in_buff.src, DHCP_ACK);
				}

			//Request after an offer
			} else {
				pool_item = getPoolItemByIP(server_options.requested_ip);

				if (pool_item < 0 || dhcp_addr_pool[pool_item].state == USED) {
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_NAK);
				} else {
					offer_ip = dhcp_addr_pool[pool_item].ip_addr;
					memcpy(dhcp_addr_pool[pool_item].mac_addr, dhcp_in_buff.input.chaddr, MAC_ADDR_LEN);
					dhcp_addr_pool[pool_item].state = USED;
					addr_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_ACK);
				}
			}

		break;
		case DHCP_RELEASE:
			pool_item = isClient(dhcp_in_buff.input.chaddr);
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

void dhcpServerReceive(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
	if (p->len != p->tot_len)
		goto return_;

	udb_cb_buff.length = p->tot_len;
	udb_cb_buff.src = addr->addr;
	pbuf_copy_partial(p, &udb_cb_buff.input, udb_cb_buff.length, 0);

	xQueueSend(q_dhcp_input, &udb_cb_buff, 0);

	memset((void *)&udb_cb_buff, 0, sizeof(DHCP_input_t));

return_:
	pbuf_free(p);
}

static void task_dhcpServer(void *args) {
	for (;;) {
		xQueueReceive(q_dhcp_input, &dhcp_in_buff, portMAX_DELAY);
		handleMessage();
	}
}

void dhcpServerStart(void) {
	uint32_t ip_addr_init = DHCP_NET_ADDR_MIN;

	// DHCP pool init
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (ip_addr_init == DHCP_SERVER_IP)
			ip_addr_init++;

		dhcp_addr_pool[ix].ip_addr = htonl(ip_addr_init);
		ip_addr_init++;
	}

	netif_set_addr(&gnetif, &(ip4_addr_t){htonl(DHCP_SERVER_IP)}, &(ip4_addr_t){htonl(DHCP_NET_NETMASK)}, &(ip4_addr_t){htonl(DHCP_NET_GATEWAY)});

	if ( (q_dhcp_input = xQueueCreate(DHCP_INPUT_QUEUE_LEN, sizeof(DHCP_input_t))) == NULL )
		return;

	if (dhcpInit(DHCP_SERVER_PORT, dhcpServerReceive) == 1) {
		vQueueDelete(q_dhcp_input);
		return;
	}

	//Constant fields
	dhcpFillMessage(DHCP_FLD_OP, NULL);
	dhcpFillMessage(DHCP_FLD_HTYPE, NULL);
	dhcpFillMessage(DHCP_FLD_HLEN, NULL);
	dhcpFillMessage(DHCP_FLD_COOKIE, NULL);

	if (xTaskCreate(task_dhcpServer, "task_dhcpServer", 1024, NULL, 0, &t_dhcp_server) != pdPASS)
		return;
}
