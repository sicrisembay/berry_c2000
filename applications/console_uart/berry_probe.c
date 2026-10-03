#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Memory.h>
#include <xdc/runtime/System.h>
#include <stdint.h>
#include <string.h>
#include "berry.h"
#include "be_gc.h"

static Char berry_probe_stack[2048];
static Memory_Size berry_heap_low_water = (Memory_Size)-1;
static const int berry_probe_cycles = 3;

#define BERRY_LOG_DRAIN_TICKS 16
#define BERRY_LOG(...) do { \
    System_printf(__VA_ARGS__); \
    Task_sleep(BERRY_LOG_DRAIN_TICKS); \
} while (0)

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

static Void berry_probe_task(UArg arg0, UArg arg1)
{
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

    (void)arg0;
    (void)arg1;
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
}

void BerryProbe_start(void)
{
    Task_Params params;
    Error_Block error;
    Task_Handle task;

    Task_Params_init(&params);
    params.stack = berry_probe_stack;
    params.stackSize = sizeof(berry_probe_stack);
    params.priority = 1;
    params.instance->name = "berryProbe";
    Error_init(&error);
    task = Task_create(berry_probe_task, &params, &error);
    if (task == NULL) {
        System_printf("BERRY_TASK_CREATE=FAIL\r\n");
    }
}
