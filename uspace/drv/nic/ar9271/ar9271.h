/*
 * Copyright (c) 2014 Jan Kolarik
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file ar9271.h
 *
 * Header file for AR9271 USB WiFi dongle.
 *
 */

#ifndef AR9271_H_
#define AR9271_H_

#include <usb/dev/driver.h>

#include "htc.h"

/** Number of transmission queues */
#define AR9271_QUEUES_COUNT 10

/** Number of GPIO pin used for handling led light */
#define AR9271_LED_PIN 15

/** AR9271 Registers */
typedef enum {
	/* ATH command register */
	AR9271_COMMAND = 0x0008,
	AR9271_COMMAND_RX_ENABLE = 0x00000004,
	
	/* ATH config register */
	AR9271_CONFIG = 0x0014,
	AR9271_CONFIG_ADHOC = 0x00000020,
	
	AR9271_QUEUE_BASE_MASK = 0x1000,
	
	/* EEPROM Addresses */
	AR9271_EEPROM_BASE = 0x2100,
	AR9271_EEPROM_MAC_ADDR_START = 0x2118,
	
	/* Reset MAC interface */
	AR9271_RC = 0x4000,
	AR9271_RC_AHB = 0x00000001,
		
	/* GPIO registers */
	AR9271_GPIO_IN_OUT = 0x4048,		/**< GPIO value read/set  */
	AR9271_GPIO_OE_OUT = 0x404C,		/**< GPIO set to output  */
	AR9271_GPIO_OE_OUT_ALWAYS = 0x3,	/**< GPIO always drive output */
	AR9271_GPIO_OUT_MUX1 = 0x4060,
	AR9271_GPIO_OUT_MUX2 = 0x4064,
	AR9271_GPIO_OUT_MUX3 = 0x4068,
	AR9271_GPIO_OUT_MUX_AS_OUT = 0x0,	/**< GPIO set mux as output */
    
	/* Wakeup related registers */
	AR9271_RTC_RC = 0x7000,
	AR9271_RTC_RC_MAC_WARM = 0x00000001,
	AR9271_RTC_RC_MAC_COLD = 0x00000002,
	AR9271_RTC_RC_MASK = 0x00000003,
	AR9271_RTC_PLL_CONTROL = 0x7014,
	AR9271_RTC_RESET = 0x7040,
	AR9271_RTC_STATUS = 0x7044,
	AR9271_RTC_STATUS_MASK = 0x0000000F,
	AR9271_RTC_STATUS_SHUTDOWN = 0x00000001,
	AR9271_RTC_STATUS_ON = 0x00000002,
	AR9271_RTC_FORCE_WAKE = 0x704C,
	AR9271_RTC_FORCE_WAKE_ENABLE = 0x00000001,
	AR9271_RTC_FORCE_WAKE_ON_INT = 0x00000002,
		
	/* MAC Registers */
	AR9271_STATION_ID0 = 0x8000,	/**< STA Address Lower 32 Bits */
	AR9271_STATION_ID1 = 0x8004,	/**< STA Address Upper 16 Bits */
	AR9271_BSSID0 = 0x8008,			/**< BSSID Lower 32 Bits */
	AR9271_BSSID1 = 0x800C,			/**< BSSID Upper 16 Bits */
	AR9271_BSSID_MASK0 = 0x80E0,		/**< BSSID Mask Lower 32 Bits */
	AR9271_BSSID_MASK1 = 0x80E4,		/**< BSSID Mask Upper 16 Bits */
	AR9271_STATION_ID1_MASK = 0x0000FFFF,
	AR9271_STATION_ID1_POWER_SAVING = 0x00040000,
		
	/* RX filtering register */
	AR9271_RX_FILTER = 0x803C,
	AR9271_RX_FILTER_UNI = 0x00000001,
	AR9271_RX_FILTER_MULTI = 0x00000002,
	AR9271_RX_FILTER_BROAD = 0x00000004,
	AR9271_RX_FILTER_CONTROL = 0x00000008,
	AR9271_RX_FILTER_BEACON = 0x00000010,
	AR9271_RX_FILTER_PROMISCUOUS = 0x00000020,
	AR9271_RX_FILTER_PROBEREQ = 0x00000080,
	AR9271_RX_FILTER_MYBEACON = 0x00000200,
	AR9271_MULTICAST_FILTER1 = 0x8040,
	AR9271_MULTICAST_FILTER2 = 0x8044,	
	AR9271_DIAG = 0x8048,
		
	/* Physical layer registers */
	AR9271_PHY_ACTIVE = 0x981C,
	AR9271_ADC_CONTROL = 0x982C,
	AR9271_AGC_CONTROL = 0x9860,
	AR9271_PHY_SYNTH_CONTROL = 0x9874, 
	AR9271_PHY_SPECTRAL_SCAN = 0x9910,
	AR9271_PHY_RADAR0 = 0x9954,
	AR9271_PHY_RADAR0_FFT_ENABLED = 0x80000000,
	AR9271_PHY_RFBUS_KILL = 0x997C,
	AR9271_PHY_RFBUS_GRANT = 0x9C20,
	AR9271_PHY_MODE = 0xA200,
	AR9271_PHY_CCK_TX_CTRL = 0xA204,
	AR9271_PHY_TPCRG1 = 0xA258, 
	AR9271_CARRIER_LEAK_CONTROL = 0xA358,
	AR9271_ADC_CONTROL_OFF_PWDADC = 0x00008000,
	AR9271_AGC_CONTROL_CALIB = 0x00000001,
	AR9271_AGC_CONTROL_NF_CALIB = 0x00000002,
	AR9271_AGC_CONTROL_NF_CALIB_EN = 0x00008000, 
	AR9271_AGC_CONTROL_TX_CALIB = 0x00010000,
	AR9271_AGC_CONTROL_NF_NOT_UPDATE = 0x00020000,
	AR9271_PHY_MODE_DYNAMIC = 0x04,
	AR9271_PHY_CCK_TX_CTRL_JAPAN = 0x00000010,
	AR9271_PHY_TPCRG1_PD_CALIB = 0x00400000,
	AR9271_CARRIER_LEAK_CALIB = 0x00000002,
		
	AR9271_OPMODE_STATION_AP_MASK =	0x00010000,
	AR9271_OPMODE_ADHOC_MASK = 0x00020000,
		
	AR9271_CLOCK_CONTROL = 0x50040,
	AR9271_MAX_CPU_CLOCK = 0x304,
		
	AR9271_RESET_POWER_DOWN_CONTROL = 0x50044,
	AR9271_RADIO_RF_RESET = 0x20,
	AR9271_GATE_MAC_CONTROL = 0x4000,
    
	/* FW Addresses */
	AR9271_FW_ADDRESS = 0x501000,
	AR9271_FW_OFFSET = 0x903000,
} ar9271_registers_t;

/** AR9271 Requests */
typedef enum {
	AR9271_FW_DOWNLOAD = 0x30,
	AR9271_FW_DOWNLOAD_COMP = 0x31,
} ar9271_requests_t;

/** AR9271 device data */
typedef struct {
	/** Lock for access. */
	fibril_mutex_t ar9271_lock;
	
	/** Whether device is starting up. */
	bool starting_up;
	
	/** Backing DDF device */
	ddf_dev_t *ddf_dev;
	
	/** USB device data */
	usb_device_t *usb_device;
	
	/** IEEE 802.11 device data */
	ieee80211_dev_t *ieee80211_dev;
	
	/** ATH device data */
	ath_t *ath_device;
	
	/** HTC device data */
	htc_device_t *htc_device;
} ar9271_t;

/**
 * AR9271 init values for 2GHz mode operation.
 * 
 * Including settings of noise floor limits.
 * 
 * Taken from Linux sources.
 */
static const uint32_t ar9271_2g_mode_array[][2] = {
	{0x00001030, 0x00000160},
	{0x00001070, 0x0000018c},
	{0x000010b0, 0x00003e38},
	{0x000010f0, 0x00000000},
	{0x00008014, 0x08400b00},
	{0x0000801c, 0x12e0002b},
	{0x00008318, 0x00003440},
	{0x00009804, 0x000003c0},/*< note: overridden */
	{0x00009820, 0x02020200},
	{0x00009824, 0x01000e0e},
	{0x00009828, 0x0a020001},/*< note: overridden */
	{0x00009834, 0x00000e0e},
	{0x00009838, 0x00000007},
	{0x00009840, 0x206a012e},
	{0x00009844, 0x03721620},
	{0x00009848, 0x00001053},
	{0x0000a848, 0x00001053},
	{0x00009850, 0x6d4000e2},
	{0x00009858, 0x7ec84d2e},
	{0x0000985c, 0x3137605e},
	{0x00009860, 0x00058d18},
	{0x00009864, 0x0001ce00},
	{0x00009868, 0x5ac640d0},
	{0x0000986c, 0x06903881},
	{0x00009910, 0x30002310},
	{0x00009914, 0x00000898},
	{0x00009918, 0x0000000b},
	{0x00009924, 0xd00a800d},
	{0x00009944, 0xffbc1020},
	{0x00009960, 0x00000000},
	{0x00009964, 0x00000000},
	{0x000099b8, 0x0000421c},
	{0x000099bc, 0x00000c00},
	{0x000099c0, 0x05eea6d4},
	{0x000099c4, 0x06336f77},
	{0x000099c8, 0x6af6532f},
	{0x000099cc, 0x08f186c8},
	{0x000099d0, 0x00046384},
	{0x000099d4, 0x00000000},
	{0x000099d8, 0x00000000},
	{0x00009a00, 0x00058084},
	{0x00009a04, 0x00058088},
	{0x00009a08, 0x0005808c},
	{0x00009a0c, 0x00058100},
	{0x00009a10, 0x00058104},
	{0x00009a14, 0x00058108},
	{0x00009a18, 0x0005810c},
	{0x00009a1c, 0x00058110},
	{0x00009a20, 0x00058114},
	{0x00009a24, 0x00058180},
	{0x00009a28, 0x00058184},
	{0x00009a2c, 0x00058188},
	{0x00009a30, 0x0005818c},
	{0x00009a34, 0x00058190},
	{0x00009a38, 0x00058194},
	{0x00009a3c, 0x000581a0},
	{0x00009a40, 0x0005820c},
	{0x00009a44, 0x000581a8},
	{0x00009a48, 0x00058284},
	{0x00009a4c, 0x00058288},
	{0x00009a50, 0x00058224},
	{0x00009a54, 0x00058290},
	{0x00009a58, 0x00058300},
	{0x00009a5c, 0x00058304},
	{0x00009a60, 0x00058308},
	{0x00009a64, 0x0005830c},
	{0x00009a68, 0x00058380},
	{0x00009a6c, 0x00058384},
	{0x00009a70, 0x00068700},
	{0x00009a74, 0x00068704},
	{0x00009a78, 0x00068708},
	{0x00009a7c, 0x0006870c},
	{0x00009a80, 0x00068780},
	{0x00009a84, 0x00068784},
	{0x00009a88, 0x00078b00},
	{0x00009a8c, 0x00078b04},
	{0x00009a90, 0x00078b08},
	{0x00009a94, 0x00078b0c},
	{0x00009a98, 0x00078b80},
	{0x00009a9c, 0x00078b84},
	{0x00009aa0, 0x00078b88},
	{0x00009aa4, 0x00078b8c},
	{0x00009aa8, 0x00078b90},
	{0x00009aac, 0x000caf80},
	{0x00009ab0, 0x000caf84},
	{0x00009ab4, 0x000caf88},
	{0x00009ab8, 0x000caf8c},
	{0x00009abc, 0x000caf90},
	{0x00009ac0, 0x000db30c},
	{0x00009ac4, 0x000db310},
	{0x00009ac8, 0x000db384},
	{0x00009acc, 0x000db388},
	{0x00009ad0, 0x000db324},
	{0x00009ad4, 0x000eb704},
	{0x00009ad8, 0x000eb6a4},
	{0x00009adc, 0x000eb6a8},
	{0x00009ae0, 0x000eb710},
	{0x00009ae4, 0x000eb714},
	{0x00009ae8, 0x000eb720},
	{0x00009aec, 0x000eb724},
	{0x00009af0, 0x000eb728},
	{0x00009af4, 0x000eb72c},
	{0x00009af8, 0x000eb7a0},
	{0x00009afc, 0x000eb7a4},
	{0x00009b00, 0x000eb7a8},
	{0x00009b04, 0x000eb7b0},
	{0x00009b08, 0x000eb7b4},
	{0x00009b0c, 0x000eb7b8},
	{0x00009b10, 0x000eb7a5},
	{0x00009b14, 0x000eb7a9},
	{0x00009b18, 0x000eb7ad},
	{0x00009b1c, 0x000eb7b1},
	{0x00009b20, 0x000eb7b5},
	{0x00009b24, 0x000eb7b9},
	{0x00009b28, 0x000eb7c5},
	{0x00009b2c, 0x000eb7c9},
	{0x00009b30, 0x000eb7d1},
	{0x00009b34, 0x000eb7d5},
	{0x00009b38, 0x000eb7d9},
	{0x00009b3c, 0x000eb7c6},
	{0x00009b40, 0x000eb7ca},
	{0x00009b44, 0x000eb7ce},
	{0x00009b48, 0x000eb7d2},
	{0x00009b4c, 0x000eb7d6},
	{0x00009b50, 0x000eb7c3},
	{0x00009b54, 0x000eb7c7},
	{0x00009b58, 0x000eb7cb},
	{0x00009b5c, 0x000eb7cf},
	{0x00009b60, 0x000eb7d7},
	{0x00009b64, 0x000eb7db},
	{0x00009b68, 0x000eb7db},
	{0x00009b6c, 0x000eb7db},
	{0x00009b70, 0x000eb7db},
	{0x00009b74, 0x000eb7db},
	{0x00009b78, 0x000eb7db},
	{0x00009b7c, 0x000eb7db},
	{0x00009b80, 0x000eb7db},
	{0x00009b84, 0x000eb7db},
	{0x00009b88, 0x000eb7db},
	{0x00009b8c, 0x000eb7db},
	{0x00009b90, 0x000eb7db},
	{0x00009b94, 0x000eb7db},
	{0x00009b98, 0x000eb7db},
	{0x00009b9c, 0x000eb7db},
	{0x00009ba0, 0x000eb7db},
	{0x00009ba4, 0x000eb7db},
	{0x00009ba8, 0x000eb7db},
	{0x00009bac, 0x000eb7db},
	{0x00009bb0, 0x000eb7db},
	{0x00009bb4, 0x000eb7db},
	{0x00009bb8, 0x000eb7db},
	{0x00009bbc, 0x000eb7db},
	{0x00009bc0, 0x000eb7db},
	{0x00009bc4, 0x000eb7db},
	{0x00009bc8, 0x000eb7db},
	{0x00009bcc, 0x000eb7db},
	{0x00009bd0, 0x000eb7db},
	{0x00009bd4, 0x000eb7db},
	{0x00009bd8, 0x000eb7db},
	{0x00009bdc, 0x000eb7db},
	{0x00009be0, 0x000eb7db},
	{0x00009be4, 0x000eb7db},
	{0x00009be8, 0x000eb7db},
	{0x00009bec, 0x000eb7db},
	{0x00009bf0, 0x000eb7db},
	{0x00009bf4, 0x000eb7db},
	{0x00009bf8, 0x000eb7db},
	{0x00009bfc, 0x000eb7db},
	{0x0000aa00, 0x00058084},
	{0x0000aa04, 0x00058088},
	{0x0000aa08, 0x0005808c},
	{0x0000aa0c, 0x00058100},
	{0x0000aa10, 0x00058104},
	{0x0000aa14, 0x00058108},
	{0x0000aa18, 0x0005810c},
	{0x0000aa1c, 0x00058110},
	{0x0000aa20, 0x00058114},
	{0x0000aa24, 0x00058180},
	{0x0000aa28, 0x00058184},
	{0x0000aa2c, 0x00058188},
	{0x0000aa30, 0x0005818c},
	{0x0000aa34, 0x00058190},
	{0x0000aa38, 0x00058194},
	{0x0000aa3c, 0x000581a0},
	{0x0000aa40, 0x0005820c},
	{0x0000aa44, 0x000581a8},
	{0x0000aa48, 0x00058284},
	{0x0000aa4c, 0x00058288},
	{0x0000aa50, 0x00058224},
	{0x0000aa54, 0x00058290},
	{0x0000aa58, 0x00058300},
	{0x0000aa5c, 0x00058304},
	{0x0000aa60, 0x00058308},
	{0x0000aa64, 0x0005830c},
	{0x0000aa68, 0x00058380},
	{0x0000aa6c, 0x00058384},
	{0x0000aa70, 0x00068700},
	{0x0000aa74, 0x00068704},
	{0x0000aa78, 0x00068708},
	{0x0000aa7c, 0x0006870c},
	{0x0000aa80, 0x00068780},
	{0x0000aa84, 0x00068784},
	{0x0000aa88, 0x00078b00},
	{0x0000aa8c, 0x00078b04},
	{0x0000aa90, 0x00078b08},
	{0x0000aa94, 0x00078b0c},
	{0x0000aa98, 0x00078b80},
	{0x0000aa9c, 0x00078b84},
	{0x0000aaa0, 0x00078b88},
	{0x0000aaa4, 0x00078b8c},
	{0x0000aaa8, 0x00078b90},
	{0x0000aaac, 0x000caf80},
	{0x0000aab0, 0x000caf84},
	{0x0000aab4, 0x000caf88},
	{0x0000aab8, 0x000caf8c},
	{0x0000aabc, 0x000caf90},
	{0x0000aac0, 0x000db30c},
	{0x0000aac4, 0x000db310},
	{0x0000aac8, 0x000db384},
	{0x0000aacc, 0x000db388},
	{0x0000aad0, 0x000db324},
	{0x0000aad4, 0x000eb704},
	{0x0000aad8, 0x000eb6a4},
	{0x0000aadc, 0x000eb6a8},
	{0x0000aae0, 0x000eb710},
	{0x0000aae4, 0x000eb714},
	{0x0000aae8, 0x000eb720},
	{0x0000aaec, 0x000eb724},
	{0x0000aaf0, 0x000eb728},
	{0x0000aaf4, 0x000eb72c},
	{0x0000aaf8, 0x000eb7a0},
	{0x0000aafc, 0x000eb7a4},
	{0x0000ab00, 0x000eb7a8},
	{0x0000ab04, 0x000eb7b0},
	{0x0000ab08, 0x000eb7b4},
	{0x0000ab0c, 0x000eb7b8},
	{0x0000ab10, 0x000eb7a5},
	{0x0000ab14, 0x000eb7a9},
	{0x0000ab18, 0x000eb7ad},
	{0x0000ab1c, 0x000eb7b1},
	{0x0000ab20, 0x000eb7b5},
	{0x0000ab24, 0x000eb7b9},
	{0x0000ab28, 0x000eb7c5},
	{0x0000ab2c, 0x000eb7c9},
	{0x0000ab30, 0x000eb7d1},
	{0x0000ab34, 0x000eb7d5},
	{0x0000ab38, 0x000eb7d9},
	{0x0000ab3c, 0x000eb7c6},
	{0x0000ab40, 0x000eb7ca},
	{0x0000ab44, 0x000eb7ce},
	{0x0000ab48, 0x000eb7d2},
	{0x0000ab4c, 0x000eb7d6},
	{0x0000ab50, 0x000eb7c3},
	{0x0000ab54, 0x000eb7c7},
	{0x0000ab58, 0x000eb7cb},
	{0x0000ab5c, 0x000eb7cf},
	{0x0000ab60, 0x000eb7d7},
	{0x0000ab64, 0x000eb7db},
	{0x0000ab68, 0x000eb7db},
	{0x0000ab6c, 0x000eb7db},
	{0x0000ab70, 0x000eb7db},
	{0x0000ab74, 0x000eb7db},
	{0x0000ab78, 0x000eb7db},
	{0x0000ab7c, 0x000eb7db},
	{0x0000ab80, 0x000eb7db},
	{0x0000ab84, 0x000eb7db},
	{0x0000ab88, 0x000eb7db},
	{0x0000ab8c, 0x000eb7db},
	{0x0000ab90, 0x000eb7db},
	{0x0000ab94, 0x000eb7db},
	{0x0000ab98, 0x000eb7db},
	{0x0000ab9c, 0x000eb7db},
	{0x0000aba0, 0x000eb7db},
	{0x0000aba4, 0x000eb7db},
	{0x0000aba8, 0x000eb7db},
	{0x0000abac, 0x000eb7db},
	{0x0000abb0, 0x000eb7db},
	{0x0000abb4, 0x000eb7db},
	{0x0000abb8, 0x000eb7db},
	{0x0000abbc, 0x000eb7db},
	{0x0000abc0, 0x000eb7db},
	{0x0000abc4, 0x000eb7db},
	{0x0000abc8, 0x000eb7db},
	{0x0000abcc, 0x000eb7db},
	{0x0000abd0, 0x000eb7db},
	{0x0000abd4, 0x000eb7db},
	{0x0000abd8, 0x000eb7db},
	{0x0000abdc, 0x000eb7db},
	{0x0000abe0, 0x000eb7db},
	{0x0000abe4, 0x000eb7db},
	{0x0000abe8, 0x000eb7db},
	{0x0000abec, 0x000eb7db},
	{0x0000abf0, 0x000eb7db},
	{0x0000abf4, 0x000eb7db},
	{0x0000abf8, 0x000eb7db},
	{0x0000abfc, 0x000eb7db},
	{0x0000a204, 0x00000004},
	{0x0000a20c, 0x0001f000},
	{0x0000b20c, 0x0001f000},
	{0x0000a21c, 0x1883800a},
	{0x0000a230, 0x00000108},
	{0x0000a250, 0x0004a000},
	{0x0000a358, 0x7999aa0e}
};

/**
 * AR9271 TX init values for 2GHz mode operation.
 * 
 * Taken from Linux sources.
 */
static const uint32_t ar9271_2g_tx_array[][2] = {
	{0x0000a300, 0x00010000},
	{0x0000a304, 0x00016200},
	{0x0000a308, 0x00018201},
	{0x0000a30c, 0x0001b240},
	{0x0000a310, 0x0001d241},
	{0x0000a314, 0x0001f600},
	{0x0000a318, 0x00022800},
	{0x0000a31c, 0x00026802},
	{0x0000a320, 0x0002b805},
	{0x0000a324, 0x0002ea41},
	{0x0000a328, 0x00038b00},
	{0x0000a32c, 0x0003ab40},
	{0x0000a330, 0x0003cd80},
	{0x0000a334, 0x000368de},
	{0x0000a338, 0x0003891e},
	{0x0000a33c, 0x0003a95e},
	{0x0000a340, 0x0003e9df},
	{0x0000a344, 0x0003e9df},
	{0x0000a348, 0x0003e9df},
	{0x0000a34c, 0x0003e9df},
	{0x0000a350, 0x0003e9df},
	{0x0000a354, 0x0003e9df},
	{0x00007838, 0x0000002b},
	{0x00007824, 0x00d8a7ff},
	{0x0000786c, 0x08609eba},
	{0x00007820, 0x00000c00},
	{0x0000a274, 0x0a214652},
	{0x0000a278, 0x0e739ce7},
	{0x0000a27c, 0x05018063},
	{0x0000a394, 0x06318c63},
	{0x0000a398, 0x00000063},
	{0x0000a3dc, 0x06318c63},
	{0x0000a3e0, 0x00000063}
};

/**
 * AR9271 hardware init values.
 * 
 * Taken from Linux sources, some values omitted.
 */
static const uint32_t ar9271_init_array[][2] = {
	{0x0000000c, 0x00000000},
	{0x00000030, 0x00020045},
	{0x00000034, 0x00000005},
	{0x00000040, 0x00000000},
	{0x00000044, 0x00000008},
	{0x00000048, 0x00000008},
	{0x0000004c, 0x00000010},
	{0x00000050, 0x00000000},
	{0x00000054, 0x0000001f},
	{0x00000800, 0x00000000},
	{0x00000804, 0x00000000},
	{0x00000808, 0x00000000},
	{0x0000080c, 0x00000000},
	{0x00000810, 0x00000000},
	{0x00000814, 0x00000000},
	{0x00000818, 0x00000000},
	{0x0000081c, 0x00000000},
	{0x00000820, 0x00000000},
	{0x00000824, 0x00000000},
	{0x00001040, 0x002ffc0f},
	{0x00001044, 0x002ffc0f},
	{0x00001048, 0x002ffc0f},
	{0x0000104c, 0x002ffc0f},
	{0x00001050, 0x002ffc0f},
	{0x00001054, 0x002ffc0f},
	{0x00001058, 0x002ffc0f},
	{0x0000105c, 0x002ffc0f},
	{0x00001060, 0x002ffc0f},
	{0x00001064, 0x002ffc0f},
	{0x00001230, 0x00000000},
	{0x00001270, 0x00000000},
	{0x00001038, 0x00000000},
	{0x00001078, 0x00000000},
	{0x000010b8, 0x00000000},
	{0x000010f8, 0x00000000},
	{0x00001138, 0x00000000},
	{0x00001178, 0x00000000},
	{0x000011b8, 0x00000000},
	{0x000011f8, 0x00000000},
	{0x00001238, 0x00000000},
	{0x00001278, 0x00000000},
	{0x000012b8, 0x00000000},
	{0x000012f8, 0x00000000},
	{0x00001338, 0x00000000},
	{0x00001378, 0x00000000},
	{0x000013b8, 0x00000000},
	{0x000013f8, 0x00000000},
	{0x00001438, 0x00000000},
	{0x00001478, 0x00000000},
	{0x000014b8, 0x00000000},
	{0x000014f8, 0x00000000},
	{0x00001538, 0x00000000},
	{0x00001578, 0x00000000},
	{0x000015b8, 0x00000000},
	{0x000015f8, 0x00000000},
	{0x00001638, 0x00000000},
	{0x00001678, 0x00000000},
	{0x000016b8, 0x00000000},
	{0x000016f8, 0x00000000},
	{0x00001738, 0x00000000},
	{0x00001778, 0x00000000},
	{0x000017b8, 0x00000000},
	{0x000017f8, 0x00000000},
	{0x0000103c, 0x00000000},
	{0x0000107c, 0x00000000},
	{0x000010bc, 0x00000000},
	{0x000010fc, 0x00000000},
	{0x0000113c, 0x00000000},
	{0x0000117c, 0x00000000},
	{0x000011bc, 0x00000000},
	{0x000011fc, 0x00000000},
	{0x0000123c, 0x00000000},
	{0x0000127c, 0x00000000},
	{0x000012bc, 0x00000000},
	{0x000012fc, 0x00000000},
	{0x0000133c, 0x00000000},
	{0x0000137c, 0x00000000},
	{0x000013bc, 0x00000000},
	{0x000013fc, 0x00000000},
	{0x0000143c, 0x00000000},
	{0x0000147c, 0x00000000},
	{0x00004030, 0x00000002},
	{0x0000403c, 0x00000002},
	{0x00004024, 0x0000001f},
	{0x00004060, 0x00000000},
	{0x00004064, 0x00000000},
	{0x00008018, 0x00000700},
	{0x00008020, 0x00000000},
	{0x00008038, 0x00000000},
	{0x00008048, 0x00000000},
	{0x00008054, 0x00000000},
	{0x00008058, 0x00000000},
	{0x0000805c, 0x000fc78f},
	{0x00008060, 0xc7ff000f},
	{0x00008064, 0x00000000},
	{0x00008070, 0x00000000},
	{0x000080b0, 0x00000000},
	{0x000080b4, 0x00000000},
	{0x000080b8, 0x00000000},
	{0x000080bc, 0x00000000},
	{0x000080c0, 0x2a80001a},
	{0x000080c4, 0x05dc01e0},
	{0x000080c8, 0x1f402710},
	{0x000080cc, 0x01f40000},
	{0x000080d0, 0x00001e00},
	{0x000080d4, 0x00000000},
	{0x000080d8, 0x00400000},
	{0x000080e0, 0xffffffff},
	{0x000080e4, 0x0000ffff},
	{0x000080e8, 0x003f3f3f},
	{0x000080ec, 0x00000000},
	{0x000080f0, 0x00000000},
	{0x000080f4, 0x00000000},
	{0x000080f8, 0x00000000},
	{0x000080fc, 0x00020000},
	{0x00008100, 0x00020000},
	{0x00008104, 0x00000001},
	{0x00008108, 0x00000052},
	{0x0000810c, 0x00000000},
	{0x00008110, 0x00000168},
	{0x00008118, 0x000100aa},
	{0x0000811c, 0x00003210},
	{0x00008120, 0x08f04810},
	{0x00008124, 0x00000000},
	{0x00008128, 0x00000000},
	{0x0000812c, 0x00000000},
	{0x00008130, 0x00000000},
	{0x00008134, 0x00000000},
	{0x00008138, 0x00000000},
	{0x0000813c, 0x00000000},
	{0x00008144, 0xffffffff},
	{0x00008168, 0x00000000},
	{0x0000816c, 0x00000000},
	{0x00008170, 0x32143320},
	{0x00008174, 0xfaa4fa50},
	{0x00008178, 0x00000100},
	{0x0000817c, 0x00000000},
	{0x000081c0, 0x00000000},
	{0x000081d0, 0x0000320a},
	{0x000081ec, 0x00000000},
	{0x000081f0, 0x00000000},
	{0x000081f4, 0x00000000},
	{0x000081f8, 0x00000000},
	{0x000081fc, 0x00000000},
	{0x00008200, 0x00000000},
	{0x00008204, 0x00000000},
	{0x00008208, 0x00000000},
	{0x0000820c, 0x00000000},
	{0x00008210, 0x00000000},
	{0x00008214, 0x00000000},
	{0x00008218, 0x00000000},
	{0x0000821c, 0x00000000},
	{0x00008220, 0x00000000},
	{0x00008224, 0x00000000},
	{0x00008228, 0x00000000},
	{0x0000822c, 0x00000000},
	{0x00008230, 0x00000000},
	{0x00008234, 0x00000000},
	{0x00008238, 0x00000000},
	{0x0000823c, 0x00000000},
	{0x00008240, 0x00100000},
	{0x00008244, 0x0010f400},
	{0x00008248, 0x00000100},
	{0x0000824c, 0x0001e800},
	{0x00008250, 0x00000000},
	{0x00008254, 0x00000000},
	{0x00008258, 0x00000000},
	{0x0000825c, 0x400000ff},
	{0x00008260, 0x00080922},
	{0x00008264, 0x88a00010},
	{0x00008270, 0x00000000},
	{0x00008274, 0x40000000},
	{0x00008278, 0x003e4180},
	{0x0000827c, 0x00000000},
	{0x00008284, 0x0000002c},
	{0x00008288, 0x0000002c},
	{0x0000828c, 0x00000000},
	{0x00008294, 0x00000000},
	{0x00008298, 0x00000000},
	{0x0000829c, 0x00000000},
	{0x00008300, 0x00000040},
	{0x00008314, 0x00000000},
	{0x00008328, 0x00000000},
	{0x0000832c, 0x00000001},
	{0x00008330, 0x00000302},
	{0x00008334, 0x00000e00},
	{0x00008338, 0x00ff0000},
	{0x0000833c, 0x00000000},
	{0x00008340, 0x00010380},
	{0x00008344, 0x00481083},/*< note: disabled ADHOC_MCAST_KEYID feature */
	{0x00007010, 0x00000030},
	{0x00007034, 0x00000002},
	{0x00007038, 0x000004c2},
	{0x00007800, 0x00140000},
	{0x00007804, 0x0e4548d8},
	{0x00007808, 0x54214514},
	{0x0000780c, 0x02025820},
	{0x00007810, 0x71c0d388},
	{0x00007814, 0x924934a8},
	{0x0000781c, 0x00000000},
	{0x00007828, 0x66964300},
	{0x0000782c, 0x8db6d961},
	{0x00007830, 0x8db6d96c},
	{0x00007834, 0x6140008b},
	{0x0000783c, 0x72ee0a72},
	{0x00007840, 0xbbfffffc},
	{0x00007844, 0x000c0db6},
	{0x00007848, 0x6db6246f},
	{0x0000784c, 0x6d9b66db},
	{0x00007850, 0x6d8c6dba},
	{0x00007854, 0x00040000},
	{0x00007858, 0xdb003012},
	{0x0000785c, 0x04924914},
	{0x00007860, 0x21084210},
	{0x00007864, 0xf7d7ffde},
	{0x00007868, 0xc2034080},
	{0x00007870, 0x10142c00},
	{0x00009808, 0x00000000},
	{0x0000980c, 0xafe68e30},
	{0x00009810, 0xfd14e000},
	{0x00009814, 0x9c0a9f6b},
	{0x0000981c, 0x00000000},
	{0x0000982c, 0x0000a000},
	{0x00009830, 0x00000000},
	{0x0000983c, 0x00200400},
	{0x0000984c, 0x0040233c},
	{0x00009854, 0x00000044},
	{0x00009900, 0x00000000},
	{0x00009904, 0x00000000},
	{0x00009908, 0x00000000},
	{0x0000990c, 0x00000000},
	{0x0000991c, 0x10000fff},
	{0x00009920, 0x04900000},
	{0x00009928, 0x00000001},
	{0x0000992c, 0x00000004},
	{0x00009934, 0x1e1f2022},
	{0x00009938, 0x0a0b0c0d},
	{0x0000993c, 0x00000000},
	{0x00009940, 0x14750604},
	{0x00009948, 0x9280c00a},
	{0x0000994c, 0x00020028},
	{0x00009954, 0x5f3ca3de},
	{0x00009958, 0x0108ecff},
	{0x00009968, 0x000003ce},
	{0x00009970, 0x192bb514},
	{0x00009974, 0x00000000},
	{0x00009978, 0x00000001},
	{0x0000997c, 0x00000000},
	{0x00009980, 0x00000000},
	{0x00009984, 0x00000000},
	{0x00009988, 0x00000000},
	{0x0000998c, 0x00000000},
	{0x00009990, 0x00000000},
	{0x00009994, 0x00000000},
	{0x00009998, 0x00000000},
	{0x0000999c, 0x00000000},
	{0x000099a0, 0x00000000},
	{0x000099a4, 0x00000001},
	{0x000099a8, 0x201fff00},
	{0x000099ac, 0x2def0400},
	{0x000099b0, 0x03051000},
	{0x000099b4, 0x00000820},
	{0x000099dc, 0x00000000},
	{0x000099e0, 0x00000000},
	{0x000099e4, 0xaaaaaaaa},
	{0x000099e8, 0x3c466478},
	{0x000099ec, 0x0cc80caa},
	{0x000099f0, 0x00000000},
	{0x0000a208, 0x803e68c8},
	{0x0000a210, 0x4080a333},
	{0x0000a214, 0x00206c10},
	{0x0000a218, 0x009c4060},
	{0x0000a220, 0x01834061},
	{0x0000a224, 0x00000400},
	{0x0000a228, 0x000003b5},
	{0x0000a22c, 0x00000000},
	{0x0000a234, 0x20202020},
	{0x0000a238, 0x20202020},
	{0x0000a244, 0x00000000},
	{0x0000a248, 0xfffffffc},
	{0x0000a24c, 0x00000000},
	{0x0000a254, 0x00000000},
	{0x0000a258, 0x0ccb5380},
	{0x0000a25c, 0x15151501},
	{0x0000a260, 0xdfa90f01},
	{0x0000a268, 0x00000000},
	{0x0000a26c, 0x0ebae9e6},
	{0x0000a388, 0x0c000000},
	{0x0000a38c, 0x20202020},
	{0x0000a390, 0x20202020},
	{0x0000a39c, 0x00000001},
	{0x0000a3a0, 0x00000000},
	{0x0000a3a4, 0x00000000},
	{0x0000a3a8, 0x00000000},
	{0x0000a3ac, 0x00000000},
	{0x0000a3b0, 0x00000000},
	{0x0000a3b4, 0x00000000},
	{0x0000a3b8, 0x00000000},
	{0x0000a3bc, 0x00000000},
	{0x0000a3c0, 0x00000000},
	{0x0000a3c4, 0x00000000},
	{0x0000a3cc, 0x20202020},
	{0x0000a3d0, 0x20202020},
	{0x0000a3d4, 0x20202020},
	{0x0000a3e4, 0x00000000},
	{0x0000a3e8, 0x18c43433},
	{0x0000a3ec, 0x00f70081},
	{0x0000a3f0, 0x01036a2f},
	{0x0000a3f4, 0x00000000},
	{0x0000d270, 0x0d820820},
	{0x0000d35c, 0x07ffffef},
	{0x0000d360, 0x0fffffe7},
	{0x0000d364, 0x17ffffe5},
	{0x0000d368, 0x1fffffe4},
	{0x0000d36c, 0x37ffffe3},
	{0x0000d370, 0x3fffffe3},
	{0x0000d374, 0x57ffffe3},
	{0x0000d378, 0x5fffffe2},
	{0x0000d37c, 0x7fffffe2},
	{0x0000d380, 0x7f3c7bba},
	{0x0000d384, 0xf3307ff0}
};

#endif