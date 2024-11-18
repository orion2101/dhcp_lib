#ifndef DHCP_CLIENT_H
#define DHCP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

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

uint8_t dhcpClientGetState(void);
uint8_t dhcpClientGetDiscoveryTryCnt(void);
void dhcpClientStart(uint8_t is_standalone, uint8_t discover_cnt);
void dhcpClientStop(void);

#ifdef __cplusplus
}
#endif

#endif
