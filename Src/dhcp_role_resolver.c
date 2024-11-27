#include "rng.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dhcp_server.h"
#include "dhcp_client.h"


#define DHCP_ROLE_RESOLVER_DELAY_MS		DHCP_RESPONSE_TIMEOUT_MS
#define DHCP_ROLE_RESOLVER_STACK_SZ		256

extern RNG_HandleTypeDef hrng;
extern struct netif gnetif;
static TaskHandle_t t_dhcpRoleResolver;

static uint8_t getRandomInRange(uint8_t from, uint8_t to) {
	uint32_t random = 0;
	const uint8_t *result = (uint8_t *)&random;

	do {
		if ((random >>= 1) == 0)
			HAL_RNG_GenerateRandomNumber(&hrng, &random);
	} while (*result < from || *result >= to);

	return *result;
}

static void task_dhcpRoleResolver(void *args) {
	uint8_t dhcp_tries = getRandomInRange(1, 11);
	DHCP_client_info client_info;
	dhcpClientStart(0, dhcp_tries); //client start with first parameter as 0 (managed)

	for (;;) {
		client_info = dhcpClientGetInfo();
		if (client_info.state == INIT && client_info.discover_cnt == 0) {
			dhcpClientStop();
			dhcpServerStart();
			vTaskDelete(NULL);
		}

		vTaskDelay(pdMS_TO_TICKS(DHCP_ROLE_RESOLVER_DELAY_MS * dhcp_tries));
	}
}

void dhcpRoleResolverStart(void) {
	xTaskCreate(task_dhcpRoleResolver, "dhcpRoleResolver", DHCP_ROLE_RESOLVER_STACK_SZ, NULL, 0, &t_dhcpRoleResolver);
}
