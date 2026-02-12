.. _allwinner_ip_cores:

Allwinner IP Cores and Driver Naming
====================================

Allwinner reuses peripheral IP blocks across SoC generations. The linux-sunxi
wiki maintains a comprehensive list:

- `Used IP cores <https://linux-sunxi.org/Used_IP_cores>`_

This page documents which peripheral blocks (SPI, I2C, CCU, GPIO, PWM, etc.)
are shared between SoC families, and directly informs the naming convention
used in both the Linux kernel and Zephyr drivers.

Naming convention
*****************

Zephyr follows the Linux kernel convention for Allwinner driver naming:

Shared IP (``sun6i``)
   IP blocks unchanged since an earlier SoC generation. Linux uses generic
   fallback compatible strings such as ``allwinner,sun6i-a31-spi`` and
   ``allwinner,sun6i-a31-i2c``. These drivers can be reused across many SoC
   families (D1, T113, H3, A64, etc.) without modification.

   Zephyr drivers: ``spi_sun6i.c``, ``i2c_sun6i.c``

SoC-specific (``sun20i_d1``)
   IP blocks with register layouts specific to a particular SoC family. Linux
   uses SoC-specific compatible strings such as ``allwinner,sun20i-d1-ccu``.

   Zephyr drivers: ``clock_control_sun20i_d1.c``, ``pinctrl_sun20i_d1.c``,
   ``gpio_sun20i_d1.c``, ``pwm_sun20i_d1.c``, ``sun20i_d1_timer.c``

Adding support for a new SoC
*****************************

When adding a new Allwinner SoC (e.g. T113):

1. Check `Used IP cores <https://linux-sunxi.org/Used_IP_cores>`_ to determine
   which IP blocks are shared with already-supported SoCs.

2. Shared IP drivers (e.g. ``spi_sun6i.c``) can be reused directly. Add a new
   DTS compatible and binding if the Linux kernel uses a different primary
   compatible string for that SoC.

3. For SoC-specific IP blocks, either extend an existing driver (if the register
   layout is identical) or create a new one following the ``<subsystem>_<family>.c``
   naming pattern.
