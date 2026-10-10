#include "autoconf.h"
#include <stdbool.h>
#include <xdc/runtime/Types.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include "DSP2833x_Device.h"
#include "board.h"

static bool board_initialized = false;


static void configure_core_pll(UInt16 val)
{
    /* Make sure the PLL is not running in limp mode */
    if (SysCtrlRegs.PLLSTS.bit.MCLKSTS != 0)
    {
       /*
        * Missing external clock has been detected
        * Replace this line with a call to an appropriate
        * SystemShutdown(); function.
        */
       asm("        ESTOP0");
    }

    /* Change the PLLCR */
    if (SysCtrlRegs.PLLCR.bit.DIV != val)
    {

       EALLOW;
       /* Before setting PLLCR turn off missing clock detect logic */
       SysCtrlRegs.PLLSTS.bit.MCLKOFF = 1;
       SysCtrlRegs.PLLCR.bit.DIV = val;
       EDIS;

       /*
        * Wait for PLL to lock.
        * During this time the CPU will switch to OSCCLK/2 until
        * the PLL is stable.  Once the PLL is stable the CPU will
        *
        * Wait for the PLL lock bit to be set.
        */
       while(SysCtrlRegs.PLLSTS.bit.PLLLOCKS == 0) {
           /*
            * Note: The watchdog should be fed within
            * the loop via ServiceDog().
            */
       }

       EALLOW;
       SysCtrlRegs.PLLSTS.bit.MCLKOFF = 0;
       EDIS;
     }
}


//
// InitFlash - This function initializes the Flash Control registers
//                   CAUTION
// This function MUST be executed out of RAM. Executing it
// out of OTP/Flash will yield unpredictable results
//
#pragma CODE_SECTION(InitFlashWaitState, "ramfuncs");
static void InitFlashWaitState(void)
{
    EALLOW;

    //
    // Enable Flash Pipeline mode to improve performance
    // of code executed from Flash.
    //
    FlashRegs.FOPT.bit.ENPIPE = 1;

    //
    //                CAUTION
    // Minimum waitstates required for the flash operating
    // at a given CPU rate must be characterized by TI.
    // Refer to the datasheet for the latest information.
    //
#if CONFIG_CORE_FREQ_150MHZ
    //
    // Set the Paged Waitstate for the Flash
    //
    FlashRegs.FBANKWAIT.bit.PAGEWAIT = 5;

    //
    // Set the Random Waitstate for the Flash
    //
    FlashRegs.FBANKWAIT.bit.RANDWAIT = 5;

    //
    // Set the Waitstate for the OTP
    //
    FlashRegs.FOTPWAIT.bit.OTPWAIT = 8;
#elif (CONFIG_CORE_FREQ_80MHZ || CONFIG_CORE_FREQ_100MHZ)
    //
    // Set the Paged Waitstate for the Flash
    //
    FlashRegs.FBANKWAIT.bit.PAGEWAIT = 3;

    //
    // Set the Random Waitstate for the Flash
    //
    FlashRegs.FBANKWAIT.bit.RANDWAIT = 3;

    //
    // Set the Waitstate for the OTP
    //
    FlashRegs.FOTPWAIT.bit.OTPWAIT = 5;
#else
#error "Other clock not supported for now"
#endif
    //
    //                CAUTION
    // ONLY THE DEFAULT VALUE FOR THESE 2 REGISTERS SHOULD BE USED
    //
    FlashRegs.FSTDBYWAIT.bit.STDBYWAIT = 0x01FF;
    FlashRegs.FACTIVEWAIT.bit.ACTIVEWAIT = 0x01FF;

    EDIS;

    //
    // Force a pipeline flush to ensure that the write to
    // the last register configured occurs before returning.
    //
    asm(" RPT #7 || NOP");
}

void BOARD_init(void)
{
    if (!board_initialized)
    {
        Types_FreqHz coreFreq;

        /* Configure Core Frequency */
        DINT;
        IER = 0x0000;
        IFR = 0x0000;

#if CONFIG_CORE_FREQ_80MHZ
#if CONFIG_CRYSTAL_20MHZ
        configure_core_pll(0x8);
#else
#error "Unsupported Crystal Value!"
#endif

#elif CONFIG_CORE_FREQ_100MHZ
#if CONFIG_CRYSTAL_20MHZ
        configure_core_pll(0xA);
#else
#error "Unsupported Crystal Value!"
#endif

#elif CONFIG_CORE_FREQ_150MHZ
#if CONFIG_CRYSTAL_20MHZ
        configure_core_pll(0xF);
#elif CONFIG_CRYSTAL_30MHZ
        configure_core_pll(0xA);
#else
#error "Unsupported Crystal Value!"
#endif

#else
#error "Invalid Core Frequency Value!"
#endif /* CONFIG_CORE_FREQ_XXXMHZ */

        InitFlashWaitState();

        /* Update SYSBIOS Core Clock */
        coreFreq.hi = 0;
        coreFreq.lo = CONFIG_SYSTEM_FREQ_MHZ * 1000000U;
        BIOS_setCpuFreq(&coreFreq);
        Clock_tickReconfig();
        board_initialized = true;
    }
}

void BOARD_reset(void)
{
    EALLOW;
    SysCtrlRegs.WDKEY = 0x0055;
    SysCtrlRegs.WDKEY = 0x00AA;
    EDIS;

    /* Enable watchdog */
    EALLOW;
    SysCtrlRegs.WDCR = 0x0000;  /* writing value other than b101 to WDCHK will immediately resets the device */
    EDIS;

    /* Should not reach here */
    while(1);
}
