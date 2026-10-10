#ifndef BOARD_H
#define BOARD_H

/**
 * @brief Initialize the board clock and flash wait states.
 *
 * Applies the clock configuration selected by Kconfig and updates the
 * SYS/BIOS CPU frequency. Repeated calls have no effect.
 */
void BOARD_init(void);

/**
 * @brief Reset the device using the hardware watchdog.
 *
 * This function does not return.
 */
void BOARD_reset(void);

#endif // BOARD_H
