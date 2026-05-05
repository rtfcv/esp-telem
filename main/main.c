#define LOG_LOCAL_LEVEL ESP_LOG_ERROR

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#include "mymac.h"

// dummy max delay
#define ESPNOW_MAXDELAY 10*portMAX_DELAY
#define BUFFER_SIZE 250 // max of 250 bytes

// works on esp32c6 for now.
#define UART_PORT UART_NUM_1
#define TX_PIN 16
#define RX_PIN 17
#define BUF_SIZE 1024 // serial buffer size

typedef struct {
    uint8_t buffer[BUFFER_SIZE];
    size_t  size;  //should be max of 250
} espnow_queue_t;

typedef struct {
    uint8_t buffer[BUFFER_SIZE];
    size_t  size;  //should be max of 250
} serial_queue_t;

static const char *TAG = "MAIN";
static QueueHandle_t my_espnow_send_queue = NULL;
static QueueHandle_t my_serial_send_queue = NULL;
static uint8_t dest_addr[] = { 0, 0, 0, 0, 0, 0 };
static QueueHandle_t uart_queue = NULL;

void serial_setup(void){
    uart_config_t config = {
        .baud_rate = 115200,
        // .baud_rate = 57600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_PORT, &config);
    uart_set_pin(UART_PORT, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE, 20, &uart_queue, 0);
}

// print debug info
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

void recv_serial(void *pvParameters)
{
    // read serial and add contents to espnow queue
    uart_event_t event;
    uint8_t* data = malloc(BUF_SIZE);
    espnow_queue_t buffer;

    while (1) {if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
        switch (event.type) {
            case UART_DATA:
                int len = uart_read_bytes(UART_PORT, data, event.size, portMAX_DELAY);
                ESP_LOGI(TAG, "RX: %.*s", event.size, data);
                int offset = 0;
                int chunk_size = BUFFER_SIZE;
                while (offset < len) {
                    if (chunk_size > len - offset) {chunk_size = len - offset;}

                    memcpy(buffer.buffer, data + offset, chunk_size);
                    buffer.size = chunk_size;

                    xQueueSend(my_espnow_send_queue, &buffer, ESPNOW_MAXDELAY);
                    offset += chunk_size;
                }
                break;

            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "HW FIFO Overflow");
                uart_flush_input(UART_PORT);
                xQueueReset(uart_queue);
                break;

            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "Ring buffer full");
                uart_flush_input(UART_PORT);
                xQueueReset(uart_queue);
                break;

            default:
                ESP_LOGI(TAG, "UART event type: %d", event.type);
                break;
        }
    }}
    free(data);
}
void send_serial(void *args){
    // read serial queue and relay its contents to serial tx
    serial_queue_t buffer;
    // my_serial_send_queue should not be NULL...
    while(xQueueReceive(my_serial_send_queue, &buffer, portMAX_DELAY) == pdTRUE){
        ESP_LOGI(TAG, "%.*s", buffer.size, buffer.buffer);
        uart_write_bytes(UART_PORT, buffer.buffer, buffer.size);
    }
}


void recv_espnow(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len){
    /** 
     * callback for espnow recieving event
    */
    ESP_LOGI(TAG, "receiving data");
    serial_queue_t buffer;
    memcpy(&(buffer.buffer), incomingData, len);
    buffer.size = len;
    // forward the data to serial tx
    xQueueSend(my_serial_send_queue, &buffer, ESPNOW_MAXDELAY);
}
void send_espnow(void *args){
    // read espnow queue and relay its contents to espnow
    espnow_queue_t buffer;
    // my_espnow_send_queue should not be NULL...
    while(xQueueReceive(my_espnow_send_queue, &buffer, portMAX_DELAY) == pdTRUE){
        esp_err_t result = esp_now_send(dest_addr, (uint8_t *) &(buffer.buffer), buffer.size);
        ESP_LOGI(TAG, "sending data whose result was: %s", esp_err_to_name(result));
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

    // --- REQUIRED: add peer ---
    esp_now_peer_info_t peer = {0};
    static const uint8_t dest_addr1[] = MY_DEST_MAC_ADDR1;
    static const uint8_t dest_addr2[] = MY_DEST_MAC_ADDR2;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); 
    if(mac[0] == dest_addr1[0]) {
        memcpy(dest_addr, dest_addr2, 6);
    }else if(mac[0] == dest_addr2[0]){
        memcpy(dest_addr, dest_addr1, 6);
    } 
    memcpy(peer.peer_addr, dest_addr, 6);
    peer.channel = 0;          // 0 = current WiFi channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    /*
    esp_err_t add_status = esp_now_add_peer(&peer);
    ESP_LOGI(TAG, "add_peer: %s", esp_err_to_name(add_status));
    */

    // xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    my_espnow_send_queue = xQueueCreate(6, sizeof(espnow_queue_t));
    my_serial_send_queue = xQueueCreate(6, sizeof(serial_queue_t));
    
}

void app_main(void)
{
    my_setup();
    serial_setup();
    // xTaskCreate(my_esp_now_info, "my_esp_now_info", 2048, NULL, 4, NULL);

    xTaskCreate(send_espnow, "send_espnow", 2048, NULL, 4, NULL);
    esp_now_register_recv_cb(recv_espnow);

    xTaskCreate(send_serial, "send_serial", 1024, NULL, 4, NULL);
    xTaskCreate(recv_serial, "uart_event_task", 4096, NULL, 12, NULL);
}
