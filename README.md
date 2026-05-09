# WHK_ConsoleSoftware

_**NOTICE: This uses the 5 libraries that can be seen at the top of the main.cpp file most importantly Adafruit ones as that allows grahpical use of the ILI9341.**_

This was developed on the Nucleo-GO71RB stm32 on VS studio with platformIO and tested on the ILI9341 LCD and a generic 2 line LCD on a bread board.
It hosts a software that for a project that works like a handheld console making use of both the screen and some extra components (e.g. a speak and IR sensors), the games are:

  -PONG
  
  -Bunny Jumper
  
  -Risky Road
  
  -IR Rhythm
  
  -RPG
  
-Settings (Not a game obviously)

The overall software has some "Jank" and "Kinks" due to it being a 1st Year Uni project but overall runs as intended which each game attempting to make full use of compents despite some limitations of the light weight hardware.

// ============================================================

//  Unified Game Console  –  Nucleo-G071RB
//  Display 1 : ILI9341 TFT (320×240, landscape)  
//  Display 2 : 16×2 character LCD              
//  Framework : Arduino (PlatformIO / STM32duino)

// ============================================================


//  WIRING SUMMARY

//  ─────────────────────────────────────────────────────────

_**TFT ILI9341**_

//    CS   → D10   DC  → D8    RST → D9

//    MOSI → D11  MISO→ D12  SCK → D13   (hardware SPI)

_**16×2 LCD (4-bit parallel)**_

//    RS → D2   EN → D3

//    D4 → D4   D5 → D5   D6 → D6   D7 → D7


_**Buttons**_

//    BTN_UP   → A0  (INPUT_PULLUP)

//    BTN_DOWN → A1  (INPUT_PULLUP)

//    BTN_SEL  → A2  (INPUT_PULLUP)   ←  "select" button

//    BTN_LEFT → A4     (INPUT_PULLUP)

//    BTN_RIGHT → D15   (INPUT_PULLUP)

//  Buzzer   → D5   (PWM capable)

// ============================================================

<img width="513" height="486" alt="image" src="https://github.com/user-attachments/assets/fa9ba745-4485-4b6a-8321-e2c17fcb66c7" />

<img width="472" height="530" alt="image" src="https://github.com/user-attachments/assets/8a1ff19d-8c30-4b1b-a830-749333f5039e" />

<img width="392" height="430" alt="image" src="https://github.com/user-attachments/assets/8bf5a3b6-00ea-4075-bb3b-d509ff90b2ad" />

<img width="388" height="438" alt="image" src="https://github.com/user-attachments/assets/332cc249-599b-41ba-8042-0624bd73f3c4" />




