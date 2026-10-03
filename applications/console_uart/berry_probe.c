#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Memory.h>
#include <xdc/runtime/System.h>
#include <stdint.h>
#include <string.h>
#include "berry.h"

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
    static const char exception_script[] =
        "phase4_raise()\n";
    static const char recovery_script[] =
        "assert(40 + 2 == 42)\n"
        "print('BERRY_EXCEPTION_RECOVERY=PASS')\n";
    int cycle;

    (void)arg0;
    (void)arg1;
    BERRY_LOG("BERRY_PHASE4=START build=11 cycles=%d\r\n", berry_probe_cycles);
    for (cycle = 1; cycle <= berry_probe_cycles; ++cycle) {
        bvm *vm;
        int api_result;
        int script_result;
        int exception_result;
        int recovery_result;

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

        api_result = berry_probe_test_string_api(vm);
        BERRY_LOG("BERRY_STRING_API=%s code=%d cycle=%d\r\n",
            api_result == 0 ? "PASS" : "FAIL", api_result, cycle);
        script_result = berry_probe_run_source(vm, script, "BERRY_SCRIPT", cycle);
        BERRY_LOG("BERRY_SCRIPT=DONE code=%d cycle=%d\r\n", script_result, cycle);
        exception_result = script_result == BE_OK
            ? berry_probe_run_source(vm, exception_script, "BERRY_EXCEPTION", cycle)
            : script_result;
        BERRY_LOG("BERRY_EXCEPTION=DONE code=%d cycle=%d\r\n", exception_result, cycle);
        if (exception_result == BE_EXCEPTION) {
            be_dumpexcept(vm);
            BERRY_LOG("BERRY_EXCEPTION=PASS cycle=%d\r\n", cycle);
        }
        recovery_result = exception_result == BE_OK
            ? berry_probe_run_source(vm, recovery_script, "BERRY_RECOVERY", cycle)
            : (exception_result == BE_EXCEPTION
                ? berry_probe_run_source(vm, recovery_script, "BERRY_RECOVERY", cycle)
                : exception_result);
        BERRY_LOG("BERRY_RECOVERY=DONE code=%d cycle=%d\r\n", recovery_result, cycle);
        berry_probe_report_usage(cycle, "after_phase4");
        if (script_result != BE_OK || exception_result != BE_EXCEPTION || recovery_result != BE_OK) {
            BERRY_LOG("BERRY_PHASE4=FAIL cycle=%d\r\n", cycle);
            be_dumpexcept(vm);
        }
        be_vm_delete(vm);
        berry_probe_report_usage(cycle, "after_vm_delete");
        if (api_result != 0 || script_result != BE_OK || exception_result != BE_EXCEPTION || recovery_result != BE_OK) {
            return;
        }
        BERRY_LOG("BERRY_CYCLE=%d_PASS\r\n", cycle);
    }
    BERRY_LOG("BERRY_PHASE4=PASS\r\n");
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
