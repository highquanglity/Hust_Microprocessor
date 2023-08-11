#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sleep.h"
#include "driver/i2c.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "pms7003.h"
#include "i2c-lcd.h"

#define PMS_UART_NUM UART_NUM_2
#define PMS_TXD_PIN (GPIO_NUM_17)
#define PMS_RXD_PIN (GPIO_NUM_16)

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_FREQ_HZ 1000000

#define LCD_I2C_ADDR 0x27
#define LCD_DATA_PIN I2C_MASTER_SDA_IO
#define LCD_CLOCK_PIN I2C_MASTER_SCL_IO
#define LCD_COLS 16
#define LCD_ROWS 2

#define WEB_SERVER "api.thingspeak.com"
#define WEB_PORT "80"
#define THINGSPEAK_API_KEY "X6MYOELSXKZIGT15"

static const char *TAG = "example";

static const char *REQUEST_TEMPLATE = "GET /update?api_key=%s&field1=%d&field2=%d&field3=%d HTTP/1.0\r\n"
                                      "Host: "WEB_SERVER"\r\n"
                                      "User-Agent: esp-idf/1.0 esp32\r\n"
                                      "\r\n";

static void send_to_thingspeak(uint32_t pm1_0, uint32_t pm2_5, uint32_t pm10)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s;
    char request[256];

    int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
    if(err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        return;
    }

    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if(s < 0) {
        ESP_LOGE(TAG, "Failed to allocate socket.");
        freeaddrinfo(res);
        return;
    }

    if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "Socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        return;
    }

    freeaddrinfo(res);

    snprintf(request, sizeof(request), REQUEST_TEMPLATE, THINGSPEAK_API_KEY, pm1_0, pm2_5, pm10);

    if (write(s, request, strlen(request)) < 0) {
        ESP_LOGE(TAG, "Socket send failed");
        close(s);
        return;
    }

    ESP_LOGI(TAG, "Data sent to Thingspeak");

    close(s);
}
static void lcd_init_cus(char line1[], char line2[]){
    lcd_put_cur(0, 0);
    lcd_send_string(line1);
    lcd_put_cur(1, 0);
    lcd_send_string(line2);
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Khởi tạo I2C cho LCD
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
.master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
      // Khởi tạo LCD
    lcd_init();
    lcd_clear();


       // Cấu hình UART
    uart_config_t uart_config = UART_CONFIG_DEFAULT();
    uart_config.baud_rate = 9600; // Tốc độ baud thông thường cho PMS7003

    // Khởi tạo UART với cấu hình đã cho
    if (pms7003_initUart(&uart_config) != ESP_OK) {
        ESP_LOGE(__func__, "Không khởi tạo được UART cho PMS7003");
        return;
    }

    // Đặt chế độ hoạt động
    if (pms7003_activeMode() != ESP_OK) {
        ESP_LOGE(__func__, "Không thiết lập được chế độ hoạt động cho PMS7003");
        return;
    }

    ESP_ERROR_CHECK(example_connect());

    // Initialize PMS7003 sensor here

    uint32_t pm1_0, pm2_5, pm10;

      while (1) {
        if (pms7003_readData(indoor, &pm1_0, &pm2_5, &pm10) == ESP_OK) {
            ESP_LOGI(TAG, "PM1.0: %d ug/m3, PM2.5: %d ug/m3, PM10: %d ug/m3", pm1_0, pm2_5, pm10);   
            char line1[17], line2[17];
            snprintf(line1, sizeof(line1), "PM1.0: %d ug/m3", pm1_0);
            snprintf(line2, sizeof(line2), "PM2.5: %d ug/m3", pm2_5);
            lcd_init_cus(line1, line2);
            send_to_thingspeak(pm1_0, pm2_5, pm10);
             // Chế độ sleep trong 10 giây (10 giây = 10000 ms)
            ESP_LOGI(TAG, "Entering deep sleep for 10 seconds...");
            esp_deep_sleep(10000000);
            // ESP_LOGI(TAG, "Entering light sleep for 10 seconds...");
           
            // esp_sleep_enable_timer_wakeup(10000000);
            // esp_light_sleep_start();
            
        } else {
            ESP_LOGE(TAG, "Không đọc được dữ liệu từ PMS7003");
        }
        //vTaskDelay(3000 / portTICK_PERIOD_MS);
         
    }
}
