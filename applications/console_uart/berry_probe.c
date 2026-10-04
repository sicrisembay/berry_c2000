#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Memory.h>
#include <xdc/runtime/System.h>
#include "autoconf.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "berry.h"
#include "be_gc.h"
#include "be_repl.h"

static Char berry_console_stack[2048];
#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
static Char berry_allocator_stack[512];
static Memory_Size berry_heap_low_water = (Memory_Size)-1;
static volatile int berry_allocator_result = -1;
static volatile UInt16 berry_allocator_stack_peak = 0;
static const int berry_probe_cycles = 3;
#endif
static char berry_console_line[256];
static bvm *berry_console_vm = NULL;

#define BERRY_LOG_DRAIN_TICKS 16
#define BERRY_LOG(...) do { \
    System_printf(__VA_ARGS__); \
    Task_sleep(BERRY_LOG_DRAIN_TICKS); \
} while (0)

#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
static void berry_probe_report_usage(int cycle, const char *stage)
{
    Memory_Stats heap_stats;
    Task_Stat task_stats;

    Memory_getStats(Memory_defaultHeapInstance, &heap_stats);
    Task_stat(Task_self(), &task_stats);
    if (heap_stats.totalFreeSize < berry_heap_low_water) {
        berry_heap_low_water = heap_stats.totalFreeSize;
    }
    BERRY_LOG(
        "BERRY_MEM cycle=%d stage=%s total_mau=%lu free_mau=%lu largest_mau=%lu low_free_mau=%lu stack_peak_mau=%lu stack_size_mau=%lu\r\n",
        cycle,
        stage,
        (unsigned long)heap_stats.totalSize,
        (unsigned long)heap_stats.totalFreeSize,
        (unsigned long)heap_stats.largestFreeSize,
        (unsigned long)berry_heap_low_water,
        (unsigned long)task_stats.used,
        (unsigned long)task_stats.stackSize);
}

static int berry_probe_test_string_api(bvm *vm)
{
    static const char octets[] = { 'A', 0, 0x7F, 0x80, 0xFF };
    static const uint16_t expected[] = { 0x41, 0x00, 0x7F, 0x80, 0xFF };
    const char *actual;
    int index;

    be_pushnstring(vm, octets, sizeof(octets));
    if (!be_isstring(vm, -1) || be_strlen(vm, -1) != (int)sizeof(octets)) {
        be_pop(vm, 1);
        return 1;
    }
    actual = be_tostring(vm, -1);
    for (index = 0; index < (int)sizeof(octets); ++index) {
        if (((uint16_t)actual[index] & 0xFFu) != expected[index]) {
            be_pop(vm, 1);
            return 2;
        }
    }
    be_pop(vm, 1);
    return 0;
}

static int berry_probe_raise_expected(bvm *vm)
{
    be_raise(vm, "internal_error", "phase4 expected");
    return 0;
}

static int berry_probe_fill_octets_impl(bvm *vm)
{
    bbyte *buffer;
    int index;

    if (!be_getglobal(vm, "phase5_octets")) {
        be_pop(vm, 1);
        return 1;
    }
    if (!be_getmember(vm, -1, ".p") || !be_iscomptr(vm, -1)) {
        be_pop(vm, 2);
        return 2;
    }
    buffer = (bbyte*)be_tocomptr(vm, -1);
    for (index = 0; index < 256; ++index) {
        buffer[index] = be_octet_from_u32((uint32_t)index);
    }
    be_pop(vm, 2);
    return 0;
}

static int berry_probe_fill_octets(bvm *vm)
{
    be_pushint(vm, berry_probe_fill_octets_impl(vm));
    be_return(vm);
}

static int berry_probe_verify_octets_impl(bvm *vm)
{
    bbyte *buffer;
    int index;

    if (!be_getglobal(vm, "phase5_octets")) {
        be_pop(vm, 1);
        return 1;
    }
    if (!be_getmember(vm, -1, ".p")) {
        be_pop(vm, 2);
        return 2;
    }
    if (!be_iscomptr(vm, -1)) {
        be_pop(vm, 2);
        return 3;
    }
    buffer = (bbyte*)be_tocomptr(vm, -1);
    for (index = 0; index < 256; ++index) {
        if (((uint16_t)buffer[index] & 0xFFu) != (uint16_t)index) {
            be_pop(vm, 2);
            return 4;
        }
    }
    be_pop(vm, 2);
    return 0;
}

static int berry_probe_verify_octets(bvm *vm)
{
    be_pushint(vm, berry_probe_verify_octets_impl(vm));
    be_return(vm);
}
#endif

static char *berry_console_getline(const char *prompt)
{
#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
    static unsigned int prompt_count = 0;
    static bbool allocator_result_reported = bfalse;
#endif
    size_t length;

    if (berry_console_vm != NULL) {
        be_gc_collect(berry_console_vm);
    }
#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
    if (!allocator_result_reported && berry_allocator_result >= 0) {
        BERRY_LOG("BERRY_ALLOCATOR_STRESS=%s code=%d stack_peak_mau=%u stack_size_mau=%u\r\n",
            berry_allocator_result == 0 ? "PASS" : "FAIL",
            berry_allocator_result,
            (unsigned int)berry_allocator_stack_peak,
            (unsigned int)(sizeof(berry_allocator_stack) / sizeof(berry_allocator_stack[0])));
        allocator_result_reported = btrue;
    }
    if ((++prompt_count & 0x0Fu) == 0) {
        berry_probe_report_usage(0, "console_sustained");
    }
#endif
    be_writebuffer(prompt, strlen(prompt));
    if (be_readstring(berry_console_line, sizeof(berry_console_line)) == NULL) {
        return NULL;
    }
    length = strlen(berry_console_line);
    if (length > 0 && berry_console_line[length - 1] == '\n') {
        berry_console_line[--length] = '\0';
    }
    if (length == 0) {
        strcpy(berry_console_line, "nil");
    }
    return berry_console_line;
}

#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
static Void berry_allocator_stress_task(UArg arg0, UArg arg1)
{
    Task_Stat task_stats;
    int iteration;
    int result = 0;

    (void)arg0;
    (void)arg1;
    for (iteration = 0; iteration < 256; ++iteration) {
        size_t original_size = 16u + (size_t)(iteration & 31);
        size_t expanded_size = original_size + 8u;
        Char *block = (Char*)malloc(original_size);
        Char *expanded;
        size_t index;

        if (block == NULL) {
            result = 1;
            break;
        }
        for (index = 0; index < original_size; ++index) {
            block[index] = (Char)(iteration & 0xFF);
        }
        Task_sleep(1);
        expanded = (Char*)realloc(block, expanded_size);
        if (expanded == NULL) {
            free(block);
            result = 2;
            break;
        }
        for (index = 0; index < original_size; ++index) {
            if ((uint16_t)expanded[index] != (uint16_t)(iteration & 0xFF)) {
                result = 3;
                break;
            }
        }
        free(expanded);
        if (result != 0) {
            break;
        }
    }
    Task_stat(Task_self(), &task_stats);
    berry_allocator_stack_peak = (UInt16)task_stats.used;
    berry_allocator_result = result;
}

static void berry_allocator_stress_start(void)
{
    Task_Params params;
    Error_Block error;

    Task_Params_init(&params);
    params.stack = berry_allocator_stack;
    params.stackSize = sizeof(berry_allocator_stack);
    params.priority = 2;
    params.instance->name = "berryAllocatorTest";
    berry_allocator_result = -1;
    berry_allocator_stack_peak = 0;
    Error_init(&error);
    if (Task_create(berry_allocator_stress_task, &params, &error) == NULL) {
        berry_allocator_result = 4;
    }
}

static void berry_console_diagnose_call(bvm *vm, const char *source,
    const char *stage)
{
    int result = be_loadstring(vm, source);

    BERRY_LOG("BERRY_DIAG_%s_LOAD=%d top=%d\r\n", stage, result, be_top(vm));
    if (result != BE_OK) {
        if (result == BE_EXCEPTION && be_top(vm) >= 2) {
            const char *exception = be_tostring(vm, -2);
            const char *message = be_tostring(vm, -1);
            BERRY_LOG("BERRY_DIAG_%s_LOAD_ERROR=%s message=%s\r\n",
                stage, exception, message);
            be_pop(vm, 2);
        }
        return;
    }
    result = be_pcall(vm, 0);
    if (result == BE_OK) {
        int is_integer = be_isint(vm, -1);
        int value = is_integer ? (int)be_toint(vm, -1) : 0;
        BERRY_LOG("BERRY_DIAG_%s_CALL=%d top=%d nil=%d int=%d value=%d\r\n",
            stage, result, be_top(vm), be_isnil(vm, -1), is_integer, value);
        be_pop(vm, 1);
    } else if (result == BE_EXCEPTION) {
        const char *exception = be_tostring(vm, -2);
        const char *message = be_tostring(vm, -1);
        BERRY_LOG("BERRY_DIAG_%s_CALL=%d top=%d exception=%s message=%s\r\n",
            stage, result, be_top(vm), exception, message);
        be_pop(vm, 2);
    } else {
        BERRY_LOG("BERRY_DIAG_%s_CALL=%d top=%d\r\n",
            stage, result, be_top(vm));
    }
}

static int berry_probe_run_source(bvm *vm, const char *source,
    const char *stage, int cycle)
{
    int result;

    BERRY_LOG("%s_LOAD=START cycle=%d\r\n", stage, cycle);
    result = be_loadstring(vm, source);
    BERRY_LOG("%s_LOAD=DONE code=%d cycle=%d\r\n", stage, result, cycle);
    if (result != BE_OK) {
        return result;
    }
    result = be_pcall(vm, 0);
    BERRY_LOG("%s_CALL=DONE code=%d cycle=%d\r\n", stage, result, cycle);
    return result;
}
#endif

static Void berry_console_task(UArg arg0, UArg arg1)
{
#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
    static const char script[] =
        "var ascii = \"Berry C28\"\n"
        "assert(ascii == \"Berry C28\")\n"
        "var euro = \"\\u20AC\"\n"
        "assert(euro == \"\\xE2\\x82\\xAC\")\n"
        "var keys = {}\n"
        "keys[euro] = 23\n"
        "assert(keys[\"\\xE2\\x82\\xAC\"] == 23)\n"
        "var phase4_global = 41\n"
        "assert(phase4_global + 1 == 42)\n"
        "assert(1 + 2 == 3)\n"
        "print('BERRY_PHASE4_SCRIPT=PASS')\n";
    static const char octets_script[] =
        "var phase5_octets = bytes()\n"
        "phase5_octets.resize(256)\n"
        "assert(phase5_fill_octets() == 0)\n"
        "assert(phase5_verify_octets() == 0)\n"
        "assert(phase5_octets.size() == 256)\n"
        "print('BERRY_PHASE5_OCTETS=PASS')\n";
    static const char bounds_script[] =
        "assert(phase5_octets.get(0) == 0)\n"
        "assert(phase5_octets.get(255) == 255)\n"
        "assert(phase5_octets.get(-1) == 255)\n"
        "assert(phase5_octets.get(256) == 0)\n"
        "assert(phase5_octets.get(255, 2) == 0)\n"
        "phase5_octets.set(255, 0x1FF)\n"
        "assert(phase5_octets.get(255) == 255)\n"
        "var phase5_add = bytes()\n"
        "phase5_add.add(0x1FF)\n"
        "assert(phase5_add.tohex() == 'FF')\n"
        "phase5_octets = nil\n"
        "print('BERRY_PHASE5_BOUNDS=PASS')\n";
    static const char endian_script[] =
        "var phase5_endian = bytes()\n"
        "phase5_endian.resize(4)\n"
        "phase5_endian.set(0, 0x1234, 2)\n"
        "assert(phase5_endian.tohex() == '34120000')\n"
        "assert(phase5_endian.get(0, 2) == 0x1234)\n"
        "phase5_endian.set(0, 0x1234, -2)\n"
        "assert(phase5_endian.tohex() == '12340000')\n"
        "assert(phase5_endian.get(0, -2) == 0x1234)\n"
        "phase5_endian.set(0, 0x123456, 3)\n"
        "assert(phase5_endian.tohex() == '56341200')\n"
        "assert(phase5_endian.get(0, 3) == 0x123456)\n"
        "phase5_endian.set(0, 0x123456, -3)\n"
        "assert(phase5_endian.tohex() == '12345600')\n"
        "assert(phase5_endian.get(0, -3) == 0x123456)\n"
        "phase5_endian.set(0, 0x12345678, 4)\n"
        "assert(phase5_endian.tohex() == '78563412')\n"
        "assert(phase5_endian.get(0, 4) == 0x12345678)\n"
        "phase5_endian.set(0, 0x12345678, -4)\n"
        "assert(phase5_endian.tohex() == '12345678')\n"
        "assert(phase5_endian.get(0, -4) == 0x12345678)\n"
        "phase5_endian.set(0, 0x80, 1)\n"
        "assert(phase5_endian.geti(0, 1) == -128)\n"
        "phase5_endian.set(0, 0x8000, 2)\n"
        "assert(phase5_endian.geti(0, 2) == -32768)\n"
        "phase5_endian.set(0, 0x800000, 3)\n"
        "assert(phase5_endian.geti(0, 3) == -8388608)\n"
        "print('BERRY_PHASE5_ENDIAN=PASS')\n";
    static const char slice_script[] =
        "var phase5_sample = bytes('001122334455')\n"
        "assert(phase5_sample[0..2] == bytes('001122'))\n"
        "assert(phase5_sample[1..2] == bytes('1122'))\n"
        "assert(phase5_sample.copy() == phase5_sample)\n"
        "print('BERRY_PHASE5_SLICE=PASS')\n";
    static const char exception_script[] =
        "phase4_raise()\n";
    static const char recovery_script[] =
        "assert(40 + 2 == 42)\n"
        "print('BERRY_EXCEPTION_RECOVERY=PASS')\n";
    int cycle;
#endif

    (void)arg0;
    (void)arg1;
#if CONFIG_BERRY_STARTUP_DIAGNOSTICS
    BERRY_LOG("BERRY_PHASE5=START build=28 cycles=%d\r\n", berry_probe_cycles);
    for (cycle = 1; cycle <= berry_probe_cycles; ++cycle) {
        bvm *vm;
        int api_result;
        int octets_result;
        int bounds_result;
        int endian_result;
        int slice_result;
        int bytes_result;
        int script_result;
        int exception_result;
        int recovery_result;
        bbool exception_expected = bfalse;

        BERRY_LOG("BERRY_CYCLE=%d_START\r\n", cycle);
        berry_probe_report_usage(cycle, "before_vm");
        vm = be_vm_new();
        if (vm == NULL) {
            BERRY_LOG("BERRY_VM=CREATE_FAIL cycle=%d\r\n", cycle);
            return;
        }
        BERRY_LOG("BERRY_VM=CREATED cycle=%d\r\n", cycle);
        berry_probe_report_usage(cycle, "after_vm");
        be_regfunc(vm, "phase4_raise", berry_probe_raise_expected);
        be_regfunc(vm, "phase5_fill_octets", berry_probe_fill_octets);
        be_regfunc(vm, "phase5_verify_octets", berry_probe_verify_octets);

        api_result = berry_probe_test_string_api(vm);
        BERRY_LOG("BERRY_STRING_API=%s code=%d cycle=%d\r\n",
            api_result == 0 ? "PASS" : "FAIL", api_result, cycle);

        octets_result = berry_probe_run_source(vm, octets_script, "BERRY_OCTETS", cycle);
        BERRY_LOG("BERRY_OCTETS=DONE code=%d cycle=%d\r\n", octets_result, cycle);
        bounds_result = octets_result == BE_OK
            ? berry_probe_run_source(vm, bounds_script, "BERRY_BOUNDS", cycle)
            : octets_result;
        BERRY_LOG("BERRY_BOUNDS=DONE code=%d cycle=%d\r\n", bounds_result, cycle);
        if (bounds_result == BE_OK) {
            be_gc_collect(vm);
        }
        endian_result = bounds_result == BE_OK
            ? berry_probe_run_source(vm, endian_script, "BERRY_ENDIAN", cycle)
            : bounds_result;
        BERRY_LOG("BERRY_ENDIAN=DONE code=%d cycle=%d\r\n", endian_result, cycle);
        if (endian_result == BE_OK) {
            be_gc_collect(vm);
        }
        slice_result = endian_result == BE_OK
            ? berry_probe_run_source(vm, slice_script, "BERRY_SLICE", cycle)
            : endian_result;
        BERRY_LOG("BERRY_SLICE=DONE code=%d cycle=%d\r\n", slice_result, cycle);
        if (slice_result == BE_OK) {
            be_gc_collect(vm);
            BERRY_LOG("BERRY_PHASE5_BYTES=PASS cycle=%d\r\n", cycle);
        }
        bytes_result = slice_result;
        script_result = bytes_result == BE_OK
            ? berry_probe_run_source(vm, script, "BERRY_SCRIPT", cycle)
            : bytes_result;
        BERRY_LOG("BERRY_SCRIPT=DONE code=%d cycle=%d\r\n", script_result, cycle);
        exception_result = script_result == BE_OK
            ? berry_probe_run_source(vm, exception_script, "BERRY_EXCEPTION", cycle)
            : script_result;
        BERRY_LOG("BERRY_EXCEPTION=DONE code=%d cycle=%d\r\n", exception_result, cycle);
        if (exception_result == BE_EXCEPTION) {
            exception_expected = be_isstring(vm, -2)
                && strcmp(be_tostring(vm, -2), "internal_error") == 0;
            be_dumpexcept(vm);
            if (exception_expected) {
                BERRY_LOG("BERRY_EXCEPTION=PASS cycle=%d\r\n", cycle);
            }
        }
        recovery_result = exception_expected
            ? berry_probe_run_source(vm, recovery_script, "BERRY_RECOVERY", cycle)
            : BE_EXEC_ERROR;
        BERRY_LOG("BERRY_RECOVERY=DONE code=%d cycle=%d\r\n", recovery_result, cycle);
        berry_probe_report_usage(cycle, "after_phase5");
        if (script_result != BE_OK || bytes_result != BE_OK || octets_result != BE_OK ||
            bounds_result != BE_OK || endian_result != BE_OK || slice_result != BE_OK ||
            exception_result != BE_EXCEPTION || recovery_result != BE_OK) {
            BERRY_LOG("BERRY_PHASE5=FAIL cycle=%d\r\n", cycle);
        }
        be_vm_delete(vm);
        berry_probe_report_usage(cycle, "after_vm_delete");
        if (api_result != 0 || script_result != BE_OK || bytes_result != BE_OK ||
            octets_result != BE_OK || bounds_result != BE_OK || endian_result != BE_OK ||
            slice_result != BE_OK || exception_result != BE_EXCEPTION || recovery_result != BE_OK) {
            return;
        }
        BERRY_LOG("BERRY_CYCLE=%d_PASS\r\n", cycle);
    }
    BERRY_LOG("BERRY_PHASE5=PASS\r\n");
#endif

    {
        bvm *vm;
        int result;

    #if CONFIG_BERRY_STARTUP_DIAGNOSTICS
        berry_heap_low_water = (Memory_Size)-1;
        berry_probe_report_usage(0, "console_before_vm");
    #endif
        vm = be_vm_new();
        if (vm == NULL) {
            BERRY_LOG("BERRY_CONSOLE=VM_CREATE_FAIL\r\n");
            return;
        }
    #if CONFIG_BERRY_STARTUP_DIAGNOSTICS
        berry_probe_report_usage(0, "console_vm_created");
    #endif
        result = be_dostring(vm, "print('Berry ready')");
        if (result != BE_OK) {
            BERRY_LOG("BERRY_CONSOLE=SMOKE_FAIL code=%d\r\n", result);
            be_dumpexcept(vm);
            be_vm_delete(vm);
            return;
        }
    #if CONFIG_BERRY_STARTUP_DIAGNOSTICS
        berry_probe_report_usage(0, "console_smoke");
        berry_console_diagnose_call(vm, "return (40 + 2)", "EXPR");
        berry_console_diagnose_call(vm, "assert(false, 'ASSERT_PROBE')", "EXCEPTION");
        be_gc_collect(vm);
    #endif
        BERRY_LOG("BERRY_CONSOLE=READY\r\n");
        berry_console_vm = vm;
    #if CONFIG_BERRY_STARTUP_DIAGNOSTICS
        berry_allocator_stress_start();
    #endif
        result = be_repl(vm, berry_console_getline, NULL);
        berry_console_vm = NULL;
        BERRY_LOG("BERRY_CONSOLE=REPL_EXIT code=%d\r\n", result);
        be_vm_delete(vm);
    }
}

void BerryConsole_start(void)
{
    Task_Params params;
    Error_Block error;

    Task_Params_init(&params);
    params.stack = berry_console_stack;
    params.stackSize = sizeof(berry_console_stack);
    params.priority = 1;
    params.instance->name = "berryConsole";
    Error_init(&error);
    if (Task_create(berry_console_task, &params, &error) == NULL) {
        System_printf("BERRY_CONSOLE=TASK_CREATE_FAIL\r\n");
    }
}
