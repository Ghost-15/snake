# Snake ESP32 via Wi-Fi

Ce projet fait tourner la logique du Snake sur un ESP32. L'ESP32 affiche le jeu sur un ecran OLED I2C SSD1306 128x64, ouvre une socket TCP en Wi-Fi, et ton ordinateur sert de manette avec un client Python.

## 1. Wi-Fi

Si `WIFI_SSID` vaut `TON_WIFI`, l'ESP32 cree son propre point d'acces :

- Wi-Fi : `ESP32-Snake`
- IP de l'ESP32 : `192.168.4.1`
- Port socket : `4242`

Si tu veux utiliser ton Wi-Fi maison, modifie dans [snake.ino](snake.ino) :

```cpp
const char *WIFI_SSID = "TON_WIFI";
const char *WIFI_PASSWORD = "TON_MOT_DE_PASSE";
```

Quand un vrai Wi-Fi est configure dans `snake.ino`, ton ordinateur doit etre connecte au meme reseau Wi-Fi que l'ESP32.

## 2. OLED

Le code utilise un OLED I2C SSD1306 128x64, adresse `0x3C` ou `0x3D`.

Si l'ecran reste noir, verifie dans [snake.ino](snake.ino) :

```cpp
const int OLED_SDA = 17;
const int OLED_SCL = 18;
```

Le montage actuel utilise `SDA -> GPIO17` et `SCL -> GPIO18`.

Cablage habituel d'un OLED I2C 4 broches :

- `VCC` vers `3V3`
- `GND` vers `GND`
- `SDA` vers une broche GPIO SDA
- `SCL` vers une broche GPIO SCL

## 3. Envoyer le programme sur l'ESP32 avec Arduino IDE

1. Branche l'ESP32 a ton ordinateur avec le cable USB-C.
2. Ouvre l'application Arduino IDE.
3. Ouvre le fichier `snake.ino`.
4. Selectionne ta carte ESP32 dans `Outils > Type de carte`.
5. Selectionne le port USB dans `Outils > Port`.
6. Clique sur `Televerser`.

Si la carte ESP32 n'apparait pas dans Arduino IDE, installe le support de cartes `esp32` d'Espressif depuis le gestionnaire de cartes.

Ouvre ensuite le moniteur serie a `115200 bauds` pour voir l'adresse IP de l'ESP32. Le programme reaffiche l'adresse IP toutes les 3 secondes.

## 4. Jouer depuis l'ordinateur

Installe Python 3 si besoin, puis lance :

```powershell
python tools/snake_client.py 192.168.4.1
```

Si tu as configure ton Wi-Fi maison, remplace `192.168.4.1` par l'IP affichee dans le moniteur serie.

Commandes :

- Fleches du clavier
- `ZQSD` ou `WASD`
- `r` pour recommencer
- `x` ou `Ctrl+C` pour quitter

Le snake ne bouge pas tout seul : chaque touche de direction le fait avancer d'une case.

## Protocole socket

Le client envoie une commande texte terminee par `\n` :

- `U` : haut
- `D` : bas
- `L` : gauche
- `R` : droite
- `N` : nouvelle partie

L'ESP32 renvoie l'etat du jeu en JSON, une ligne par mise a jour.
