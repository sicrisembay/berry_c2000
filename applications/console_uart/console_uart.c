#include <xdc/std.h>
#include <xdc/runtime/Log.h>
#include <ti/sysbios/BIOS.h>
#include "drivers/uart/uart.h"

Int main()
{
    UART_init();

    BIOS_start();    /* does not return */
    return(0);
}
