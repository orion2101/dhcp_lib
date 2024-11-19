#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rng.h"
#include "dhcp_server.h"
#include "dhcp_client.h"

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
	dhcpClientStart(0, dhcp_tries);

	for (;;) {
		if (dhcpClientGetState() == SELECTING && dhcpClientGetDiscoveryTryCnt() == 0) {
			dhcpClientStop();
			dhcpServerStart();
			vTaskDelete(NULL);
		}

		vTaskDelay(1000);
	}
}

void dhcpRoleResolverStart(void) {
	xTaskCreate(task_dhcpRoleResolver, "dhcpRoleResolver", 256, NULL, 0, &t_dhcpRoleResolver);
}
