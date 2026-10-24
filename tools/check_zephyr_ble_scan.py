"""Exercise the production scan entry point with host-side stubs.

Checks unsupported-request handling and existing scan ownership. This is not
a controller, simulator, or physical radio test. Requires Python 3 and cc.
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


PREFIX = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
typedef void *mp_obj_t;
typedef int mp_int_t;
typedef float mp_float_t;
typedef struct {bool done;} result_t;
typedef struct {result_t *scan_results;} bleio_adapter_obj_t;
struct bt_le_scan_param {int type,options;uint16_t interval,window,timeout,interval_coded,window_coded;};
enum {BT_LE_SCAN_TYPE_ACTIVE=1,BT_LE_SCAN_TYPE_PASSIVE=0};
#define MP_OBJ_FROM_PTR(ptr) ((void *)(ptr))
static result_t old_result,new_result;
static result_t *active_scan_results;
static bool scan_callbacks_registered;
static struct {void (*recv)(void);void (*timeout)(void);} scan_callbacks;
static uint64_t scan_deadline_ms;
static int allocations,stops,starts,registrations,raised,checks,failures;
static struct bt_le_scan_param last_params;
static jmp_buf exception;
static void mp_raise_NotImplementedError(const char *message) {(void)message;raised=1;longjmp(exception,1);}
static void raise_zephyr_error(int err) {raised=err;longjmp(exception,1);}
static bool shared_module_bleio_scanresults_get_done(result_t *r) {return r->done;}
static void common_hal_bleio_adapter_stop_scan(bleio_adapter_obj_t *self) {
 stops++;self->scan_results->done=true;self->scan_results=NULL;
 active_scan_results=NULL;scan_deadline_ms=0;
}
static result_t *shared_module_bleio_new_scanresults(int size,uint8_t *prefixes,size_t length,int rssi) {
 (void)size;(void)prefixes;(void)length;(void)rssi;allocations++;new_result.done=false;return &new_result;
}
static void scan_recv_cb(void) {}
static void scan_timeout_cb(void) {}
static int bt_le_scan_cb_register(void *cb) {(void)cb;registrations++;return 0;}
static uint16_t bleio_validate_and_convert_timeout(float timeout) {return (uint16_t)(timeout*100);}
static int bt_le_scan_start(struct bt_le_scan_param *params,void *cb) {
 (void)cb;starts++;last_params=*params;return 0;
}
static uint64_t supervisor_ticks_ms64(void) {return 1000;}
static void check(bool ok,const char *label) {checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static void reset(bleio_adapter_obj_t *adapter,bool scanning) {
 old_result.done=false;new_result.done=false;
 adapter->scan_results=scanning?&old_result:NULL;
 active_scan_results=adapter->scan_results;
 scan_deadline_ms=scanning?5000:0;
 scan_callbacks_registered=scanning;
 allocations=stops=starts=registrations=raised=0;
}
"""

CASES = r"""
static void request(bleio_adapter_obj_t *adapter,bool extended,bool active,float timeout) {
 raised=0;
 if(setjmp(exception)==0) {
  common_hal_bleio_adapter_start_scan(adapter,NULL,0,extended,512,timeout,0.1f,0.05f,-80,active);
 }
}
int main(void) {
 bleio_adapter_obj_t adapter;
 for(int scanning=0;scanning<2;scanning++) {
  reset(&adapter,scanning);
  request(&adapter,true,true,2);
  check(raised==1,"extended request raises NotImplementedError");
  check(allocations==0 && registrations==0 && starts==0,"unsupported request has no allocation or HCI side effects");
  check(stops==0 && !old_result.done,"unsupported request preserves existing scan");
  check(adapter.scan_results==(scanning?&old_result:NULL),"scan owner unchanged");
  check(active_scan_results==adapter.scan_results,"active scan owner unchanged");
  check(scan_deadline_ms==(scanning?5000:0),"existing deadline unchanged");
  request(&adapter,false,true,2);
  check(raised==0 && allocations==1 && starts==1,"ordinary scan still works after rejected request");
 }
 for(int active=0;active<2;active++) {
  reset(&adapter,false);
  request(&adapter,false,active,2);
  check(raised==0 && starts==1 && allocations==1,"ordinary scan starts");
  check(last_params.type==active && last_params.options==0,"active/passive and duplicate policy preserved");
  check(last_params.interval==160 && last_params.window==80 && last_params.timeout==200,"scan timing preserved");
  check(scan_deadline_ms==3000,"host timeout preserved");
  request(&adapter,false,active,0);
  check(stops==1 && starts==2 && registrations==1,"replacement scan stops old scan without duplicate callback registration");
  check(scan_deadline_ms==0,"indefinite scan has no deadline");
 }
 printf("%d/%d scan checks passed\n",checks-failures,checks);
 return failures?1:0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--circuitpython", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    path = args.circuitpython / "ports/zephyr-cp/common-hal/_bleio/Adapter.c"
    production = function(path, "mp_obj_t common_hal_bleio_adapter_start_scan(")
    with tempfile.TemporaryDirectory(prefix="ble-scan-") as directory:
        root = Path(directory)
        source = root / "scan.c"
        source.write_text(PREFIX + production + CASES)
        binary = root / "scan"
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Wno-unused-function",
                "-fsanitize=undefined",
                "-o",
                str(binary),
                str(source),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
