/*
 * Minimal eGON.BT0 header embedded at image start.
 */

	.section .sunxi_egon_header, "ax"
	.balign 4
	.global __sunxi_egon_header_start
__sunxi_egon_header_start:
#if defined(__riscv)
	j __start
#elif defined(__arm__)
	b __start
#else
	#error "Unsupported arch for eGON header"
#endif
	.ascii "eGON.BT0" /* exactly 8 bytes, no NUL */
	.word 0x00000000 /* checksum (patched post-build) */
	.word 0x00000000 /* length   (patched post-build) */
	.word 0x00000030 /* pub_head_size */
	.byte 0x00, 0x00, 0x00, 0x00 /* pub_head_vsn */
	.byte 0x00, 0x00, 0x00, 0x00 /* file_head_vsn */
	.byte 0x00, 0x00, 0x00, 0x00 /* Boot_vsn */
	.byte 0x00, 0x00, 0x00, 0x00 /* eGON_vsn */
	.byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /* platform */
	.balign 4
