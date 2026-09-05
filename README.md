# Nœud météo MQTT avec ESP32‑C6 et capteur SI7021
Ce projet met en œuvre un nœud météo basé sur un ESP32‑C6 et un capteur SI7021, avec publication des mesures horodatées en MQTT.
Les données peuvent ensuite être intégrées dans un tableau de bord (Grafana, MQTT Explorer, etc.).
Ce projet sert de prototype pour un petit nœud météo local, et à titre pédagogique pour illustrer l’usage d’un ESP32‑C6, d’un capteur I²C, de FreeRTOS, SNTP, et de MQTT.
Il peut être étendu facilement (NVS, interface Web, BLE, autres capteurs…).

## Version de l’ESP‑IDF
Ce projet utilise ESP‑IDF v5.5.
Il est recommandé d’utiliser cette version ou une version ultérieure compatible, afin d’éviter les différences d’API (Wi‑Fi, MQTT, esp_netif, FreeRTOS, etc.).

## Fonctionnement général
Le programme repose sur plusieurs tâches FreeRTOS :

- **si7021_task**
Lecture périodique du capteur SI7021 via I²C (toutes les 10s par défaut).

- **decision_task**  
Décision de publication.
Détection des variations significatives (température / humidité).
Publication si seuil de température ou humidité franchi, envoi périodique sinon (toutes les 10min par défaut).

- **mqtt_task**  
Publication des mesures en MQTT dès qu’un message est disponible dans la queue. La publication est horodatée (RTC et synchro via SNTP).

- **wifi_task**  
Connexion au réseau Wi‑Fi en mode station. Gestion des évènements réseau.

Les tâches communiquent via :

- un mutex pour protéger les données du capteur ;

- une queue pour transmettre les mesures à publier.

## Configuration
Les paramètres suivants sont configurables via `idf.py menuconfig` :
- Paramètres WiFi
  - SSID / mot de passe Wi‑Fi
  - URI du broker MQTT

- Paramètres MQTT
  - Port MQTT
  - Topic de publication
  - QoS
  - Identifiants MQTT (optionnels)

- Paramètres de décision de publication
  - Intervalle entre deux acquisitions du capteur SI7021 (secondes)
  - Seuil de variation de température (dixièmes de °C)
  - Seuil variation humidité (dixièmes de %)
  - Délai minimal entre deux publications MQTT (secondes)
  - Intervalle de publication périodique (minutes)

Le fichier `Kconfig.projbuild` contient les entrées correspondantes.



## Quick Start

```
git clone https://github.com/fleb72/esp32c6-si7021-mqtt-node
idf.py set-target esp32c6
idf.py menuconfig      # configurer WiFi + MQTT + paramètres de décision de publication
idf.py build
idf.py -p PORT flash   # PORT=/dev/ttyUSB0 ou PORT=COM3, etc.
idf.py -p PORT monitor
```

## Format des messages MQTT
Les mesures sont publiées sous forme de JSON :

```json
{ "temperature" : 23.57,
  "humidity" : 59.10,
  "timestamp" : "2026-08-30T19:12:18" }
```
Les données sont horodatées au moment de la publication MQTT (date/heure UTC) en se connectant à un serveur SNTP.
Le topic de publication est défini dans `menuconfig`.

## Exemple de log

```
...
WiFi connected, IP obtained.
I (5019) esp_netif_handlers: sta ip: 192.168.1.131, mask: 255.255.255.0, gw: 192.168.1.254
SNTP started.
Envoi dans la queue -> Temperature: 23.66 °C, Humidity: 59.13 %
MQTT connected
Envoi dans la queue -> Temperature: 26.53 °C, Humidity: 64.61 %
MQTT published at 2026-08-31T19:52:23: {"temperature":26.53,"humidity":64.61,"timestamp":"2026-08-31T19:52:23"}
Envoi dans la queue -> Temperature: 25.45 °C, Humidity: 68.37 %
MQTT published at 2026-08-31T19:52:28: {"temperature":25.45,"humidity":68.37,"timestamp":"2026-08-31T19:52:28"}
...
```


## Matériel utilisé
- ESP32‑C6

- Capteur SI7021 (I²C)

- Broker MQTT (Mosquitto ou autre)