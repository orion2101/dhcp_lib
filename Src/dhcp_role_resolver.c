#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rng.h"
#include "dhcp_server.h"

extern RNG_HandleTypeDef hrng;
extern struct netif gnetif;
TaskHandle_t t_dhcp_role_resolver;

static uint8_t getRandomInRange(uint8_t from, uint8_t to) {
	uint8_t result = 0;
	uint32_t random = 0;
	HAL_RNG_GenerateRandomNumber(&hrng, &random);

	while (result < from || result >= to) {
		result = (uint8_t)random;
		random = random >> 8;
		if (random == 0)
			HAL_RNG_GenerateRandomNumber(&hrng, &random);
	}

	return result;
}

void dhcp_role_resolver_task(void *args) {
	struct dhcp *dhcp = netif_dhcp_data(&gnetif);
	uint8_t dhcp_tries = getRandomInRange(1, 11);

	for (;;) {
		if (dhcp->state == DHCP_STATE_SELECTING && dhcp->tries >= dhcp_tries) {
			dhcp_release_and_stop(&gnetif);
			dhcp_server_init();
			vTaskDelete(NULL);
		}
	}
}

void dhcp_role_resolver(void) {
	xTaskCreate(dhcp_role_resolver_task, "dhcp_role_resolver", 256, NULL, 0, &t_dhcp_role_resolver);
}
