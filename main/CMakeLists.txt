idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES spi_flash
                       PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash mqtt
                       INCLUDE_DIRS "")
