#include "dhcp_common.h"
#include "dhcp_server.h"
#include "ethernet.h"
#include "dhcp_config.h"


typedef enum {
	FREE,
	OFFERED,
	USED
} Binding_state_t;

typedef struct {
	Binding_state_t state;
	uint32_t 		ip_addr;
	uint8_t			mac_addr[MAC_ADDR_LEN];
} DHCP_pool_t;

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
} parsed_options;


//Defined in dhcp_common.c
extern struct udp_pcb *dhcp_pcb;
extern struct pbuf *dhcp_pbuf;
//*****************************************//

static DHCP_pool_t dhcp_pool[DHCP_SERVER_MAX_CLIENTS];
static uint8_t addr_cnt = DHCP_SERVER_MAX_CLIENTS;
static uint32_t offer_ip = 0;
static DHCP_input_t udb_cb_buff, dhcp_in_buff;
static TaskHandle_t t_dhcp_server;
static QueueHandle_t q_dhcp_input;



inline static DHCP_pool_t* getPoolItemByIP(uint32_t ip_addr) {
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (dhcp_pool[ix].ip_addr == ip_addr)
			return &dhcp_pool[ix];
	}

	return NULL;
}

inline static DHCP_pool_t* getFreeIp(void) {
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (dhcp_pool[ix].state != USED)
			return &dhcp_pool[ix];
	}

	return NULL;
}

inline static DHCP_pool_t* isClient(uint8_t *mac_addr) {
	for (int ix = 0; ix < DHCP_SERVER_MAX_CLIENTS; ix++) {
		if (memcmp(dhcp_pool[ix].mac_addr, mac_addr, (size_t)MAC_ADDR_LEN) == 0)
			return &dhcp_pool[ix];
	}

	return NULL;
}

inline static void sendMessage(uint32_t ip, uint8_t message_type) {
	uint8_t opt_ofst = 0;
//	uint16_t dhcp_out_len = 0;

	dhcpFillMessage(DHCP_FLD_XID, &dhcp_in_buff.input.xid);
	dhcpFillMessage(DHCP_FLD_FLAGS, (ip == IP4_ADDR_BROADCAST->addr) ? &(uint16_t){htons(BROADCAST_FLAG)} : &(uint16_t){0});
	dhcpFillMessage(DHCP_FLD_YIADDR, &offer_ip);
	dhcpFillMessage(DHCP_FLD_CHADDR, &dhcp_in_buff.input.chaddr);
	opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_MESSAGE_TYPE, &message_type);
	opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_SERVER_ID, &dhcp_netif->ip_addr);

	if (message_type != DHCP_NAK) {
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_T1, &(uint32_t){DHCP_RENEW_TIME});
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_T2, &(uint32_t){DHCP_REBIND_TIME});
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_LEASE_TIME, &(uint32_t){DHCP_LEASE_TIME});
		opt_ofst += dhcpFillOption(opt_ofst, DHCP_OPTION_SUBNET_MASK, &dhcp_netif->netmask);
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

	while ( (ofst < length) && (opt_code != DHCP_OPTION_END) ) {
		opt_len = *(in_options_ptr + ofst + 1);
		ofst += 2;
		opt_data = in_options_ptr + ofst;

		switch (opt_code) {
			case DHCP_OPTION_MESSAGE_TYPE:
				parsed_options.msg_type = *opt_data;
			break;
			case DHCP_OPTION_SERVER_ID:
				memcpy(&parsed_options.server_ip, opt_data, opt_len);
			break;
			case DHCP_OPTION_REQUESTED_IP:
				memcpy(&parsed_options.requested_ip, opt_data, opt_len);
			break;
			default: break;
		}

		ofst += opt_len;
		opt_code = *(in_options_ptr + ofst);
	}
}

inline static void handleMessage(void) {
	DHCP_pool_t *pool_item = NULL;
	offer_ip = 0;

	parseOptions();

	switch (parsed_options.msg_type) {
		case DHCP_DISCOVER:
			// The requested IP was found in the pool and is free or client is the registered owner
			if ( ( (parsed_options.requested_ip != 0) && (pool_item = getPoolItemByIP(parsed_options.requested_ip)) != NULL ) && \
				( (pool_item->state == FREE) || (memcmp(pool_item->mac_addr, dhcp_in_buff.input.chaddr, MAC_ADDR_LEN) == 0) ) ) 
			{
				pool_item->state = OFFERED;
				offer_ip = pool_item->ip_addr;
			} 
			// Maybe client initiates new discover without requesting his previous address ?
			else if ( (parsed_options.requested_ip == 0) && (pool_item = isClient(dhcp_in_buff.input.chaddr)) != NULL ) {
				pool_item->state = OFFERED;
				offer_ip = pool_item->ip_addr;
			}

			//If no IP was offered on previous check and free addresses are available
			if ( (offer_ip == 0) && (addr_cnt > 0) ) {
				pool_item = getFreeIp();
				pool_item->state = OFFERED;
				offer_ip = pool_item->ip_addr;
			}

			if (offer_ip != 0)
				sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_OFFER);

		break;
		case DHCP_REQUEST:
			//Client renewing it's lease time or validating its configuration
			if (dhcp_in_buff.input.ciaddr.addr != 0) {
				//IP not found in the pool or client is not the owner
				if ( (pool_item = getPoolItemByIP(dhcp_in_buff.input.ciaddr.addr)) == NULL || \
					(memcmp(pool_item->mac_addr, dhcp_in_buff.input.chaddr, MAC_ADDR_LEN) != 0) ) 
				{
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_NAK);
				}
				else {
					offer_ip = pool_item->ip_addr;
					pool_item->state = USED;

					//BROADCAST_FLAG -> Client restarts and tries to validate his configuration
					//UNICAST_FLAG -> Client tries to renew it's lease time
					sendMessage((dhcp_in_buff.input.flags == htons(BROADCAST_FLAG)) ? IP4_ADDR_BROADCAST->addr : dhcp_in_buff.src, DHCP_ACK);
				}

			//Request after an offer
			} else {
				if ( (pool_item = getPoolItemByIP(parsed_options.requested_ip)) == NULL || pool_item->state == USED ) {
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_NAK);
				} else {
					offer_ip = pool_item->ip_addr;
					memcpy(pool_item->mac_addr, dhcp_in_buff.input.chaddr, MAC_ADDR_LEN);
					pool_item->state = USED;
					addr_cnt--;
					sendMessage(IP4_ADDR_BROADCAST->addr, DHCP_ACK);
				}
			}

		break;
		case DHCP_RELEASE:
			if ( (pool_item = isClient(dhcp_in_buff.input.chaddr)) != NULL ) {
				pool_item->state = FREE;
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

		dhcp_pool[ix].ip_addr = htonl(ip_addr_init);
		ip_addr_init++;
	}

	netif_set_addr(dhcp_netif, &(ip4_addr_t){htonl(DHCP_SERVER_IP)}, &(ip4_addr_t){htonl(DHCP_NET_NETMASK)}, &(ip4_addr_t){htonl(DHCP_NET_GATEWAY)});

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
