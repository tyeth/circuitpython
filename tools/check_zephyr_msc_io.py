#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run actual MSC/FatFs/block-write C functions against injected disk failures.

Use --revision to run the same cases against preceding firmware source.
Each case runs in its own process so a missing-media crash cannot hide later cases.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'^[\w *]+\b' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError('Missing function ' + name)
    start = match.start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#define MSC_FLASH_BLOCK_SIZE 512
#define FILESYSTEM_BLOCK_SIZE 512
#define FF_MIN_SS 512
#define PART1_START_BLOCK 1
#define CIRCUITPY_SAVES_PARTITION_SIZE 0
#define AUTORELOAD_SUSPEND_USB 1
#define GET_SECTOR_COUNT 1
#define GET_SECTOR_SIZE 2
#define DISK_IOCTL_GET_SECTOR_COUNT 3
#define DISK_IOCTL_GET_SECTOR_SIZE 4
#define DISK_IOCTL_CTRL_SYNC 5
#define MP_EROFS EROFS
#define printk(...) ((void)0)
typedef enum { RES_OK, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR } DRESULT;
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef unsigned int mp_uint_t;
typedef void *mp_obj_t;
typedef struct { int unused; } blockdev_t;
typedef struct {
 blockdev_t blockdev;
 struct { uint32_t winsect,fatbase; uint16_t ssize; uint8_t win[512]; } fatfs;
} fs_user_mount_t;
typedef struct { fs_user_mount_t fat; } supervisor_vfs_t;
typedef fs_user_mount_t *bdev_t;
struct disk_info { int unused; };
static supervisor_vfs_t volume;
static bool present=true,content_write[1],suspended;
static int ioctl_error,read_error,write_error,read_calls,write_calls,dirty_calls,trigger_calls;
static uint32_t count=32;
static uint8_t sectors[32*512],buffer[8*512];
static supervisor_vfs_t *filesystem_circuitpy(void) { return present?&volume:NULL; }
static fs_user_mount_t *disk_get_device(bdev_t p) { return p; }
static DRESULT disk_ioctl(fs_user_mount_t *root,int cmd,void *dest) {
 if(!root || ioctl_error)return RES_ERROR;
 if(cmd==GET_SECTOR_COUNT)*(uint32_t*)dest=count;
 else if(cmd==GET_SECTOR_SIZE)*(uint16_t*)dest=512;
 else return RES_PARERR;
 return RES_OK;
}
static void autoreload_suspend(int what) { (void)what;suspended=true; }
static void autoreload_resume(int what) { (void)what;suspended=false; }
static void autoreload_trigger(void) { trigger_calls++; }
static void supervisor_flash_mark_dirty(void) { dirty_calls++; }
static mp_uint_t supervisor_flash_write_blocks(const uint8_t *data,uint32_t start,uint32_t length) {
 write_calls++;
 if(write_error)return write_error;
 if(start>=31 || length>31-start)return EIO;
 memcpy(sectors+(start+1)*512,data,length*512);
 return 0;
}
static int mp_vfs_blockdev_read(blockdev_t *b,uint32_t start,uint32_t length,uint8_t *data) {
 (void)b;read_calls++;
 if(read_error)return read_error;
 if(start>=32 || length>32-start)return EIO;
 memcpy(data,sectors+start*512,length*512);return 0;
}
'''

BRIDGE = r'''
static int mp_vfs_blockdev_write(blockdev_t *b,uint32_t start,uint32_t length,const uint8_t *data) {
 if(write_error==-EROFS)return -EROFS;
 return flash_write_blocks(b,data,start,length);
}
'''

TESTS = r'''
static void reset(void) {
 memset(&volume,0,sizeof(volume));memset(sectors,0x33,sizeof(sectors));
 for(unsigned n=0;n<sizeof(buffer);n++)buffer[n]=(uint8_t)(n/512+0x80);
 memset(volume.fat.fatfs.win,0x5a,512);
 volume.fat.fatfs.winsect=5;volume.fat.fatfs.fatbase=2;volume.fat.fatfs.ssize=512;
 present=true;content_write[0]=suspended=false;
 ioctl_error=read_error=write_error=read_calls=write_calls=dirty_calls=trigger_calls=0;count=32;
}
static bool read_ok(void) { return _zephyr_disk_read(NULL,buffer,2,2)==0 && read_calls==1 && buffer[0]==0x33; }
static bool read_failure(void) { read_error=EIO;return _zephyr_disk_read(NULL,buffer,2,2)==-EIO && read_calls==1; }
static bool read_bounds(void) { return _zephyr_disk_read(NULL,buffer,31,2)==-EIO && read_calls==0; }
static bool read_overflow(void) { return _zephyr_disk_read(NULL,buffer,UINT32_MAX,2)==-EIO && read_calls==0; }
static bool read_capacity_failure(void) { ioctl_error=1;return _zephyr_disk_read(NULL,buffer,0,1)==-EIO && read_calls==0; }
static bool write_capacity_failure(void) { ioctl_error=1;return _zephyr_disk_write(NULL,buffer,2,2)==-EIO && !suspended && write_calls==0; }
static bool write_failure(void) { write_error=EIO;int r=_zephyr_disk_write(NULL,buffer,5,1);return r==-EIO && !suspended && !content_write[0] && volume.fat.fatfs.win[0]==0x5a; }
static bool write_readonly(void) { write_error=-EROFS;return _zephyr_disk_write(NULL,buffer,5,1)==-EROFS && !suspended && !content_write[0]; }
static bool write_bounds(void) { return _zephyr_disk_write(NULL,buffer,31,2)==-EIO && write_calls==0 && !suspended; }
static bool write_overflow(void) { return _zephyr_disk_write(NULL,buffer,UINT32_MAX,2)==-EIO && write_calls==0 && !suspended; }
static bool mbr_single(void) { return _zephyr_disk_write(NULL,buffer,0,1)==0 && write_calls==0 && !content_write[0]; }
static bool mbr_multi_rejected(void) {
 int r=_zephyr_disk_write(NULL,buffer,0,8);
 return r==-EIO && write_calls==0 && !suspended && !content_write[0] && sectors[2*512]==0x33 && volume.fat.fatfs.win[0]==0x5a;
}
static bool real_partition_written(void) {
 int r=_zephyr_disk_write(NULL,buffer,1,8);
 return r==0 && write_calls==1 && suspended && content_write[0] && !memcmp(sectors+512,buffer,sizeof(buffer));
}
static bool middle_cached_sector(void) { return _zephyr_disk_write(NULL,buffer,2,8)==0 && volume.fat.fatfs.win[0]==buffer[3*512] && volume.fat.fatfs.win[511]==buffer[4*512-1]; }
static bool outside_cache_unchanged(void) { volume.fat.fatfs.winsect=10;return _zephyr_disk_write(NULL,buffer,2,8)==0 && volume.fat.fatfs.win[0]==0x5a; }
static bool crosses_fatbase(void) { volume.fat.fatfs.fatbase=5;return _zephyr_disk_write(NULL,buffer,1,8)==0 && content_write[0]; }
static bool reserved_only_no_reload(void) { volume.fat.fatfs.fatbase=5;return _zephyr_disk_write(NULL,buffer,1,4)==0 && !content_write[0]; }
static bool last_sector(void) { return _zephyr_disk_write(NULL,buffer,31,1)==0 && sectors[31*512]==buffer[0]; }
static bool successful_sync(void) { _zephyr_disk_write(NULL,buffer,2,1);return _zephyr_disk_ioctl(NULL,DISK_IOCTL_CTRL_SYNC,NULL)==0 && !suspended && !content_write[0] && trigger_calls==1; }
static bool error_sync_no_reload(void) { write_error=EIO;_zephyr_disk_write(NULL,buffer,2,1);return _zephyr_disk_ioctl(NULL,DISK_IOCTL_CTRL_SYNC,NULL)==0 && !suspended && trigger_calls==0; }
static bool prior_success_then_error(void) { _zephyr_disk_write(NULL,buffer,2,1);write_error=EIO;_zephyr_disk_write(NULL,buffer,5,1);_zephyr_disk_ioctl(NULL,DISK_IOCTL_CTRL_SYNC,NULL);return !suspended && trigger_calls==1; }
static bool ioctl_failure(void) { uint32_t value=0;ioctl_error=1;return _zephyr_disk_ioctl(NULL,DISK_IOCTL_GET_SECTOR_COUNT,&value)==-EIO && _zephyr_disk_ioctl(NULL,DISK_IOCTL_GET_SECTOR_SIZE,&value)==-EIO; }
static bool ioctl_success(void) { uint32_t value=0;return _zephyr_disk_ioctl(NULL,DISK_IOCTL_GET_SECTOR_COUNT,&value)==0 && value==32; }
static bool absent_read(void) { present=false;return _zephyr_disk_read(NULL,buffer,1,1)==-ENODEV && read_calls==0; }
static bool absent_write(void) { present=false;return _zephyr_disk_write(NULL,buffer,1,1)==-ENODEV && write_calls==0 && !suspended; }
static bool absent_ioctl(void) { present=false;return _zephyr_disk_ioctl(NULL,DISK_IOCTL_GET_SECTOR_COUNT,&count)==-ENODEV; }
static bool removed_media_sync(void) { _zephyr_disk_write(NULL,buffer,2,1);present=false;return _zephyr_disk_ioctl(NULL,DISK_IOCTL_CTRL_SYNC,NULL)==-ENODEV && !suspended && !content_write[0] && trigger_calls==0; }
static bool variable_sector_cache(void) {
 volume.fat.fatfs.ssize=1024;
 int r=_zephyr_disk_write(NULL,buffer,5,1);
 #if FF_MAX_SS != FF_MIN_SS
 return r==0 && volume.fat.fatfs.win[0]==0x5a;
 #else
 return r==0 && volume.fat.fatfs.win[0]==buffer[0];
 #endif
}
#define CASE(n) {#n,n}
static struct {const char *name;bool (*run)(void);} cases[]={
 CASE(read_ok),CASE(read_failure),CASE(read_bounds),CASE(read_overflow),CASE(read_capacity_failure),
 CASE(write_capacity_failure),CASE(write_failure),CASE(write_readonly),CASE(write_bounds),CASE(write_overflow),
 CASE(mbr_single),CASE(mbr_multi_rejected),CASE(real_partition_written),CASE(middle_cached_sector),
 CASE(outside_cache_unchanged),CASE(crosses_fatbase),CASE(reserved_only_no_reload),CASE(last_sector),
 CASE(successful_sync),CASE(error_sync_no_reload),CASE(prior_success_then_error),CASE(ioctl_failure),
 CASE(ioctl_success),CASE(absent_read),CASE(absent_write),CASE(absent_ioctl),CASE(removed_media_sync),CASE(variable_sector_cache)
};
int main(int argc,char **argv) {
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
  if(argc==1)puts(cases[i].name);
  else if(!strcmp(argv[1],cases[i].name)) {reset();bool ok=cases[i].run();printf("%s %s\n",ok?"PASS":"FAIL",cases[i].name);return ok?0:1;}
 }
 return argc==1?0:2;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--circuitpython', type=Path, default=Path('.'))
    parser.add_argument('--revision')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.circuitpython.resolve()
    paths = ['ports/zephyr-cp/supervisor/usb.c', 'supervisor/shared/flash.c', 'extmod/vfs_fat_diskio.c']
    sources = {p: subprocess.check_output(['git', '-C', str(root), 'show', args.revision + ':' + p], text=True)
               if args.revision else (root / p).read_text() for p in paths}
    usb, flash, disk = (sources[p] for p in paths)
    code = HARNESS + function(flash, 'flash_write_blocks') + BRIDGE
    code += '\n'.join(function(disk, n) for n in ('disk_read', 'disk_write'))
    if 'static int zephyr_disk_check_range(' in usb:
        code += function(usb, 'zephyr_disk_check_range')
    code += '\n'.join(function(usb, n) for n in ('_zephyr_disk_read', '_zephyr_disk_write', '_zephyr_disk_ioctl'))
    code += TESTS
    results = []
    with tempfile.TemporaryDirectory(prefix='cp-msc-io-') as tmp:
        source = Path(tmp) / 'msc.c'
        source.write_text(code)
        for maximum in (512, 4096):
            exe = Path(tmp) / ('msc-' + str(maximum))
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                            '-DFF_MAX_SS=' + str(maximum), str(source), '-o', str(exe)], check=True)
            for case in subprocess.check_output([str(exe)], text=True).splitlines():
                p = subprocess.run([str(exe), case], capture_output=True, text=True)
                print(str(maximum), p.stdout.strip() or ('FAIL ' + case), flush=True)
                if p.stderr:
                    print(p.stderr, flush=True)
                results.append(dict(max_sector_size=maximum, case=case, returncode=p.returncode,
                                    stdout=p.stdout, stderr=p.stderr))
    evidence = dict(revision=args.revision or 'working-tree',
                    source_sha256={p: hashlib.sha256(s.encode()).hexdigest() for p, s in sources.items()},
                    results=results, scope='Actual MSC wrappers, FatFs read/write adapters and synthetic-MBR block-write code; mocked flash, capacity, autoreload and injected failures. No physical USB.')
    if args.output:
        args.output.write_text(json.dumps(evidence, indent=2) + '\n')
    failed = sum(r['returncode'] != 0 for r in results)
    print('SUMMARY', len(results), 'checks,', failed, 'failures')
    return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
