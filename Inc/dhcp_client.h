#ifndef DHCP_CLIENT_H
#define DHCP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

enum client_states {
	UNDEFINED = -1,
	INIT = 0,
	SELECTING,
	REQUESTING,
	BOUND,
	RENEWING,
	REBINDING,
	REBOOTING,
	INIT_REBOOT
};

typedef struct {
	int8_t state;
	uint8_t discover_cnt;
} DHCP_client_info;


DHCP_client_info dhcpClientGetInfo(void);
void dhcpClientStart(uint8_t is_standalone, uint8_t discover_cnt);
void dhcpClientStop(void);

#ifdef __cplusplus
}
#endif

#endif /* DHCP_CLIENT_H */
