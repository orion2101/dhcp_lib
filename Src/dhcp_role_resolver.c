#include "dhcp_client.h"
#include "dhcp_server.h"
#include "FreeRTOS.h"
#include "task.h"

TaskHandle_t t_dhcp_role_resolver;

void dhcp_role_resolver_task(void const *args) {
	dhcp_client_init();
	vTaskSuspend(NULL);
	dhcp_server_init();
	vTaskDelete(NULL);
}

void dhcp_role_resolver(void) {
	xTaskCreate(dhcp_role_resolver_task, "dhcp_role_resolver", 256, NULL, 0, &t_dhcp_role_resolver);
}
