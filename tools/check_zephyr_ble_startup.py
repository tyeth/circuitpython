"""Check actual Zephyr/CP BLE startup functions using host-side fault injection.

Run with Python 3 and a C compiler (cc) supporting UndefinedBehaviorSanitizer:
  python tools/check_zephyr_ble_startup.py --circuitpython . --zephyr ../zephyr

The harness extracts the production functions instead of duplicating them. Its
kernel, controller and settings stubs check startup ordering and cleanup only;
it does not establish physical transport recovery, concurrency or soak stability.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


def function(path, signature):
    source = path.read_text()
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


prefix = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#define CONFIG_ZTEST 0
#define CONFIG_BT_SETTINGS 1
#define CONFIG_BT_DEVICE_NAME_DYNAMIC 0
#define CONFIG_BT_WAIT_NOP 0
#define CONFIG_BT_DEVICE_NAME "test"
#define CONFIG_BT_ID_MAX 1
#define BT_ID_DEFAULT 0
#define BT_ADDR_LE_ANY NULL
#define CONFIG_BT_RX_STACK_SIZE 64
#define CONFIG_BT_RX_PRIO 1
#define IS_ENABLED(value) (value)
#define K_PRIO_COOP(value) (value)
#define LOG_ERR(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define BT_MONITOR_TYPE_PRIMARY 0
#define BT_HCI_BUS 0
#define BT_ADDR_ANY NULL
#define BT_HCI_NAME "test"
#define BT_MONITOR_OPEN_INDEX 0
enum {BT_DEV_DISABLE, BT_DEV_ENABLE, BT_DEV_READY};
struct {unsigned flags[1]; void *hci; int ncmd_sem,cmd_tx_queue,init;} bt_dev;
struct {int thread_id;} bt_workq;
static int rx_thread_stack[64];
typedef void (*bt_ready_cb_t)(int);
static bt_ready_cb_t ready_cb;
static int open_error, settings_init_error, init_error, load_error;
static bool driver_ready, host_initialized, load_sets_ready, ready_during_enable;
static int opens, inits, loads, unsafe_loads, aborts, starts, live_threads, duplicate_threads;
static int submissions;
static int failures, checks;
typedef struct {struct {unsigned char val[6];} a;} bt_addr_le_t;
typedef struct {struct {void *type;} base;} bleio_address_obj_t;
typedef struct {bleio_address_obj_t address;} bleio_adapter_obj_t;
static int bleio_address_type;
static int identity_reads, identity_creates, raised_error;
static jmp_buf exception;
static void raise_zephyr_error(int err) {raised_error=err;longjmp(exception,1);}
static void bt_id_get(bt_addr_le_t *addresses,size_t *count) {
    identity_reads++;memset(addresses,0,sizeof(*addresses));*count=1;
}
static bool bt_addr_le_eq(const bt_addr_le_t *a,const bt_addr_le_t *b) {(void)a;(void)b;return false;}
static int bt_id_create(void *a,void *b) {(void)a;(void)b;identity_creates++;return 0;}
static int bleio_address_type_from_zephyr(const bt_addr_le_t *a) {(void)a;return 0;}
static void common_hal_bleio_address_construct(bleio_address_obj_t *a,const unsigned char *bytes,int type) {
    (void)a;(void)bytes;(void)type;
}
static void check(bool value, const char *name) {
    checks++;
    if (!value) {fprintf(stderr, "FAIL: %s\n",name); failures++;}
}
static void atomic_clear_bit(unsigned *flags, int bit) {*flags &= ~(1u<<bit);}
static void atomic_set_bit(unsigned *flags, int bit) {*flags |= 1u<<bit;}
static bool atomic_test_and_set_bit(unsigned *flags, int bit) {
    bool was = (*flags & (1u<<bit)) != 0;
    *flags |= 1u<<bit;
    if (ready_during_enable && bit == BT_DEV_ENABLE) atomic_set_bit(flags,BT_DEV_READY);
    return was;
}
static bool bt_is_ready(void) {return (bt_dev.flags[0] & (1u<<BT_DEV_READY)) != 0;}
static bool device_is_ready(void *dev) {(void)dev; return driver_ready;}
static void bt_monitor_new_index(int a,int b,void *c,const char *d) {(void)a;(void)b;(void)c;(void)d;}
static int bt_settings_init(void) {return settings_init_error;}
static int bt_set_name(const char *name) {(void)name;return 0;}
static void k_sem_init(int *a,int b,int c) {(void)a;(void)b;(void)c;}
static void k_fifo_init(int *a) {(void)a;}
static void k_work_queue_init(void *q) {(void)q;}
static void k_work_queue_start(void *q,int *stack,int size,int priority,void *config) {
    (void)q;(void)stack;(void)size;(void)priority;(void)config;
    starts++; if(live_threads) duplicate_threads++; live_threads++;
}
static void k_thread_name_set(int id,const char *name) {(void)id;(void)name;}
static void k_thread_abort(int id) {(void)id;aborts++;live_threads--;}
static void bt_recv(void) {}
static int bt_hci_open(void *dev,void (*recv)(void)) {(void)dev;(void)recv;opens++;return open_error;}
static void bt_monitor_send(int a,void *b,int c) {(void)a;(void)b;(void)c;}
static int bt_init(void) {inits++;if(!init_error)host_initialized=true;return init_error;}
static void k_work_submit(int *work) {(void)work;submissions++;}
static void ready_callback(int err) {(void)err;}
static int settings_load(void) {
    loads++;
    if(!host_initialized) unsafe_loads++;
    if(!load_error && load_sets_ready) atomic_set_bit(bt_dev.flags,BT_DEV_READY);
    return load_error;
}
static void reset(void) {
    memset(&bt_dev,0,sizeof(bt_dev));bt_dev.hci=&bt_dev;
    open_error=settings_init_error=init_error=load_error=0;
    driver_ready=load_sets_ready=true;
    host_initialized=ready_during_enable=false;
    opens=inits=loads=unsafe_loads=aborts=starts=live_threads=duplicate_threads=0;
    ready_cb=NULL;
    identity_reads=identity_creates=raised_error=0;
    submissions=0;
}
"""
cases = r"""
int main(void) {
    reset();open_error=-EIO;
    check(bleio_adapter_ensure_stack_ready()==-EIO,"initial open error is propagated");
    check(!(bt_dev.flags[0]&(1u<<BT_DEV_ENABLE)),"failed open clears ENABLE");
    check(loads==0 && inits==0,"failed open never loads settings or initializes host");
    check(live_threads==0,"failed open leaves no receive workqueue");
    open_error=0;
    check(bleio_adapter_ensure_stack_ready()==0,"retry after open failure succeeds");
    check(opens==2 && inits==1 && loads==1,"retry performs actual HCI open and host init");
    check(unsafe_loads==0 && duplicate_threads==0,"retry neither loads uninitialized settings nor duplicates workqueue");
    check(bt_is_ready(),"successful retry is actually ready");

    reset();settings_init_error=-ENOMEM;
    check(bleio_adapter_ensure_stack_ready()==-ENOMEM,"settings init error propagates");
    check(!(bt_dev.flags[0]&(1u<<BT_DEV_ENABLE)),"settings init failure clears ENABLE");
    settings_init_error=0;
    check(bleio_adapter_ensure_stack_ready()==0 && opens==1,"settings initialization can be retried");

    reset();atomic_set_bit(bt_dev.flags,BT_DEV_ENABLE);
    check(bleio_adapter_ensure_stack_ready()==-EALREADY,"incomplete previous enable remains an error");
    check(loads==0 && !bt_is_ready(),"EALREADY never runs settings on an uninitialized host");

    reset();atomic_set_bit(bt_dev.flags,BT_DEV_ENABLE);ready_during_enable=true;
    check(bleio_adapter_ensure_stack_ready()==0,"concurrently completed enable is accepted only when ready");
    check(loads==0,"already-ready race does not reload settings");

    reset();init_error=-EIO;
    check(bleio_adapter_ensure_stack_ready()==-EIO,"host init failure propagates");
    check(bleio_adapter_ensure_stack_ready()==-EALREADY && loads==0,"later retry never loads settings after failed host init");

    reset();load_error=-EIO;
    check(bleio_adapter_ensure_stack_ready()==-EIO,"settings load error is propagated");
    check(!bt_is_ready(),"failed settings do not claim readiness");

    reset();load_sets_ready=false;
    check(bleio_adapter_ensure_stack_ready()!=0,"successful settings call without readiness is not success");

    reset();driver_ready=false;
    check(bleio_adapter_ensure_stack_ready()==-ENODEV && opens==0 && loads==0,"unready driver is not touched");

    reset();atomic_set_bit(bt_dev.flags,BT_DEV_READY);
    check(bleio_adapter_ensure_stack_ready()==0 && opens==0 && loads==0,"ready fast path has no side effects");

    reset();open_error=-EIO;
    bleio_adapter_obj_t adapter={0};
    if(setjmp(exception)==0) {
        (void)common_hal_bleio_adapter_get_address(&adapter);
        check(false,"address read after failed startup must raise");
    } else {
        check(raised_error==-EIO,"address read propagates startup error");
    }
    check(identity_reads==0 && identity_creates==0,"failed startup never reads or manufactures an identity");
    open_error=0;
    check(common_hal_bleio_adapter_get_address(&adapter)==&adapter.address && identity_reads==1,
          "address is available after successful startup retry");

    reset();open_error=-EIO;
    for(int attempt=0;attempt<3;attempt++) {
        check(bt_enable(ready_callback)==-EIO && ready_cb==NULL && live_threads==0,
              "repeated async open failures clean callback and receive thread");
    }
    open_error=0;
    check(bt_enable(ready_callback)==0 && opens==4 && submissions==1 && inits==0 && loads==0,
          "async retry submits initialization only after actual transport open");
    check(ready_cb==ready_callback && duplicate_threads==0,"async retry retains only the successful callback and thread");
    printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--circuitpython", type=Path, required=True)
    parser.add_argument("--zephyr", type=Path, required=True)
    args = parser.parse_args()
    adapter = args.circuitpython / "ports/zephyr-cp/common-hal/_bleio/Adapter.c"
    core = args.zephyr / "subsys/bluetooth/host/hci_core.c"
    functions = "\n".join(
        [
            function(core, "int bt_enable(bt_ready_cb_t cb)"),
            function(adapter, "static int bleio_adapter_ensure_stack_ready(void)"),
            function(
                adapter,
                "bleio_address_obj_t *common_hal_bleio_adapter_get_address(bleio_adapter_obj_t *self)",
            ),
        ]
    )
    return run(functions)


def run(functions):
    with tempfile.TemporaryDirectory(prefix="envhub-ble-startup-") as temporary:
        directory = Path(temporary)
        source = directory / "startup.c"
        source.write_text(prefix + functions + cases)
        failed = False
        for dedicated_workqueue in (False, True):
            output = directory / ("workqueue" if dedicated_workqueue else "system")
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-Wno-unused-variable",
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all",
            ]
            if dedicated_workqueue:
                command.append("-DCONFIG_BT_RECV_WORKQ_BT=1")
            subprocess.run(command + [str(source), "-o", str(output)], check=True)
            result = subprocess.run([str(output)], check=False)
            print(
                "dedicated_workqueue=", dedicated_workqueue, "exit=", result.returncode, flush=True
            )
            failed |= result.returncode != 0
        return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
