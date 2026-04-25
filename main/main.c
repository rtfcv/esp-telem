#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mymac.h"

// dummy max delay
#define ESPNOW_MAXDELAY 10*portMAX_DELAY
#define BUFFER_SIZE 250 // max of 250 bytes

typedef struct {
    uint8_t buffer[BUFFER_SIZE];
} espnow_queue_t;

typedef struct {
    uint8_t buffer[BUFFER_SIZE];
} serial_queue_t;

static const char *TAG = "MAIN";
static QueueHandle_t my_espnow_send_queue = NULL;
static QueueHandle_t my_serial_send_queue = NULL;
static const uint8_t dest_addr[] = MY_DEST_MAC_ADDR;


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

void dummy_data(void *args){ 
    const static uint8_t data[] = "hello_world";
    for(;;){
        esp_err_t result = esp_now_send(dest_addr, (uint8_t *) &data, sizeof(data));
        ESP_LOGI(TAG, "sending data whose result was: %s", esp_err_to_name(result));
        vTaskDelay(2000 / portTICK_PERIOD_MS); 
    }
}

void my_add_peer(){
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, dest_addr, 6);
    peer.channel = 0;          // 0 = current WiFi channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t add_status = esp_now_add_peer(&peer);
    ESP_LOGI(TAG, "add_peer: %s", esp_err_to_name(add_status));
}

void recv_serial(void *args){
    // read serial and add contents to espnow queue
    espnow_queue_t buffer;
    strcpy((char*)buffer.buffer, "");
    // divide serial buffer into esp queue
    xQueueSend(my_espnow_send_queue, &buffer, ESPNOW_MAXDELAY);
}
void send_serial(void *args){
    // read serial queue and relay its contents to serial tx
    serial_queue_t buffer;
    // my_serial_send_queue should not be NULL...
    while(xQueueReceive(my_serial_send_queue, &buffer, portMAX_DELAY) == pdTRUE){
        ESP_LOGI(TAG, "%s", buffer.buffer);
    }
}

void recv_espnow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len){
    // callback for espnow recieving event

    ESP_LOGI(TAG, "receiving data");
    serial_queue_t buffer;
    memcpy(&(buffer.buffer), incomingData, BUFFER_SIZE);
    // Serial.write(buf_recv, len);
    // divide serial buffer into esp queue
    xQueueSend(my_serial_send_queue, &buffer, ESPNOW_MAXDELAY);
}
void send_espnow(void *args){
    // read espnow queue and relay its contents to espnow
    espnow_queue_t buffer;
    // my_espnow_send_queue should not be NULL...
    while(xQueueReceive(my_espnow_send_queue, &buffer, portMAX_DELAY) == pdTRUE){
        ESP_LOGI(TAG, "dummy");
    }
}


void my_setup(void){
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

    //
    my_add_peer();

    // xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    my_espnow_send_queue = xQueueCreate(6, sizeof(espnow_queue_t));
    my_serial_send_queue = xQueueCreate(6, sizeof(serial_queue_t));
    
    esp_now_register_recv_cb(recv_espnow);
}

void app_main(void)
{
    my_setup();
    xTaskCreate(my_esp_now_info, "my_esp_now_info", 2048, NULL, 4, NULL);
    xTaskCreate(dummy_data, "dummy_data", 2048, NULL, 4, NULL);
    xTaskCreate(send_serial, "send_serial", 2048, NULL, 4, NULL);
}
