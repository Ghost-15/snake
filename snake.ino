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
const int MAX_CLIENTS = 2;
const uint32_t TICK_MS = 180;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
const uint32_t STATUS_PRINT_MS = 3000;

// Ecran OLED I2C SSD1306 128x64.
// Broches de ton OLED: SDA sur GPIO17, SCL sur GPIO18.
const int OLED_SDA = 17;
const int OLED_SCL = 18;
const int OLED_W = 128;
const int OLED_H = 64;
const int OLED_BUFFER_SIZE = OLED_W * OLED_H / 8;
const int OLED_CELL = 4;
const int OLED_BOARD_X = (OLED_W - BOARD_W * OLED_CELL) / 2;
const int OLED_BOARD_Y = 13;

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

enum GameMode {
  MODE_SOLO,
  MODE_DUO
};

enum GameScreen {
  SCREEN_MENU,
  SCREEN_PLAYING,
  SCREEN_GAME_OVER
};

WiFiServer server(SERVER_PORT);
WiFiClient clients[MAX_CLIENTS];
char commandBuffers[MAX_CLIENTS][18];
uint8_t commandLengths[MAX_CLIENTS] = {0};

Point snake1[MAX_SNAKE];
Point snake2[MAX_SNAKE];
Point food;
int snake1Length = 0;
int snake2Length = 0;
int score1 = 0;
int score2 = 0;
int highScoreSolo = 0;
int highScoreDuo = 0;
Direction direction1 = DIR_RIGHT;
Direction nextDirection1 = DIR_RIGHT;
Direction direction2 = DIR_LEFT;
Direction nextDirection2 = DIR_LEFT;
GameMode gameMode = MODE_SOLO;
GameScreen screen = SCREEN_MENU;
bool alive1 = true;
bool alive2 = false;
uint32_t lastTick = 0;
uint32_t lastStateSent = 0;
uint32_t lastStatusPrinted = 0;
bool oledReady = false;
uint8_t oledAddr = 0x3C;
uint8_t oledBuffer[OLED_BUFFER_SIZE];

void broadcastState();

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

void glyphFor(char c, uint8_t glyph[5]) {
  memset(glyph, 0, 5);

  switch (toupper(static_cast<unsigned char>(c))) {
  case '0': {
    uint8_t g[] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    memcpy(glyph, g, 5);
    break;
  }
  case '1': {
    uint8_t g[] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    memcpy(glyph, g, 5);
    break;
  }
  case '2': {
    uint8_t g[] = {0x42, 0x61, 0x51, 0x49, 0x46};
    memcpy(glyph, g, 5);
    break;
  }
  case '3': {
    uint8_t g[] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    memcpy(glyph, g, 5);
    break;
  }
  case '4': {
    uint8_t g[] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    memcpy(glyph, g, 5);
    break;
  }
  case '5': {
    uint8_t g[] = {0x27, 0x45, 0x45, 0x45, 0x39};
    memcpy(glyph, g, 5);
    break;
  }
  case '6': {
    uint8_t g[] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    memcpy(glyph, g, 5);
    break;
  }
  case '7': {
    uint8_t g[] = {0x01, 0x71, 0x09, 0x05, 0x03};
    memcpy(glyph, g, 5);
    break;
  }
  case '8': {
    uint8_t g[] = {0x36, 0x49, 0x49, 0x49, 0x36};
    memcpy(glyph, g, 5);
    break;
  }
  case '9': {
    uint8_t g[] = {0x06, 0x49, 0x49, 0x29, 0x1E};
    memcpy(glyph, g, 5);
    break;
  }
  case 'A': {
    uint8_t g[] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    memcpy(glyph, g, 5);
    break;
  }
  case 'C': {
    uint8_t g[] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    memcpy(glyph, g, 5);
    break;
  }
  case 'D': {
    uint8_t g[] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    memcpy(glyph, g, 5);
    break;
  }
  case 'E': {
    uint8_t g[] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    memcpy(glyph, g, 5);
    break;
  }
  case 'G': {
    uint8_t g[] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    memcpy(glyph, g, 5);
    break;
  }
  case 'I': {
    uint8_t g[] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    memcpy(glyph, g, 5);
    break;
  }
  case 'J': {
    uint8_t g[] = {0x20, 0x40, 0x41, 0x3F, 0x01};
    memcpy(glyph, g, 5);
    break;
  }
  case 'K': {
    uint8_t g[] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    memcpy(glyph, g, 5);
    break;
  }
  case 'L': {
    uint8_t g[] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    memcpy(glyph, g, 5);
    break;
  }
  case 'M': {
    uint8_t g[] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    memcpy(glyph, g, 5);
    break;
  }
  case 'N': {
    uint8_t g[] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    memcpy(glyph, g, 5);
    break;
  }
  case 'O': {
    uint8_t g[] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    memcpy(glyph, g, 5);
    break;
  }
  case 'P': {
    uint8_t g[] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    memcpy(glyph, g, 5);
    break;
  }
  case 'R': {
    uint8_t g[] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    memcpy(glyph, g, 5);
    break;
  }
  case 'S': {
    uint8_t g[] = {0x46, 0x49, 0x49, 0x49, 0x31};
    memcpy(glyph, g, 5);
    break;
  }
  case 'T': {
    uint8_t g[] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    memcpy(glyph, g, 5);
    break;
  }
  case 'U': {
    uint8_t g[] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    memcpy(glyph, g, 5);
    break;
  }
  case 'V': {
    uint8_t g[] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    memcpy(glyph, g, 5);
    break;
  }
  case 'W': {
    uint8_t g[] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
    memcpy(glyph, g, 5);
    break;
  }
  case 'X': {
    uint8_t g[] = {0x63, 0x14, 0x08, 0x14, 0x63};
    memcpy(glyph, g, 5);
    break;
  }
  case ':': {
    uint8_t g[] = {0x00, 0x36, 0x36, 0x00, 0x00};
    memcpy(glyph, g, 5);
    break;
  }
  case '-': {
    uint8_t g[] = {0x08, 0x08, 0x08, 0x08, 0x08};
    memcpy(glyph, g, 5);
    break;
  }
  default:
    break;
  }
}

void oledDrawChar(int x, int y, char c, bool on = true) {
  uint8_t glyph[5];
  glyphFor(c, glyph);

  for (int col = 0; col < 5; col++) {
    for (int row = 0; row < 7; row++) {
      if (glyph[col] & (1 << row)) {
        oledPixel(x + col, y + row, on);
      }
    }
  }
}

void oledDrawText(int x, int y, const char *text, bool on = true) {
  for (int i = 0; text[i] != '\0'; i++) {
    oledDrawChar(x + i * 6, y, text[i], on);
  }
}

void oledDrawTextCentered(int y, const char *text, bool on = true) {
  int width = strlen(text) * 6 - 1;
  int x = (OLED_W - width) / 2;
  oledDrawText(x, y, text, on);
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

bool samePoint(Point a, Point b) {
  return a.x == b.x && a.y == b.y;
}

bool snakeOccupies(const Point snake[], int length, Point p) {
  for (int i = 0; i < length; i++) {
    if (samePoint(snake[i], p)) {
      return true;
    }
  }
  return false;
}

bool occupiedByAnySnake(Point p) {
  if (snakeOccupies(snake1, snake1Length, p)) {
    return true;
  }
  return gameMode == MODE_DUO && snakeOccupies(snake2, snake2Length, p);
}

void placeFood() {
  if (snake1Length + snake2Length >= MAX_SNAKE) {
    food = {-1, -1};
    return;
  }

  do {
    food.x = random(0, BOARD_W);
    food.y = random(0, BOARD_H);
  } while (occupiedByAnySnake(food));
}

void drawMenuOnOled() {
  if (!oledReady) {
    return;
  }

  char line[18];

  oledClearBuffer();
  oledDrawRect(0, 0, OLED_W, OLED_H);
  oledDrawTextCentered(4, "SNAKE ESP32");
  oledDrawText(16, 20, "1 SOLO");
  oledDrawText(16, 31, "2 DUO");
  snprintf(line, sizeof(line), "REC S:%d", highScoreSolo);
  oledDrawText(16, 45, line);
  snprintf(line, sizeof(line), "D:%d", highScoreDuo);
  oledDrawText(82, 45, line);
  oledDisplay();
}

void drawGameOnOled() {
  if (!oledReady) {
    return;
  }

  if (screen == SCREEN_MENU) {
    drawMenuOnOled();
    return;
  }

  char line[18];

  oledClearBuffer();

  if (gameMode == MODE_DUO) {
    snprintf(line, sizeof(line), "J1:%d J2:%d", score1, score2);
    oledDrawText(0, 1, line);
    snprintf(line, sizeof(line), "R:%d", highScoreDuo);
    oledDrawText(98, 1, line);
  } else {
    snprintf(line, sizeof(line), "SCORE:%d", score1);
    oledDrawText(0, 1, line);
    snprintf(line, sizeof(line), "REC:%d", highScoreSolo);
    oledDrawText(80, 1, line);
  }

  oledDrawRect(OLED_BOARD_X - 1, OLED_BOARD_Y - 1, BOARD_W * OLED_CELL + 2,
               BOARD_H * OLED_CELL + 2);

  if (food.x >= 0 && food.y >= 0) {
    int fx = OLED_BOARD_X + food.x * OLED_CELL;
    int fy = OLED_BOARD_Y + food.y * OLED_CELL;
    oledFillRect(fx + 1, fy + 1, OLED_CELL - 2, OLED_CELL - 2, true);
  }

  for (int i = snake1Length - 1; i >= 0; i--) {
    int sx = OLED_BOARD_X + snake1[i].x * OLED_CELL;
    int sy = OLED_BOARD_Y + snake1[i].y * OLED_CELL;
    int inset = i == 0 ? 0 : 1;
    oledFillRect(sx + inset, sy + inset, OLED_CELL - inset, OLED_CELL - inset, true);
  }

  if (gameMode == MODE_DUO) {
    for (int i = snake2Length - 1; i >= 0; i--) {
      int sx = OLED_BOARD_X + snake2[i].x * OLED_CELL;
      int sy = OLED_BOARD_Y + snake2[i].y * OLED_CELL;
      oledDrawRect(sx, sy, OLED_CELL, OLED_CELL);
      if (i == 0) {
        oledFillRect(sx + 2, sy + 2, 1, 1, true);
      }
    }
  }

  if (screen == SCREEN_GAME_OVER) {
    oledFillRect(30, 22, 68, 27, false);
    oledDrawRect(30, 22, 68, 27);
    oledDrawTextCentered(27, "GAME OVER");
    oledDrawTextCentered(38, "R RESET");
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

bool isReverse(Direction current, Direction next) {
  return (current == DIR_UP && next == DIR_DOWN) ||
         (current == DIR_DOWN && next == DIR_UP) ||
         (current == DIR_LEFT && next == DIR_RIGHT) ||
         (current == DIR_RIGHT && next == DIR_LEFT);
}

void setNextDirection(int player, Direction requested) {
  if (player == 1) {
    if (!isReverse(direction1, requested)) {
      nextDirection1 = requested;
    }
    return;
  }

  if (gameMode == MODE_DUO && !isReverse(direction2, requested)) {
    nextDirection2 = requested;
  }
}

void updateHighScores() {
  if (gameMode == MODE_SOLO) {
    if (score1 > highScoreSolo) {
      highScoreSolo = score1;
    }
    return;
  }

  int total = score1 + score2;
  if (total > highScoreDuo) {
    highScoreDuo = total;
  }
}

void resetGame(GameMode mode) {
  gameMode = mode;
  screen = SCREEN_PLAYING;
  snake1Length = 3;
  snake2Length = mode == MODE_DUO ? 3 : 0;
  score1 = 0;
  score2 = 0;
  alive1 = true;
  alive2 = mode == MODE_DUO;

  snake1[0] = {BOARD_W / 4, BOARD_H / 2};
  snake1[1] = {BOARD_W / 4 - 1, BOARD_H / 2};
  snake1[2] = {BOARD_W / 4 - 2, BOARD_H / 2};
  direction1 = DIR_RIGHT;
  nextDirection1 = DIR_RIGHT;

  if (mode == MODE_DUO) {
    snake2[0] = {BOARD_W * 3 / 4, BOARD_H / 2};
    snake2[1] = {BOARD_W * 3 / 4 + 1, BOARD_H / 2};
    snake2[2] = {BOARD_W * 3 / 4 + 2, BOARD_H / 2};
    direction2 = DIR_LEFT;
    nextDirection2 = DIR_LEFT;
  }

  placeFood();
  lastTick = millis();
  drawGameOnOled();
  broadcastState();
}

void showMenu() {
  screen = SCREEN_MENU;
  snake1Length = 0;
  snake2Length = 0;
  score1 = 0;
  score2 = 0;
  food = {-1, -1};
  drawGameOnOled();
  broadcastState();
}

Point nextHead(Point head, Direction direction) {
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
  return head;
}

bool hitsWall(Point p) {
  return p.x < 0 || p.x >= BOARD_W || p.y < 0 || p.y >= BOARD_H;
}

bool hitsSnake(Point p, const Point snake[], int length, int ignoredTailCount) {
  int limit = length - ignoredTailCount;
  if (limit < 0) {
    limit = 0;
  }

  for (int i = 0; i < limit; i++) {
    if (samePoint(p, snake[i])) {
      return true;
    }
  }
  return false;
}

void moveSnake(Point snake[], int &length, Point head, bool eatsFood) {
  if (eatsFood && length < MAX_SNAKE) {
    length++;
  }

  for (int i = length - 1; i > 0; i--) {
    snake[i] = snake[i - 1];
  }
  snake[0] = head;
}

bool updateGame() {
  if (screen != SCREEN_PLAYING) {
    return false;
  }

  direction1 = nextDirection1;
  direction2 = nextDirection2;

  Point oldHead1 = snake1[0];
  Point oldHead2 = snake2[0];
  Point head1 = alive1 ? nextHead(oldHead1, direction1) : oldHead1;
  Point head2 = alive2 ? nextHead(oldHead2, direction2) : oldHead2;
  bool eats1 = alive1 && samePoint(head1, food);
  bool eats2 = alive2 && samePoint(head2, food);
  int ignoreTail1 = alive1 && !eats1 ? 1 : 0;
  int ignoreTail2 = alive2 && !eats2 ? 1 : 0;
  bool dies1 = false;
  bool dies2 = false;

  if (alive1) {
    dies1 = hitsWall(head1) ||
            hitsSnake(head1, snake1, snake1Length, ignoreTail1) ||
            (gameMode == MODE_DUO && hitsSnake(head1, snake2, snake2Length, ignoreTail2));
  }

  if (alive2) {
    dies2 = hitsWall(head2) ||
            hitsSnake(head2, snake2, snake2Length, ignoreTail2) ||
            hitsSnake(head2, snake1, snake1Length, ignoreTail1);
  }

  if (gameMode == MODE_DUO && alive1 && alive2) {
    if (samePoint(head1, head2) || (samePoint(head1, oldHead2) && samePoint(head2, oldHead1))) {
      dies1 = true;
      dies2 = true;
    }
  }

  if (dies1) {
    alive1 = false;
  }
  if (dies2) {
    alive2 = false;
  }

  bool foodWasEaten = false;
  if (alive1) {
    if (eats1) {
      score1++;
      foodWasEaten = true;
    }
    moveSnake(snake1, snake1Length, head1, eats1);
  }

  if (alive2) {
    if (eats2) {
      score2++;
      foodWasEaten = true;
    }
    moveSnake(snake2, snake2Length, head2, eats2);
  }

  if (foodWasEaten) {
    placeFood();
  }

  if (!alive1 && (gameMode == MODE_SOLO || !alive2)) {
    screen = SCREEN_GAME_OVER;
    updateHighScores();
  }

  return true;
}

void printSnakeJson(WiFiClient &out, const Point snake[], int length) {
  out.print("[");
  for (int i = 0; i < length; i++) {
    if (i > 0) {
      out.print(",");
    }
    out.print("[");
    out.print(snake[i].x);
    out.print(",");
    out.print(snake[i].y);
    out.print("]");
  }
  out.print("]");
}

void sendStateTo(WiFiClient &out) {
  if (!out || !out.connected()) {
    return;
  }

  out.print("{\"w\":");
  out.print(BOARD_W);
  out.print(",\"h\":");
  out.print(BOARD_H);
  out.print(",\"mode\":\"");
  out.print(gameMode == MODE_DUO ? "duo" : "solo");
  out.print("\",\"screen\":\"");
  if (screen == SCREEN_MENU) {
    out.print("menu");
  } else if (screen == SCREEN_GAME_OVER) {
    out.print("over");
  } else {
    out.print("playing");
  }
  out.print("\",\"score\":");
  out.print(score1 + score2);
  out.print(",\"score1\":");
  out.print(score1);
  out.print(",\"score2\":");
  out.print(score2);
  out.print(",\"highScoreSolo\":");
  out.print(highScoreSolo);
  out.print(",\"highScoreDuo\":");
  out.print(highScoreDuo);
  out.print(",\"alive1\":");
  out.print(alive1 ? "true" : "false");
  out.print(",\"alive2\":");
  out.print(alive2 ? "true" : "false");
  out.print(",\"over\":");
  out.print(screen == SCREEN_GAME_OVER ? "true" : "false");
  out.print(",\"food\":[");
  out.print(food.x);
  out.print(",");
  out.print(food.y);
  out.print("],\"snake\":");
  printSnakeJson(out, snake1, snake1Length);
  out.print(",\"snake2\":");
  printSnakeJson(out, snake2, snake2Length);
  out.println("}");
}

void broadcastState() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    sendStateTo(clients[i]);
  }
}

bool commandEquals(const char *command, const char *expected) {
  return strcmp(command, expected) == 0;
}

void handleSingleCharCommand(char c) {
  switch (c) {
  case 'U':
    setNextDirection(1, DIR_UP);
    return;
  case 'D':
    setNextDirection(1, DIR_DOWN);
    return;
  case 'L':
    setNextDirection(1, DIR_LEFT);
    return;
  case 'R':
    setNextDirection(1, DIR_RIGHT);
    return;
  case '1':
    resetGame(MODE_SOLO);
    return;
  case '2':
    resetGame(MODE_DUO);
    return;
  case 'N':
    resetGame(gameMode);
    return;
  case 'M':
    showMenu();
    return;
  default:
    break;
  }

  switch (tolower(static_cast<unsigned char>(c))) {
  case 'z':
  case 'w':
    setNextDirection(1, DIR_UP);
    break;
  case 's':
    setNextDirection(1, DIR_DOWN);
    break;
  case 'a':
  case 'q':
    setNextDirection(1, DIR_LEFT);
    break;
  case 'd':
    setNextDirection(1, DIR_RIGHT);
    break;
  case 'i':
    setNextDirection(2, DIR_UP);
    break;
  case 'k':
    setNextDirection(2, DIR_DOWN);
    break;
  case 'j':
    setNextDirection(2, DIR_LEFT);
    break;
  case 'l':
    setNextDirection(2, DIR_RIGHT);
    break;
  case 'r':
    resetGame(gameMode);
    break;
  case 'm':
    showMenu();
    break;
  default:
    break;
  }
}

void handleCommand(const char *command) {
  if (command[0] == '\0') {
    return;
  }

  if (strlen(command) == 1) {
    handleSingleCharCommand(command[0]);
    return;
  }

  if (commandEquals(command, "SINGLE")) {
    resetGame(MODE_SOLO);
  } else if (commandEquals(command, "MULTI")) {
    resetGame(MODE_DUO);
  } else if (commandEquals(command, "MENU")) {
    showMenu();
  } else if (commandEquals(command, "RESET")) {
    resetGame(gameMode);
  } else if (commandEquals(command, "P1U")) {
    setNextDirection(1, DIR_UP);
  } else if (commandEquals(command, "P1D")) {
    setNextDirection(1, DIR_DOWN);
  } else if (commandEquals(command, "P1L")) {
    setNextDirection(1, DIR_LEFT);
  } else if (commandEquals(command, "P1R")) {
    setNextDirection(1, DIR_RIGHT);
  } else if (commandEquals(command, "P2U")) {
    setNextDirection(2, DIR_UP);
  } else if (commandEquals(command, "P2D")) {
    setNextDirection(2, DIR_DOWN);
  } else if (commandEquals(command, "P2L")) {
    setNextDirection(2, DIR_LEFT);
  } else if (commandEquals(command, "P2R")) {
    setNextDirection(2, DIR_RIGHT);
  }
}

void readClientCommands() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i] || !clients[i].connected()) {
      continue;
    }

    while (clients[i].available()) {
      char c = clients[i].read();
      if (c == '\r') {
        continue;
      }

      if (c == '\n') {
        commandBuffers[i][commandLengths[i]] = '\0';
        handleCommand(commandBuffers[i]);
        commandLengths[i] = 0;
        continue;
      }

      if (commandLengths[i] < sizeof(commandBuffers[i]) - 1) {
        commandBuffers[i][commandLengths[i]++] = c;
      } else {
        commandBuffers[i][sizeof(commandBuffers[i]) - 1] = '\0';
        handleCommand(commandBuffers[i]);
        commandLengths[i] = 0;
      }
    }
  }
}

void acceptClients() {
  WiFiClient newClient = server.available();
  if (!newClient) {
    return;
  }

  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i] || !clients[i].connected()) {
      if (clients[i]) {
        clients[i].stop();
      }

      clients[i] = newClient;
      clients[i].setNoDelay(true);
      commandLengths[i] = 0;
      Serial.print("Client ");
      Serial.print(i + 1);
      Serial.print(" connecte: ");
      Serial.println(clients[i].remoteIP());
      sendStateTo(clients[i]);
      return;
    }
  }

  Serial.println("Client refuse: deja deux joueurs connectes.");
  newClient.stop();
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
  setupWifi();
  server.begin();

  Serial.print("Socket TCP ouverte sur le port ");
  Serial.println(SERVER_PORT);

  setupOled();
  showMenu();
}

void loop() {
  acceptClients();
  readClientCommands();

  uint32_t now = millis();
  if (now - lastStatusPrinted >= STATUS_PRINT_MS) {
    lastStatusPrinted = now;
    printNetworkStatus();
  }

  if (screen == SCREEN_PLAYING && now - lastTick >= TICK_MS) {
    lastTick = now;
    if (updateGame()) {
      drawGameOnOled();
      broadcastState();
      lastStateSent = now;
    }
  } else if (now - lastStateSent >= 1000) {
    broadcastState();
    lastStateSent = now;
  }
}
