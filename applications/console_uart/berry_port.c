#include <stddef.h>
#include <stdint.h>
#include <xdc/std.h>
#include <ti/sysbios/knl/Task.h>
#include "drivers/uart/uart.h"
#include "berry.h"
#include "be_sys.h"

BERRY_API void be_writebuffer(const char *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        size_t remaining = length - offset;
        UInt16 count = remaining > 0xFFFFu ? 0xFFFFu : (UInt16)remaining;
        UInt16 sent = UART_send(UART_A, (Char *)buffer + offset, count);
        if (sent == 0) {
            Task_yield();
        } else {
            offset += sent;
        }
    }
}

BERRY_API char* be_readstring(char *buffer, size_t size)
{
    static bbool skip_lf = bfalse;
    size_t length = 0;
    if (buffer == NULL || size == 0) {
        return NULL;
    }
    for (;;) {
        Char received;
        if (UART_receive(UART_A, &received, 1) == 0) {
            Task_yield();
            continue;
        }
        if (skip_lf) {
            skip_lf = bfalse;
            if (received == '\n') {
                continue;
            }
        }
        if (received == '\r' || received == '\n') {
            skip_lf = received == '\r' ? btrue : bfalse;
            break;
        }
        if (received == '\b' || received == 0x7F) {
            if (length > 0) {
                --length;
            }
            continue;
        }
        if (length + 1 < size) {
            buffer[length++] = (char)((uint16_t)received & 0xFFu);
        }
    }
    buffer[length] = '\0';
    return buffer;
}

void* be_fopen(const char *filename, const char *modes)
{
    (void)filename;
    (void)modes;
    return NULL;
}

int be_fclose(void *hfile)
{
    (void)hfile;
    return 0;
}

size_t be_fwrite(void *hfile, const void *buffer, size_t length)
{
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

size_t be_fread(void *hfile, void *buffer, size_t length)
{
    (void)hfile;
    (void)buffer;
    (void)length;
    return 0;
}

char* be_fgets(void *hfile, void *buffer, int size)
{
    (void)hfile;
    (void)buffer;
    (void)size;
    return NULL;
}

int be_fseek(void *hfile, long offset)
{
    (void)hfile;
    (void)offset;
    return -1;
}

long int be_ftell(void *hfile)
{
    (void)hfile;
    return -1;
}

long int be_fflush(void *hfile)
{
    (void)hfile;
    return -1;
}

size_t be_fsize(void *hfile)
{
    (void)hfile;
    return 0;
}
