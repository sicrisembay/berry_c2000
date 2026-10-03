#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/System.h>
#include <string.h>
#include "berry.h"

static Char berry_probe_stack[2048];

static Void berry_probe_task(UArg arg0, UArg arg1)
{
    static const char script[] =
        "var b = bytes('00FF7F80')\n"
        "assert(b.tohex() == '00FF7F80')\n"
        "assert(b.get(1, 1) == 255)\n"
        "assert(b.geti(1, 1) == -1)\n"
        "var p = b._buffer()\n"
        "assert(p[0] == 0 && p[1] == 255 && p[2] == 127 && p[3] == 128)\n"
        "b.set(2, 0x1FF)\n"
        "assert(b[2] == 255)\n"
        "assert(p[2] == 255)\n"
        "assert(b.tohex() == '00FFFF80')\n"
        "print('BERRY_BYTES_COMPTR_READ=PASS')\n";
    bvm *vm;
    int result;

    (void)arg0;
    (void)arg1;
    System_printf("BERRY_VM=START\r\n");
    vm = be_vm_new();
    if (vm == NULL) {
        System_printf("BERRY_VM=CREATE_FAIL\r\n");
        return;
    }
    System_printf("BERRY_VM=CREATED\r\n");
    System_printf("BERRY_SCRIPT=START\r\n");
    result = be_dostring(vm, script);
    System_printf("BERRY_SCRIPT=DONE code=%d\r\n", result);
    if (result == BE_OK) {
        System_printf("BERRY_VM=PASS\r\n");
    } else {
        System_printf("BERRY_VM=FAIL code=%d\r\n", result);
        be_dumpexcept(vm);
    }
    be_vm_delete(vm);
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
