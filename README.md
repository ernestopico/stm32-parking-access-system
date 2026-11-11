# STM32 Parking Access Control System

A bare-metal C parking counter for the **NUCLEO-F446RE** (STM32F446RE, ARM Cortex-M4). Two push buttons track cars entering and leaving, a seven-segment display shows how many cars are inside (0–9), and green/red LEDs show whether there is space left.

Written with **direct register access only**: no HAL, no CMSIS headers, no interrupts. Every peripheral is configured through its memory-mapped address.

<!-- Demo GIF goes here: ![Demo](media/demo.gif) -->

## Features

- **Car counter (0–9):** SW1 adds a car, SW2 removes one. The count stops at 9 and never goes below 0.
- **Status LEDs:** green while spaces are available (0–8), red when the lot is full (9).
- **Non-blocking 1 ms time base:** SysTick is polled through its `COUNTFLAG` bit instead of using busy-wait delay loops.
- **Software debounce:** a button's new state is accepted only after it stays stable for 20 ms, so contact bounce is ignored.
- **Edge detection:** a press counts once, no matter how long the button is held.

## Hardware

| Component | Details |
|---|---|
| Board | NUCLEO-F446RE (STM32F446RE, 16 MHz HSI) |
| Display | Common-cathode seven-segment display |
| SW1 (car enters) | On-board blue button, PC13 |
| SW2 (car leaves) | External push button, PA10, 4.7 kΩ pull-up |
| LEDs | Green and red, with current-limiting resistors |

### Wiring

| Signal | MCU pin | Nucleo header |
|---|---|---|
| Segment A | PA0 | A0 |
| Segment B | PA1 | A1 |
| Segment C | PB0 | A3 |
| Segment D | PC1 | A4 |
| Segment E | PA4 | A2 |
| Segment F | PA5 | D13 |
| Segment G | PA6 | D12 |
| Green LED | PA8 | D7 |
| Red LED | PA9 | D8 |
| SW2 | PA10 | D2 |
| SW1 | PC13 | On-board (B1) |

PA2 and PA3 are skipped on purpose: on the Nucleo board they are connected to the ST-LINK virtual COM port by default and are not available on the headers. Segments C and D were moved to PB0 and PC1 instead.

## How it works

1. **Clock and GPIO setup:** enables the GPIOA, GPIOB and GPIOC clocks in `RCC_AHB1ENR`, sets the segment and LED pins to output in each `MODER` register, and sets PA10 and PC13 to input.
2. **SysTick:** loaded with `16000 - 1` so it reaches zero once every millisecond at 16 MHz. The main loop only does work when `COUNTFLAG` is set.
3. **Debounce:** every millisecond each button's raw level is compared with its last accepted state. A change has to hold for 20 consecutive samples before it is accepted, and only the released → pressed edge returns a press.
4. **Display:** each digit is stored as a 7-bit segment pattern. Segments on GPIOA are written in a single `ODR` update, and segments C and D are routed to PB0 and PC1.

## Build and flash

1. In **STM32CubeIDE**, create a new **STM32 Project** for the **NUCLEO-F446RE** board, choosing the **Empty** project type.
2. Replace the generated `Src/main.c` with [`Src/main.c`](Src/main.c) from this repo.
3. Build, connect the board over USB, and run it.

## Project structure

```
.
├── Src/
│   └── main.c     # Full program: register definitions, GPIO, SysTick, debounce, display
├── LICENSE
└── README.md
```

## Concepts used

Memory-mapped I/O · RCC clock gating · GPIO MODER/IDR/ODR · SysTick timer · Software debouncing · Seven-segment encoding · Bit masking

## License

[MIT](LICENSE)
