.. _allwinner_egon_header:

Booting Allwinner SoCs (eGON.BT0)
=================================

Overview
********
Allwinner SoCs boot from an on‑chip BROM (boot ROM). The BROM loads a bootable
image into SRAM and jumps to its entry point. On sunxi devices, that image
starts with an eGON.BT0 header (sometimes called a “boot0” header).

Authoritative references (linux-sunxi):

- `BROM <https://linux-sunxi.org/BROM>`_
- `eGON <https://linux-sunxi.org/EGON>`_

Most documented SoCs have a BROM image size limit of 24 KiB or 32 KiB for the
SRAM‑loaded first stage. Keep your SRAM image within that limit.

In Zephyr, a minimal eGON.BT0 header is embedded directly into ``zephyr.bin``.
The header is placed at the start of the image and contains a single jump
instruction to ``__start``. A “flash‑ready” binary can also be loaded and run
using tools like ``xfel``.

Header patching is performed by ``tools/sunxi/mksunxiboot.py``. It validates the
header at offset 0, pads the image, and fills the length/checksum fields.

Build process
*************
Build for SRAM
==============
Build for SRAM by using the SRAM overlay and keeping the image size within the
SRAM/BROM limit.

Header patching
===============
The eGON header is embedded by the linker. After linking, the patching tool
updates the header fields in place. This is invoked automatically for Allwinner
SoCs via a POST_BUILD step.

Manual usage (if needed)::

   ./tools/sunxi/mksunxiboot.py build/zephyr/zephyr.bin build/zephyr/zephyr.bin

Build for DDR
=============
To run Zephyr from DDR, link the image into a DDR region via a devicetree
overlay and load it to the matching DDR address.

Specify the memory target by setting ``zephyr,sram`` to a DDR memory node::

   / {
       chosen {
           zephyr,sram = &dram0;
       };
   };

Reserve memory for other payloads by defining a ``reserved-memory`` region in
an application overlay and marking it ``no-map``::

   / {
       reserved-memory {
           #address-cells = <1>;
           #size-cells = <1>;
           ranges;

           linux_payload: payload@40800000 {
               reg = <0x40800000 0x1F800000>;
               no-map;
           };
       };
   };

Development runs
****************
Running from SRAM
=================
Example build and run (SRAM)::

   west build -b mangopi_mq1w zephyr/samples/basic/blinky --pristine -- \
     -DOVERLAY_CONFIG=zephyr/boards/mangopi/mq1w/mangopi_mq1w_sram.overlay
   xfel write 0x20000 build/zephyr/zephyr.bin
   xfel exec 0x20000

Running from DDR
================
Example build and run (DDR)::

   west build -b mangopi_mq1w zephyr/samples/basic/blinky --pristine
   xfel ddr f133
   xfel write 0x40000000 build/zephyr/zephyr.bin
   xfel exec 0x40000000
