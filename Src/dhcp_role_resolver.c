#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rng.h"
#include "dhcp_server.h"
#include "dhcp_client.h"

extern RNG_HandleTypeDef hrng;
extern struct netif gnetif;
static TaskHandle_t t_dhcp_role_resolver;

static uint8_t getRandomInRange(uint8_t from, uint8_t to) {
	uint32_t random = 0;
	const uint8_t *result = (uint8_t *)&random;

	do {
		if ((random >>= 1) == 0)
			HAL_RNG_GenerateRandomNumber(&hrng, &random);
	} while (*result < from || *result >= to);

	return *result;
}

void dhcp_role_resolver_task(void *args) {
	uint8_t dhcp_tries = getRandomInRange(1, 11);
	dhcpClientStart(0, dhcp_tries);

	for (;;) {
		if (dhcpClientGetState() == SELECTING && dhcpClientGetDiscoveryTryCnt() == 0) {
			dhcpClientStop();
			dhcp_server_init();
			vTaskDelete(NULL);
		}

		vTaskDelay(1000);
	}
}

void dhcp_role_resolver(void) {
	xTaskCreate(dhcp_role_resolver_task, "dhcp_role_resolver", 256, NULL, 0, &t_dhcp_role_resolver);
}
