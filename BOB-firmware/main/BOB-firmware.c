#include "balance.h"
#include "drv8833.h"
#include "wifi.h"

void app_main(void) {
    drv8833_init();
    drv8833_wake();

    ESP_ERROR_CHECK(balance_start());
    ESP_ERROR_CHECK(wifi_init());
}
