#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "rng.h"
#include "dhcp_server.h"

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
	struct dhcp *dhcp = netif_dhcp_data(&gnetif);
	uint8_t dhcp_tries = getRandomInRange(1, 11);

	for (;;) {
		// On lwip init, if the NETIF_FLAG_UP flag is not set, lwip doesn't allocate DHCP data and, as a result, the task will not be able to configure the device as either a client or a server.\
			Note that NETIF_FLAG_UP flag is set when NETIF_FLAG_LINK_UP flag is set.

		if ((gnetif.flags & NETIF_FLAG_LINK_UP) && dhcp == NULL) {
			dhcp_start(&gnetif);
			dhcp = netif_dhcp_data(&gnetif);
		}

		if (dhcp->state == DHCP_STATE_SELECTING && dhcp->tries >= dhcp_tries) {
			dhcp_release_and_stop(&gnetif);
			dhcp_server_init();
			vTaskDelete(NULL);
		}

		vTaskDelay(1000);
	}
}

void dhcp_role_resolver(void) {
	xTaskCreate(dhcp_role_resolver_task, "dhcp_role_resolver", 256, NULL, 0, &t_dhcp_role_resolver);
}
