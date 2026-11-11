/*
 * Parking Access Control System
 * Target: NUCLEO-F446RE (bare-metal, register-level C)
 * Clock:  16 MHz HSI (reset default)
 *
 * Inputs (active low: pressed = 0)
 *   SW1 (car enters) = PC13, on-board blue button
 *   SW2 (car leaves) = PA10, external 4.7 k pull-up
 *
 * Outputs
 *   Seven-segment display, common cathode:
 *     A=PA0(A0)  B=PA1(A1)  C=PB0(A3)  D=PC1(A4)
 *     E=PA4(A2)  F=PA5(D13) G=PA6(D12)
 *     (C and D are moved from PA2/PA3, which are not available on the
 *     headers by default because they are tied to the ST-LINK USART.)
 *   Green LED = PA8, ON for counts 0 to 8
 *   Red LED   = PA9, ON at count 9
 */

#include <stdint.h>

/* RCC */
#define RCC_AHB1ENR   (*(volatile uint32_t *)0x40023830U)

/* GPIOA */
#define GPIOA_MODER   (*(volatile uint32_t *)0x40020000U)
#define GPIOA_IDR     (*(volatile uint32_t *)0x40020010U)
#define GPIOA_ODR     (*(volatile uint32_t *)0x40020014U)

/* GPIOB */
#define GPIOB_MODER   (*(volatile uint32_t *)0x40020400U)
#define GPIOB_ODR     (*(volatile uint32_t *)0x40020414U)

/* GPIOC */
#define GPIOC_MODER   (*(volatile uint32_t *)0x40020800U)
#define GPIOC_IDR     (*(volatile uint32_t *)0x40020810U)
#define GPIOC_ODR     (*(volatile uint32_t *)0x40020814U)

/* SysTick: used only as a non-blocking 1 ms time base */
#define SYST_CSR      (*(volatile uint32_t *)0xE000E010U)
#define SYST_RVR      (*(volatile uint32_t *)0xE000E014U)
#define SYST_CVR      (*(volatile uint32_t *)0xE000E018U)

#define SYST_ENABLE     (1U << 0)
#define SYST_CLKSOURCE  (1U << 2)   /* use the processor clock */
#define SYST_COUNTFLAG  (1U << 16)  /* set when the counter reaches 0 */

/* RCC clock enable bits */
#define GPIOAEN       (1U << 0)
#define GPIOBEN       (1U << 1)
#define GPIOCEN       (1U << 2)

/*
 * Segment bits used in the digit patterns. A, B, E, F and G match their
 * GPIOA pin numbers. C and D are bit 2 and bit 3 of the pattern only:
 * update_outputs() sends them to PB0 and PC1.
 */
#define SEG_A         (1U << 0)
#define SEG_B         (1U << 1)
#define SEG_C         (1U << 2)
#define SEG_D         (1U << 3)
#define SEG_E         (1U << 4)
#define SEG_F         (1U << 5)
#define SEG_G         (1U << 6)
#define SEG_ALL       (SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G)

/* Physical pin masks */
#define PORTA_SEGS    (SEG_A | SEG_B | SEG_E | SEG_F | SEG_G)
#define PB0_MASK      (1U << 0)     /* segment C */
#define PC1_MASK      (1U << 1)     /* segment D */

/* LED and button bit masks */
#define LED_GREEN     (1U << 8)
#define LED_RED       (1U << 9)
#define SW1_MASK      (1U << 13)    /* PC13 */
#define SW2_MASK      (1U << 10)    /* PA10 */

#define MAX_COUNT     9U
#define DEBOUNCE_MS   20U

/* Debounce state for one button */
typedef struct {
    uint8_t stable;   /* accepted state: 1 = released, 0 = pressed */
    uint8_t timer;    /* ms the raw reading has differed from stable */
} Button;

/* Segment patterns for digits 0 to 9 */
static const uint32_t digits[10] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,          /* 0 */
    SEG_B | SEG_C,                                          /* 1 */
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                  /* 2 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                  /* 3 */
    SEG_B | SEG_C | SEG_F | SEG_G,                          /* 4 */
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                  /* 5 */
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,          /* 6 */
    SEG_A | SEG_B | SEG_C,                                  /* 7 */
    SEG_ALL,                                                /* 8 */
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G           /* 9 */
};

static void gpio_init(void)
{
    /* Each pin has a 2-bit MODER field: 01 = output */
    static const uint8_t output_pins[] = {0, 1, 4, 5, 6, 8, 9};

    RCC_AHB1ENR |= GPIOAEN | GPIOBEN | GPIOCEN;

    for (uint32_t i = 0; i < sizeof(output_pins); i++) {
        uint32_t pin = output_pins[i];
        GPIOA_MODER = (GPIOA_MODER & ~(3U << (pin * 2U))) | (1U << (pin * 2U));
    }

    /* PB0 (segment C) and PC1 (segment D) are outputs */
    GPIOB_MODER = (GPIOB_MODER & ~(3U << (0U * 2U))) | (1U << (0U * 2U));
    GPIOC_MODER = (GPIOC_MODER & ~(3U << (1U * 2U))) | (1U << (1U * 2U));

    /* 00 = input. PA10 and PC13 both use external pull-ups. */
    GPIOA_MODER &= ~(3U << (10U * 2U));
    GPIOC_MODER &= ~(3U << (13U * 2U));
}

static void systick_init(void)
{
    SYST_RVR = 16000U - 1U;     /* 16 MHz / 16000 = 1 kHz = 1 ms period */
    SYST_CVR = 0U;
    SYST_CSR = SYST_CLKSOURCE | SYST_ENABLE;
}

/* Writes the digit to GPIOA, PB0 and PC1, and sets the LEDs */
static void update_outputs(uint32_t count)
{
    uint32_t led = (count == MAX_COUNT) ? LED_RED : LED_GREEN;

    uint32_t pattern = digits[count];

    GPIOA_ODR = (GPIOA_ODR & ~(PORTA_SEGS | LED_GREEN | LED_RED))
              | (pattern & PORTA_SEGS) | led;

    GPIOB_ODR = (GPIOB_ODR & ~PB0_MASK) | ((pattern & SEG_C) ? PB0_MASK : 0U);
    GPIOC_ODR = (GPIOC_ODR & ~PC1_MASK) | ((pattern & SEG_D) ? PC1_MASK : 0U);
}

/*
 * Call once per ms with the raw pin level (1 = released, 0 = pressed).
 * A new state is accepted only after it holds for DEBOUNCE_MS, so bounce
 * is ignored. Returns 1 only on the press edge, so holding counts once.
 */
static uint32_t button_pressed(Button *button, uint8_t raw)
{
    if (raw == button->stable) {
        button->timer = 0U;
        return 0U;
    }

    button->timer++;
    if (button->timer < DEBOUNCE_MS) {
        return 0U;
    }

    button->timer = 0U;
    button->stable = raw;
    return (raw == 0U) ? 1U : 0U;
}

int main(void)
{
    uint32_t car_count = 0U;
    Button sw1;
    Button sw2;

    gpio_init();
    systick_init();
    update_outputs(car_count);      /* Reset state: display 0, green ON */

    sw1.stable = (GPIOC_IDR & SW1_MASK) ? 1U : 0U;
    sw1.timer  = 0U;
    sw2.stable = (GPIOA_IDR & SW2_MASK) ? 1U : 0U;
    sw2.timer  = 0U;

    while (1) {
        /* Reading CSR clears COUNTFLAG. Skip the work until 1 ms has passed. */
        if ((SYST_CSR & SYST_COUNTFLAG) == 0U) {
            continue;
        }

        uint8_t sw1_raw = (GPIOC_IDR & SW1_MASK) ? 1U : 0U;
        uint8_t sw2_raw = (GPIOA_IDR & SW2_MASK) ? 1U : 0U;

        if (button_pressed(&sw1, sw1_raw) && (car_count < MAX_COUNT)) {
            car_count++;
            update_outputs(car_count);
        }

        if (button_pressed(&sw2, sw2_raw) && (car_count > 0U)) {
            car_count--;
            update_outputs(car_count);
        }
    }
}
