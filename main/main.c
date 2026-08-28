#include <inttypes.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <si7021.h>
#include <string.h>
#include <math.h>

// wifi
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

// mqtt
#include <mqtt_client.h>

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

typedef struct {
    float temperature;
    float humidity;
} si7021_data_t;

static si7021_data_t g_si7021_data= {
    .temperature = NAN,
    .humidity = NAN
};


static SemaphoreHandle_t g_si7021_mutex;

volatile bool wifi_connected = false; // WiFi connecté ?
volatile bool mqtt_connected = false; // connexion au broker ?

static esp_mqtt_client_handle_t client = NULL;

static QueueHandle_t mqtt_queue = NULL; // queue pour les messages MQTT


void si7021_task(void *pvParameters)
{
    i2c_dev_t dev;
    memset(&dev, 0, sizeof(i2c_dev_t));

    ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));

#ifdef CONFIG_EXAMPLE_CHIP_TYPE_SI70xx
    uint64_t serial;
    si7021_device_id_t id;

    ESP_ERROR_CHECK(si7021_get_serial(&dev, &serial, false));
    ESP_ERROR_CHECK(si7021_get_device_id(&dev, &id));

    printf("Device: ");
    switch (id)
    {
        case SI_MODEL_SI7013:
            printf("Si7013");
            break;
        case SI_MODEL_SI7020:
            printf("Si7020");
            break;
        case SI_MODEL_SI7021:
            printf("Si7021");
            break;
        case SI_MODEL_SAMPLE:
            printf("Engineering sample");
            break;
        default:
            printf("Unknown");
    }
    printf("\nSerial number: 0x%08" PRIx32 "%08" PRIx32 "\n", (uint32_t)(serial >> 32), (uint32_t)serial);
#endif

    float valT, valH;
    esp_err_t resT, resH;

    /* wait for the device to boot. HTU21D sometimes fails to return data
     * at the initial reading. the datasheet does not say anything about
     * startup sequence. */
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1)
    {
        resT = si7021_measure_temperature(&dev, &valT);

        /*
        if (resT != ESP_OK)
            printf("Could not measure temperature: %d (%s)\n", resT, esp_err_to_name(resT));
        else
            printf("Temperature: %.2f\n", valT);
        */

        resH = si7021_measure_humidity(&dev, &valH);
        /*
        if (resH != ESP_OK)
            printf("Could not measure humidity: %d (%s)\n", resH, esp_err_to_name(resH));
        else
            printf("Humidity: %.2f\n", valH);
        */

        // Mise à jour des valeurs globales
        xSemaphoreTake(g_si7021_mutex, portMAX_DELAY);
        if (resT==ESP_OK && resH==ESP_OK) {
            g_si7021_data.temperature = valT;
            g_si7021_data.humidity = valH;               
        }

        xSemaphoreGive(g_si7021_mutex);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool si7021_get_data(float *temperature, float *humidity)
{
    if (xSemaphoreTake(g_si7021_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        *temperature = g_si7021_data.temperature;
        *humidity = g_si7021_data.humidity;
        xSemaphoreGive(g_si7021_mutex);
        return true;
    }
    return false;
}


void queue_writer_task(void *pvParameters)
{
    float lastT = -1000.0f;   // valeurs impossibles pour forcer un premier affichage
    float lastH = -1000.0f;
    float t, h;

    const float TEMP_DELTA = 0.5f;   // seuil de variation température
    const float HUM_DELTA  = 1.0f;   // seuil de variation humidité

    const uint32_t PERIODIC_DELAY_MS = 12 * 60 * 1000; // 12 minutes, 5x par heure
    uint32_t lastWriteTime = xTaskGetTickCount();

    TickType_t lastWake = xTaskGetTickCount();

    while (1)
    {
        if (si7021_get_data(&t, &h))
        {
            // Ignorer les valeurs tant qu'elles ne sont pas valides
            if (isnan(t) || isnan(h)) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;   // on attend la prochaine mesure
            }

            bool tempChanged = fabsf(t - lastT) >= TEMP_DELTA;
            bool humChanged  = fabsf(h - lastH) >= HUM_DELTA;

            uint32_t now = xTaskGetTickCount();
            bool periodicWrite = (now - lastWriteTime) >= pdMS_TO_TICKS(PERIODIC_DELAY_MS);

            if (tempChanged || humChanged || periodicWrite)
            {
                si7021_data_t msg = {
                        .temperature = t,
                        .humidity = h
                    };

    
                printf("Envoi dans la queue -> Temperature: %.2f °C, Humidity: %.2f %%\n", t, h);
                xQueueSend(mqtt_queue, &msg, 0);   // envoi immédiat, non bloquant

                lastT = t;
                lastH = h;
                lastWriteTime = now;
            }
        }
       
        //vTaskDelay(pdMS_TO_TICKS(5000)); // vérification toutes les 5 secondes
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5000));
    }
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        printf("WiFi disconnected, retrying...\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        printf("WiFi connected, IP obtained.\n");
    }
}


static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register( WIFI_EVENT,
                                                WIFI_EVENT_STA_DISCONNECTED,
                                                &wifi_event_handler,
                                                NULL));

    // Récupérer l'interface réseau
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    // Définir le hostname
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, "meteo-esp32c6"));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_connect());
}


void wifi_task(void *pvParameters)
{
    wifi_init_sta();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            printf("MQTT connected\n");
            break;

        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            printf("MQTT disconnected, retrying...\n");
            break;

        default:
            break;
    }
}


static void mqtt_task(void *pvParameters)
{
    // --- config MQTT ---
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI,
        .broker.address.port = CONFIG_MQTT_BROKER_PORT,
        .credentials.username = CONFIG_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_PASSWORD,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);

    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(client,
                                       ESP_EVENT_ANY_ID,
                                       mqtt_event_handler,
                                       NULL)
    );

    esp_mqtt_client_start(client);

    si7021_data_t msg;

    while (1)
    {
        // attendre que MQTT soit connecté
        if (!mqtt_connected) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // attendre un message venant de print_task
        if (xQueueReceive(mqtt_queue, &msg, portMAX_DELAY))
        {         
            char payload[64];
            snprintf(payload, sizeof(payload),
                     "{\"temperature\":%.2f,\"humidity\":%.2f}",
                     msg.temperature, msg.humidity);

            esp_mqtt_client_publish(client, CONFIG_MQTT_PUB_TOPIC, payload, 0, CONFIG_MQTT_QOS, 0);

            printf("MQTT published: %s\n", payload);
     
        }
    }
}




void app_main()
{
    ESP_ERROR_CHECK(i2cdev_init());

    g_si7021_mutex = xSemaphoreCreateMutex();

    mqtt_queue = xQueueCreate(10, sizeof(si7021_data_t));
    assert(mqtt_queue != NULL);


    xTaskCreatePinnedToCore(si7021_task, "si7021", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, APP_CPU_NUM);

    xTaskCreatePinnedToCore(queue_writer_task, "queue_writer", configMINIMAL_STACK_SIZE * 4, NULL, 5, NULL, APP_CPU_NUM);

    xTaskCreatePinnedToCore(wifi_task, "wifi", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, APP_CPU_NUM);
    xTaskCreatePinnedToCore(mqtt_task, "mqtt", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, APP_CPU_NUM);

}