// Sketch Arduino pour ESP32: Snake pilote par socket TCP Wi-Fi.
#include <Arduino.h>
#include <cctype>
#include <cstring>
#include <WiFi.h>
#include <Wire.h>

// Mets ici ton Wi-Fi maison. Si tu laisses "TON_WIFI", l'ESP32 cree son propre
// point d'acces "ESP32-Snake" et ton ordinateur devra s'y connecter.
const char *WIFI_SSID = "decode-etudiants";
const char *WIFI_PASSWORD = "learnByDoing25!";

const uint16_t SERVER_PORT = 4242;
const int BOARD_W = 20;
const int BOARD_H = 12;
const int MAX_SNAKE = BOARD_W * BOARD_H;
const uint32_t TICK_MS = 250;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
const uint32_t STATUS_PRINT_MS = 3000;

// Ecran OLED I2C SSD1306 128x64.
// Broches de ton OLED: SDA sur GPIO17, SCL sur GPIO18.
const int OLED_SDA = 17;
const int OLED_SCL = 18;
const int OLED_W = 128;
const int OLED_H = 64;
const int OLED_BUFFER_SIZE = OLED_W * OLED_H / 8;
const int OLED_CELL = 5;
const int OLED_BOARD_X = (OLED_W - BOARD_W * OLED_CELL) / 2;
const int OLED_BOARD_Y = (OLED_H - BOARD_H * OLED_CELL) / 2;

struct Point {
  int x;
  int y;
};

enum Direction {
  DIR_UP,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT
};

WiFiServer server(SERVER_PORT);
WiFiClient client;

Point snake[MAX_SNAKE];
Point food;
int snakeLength = 0;
int score = 0;
Direction direction = DIR_RIGHT;
Direction nextDirection = DIR_RIGHT;
bool gameOver = false;
bool waitingForDirection = true;
bool moveRequested = false;
uint32_t lastTick = 0;
uint32_t lastStateSent = 0;
uint32_t lastStatusPrinted = 0;
bool oledReady = false;
uint8_t oledAddr = 0x3C;
uint8_t oledBuffer[OLED_BUFFER_SIZE];

bool usingAccessPoint() {
  return strcmp(WIFI_SSID, "TON_WIFI") == 0 || strlen(WIFI_SSID) == 0;
}

void oledCommand(uint8_t command) {
  Wire.beginTransmission(oledAddr);
  Wire.write(0x00);
  Wire.write(command);
  Wire.endTransmission();
}

void oledClearBuffer() {
  memset(oledBuffer, 0, sizeof(oledBuffer));
}

void oledDisplay() {
  if (!oledReady) {
    return;
  }

  oledCommand(0x21); // column address
  oledCommand(0);
  oledCommand(OLED_W - 1);
  oledCommand(0x22); // page address
  oledCommand(0);
  oledCommand((OLED_H / 8) - 1);

  for (int i = 0; i < OLED_BUFFER_SIZE; i += 16) {
    Wire.beginTransmission(oledAddr);
    Wire.write(0x40);
    for (int j = 0; j < 16 && i + j < OLED_BUFFER_SIZE; j++) {
      Wire.write(oledBuffer[i + j]);
    }
    Wire.endTransmission();
  }
}

void oledPixel(int x, int y, bool on) {
  if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) {
    return;
  }

  int index = x + (y / 8) * OLED_W;
  uint8_t mask = 1 << (y & 7);
  if (on) {
    oledBuffer[index] |= mask;
  } else {
    oledBuffer[index] &= ~mask;
  }
}

void oledFillRect(int x, int y, int w, int h, bool on) {
  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      oledPixel(px, py, on);
    }
  }
}

void oledDrawRect(int x, int y, int w, int h) {
  for (int px = x; px < x + w; px++) {
    oledPixel(px, y, true);
    oledPixel(px, y + h - 1, true);
  }
  for (int py = y; py < y + h; py++) {
    oledPixel(x, py, true);
    oledPixel(x + w - 1, py, true);
  }
}

bool findOledAddress() {
  const uint8_t addresses[] = {0x3C, 0x3D};
  for (uint8_t i = 0; i < sizeof(addresses); i++) {
    Wire.beginTransmission(addresses[i]);
    if (Wire.endTransmission() == 0) {
      oledAddr = addresses[i];
      return true;
    }
  }
  return false;
}

bool tryOledPins(int sda, int scl) {
  if (sda >= 0 && scl >= 0) {
    Serial.print("Test OLED: SDA=GPIO");
    Serial.print(sda);
    Serial.print(" SCL=GPIO");
    Serial.println(scl);
  } else {
    Serial.println("Test OLED: broches I2C par defaut");
  }

  Wire.end();
  if (sda >= 0 && scl >= 0) {
    Wire.begin(sda, scl);
  } else {
    Wire.begin();
  }

  delay(50);
  if (!findOledAddress()) {
    Serial.println("  -> rien detecte");
    return false;
  }

  Serial.print("OLED detecte sur adresse 0x");
  Serial.print(oledAddr, HEX);
  if (sda >= 0 && scl >= 0) {
    Serial.print(" avec SDA=GPIO");
    Serial.print(sda);
    Serial.print(" SCL=GPIO");
    Serial.println(scl);
  } else {
    Serial.println(" avec les broches I2C par defaut");
  }
  return true;
}

void setupOled() {
  bool found = false;

  if (OLED_SDA >= 0 && OLED_SCL >= 0) {
    found = tryOledPins(OLED_SDA, OLED_SCL);
  } else {
    found = tryOledPins(-1, -1);

    if (!found) {
      const int pinPairs[][2] = {
          {8, 9},   // frequent sur ESP32-S3
          {9, 8},   // SDA/SCL inverses
          {21, 22}, // frequent sur ESP32 classique
          {22, 21},
          {4, 5},
          {5, 4},
          {5, 6},
          {6, 5},
          {6, 7},
          {7, 6},
          {10, 11},
          {11, 10},
          {17, 18},
          {18, 17},
          {1, 2},
          {2, 1},
          {3, 4},
          {4, 3},
          {15, 16},
          {16, 15},
      };

      for (uint8_t i = 0; i < sizeof(pinPairs) / sizeof(pinPairs[0]); i++) {
        if (tryOledPins(pinPairs[i][0], pinPairs[i][1])) {
          found = true;
          break;
        }
      }
    }
  }

  if (!found) {
    Serial.println("OLED non detecte.");
    Serial.println("Verifie le cablage: VCC->3V3, GND->GND, SDA->GPIO SDA, SCL->GPIO SCL.");
    Serial.println("Dis-moi sur quels GPIO sont branches SDA et SCL pour les fixer dans le code.");
    return;
  }

  oledCommand(0xAE); // display off
  oledCommand(0xD5);
  oledCommand(0x80);
  oledCommand(0xA8);
  oledCommand(0x3F);
  oledCommand(0xD3);
  oledCommand(0x00);
  oledCommand(0x40);
  oledCommand(0x8D);
  oledCommand(0x14);
  oledCommand(0x20);
  oledCommand(0x00);
  oledCommand(0xA1);
  oledCommand(0xC8);
  oledCommand(0xDA);
  oledCommand(0x12);
  oledCommand(0x81);
  oledCommand(0xCF);
  oledCommand(0xD9);
  oledCommand(0xF1);
  oledCommand(0xDB);
  oledCommand(0x40);
  oledCommand(0xA4);
  oledCommand(0xA6);
  oledCommand(0x2E);
  oledCommand(0xAF); // display on

  oledReady = true;
  oledClearBuffer();
  oledDisplay();
  Serial.println("OLED detecte et initialise.");
}

void drawGameOnOled() {
  if (!oledReady) {
    return;
  }

  oledClearBuffer();
  oledDrawRect(OLED_BOARD_X - 1, OLED_BOARD_Y - 1, BOARD_W * OLED_CELL + 2,
               BOARD_H * OLED_CELL + 2);

  if (food.x >= 0 && food.y >= 0) {
    int fx = OLED_BOARD_X + food.x * OLED_CELL;
    int fy = OLED_BOARD_Y + food.y * OLED_CELL;
    oledFillRect(fx + 1, fy + 1, OLED_CELL - 2, OLED_CELL - 2, true);
  }

  for (int i = snakeLength - 1; i >= 0; i--) {
    int sx = OLED_BOARD_X + snake[i].x * OLED_CELL;
    int sy = OLED_BOARD_Y + snake[i].y * OLED_CELL;
    int inset = i == 0 ? 0 : 1;
    oledFillRect(sx + inset, sy + inset, OLED_CELL - inset, OLED_CELL - inset, true);
  }

  if (gameOver) {
    oledFillRect(20, 29, 88, 2, true);
    oledFillRect(20, 34, 88, 2, true);
  }

  oledDisplay();
}

void printNetworkStatus() {
  if (WiFi.getMode() == WIFI_AP) {
    Serial.println();
    Serial.println("=== Snake ESP32 ===");
    Serial.println("Mode Wi-Fi: point d'acces");
    Serial.println("Connecte ton ordinateur au Wi-Fi: ESP32-Snake");
    Serial.print("Lance le client avec: python tools/snake_client.py ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Port socket: ");
    Serial.println(SERVER_PORT);
    Serial.println("===================");
    return;
  }

  Serial.println();
  Serial.println("=== Snake ESP32 ===");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Mode Wi-Fi: connecte au reseau configure");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Lance le client avec: python tools/snake_client.py ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Mode Wi-Fi: pas encore connecte");
  }
  Serial.print("Port socket: ");
  Serial.println(SERVER_PORT);
  Serial.println("===================");
}

bool samePoint(Point a, Point b) {
  return a.x == b.x && a.y == b.y;
}

bool snakeOccupies(Point p) {
  for (int i = 0; i < snakeLength; i++) {
    if (samePoint(snake[i], p)) {
      return true;
    }
  }
  return false;
}

void placeFood() {
  if (snakeLength >= MAX_SNAKE) {
    food = {-1, -1};
    return;
  }

  do {
    food.x = random(0, BOARD_W);
    food.y = random(0, BOARD_H);
  } while (snakeOccupies(food));
}

void resetGame() {
  snakeLength = 3;
  snake[0] = {BOARD_W / 2, BOARD_H / 2};
  snake[1] = {BOARD_W / 2 - 1, BOARD_H / 2};
  snake[2] = {BOARD_W / 2 - 2, BOARD_H / 2};
  direction = DIR_RIGHT;
  nextDirection = DIR_RIGHT;
  score = 0;
  gameOver = false;
  waitingForDirection = true;
  moveRequested = false;
  placeFood();
  lastTick = millis();
  drawGameOnOled();
  sendState();
}

bool isReverse(Direction current, Direction next) {
  return (current == DIR_UP && next == DIR_DOWN) ||
         (current == DIR_DOWN && next == DIR_UP) ||
         (current == DIR_LEFT && next == DIR_RIGHT) ||
         (current == DIR_RIGHT && next == DIR_LEFT);
}

void setNextDirection(Direction requested) {
  if (waitingForDirection || !isReverse(direction, requested)) {
    nextDirection = requested;
    moveRequested = true;
    waitingForDirection = false;
  }
}

void handleCommand(char c) {
  switch (c) {
  case 'U':
    setNextDirection(DIR_UP);
    return;
  case 'D':
    setNextDirection(DIR_DOWN);
    return;
  case 'L':
    setNextDirection(DIR_LEFT);
    return;
  case 'R':
    setNextDirection(DIR_RIGHT);
    return;
  case 'N':
    resetGame();
    return;
  default:
    break;
  }

  switch (tolower(static_cast<unsigned char>(c))) {
  case 'z':
  case 'w':
    setNextDirection(DIR_UP);
    break;
  case 's':
    setNextDirection(DIR_DOWN);
    break;
  case 'a':
  case 'q':
    setNextDirection(DIR_LEFT);
    break;
  case 'd':
    setNextDirection(DIR_RIGHT);
    break;
  case 'r':
    resetGame();
    break;
  default:
    break;
  }
}

bool updateGame() {
  if (gameOver) {
    return false;
  }

  if (!moveRequested) {
    return false;
  }

  moveRequested = false;
  direction = nextDirection;
  Point head = snake[0];

  switch (direction) {
  case DIR_UP:
    head.y--;
    break;
  case DIR_DOWN:
    head.y++;
    break;
  case DIR_LEFT:
    head.x--;
    break;
  case DIR_RIGHT:
    head.x++;
    break;
  }

  if (head.x < 0 || head.x >= BOARD_W || head.y < 0 || head.y >= BOARD_H) {
    gameOver = true;
    return true;
  }

  bool eatsFood = samePoint(head, food);
  int limit = eatsFood ? snakeLength : snakeLength - 1;
  for (int i = 0; i < limit; i++) {
    if (samePoint(head, snake[i])) {
      gameOver = true;
      return true;
    }
  }

  if (eatsFood && snakeLength < MAX_SNAKE) {
    snakeLength++;
    score++;
  }

  for (int i = snakeLength - 1; i > 0; i--) {
    snake[i] = snake[i - 1];
  }
  snake[0] = head;

  if (eatsFood) {
    placeFood();
  }

  return true;
}

void sendState() {
  if (!client || !client.connected()) {
    return;
  }

  client.print("{\"w\":");
  client.print(BOARD_W);
  client.print(",\"h\":");
  client.print(BOARD_H);
  client.print(",\"score\":");
  client.print(score);
  client.print(",\"over\":");
  client.print(gameOver ? "true" : "false");
  client.print(",\"food\":[");
  client.print(food.x);
  client.print(",");
  client.print(food.y);
  client.print("],\"snake\":[");

  for (int i = 0; i < snakeLength; i++) {
    if (i > 0) {
      client.print(",");
    }
    client.print("[");
    client.print(snake[i].x);
    client.print(",");
    client.print(snake[i].y);
    client.print("]");
  }

  client.println("]}");
}

void readClientCommands() {
  if (!client || !client.connected()) {
    return;
  }

  while (client.available()) {
    char c = client.read();
    if (c != '\n' && c != '\r') {
      handleCommand(c);
    }
  }
}

void acceptClient() {
  if (client && client.connected()) {
    return;
  }

  WiFiClient newClient = server.available();
  if (!newClient) {
    return;
  }

  if (client) {
    client.stop();
  }

  client = newClient;
  client.setNoDelay(true);
  Serial.print("Client connecte: ");
  Serial.println(client.remoteIP());
  sendState();
}

void setupWifi() {
  if (usingAccessPoint()) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-Snake");
    printNetworkStatus();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connexion au Wi-Fi");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    printNetworkStatus();
    return;
  }

  Serial.println();
  Serial.println("Connexion Wi-Fi impossible, creation du point d'acces ESP32-Snake.");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Snake");
  printNetworkStatus();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  randomSeed(esp_random());
  resetGame();
  setupWifi();
  server.begin();

  Serial.print("Socket TCP ouverte sur le port ");
  Serial.println(SERVER_PORT);

  setupOled();
  drawGameOnOled();
}

void loop() {
  acceptClient();
  readClientCommands();

  uint32_t now = millis();
  if (now - lastStatusPrinted >= STATUS_PRINT_MS) {
    lastStatusPrinted = now;
    printNetworkStatus();
  }

  if (moveRequested && now - lastTick >= 50) {
    lastTick = now;
    if (updateGame()) {
      drawGameOnOled();
      sendState();
      lastStateSent = now;
    }
  } else if (client && client.connected() && now - lastStateSent >= 1000) {
    sendState();
    lastStateSent = now;
  }
}
