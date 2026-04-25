#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
} espnow_queue_t;

typedef struct {
} serial_queue_t;

static const char *TAG = "MAIN";
static QueueHandle_t my_espnow_send_queue = NULL;
static QueueHandle_t my_serial_send_queue = NULL;


void my_esp_now_info(void *args){
    // --- OPTIONAL: Read ESP-NOW version ---
    uint32_t version = 0;
    esp_now_get_version(&version);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); 

    for(;;){
        ESP_LOGI(TAG, "ESP-NOW version: %u", version);
        ESP_LOGI("MAC", "MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        vTaskDelay(2000 / portTICK_PERIOD_MS); 
    }
}

void recv_serial(void *args){
    // read serial
    // add to queue
}
void send_serial(void *args){
    // xQueueReceive(my_serial_send_queue, &evt, portMAX_DELAY) == pdTRUE
}


void recv_espnow(void *args){
}
void send_espnow(void *args){
}

void app_main(void)
{
    // --- REQUIRED: Initialize NVS ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- REQUIRED: Initialize networking stack ---
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // --- REQUIRED: Initialize Wi-Fi driver ---
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi initialized");

    // --- REQUIRED: Initialize ESP-NOW ---
    ESP_ERROR_CHECK(esp_now_init());
    ESP_LOGI(TAG, "ESP-NOW initialized");

   xTaskCreate(my_esp_now_info, "my_esp_now_info", 2048, NULL, 4, NULL);
}
