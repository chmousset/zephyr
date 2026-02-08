.. zephyr:board:: mangopi_mq1w

Overview
********
The MangoPi MQ1W is a compact Allwinner D1s/F133 (RISC-V) board. Zephyr uses
UART3 as the default console at 115200 baud and exposes a single status LED.

Hardware
********
* Allwinner D1s/F133 (RISC-V)
* 64 MiB DDR (see ``dram0`` in the board devicetree)

Supported Features
==================

.. zephyr:board-supported-hw::

Connections and IOs
===================

LED
---
* LED0 (STATUS) = PD22 (active low)

UART
----
* UART3 TX = PB6
* UART3 RX = PB7

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

This board can be loaded via FEL using :ref:`xfel`. See the Allwinner booting
documentation for details about the eGON.BT0 header and SRAM/DDR workflows:
:ref:`allwinner_egon_header`.

Flashing
========
Build and run from SRAM using the SRAM overlay and FEL:

.. code-block:: console

   west build -b mangopi_mq1w zephyr/samples/basic/blinky --pristine -- \
      -DOVERLAY_CONFIG=zephyr/boards/mangopi/mq1w/mangopi_mq1w_sram.overlay
   xfel write 0x20000 build/zephyr/zephyr.bin
   xfel exec 0x20000

Build and run from DDR:

.. code-block:: console

   west build -b mangopi_mq1w zephyr/samples/basic/blinky --pristine
   xfel ddr f133
   xfel write 0x40000000 build/zephyr/zephyr.bin
   xfel exec 0x40000000

Debugging
=========
Use UART3 at 115200 baud for console output. It's located on the 2-pins
terminal marked "3" near the camera and display connector.

References
**********
* `MangoPi MQ1W product page`_
* `Allwinner BROM documentation`_
* `eGON.BT0 documentation`_
* `Allwinner SoC IP cores`_ — documents which peripheral IP blocks are shared
  across Allwinner SoC generations; used to determine driver naming conventions
  (e.g. ``sun6i`` for SPI/I2C shared IP vs ``sun20i_d1`` for D1-specific blocks)
* `xfel`_

.. _MangoPi MQ1W product page: https://mangopi.org/mangopi_mq
.. _Allwinner BROM documentation: https://linux-sunxi.org/BROM
.. _eGON.BT0 documentation: https://linux-sunxi.org/EGON
.. _Allwinner SoC IP cores: https://linux-sunxi.org/Used_IP_cores
.. _xfel: https://github.com/xboot/xfel
