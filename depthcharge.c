/*
 * Copyright (c) 2012 The Chromium OS Authors.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <libpayload-config.h>
#include <libpayload.h>

#include <vboot_api.h>

VbCommonParams cparams = {
	.gbb_data = NULL,
	.gbb_size = 0,
	.shared_data_blob = NULL,
	.shared_data_size = 0,
	.vboot_context = NULL,
	.caller_context = NULL
};

static int vboot_init(void)
{
	VbInitParams iparams = {
		.flags = 0
	};

	// Decide what flags to pass into VbInit.
	if (1)
		iparams.flags |= VB_INIT_FLAG_DEV_SWITCH_ON;
	if (0)
		iparams.flags |= VB_INIT_FLAG_REC_BUTTON_PRESSED;
	if (1)
		iparams.flags |= VB_INIT_FLAG_WP_ENABLED;
	if (0)
		iparams.flags |= VB_INIT_FLAG_S3_RESUME;
	if (0)
		iparams.flags |= VB_INIT_FLAG_PREVIOUS_BOOT_FAIL;
	if (0)
		iparams.flags |= VB_INIT_FLAG_RO_NORMAL_SUPPORT;

	printf("Calling VbInit().\n");
	VbError_t res = VbInit(&cparams, &iparams);
	if (res != VBERROR_SUCCESS)
		return 1;

	// Figure out what VbInit wants us to do now.
	if (iparams.out_flags && VB_INIT_OUT_ENABLE_RECOVERY)
		printf("VB_INIT_OUT_ENABLE_RECOVERY set but ignored.\n");
	if (iparams.out_flags && VB_INIT_OUT_CLEAR_RAM)
		printf("VB_INIT_OUT_CLEAR_RAM set but ignored.\n");
	if (iparams.out_flags && VB_INIT_OUT_ENABLE_DISPLAY)
		printf("VB_INIT_OUT_ENABLE_DISPLAY set but ignored.\n");
	if (iparams.out_flags && VB_INIT_OUT_ENABLE_USB_STORAGE)
		printf("VB_INIT_OUT_ENABLE_USB_STORAGE set but ignored.\n");
	if (iparams.out_flags && VB_INIT_OUT_S3_DEBUG_BOOT)
		printf("VB_INIT_OUT_S3_DEBUG_BOOT set but ignored.\n");
	if (iparams.out_flags && VB_INIT_OUT_ENABLE_OPROM)
		printf("VB_INIT_OUT_ENABLE_OPROM set but ignored.\n");
	if (iparams.out_flags && VB_INIT_OUT_ENABLE_ALTERNATE_OS)
		printf("VB_INIT_OUT_ENABLE_ALTERNATE_OS set but ignored.\n");
	return 0;
};

static int vboot_select_firmware(void)
{
	VbSelectFirmwareParams fparams = {
		.verification_block_A = NULL,
		.verification_block_B = NULL,
		.verification_size_A = 0,
		.verification_size_B = 0
	};

	printf("Calling VbSelectFirmware().\n");
	VbError_t res = VbSelectFirmware(&cparams, &fparams);
	if (res != VBERROR_SUCCESS)
		return 1;

	printf("Selected firmware: ");
	switch (fparams.selected_firmware) {
	case VB_SELECT_FIRMWARE_RECOVERY:
		printf("recovery\n");
		break;
	case VB_SELECT_FIRMWARE_A:
		printf("a\n");
		break;
	case VB_SELECT_FIRMWARE_B:
		printf("b\n");
		break;
	case VB_SELECT_FIRMWARE_READONLY:
		printf("read only\n");
		break;
	}
	return 0;
}

static int vboot_select_and_load_kernel(void)
{
	VbSelectAndLoadKernelParams kparams = {
		.kernel_buffer = NULL,
		.kernel_buffer_size = 0
	};

	printf("Calling VbSelectAndLoadKernel().\n");
	VbError_t res = VbSelectAndLoadKernel(&cparams, &kparams);
	if (res != VBERROR_SUCCESS)
		return 1;

	printf("Disk handle = %p.\n", kparams.disk_handle);
	printf("Partition number = %d.\n", kparams.partition_number);
	printf("Bootloader address = %lld.\n", kparams.bootloader_address);
	printf("Bootloader size = %d.\n", kparams.bootloader_size);
	printf("Partition guid =");
	for (int i = 0; i < ARRAY_SIZE(kparams.partition_guid); i++)
		printf(" %02X", kparams.partition_guid[i]);
	printf(".\n");
	return 0;
}

int main(void)
{
	// Let the world know we're alive.
	outb(0xaa, 0x80);
	printf("\n\nStarting depthcharge...\n");

	if (vboot_init())
		halt();
	if (vboot_select_firmware())
		halt();
	if (vboot_select_and_load_kernel())
		halt();
	return 0;
}
