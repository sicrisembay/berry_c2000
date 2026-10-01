#ifndef UART_H
#define UART_H

#include "autoconf.h"

#if CONFIG_USE_UART
#include <xdc/std.h>

/*!
 * \page page_uart UART Manager Interface Document
 */

/*!
 * \enum UART_ID_T
 *
 * Enumeration of UART channel ID.
 */
typedef enum {
#if CONFIG_ENABLE_UARTA
    UART_A,                 /*!< ID for UART-A */
#endif
#if CONFIG_ENABLE_UARTB
    UART_B,                 /*!< ID for UART-B */
#endif
#if CONFIG_ENABLE_UARTC
    UART_C,                 /*!< ID for UART-C */
#endif
    N_UART                  /*!< Number of UART instance (always last in enumeration) */
} UART_ID_T;


/*!
 * \page page_uart
 * \section section_uart_intf UART Manager Interface
 */


/*!
 * \page page_uart
 * \subsection subsect_uart_init UART_init
 * <PRE>void UART_init(void);</PRE>
 *
 * This function initializes the UART manager and its underlying peripherals.
 *
 * \param None
 *
 * \return void
 */
void UART_init(void);


/*!
 * \page page_uart
 * \subsection subsect_uart_init_done UART_init_done
 * <PRE>Bool UART_init_done(void);</PRE>
 *
 * This function returns true if the UART manager has been initialized.
 *
 * \param None
 *
 * \return true: UART manager has been initialized, false: otherwise
 */
Bool UART_init_done(void);


/*!
 * \page page_uart
 * \subsection subsect_uart_send UART_send
 * <PRE>UInt16 UART_send(UART_ID_T uart_id, Char * pBuf, UInt16 count);</PRE>
 *
 * This function sends data to UART manager.
 *
 * \param uart_id    UART channel ID (see ::UART_ID_T)
 * \param pBuf  Pointer to buffer
 * \param count Number of bytes to send
 *
 * \return Number of bytes sent
 */
UInt16 UART_send(UART_ID_T uart_id, Char * pBuf, UInt16 count);


/*!
 * \page page_uart
 * \subsection subsect_uart_receive UART_receive
 * <PRE>UInt16 UART_receive(UART_ID_T uart_id, Char * pBuf, UInt16 count);</PRE>
 *
 * This function receives data from UART manager.
 *
 * \param uart_id    UART channel ID (see ::UART_ID_T)
 * \param pBuf  Pointer to buffer
 * \param count Number of bytes to receive
 *
 * \return Actual number of bytes received
 */
UInt16 UART_receive(UART_ID_T uart_id, Char * pBuf, UInt16 count);


#endif /* CONFIG_USE_UART */
#endif /* UART_H */
