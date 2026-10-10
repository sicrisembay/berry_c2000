#include "stdbool.h"
#include <xdc/std.h>
#include <xdc/runtime/Log.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Types.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include "DSP2833x_Device.h"
#include "drivers/uart/uart.h"
#include "drivers/board/board.h"

extern unsigned int RamfuncsLoadStart;
extern unsigned int RamfuncsLoadEnd;
extern unsigned int RamfuncsRunStart;
extern void BerryConsole_start(void);

Int main()
{
    /*
     * Copy ramfuncs section
     */
    memcpy(&RamfuncsRunStart, &RamfuncsLoadStart,
            &RamfuncsLoadEnd - &RamfuncsLoadStart);

    BOARD_init();
    UART_init();

    BerryConsole_start();

    BIOS_start();    /* does not return */
    return(0);
}

void Console_putch(char ch)
{
    UART_send(UART_A, &ch, 1);
}
