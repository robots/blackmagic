/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file implements support for STM32WB0x series devices, providing
 * memory maps and Flash programming routines.
 *
 * References:
 * RM0530 - STM32WB07xC and STM32WB06xC ultra-low power wireless 32-bit MCUs Arm®-based Cortex®-M0+ with Bluetooth® Low Energy and 2.4 GHz radio solution, Rev. 1
 * - https://www.st.com/resource/en/reference_manual/rm0530--stm32wb07xc-and-stm32wb06xc-ultralow-power-wireless-32bit-mcus-armbased-cortexm0-with-bluetooth-low-energy-and-24-ghz-radio-solution-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"

/* Memory map constants for STM32WB0x */
#define STM32WB0_FLASH_BANK_BASE 0x10000000U
#define STM32WB0_FLASH_BANK_SIZE 0x00080000U
#define STM32WB0_SRAM_BASE       0x20000000U
#define STM32WB0_SRAM_SIZE       0x00010000U

#define STM32WB0_FLASH_BASE       0x40001000U
#define STM32WB0_FLASH_FLASH_SIZE (STM32WB0_FLASH_BASE + 0x014U)

#define STM32WB0_PWRC_BASE 0x48500000U
#define STM32WB0_PWRC_DBGR (STM32WB0_PWRC_BASE + 0x084U)

#define STM32WB0_PWRC_DBGR_DEEPSTOP2 (1U << 0U)

#define ID_STM32WB0 0x01eU

static void stm32wb0_add_flash(target_s *const target, const size_t length)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = STM32WB0_FLASH_BANK_BASE;
	flash->length = length;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

uint32_t stm32wb0_ram_size(const uint32_t signature)
{
	/* Determine how much RAM is available on the device */
	switch ((signature >> 17U) & 3U) {
	case 2U:
		/* 48KiB */
		return UINT32_C(48) * 1024U;
	case 3U:
		/* 64KiB */
		return UINT32_C(64) * 1024U;
	default:
		/* 32KiB */
		return UINT32_C(32) * 1024U;
	}
}

bool stm32wb0_probe(target_s *const target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	/* Use the partno from the AP always to handle the difference between JTAG and SWD */
	if (ap->partno != ID_STM32WB0)
		return false;
	target->part_id = ap->partno;

	/* Prevent deep sleeping from taking the debug link out */
	target_mem32_write16(target, STM32WB0_PWRC_DBGR, STM32WB0_PWRC_DBGR_DEEPSTOP2);

	target->driver = "STM32WB0";

	const uint32_t signature = target_mem32_read32(target, STM32WB0_FLASH_FLASH_SIZE);
	target_add_ram32(target, STM32WB0_SRAM_BASE, stm32wb0_ram_size(signature));
	stm32wb0_add_flash(target, ((signature & 0x0000ffffU) + 1U) << 2U);
	return true;
}
