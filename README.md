# Nœud météo MQTT avec ESP32‑C6 et capteur SI7021
Ce projet met en œuvre un nœud météo basé sur un ESP32‑C6 et un capteur SI7021, avec publication des mesures en MQTT.
Les données peuvent ensuite être intégrées dans un tableau de bord (Grafana, MQTT Explorer, etc.).

## Version de l’ESP‑IDF
Ce projet utilise ESP‑IDF v5.5.
Il est recommandé d’utiliser cette version ou une version ultérieure compatible, afin d’éviter les différences d’API (Wi‑Fi, MQTT, esp_netif, FreeRTOS, etc.).

## Fonctionnement général
Le programme repose sur plusieurs tâches FreeRTOS :

- **si7021_task** 
Lecture périodique du capteur SI7021 via I²C.

- **queue_writer_task**  
Détection des variations significatives (température / humidité) et envoi périodique d’un message dans une queue.

- **mqtt_task**  
Publication des mesures en MQTT dès qu’un message est disponible dans la queue.

- **wifi_task**  
Connexion au réseau Wi‑Fi en mode station.

Les tâches communiquent via :

- un mutex pour protéger les données du capteur ;

- une queue pour transmettre les mesures à publier.

## Configuration
Les paramètres suivants sont configurables via `menuconfig` :

- SSID / mot de passe Wi‑Fi

- URI du broker MQTT

- Port MQTT

- Topic de publication

- QoS

- Identifiants MQTT (optionnels)

Le fichier `Kconfig.projbuild` contient les entrées correspondantes.

## Compilation et flash

```
idf.py set-target esp32c6
idf.py build
idf.py flash
idf.py monitor
```
## Format des messages MQTT
Les mesures sont publiées sous forme de JSON :

```json
{"temperature": 23.5, "humidity": 48.2}
```

Le topic est défini dans `menuconfig`.

## Matériel utilisé
- ESP32‑C6

- Capteur SI7021 (I²C)

- Broker MQTT (Mosquitto ou autre)

**Notes**
Ce projet sert de prototype pour un petit nœud météo local.
Il peut être étendu facilement (timestamp, retain, NVS, interface Web, BLE, autres capteurs…).