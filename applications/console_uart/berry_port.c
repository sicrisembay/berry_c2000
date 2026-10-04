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
    static Char pending[32];
    static UInt16 pending_count = 0;
    static UInt16 pending_index = 0;
    static char input_line[256];
    static size_t input_line_length = 0;
    static size_t input_line_offset = 0;
    static bbool input_line_ready = bfalse;
    size_t count;

    if (buffer == NULL || size == 0) {
        return NULL;
    }
    if (size == 1) {
        buffer[0] = '\0';
        return NULL;
    }
    if (!input_line_ready) {
        bbool overflow = bfalse;

        input_line_length = 0;
        for (;;) {
            Char received;

            if (pending_index == pending_count) {
                pending_count = UART_receive(UART_A, pending,
                    (UInt16)(sizeof(pending) / sizeof(pending[0])));
                pending_index = 0;
                if (pending_count == 0) {
                    Task_sleep(1);
                    continue;
                }
            }
            received = pending[pending_index++];
            if (skip_lf) {
                skip_lf = bfalse;
                if (received == '\n') {
                    continue;
                }
            }
            if (received == '\r' || received == '\n') {
                skip_lf = received == '\r' ? btrue : bfalse;
                be_writebuffer("\r\n", 2);
                if (overflow) {
                    input_line[0] = '\n';
                    input_line_length = 1;
                    be_writebuffer("[Berry: input line too long]\r\n", 30);
                } else {
                    input_line[input_line_length++] = '\n';
                }
                input_line[input_line_length] = '\0';
                input_line_ready = btrue;
                break;
            }
            if (received == '\b' || received == 0x7F) {
                if (input_line_length > 0 && !overflow) {
                    --input_line_length;
                    be_writebuffer("\b \b", 3);
                }
                continue;
            }
            if (received == 0 || overflow) {
                continue;
            }
            if (input_line_length < sizeof(input_line) - 2) {
                char echo = (char)((uint16_t)received & 0xFFu);
                input_line[input_line_length++] = echo;
                be_writebuffer(&echo, 1);
            } else {
                overflow = btrue;
            }
        }
    }

    count = input_line_length - input_line_offset;
    if (count >= size) {
        count = size - 1;
    }
    memcpy(buffer, input_line + input_line_offset, count);
    buffer[count] = '\0';
    input_line_offset += count;
    if (input_line_offset == input_line_length) {
        input_line_offset = 0;
        input_line_ready = bfalse;
    }
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
