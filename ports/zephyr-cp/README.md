# Zephyr

This is an initial port of CircuitPython onto Zephyr. We intend on migrating all
existing boards onto Zephyr. To start, we'll only support new boards. Existing
boards will be switched as the Zephyr port reaches parity with the existing
implementation.

## Getting Started

First, install Zephyr tools (see [Zephyr's Getting Started Guide](https://docs.zephyrproject.org/4.0.0/develop/getting_started/index.html)). (These are `fish` commands because that's what Scott uses.)


```sh
pip install west
west init -l zephyr-config
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west sdk install
```

Now to build from `ports/zephyr-cp`:

```sh
make BOARD=nordic_nrf7002dk
```

This uses Zephyr's cmake to generate Makefiles that then delegate to
`tools/cpbuild/build_circuitpython.py` to build the CircuitPython bits in parallel.

## Native simulator build container

Building the native sim requires `libsdl2-dev:i386` and other 32bit dependencies that
can cause conflicts on 64bit systems resulting in the removal of 64bit versions of critical
software such as the display manager and network manager. A Containerfile and a few scripts
are provided to set up a container to make the native sim build inside without affecting the
host system.

The container automatically mounts this instance of the circuitpython repo inside at
`/home/dev/circuitpython`. Changes made in the repo inside the container and on the host PC
will sync automatically between host and container.

To use the container file:

1. Build the container with `podman build -t zephyr-cp-dev -f native_sim_build_Containerfile .`
2. Run/Start the container by running `./native_sim_build_run_container.sh` on the host PC.
   The script will automatically run or start based on whether the container has been run before.
3. Init requirements inside the container with `./native_sim_build_init_container.sh`

To delete the container and cleanup associated files:
```sh
podman ps -a --filter ancestor=zephyr-cp-dev -q | xargs -r podman rm -f
podman rmi zephyr-cp-dev
podman image prune -f
podman rm -f zcp
```

## Running the native simulator

From `ports/zephyr-cp`, run:

```sh
make run-sim
```

`run-sim` starts the native simulator in realtime.
It prints the PTY path to connect to the simulator REPL.
If a local `./CIRCUITPY/` folder exists, its files are used as the simulator's CIRCUITPY drive.

Edit files in `./CIRCUITPY` (for example `code.py`) and rerun `make run-sim` to test changes.

## Shields

Board defaults can be set in `boards/<vendor>/<board>/circuitpython.toml`:

```toml
SHIELDS = ["shield1", "shield2"]
```

For example, `boards/renesas/ek_ra8d1/circuitpython.toml` enables:

```toml
SHIELDS = ["rtkmipilcdb00000be"]
```

You can override shield selection from the command line:

```sh
# Single shield
make BOARD=renesas_ek_ra8d1 SHIELD=rtkmipilcdb00000be

# Multiple shields (comma, semicolon, or space separated)
make BOARD=my_vendor_my_board SHIELDS="shield1,shield2"
```

Behavior and precedence:

- If `SHIELD` or `SHIELDS` is explicitly provided, it overrides board defaults.
- If neither is provided, defaults from `circuitpython.toml` are used.
- Use `SHIELD=` (empty) to disable a board default shield for one build.

## Testing other boards

[Any Zephyr board](https://docs.zephyrproject.org/latest/boards/index.html#) can
be used with CircuitPython. To test a different board, use `west` directly to
build the board. The build will do its best to support as much as possible. By
default the Zephyr console will be used for output. USB support is limited by
initialization support in `supervisor/usb.c`. Only flash regions not used by
Zephyr are used for CIRCUITPY. A manual `circuitpython` partition can be
specified instead.

For example, to test the `nrf52840dk` board:

```sh
west build -b nrf52840dk/nrf52840
```

This is already supported in `ports/nordic` as `pca10056`.

## Board overlay pitfalls

Two things about `boards/<board>.overlay` that are easy to get wrong and fail in
ways that point away from the cause.

### Keep `ranges;` when replacing the partitions node

If an overlay does `/delete-node/ partitions;` and rebuilds the node, it must
re-add `ranges;`. Board DTS files put it there, and it is what lets devicetree
address translation walk from a partition up through the flash node's
`ranges = <0x0 0x10000000 ...>` into the SoC address space. Drop it and the code
partition resolves to a raw offset instead of an absolute address:

```
code_partition REG_IDX_0_VAL_ADDRESS = 256          /* 0x100, wrong  */
                                     = 268435712    /* 0x10000100    */
```

Prefer merging into the existing `partitions` node over deleting and recreating
it, which avoids the problem entirely.

On a SoC whose flash base is not `0x0` this produces an unbootable image, and the
symptom is remote from the cause. With `CONFIG_FLASH_USES_MAPPED_PARTITION=y` the
linker takes `ROM_ADDR` straight from the partition address, so the whole image
is mis-linked. On RP2040 it also silently disables the second-stage bootloader:
`soc/raspberrypi/rpi_pico/rp2040/Kconfig` gates `RP2_REQUIRES_SECOND_STAGE_BOOT`
on the address being *exactly* `0x10000100`, so `.boot2` is omitted from the ELF
altogether and the chip drops back to BOOTSEL when flashed.

Overlays whose flash base is `0x0` (the nRF boards, `native_sim`) are unaffected,
because the untranslated offset happens to equal the absolute address.

### Size the settings partition for the flash geometry

Any board enabling `CONFIG_BT_SETTINGS` (which Bluetooth bond keys need) must
give the `storage` partition at least **two erase sectors**, aligned to an erase
sector boundary. NVS needs two sectors minimum, and `flash_area_get_sectors()`
reports zero sectors for a partition too small or misaligned to hold one.

`settings_subsys_init()` then fails and `bt_enable()` returns before it ever
opens the HCI driver, which surfaces to Python as a bare `OSError` from
`import _bleio` with nothing pointing at flash layout. On a part with 4K sectors
that means 8K aligned to 4K, not the 2K some boards started with.
