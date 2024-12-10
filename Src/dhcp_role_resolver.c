#include "rng.h"
#include "FreeRTOS.h"
#include "task.h"
#include "dhcp_server.h"
#include "dhcp_client.h"


#define DHCP_ROLE_RESOLVER_DELAY_MS		DHCP_RESPONSE_TIMEOUT_MS
#define DHCP_ROLE_RESOLVER_STACK_SZ		256

#define DHCP_TRIES_RND_MIN		1
#define DHCP_TRIES_RND_MAX		15

extern RNG_HandleTypeDef hrng;

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
	uint8_t dhcp_tries = getRandomInRange(DHCP_TRIES_RND_MIN, DHCP_TRIES_RND_MAX);
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
	if (xTaskCreate(task_dhcpRoleResolver, "dhcpRoleResolver", DHCP_ROLE_RESOLVER_STACK_SZ, NULL, 0, &t_dhcpRoleResolver) != pdPASS)
		return;
}
