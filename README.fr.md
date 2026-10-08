# PN532 ESP32-C3 Terminal

*[English version](README.md)*

Une borne NFC passive pilotée depuis le réseau local : un ESP32-C3 Super Mini, un lecteur
PN532 et un petit écran OLED. Les applications créent des opérations (lire, écrire,
effacer) via une API REST + WebSocket ; la borne demande un badge sur son écran et
exécute l'opération sur le premier badge présenté. Une app web intégrée, servie par la
borne, utilise la même API.

## Fonctions

- **Lecture** des NTAG213/215/216 et MIFARE Ultralight (contenu complet + NDEF décodé),
  et des MIFARE Classic 1K/4K/Mini (secteurs lisibles avec la clé A fournie)
- **Écriture** de messages NDEF (plusieurs liens / textes), de blocs ou pages bruts, et
  **effacement** qui remet le badge dans son état d'usine
- **Événements en direct** par WebSocket : badge posé / retiré, avancement des opérations
- **Appairage** par code à 6 chiffres affiché sur l'écran (preuve d'accès physique) ;
  jetons stockés hachés
- **App web intégrée** (français / anglais)
- **Mises à jour sans fil**, réseau de configuration avec mot de passe propre à chaque
  borne, réinitialisation par le bouton BOOT
- **Découverte** en mDNS (`_rfid-terminal._tcp`)

**Sûr par conception :** le bloc 0, les blocs de clés et les pages système ne sont jamais
écrits, une erreur ne peut donc pas rendre un badge inutilisable. Le firmware ne clone
pas d'UID et ne cherche pas de clés inconnues.

## Matériel et câblage

| ESP32-C3 | PN532 (connecteur 4 broches) | OLED |
|----------|------------------------------|------|
| 3V3      | VCC                          | VCC  |
| GND      | GND                          | GND  |
| GPIO 2   | SDA                          | SDA  |
| GPIO 1   | SCL                          | SCL  |
| GPIO 3   | IRQ (connecteur 8 broches)   |      |

- PN532 en mode **I2C** : interrupteur 1 sur ON, 2 sur OFF, **puis couper
  l'alimentation** (le mode n'est lu qu'à la mise sous tension).
- Tout en **3V3** : les résistances de rappel du bus restent à 3,3 V, comme l'exige
  l'ESP32-C3.
- Écran 1,3" (SH1106) : compiler l'environnement `c3-sh1106`.

## Installation

Depuis une [release](https://github.com/timiliris/pn532-esp32c3-terminal/releases) :
flasher `factory-c3.bin` à l'adresse `0x0`. Depuis les sources :

```bash
pio run -e c3 -t upload && pio device monitor
```

## Premier démarrage

1. L'écran affiche un réseau `RFID-XXXX` et son mot de passe. S'y connecter puis ouvrir
   `http://192.168.4.1`.
2. **Afficher un code sur la borne**, puis saisir le code de l'écran.
3. **Réglages > Wi-Fi** : choisir son réseau. La borne redémarre, ensuite
   `http://rfid.local`.

La langue de l'écran se règle dans **Réglages > Borne**. Réinitialisation : maintenir le
bouton BOOT 5 secondes.

## API

Spécification complète : [`docs/openapi.yaml`](docs/openapi.yaml). Exemples dans le
[README anglais](README.md#api).

## Licence

[MIT](LICENSE)
