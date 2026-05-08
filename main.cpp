// ============================================================
//  Unified Game Console  –  Nucleo-G071RB
//  Display 1 : ILI9341 TFT (320×240, landscape)  → Pong
//  Display 2 : 16×2 character LCD               → Bunny Jumper / Risky Road
//  Framework : Arduino (PlatformIO / STM32duino)
// ============================================================
//
//  WIRING SUMMARY
//  ─────────────────────────────────────────────────────────
//  TFT ILI9341
//    CS   → D10   DC  → D8    RST → D9
//    MOSI → D11   SCK → D13   (hardware SPI)
//
//  16×2 LCD (4-bit parallel)
//    RS → D2   EN → D3
//    D4 → D4   D5 → D5   D6 → D6   D7 → D7
//
//  Buttons
//    BTN_UP   → A0  (INPUT_PULLUP)
//    BTN_DOWN → A1  (INPUT_PULLUP)
//    BTN_SEL  → A2  (INPUT_PULLUP)   ← new "select / jump" button
//    BTN_LEFT → A4     (INPUT_PULLUP)
//    BTN_RIGHT → D15   (INPUT_PULLUP)
//
//  Buzzer   → D5   (PWM capable)
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <LiquidCrystal.h>

#include "stm32g0xx_hal.h"
#include "stm32g0xx_hal_flash.h"

//game saves
int activeSaveSlot = 0;
int dungeonRun = 1;

// ─── TFT (leave SPI bus D11/D12/D13 alone) ───
#define TFT_CS   D10
#define TFT_DC   D2
#define TFT_RST  D9

#define TFT_MOSI D11
#define TFT_MISO D12
#define TFT_SCK D13

// ─── Buttons ───
#define BTN_UP   A0
#define BTN_DOWN A1
#define BTN_SEL  A2
#define BTN_LEFT A4
#define BTN_RIGHT D15

#define IR_SENSOR A5

// ─── Buzzer ───
#define BUZZER   A3

// ─── LCD (4-bit parallel, unchanged) ───
LiquidCrystal lcd(D8, D7, D6, D5, D4, D3); // RS,EN,d4,d5,d6,d7

// ─── Display objects ────────────────────────────────────────
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);


// ─── Screen size (landscape) ────────────────────────────────
const int SW = 320;
const int SH = 240;

// ─── Global volume  (0 = off, 1-5 = quiet→loud) ────────────
int g_volume = 3;          // default mid

// ============================================================
//  UTILITY: debounced button helpers
// ============================================================
bool btnPressed(int pin) {
    if (digitalRead(pin) == LOW) {
        delay(30);
        return digitalRead(pin) == LOW;
    }
    return false;
}

// Wait for a button to be fully released
void waitRelease(int pin) {
    while (digitalRead(pin) == LOW) delay(10);
    delay(30);
}

// ============================================================
//  SOUND – volume-aware wrappers around tone()
// ============================================================
// Map volume level to an amplitude hint. Because tone() has no
// volume on most MCUs we fake it: at vol=0 we simply skip the
// call.  If your buzzer is on a PWM-with-transistor circuit you
// can swap the analogWrite line instead.
void playTone(int freq, int dur) {
    if (g_volume == 0) return;
    // Optional: drive a PWM duty to the buzzer for coarse volume
    // analogWrite(BUZZER, map(g_volume, 1, 5, 30, 255));
    tone(BUZZER, freq, dur);
}

void soundHit(int hitPos = 0) { playTone(900 + abs(hitPos) * 10, 40); }
void soundWall()               { playTone(600, 30); }
void soundScore()              { playTone(200, 200); delay(80); playTone(300, 200); }
void soundWin()                { playTone(1000,150); delay(180); playTone(1300,150); delay(180); playTone(1600,300); }
void soundMenu()               { playTone(800, 60); }
void soundBack()               { playTone(400, 60); }

// ============================================================
//  TFT MENU HELPERS
// ============================================================
void tftClear() { tft.fillScreen(ILI9341_BLACK); }

void tftTitle(const char* text, int y = 40) {
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_CYAN);
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SW - w) / 2, y);
    tft.print(text);
}

void tftMenuItem(const char* text, int idx, bool selected) {
    int y = 100 + idx * 40;
    tft.fillRect(40, y - 4, SW - 80, 32, ILI9341_BLACK);
    tft.setTextSize(2);
    if (selected) {
        tft.fillRect(38, y - 4, SW - 76, 32, ILI9341_NAVY);
        tft.drawRect(38, y - 4, SW - 76, 32, ILI9341_CYAN);
        tft.setTextColor(ILI9341_CYAN);
    } else {
        tft.setTextColor(ILI9341_WHITE);
    }
    tft.setCursor(60, y);
    tft.print(selected ? "> " : "  ");
    tft.print(text);
}

// ============================================================
//  SETTINGS MENU  (volume only for now)
// ============================================================
extern "C" char* sbrk(int incr);

int freeMemory() {
    char top;
    return &top - reinterpret_cast<char*>(sbrk(0));
}

void settingsMenu() {
    tftClear();
    tftTitle("SETTINGS");

    // Draw volume bar
    auto drawVol = [&]() {
        tft.fillRect(60, 95, 200, 20, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(60, 100);
        tft.print("Volume: ");
        if (g_volume == 0) tft.print("OFF ");
        else {
          tft.print(g_volume);
          tft.print(" ");  // ← padding to overwrite leftovers
        }

        // ---- Bar ----
        int barW = (320 - 120);
        tft.fillRect(60, 125, barW, 20, ILI9341_BLACK);
        tft.drawRect(60, 125, barW, 20, ILI9341_WHITE);
        tft.fillRect(62, 127, map(g_volume, 0, 5, 0, barW - 4), 16, ILI9341_CYAN);

        // ---- RAM (ADD HERE) ----
        int freeRam = freeMemory();

        float freeKB = freeRam / 1024.0;
        float usedKB = 36.0 - freeKB;

        tft.setTextColor(ILI9341_RED);
        tft.setCursor(60, 150);

        tft.print("RAM: ");
        tft.print(usedKB, 1);
        tft.print("/");
        tft.print(36);
        tft.print(" KB");      

    };
    

    drawVol();

    while (true) {
        if (btnPressed(BTN_RIGHT)) {
            waitRelease(BTN_RIGHT);
            if (g_volume < 5) g_volume++;
            soundMenu();
            drawVol();
        }
        if (btnPressed(BTN_LEFT)) {
            waitRelease(BTN_LEFT);
            if (g_volume > 0) g_volume--;
            soundBack();
            drawVol();
        }
        if (btnPressed(BTN_SEL)) {
            waitRelease(BTN_SEL);
            soundBack();
            return;   // back to main menu
        }
        delay(50);
    }
}

// ============================================================
//  MAIN TFT MENU (SCROLLING VERSION)
// ============================================================

int mainMenu() {

    const char* items[] = {
        "Pong",
        "Bunny Jumper",
        "Risky Road",
        "IR Rhythm",
        "RPG",
        "Settings"
    };

    const int N = sizeof(items) / sizeof(items[0]);

    int sel = 0;
    int scrollOffset = 0;

    const int visibleItems = 4;

    auto redraw = [&]() {

        tftClear();

        tftTitle("GAME CONSOLE", 20);

        // Keep selected item visible
        if (sel < scrollOffset)
            scrollOffset = sel;

        if (sel >= scrollOffset + visibleItems)
            scrollOffset = sel - visibleItems + 1;

        // Draw visible menu entries
        for (int i = 0; i < visibleItems; i++) {

            int itemIndex = i + scrollOffset;

            if (itemIndex >= N)
                break;

            tftMenuItem(
                items[itemIndex],
                i,
                itemIndex == sel
            );
        }

        // Scroll indicators
        tft.setTextSize(2);

        if (scrollOffset > 0) {
            tft.setCursor(290, 70);
            tft.setTextColor(ILI9341_WHITE);
            tft.print("^");
        }

        if (scrollOffset + visibleItems < N) {
            tft.setCursor(290, 220);
            tft.setTextColor(ILI9341_WHITE);
            tft.print("v");
        }
    };

    redraw();

    while (true) {

        // ---- UP ----
        if (btnPressed(BTN_UP)) {

            waitRelease(BTN_UP);

            sel--;

            if (sel < 0)
                sel = N - 1;

            soundMenu();

            redraw();
        }

        // ---- DOWN ----
        if (btnPressed(BTN_DOWN)) {

            waitRelease(BTN_DOWN);

            sel++;

            if (sel >= N)
                sel = 0;

            soundMenu();

            redraw();
        }

        // ---- SELECT ----
        if (btnPressed(BTN_SEL)) {

            waitRelease(BTN_SEL);

            // Settings
            if (sel == 5) {

                settingsMenu();

                redraw();
            }
            else {

                soundWin();

                return sel;
            }
        }

        delay(50);
    }
}



// ============================================================
//  GAME 1 – PONG (ILI9341 TFT)
// ============================================================
void gamePong() {
    tftClear();

    int ballX = SW / 2, ballY = SH / 2;
    int dx = 2, dy = 2;
    int prevBallX = ballX, prevBallY = ballY;

    int paddleH = 50, paddleW = 8;
    int leftY = 95, rightY = 95;
    int prevLeftY = leftY, prevRightY = rightY;

    int playerScore = 0, aiScore = 0;

    auto resetBall = [&]() {
        ballX = SW / 2; ballY = SH / 2;
        dx = (random(0, 2) == 0) ? 2 : -2;
        dy = random(-2, 3);
    };

    while (true) {
        prevBallX = ballX; prevBallY = ballY;
        prevLeftY = leftY; prevRightY = rightY;

        // Input
        if (digitalRead(BTN_UP)   == LOW) leftY -= 3;
        if (digitalRead(BTN_DOWN) == LOW) leftY += 3;
        leftY = constrain(leftY, 0, SH - paddleH);

        // Ball move
        ballX += dx; ballY += dy;

        // Wall bounce
        if (ballY <= 4 || ballY >= SH - 4) { dy = -dy; soundWall(); }

        // AI paddle
        int target = ballY - paddleH / 2;
        if (rightY + paddleH / 2 < target) rightY += 2;
        else if (rightY + paddleH / 2 > target) rightY -= 2;
        rightY = constrain(rightY, 0, SH - paddleH);

        // Left paddle collision
        if (ballX <= 10 + paddleW && ballY >= leftY && ballY <= leftY + paddleH) {
            dx = abs(dx);
            int hp = ballY - (leftY + paddleH / 2);
            dy = hp / 5;
            soundHit(hp);
        }

        // Right paddle collision
        if (ballX >= SW - 10 - paddleW && ballY >= rightY && ballY <= rightY + paddleH) {
            dx = -abs(dx);
            int hp = ballY - (rightY + paddleH / 2);
            dy = hp / 5;
            soundHit(hp);
        }

        // Scoring
        if (ballX < 0)  { aiScore++;     soundScore(); resetBall(); }
        if (ballX > SW) { playerScore++; soundScore(); resetBall(); }

        // Win
        if (playerScore >= 5 || aiScore >= 5) {
            tftClear();
            tft.setCursor(80, 100);
            tft.setTextSize(3);
            tft.setTextColor(ILI9341_WHITE);
            tft.print(playerScore > aiScore ? "YOU WIN!" : "AI WINS!");
            soundWin();
            delay(3000);
            return;   // back to menu
        }

        // Erase & draw
        tft.fillCircle(prevBallX, prevBallY, 4, ILI9341_BLACK);
        tft.fillRect(10,      prevLeftY,  paddleW, paddleH, ILI9341_BLACK);
        tft.fillRect(SW - 18, prevRightY, paddleW, paddleH, ILI9341_BLACK);

        tft.fillRect(10,      leftY,  paddleW, paddleH, ILI9341_WHITE);
        tft.fillRect(SW - 18, rightY, paddleW, paddleH, ILI9341_WHITE);
        tft.fillCircle(ballX, ballY, 4, ILI9341_WHITE);

        // Score display
        tft.fillRect(SW / 2 - 40, 0, 80, 30, ILI9341_BLACK);
        tft.setCursor(SW / 2 - 20, 10);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_WHITE);
        tft.print(playerScore);
        tft.print(":");
        tft.print(aiScore);

        delay(10);
    }
}

// ============================================================
//  LCD CUSTOM CHARACTERS
//  LiquidCrystal uses createChar(slot, byte[8])
// ============================================================
byte charDino[8]   = {0x04,0x14,0x0D,0x05,0x16,0x14,0x0C,0x04};
byte charCactus[8] = {0x04,0x03,0x03,0x16,0x1E,0x1E,0x12,0x19};
byte charPlayer[8] = {0x00,0x00,0x1B,0x1F,0x1F,0x1B,0x00,0x00};
byte charEnemy[8]  = {0x04,0x0E,0x0E,0x0E,0x0E,0x0E,0x04,0x00};

// ============================================================
//  GAME 2 – BUNNY JUMPER (16×2 LCD)
//  Ported from your mbed game1().
//  BTN_SEL = jump (hold = stay in row 0, release = row 1)
// ============================================================
void gameBunnyJumper() {
    lcd.clear();
    lcd.createChar(0, charCactus);
    lcd.createChar(1, charDino);

    int dinoPos = 1;          // 0 = top row, 1 = bottom row
    int prevDinoPos = 1;
    int cactusCol = 15;
    int prevCactusCol = 15;
    int score = 0;
    int delayTime = 200;

    // Show "READY" on TFT
    tftClear();
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(80, 100);
    tft.print("Bunny Jumper");
    tft.setCursor(90, 130);
    tft.setTextColor(ILI9341_WHITE);
    tft.print("Watch the LCD!");

    while (true) {
        // ---- Input: hold SEL to jump (row 0) ----
        dinoPos = (digitalRead(BTN_SEL) == LOW) ? 0 : 1;

        // ---- Score ----
        if (dinoPos == 1) score++;
        else { score -= 2; if (score < 0) score = 0; }

        // ---- Speed ----
        delayTime = max(60, 200 - (score / 10) * 20);

        // ---- Clear old positions ----
        lcd.setCursor(0, prevDinoPos);   lcd.print(" ");
        lcd.setCursor(prevCactusCol, 1); lcd.print(" ");

        // ---- Draw ----
        lcd.setCursor(0, dinoPos);   lcd.write(byte(0));   // dino
        lcd.setCursor(cactusCol, 1); lcd.write(byte(1));   // cactus

        // ---- Score on LCD row 0 right side ----
        lcd.setCursor(10, 0);
        char buf[6]; sprintf(buf, "%4d", score);
        lcd.print(buf);

        // ---- Score mirrored on TFT ----
        tft.fillRect(100, 170, 120, 30, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(100, 170);
        tft.print("Score: ");
        tft.print(score);

        // ---- Update previous ----
        prevDinoPos  = dinoPos;
        prevCactusCol = cactusCol;

        // ---- Move cactus ----
        cactusCol--;
        if (cactusCol < 0) cactusCol = 15;

        // ---- Collision ----
        if (cactusCol == 0 && dinoPos == 1) {
            lcd.clear();
            lcd.setCursor(0, 0); lcd.print("GAME OVER");
            lcd.setCursor(0, 1);
            char buf2[16]; sprintf(buf2, "Score:%d", score); lcd.print(buf2);

            tftClear();
            tft.setTextSize(3);
            tft.setTextColor(ILI9341_RED);
            tft.setCursor(60, 80);  tft.print("GAME OVER");
            tft.setTextSize(2);
            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(80, 140); tft.print("Score: "); tft.print(score);
            tft.setCursor(40, 180); tft.print("SEL = return to menu");

            playTone(300, 600);

            // Wait for SEL to go back
            while (!btnPressed(BTN_SEL)) delay(50);
            waitRelease(BTN_SEL);
            lcd.clear();
            return;
        }

        delay(delayTime);
    }
}

// ============================================================
//  GAME 3 – RISKY ROAD (16×2 LCD)
//  Ported from your mbed game2().
//  BTN_SEL = toggle lane (edge-triggered)
// ============================================================
void gameRiskyRoad() {
    lcd.clear();
    lcd.createChar(0, charPlayer);
    lcd.createChar(1, charEnemy);

    int playerLane = 1;      // 0 = top, 1 = bottom
    int enemyPos   = 15;
    int enemyLane  = random(0, 2);

    int prevPlayerLane = 1;
    int prevEnemyPos   = 15;

    int score     = 0;
    int delayTime = 200;
    bool lastBtn  = HIGH;

    // TFT background
    tftClear();
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(80, 100);
    tft.print("Risky Road");
    tft.setCursor(90, 130);
    tft.setTextColor(ILI9341_WHITE);
    tft.print("Watch the LCD!");

    while (true) {
        // ---- Input: edge-detect on SEL ----
        bool cur = (digitalRead(BTN_SEL) == LOW);
        if (cur && !lastBtn) playerLane = !playerLane;
        lastBtn = cur;

        // ---- Score ----
        score++;

        // ---- Speed ----
        delayTime = max(60, 200 - (score / 50) * 15);

        // ---- Clear old ----
        lcd.setCursor(0, prevPlayerLane); lcd.print(" ");
        lcd.setCursor(prevEnemyPos, enemyLane); lcd.print(" ");

        // ---- Draw ----
        lcd.setCursor(0, playerLane);   lcd.write(byte(0));
        lcd.setCursor(enemyPos, enemyLane); lcd.write(byte(1));

        // ---- Score on LCD ----
        lcd.setCursor(10, 0);
        char buf[6]; sprintf(buf, "%4d", score); lcd.print(buf);

        // ---- Score on TFT ----
        tft.fillRect(100, 170, 120, 30, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(100, 170);
        tft.print("Score: ");
        tft.print(score);

        // ---- Save previous ----
        prevPlayerLane = playerLane;
        prevEnemyPos   = enemyPos;

        // ---- Move enemy ----
        enemyPos--;
        if (enemyPos < 0) {
            enemyPos  = 15;
            enemyLane = random(0, 2);
        }

        // ---- Collision ----
        if (enemyPos == 0 && enemyLane == playerLane) {
            lcd.clear();
            lcd.setCursor(0, 0); lcd.print("CRASH!");
            lcd.setCursor(0, 1);
            char buf2[16]; sprintf(buf2, "Score:%d", score); lcd.print(buf2);

            tftClear();
            tft.setTextSize(3);
            tft.setTextColor(ILI9341_RED);
            tft.setCursor(80, 80);  tft.print("CRASH!");
            tft.setTextSize(2);
            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(80, 140); tft.print("Score: "); tft.print(score);
            tft.setCursor(40, 180); tft.print("SEL = return to menu");

            playTone(200, 800);

            while (!btnPressed(BTN_SEL)) delay(50);
            waitRelease(BTN_SEL);
            lcd.clear();
            return;
        }

        delay(delayTime);
    }
}


// ============================================================
//  GAME 4 – IR RHYTHM GAME
// ============================================================

struct Note {
    int x;
    int y;
    bool active;
};

const int NOTE_X = 140;
const int HIT_Y  = 200;

Note note;

int rhythmScore = 0;
bool beamBrokenLast = false;
unsigned long lastHit = 0;

// ============================================================
// Spawn note
// ============================================================

void spawnNote() {
    note.x = random(40, 280);
    note.y = 0;
    note.active = true;
}

// ============================================================
// Draw frame
// ============================================================

void drawRhythmGame() {

    tft.fillScreen(ILI9341_BLACK);

    // Lane
    tft.drawRect(NOTE_X - 10, 0, 40, 240, ILI9341_DARKGREY);

    // Hit zone
    tft.fillRect(NOTE_X - 15, HIT_Y, 50, 8, ILI9341_RED);

    // Note
    if (note.active) {
        tft.fillRect(NOTE_X, note.y, 20, 20, ILI9341_CYAN);
    }

    // Score
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);

    tft.print("Score: ");
    tft.print(rhythmScore);

    // Instructions
    tft.setTextSize(1);
    tft.setCursor(10, 220);
    tft.print("Wave hand through IR beam");
}

// ============================================================
// Main game
// ============================================================


void gameRhythm() {

    pinMode(IR_SENSOR, INPUT);

    rhythmScore = 0;

    unsigned long lastHit = 0;

    spawnNote();

    while (true) {

        // ---- Move note ----
        if (note.active) {
            note.y += 4 + rhythmScore / 10;
        }

        // ---- Missed note ----
        if (note.y > 240) {

            playTone(250, 120);

            note.active = false;

            delay(250);

            spawnNote();
        }

        // ---- Stable IR detection ----
        bool beamBroken = false;

        if (digitalRead(IR_SENSOR) == LOW) {

            delay(5);

            if (digitalRead(IR_SENSOR) == LOW) {
                beamBroken = true;
            }
        }

        // ---- Hit detection ----
        if (beamBroken && millis() - lastHit > 300)
        {
            lastHit = millis();

            if (abs(note.y - HIT_Y) < 20)
            {
                rhythmScore++;

                playTone(1000, 60);

                tft.fillScreen(ILI9341_GREEN);
                delay(30);
            }
            else
            {
                playTone(300, 120);

                tft.fillScreen(ILI9341_RED);
                delay(30);
            }
            
            spawnNote();
        }

        // ---- Draw game ----
        tft.fillScreen(ILI9341_BLACK);

        // Hit line
        tft.drawFastHLine(0, HIT_Y, 320, ILI9341_WHITE);

        // Draw note
        if (note.active) {
            tft.fillCircle(note.x, note.y, 12, ILI9341_CYAN);
        }

        // Score
        tft.setCursor(10, 10);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_WHITE);
        tft.print("Score: ");
        tft.print(rhythmScore);

        delay(16);
    }
}

// ============================================================
//  GAME 5 – DUNGEON RPG
//  FULL MERGED VERSION
//  - Binary tree dungeon (8 rooms, 1 boss)
//  - Inventory system (items, spells, equipment)
//  - Room reward popup (choose 1 of 3)
//  - LEFT button opens inventory on map screen
//  - LEFT/RIGHT cycles tabs in inventory
//  - UP/DOWN scrolls within tab, RIGHT closes inventory
//  - Combat uses derived stats (getAttack/getDefence/getCrit)
// ============================================================

// ============================================================
// ITEM TYPES
// ============================================================

#define ITEM_NONE   0
#define ITEM_POTION 1
#define ITEM_HELMET 2
#define ITEM_ARMOUR 3
#define ITEM_BOOTS  4
#define ITEM_WEAPON 5
#define ITEM_RING   6
#define ITEM_SPELL  7

struct Item {
    const char* name;
    int         type;
    int         statBonus;
    const char* description;
    bool        owned;
};

#define ITEM_POOL_SIZE 18

Item itemPool[ITEM_POOL_SIZE] = {
    // Helmets
    {"Iron Helm",   ITEM_HELMET, 3,  "DEF +3",          false},
    {"Steel Helm",  ITEM_HELMET, 6,  "DEF +6",          false},
    {"Mage Hood",   ITEM_HELMET, 2,  "DEF+2 ARC+1",     false},
    // Armour
    {"Leather",     ITEM_ARMOUR, 4,  "DEF +4",          false},
    {"Chain Mail",  ITEM_ARMOUR, 8,  "DEF +8",          false},
    {"Plate Mail",  ITEM_ARMOUR, 12, "DEF +12",         false},
    // Boots
    {"Light Boots", ITEM_BOOTS,  2,  "DEF +2",          false},
    {"Iron Boots",  ITEM_BOOTS,  5,  "DEF +5",          false},
    {"Swift Boots", ITEM_BOOTS,  3,  "DEF+3 CRIT+0.1",  false},
    // Weapons
    {"Short Sword", ITEM_WEAPON, 5,  "STR +5",          false},
    {"Iron Sword",  ITEM_WEAPON, 10, "STR +10",         false},
    {"Great Sword", ITEM_WEAPON, 16, "STR +16",         false},
    // Rings
    {"Gold Ring",   ITEM_RING,   0,  "CRIT +0.15",      false},
    {"Ruby Ring",   ITEM_RING,   0,  "CRIT +0.25",      false},
    {"Power Ring",  ITEM_RING,   5,  "STR+5 CRIT+0.1",  false},
    // Spells
    {"Fire Spell",  ITEM_SPELL,  20, "Fire 20dmg 5mp",  false},
    {"Ice Spell",   ITEM_SPELL,  15, "Ice 15dmg 3mp",   false},
    {"Thunder",     ITEM_SPELL,  25, "Bolt 25dmg 8mp",  false},
};

// ============================================================
// PLAYER
// ============================================================

#define INV_SIZE 16

struct Player {
    int hp;
    int maxHp;
    int mp;
    int maxMp;

    int   baseAttack;
    int   baseDefence;
    float critChance;   // base 1.0f; >1.0 = bonus crit %
    int   arcane;       // spell damage multiplier

    int potions;

    // Equipment slots — index into itemPool, -1 = empty
    int equippedHelmet;
    int equippedArmour;
    int equippedBoots;
    int equippedWeapon;
    int equippedRing;

    // Spells known
    int spells[3];
    int spellCount;

    // General inventory (gear waiting to be equipped)
    int inventory[INV_SIZE];
    int invCount;
};

// ============================================================
// ENEMY / ROOM
// ============================================================

struct Enemy {
    const char* name;
    int hp;
    int maxHp;
    int attack;
};

struct Room {
    int  rewardType;
    bool cleared;
};

// ============================================================
// GLOBALS
// ============================================================

Player player;
Enemy  enemy;

#define REWARD_NONE  0
#define REWARD_ITEM  1   // generic — all non-boss rooms give item choice
#define REWARD_BOSS  2

const int ROOM_COUNT = 8;
Room rooms[ROOM_COUNT];

//
//          0
//        /   \
//       1     2
//      / \   / \
//     3  4  5  6
//      \  \ /  /
//          7  (BOSS)
//
bool roomLinks[ROOM_COUNT][ROOM_COUNT] = {
 //0 1 2 3 4 5 6 7
  {0,1,1,0,0,0,0,0}, // 0
  {1,0,0,1,1,0,0,0}, // 1
  {1,0,0,0,0,1,1,0}, // 2
  {0,1,0,0,0,0,0,1}, // 3
  {0,1,0,0,0,0,0,1}, // 4
  {0,0,1,0,0,0,0,1}, // 5
  {0,0,1,0,0,0,0,1}, // 6
  {0,0,0,1,1,1,1,0}  // 7 BOSS
};

int currentRoom = 0;

// Combat action menu
const char* actions[] = { "Attack", "Items", "Magic", "Run" };
int actionSel = 0;

// Active spell selected in combat (index into player.spells[])
int combatSpellSel = 0;

// ============================================================
// DERIVED STAT HELPERS
// ============================================================

int getAttack() {
    int atk = player.baseAttack;
    if (player.equippedWeapon >= 0)
        atk += itemPool[player.equippedWeapon].statBonus;
    // Power Ring STR bonus
    if (player.equippedRing >= 0 &&
        itemPool[player.equippedRing].type == ITEM_RING)
        atk += itemPool[player.equippedRing].statBonus;
    return atk;
}

int getDefence() {
    int def = player.baseDefence;
    if (player.equippedHelmet >= 0) def += itemPool[player.equippedHelmet].statBonus;
    if (player.equippedArmour >= 0) def += itemPool[player.equippedArmour].statBonus;
    if (player.equippedBoots  >= 0) def += itemPool[player.equippedBoots ].statBonus;
    return def;
}

float getCritChance() {
    float crit = player.critChance;
    if (player.equippedRing >= 0)
        crit += itemPool[player.equippedRing].statBonus * 0.1f;
    if (player.equippedBoots >= 0 &&
        strcmp(itemPool[player.equippedBoots].name, "Swift Boots") == 0)
        crit += 0.1f;
    return crit;
}

int getArcane() {
    int arc = player.arcane;
    // Mage Hood gives +1 arcane
    if (player.equippedHelmet >= 0 &&
        strcmp(itemPool[player.equippedHelmet].name, "Mage Hood") == 0)
        arc += 1;
    return arc;
}

// ============================================================
// INIT
// ============================================================

void initDungeon() {
    currentRoom = 0;

    for (int i = 0; i < ROOM_COUNT; i++) {
        rooms[i].cleared    = false;
        rooms[i].rewardType = (i == 7) ? REWARD_BOSS : REWARD_ITEM;
    }

    rooms[0].cleared = true; // player starts here
}

void initPlayer() {
    player.maxHp       = 100;
    player.hp          = 100;
    player.maxMp       = 30;
    player.mp          = 30;
    player.baseAttack  = 8;
    player.baseDefence = 0;
    player.critChance  = 1.0f;
    player.arcane      = 1;
    player.potions     = 3;

    player.equippedHelmet = -1;
    player.equippedArmour = -1;
    player.equippedBoots  = -1;
    player.equippedWeapon = -1;
    player.equippedRing   = -1;

    player.spellCount = 0;
    player.invCount   = 0;

    for (int i = 0; i < INV_SIZE; i++) player.inventory[i] = -1;
    for (int i = 0; i < 3;        i++) player.spells[i]    = -1;

    // Reset item pool
    for (int i = 0; i < ITEM_POOL_SIZE; i++)
        itemPool[i].owned = false;
}

// ============================================================
// ENEMY SETUP
// ============================================================

void setupEnemy(bool boss) {
    if (boss) {
        enemy.name   = "Goblin King";
        enemy.maxHp  = 120;
        enemy.hp     = 120;
        enemy.attack = 14;
    } else {
        enemy.name   = "Goblin";
        enemy.maxHp  = 50;
        enemy.hp     = 50;
        enemy.attack = 8;
    }
}

// ============================================================
// DRAW RPG SCREEN (TFT)
// ============================================================

void drawRPGScreen() {
    tft.fillScreen(ILI9341_BLACK);

    // Enemy name
    tft.setTextSize(2);
    tft.setTextColor(ILI9341_RED);
    tft.setCursor(100, 20);
    tft.print(enemy.name);

    // Enemy HP bar
    tft.drawRect(60, 50, 200, 16, ILI9341_WHITE);
    int enemyBar = map(enemy.hp, 0, enemy.maxHp, 0, 196);
    tft.fillRect(62, 52, enemyBar, 12, ILI9341_RED);

    // Enemy sprite placeholder
    tft.fillRect(130, 90, 60, 60, ILI9341_MAGENTA);

    // Player stat box
    tft.drawRect(20, 180, 280, 55, ILI9341_CYAN);

    // HP bar
    tft.setCursor(30, 190);
    tft.setTextColor(ILI9341_GREEN);
    tft.setTextSize(1);
    tft.print("HP");
    tft.drawRect(50, 190, 110, 8, ILI9341_WHITE);
    int hpBar = map(player.hp, 0, player.maxHp, 0, 106);
    tft.fillRect(52, 192, hpBar, 4, ILI9341_GREEN);

    // MP bar
    tft.setCursor(30, 205);
    tft.setTextColor(ILI9341_CYAN);
    tft.print("MP");
    tft.drawRect(50, 205, 110, 8, ILI9341_WHITE);
    int mpBar = map(player.mp, 0, player.maxMp, 0, 106);
    tft.fillRect(52, 207, mpBar, 4, ILI9341_BLUE);

    // Numeric stats
    char buf[32];
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(170, 188);
    sprintf(buf, "STR:%d DEF:%d", getAttack(), getDefence());
    tft.print(buf);
    tft.setCursor(170, 200);
    sprintf(buf, "ARC:%d", getArcane());
    tft.print(buf);
    tft.setCursor(170, 212);
    sprintf(buf, "HP:%d/%d", player.hp, player.maxHp);
    tft.print(buf);
    tft.setCursor(170, 224);
    sprintf(buf, "MP:%d/%d", player.mp, player.maxMp);
    tft.print(buf);
}

// ============================================================
// DRAW RPG MENU (LCD)
// ============================================================

void drawRPGMenu() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(">");
    lcd.print(actions[actionSel]);

    lcd.setCursor(0, 1);

    if (actionSel == 0) {
        // Show weapon equipped
        if (player.equippedWeapon >= 0)
            lcd.print(itemPool[player.equippedWeapon].name);
        else
            lcd.print("Bare hands");
    }

    if (actionSel == 1) {
        lcd.print("Potion x");
        lcd.print(player.potions);
    }

    if (actionSel == 2) {
        if (player.spellCount > 0) {
            int si = player.spells[combatSpellSel];
            lcd.print(itemPool[si].name);
        } else {
            lcd.print("No spells");
        }
    }

    if (actionSel == 3) {
        lcd.print("Run Away");
    }
}

// ============================================================
// COMBAT
// ============================================================

bool gameRPG(bool bossFight) {
    setupEnemy(bossFight);
    actionSel     = 0;
    combatSpellSel = 0;

    drawRPGScreen();
    drawRPGMenu();

    while (true) {

        // UP — scroll action menu up
        if (btnPressed(BTN_UP)) {
            waitRelease(BTN_UP);
            actionSel--;
            if (actionSel < 0) actionSel = 3;
            drawRPGMenu();
        }

        // DOWN — scroll action menu down
        if (btnPressed(BTN_DOWN)) {
            waitRelease(BTN_DOWN);
            actionSel++;
            if (actionSel > 3) actionSel = 0;
            drawRPGMenu();
        }

        // LEFT — cycle spell selection when on Magic action
        if (btnPressed(BTN_LEFT)) {
            waitRelease(BTN_LEFT);
            if (actionSel == 2 && player.spellCount > 1) {
                combatSpellSel = (combatSpellSel + 1) % player.spellCount;
                drawRPGMenu();
            }
        }

        // SELECT — confirm action
        if (btnPressed(BTN_SEL)) {
            waitRelease(BTN_SEL);

            bool tookAction = false;

            // ── ATTACK ──────────────────────────────────────
            if (actionSel == 0) {
                int dmg = random(getAttack() - 2, getAttack() + 4);

                // Crit — critChance above 1.0 = extra % chance
                float critBonus = getCritChance() - 1.0f;
                if (critBonus > 0.0f && (random(0, 100) < (int)(critBonus * 100))) {
                    dmg *= 2;
                    lcd.clear();
                    lcd.print("CRITICAL HIT!");
                    delay(600);
                }

                enemy.hp -= dmg;
                playTone(1000, 80);
                tookAction = true;
            }

            // ── ITEMS (potion) ───────────────────────────────
            if (actionSel == 1) {
                if (player.potions > 0) {
                    player.hp += 25;
                    if (player.hp > player.maxHp) player.hp = player.maxHp;
                    player.potions--;
                    playTone(700, 100);
                    tookAction = true;
                } else {
                    lcd.clear();
                    lcd.print("No potions!");
                    delay(600);
                }
            }

            // ── MAGIC ────────────────────────────────────────
            if (actionSel == 2) {
                if (player.spellCount > 0) {
                    int si  = player.spells[combatSpellSel];
                    int spellDmg  = itemPool[si].statBonus;
                    int spellCost = 0;

                    // MP costs match item description
                    if (strcmp(itemPool[si].name, "Fire Spell") == 0) spellCost = 5;
                    else if (strcmp(itemPool[si].name, "Ice Spell")  == 0) spellCost = 3;
                    else if (strcmp(itemPool[si].name, "Thunder")    == 0) spellCost = 8;

                    if (player.mp >= spellCost) {
                        player.mp  -= spellCost;
                        int finalDmg = spellDmg * getArcane();
                        enemy.hp   -= finalDmg;
                        playTone(1400, 120);
                        tookAction = true;
                    } else {
                        lcd.clear();
                        lcd.print("Not enough MP!");
                        delay(700);
                    }
                } else {
                    lcd.clear();
                    lcd.print("No spells!");
                    delay(600);
                }
            }

            // ── RUN ──────────────────────────────────────────
            if (actionSel == 3) {
                lcd.clear();
                lcd.print("Escaped!");
                delay(1000);
                return false;
            }

            if (!tookAction) continue;

            // Clamp enemy HP
            if (enemy.hp < 0) enemy.hp = 0;

            drawRPGScreen();
            drawRPGMenu();

            // Enemy dead
            if (enemy.hp <= 0) {
                lcd.clear();
                lcd.print("Enemy Down!");
                soundWin();
                delay(1500);
                return true;
            }

            // ── ENEMY TURN ───────────────────────────────────
            int rawDmg   = random(4, enemy.attack + 1);
            int enemyDmg = rawDmg - getDefence();
            if (enemyDmg < 1) enemyDmg = 1;

            player.hp -= enemyDmg;
            if (player.hp < 0) player.hp = 0;

            playTone(300, 60);

            drawRPGScreen();
            drawRPGMenu();

            // Player dead
            if (player.hp <= 0) {
                lcd.clear();
                lcd.print("You Died");
                delay(3000);
                return false;
            }
        }

        delay(20);
    }
}

// ============================================================
// DRAW DUNGEON MAP
// ============================================================

void drawDungeonMap(int selectedRoom) {
    tft.fillScreen(ILI9341_BLACK);

    int roomX[ROOM_COUNT] = {160,  90, 230,  50, 130, 190, 270, 160};
    int roomY[ROOM_COUNT] = { 20,  80,  80, 150, 150, 150, 150, 220};

    // Connections tier 0-1
    tft.drawLine(160, 20,  90, 80, ILI9341_RED);
    tft.drawLine(160, 20, 230, 80, ILI9341_RED);

    // Connections tier 1-2
    tft.drawLine( 90, 80,  50, 150, ILI9341_RED);
    tft.drawLine( 90, 80, 130, 150, ILI9341_RED);
    tft.drawLine(230, 80, 190, 150, ILI9341_RED);
    tft.drawLine(230, 80, 270, 150, ILI9341_RED);

    // All leaves → boss
    tft.drawLine( 50, 150, 160, 220, ILI9341_RED);
    tft.drawLine(130, 150, 160, 220, ILI9341_RED);
    tft.drawLine(190, 150, 160, 220, ILI9341_RED);
    tft.drawLine(270, 150, 160, 220, ILI9341_RED);

    for (int i = 0; i < ROOM_COUNT; i++) {
        uint16_t color = ILI9341_RED;

        if (rooms[i].cleared)
            color = ILI9341_GREEN;

        if (rooms[i].rewardType == REWARD_BOSS)
            color = ILI9341_MAGENTA;

        tft.fillCircle(roomX[i], roomY[i], 12, color);

        if (i == currentRoom)
            tft.drawCircle(roomX[i], roomY[i], 18, ILI9341_WHITE);

        if (i == selectedRoom)
            tft.drawCircle(roomX[i], roomY[i], 22, ILI9341_CYAN);
    }

    // Legend
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(5, 295);
    tft.print("LEFT=Inventory");
}

// ============================================================
// REWARD SYSTEM — pick 3 popup
// ============================================================

void pickRewardItems(int out[3]) {
    int candidates[ITEM_POOL_SIZE];
    int count = 0;

    for (int i = 0; i < ITEM_POOL_SIZE; i++)
        if (!itemPool[i].owned)
            candidates[count++] = i;

    // Fisher-Yates shuffle
    for (int i = count - 1; i > 0; i--) {
        int j = random(0, i + 1);
        int tmp      = candidates[i];
        candidates[i] = candidates[j];
        candidates[j] = tmp;
    }

    for (int i = 0; i < 3; i++)
        out[i] = (i < count) ? candidates[i] : -1;
}

void addItemToPlayer(int poolIdx) {
    if (poolIdx < 0) return;
    itemPool[poolIdx].owned = true;

    if (itemPool[poolIdx].type == ITEM_SPELL) {
        if (player.spellCount < 3)
            player.spells[player.spellCount++] = poolIdx;
    } else if (itemPool[poolIdx].type == ITEM_POTION) {
        player.potions++;
    } else {
        if (player.invCount < INV_SIZE)
            player.inventory[player.invCount++] = poolIdx;
    }
}

void giveRoomReward() {
    int choices[3];
    pickRewardItems(choices);

    int sel = 0;

    // Ensure at least one valid choice exists
    bool anyValid = false;
    for (int i = 0; i < 3; i++) if (choices[i] >= 0) { anyValid = true; break; }
    if (!anyValid) return;

    auto drawRewardPopup = [&]() {
        tft.fillScreen(ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_YELLOW);
        tft.setCursor(50, 10);
        tft.print("Choose Reward!");

        for (int i = 0; i < 3; i++) {
            if (choices[i] < 0) continue;
            uint16_t col = (i == sel) ? ILI9341_CYAN : ILI9341_WHITE;
            tft.setTextColor(col);
            tft.setTextSize(2);
            tft.setCursor(20, 60 + i * 55);
            tft.print((i == sel) ? "> " : "  ");
            tft.print(itemPool[choices[i]].name);
            tft.setTextSize(1);
            tft.setTextColor(ILI9341_MAGENTA);
            tft.setCursor(40, 80 + i * 55);
            tft.print(itemPool[choices[i]].description);
        }

        // LCD shows selected item info
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(itemPool[choices[sel]].name);
        lcd.setCursor(0, 1);
        lcd.print(itemPool[choices[sel]].description);
    };

    drawRewardPopup();

    while (true) {
        if (btnPressed(BTN_UP)) {
            waitRelease(BTN_UP);
            sel = (sel - 1 + 3) % 3;
            while (choices[sel] < 0) sel = (sel - 1 + 3) % 3;
            drawRewardPopup();
        }

        if (btnPressed(BTN_DOWN)) {
            waitRelease(BTN_DOWN);
            sel = (sel + 1) % 3;
            while (choices[sel] < 0) sel = (sel + 1) % 3;
            drawRewardPopup();
        }

        if (btnPressed(BTN_SEL)) {
            waitRelease(BTN_SEL);
            addItemToPlayer(choices[sel]);

            lcd.clear();
            lcd.print("Got:");
            lcd.setCursor(0, 1);
            lcd.print(itemPool[choices[sel]].name);
            playTone(1200, 120);
            delay(1200);
            return;
        }

        delay(20);
    }
}

// ============================================================
// INVENTORY SCREEN
// ============================================================

// Helper: count equippable items in inventory (not potions/spells)
int countEquippable() {
    int n = 0;
    for (int i = 0; i < player.invCount; i++) {
        int idx = player.inventory[i];
        if (idx < 0) continue;
        int t = itemPool[idx].type;
        if (t != ITEM_POTION && t != ITEM_SPELL) n++;
    }
    return n;
}

// Get inventory index of the Nth equippable item
int getEquippableInvIdx(int n) {
    int row = 0;
    for (int i = 0; i < player.invCount; i++) {
        int idx = player.inventory[i];
        if (idx < 0) continue;
        int t = itemPool[idx].type;
        if (t == ITEM_POTION || t == ITEM_SPELL) continue;
        if (row == n) return i;
        row++;
    }
    return -1;
}

void equipItem(int invIdx) {
    if (invIdx < 0 || invIdx >= player.invCount) return;
    int poolIdx = player.inventory[invIdx];
    if (poolIdx < 0) return;

    int type = itemPool[poolIdx].type;
    int* slot = nullptr;

    if      (type == ITEM_HELMET) slot = &player.equippedHelmet;
    else if (type == ITEM_ARMOUR) slot = &player.equippedArmour;
    else if (type == ITEM_BOOTS)  slot = &player.equippedBoots;
    else if (type == ITEM_WEAPON) slot = &player.equippedWeapon;
    else if (type == ITEM_RING)   slot = &player.equippedRing;

    if (!slot) return;

    int old = *slot;
    *slot = poolIdx;

    // Remove from inventory (compact)
    player.inventory[invIdx] = -1;
    int write = 0;
    for (int i = 0; i < player.invCount; i++)
        if (player.inventory[i] >= 0)
            player.inventory[write++] = player.inventory[i];
    player.invCount = write;

    // Put old item back into inventory
    if (old >= 0 && player.invCount < INV_SIZE)
        player.inventory[player.invCount++] = old;

    lcd.clear();
    lcd.print("Equipped!");
    lcd.setCursor(0, 1);
    lcd.print(itemPool[poolIdx].name);
    playTone(1000, 80);
    delay(800);
}

void drawInventoryTFT(int tab, int cursor) {
    tft.fillScreen(0x0319); // dark blue background

    // ── Tab bar ──────────────────────────────────────────────
    const char* tabs[] = {"Items", "Spells", "Equip"};
    for (int t = 0; t < 3; t++) {
        uint16_t col = (t == tab) ? ILI9341_YELLOW : ILI9341_WHITE;
        tft.setTextColor(col);
        tft.setTextSize(2);
        tft.setCursor(10 + t * 100, 5);
        tft.print(tabs[t]);
    }
    tft.drawFastHLine(0, 26, 320, ILI9341_WHITE);

    // ── Content ──────────────────────────────────────────────

    if (tab == 0) {
        // Items list
        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(5, 32);
        tft.print("INVENTORY  (UP/DOWN scroll, RIGHT=close)");

        if (player.invCount == 0) {
            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(10, 50);
            tft.print("Empty");
        }

        for (int i = 0; i < player.invCount && i < 9; i++) {
            int idx = player.inventory[i];
            if (idx < 0) continue;
            uint16_t col = (i == cursor) ? ILI9341_CYAN : ILI9341_WHITE;
            tft.setTextColor(col);
            tft.setCursor(10, 45 + i * 13);
            tft.print((i == cursor) ? ">" : " ");
            tft.print(itemPool[idx].name);
        }

        // Equipped summary
        tft.drawFastHLine(0, 170, 320, ILI9341_WHITE);
        tft.setTextColor(ILI9341_GREEN);
        tft.setTextSize(1);
        int ey = 175;

        auto printSlot = [&](const char* label, int slotIdx) {
            tft.setCursor(5, ey);
            tft.print(label);
            tft.print((slotIdx >= 0) ? itemPool[slotIdx].name : "---");
            ey += 12;
        };

        printSlot("Helm:   ", player.equippedHelmet);
        printSlot("Armour: ", player.equippedArmour);
        printSlot("Boots:  ", player.equippedBoots);
        printSlot("Weapon: ", player.equippedWeapon);
        printSlot("Ring:   ", player.equippedRing);
    }

    else if (tab == 1) {
        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(5, 32);
        tft.print("SPELLS KNOWN");

        if (player.spellCount == 0) {
            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(10, 50);
            tft.print("No spells yet");
        }

        for (int i = 0; i < player.spellCount; i++) {
            int idx = player.spells[i];
            if (idx < 0) continue;
            uint16_t col = (i == cursor) ? ILI9341_CYAN : ILI9341_WHITE;
            tft.setTextColor(col);
            tft.setCursor(10, 50 + i * 30);
            tft.print((i == cursor) ? "> " : "  ");
            tft.print(itemPool[idx].name);
            tft.setTextColor(ILI9341_MAGENTA);
            tft.setCursor(20, 62 + i * 30);
            tft.print(itemPool[idx].description);
        }
    }

    else if (tab == 2) {
        tft.setTextColor(ILI9341_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(5, 32);
        tft.print("EQUIP ITEM  (SEL to equip)");

        int row = 0;
        for (int i = 0; i < player.invCount; i++) {
            int idx = player.inventory[i];
            if (idx < 0) continue;
            int t = itemPool[idx].type;
            if (t == ITEM_POTION || t == ITEM_SPELL) continue;

            uint16_t col = (row == cursor) ? ILI9341_CYAN : ILI9341_WHITE;
            tft.setTextColor(col);
            tft.setCursor(10, 45 + row * 16);
            tft.print((row == cursor) ? ">" : " ");
            tft.print(itemPool[idx].name);
            tft.setTextColor(ILI9341_MAGENTA);
            tft.print("  ");
            tft.print(itemPool[idx].description);
            row++;
        }

        if (row == 0) {
            tft.setTextColor(ILI9341_WHITE);
            tft.setCursor(10, 60);
            tft.print("No equippable items");
        }
    }

    // ── Stat bar ─────────────────────────────────────────────
    tft.drawFastHLine(0, 282, 320, ILI9341_WHITE);
    tft.setTextSize(1);

    char buf[48];
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(5, 287);
    sprintf(buf, "HP:%d/%d  MP:%d/%d  Potions:%d",
            player.hp, player.maxHp, player.mp, player.maxMp, player.potions);
    tft.print(buf);

    tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(5, 300);
    sprintf(buf, "STR:%d DEF:%d ARC:%d CRIT:%.2f",
            getAttack(), getDefence(), getArcane(), getCritChance());
    tft.print(buf);
}

void drawInventoryLCD(int tab, int cursor) {
    lcd.clear();

    if (tab == 0) {
        if (player.invCount > 0 && cursor < player.invCount) {
            int idx = player.inventory[cursor];
            if (idx >= 0) {
                lcd.setCursor(0, 0);
                lcd.print(itemPool[idx].name);
                lcd.setCursor(0, 1);
                lcd.print(itemPool[idx].description);
                return;
            }
        }
        lcd.print("Empty inventory");
        lcd.setCursor(0, 1);
        lcd.print("RIGHT=close");
    }

    else if (tab == 1) {
        if (player.spellCount > 0 && cursor < player.spellCount) {
            int idx = player.spells[cursor];
            lcd.setCursor(0, 0);
            lcd.print(itemPool[idx].name);
            lcd.setCursor(0, 1);
            lcd.print(itemPool[idx].description);
        } else {
            lcd.print("No spells yet");
            lcd.setCursor(0, 1);
            lcd.print("RIGHT=close");
        }
    }

    else if (tab == 2) {
        int eqCount = countEquippable();
        if (eqCount > 0 && cursor < eqCount) {
            int invIdx  = getEquippableInvIdx(cursor);
            int poolIdx = (invIdx >= 0) ? player.inventory[invIdx] : -1;
            if (poolIdx >= 0) {
                lcd.setCursor(0, 0);
                lcd.print(itemPool[poolIdx].name);
                lcd.setCursor(0, 1);
                lcd.print("SEL=equip");
                return;
            }
        }
        lcd.print("No equip items");
        lcd.setCursor(0, 1);
        lcd.print("RIGHT=close");
    }
}

void openInventory() {
    int tab    = 0;
    int cursor = 0;

    drawInventoryTFT(tab, cursor);
    drawInventoryLCD(tab, cursor);

    while (true) {

        // RIGHT = close
        if (btnPressed(BTN_RIGHT)) {
            waitRelease(BTN_RIGHT);
            return;
        }

        // LEFT = previous tab
        if (btnPressed(BTN_LEFT)) {
            waitRelease(BTN_LEFT);
            tab    = (tab - 1 + 3) % 3;
            cursor = 0;
            drawInventoryTFT(tab, cursor);
            drawInventoryLCD(tab, cursor);
        }

        // DOWN = next tab (when UP/DOWN have no items to scroll)
        // or scroll within tab
        if (btnPressed(BTN_UP)) {
            waitRelease(BTN_UP);
            if (cursor > 0) cursor--;
            drawInventoryTFT(tab, cursor);
            drawInventoryLCD(tab, cursor);
        }

        if (btnPressed(BTN_DOWN)) {
            waitRelease(BTN_DOWN);
            int maxCursor = 0;
            if (tab == 0) maxCursor = max(0, player.invCount - 1);
            if (tab == 1) maxCursor = max(0, player.spellCount - 1);
            if (tab == 2) maxCursor = max(0, countEquippable() - 1);

            if (cursor < maxCursor) cursor++;
            else {
                // Wrap to next tab
                tab    = (tab + 1) % 3;
                cursor = 0;
            }
            drawInventoryTFT(tab, cursor);
            drawInventoryLCD(tab, cursor);
        }

        // SELECT = equip (only active on Equip tab)
        if (btnPressed(BTN_SEL)) {
            waitRelease(BTN_SEL);

            if (tab == 2) {
                int invIdx = getEquippableInvIdx(cursor);
                if (invIdx >= 0) {
                    equipItem(invIdx);
                    cursor = 0;
                    drawInventoryTFT(tab, cursor);
                    drawInventoryLCD(tab, cursor);
                }
            }
        }

        delay(20);
    }
}

// ============================================================
//  SAVE SYSTEM — STM32 HAL Flash (Nucleo-G071RB)
//  + DUNGEON LOOP after boss defeat
//
//  G071RB Flash: 128KB, page size = 2KB, 64 pages
//  We use the LAST 3 pages (pages 61, 62, 63) for saves
//  so we never overlap program code.
//
//  Page 61 = Save Slot 0
//  Page 62 = Save Slot 1
//  Page 63 = Save Slot 2
//
//  Each page = 2048 bytes, our SaveData struct is well under that.
//
//  IMPORTANT: Add to your includes at top of main sketch:
//    #include "stm32g0xx_hal.h"
//    #include "stm32g0xx_hal_flash.h"
// ============================================================

// ============================================================
// FLASH ADDRESSES
// ============================================================

#define FLASH_BASE_ADDR     0x08000000UL
#define FLASH_PAGE_SIZE     2048UL
#define FLASH_TOTAL_PAGES   64

// Last 3 pages reserved for saves
#define SAVE_PAGE_0   61
#define SAVE_PAGE_1   62
#define SAVE_PAGE_2   63
#define SAVE_SLOT_COUNT 3

#define SAVE_MAGIC 0xDEADBEEF  // validity check

uint32_t getSaveAddress(int slot) {
    int page = SAVE_PAGE_0 + slot;
    return FLASH_BASE_ADDR + (page * FLASH_PAGE_SIZE);
}

// ============================================================
// SAVE DATA STRUCT
// ============================================================
// Everything needed to fully restore a game session.
// Keep it packed to stay well under 2KB page size.

struct SaveData {
    uint32_t magic;          // must equal SAVE_MAGIC to be valid

    // Player core
    int hp;
    int maxHp;
    int mp;
    int maxMp;
    int baseAttack;
    int baseDefence;
    float critChance;
    int arcane;
    int potions;

    // Equipment slots
    int equippedHelmet;
    int equippedArmour;
    int equippedBoots;
    int equippedWeapon;
    int equippedRing;

    // Spells
    int spells[3];
    int spellCount;

    // Inventory
    int inventory[INV_SIZE];
    int invCount;

    // Item pool ownership (which items have been picked up ever)
    bool itemOwned[ITEM_POOL_SIZE];

    // Meta
    int  dungeonRun;    // how many times boss has been beaten
    char timestamp[20]; // simple run counter label e.g. "Run 3"
};

// ============================================================
// FLASH WRITE HELPERS
// ============================================================

// Erase a single flash page
bool flashErasePage(int pageNum) {
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef eraseInit;
    eraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
    eraseInit.Page        = pageNum;
    eraseInit.NbPages     = 1;

    uint32_t pageError = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &pageError);

    HAL_FLASH_Lock();

    return (status == HAL_OK && pageError == 0xFFFFFFFF);
}

// Write a SaveData struct to flash at given page
// STM32G0 requires 8-byte (double-word) aligned writes
bool flashWriteSave(int slot, const SaveData& data) {
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return false;

    int      pageNum = SAVE_PAGE_0 + slot;
    uint32_t addr    = getSaveAddress(slot);

    if (!flashErasePage(pageNum)) return false;

    HAL_FLASH_Unlock();

    const uint8_t* src    = (const uint8_t*)&data;
    uint32_t       length = sizeof(SaveData);

    // Pad to 8-byte boundary
    uint32_t paddedLen = (length + 7) & ~7UL;
    uint8_t  padded[paddedLen];
    memset(padded, 0xFF, paddedLen);
    memcpy(padded, src, length);

    bool ok = true;
    for (uint32_t i = 0; i < paddedLen; i += 8) {
        uint64_t dword;
        memcpy(&dword, padded + i, 8);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              addr + i, dword) != HAL_OK) {
            ok = false;
            break;
        }
    }

    HAL_FLASH_Lock();
    return ok;
}

// Read a SaveData struct from flash
bool flashReadSave(int slot, SaveData& data) {
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return false;

    uint32_t addr = getSaveAddress(slot);
    memcpy(&data, (const void*)addr, sizeof(SaveData));

    return (data.magic == SAVE_MAGIC);
}

// Wipe a save slot (erase page, magic becomes 0xFF... = invalid)
bool flashWipeSave(int slot) {
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return false;
    return flashErasePage(SAVE_PAGE_0 + slot);
}

// ============================================================
// SAVE / LOAD GAME STATE
// ============================================================

void saveGame(int slot) {
    SaveData data;
    memset(&data, 0, sizeof(data));

    data.magic       = SAVE_MAGIC;
    data.hp          = player.hp;
    data.maxHp       = player.maxHp;
    data.mp          = player.mp;
    data.maxMp       = player.maxMp;
    data.baseAttack  = player.baseAttack;
    data.baseDefence = player.baseDefence;
    data.critChance  = player.critChance;
    data.arcane      = player.arcane;
    data.potions     = player.potions;

    data.equippedHelmet = player.equippedHelmet;
    data.equippedArmour = player.equippedArmour;
    data.equippedBoots  = player.equippedBoots;
    data.equippedWeapon = player.equippedWeapon;
    data.equippedRing   = player.equippedRing;

    data.spellCount = player.spellCount;
    for (int i = 0; i < 3; i++)
        data.spells[i] = player.spells[i];

    data.invCount = player.invCount;
    for (int i = 0; i < INV_SIZE; i++)
        data.inventory[i] = player.inventory[i];

    for (int i = 0; i < ITEM_POOL_SIZE; i++)
        data.itemOwned[i] = itemPool[i].owned;

    data.dungeonRun = dungeonRun;

    // Label e.g. "Run 4"
    snprintf(data.timestamp, 20, "Run %d", dungeonRun);

    lcd.clear();
    if (flashWriteSave(slot, data)) {
        lcd.print("Saved! Slot ");
        lcd.print(slot + 1);
    } else {
        lcd.print("Save FAILED");
    }
    delay(1200);
}

void loadGame(int slot, SaveData& data) {
    player.hp          = data.hp;
    player.maxHp       = data.maxHp;
    player.mp          = data.mp;
    player.maxMp       = data.maxMp;
    player.baseAttack  = data.baseAttack;
    player.baseDefence = data.baseDefence;
    player.critChance  = data.critChance;
    player.arcane      = data.arcane;
    player.potions     = data.potions;

    player.equippedHelmet = data.equippedHelmet;
    player.equippedArmour = data.equippedArmour;
    player.equippedBoots  = data.equippedBoots;
    player.equippedWeapon = data.equippedWeapon;
    player.equippedRing   = data.equippedRing;

    player.spellCount = data.spellCount;
    for (int i = 0; i < 3; i++)
        player.spells[i] = data.spells[i];

    player.invCount = data.invCount;
    for (int i = 0; i < INV_SIZE; i++)
        player.inventory[i] = data.inventory[i];

    for (int i = 0; i < ITEM_POOL_SIZE; i++)
        itemPool[i].owned = data.itemOwned[i];

    dungeonRun = data.dungeonRun;
}

// ============================================================
// SAVE SLOT SCREEN
// ============================================================

void drawSaveScreen(int cursor) {
    tft.fillScreen(ILI9341_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(70, 10);
    tft.print("SELECT SAVE");

    tft.drawFastHLine(0, 32, 320, ILI9341_WHITE);

    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        SaveData data;
        bool valid = flashReadSave(i, data);

        uint16_t col = (i == cursor) ? ILI9341_CYAN : ILI9341_WHITE;
        tft.setTextColor(col);
        tft.setTextSize(2);
        tft.setCursor(20, 50 + i * 70);

        if (i == cursor) tft.print("> ");
        else             tft.print("  ");

        tft.print("Slot ");
        tft.print(i + 1);
        tft.print(": ");

        if (valid) {
            tft.print(data.timestamp);
            // Stats summary
            tft.setTextSize(1);
            tft.setTextColor(ILI9341_GREEN);
            tft.setCursor(40, 68 + i * 70);
            char buf[40];
            sprintf(buf, "HP:%d/%d  STR:%d  DEF:%d  Run:%d",
                    data.hp, data.maxHp,
                    data.baseAttack, data.baseDefence,
                    data.dungeonRun);
            tft.print(buf);
        } else {
            tft.setTextColor(ILI9341_RED);
            tft.print("Empty");
        }
    }

    // Bottom hint
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(5, 295);
    tft.print("SEL=Load/New  RIGHT=Wipe slot");
}

void drawSaveLCD(int cursor) {
    SaveData data;
    bool valid = flashReadSave(cursor, data);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Slot ");
    lcd.print(cursor + 1);
    lcd.print(": ");
    lcd.print(valid ? data.timestamp : "Empty");
    lcd.setCursor(0, 1);
    lcd.print(valid ? "SEL=Load RIGHT=Wipe" : "SEL=New game");
}

// ============================================================
// STARTUP SAVE SELECTION SCREEN
// ============================================================
// Returns true if a game was loaded/started, false to exit.

bool saveSelectScreen() {
    int cursor = 0;

    drawSaveScreen(cursor);
    drawSaveLCD(cursor);

    while (true) {

        if (btnPressed(BTN_UP)) {
            waitRelease(BTN_UP);
            cursor = (cursor - 1 + SAVE_SLOT_COUNT) % SAVE_SLOT_COUNT;
            drawSaveScreen(cursor);
            drawSaveLCD(cursor);
        }

        if (btnPressed(BTN_DOWN)) {
            waitRelease(BTN_DOWN);
            cursor = (cursor + 1) % SAVE_SLOT_COUNT;
            drawSaveScreen(cursor);
            drawSaveLCD(cursor);
        }

        // RIGHT = wipe slot
        if (btnPressed(BTN_RIGHT)) {
            waitRelease(BTN_RIGHT);

            SaveData check;
            if (!flashReadSave(cursor, check)) {
                lcd.clear();
                lcd.print("Already empty!");
                delay(800);
            } else {
                // Confirm wipe
                lcd.clear();
                lcd.print("Wipe slot ");
                lcd.print(cursor + 1);
                lcd.print("?");
                lcd.setCursor(0, 1);
                lcd.print("SEL=Yes  UP=Cancel");

                bool waiting = true;
                while (waiting) {
                    if (btnPressed(BTN_SEL)) {
                        waitRelease(BTN_SEL);
                        flashWipeSave(cursor);
                        lcd.clear();
                        lcd.print("Slot ");
                        lcd.print(cursor + 1);
                        lcd.print(" wiped");
                        delay(1000);
                        waiting = false;
                    }
                    if (btnPressed(BTN_UP)) {
                        waitRelease(BTN_UP);
                        lcd.clear();
                        lcd.print("Cancelled");
                        delay(600);
                        waiting = false;
                    }
                    delay(20);
                }
            }

            drawSaveScreen(cursor);
            drawSaveLCD(cursor);
        }

        // SELECT = load existing or start new
        if (btnPressed(BTN_SEL)) {
            waitRelease(BTN_SEL);

            SaveData data;
            bool valid = flashReadSave(cursor, data);

            if (valid) {
                // Load save into player
                loadGame(cursor, data);
                lcd.clear();
                lcd.print("Loaded slot ");
                lcd.print(cursor + 1);
                delay(1000);
            } else {
                // New game
                initPlayer();
                dungeonRun = 1;
                lcd.clear();
                lcd.print("New Game!");
                lcd.setCursor(0, 1);
                lcd.print("Slot ");
                lcd.print(cursor + 1);
                delay(1000);
            }

            // Store which slot we're using so we can auto-save
            activeSaveSlot = cursor;
            return true;
        }

        delay(20);
    }
}

// ============================================================
// DUNGEON LOOP  (replaces old gameDungeon body)
// ============================================================
// After boss is beaten: save, increment run, reset dungeon,
// keep all player gear — then loop back into the dungeon.

void gameDungeon() {

    // Save select screen shown first (before dungeon starts)
    if (!saveSelectScreen()) return;

    while (true) {   // outer loop — repeats each dungeon run

        initDungeon(); // resets rooms only, player untouched

        int selectedRoom = 1;

        drawDungeonMap(selectedRoom);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Run ");
        lcd.print(dungeonRun);
        lcd.setCursor(0, 1);
        lcd.print("LEFT=Inventory");

        bool gameOver = false;

        while (true) {   // inner loop — one dungeon run

            // ── Room selection ────────────────────────────────

            if (btnPressed(BTN_UP)) {
                waitRelease(BTN_UP);
                selectedRoom = (selectedRoom + 1) % ROOM_COUNT;
                drawDungeonMap(selectedRoom);
                lcd.clear();
                lcd.print("Run ");
                lcd.print(dungeonRun);
                lcd.setCursor(0, 1);
                lcd.print("LEFT=Inventory");
            }

            if (btnPressed(BTN_DOWN)) {
                waitRelease(BTN_DOWN);
                selectedRoom = (selectedRoom - 1 + ROOM_COUNT) % ROOM_COUNT;
                drawDungeonMap(selectedRoom);
                lcd.clear();
                lcd.print("Run ");
                lcd.print(dungeonRun);
                lcd.setCursor(0, 1);
                lcd.print("LEFT=Inventory");
            }

            // ── Inventory ─────────────────────────────────────

            if (btnPressed(BTN_LEFT)) {
                waitRelease(BTN_LEFT);
                openInventory();
                drawDungeonMap(selectedRoom);
                lcd.clear();
                lcd.print("Run ");
                lcd.print(dungeonRun);
                lcd.setCursor(0, 1);
                lcd.print("LEFT=Inventory");
            }

            // ── Enter room ────────────────────────────────────

            if (btnPressed(BTN_SEL)) {
                waitRelease(BTN_SEL);

                if (!roomLinks[currentRoom][selectedRoom]) {
                    lcd.clear();
                    lcd.print("No Path!");
                    playTone(300, 100);
                    delay(700);
                    drawDungeonMap(selectedRoom);
                    lcd.clear();
                    lcd.print("Run ");
                    lcd.print(dungeonRun);
                    lcd.setCursor(0, 1);
                    lcd.print("LEFT=Inventory");
                    continue;
                }

                if (rooms[selectedRoom].cleared) {
                    lcd.clear();
                    lcd.print("Already cleared");
                    delay(700);
                    drawDungeonMap(selectedRoom);
                    lcd.clear();
                    lcd.print("Run ");
                    lcd.print(dungeonRun);
                    lcd.setCursor(0, 1);
                    lcd.print("LEFT=Inventory");
                    continue;
                }

                currentRoom = selectedRoom;

                bool bossFight = (rooms[currentRoom].rewardType == REWARD_BOSS);
                bool won       = gameRPG(bossFight);

                // ── Player died ───────────────────────────────
                if (player.hp <= 0) {
                    tft.fillScreen(ILI9341_BLACK);
                    tft.setTextSize(3);
                    tft.setTextColor(ILI9341_RED);
                    tft.setCursor(60, 90);
                    tft.print("GAME OVER");
                    tft.setTextSize(2);
                    tft.setTextColor(ILI9341_WHITE);
                    tft.setCursor(50, 140);
                    tft.print("Run: ");
                    tft.print(dungeonRun);

                    lcd.clear();
                    lcd.print("You have fallen.");
                    lcd.setCursor(0, 1);
                    lcd.print("Slot wiped.");

                    // Wipe save on death — roguelike style
                    flashWipeSave(activeSaveSlot);

                    delay(4000);
                    gameOver = true;
                    break;
                }

                // ── Won combat ────────────────────────────────
                if (won) {
                    rooms[currentRoom].cleared = true;

                    if (!bossFight) {
                        giveRoomReward();
                    } else {
                        // ── BOSS DEFEATED ─────────────────────
                        tft.fillScreen(ILI9341_BLACK);
                        tft.setTextSize(3);
                        tft.setTextColor(ILI9341_GREEN);
                        tft.setCursor(60, 80);
                        tft.print("YOU WIN!");
                        tft.setTextSize(2);
                        tft.setTextColor(ILI9341_YELLOW);
                        tft.setCursor(40, 130);
                        tft.print("Run ");
                        tft.print(dungeonRun);
                        tft.print(" Complete!");
                        tft.setTextSize(1);
                        tft.setTextColor(ILI9341_WHITE);
                        tft.setCursor(30, 170);
                        tft.print("Dungeon resets. Gear kept.");

                        soundWin();

                        lcd.clear();
                        lcd.print("Boss defeated!");
                        lcd.setCursor(0, 1);
                        lcd.print("Saving...");

                        delay(2500);

                        // Increment run counter and auto-save
                        dungeonRun++;
                        saveGame(activeSaveSlot);

                        // Break inner loop → outer loop resets dungeon
                        break;
                    }
                }

                drawDungeonMap(selectedRoom);
                lcd.clear();
                lcd.print("Run ");
                lcd.print(dungeonRun);
                lcd.setCursor(0, 1);
                lcd.print("LEFT=Inventory");
            }

            delay(20);
        }  // end inner while

        if (gameOver) break;  // exit outer loop on death

        // Otherwise outer loop continues — dungeon resets,
        // player keeps everything, run counter incremented

        // Brief transition screen
        tft.fillScreen(ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(ILI9341_CYAN);
        tft.setCursor(40, 100);
        tft.print("Entering Run ");
        tft.print(dungeonRun);
        tft.setCursor(30, 130);
        tft.setTextColor(ILI9341_WHITE);
        tft.print("Dungeon Reborn...");

        lcd.clear();
        lcd.print("Run ");
        lcd.print(dungeonRun);
        lcd.setCursor(0, 1);
        lcd.print("Good luck!");

        delay(2500);

    }  // end outer while
}


// ============================================================
//  SETUP & LOOP
// ============================================================
void setup() {
    // Buttons
    pinMode(BTN_UP,   INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SEL,  INPUT_PULLUP);
    pinMode(BTN_RIGHT,  INPUT_PULLUP);
    pinMode(BTN_LEFT,  INPUT_PULLUP);
    pinMode(BUZZER,   OUTPUT);

    SPI.begin();
    tft.begin();
    tft.setRotation(1);
    
    delay(10);
    tft.fillScreen(ILI9341_BLACK);

    // ── WHK logo — stacked, centred ──
    tft.setTextSize(5);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(136, 30);  tft.print("W");
    tft.setCursor(136, 70);  tft.print("H");
    tft.setCursor(136, 110); tft.print("K");


    // LCD
      lcd.begin(16, 2);
      lcd.setCursor(0, 0);
      lcd.print("WHK Game Console");

    randomSeed(analogRead(A3));

    // Boot jingle
    playTone(800, 80); delay(100);
    playTone(1000, 80); delay(100);
    playTone(1200, 120);
}

void loop() {
    int choice = mainMenu();

    switch (choice) {
        case 0: gamePong();        break;
        case 1: gameBunnyJumper(); break;
        case 2: gameRiskyRoad();   break;
        case 3: gameRhythm(); break;
        case 4: gameDungeon(); break;
    }

    // Small pause before re-entering menu
    delay(500);
}