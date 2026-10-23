#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise actual Zephyr station lifecycle functions with driver fault injection.

python tools/check_zephyr_station_lifecycle.py --circuitpython .
Use --revision REV to demonstrate the preceding source fails the same checks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def block(source, start):
    opening=source.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]


def function(source,name):
    match=re.search(r'^[\w *]+\b'+re.escape(name)+r'\([^;]*?\)\s*\{',source,re.M)
    if match is None:raise ValueError('Missing source function '+name)
    return block(source,match.start())


HARNESS=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <stdatomic.h>
typedef _Atomic int atomic_t;
#define atomic_get(p) atomic_load(p)
#define atomic_set(p,v) atomic_exchange(p,v)
#define LOG_DBG(...) ((void)0)
#define NET_REQUEST_WIFI_DISCONNECT 1
struct net_if { bool up; };
struct wifi_status { int status; };
struct net_mgmt_event_callback { const void *info; };
typedef struct {
 struct net_if *sta_netif, *ap_netif;
 void *current_scan;
 bool started, connected, ap_mode, sta_mode;
 size_t current_ssid_len;
 atomic_t last_disconnect_status;
 uint8_t last_disconnect_reason;
 int connect_sem;
} wifi_radio_obj_t;
static wifi_radio_obj_t radio;
static struct net_if sta,ap;
static int request_result,event_result,event_at,request_calls,up_calls,down_calls;
static int dhcp_stops,scan_stops,up_result,down_result,background_calls;
static int64_t now;
static bool no_event,null_event,interrupt_now;
static jmp_buf exception;
static int raised;
static void dispatch_disconnect(struct net_mgmt_event_callback *cb);
static void send_event(void) {
 struct wifi_status status={event_result};
 struct net_mgmt_event_callback cb={null_event?NULL:&status};
 dispatch_disconnect(&cb);
}
static void raise_zephyr_error(int error) { raised=error;longjmp(exception,1); }
static int net_if_up(struct net_if *iface) {
 up_calls++;if(up_result==0 || up_result==-EALREADY)iface->up=true;return up_result;
}
static int net_if_down(struct net_if *iface) {
 down_calls++;if(down_result==0 || down_result==-EALREADY)iface->up=false;return down_result;
}
static int net_mgmt(int op,struct net_if *iface,void *data,size_t len) {
 assert(op==NET_REQUEST_WIFI_DISCONNECT && iface==&sta && data==NULL && len==0);
 request_calls++;
 if(!no_event && event_at==0)send_event();
 return request_result;
}
static void common_hal_wifi_radio_stop_scanning_networks(wifi_radio_obj_t *self) {
 if(self->current_scan) {scan_stops++;self->current_scan=NULL;}
}
static void net_dhcpv4_stop(struct net_if *iface) { assert(iface==&sta);dhcp_stops++; }
static void k_sem_give(int *sem) { (*sem)++; }
static int64_t k_uptime_get(void) { return now; }
static void k_msleep(int delay) {
 now+=delay;
 if(!no_event && event_at>0 && now>=event_at) {event_at=-1;send_event();}
}
static bool mp_hal_is_interrupted(void) {return interrupt_now;}
#define RUN_BACKGROUND_TASKS do { background_calls++; } while(0)
'''

TESTS=r'''
static int failed,total;
static void check(const char *name,bool ok) {
 total++;if(!ok)failed++;
 printf("%s %s\n",ok?"PASS":"FAIL",name);
}
static void reset(void) {
 sta=(struct net_if){true};ap=(struct net_if){true};
 radio=(wifi_radio_obj_t){.sta_netif=&sta,.started=true,.connected=true,.sta_mode=true,.current_ssid_len=8,.current_scan=(void*)1};
 request_result=event_result=request_calls=up_calls=down_calls=0;
 dhcp_stops=scan_stops=up_result=down_result=background_calls=0;
 event_at=100;now=0;no_event=null_event=interrupt_now=false;raised=0;
}
static void stop(void) {
 if(setjmp(exception)==0)common_hal_wifi_radio_stop_station(&radio);
}
static void start(void) {
 if(setjmp(exception)==0)common_hal_wifi_radio_start_station(&radio);
}
int main(void) {
 reset();event_result=-EIO;send_event();
 check("failed_disconnect_event_does_not_clear_live_link",radio.connected && radio.current_ssid_len==8);
 reset();send_event();
 check("successful_disconnect_event_clears_cached_ssid",!radio.connected && radio.current_ssid_len==0);
 reset();stop();
 check("async_disconnect_confirms_before_return",raised==0 && !radio.connected && !radio.sta_mode && radio.current_ssid_len==0 && request_calls==1 && now==100);
 check("stop_preserves_enabled_interface",radio.started && sta.up && down_calls==0);
 check("stop_cleans_scan_and_dhcp",scan_stops==1 && dhcp_stops==EXPECT_DHCP);
 reset();event_at=0;stop();
 check("synchronous_event_not_lost",raised==0 && !radio.connected && request_calls==1 && now==0);
 reset();request_result=-EIO;event_result=-EIO;event_at=0;stop();
 check("request_error_preserves_connection",raised==-EIO && radio.connected && radio.current_ssid_len==8 && dhcp_stops==0);
 reset();event_result=-EIO;stop();
 check("async_error_preserves_connection",raised==-EIO && radio.connected && dhcp_stops==0);
 reset();null_event=true;stop();
 check("missing_event_payload_is_error",raised==-EIO && radio.connected && dhcp_stops==0);
 reset();no_event=true;stop();
 check("missing_event_times_out_without_false_success",raised==-ETIMEDOUT && radio.connected && now==2000 && dhcp_stops==0);
 reset();no_event=true;interrupt_now=true;stop();
 check("interrupt_returns_without_false_success",raised==-EINTR && radio.connected && now==0);
 reset();no_event=true;request_result=-EALREADY;stop();
 check("already_disconnected_idempotent",raised==0 && !radio.connected && !radio.sta_mode && now==0);
 reset();no_event=true;request_result=-ENOTCONN;stop();
 check("not_connected_idempotent",raised==0 && !radio.connected && now==0);
 reset();radio.ap_mode=true;radio.ap_netif=&sta;radio.connected=false;stop();
 check("shared_interface_ap_preserved",raised==0 && radio.ap_mode && radio.started && sta.up && !radio.sta_mode && request_calls==0 && dhcp_stops==0 && scan_stops==0);
 reset();radio.ap_mode=true;radio.ap_netif=NULL;radio.connected=false;stop();
 check("implicit_shared_interface_ap_preserved",raised==0 && radio.ap_mode && request_calls==0 && dhcp_stops==0);
 reset();radio.ap_mode=true;radio.ap_netif=&ap;stop();
 check("ap_preserved_even_with_unused_separate_ap_pointer",raised==0 && radio.ap_mode && ap.up && radio.started && request_calls==0 && dhcp_stops==0);
 reset();radio.started=false;radio.sta_mode=false;radio.connected=false;sta.up=false;start();
 check("start_brings_interface_up",raised==0 && radio.started && radio.sta_mode && sta.up && up_calls==1);
 start();check("repeated_start_idempotent",raised==0 && up_calls==1);
 reset();radio.started=false;radio.sta_mode=false;sta.up=false;up_result=-EIO;start();
 check("start_failure_does_not_set_flags",raised==-EIO && !radio.started && !radio.sta_mode && !sta.up);
 reset();radio.started=false;radio.sta_mode=false;up_result=-EALREADY;start();
 check("start_accepts_already_up",raised==0 && radio.started && radio.sta_mode);
 reset();radio.ap_mode=true;radio.ap_netif=&sta;radio.sta_mode=false;start();
 check("start_does_not_disrupt_shared_ap",raised==0 && radio.ap_mode && sta.up && request_calls==0 && down_calls==0);
 reset();radio.sta_netif=NULL;stop();check("missing_interface_stop_errors",raised==-ENODEV && request_calls==0);
 reset();radio.sta_netif=NULL;start();check("missing_interface_start_errors",raised==-ENODEV && up_calls==0);
 printf("SUMMARY %d checks, %d failures\n",total,failed);
 return failed?1:0;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--circuitpython',type=Path,default=Path('.'))
    parser.add_argument('--revision')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();root=args.circuitpython.resolve()
    paths=['ports/zephyr-cp/common-hal/wifi/Radio.c','ports/zephyr-cp/common-hal/wifi/__init__.c']
    if args.revision:
        sources={p:subprocess.check_output(['git','-C',str(root),'show',args.revision+':'+p],text=True) for p in paths}
    else:sources={p:(root/p).read_text() for p in paths}
    radio=sources[paths[0]];init=sources[paths[1]]
    marker='case NET_EVENT_WIFI_DISCONNECT_RESULT: '
    event=block(init,init.index('{',init.index(marker)))
    code=HARNESS+'\n'+ '\n'.join(function(radio,n) for n in ('common_hal_wifi_radio_set_enabled','common_hal_wifi_radio_start_station','common_hal_wifi_radio_stop_station'))
    code+='\nstatic void dispatch_disconnect(struct net_mgmt_event_callback *cb) { wifi_radio_obj_t *self=&radio; switch(0) { case 0: '+event+' }}\n'+TESTS
    results=[]
    with tempfile.TemporaryDirectory(prefix='cp-station-') as tmp:
        source=Path(tmp)/'station.c';source.write_text(code)
        for dhcp in (True,False):
            exe=Path(tmp)/('station-dhcp' if dhcp else 'station-static')
            flags=['-DCONFIG_NET_DHCPV4=1','-DEXPECT_DHCP=1'] if dhcp else ['-DEXPECT_DHCP=0']
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',*flags,str(source),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            print(('DHCP' if dhcp else 'STATIC')+'\n'+run.stdout+run.stderr)
            results.append(dict(dhcp=dhcp,returncode=run.returncode,checks=run.stdout.splitlines()))
    evidence=dict(revision=args.revision or 'working-tree',source_sha256={p:hashlib.sha256(s.encode()).hexdigest() for p,s in sources.items()},results=results,scope='Actual station lifecycle/set_enabled functions and disconnect event case; mocked network/kernel callbacks, not physical hardware.')
    if args.output:args.output.write_text(json.dumps(evidence,indent=2)+'\n')
    return int(any(r['returncode'] for r in results))


if __name__=='__main__':raise SystemExit(main())
