/*
 *  linux/arch/arm/mach-mmp/board-emeidkb.c
 *
 *  Support for the Marvell PXA988 Emei DKB Development Platform.
 *
 *  Copyright (C) 2012 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c/ft5306_touch.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/backlight.h>
#include <linux/regulator/machine.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci.h>
#include <linux/mmc/card.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/sd8x_rfkill.h>
#include <linux/regmap.h>
#include <linux/mfd/88pm822.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/pm_qos.h>
#include <linux/clk.h>
#include <linux/lps331ap.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/i2c-gpio.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/uio_coda7542.h>
#ifdef CONFIG_SEC_DEBUG
#include "mach/sec_debug.h"
#endif
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#ifdef CONFIG_SENSORS_GP2A002S
#include <linux/gp2a.h>
#endif

#ifdef CONFIG_SENSORS_GP2A_PROXIMITY
#include <linux/sensor/gp2a.h>
#endif

#ifdef CONFIG_INPUT_MPU6500
#include <linux/sensor/mpu6k_input.h>
#endif

#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>
#include <asm/system_info.h>
#include <mach/addr-map.h>
#include <mach/clock-pxa988.h>
#include <mach/mmp_device.h>
#include <mach/mfp-pxa986-golden.h>
#include <mach/irqs.h>
#include <mach/isl29043.h>
#include <mach/pxa988.h>
#include <mach/soc_coda7542.h>
#include <mach/regs-rtc.h>
#include <mach/regs-ciu.h>
#include <plat/pm.h>
#include <media/soc_camera.h>
#include <mach/isp_dev.h>
#include <mach/gpio-edge.h>
#include <mach/dvfs.h>
#include <mach/features.h>
#include <mach/regs-mpmu.h>
#if defined(CONFIG_SOC_CAMERA_SR352)
#include <mach/samsung_camera_golden.h>
#else
#include <mach/samsung_camera.h>
#endif

#include "board-golden.h"

#ifdef CONFIG_PM_DEVFREQ
#include <plat/devfreq.h>
#endif
#include "common.h"
#include "onboard.h"
#if defined(CONFIG_SPA)
#include <mach/spa.h>
#endif
#if defined(CONFIG_BQ24157_CHARGER)
#include <mach/bq24157_charger.h>
#endif
#if defined(CONFIG_I2C_RT9455)
#include <linux/power/rt9455.h>
#endif
#if defined(CONFIG_STC3115_FUELGAUGE)
#include <mach/stc3115_fuelgauge.h>
#endif
#if defined(CONFIG_SM5502_MUIC)
#include <mach/sm5502-muic.h>
#endif
#if defined(CONFIG_NFC_PN547)
#include <linux/nfc/pn547.h>
#endif

#if defined(CONFIG_RT8969)||defined(CONFIG_RT8973)
#include <linux/platform_data/rtmusc.h>
#endif
#if defined(CONFIG_RT9450AC) || defined(CONFIG_RT9450B)
#include <linux/platform_data/rtsmc.h>
#endif
#if ((defined CONFIG_SENSORS_BMA2X2) ||(defined CONFIG_SENSORS_BMM050))
#include "../../../drivers/i2c/chips/bst_sensor_common.h"
#endif

#ifdef CONFIG_MFD_D2199
#include <linux/d2199/core.h>
#include <linux/d2199/pmic.h>
#include <linux/d2199/d2199_reg.h>
#include <linux/d2199/d2199_battery.h>
#endif

#ifdef CONFIG_LEDS_RT8547
#include <linux/leds-rt8547.h>
#endif /* #ifdef CONFIG_LEDS_RT8547 */

#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#endif

#ifdef CONFIG_SEC_GPIO_DVS
#include <linux/secgpio_dvs.h>
#endif

#ifdef CONFIG_SAMSUNG_JACK
#include <linux/mfd/sec_jack.h>
#endif

#define VER_1V0 0x10
#define VER_1V1 0x11
#define GPS_LDO_POWER
static int board_id;
static int recovery_mode;
extern u32 panel_id;

static int __init board_id_setup(char *str)
{
	int n;
	if (!get_option(&str, &n))
		return 0;
	system_rev = board_id = n;
	return 1;
}
__setup("board_id=", board_id_setup);

int get_board_id()
{
	return board_id;
}

static int __init recovery_mode_setup(char *str)
{
	int n;
	if (!get_option(&str, &n))
		return 0;
	recovery_mode = n;
	return 1;
}
__setup("recovery_mode=", recovery_mode_setup);

int get_recoverymode()
{
	return recovery_mode;
}

static unsigned long emeidkb_pin_config[] __initdata = {
	MFP_VCXO_OUT | MFP_PULL_LOW,
	GPIO000_KP_MKIN0,			/* KP_MKIN[0] */
	GPIO001_KP_MKOUT0 | MFP_LPM_DRIVE_HIGH
			  | MFP_PULL_FLOAT,	/* KP_MKOUT[0] */
	GPIO002_KP_MKIN1,			/* KP_MKIN[1] */
	GPIO003_GPIO_3 | MFP_LPM_INPUT | MFP_PULL_LOW,
	GPIO004_GPIO_4 | MFP_PULL_HIGH,

#define GPIO005_AP_AGPS_RESET	(GPIO005_GPIO_5 | MFP_PULL_HIGH)
	GPIO005_AP_AGPS_RESET,

	GPIO006_GPIO_6 | MFP_PULL_LOW,
	GPIO007_GPIO_7 | MFP_LPM_INPUT | MFP_PULL_LOW,
	GPIO008_GPIO_8 | MFP_LPM_INPUT | MFP_PULL_NONE,
	GPIO009_GPIO_9 | MFP_LPM_INPUT | MFP_PULL_HIGH,

#define GPIO010_ACC_INT	(GPIO010_GPIO_10 | MFP_PULL_LOW)
	GPIO010_ACC_INT,

#if defined(CONFIG_FUELGAUGE_RT5033)
#define GPIO011_GPI0_TSP_VALID	(GPIO011_GPIO_11 | MFP_LPM_INPUT | MFP_PULL_HIGH)
	GPIO011_GPI0_TSP_VALID,	/* TSP_VALID will be NC */

#define GPIO012_COM_DET	(GPIO012_GPIO_12 | MFP_LPM_INPUT)
	GPIO012_COM_DET,
	GPIO013_KP_DKIN4,
#else
	GPIO011_GPIO_11 | MFP_PULL_HIGH |  MFP_LPM_FLOAT, /* NC */
	GPIO012_GPIO_12 | MFP_PULL_HIGH |  MFP_LPM_FLOAT, /* NC */
#endif

#define GPIO014_5M_CAM_STBY	(GPIO014_GPIO_14 | MFP_PULL_LOW | MFP_LPM_DRIVE_LOW)
#define GPIO015_5M_CAM_RESET	(GPIO015_GPIO_15 | MFP_PULL_LOW | MFP_LPM_DRIVE_LOW)
	GPIO014_5M_CAM_STBY,
	GPIO015_5M_CAM_RESET,

#define GPIO016_CHG_SCL	(GPIO016_GPIO_16 | MFP_PULL_HIGH )
#define GPIO017_CHG_SDA	(GPIO017_GPIO_17 | MFP_PULL_HIGH )
	GPIO016_CHG_SCL,
	GPIO017_CHG_SDA,

#define GPIO018_LCD_RESET	(GPIO018_GPIO_18 | MFP_LPM_DRIVE_HIGH)
#define GPIO019_LCD_ESD_DETECT	(GPIO019_GPIO_19 | MFP_PULL_LOW)
#define GPIO020_CAM_FLASH_EN	(GPIO020_GPIO_20 | MFP_LPM_DRIVE_LOW)
	GPIO018_LCD_RESET,
	GPIO019_LCD_ESD_DETECT,
	GPIO020_CAM_FLASH_EN | MFP_LPM_INPUT| MFP_PULL_LOW,

	/*
	 * configure to be GPIO input to avoid leakage in production
	 * mode. would configure it to I2S MFP in sound start up
	 * function.
	 */
	GPIO021_GPIO_21 | MFP_LPM_INPUT,
	GPIO022_GPIO_22 | MFP_LPM_INPUT,
	GPIO023_GPIO_23  | MFP_LPM_INPUT,
	GPIO024_GPIO_24  | MFP_LPM_INPUT,

	GPIO025_GSSP_SCLK,	/* PCM_CLK */
	GPIO026_GSSP_SFRM,	/* PCM_SYNC */
	GPIO027_GSSP_TXD,	/* PCM_TXD */
	GPIO028_GSSP_RXD,	/* PCM_RXD */

	GPIO029_GPIO_29 | MFP_LPM_INPUT | MFP_PULL_NONE,

#define GPIO030_GPI0_TOUCH_KEY_IC_EN	(GPIO030_GPIO_30 | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW)
	GPIO030_GPI0_TOUCH_KEY_IC_EN,

#define GPIO031_GPIO_FLM	(GPIO031_GPIO_31 | MFP_PULL_HIGH)
#define GPIO032_HW_REV_MOD_2	(GPIO032_GPIO_32 | MFP_PULL_FLOAT | MFP_LPM_INPUT)
	GPIO031_GPIO_FLM,
	GPIO032_HW_REV_MOD_2,

#if defined(CONFIG_NFC_PN547)
	#define GPIO033_NFC_SCL		(GPIO033_GPIO_33 | MFP_LPM_INPUT)
	#define GPIO034_NFC_SDA		(GPIO034_GPIO_34 | MFP_LPM_INPUT)
	GPIO033_NFC_SCL,
	GPIO034_NFC_SDA,
#endif

#define GPIO035_GPIO_SENSOR_I2C_SCL	(GPIO035_GPIO_35 | MFP_LPM_INPUT)
#define GPIO036_GPIO_SENSOR_I2C_SDA	(GPIO036_GPIO_36 | MFP_LPM_INPUT)
	GPIO035_GPIO_SENSOR_I2C_SCL,
	GPIO036_GPIO_SENSOR_I2C_SDA,

	/* MMC2 WIB */
	GPIO037_MMC2_DATA3 | MFP_LPM_FLOAT,	/* WLAN_DAT3 */
	GPIO038_MMC2_DATA2 | MFP_LPM_FLOAT,	/* WLAN_DAT2 */
	GPIO039_MMC2_DATA1 | MFP_LPM_FLOAT,	/* WLAN_DAT1 */
	GPIO040_MMC2_DATA0 | MFP_LPM_FLOAT,	/* WLAN_DAT0 */
	GPIO041_MMC2_CMD,	/* WLAN_CMD */
	GPIO042_MMC2_CLK | MFP_LPM_DRIVE_HIGH,	/* WLAN_CLK */

#define GPIO043_GPIO_DVC1	(GPIO043_GPIO_43 | MFP_PULL_FLOAT)
#define GPIO044_GPIO_DVC2	(GPIO044_GPIO_44 | MFP_PULL_FLOAT)
	GPIO043_GPIO_DVC1,
	GPIO044_GPIO_DVC2,

	GPIO045_UART2_RXD,	/* GPS_UART_RXD */
	GPIO046_UART2_TXD,	/* GPS_UART_TXD */

	GPIO047_UART1_RXD,	/* AP_RXD */
	GPIO048_UART1_TXD,	/* AP_TXD */

#define GPIO049_GPIO_MUS_SCL	GPIO049_GPIO_49 | MFP_LPM_FLOAT
#define GPIO050_GPIO_MUS_SDA	GPIO050_GPIO_50 | MFP_LPM_FLOAT
#define GPIO051_WLAN_PD		GPIO051_GPIO_51
#define GPIO052_AP_AGPS_ONOFF	(GPIO052_GPIO_52 | MFP_PULL_LOW)

	GPIO049_GPIO_MUS_SCL,
	GPIO050_GPIO_MUS_SDA,
	GPIO051_WLAN_PD,
	GPIO052_AP_AGPS_ONOFF,

	GPIO053_CAM_SCL | MFP_LPM_INPUT | MFP_LPM_DRIVE_HIGH,
	GPIO054_CAM_SDA | MFP_LPM_INPUT | MFP_LPM_DRIVE_HIGH,

//	GPIO054_GPIO_54 | MFP_LPM_INPUT | MFP_PULL_HIGH,

	GPIO067_CCIC_IN7 | MFP_LPM_DRIVE_LOW,
	GPIO068_CCIC_IN6 | MFP_LPM_DRIVE_LOW,
	GPIO069_CCIC_IN5 | MFP_LPM_DRIVE_LOW,
	GPIO070_CCIC_IN4 | MFP_LPM_DRIVE_LOW,
	GPIO071_CCIC_IN3 | MFP_LPM_DRIVE_LOW,
	GPIO072_CCIC_IN2 | MFP_LPM_DRIVE_LOW,
	GPIO073_CCIC_IN1 | MFP_LPM_DRIVE_LOW,
	GPIO074_CCIC_IN0 | MFP_LPM_DRIVE_LOW,

	GPIO075_CAM_HSYNC | MFP_LPM_DRIVE_LOW,
	GPIO076_CAM_VSYNC | MFP_LPM_DRIVE_LOW,
	GPIO077_CAM_MCLK  | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW, /* CAM_MCLK */
	GPIO078_CAM_PCLK,
	GPIO079_CAM_A_EN | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW,
	GPIO080_CAM_IO_EN | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW,

#define GPIO081_GPIO_VT_CAM_RST	(GPIO081_GPIO_81 | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW)
#define GPIO082_GPIO_VT_CAM_EN	(GPIO082_GPIO_82 | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW)
#define GPIO083_GPIO_HOST_WU_WLAN	(GPIO083_GPIO_83 | MFP_LPM_DRIVE_LOW | MFP_PULL_LOW)
#define GPIO084_GPIO_WLAN_WU_HOST	(GPIO084_GPIO_84 | MFP_LPM_INPUT | MFP_PULL_LOW)
#define GPIO085_GPIO_WL_REG_ON	GPIO085_GPIO_85
#define GPIO086_GPIO_BT_REG_ON	GPIO086_GPIO_86
#define GPIO085_HW_REV_MOD_1	(GPIO085_GPIO_85 | MFP_LPM_INPUT | MFP_PULL_FLOAT)
#define GPIO086_HW_REV_MOD_0	(GPIO086_GPIO_86 | MFP_LPM_INPUT | MFP_PULL_FLOAT)

	GPIO081_GPIO_VT_CAM_RST,
	GPIO082_GPIO_VT_CAM_EN,
	GPIO083_GPIO_HOST_WU_WLAN,
	GPIO084_GPIO_WLAN_WU_HOST,
	GPIO085_HW_REV_MOD_1,
	GPIO086_HW_REV_MOD_0,

#define GPIO087_TSP_SCL		(GPIO087_CI2C_SCL_2 | MFP_LPM_FLOAT)
#define GPIO088_TSP_SDA		(GPIO088_CI2C_SDA_2 | MFP_LPM_FLOAT)
	GPIO087_TSP_SCL,
	GPIO088_TSP_SDA,

#define GPIO089_AP_AGPS_CLK_DIS	(GPIO089_GPIO_89 | MFP_PULL_LOW)
	GPIO089_AP_AGPS_CLK_DIS,
#define GPIO090_TOUCH_KEY_INT	(GPIO090_GPIO_90 | MFP_PULL_FLOAT)
	GPIO090_TOUCH_KEY_INT,

#if defined(CONFIG_NFC_PN547)
#define GPIO091_GPIO_NFC_IRQ		(GPIO091_GPIO_91 | MFP_LPM_INPUT | MFP_PULL_LOW)
	GPIO091_GPIO_NFC_IRQ,
#else
	GPIO091_GPIO_91 | MFP_PULL_HIGH,
#endif

#define GPIO091_GPIO_NFC_IRQ	(GPIO091_GPIO_91 | MFP_LPM_INPUT | MFP_PULL_LOW)
#define GPIO092_GPIO_PROXI_INT	(GPIO092_GPIO_92 | MFP_LPM_INPUT)
#define GPIO093_GPIO_MUIC_INT	(GPIO093_GPIO_93 | MFP_PULL_NONE)
#define GPIO094_GPIO_TSP_INT	(GPIO094_GPIO_94 | MFP_LPM_INPUT)
	GPIO091_GPIO_NFC_IRQ,
	GPIO092_GPIO_PROXI_INT,
	GPIO093_GPIO_MUIC_INT,
	GPIO094_GPIO_TSP_INT,

	GPIO095_GPIO_95 | MFP_PULL_HIGH,

#define GPIO096_GPIO_MOTOR_EN	(GPIO096_GPIO_96 | MFP_PULL_LOW | MFP_LPM_DRIVE_LOW)
	GPIO096_GPIO_MOTOR_EN,

#define GPIO097_CAM_FLASH_SET	(GPIO097_GPIO_97 | MFP_PULL_LOW)
	GPIO097_CAM_FLASH_SET,
#if defined(CONFIG_BQ24157_CHARGER)
#define GPIO098_GPIO_CHG_EN	(GPIO098_GPIO_98 | MFP_PULL_HIGH)
	GPIO098_GPIO_CHG_EN,
#else
	GPIO098_GPIO_98 | MFP_LPM_INPUT | MFP_PULL_HIGH, /* NC */
#endif
#define GPIO124_GPIO_CODEC_INT  (GPIO124_GPIO_124 | MFP_LPM_INPUT | MFP_PULL_HIGH)
	GPIO124_GPIO_CODEC_INT,
#if defined(CONFIG_NFC_PN547)
	GPIO126_GPIO_126 | MFP_PULL_NONE,
	GPIO127_GPIO_127 | MFP_PULL_NONE,
#endif
	/* MMC1 Micro SD */
	MMC1_DAT7_MMC1_DAT7 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	MMC1_DAT6_MMC1_DAT6 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	MMC1_DAT5_MMC1_DAT5 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	MMC1_DAT4_MMC1_DAT4 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	MMC1_DAT3_MMC1_DAT3,
	MMC1_DAT2_MMC1_DAT2,
	MMC1_DAT1_MMC1_DAT1,
	MMC1_DAT0_MMC1_DAT0,
	MMC1_CMD_MMC1_CMD | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	MMC1_CLK_MMC1_CLK | MFP_LPM_PULL_HIGH,
	MMC1_CD_MMC1_CD | MFP_PULL_LOW | MFP_LPM_FLOAT,
	MMC1_WP_MMC1_WP | MFP_PULL_HIGH |  MFP_LPM_FLOAT, /* NC */

	/* MMC3 16GB EMMC */
	ND_IO7_MMC3_DAT7 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO6_MMC3_DAT6 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO5_MMC3_DAT5 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO4_MMC3_DAT4 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO3_MMC3_DAT3 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO2_MMC3_DAT2 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO1_MMC3_DAT1 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO0_MMC3_DAT0 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_CLE_SM_OEN_MMC3_CMD | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	SM_SCLK_MMC3_CLK | MFP_PULL_HIGH | MFP_LPM_FLOAT,
#if defined(CONFIG_NFC_PN547)
	SM_BEN0_GPIO126 | MFP_PULL_LOW,
#else
	SM_BEN0_GPIO126 | MFP_PULL_HIGH,
#endif
#define GPIO_GPS_TIMER_SYNC	ANT_SW4_GPIO_28
#define GPIO_RF_PDET_EN		SM_ADV_GPIO_0 | MFP_LPM_FLOAT
#define GPIO_LCD_RESET_N	ND_RDY1_GPIO_1
#define GPIO_LED_B_CTRL		SM_ADVMUX_GPIO_2
#define GPIO_LED_R_CTRL		SM_BEN1_GPIO_127
#define GPIO_LED_G_CTRL		SM_CSN0_GPIO_103
#define GPIO102_TSP_IO_EN	ND_CS1N3_GPIO_102
	GPIO_GPS_TIMER_SYNC,
	SM_ADV_GPIO_0 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	GPIO_LCD_RESET_N | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	SM_ADVMUX_GPIO_2 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
#if defined(CONFIG_NFC_PN547)
	SM_BEN1_GPIO_127 | MFP_PULL_LOW,
#else
	SM_BEN1_GPIO_127 | MFP_PULL_HIGH,
#endif
	SM_CSN0_GPIO_103 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	SM_CSN1_GPIO_104 | MFP_PULL_HIGH |MFP_LPM_FLOAT,
	GPIO102_TSP_IO_EN | MFP_PULL_LOW,
	/* SM_RDY pin Low for download mode, High for normal boot */
	SM_RDY | MFP_PULL_HIGH | MFP_LPM_FLOAT,

	ND_IO15_ND_DAT15 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO14_ND_DAT14 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO13_ND_DAT13 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO12_ND_DAT12 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO11_ND_DAT11 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO10_ND_DAT10 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO9_ND_DAT9 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	ND_IO8_ND_DAT8 | MFP_PULL_HIGH |  MFP_LPM_FLOAT,
	ND_nCS0_SM_nCS2 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	DF_ALE_SM_WEn_ND_ALE | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	DF_WEn_DF_WEn | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	DF_REn_DF_REn | MFP_PULL_HIGH | MFP_LPM_FLOAT,
	DF_RDY0_DF_RDY0 | MFP_PULL_HIGH | MFP_LPM_FLOAT,
};

static unsigned long dvc_pin_config[] __initdata = {

#define GPIO043_GPIO_DVC1_Ax	(GPIO043_GPIO_43_Ax | MFP_PULL_FLOAT)
#define GPIO044_GPIO_DVC2_Ax	(GPIO044_GPIO_44_Ax | MFP_PULL_FLOAT)

	GPIO043_GPIO_DVC1_Ax,
	GPIO044_GPIO_DVC2_Ax,
};

static struct sram_platdata pxa988_asram_info = {
	.pool_name = "asram",
	.granularity = SRAM_GRANULARITY,
};

static struct platform_device pxa988_device_asoc_ssp1 = {
	.name		= "pxa-ssp-dai",
	.id		= 1,
};

static struct platform_device pxa988_device_asoc_gssp = {
	.name		= "pxa-ssp-dai",
	.id		= 4,
};

static struct platform_device pxa988_device_asoc_pcm = {
	.name		= "pxa-pcm-audio",
	.id		= -1,
};

static struct platform_device emei_dkb_audio_device = {
	.name	= "emei-dkb-hifi",
	.id	= -1,
};

#if defined(CONFIG_MFD_88PM822)
static struct regulator_consumer_supply regulator_supplies[] = {
	[PM822_ID_BUCK1] = REGULATOR_SUPPLY("vcc_main", NULL),
	[PM822_ID_BUCK2] = REGULATOR_SUPPLY("v_buck2", NULL),
	[PM822_ID_BUCK3] = REGULATOR_SUPPLY("v_buck3", NULL),
	[PM822_ID_BUCK4] = REGULATOR_SUPPLY("v_rf_vdd", NULL),
	[PM822_ID_BUCK5] = REGULATOR_SUPPLY("v_cam_c", NULL),

	[PM822_ID_LDO1]  = REGULATOR_SUPPLY("v_wib_3v3", NULL),
	[PM822_ID_LDO2]  = REGULATOR_SUPPLY("v_dig_1v8", NULL),
	[PM822_ID_LDO3]  = REGULATOR_SUPPLY("v_cam_avdd", NULL),
	[PM822_ID_LDO4]  = REGULATOR_SUPPLY("vmmc", "sdhci-pxav3.0"),
	[PM822_ID_LDO5]  = REGULATOR_SUPPLY("vqmmc", "sdhci-pxav3.0"),
	[PM822_ID_LDO6]  = REGULATOR_SUPPLY("v_usim1", NULL),
	[PM822_ID_LDO7]  = REGULATOR_SUPPLY("v_ramp", NULL),
	[PM822_ID_LDO8]  = REGULATOR_SUPPLY("v_usb_3v1", NULL),
	[PM822_ID_LDO9]  = REGULATOR_SUPPLY("v_lcd_3v3", NULL),
	[PM822_ID_LDO10] = REGULATOR_SUPPLY("v_tsp_3v3", NULL),
	[PM822_ID_LDO11] = REGULATOR_SUPPLY("v_sensor", NULL),
	[PM822_ID_LDO12] = REGULATOR_SUPPLY("v_ldo12", NULL),
	[PM822_ID_LDO13] = REGULATOR_SUPPLY("v_gps_1v8", NULL),
	[PM822_ID_LDO14] = REGULATOR_SUPPLY("v_micbias", NULL),
	[PM822_ID_VOUTSW] = REGULATOR_SUPPLY("v_buck2_sw", NULL),

	/* below 4 IDs are fake ids, they are only used in new dvc */
	[PM822_ID_BUCK1_AP_ACTIVE]
		= REGULATOR_SUPPLY("vcc_main_ap_active", NULL),
	[PM822_ID_BUCK1_AP_LPM]
		= REGULATOR_SUPPLY("vcc_main_ap_lpm", NULL),
	[PM822_ID_BUCK1_APSUB_IDLE]
		= REGULATOR_SUPPLY("vcc_main_apsub_idle", NULL),
	[PM822_ID_BUCK1_APSUB_SLEEP]
		= REGULATOR_SUPPLY("vcc_main_apsub_sleep", NULL),
};
static int regulator_index[] = {
	PM822_ID_BUCK1,
	PM822_ID_BUCK2,
	PM822_ID_BUCK3,
	PM822_ID_BUCK4,
	PM822_ID_BUCK5,
	PM822_ID_LDO1,
	PM822_ID_LDO2,
	PM822_ID_LDO3,
	PM822_ID_LDO4,
	PM822_ID_LDO5,
	PM822_ID_LDO6,
	PM822_ID_LDO7,
	PM822_ID_LDO8,
	PM822_ID_LDO9,
	PM822_ID_LDO10,
	PM822_ID_LDO11,
	PM822_ID_LDO12,
	PM822_ID_LDO13,
	PM822_ID_LDO14,
	PM822_ID_VOUTSW,

	/* below 4 ids are fake id, they are only used in new dvc */
	PM822_ID_BUCK1_AP_ACTIVE,
	PM822_ID_BUCK1_AP_LPM,
	PM822_ID_BUCK1_APSUB_IDLE,
	PM822_ID_BUCK1_APSUB_SLEEP,
};

#define REG_INIT(_name, _min, _max, _always, _boot)	\
{								\
	.constraints = {					\
		.name		= __stringify(_name),		\
		.min_uV		= _min,				\
		.max_uV		= _max,				\
		.always_on	= _always,			\
		.boot_on	= _boot,			\
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE	\
				| REGULATOR_CHANGE_STATUS,	\
	},							\
	.num_consumer_supplies	= 1,				\
	.consumer_supplies	= &regulator_supplies[PM822_ID_##_name], \
	.driver_data = &regulator_index[PM822_ID_##_name],	\
}

static struct regulator_init_data pm822_regulator_data[] = {
	[PM822_ID_BUCK1] = REG_INIT(BUCK1,  600000, 1800000, 1, 1),
	[PM822_ID_BUCK2] = REG_INIT(BUCK2,  600000, 3300000, 1, 1),
	[PM822_ID_BUCK3] = REG_INIT(BUCK3,  600000, 3300000, 1, 1),
	[PM822_ID_BUCK4] = REG_INIT(BUCK4,  600000, 3300000, 1, 1),
	[PM822_ID_BUCK5] = REG_INIT(BUCK5,  600000, 3950000, 0, 0),

	[PM822_ID_LDO1]  = REG_INIT(LDO1,  1700000, 3300000, 1, 1),
	[PM822_ID_LDO2]  = REG_INIT(LDO2,  1700000, 3300000, 1, 1),
	[PM822_ID_LDO3]  = REG_INIT(LDO3,  1200000, 3300000, 1, 1),
	[PM822_ID_LDO4]  = REG_INIT(LDO4,  1200000, 3300000, 0, 0),
	[PM822_ID_LDO5]  = REG_INIT(LDO5,  1200000, 3300000, 0, 0),
	[PM822_ID_LDO6]  = REG_INIT(LDO6,  1200000, 3300000, 0, 0),
	[PM822_ID_LDO7]  = REG_INIT(LDO7,  1200000, 3300000, 1, 1),
	[PM822_ID_LDO8]  = REG_INIT(LDO8,  1200000, 3300000, 1, 1),
	[PM822_ID_LDO9]  = REG_INIT(LDO9,  1200000, 3300000, 0, 0),
	[PM822_ID_LDO10] = REG_INIT(LDO10, 1200000, 3300000, 1, 1),
	[PM822_ID_LDO11] = REG_INIT(LDO11, 1200000, 3300000, 1, 1),
	[PM822_ID_LDO12] = REG_INIT(LDO12,  600000, 1500000, 0, 0),
	[PM822_ID_LDO13] = REG_INIT(LDO13, 1700000, 3300000, 1, 1),
	[PM822_ID_LDO14] = REG_INIT(LDO14, 1700000, 2800000, 0, 0),
	[PM822_ID_VOUTSW] = REG_INIT(VOUTSW, 0, 0, 0, 0),

	/* below 4 items are fake, they are only used in new dvc */
	[PM822_ID_BUCK1_AP_ACTIVE]
		= REG_INIT(BUCK1_AP_ACTIVE,  1000, 10000, 1, 1),
	[PM822_ID_BUCK1_AP_LPM]
		= REG_INIT(BUCK1_AP_LPM,  1000, 10000, 1, 1),
	[PM822_ID_BUCK1_APSUB_IDLE]
		= REG_INIT(BUCK1_APSUB_IDLE,  1000, 10000, 1, 1),
	[PM822_ID_BUCK1_APSUB_SLEEP]
		= REG_INIT(BUCK1_APSUB_SLEEP,  1000, 10000, 1, 1),
};

static struct pm822_dvc_pdata pm822_dvc = {
	.reg_dvc	= 1,
};

static int pm822_plat_config(struct pm822_chip *chip,
				struct pm822_platform_data *pdata)
{
		int data;

	if (!chip || !pdata || !chip->regmap || !chip->subchip
	    || !chip->subchip->regmap_power) {
		pr_err("%s:chip or pdata is not availiable!\n", __func__);
		return -EINVAL;
	}

	/* select 15pF internal capacitance on XTAL1 and XTAL2 */
	regmap_read(chip->regmap, PM822_RTC_MISC6, &data);
	data = (data & 0xf) |(0x5 << 4);
	regmap_write(chip->regmap, PM822_RTC_MISC6, data);

	/*
	  * Enables Dynamic Voltage Control (DVC) for the internal PMIC digital circuitry only,
	  * Enables low jitter version of the 32kHz clock
	  * Gate BUCKs clocks
	  */
	regmap_write(chip->regmap, PM822_LOW_POWER2, 0x30);

	/*base page:reg 0xd0.7 = 1 32kHZ generated from XO */
	regmap_read(chip->regmap, PM822_RTC_CTRL, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM822_RTC_CTRL, data);

	/* set oscillator running locked to 32kHZ*/
	regmap_read(chip->regmap, PM822_OSC_CTRL1, &data);
	data &= ~(1 << 1);
	regmap_write(chip->regmap, PM822_OSC_CTRL1, data);

	regmap_read(chip->regmap, PM822_OSC_CTRL6, &data);
	data &= ~(1 << 1);
	regmap_write(chip->regmap, PM822_OSC_CTRL6, data);

	/* Set internal digital sleep voltage as 0.9V */
	regmap_write(chip->regmap, PM822_LOW_POWER1, 0x00);

	/* Enabele LDO and BUCK clock gating in lpm */
	regmap_write(chip->regmap, PM822_LOW_POWER_CONFIG3, 0x80);
	/* Enable reference group sleep mode */
	regmap_write(chip->regmap, PM822_LOW_POWER_CONFIG4, 0x80);

	/* Enable 32Khz-out-from XO 1, 2 all enabled */
	regmap_write(chip->regmap, PM822_RTC_MISC2, 0xa);

	/* Enable voltage change in pmic, POWER_HOLD = 1 */
	regmap_write(chip->regmap, PM822_WAKEUP1, 0x80);

	/*
	 * Block wakeup attempts when VSYS rises above
	 * VSYS_UNDER_RISE_TH1, or power off may fail
	 * Enable Fault_WU to make watchdog as reset.
	 */
	regmap_read(chip->regmap, PM822_RTC_MISC5, &data);
	data |= 0x5;
	regmap_write(chip->regmap, PM822_RTC_MISC5, data);

	/* Check shutdown reason and clear */
	regmap_read(chip->regmap, PMIC_GENERAL_USE_REGISTER, &data);
	data &= ~(PMIC_GENERAL_USE_SHUTDOWN_MASK);
	regmap_write(chip->regmap, PMIC_GENERAL_USE_REGISTER, data);

	regmap_write(chip->subchip->regmap_gpadc, PM822_GPADC_MEAS_OFF1, 0x80);

	/* Enlarge GPADC off slots */
	regmap_write(chip->subchip->regmap_gpadc, PM822_GPADC_MEAS_OFF2, 0x0f);

	/* Enable GPADC2 */
	regmap_read(chip->subchip->regmap_gpadc, PM822_GPADC_BIAS_EN1, &data);
	data |= (0x1 << 6);
	regmap_write(chip->subchip->regmap_gpadc, PM822_GPADC_BIAS_EN1, data);

	/* Set Bias current fo GPADC2 as 31uA) */
	regmap_read(chip->subchip->regmap_gpadc, PM822_GPADC_BIAS3, &data);
	data = (data & 0xf) | 0x6;
	regmap_write(chip->subchip->regmap_gpadc, PM822_GPADC_BIAS3, data);

	/* Set sleep mode as 0.8V */
	regmap_write(chip->subchip->regmap_power, PM822_BUCK1_SLP, 0x10);

	/* Set buck1 audio mode as 0.8V */
	regmap_write(chip->subchip->regmap_power, PM822_AUDIO_MODE, 0x10);

	/* Enable buck sleep mode */
	regmap_write(chip->subchip->regmap_power, PM822_BUCK_SLP1, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM822_BUCK_SLP2, 0x2);

	/* Disalbe BUCK4 FPWM mode and LPFM setting*/
        regmap_write(chip->subchip->regmap_power, PM822_BUCK4_MISC2, 0xae);

	/*Set both buck4 runtime voltage and sleep voltage to 1.85*/
	regmap_write(chip->subchip->regmap_power, PM822_BUCK4, 0x55);
	regmap_write(chip->subchip->regmap_power, PM822_BUCK4_SLP, 0x55);

	/* Enable ldo sleep mode */
	regmap_write(chip->subchip->regmap_power, PM822_LDO_SLP1, 0xab);
	regmap_write(chip->subchip->regmap_power, PM822_LDO_SLP2, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM822_LDO_SLP3, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM822_LDO_SLP4, 0x0a);
	regmap_write(chip->subchip->regmap_power, PM822_LDO8, 0xee);
	/* Increase VDD_TCXO (PMIC LDO2) to 1.9v */
	regmap_write(chip->subchip->regmap_power, PM822_LDO2, 0x22);

	/*Disable Soft Start of LDO 1/4/13 */
	regmap_read(chip->subchip->regmap_power, PM822_LDO_MISC1, &data);
	data |= (1 << 3);
	regmap_write(chip->subchip->regmap_power, PM822_LDO_MISC1, data);

	regmap_read(chip->subchip->regmap_power, PM822_LDO_MISC2, &data);
	data |= (1 << 7);
	regmap_write(chip->subchip->regmap_power, PM822_LDO_MISC2, data);

	regmap_read(chip->subchip->regmap_power, PM822_LDO_MISC8, &data);
	data |= (1 << 3);
	regmap_write(chip->subchip->regmap_power, PM822_LDO_MISC8, data);


	/*
	* Enables the automatic switch of the GPADC operating mode to duty cycle mode when in sleep
	* Enable GPADC sleep duty cycle mode
	* Automatic scaling of MEAS_OFF parameter when entering low power mode.
	* Automatic scaling of MEAS_OFF parameter when entering low power Mode (8x0).
	*/
	regmap_read(chip->subchip->regmap_gpadc, PM822_GPADC_MISC_CONFIG2, &data);
	data |= PM822_MEANS_EN_SLP | ( 1<<5 ) | PM822_GPADC_NON_STOP | PM822_GPADC_EN;
	regmap_write(chip->subchip->regmap_gpadc, PM822_GPADC_MISC_CONFIG2, data);

	/*2013-12-23 from pmic team*/
	regmap_write(chip->subchip->regmap_power, PM822_BUCK_SHARED_CTRL, 0x06);

	/* Dump power-down log */
	regmap_read(chip->regmap, PM822_POWER_DOWN_LOG1, &data);
	pr_info("PowerDW Log1 0x%x: 0x%x\n", PM822_POWER_DOWN_LOG1, data);
	regmap_read(chip->regmap, PM822_POWER_DOWN_LOG2, &data);
	pr_info("PowerDW Log2 0x%x: 0x%x\n", PM822_POWER_DOWN_LOG2, data);

	/*
	 * DO_NOT clear the power-down log register,
	 * which are used by PMIC SW fuel gauge;
	 */
#if 0
	/* Clear power-down log */
	regmap_write(chip->regmap, PM822_POWER_DOWN_LOG1, 0xff);
	regmap_write(chip->regmap, PM822_POWER_DOWN_LOG2, 0xff);
#endif

	/*set buck2 and buck4 driver selection to be full.
	* this bit is now reserved and default value is 0, if want full
	* drive possibility it should be set to 1.
	* In next version it will be set to 1 by default.
	*/
	regmap_read(chip->subchip->regmap_power, 0x7c, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x7c, data);

	regmap_read(chip->subchip->regmap_power, 0x82, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x82, data);

	/* set buck3 all level to be 1.25v */
	regmap_write(chip->subchip->regmap_power, PM822_BUCK3, 0x34);
	regmap_write(chip->subchip->regmap_power, PM822_BUCK3_1, 0x34);
	regmap_write(chip->subchip->regmap_power, PM822_BUCK3_2, 0x34);
	regmap_write(chip->subchip->regmap_power, PM822_BUCK3_3, 0x34);

	/*
	 * Walkaround: there is a h/w issue that the default switching freq
	 * of buck regulators is half than expected.
	 */
	regmap_write(chip->regmap, PM822_OSC_CTRL4, 0x1f);

	return 0;
}

static int pm805_plat_config(struct pm822_chip *chip,
				struct pm822_platform_data *pdata)
{
	int data;
	if (!chip || !pdata || !chip->regmap) {
		pr_err("%s:chip or pdata is not availiable!\n", __func__);
		return -EINVAL;
	}

	/* power up */
	regmap_read(chip->regmap, 0x01, &data);
	data |= 0x3;
	regmap_write(chip->regmap, 0x01, data);
	msleep(1);
	regmap_write(chip->regmap, 0x30, 0x00);
	/* power off */
	data &= ~0x3;
	regmap_write(chip->regmap, 0x01, data);
	msleep(1);
	return 0;
}

#elif defined(CONFIG_MFD_88PM800)
/*
 * PMIC Regulator 88PM800
 * Power Supply ECOs:
 * ECO#6: V_2P8(LDO14) is wired to LDO7, so LDO14 should keep off
 */
static struct regulator_consumer_supply regulator_supplies[] = {
	/* BUCK power supplies: BUCK[1..5] */
	[PM800_ID_BUCK1] = REGULATOR_SUPPLY("vcc_main", NULL),
	[PM800_ID_BUCK2] = REGULATOR_SUPPLY("v_buck2", NULL),
	[PM800_ID_BUCK3] = REGULATOR_SUPPLY("v_buck3", NULL),
	[PM800_ID_BUCK4] = REGULATOR_SUPPLY("v_rf_vdd", NULL),
	[PM800_ID_BUCK5] = REGULATOR_SUPPLY("v_cam_c", NULL),
	/* LDO power supplies: LDO[1..19] */
	[PM800_ID_LDO1]  = REGULATOR_SUPPLY("v_ldo1", NULL),
	[PM800_ID_LDO2]  = REGULATOR_SUPPLY("v_micbias", NULL),
	[PM800_ID_LDO3]  = REGULATOR_SUPPLY("v_analog_2v8", NULL),
	[PM800_ID_LDO4]  = REGULATOR_SUPPLY("v_usim1", NULL),
	[PM800_ID_LDO5]  = REGULATOR_SUPPLY("v_usb_3v1", NULL),
	[PM800_ID_LDO6]  = REGULATOR_SUPPLY("v_motor_3v", NULL),
	[PM800_ID_LDO7]  = REGULATOR_SUPPLY("v_vramp_2v8"/*V_LDO7*/, NULL),
	[PM800_ID_LDO8]  = REGULATOR_SUPPLY("v_lcd_3V", NULL),
	[PM800_ID_LDO9]  = REGULATOR_SUPPLY("v_wib_3v3", NULL),
	[PM800_ID_LDO10] = REGULATOR_SUPPLY("v_proxy_3v", NULL),
	[PM800_ID_LDO11] = REGULATOR_SUPPLY("v_cam_io", NULL),
	[PM800_ID_LDO12] = REGULATOR_SUPPLY("vqmmc", "sdhci-pxav3.0"),
	[PM800_ID_LDO13] = REGULATOR_SUPPLY("vmmc", "sdhci-pxav3.0"),
	[PM800_ID_LDO14] = REGULATOR_SUPPLY("v_tsp_3v", NULL),
	[PM800_ID_LDO15] = REGULATOR_SUPPLY("v_proxy_led_3v3", NULL),
	[PM800_ID_LDO16] = REGULATOR_SUPPLY("v_cam_avdd", NULL),
	[PM800_ID_LDO17] = REGULATOR_SUPPLY("v_cam_af", NULL),
	[PM800_ID_LDO18] = REGULATOR_SUPPLY("v_ldo18", NULL),
	[PM800_ID_LDO19] = REGULATOR_SUPPLY("v_usim2", NULL),

	/* below 4 IDs are fake ids, they are only used in new dvc */
	[PM800_ID_BUCK1_AP_ACTIVE] = REGULATOR_SUPPLY("vcc_main_ap_active", NULL),
	[PM800_ID_BUCK1_AP_LPM] = REGULATOR_SUPPLY("vcc_main_ap_lpm", NULL),
	[PM800_ID_BUCK1_APSUB_IDLE] = REGULATOR_SUPPLY("vcc_main_apsub_idle", NULL),
	[PM800_ID_BUCK1_APSUB_SLEEP] = REGULATOR_SUPPLY("vcc_main_apsub_sleep", NULL),
};

static int regulator_index[] = {
	PM800_ID_BUCK1,
	PM800_ID_BUCK2,
	PM800_ID_BUCK3,
	PM800_ID_BUCK4,
	PM800_ID_BUCK5,
	PM800_ID_LDO1,
	PM800_ID_LDO2,
	PM800_ID_LDO3,
	PM800_ID_LDO4,
	PM800_ID_LDO5,
	PM800_ID_LDO6,
	PM800_ID_LDO7,
	PM800_ID_LDO8,
	PM800_ID_LDO9,
	PM800_ID_LDO10,
	PM800_ID_LDO11,
	PM800_ID_LDO12,
	PM800_ID_LDO13,
	PM800_ID_LDO14,
	PM800_ID_LDO15,
	PM800_ID_LDO16,
	PM800_ID_LDO17,
	PM800_ID_LDO18,
	PM800_ID_LDO19,

	/* below 4 ids are fake id, they are only used in new dvc */
	PM800_ID_BUCK1_AP_ACTIVE,
	PM800_ID_BUCK1_AP_LPM,
	PM800_ID_BUCK1_APSUB_IDLE,
	PM800_ID_BUCK1_APSUB_SLEEP,
};

#define REG_INIT(_name, _min, _max, _always, _boot)	\
{								\
	.constraints = {					\
		.name		= __stringify(_name),		\
		.min_uV		= _min,				\
		.max_uV		= _max,				\
		.always_on	= _always,			\
		.boot_on	= _boot,			\
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE	\
				| REGULATOR_CHANGE_STATUS,	\
	},							\
	.num_consumer_supplies	= 1,				\
	.consumer_supplies	= &regulator_supplies[PM800_ID_##_name], \
	.driver_data = &regulator_index[PM800_ID_##_name],	\
}
static struct regulator_init_data pm800_regulator_data[] = {
	/* BUCK power supplies: BUCK[1..5] */
	[PM800_ID_BUCK1] = REG_INIT(BUCK1,  600000, 3950000, 1, 1),
	[PM800_ID_BUCK2] = REG_INIT(BUCK2,  600000, 3950000, 1, 1),
	[PM800_ID_BUCK3] = REG_INIT(BUCK3,  600000, 3950000, 1, 1),
	[PM800_ID_BUCK4] = REG_INIT(BUCK4,  600000, 3950000, 1, 1),
	[PM800_ID_BUCK5] = REG_INIT(BUCK5,  600000, 3950000, 0, 0),
	/* LDO power supplies: LDO[1..19] */
	[PM800_ID_LDO1]  = REG_INIT(LDO1,   600000, 1500000, 0, 0),
	[PM800_ID_LDO2]  = REG_INIT(LDO2,  1700000, 2800000, 0, 1),
	[PM800_ID_LDO3]  = REG_INIT(LDO3,  1200000, 3300000, 1, 1),
	[PM800_ID_LDO4]  = REG_INIT(LDO4,  1200000, 3300000, 0, 0),
	[PM800_ID_LDO5]  = REG_INIT(LDO5,  1200000, 3300000, 1, 1),
	[PM800_ID_LDO6]  = REG_INIT(LDO6,  1200000, 3300000, 0, 0),
	[PM800_ID_LDO7]  = REG_INIT(LDO7,  3300000, 3300000, 0, 1),
	[PM800_ID_LDO8]  = REG_INIT(LDO8,  1200000, 3300000, 0, 1),
	[PM800_ID_LDO9]  = REG_INIT(LDO9,  1200000, 3300000, 0, 0),
	[PM800_ID_LDO10] = REG_INIT(LDO10, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO11] = REG_INIT(LDO11, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO12] = REG_INIT(LDO12, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO13] = REG_INIT(LDO13, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO14] = REG_INIT(LDO14, 1200000, 3300000, 1, 1),
	[PM800_ID_LDO15] = REG_INIT(LDO15, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO16] = REG_INIT(LDO16, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO17] = REG_INIT(LDO17, 1200000, 3300000, 0, 0),
	[PM800_ID_LDO18] = REG_INIT(LDO18, 1700000, 3300000, 1, 1),
	[PM800_ID_LDO19] = REG_INIT(LDO19, 1700000, 3300000, 0, 0),

	/* below 4 items are fake, they are only used in new dvc */
	[PM800_ID_BUCK1_AP_ACTIVE] = REG_INIT(BUCK1_AP_ACTIVE,  1000, 10000, 1, 1),
	[PM800_ID_BUCK1_AP_LPM] = REG_INIT(BUCK1_AP_LPM,  1000, 10000, 1, 1),
	[PM800_ID_BUCK1_APSUB_IDLE] = REG_INIT(BUCK1_APSUB_IDLE,  1000, 10000, 1, 1),
	[PM800_ID_BUCK1_APSUB_SLEEP] = REG_INIT(BUCK1_APSUB_SLEEP,  1000, 10000, 1, 1),
};
#elif defined (CONFIG_MFD_D2199)
#define mV_to_uV(v)                 ((v) * 1000)
#define uV_to_mV(v)                 ((v) / 1000)
#define MAX_MILLI_VOLT              (3300)


/* D2199 DC-DCs */
// BUCK1
static struct regulator_consumer_supply d2199_buck1_supplies[] = {
	REGULATOR_SUPPLY("vcc_main", NULL),
};

static struct regulator_init_data d2199_buck1 = {
	.constraints = {
		.min_uV = D2199_BUCK1_VOLT_LOWER,
		.max_uV = D2199_BUCK1_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck1_supplies),
	.consumer_supplies = d2199_buck1_supplies,
};

// BUCK2
static struct regulator_consumer_supply d2199_buck2_supplies[] = {
	REGULATOR_SUPPLY("v_buck2", NULL),
};

static struct regulator_init_data d2199_buck2 = {
	.constraints = {
		.min_uV = D2199_BUCK2_VOLT_LOWER,
		.max_uV = D2199_BUCK2_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck2_supplies),
	.consumer_supplies = d2199_buck2_supplies,
};

// BUCK3
static struct regulator_consumer_supply d2199_buck3_supplies[] = {
	REGULATOR_SUPPLY("v_buck3", NULL),
};

static struct regulator_init_data d2199_buck3 = {
	.constraints = {
		.min_uV = D2199_BUCK3_VOLT_LOWER,
		.max_uV = D2199_BUCK3_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,

	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck3_supplies),
	.consumer_supplies = d2199_buck3_supplies,
};

// BUCK4
static struct regulator_consumer_supply d2199_buck4_supplies[] = {
	REGULATOR_SUPPLY("v_rf_vdd", NULL),
};

static struct regulator_init_data d2199_buck4 = {
	.constraints = {
		.min_uV = D2199_BUCK4_VOLT_LOWER,
		.max_uV = D2199_BUCK4_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck4_supplies),
	.consumer_supplies = d2199_buck4_supplies,
};

// BUCK5
static struct regulator_consumer_supply d2199_buck5_supplies[] = {
	REGULATOR_SUPPLY("v_cam_c", NULL),
};

static struct regulator_init_data d2199_buck5 = {
	.constraints = {
		.min_uV = D2199_BUCK5_VOLT_LOWER,
		.max_uV = D2199_BUCK5_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck5_supplies),
	.consumer_supplies = d2199_buck5_supplies,
};

// BUCK6 -> Not connected
static struct regulator_consumer_supply d2199_buck6_supplies[] = {
	REGULATOR_SUPPLY("v_BUCK6", NULL),
};

static struct regulator_init_data d2199_buck6 = {
	.constraints = {
		.min_uV = D2199_BUCK6_VOLT_LOWER,
		.max_uV = D2199_BUCK6_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck6_supplies),
	.consumer_supplies = d2199_buck6_supplies,
};

/* D2199 LDOs */
// LDO1
__weak struct regulator_consumer_supply d2199_ldo1_supplies[] = {
	REGULATOR_SUPPLY("v_vramp_2v8", NULL),	// V_VRAMP_2.8V
};

static struct regulator_init_data d2199_ldo1 = {
	.constraints = {
		.min_uV = D2199_LDO1_VOLT_LOWER,
		.max_uV = D2199_LDO1_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,

	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo1_supplies),
	.consumer_supplies = d2199_ldo1_supplies,
};


// LDO2
__weak struct regulator_consumer_supply d2199_ldo2_supplies[] = {
	REGULATOR_SUPPLY("v_cam_io", NULL),	// VCAM_IO_1.8V
};

static struct regulator_init_data d2199_ldo2 = {
	.constraints = {
		.min_uV = D2199_LDO2_VOLT_LOWER,
		.max_uV = D2199_LDO2_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo2_supplies),
	.consumer_supplies = d2199_ldo2_supplies,
};

// LDO3
__weak struct regulator_consumer_supply d2199_ldo3_supplies[] = {
	REGULATOR_SUPPLY("v_analog_2v8", NULL),	// V_ANALOG_2.8V
};

static struct regulator_init_data d2199_ldo3 = {
	.constraints = {
		.min_uV = D2199_LDO3_VOLT_LOWER,
		.max_uV = D2199_LDO3_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,

	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo3_supplies),
	.consumer_supplies = d2199_ldo3_supplies,
};



// LDO4
__weak struct regulator_consumer_supply d2199_ldo4_supplies[] = {
	REGULATOR_SUPPLY("v_cam_avdd", NULL),	// VCAM_A_2.8V
};

static struct regulator_init_data d2199_ldo4 = {
	.constraints = {
		.min_uV = D2199_LDO4_VOLT_LOWER,
		.max_uV = D2199_LDO4_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo4_supplies),
	.consumer_supplies = d2199_ldo4_supplies,
};


// LDO5
__weak struct regulator_consumer_supply d2199_ldo5_supplies[] = {
	REGULATOR_SUPPLY("v_usb_3v1", NULL),	// v_usb_3v1
};

static struct regulator_init_data d2199_ldo5 = {
	.constraints = {
		.min_uV = D2199_LDO5_VOLT_LOWER,
		.max_uV = D2199_LDO5_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,

	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo5_supplies),
	.consumer_supplies = d2199_ldo5_supplies,
};


// LDO6
__weak struct regulator_consumer_supply d2199_ldo6_supplies[] = {
	REGULATOR_SUPPLY("v_ldo18", NULL),	// VDD_DIG_1.8V
};

static struct regulator_init_data d2199_ldo6 = {
	.constraints = {
		.min_uV = D2199_LDO6_VOLT_LOWER,
		.max_uV = D2199_LDO6_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo6_supplies),
	.consumer_supplies = d2199_ldo6_supplies,
};

// LDO7
__weak struct regulator_consumer_supply d2199_ldo7_supplies[] = {
	REGULATOR_SUPPLY("v_tsp_3v3", NULL),	// V_TSP_3.3V
};

static struct regulator_init_data d2199_ldo7 = {
	.constraints = {
		.min_uV = D2199_LDO7_VOLT_LOWER,
		.max_uV = D2199_LDO7_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 0,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo7_supplies),
	.consumer_supplies = d2199_ldo7_supplies,
};


// LDO8
__weak struct regulator_consumer_supply d2199_ldo8_supplies[] = {
	REGULATOR_SUPPLY("v_lcd_3V", NULL),	// V_LCD_3.0V
};

static struct regulator_init_data d2199_ldo8 = {
	.constraints = {
		.min_uV = D2199_LDO8_VOLT_LOWER,
		.max_uV = D2199_LDO8_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 0,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo8_supplies),
	.consumer_supplies = d2199_ldo8_supplies,
};

// LDO8_REV00
__weak struct regulator_consumer_supply d2199_ldo8_supplies_rev00[] = {
	REGULATOR_SUPPLY("v_proxy_led_3v3", NULL),	// V_PROXY_LED_3.3V
};

static struct regulator_init_data d2199_ldo8_rev00 = {
	.constraints = {
		.min_uV = D2199_LDO8_VOLT_LOWER,
		.max_uV = D2199_LDO8_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo8_supplies_rev00),
	.consumer_supplies = d2199_ldo8_supplies_rev00,
};


// LDO9
__weak struct regulator_consumer_supply d2199_ldo9_supplies[] = {
	REGULATOR_SUPPLY("v_proxy_3v", NULL),	// V_PROXY_3.0V
};

static struct regulator_init_data d2199_ldo9 = {
	.constraints = {
		.min_uV = D2199_LDO9_VOLT_LOWER,
		.max_uV = D2199_LDO9_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo9_supplies),
	.consumer_supplies = d2199_ldo9_supplies,
};

// LDO10
__weak struct regulator_consumer_supply d2199_ldo10_supplies[] = {
	REGULATOR_SUPPLY("v_wib_3v3", NULL),	// V_WIFI_3.3V
};

static struct regulator_init_data d2199_ldo10 = {
	.constraints = {
		.min_uV = D2199_LDO10_VOLT_LOWER,
		.max_uV = D2199_LDO10_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo10_supplies),
	.consumer_supplies = d2199_ldo10_supplies,
};

// LDO11
__weak struct regulator_consumer_supply d2199_ldo11_supplies[] = {
	REGULATOR_SUPPLY("v_cam_af", NULL),	// VCAM_AF_2.8V
};

static struct regulator_init_data d2199_ldo11 = {
	.constraints = {
		.min_uV = D2199_LDO11_VOLT_LOWER,
		.max_uV = D2199_LDO11_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo11_supplies),
	.consumer_supplies = d2199_ldo11_supplies,
};

// LDO12
__weak struct regulator_consumer_supply d2199_ldo12_supplies[] = {
	REGULATOR_SUPPLY("v_proxy_led_3v3", NULL),	// V_PROXY_LED_3.3V
};

static struct regulator_init_data d2199_ldo12 = {
	.constraints = {
		.min_uV = D2199_LDO12_VOLT_LOWER,
		.max_uV = D2199_LDO12_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo12_supplies),
	.consumer_supplies = d2199_ldo12_supplies,
};

// LDO12_REV00
__weak struct regulator_consumer_supply d2199_ldo12_supplies_rev00[] = {
	REGULATOR_SUPPLY("v_lcd_3V", NULL),	// V_LCD_3.0V
};

static struct regulator_init_data d2199_ldo12_rev00 = {
	.constraints = {
		.min_uV = D2199_LDO12_VOLT_LOWER,
		.max_uV = D2199_LDO12_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 0,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo12_supplies_rev00),
	.consumer_supplies = d2199_ldo12_supplies_rev00,
};


// LDO13
__weak struct regulator_consumer_supply d2199_ldo13_supplies[] = {
	REGULATOR_SUPPLY("v_usim1", NULL),	// V_SIM1_3.0V
};

static struct regulator_init_data d2199_ldo13 = {
	.constraints = {
		.min_uV = D2199_LDO13_VOLT_LOWER,
		.max_uV = D2199_LDO13_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo13_supplies),
	.consumer_supplies = d2199_ldo13_supplies,
};


// LDO14
__weak struct regulator_consumer_supply d2199_ldo14_supplies[] = {
	REGULATOR_SUPPLY("v_usim2", NULL),	// V_SIM2_3.1V
};

static struct regulator_init_data d2199_ldo14 = {
	.constraints = {
		.min_uV = D2199_LDO14_VOLT_LOWER,
		.max_uV = D2199_LDO14_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo14_supplies),
	.consumer_supplies = d2199_ldo14_supplies,
};


// LDO15
__weak struct regulator_consumer_supply d2199_ldo15_supplies[] = {
	REGULATOR_SUPPLY("vqmmc", "sdhci-pxav3.0"),	// V_MMC_3.3V
};

static struct regulator_init_data d2199_ldo15 = {
	.constraints = {
		.min_uV = D2199_LDO15_VOLT_LOWER,
		.max_uV = D2199_LDO15_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo15_supplies),
	.consumer_supplies = d2199_ldo15_supplies,
};



// LDO16
__weak struct regulator_consumer_supply d2199_ldo16_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "sdhci-pxav3.0"),	// V_SD_3.3V
};

static struct regulator_init_data d2199_ldo16 = {
	.constraints = {
		.min_uV = D2199_LDO16_VOLT_LOWER,
		.max_uV = D2199_LDO16_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo16_supplies),
	.consumer_supplies = d2199_ldo16_supplies,
};


// LDO17
__weak struct regulator_consumer_supply d2199_ldo17_supplies[] = {
	REGULATOR_SUPPLY("v_ldo17_dummy", NULL),	// Not connected
};

static struct regulator_init_data d2199_ldo17 = {
	.constraints = {
		.min_uV = D2199_LDO17_VOLT_LOWER,
		.max_uV = D2199_LDO17_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo17_supplies),
	.consumer_supplies = d2199_ldo17_supplies,
};

// LDO18
__weak struct regulator_consumer_supply d2199_ldo18_supplies[] = {
	REGULATOR_SUPPLY("v_ldo18_dummy", NULL),	// Notconnected
};

static struct regulator_init_data d2199_ldo18 = {
	.constraints = {
		.min_uV = D2199_LDO18_VOLT_LOWER,
		.max_uV = D2199_LDO18_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo18_supplies),
	.consumer_supplies = d2199_ldo18_supplies,
};


// LDO19
__weak struct regulator_consumer_supply d2199_ldo19_supplies[] = {
	REGULATOR_SUPPLY("v_ldo19_dummy", NULL),	// Not connected
};

static struct regulator_init_data d2199_ldo19 = {
	.constraints = {
		.min_uV = D2199_LDO19_VOLT_LOWER,
		.max_uV = D2199_LDO19_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo19_supplies),
	.consumer_supplies = d2199_ldo19_supplies,
};

// LDO20
__weak struct regulator_consumer_supply d2199_ldo20_supplies[] = {
	REGULATOR_SUPPLY("v_ldo20_dummy", NULL),	// Notconnected
};

static struct regulator_init_data d2199_ldo20 = {
	.constraints = {
		.min_uV = D2199_LDO20_VOLT_LOWER,
		.max_uV = D2199_LDO20_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldo20_supplies),
	.consumer_supplies = d2199_ldo20_supplies,
};

// LDO_AUD_1
__weak struct regulator_consumer_supply d2199_ldoaud1_supplies[] = {
	REGULATOR_SUPPLY("aud1", NULL),	// aud1
};

static struct regulator_init_data d2199_ldoaud1 = {
	.constraints = {
		.min_uV = D2199_LDOAUD1_VOLT_LOWER,
		.max_uV = D2199_LDOAUD1_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldoaud1_supplies),
	.consumer_supplies = d2199_ldoaud1_supplies,
};

// LDO_AUD_2
__weak struct regulator_consumer_supply d2199_ldoaud2_supplies[] = {
	REGULATOR_SUPPLY("aud2", NULL),	// aud2
};

static struct regulator_init_data d2199_ldoaud2 = {
	.constraints = {
		.min_uV = D2199_LDOAUD2_VOLT_LOWER,
		.max_uV = D2199_LDOAUD2_VOLT_UPPER,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_ldoaud2_supplies),
	.consumer_supplies = d2199_ldoaud2_supplies,
};


#if defined(CONFIG_D2199_DVC)
static struct regulator_consumer_supply d2199_buck1_ap_active_supplies[] = {
	REGULATOR_SUPPLY("vcc_main_ap_active", NULL),
};

static struct regulator_init_data d2199_buck1_ap_active = {
	.constraints = {
		.min_uV = 1000,
		.max_uV = 10000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck1_ap_active_supplies),
	.consumer_supplies = d2199_buck1_ap_active_supplies,
};
//---
static struct regulator_consumer_supply d2199_buck1_ap_lpm_supplies[] = {
	REGULATOR_SUPPLY("vcc_main_ap_lpm", NULL),
};

static struct regulator_init_data d2199_buck1_ap_lpm = {
	.constraints = {
		.min_uV = 1000,
		.max_uV = 10000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck1_ap_lpm_supplies),
	.consumer_supplies = d2199_buck1_ap_lpm_supplies,
};
//---
static struct regulator_consumer_supply d2199_buck1_apsub_idle_supplies[] = {
	REGULATOR_SUPPLY("vcc_main_apsub_idle", NULL),
};

static struct regulator_init_data d2199_buck1_apsub_idle = {
	.constraints = {
		.min_uV = 1000,
		.max_uV = 10000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck1_apsub_idle_supplies),
	.consumer_supplies = d2199_buck1_apsub_idle_supplies,
};
//---
static struct regulator_consumer_supply d2199_buck1_apsub_sleep_supplies[] = {
	REGULATOR_SUPPLY("vcc_main_apsub_sleep", NULL),
};

static struct regulator_init_data d2199_buck1_apsub_sleep = {
	.constraints = {
		.min_uV = 1000,
		.max_uV = 10000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(d2199_buck1_apsub_sleep_supplies),
	.consumer_supplies = d2199_buck1_apsub_sleep_supplies,
};

#endif
static struct d2199_regl_init_data d2199_regulators_init_data[D2199_NUMBER_OF_REGULATORS] = {
	[D2199_BUCK_1] = { D2199_BUCK_1,  &d2199_buck1 },
	[D2199_BUCK_2] = { D2199_BUCK_2,  &d2199_buck2 },
	[D2199_BUCK_3] = { D2199_BUCK_3,  &d2199_buck3 },
	[D2199_BUCK_4] = { D2199_BUCK_4,  &d2199_buck4 },
	[D2199_BUCK_5] = { D2199_BUCK_5,  &d2199_buck5 },
	[D2199_BUCK_6] = { D2199_BUCK_6,  &d2199_buck6 },

	[D2199_LDO_1]  = { D2199_LDO_1, &d2199_ldo1 },
	[D2199_LDO_2]  = { D2199_LDO_2, &d2199_ldo2 },
	[D2199_LDO_3]  = { D2199_LDO_3, &d2199_ldo3 },
	[D2199_LDO_4]  = { D2199_LDO_4, &d2199_ldo4 },
	[D2199_LDO_5]  = { D2199_LDO_5, &d2199_ldo5 },
	[D2199_LDO_6]  = { D2199_LDO_6, &d2199_ldo6 },
	[D2199_LDO_7]  = { D2199_LDO_7, &d2199_ldo7 },
	[D2199_LDO_8]  = { D2199_LDO_8, &d2199_ldo8 },
	[D2199_LDO_9]  = { D2199_LDO_9, &d2199_ldo9 },
	[D2199_LDO_10] = { D2199_LDO_10, &d2199_ldo10 },
	[D2199_LDO_11] = { D2199_LDO_11, &d2199_ldo11 },
	[D2199_LDO_12] = { D2199_LDO_12, &d2199_ldo12 },
	[D2199_LDO_13] = { D2199_LDO_13, &d2199_ldo13 },
	[D2199_LDO_14] = { D2199_LDO_14, &d2199_ldo14 },
	[D2199_LDO_15] = { D2199_LDO_15, &d2199_ldo15 },
	[D2199_LDO_16] = { D2199_LDO_16, &d2199_ldo16 },
	[D2199_LDO_17] = { D2199_LDO_17, &d2199_ldo17 },
	[D2199_LDO_18] = { D2199_LDO_15, &d2199_ldo18 },
	[D2199_LDO_19] = { D2199_LDO_16, &d2199_ldo19 },
	[D2199_LDO_20] = { D2199_LDO_17, &d2199_ldo20 },

	[D2199_LDO_AUD1] = { D2199_LDO_AUD1, &d2199_ldoaud1 },
	[D2199_LDO_AUD2] = { D2199_LDO_AUD2, &d2199_ldoaud2 },

#if defined(CONFIG_D2199_DVC)
	/* below 4 items are fake, they are only used in new dvc */
	[D2199_ID_BUCK1_AP_ACTIVE] 	 = { D2199_ID_BUCK1_AP_ACTIVE, 	&d2199_buck1_ap_active },
	[D2199_ID_BUCK1_AP_LPM] 	 = { D2199_ID_BUCK1_AP_LPM, 	&d2199_buck1_ap_lpm },
	[D2199_ID_BUCK1_APSUB_IDLE]  = { D2199_ID_BUCK1_APSUB_IDLE, &d2199_buck1_apsub_idle },
	[D2199_ID_BUCK1_APSUB_SLEEP] = { D2199_ID_BUCK1_APSUB_SLEEP,&d2199_buck1_apsub_sleep },
#endif
};

static struct d2199_regl_init_data d2199_regulators_init_data_rev00[D2199_NUMBER_OF_REGULATORS] = {
	[D2199_BUCK_1] = { D2199_BUCK_1,  &d2199_buck1 },
	[D2199_BUCK_2] = { D2199_BUCK_2,  &d2199_buck2 },
	[D2199_BUCK_3] = { D2199_BUCK_3,  &d2199_buck3 },
	[D2199_BUCK_4] = { D2199_BUCK_4,  &d2199_buck4 },
	[D2199_BUCK_5] = { D2199_BUCK_5,  &d2199_buck5 },
	[D2199_BUCK_6] = { D2199_BUCK_6,  &d2199_buck6 },

	[D2199_LDO_1]  = { D2199_LDO_1, &d2199_ldo1 },
	[D2199_LDO_2]  = { D2199_LDO_2, &d2199_ldo2 },
	[D2199_LDO_3]  = { D2199_LDO_3, &d2199_ldo3 },
	[D2199_LDO_4]  = { D2199_LDO_4, &d2199_ldo4 },
	[D2199_LDO_5]  = { D2199_LDO_5, &d2199_ldo5 },
	[D2199_LDO_6]  = { D2199_LDO_6, &d2199_ldo6 },
	[D2199_LDO_7]  = { D2199_LDO_7, &d2199_ldo7 },
	[D2199_LDO_8]  = { D2199_LDO_8, &d2199_ldo8_rev00 },
	[D2199_LDO_9]  = { D2199_LDO_9, &d2199_ldo9 },
	[D2199_LDO_10] = { D2199_LDO_10, &d2199_ldo10 },
	[D2199_LDO_11] = { D2199_LDO_11, &d2199_ldo11 },
	[D2199_LDO_12] = { D2199_LDO_12, &d2199_ldo12_rev00 },
	[D2199_LDO_13] = { D2199_LDO_13, &d2199_ldo13 },
	[D2199_LDO_14] = { D2199_LDO_14, &d2199_ldo14 },
	[D2199_LDO_15] = { D2199_LDO_15, &d2199_ldo15 },
	[D2199_LDO_16] = { D2199_LDO_16, &d2199_ldo16 },
	[D2199_LDO_17] = { D2199_LDO_17, &d2199_ldo17 },
	[D2199_LDO_18] = { D2199_LDO_15, &d2199_ldo18 },
	[D2199_LDO_19] = { D2199_LDO_16, &d2199_ldo19 },
	[D2199_LDO_20] = { D2199_LDO_17, &d2199_ldo20 },

	[D2199_LDO_AUD1] = { D2199_LDO_AUD1, &d2199_ldoaud1 },
	[D2199_LDO_AUD2] = { D2199_LDO_AUD2, &d2199_ldoaud2 },

#if defined(CONFIG_D2199_DVC)
	/* below 4 items are fake, they are only used in new dvc */
	[D2199_ID_BUCK1_AP_ACTIVE] 	 = { D2199_ID_BUCK1_AP_ACTIVE, 	&d2199_buck1_ap_active },
	[D2199_ID_BUCK1_AP_LPM] 	 = { D2199_ID_BUCK1_AP_LPM, 	&d2199_buck1_ap_lpm },
	[D2199_ID_BUCK1_APSUB_IDLE]  = { D2199_ID_BUCK1_APSUB_IDLE, &d2199_buck1_apsub_idle },
	[D2199_ID_BUCK1_APSUB_SLEEP] = { D2199_ID_BUCK1_APSUB_SLEEP,&d2199_buck1_apsub_sleep },
#endif
};

struct d2199_battery_platform_data pbat_pdata = {
       .battery_technology = POWER_SUPPLY_TECHNOLOGY_LION,
       .battery_capacity = 1300,
       .vf_lower    = 250,
       .vf_upper = 510,
};

#if defined(CONFIG_D2199_DVC)
static struct d2199_dvc_pdata d2199_dvc = {
	.dvc1		= MFP_PIN_GPIO43,
	.dvc2		= MFP_PIN_GPIO44,
	.gpio_dvc	= 1,
};
#endif

static int sync_time_to_soc(unsigned int ticks);

struct d2199_headset_pdata d2199_headset = {
	/*
	 * ADC Range 0 - 255
	 */
	.send_min = 0,
	.send_max = 15,
	.vol_up_min = 16,
	.vol_up_max = 31,
	.vol_down_min = 32,
	.vol_down_max = 66,
	.jack_3pole_max = 94,
	.jack_4pole_max = 255
};

struct d2199_platform_data d2199_pdata = {
	.headset = &d2199_headset,  // headset plaform data
#if defined(CONFIG_BATTERY_SAMSUNG)
	.pbat_platform = &sec_battery_pdata,
#else
	.pbat_platform  = &pbat_pdata,
#endif
#if defined(CONFIG_D2199_DVC)
	.dvc = &d2199_dvc,
#endif
#ifdef CONFIG_RTC_DRV_SA1100
	.sync	= sync_time_to_soc,
#endif
	.regulator_data = &d2199_regulators_init_data[0],
	.regl_map = {
		/*
		 *		Define initial MCTL value of WILCOX with D2199
		 *
		 *	[ LDO ]	0x0 : Off	[ BUCK 2,3,4]	0x0 : Off
		 *			0x1 : On					0x1 : On
		 *			0x2 : Sleep - LPM			0x2 : Sleep(Force PFM mode) - LPM
		 *	0x66 :	01		10		01		10	(ON , LPM, ON , LPM)
		 *	0x44 :	01		00		01		00	(ON , OFF, ON , OFF)
		 * ---------------------------------------------------------------
		 *
		*/
	 // for WILCOX 20130426 -  should be connected to M_CTL1
	D2199_MCTL_MODE_INIT(D2199_BUCK_1, 0x06, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_BUCK_2, 0x06, D2199_REGULATOR_LPM_IN_DSM), // VREG_1.8V
	D2199_MCTL_MODE_INIT(D2199_BUCK_3, 0x06, D2199_REGULATOR_LPM_IN_DSM), // VREG_1.2V
	D2199_MCTL_MODE_INIT(D2199_BUCK_4, 0x06, D2199_REGULATOR_LPM_IN_DSM), // V_DIGRF_1.8V
	D2199_MCTL_MODE_INIT(D2199_BUCK_5, 0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_C_1.2V
	D2199_MCTL_MODE_INIT(D2199_BUCK_6, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_1,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_VRAMP_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_2,  0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_IO_1.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_3,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_ANALOG_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_4,  0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_A_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_5,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_USB_3.1V
	D2199_MCTL_MODE_INIT(D2199_LDO_6,  0x06, D2199_REGULATOR_LPM_IN_DSM), // VDD_DIG_1.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_7,  0x04, D2199_REGULATOR_OFF_IN_DSM), // V_TSP_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_8,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_LCD_3.0V
	D2199_MCTL_MODE_INIT(D2199_LDO_9,  0x00, D2199_REGULATOR_OFF_IN_DSM), // V_PROXY_3.0V
	D2199_MCTL_MODE_INIT(D2199_LDO_10, 0x02, D2199_REGULATOR_LPM_IN_DSM), // V_WIFI_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_11, 0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_AF_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_12, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_PROXY_LED_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_13, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_SIM1_3.0V
	D2199_MCTL_MODE_INIT(D2199_LDO_14, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_SIM2_3.1V
	D2199_MCTL_MODE_INIT(D2199_LDO_15, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_MMC_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_16, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_SD_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_17, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_18, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_19, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_20, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected

	D2199_MCTL_MODE_INIT(D2199_LDO_AUD1, 0x00, D2199_REGULATOR_OFF_IN_DSM), // LDO_AUD1 1.8
	D2199_MCTL_MODE_INIT(D2199_LDO_AUD2, 0x00, D2199_REGULATOR_OFF_IN_DSM), // LDO_AUD2 3.3

#if defined(CONFIG_D2199_DVC)
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_AP_ACTIVE, 	0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_AP_LPM, 	0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_APSUB_IDLE, 0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_APSUB_SLEEP,0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
#endif
	},
};

struct d2199_platform_data d2199_pdata_rev00 = {
#if defined(CONFIG_BATTERY_SAMSUNG)
	.pbat_platform = &sec_battery_pdata,
#else
	.pbat_platform  = &pbat_pdata,
#endif
#if defined(CONFIG_D2199_DVC)
	.dvc = &d2199_dvc,
#endif
#ifdef CONFIG_RTC_DRV_SA1100
	.sync	= sync_time_to_soc,
#endif
	.regulator_data = &d2199_regulators_init_data_rev00[0],
	.regl_map = {
		/*
		 *		Define initial MCTL value of WILCOX with D2199
		 *
		 *	[ LDO ]	0x0 : Off	[ BUCK 2,3,4]	0x0 : Off
		 *			0x1 : On					0x1 : On
		 *			0x2 : Sleep - LPM			0x2 : Sleep(Force PFM mode) - LPM
		 *	0x66 :	01		10		01		10	(ON , LPM, ON , LPM)
		 *	0x44 :	01		00		01		00	(ON , OFF, ON , OFF)
		 * ---------------------------------------------------------------
		 *
		*/
	 // for WILCOX 20130426 -  should be connected to M_CTL1
	D2199_MCTL_MODE_INIT(D2199_BUCK_1, 0x06, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_BUCK_2, 0x06, D2199_REGULATOR_LPM_IN_DSM), // VREG_1.8V
	D2199_MCTL_MODE_INIT(D2199_BUCK_3, 0x06, D2199_REGULATOR_LPM_IN_DSM), // VREG_1.2V
	D2199_MCTL_MODE_INIT(D2199_BUCK_4, 0x06, D2199_REGULATOR_LPM_IN_DSM), // V_DIGRF_1.8V
	D2199_MCTL_MODE_INIT(D2199_BUCK_5, 0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_C_1.2V
	D2199_MCTL_MODE_INIT(D2199_BUCK_6, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_1,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_VRAMP_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_2,  0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_IO_1.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_3,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_ANALOG_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_4,  0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_A_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_5,  0x06, D2199_REGULATOR_LPM_IN_DSM), // V_USB_3.1V
	D2199_MCTL_MODE_INIT(D2199_LDO_6,  0x06, D2199_REGULATOR_LPM_IN_DSM), // VDD_DIG_1.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_7,  0x04, D2199_REGULATOR_OFF_IN_DSM), // V_TSP_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_8,  0x00, D2199_REGULATOR_OFF_IN_DSM), // V_PROXY_LED_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_9,  0x00, D2199_REGULATOR_OFF_IN_DSM), // V_PROXY_3.0V
	D2199_MCTL_MODE_INIT(D2199_LDO_10, 0x02, D2199_REGULATOR_LPM_IN_DSM), // V_WIFI_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_11, 0x00, D2199_REGULATOR_OFF_IN_DSM), // VCAM_AF_2.8V
	D2199_MCTL_MODE_INIT(D2199_LDO_12, 0x06, D2199_REGULATOR_LPM_IN_DSM), // V_LCD_3.0V
	D2199_MCTL_MODE_INIT(D2199_LDO_13, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_SIM1_3.0V
	D2199_MCTL_MODE_INIT(D2199_LDO_14, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_SIM2_3.1V
	D2199_MCTL_MODE_INIT(D2199_LDO_15, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_MMC_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_16, 0x00, D2199_REGULATOR_OFF_IN_DSM), // V_SD_3.3V
	D2199_MCTL_MODE_INIT(D2199_LDO_17, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_18, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_19, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected
	D2199_MCTL_MODE_INIT(D2199_LDO_20, 0x00, D2199_REGULATOR_OFF_IN_DSM), // -- Not connected

	D2199_MCTL_MODE_INIT(D2199_LDO_AUD1, 0x00, D2199_REGULATOR_OFF_IN_DSM), // LDO_AUD1 1.8
	D2199_MCTL_MODE_INIT(D2199_LDO_AUD2, 0x00, D2199_REGULATOR_OFF_IN_DSM), // LDO_AUD2 3.3

#if defined(CONFIG_D2199_DVC)
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_AP_ACTIVE, 	0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_AP_LPM, 	0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_APSUB_IDLE, 0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
	D2199_MCTL_MODE_INIT(D2199_ID_BUCK1_APSUB_SLEEP,0xDE, D2199_REGULATOR_LPM_IN_DSM), // VCORE_MAIN_1.2V
#endif
	},
};

#if defined(CONFIG_D2199_DVC)
static void d2199_dvctable_init(void)
{
	unsigned int *vol_table;
	/* dvc only support 4 lvl voltage*/
	unsigned int vol_tbsize = 4;
	unsigned int index, max_vl, lowest_rate;

	//printk("[WS][DVFS][%s]\n", __func__);

	vol_table = kmalloc(vol_tbsize * sizeof(unsigned int), GFP_KERNEL);
	if (!vol_table) {
		pr_err("%s failed to malloc vol table!\n", __func__);
		return ;
	}

	max_vl = pxa988_get_vl_num();
	max_vl = (max_vl > 4) ? 4 : max_vl;
	for (index = 0; index < max_vl; index++){
		vol_table[index] = pxa988_get_vl(index) * 1000;
		//printk("[WS][DVFS][%s], vol_table[%d] = [0x%x]\n", __func__, index, vol_table[index]);
	}

	lowest_rate = pxa988_get_vl(0);
	while (index < 4)
		vol_table[index++] = lowest_rate * 1000;

	d2199_dvc.vol_val = vol_table;
	d2199_dvc.size	= vol_tbsize;
	return ;
}
#endif
#endif

#ifdef CONFIG_REGULATOR_RT5033
#include <linux/mfd/rt5033.h>
static struct regulator_consumer_supply rt5033_safe_ldo_consumers[] = {
	REGULATOR_SUPPLY("vbus_5v_ldo",NULL),
};

static struct regulator_consumer_supply rt5033_ldo_consumers[] = {
	REGULATOR_SUPPLY("v_touchkey_2v2",NULL),
};

static struct regulator_consumer_supply rt5033_buck_consumers[] = {
	REGULATOR_SUPPLY("v_cam_c_1.2v",NULL),
};

static struct regulator_init_data rt5033_safe_ldo_data = {
	.constraints = {
		.name		= "VBUS_5V_LDO",
		.min_uV = 3300000,
		.max_uV = 4950000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5033_safe_ldo_consumers),
	.consumer_supplies = rt5033_safe_ldo_consumers,
};

static struct regulator_init_data rt5033_ldo_data = {
	.constraints	= {
		.name		= "V_TOUCHKEY_2V2",
		.min_uV = 2200000,
		.max_uV = 2200000,
		.always_on = 0,
		.boot_on = 0,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5033_ldo_consumers),
	.consumer_supplies = rt5033_ldo_consumers,
};

static struct regulator_init_data rt5033_buck_data = {
	.constraints	= {
		.name		= "V_CAM_C_1.2V",
		.min_uV = 1200000,
		.max_uV = 1200000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(rt5033_buck_consumers),
	.consumer_supplies = rt5033_buck_consumers,
};

struct rt5033_regulator_platform_data rv_pdata = {
	.regulator = {
		[RT5033_ID_LDO_SAFE] = &rt5033_safe_ldo_data,
		[RT5033_ID_LDO1] = &rt5033_ldo_data,
		[RT5033_ID_DCDC1] = &rt5033_buck_data,
	},
};
#endif

#if defined(CONFIG_TOUCHSCREEN_MXT336S)
struct tsp_callbacks *charger_callbacks;
struct tsp_callbacks {
	void (*inform_charger)(struct tsp_callbacks *tsp_cb, bool mode);
};
#endif

static void mic_set_power(int on)
{
	static int mic_power_flag = 0;
	struct regulator *v_ldo = regulator_get(NULL, "v_micbias");
	if (IS_ERR(v_ldo)) {
		v_ldo = NULL;
		pr_err("Get regulator error\n");
		return;
	}

	if (on && (!mic_power_flag)) {
		regulator_enable(v_ldo);
		mic_power_flag = 1;
	}

	if (mic_power_flag && (!on)) {
		regulator_disable(v_ldo);
		mic_power_flag = 0;
	}

	regulator_put(v_ldo);
	v_ldo = NULL;
}

#ifdef CONFIG_RTC_DRV_SA1100
static int sync_time_to_soc(unsigned int ticks)
{
	RCNR = ticks;
	udelay(200);
	return 0;
}
#endif

#if defined(CONFIG_MFD_88PM822)
static struct pm822_usb_pdata pm822_usb = {
	.vbus_gpio = PM822_NO_GPIO,
	.id_gpadc = PM822_NO_GPADC,
};

static struct pm822_rtc_pdata pm822_rtc = {
	.vrtc	= 1,
#ifdef CONFIG_RTC_DRV_SA1100
	.sync	= sync_time_to_soc,
#endif
};

static struct pm822_platform_data pm805_info = {
	.irq_mode = 0,
	.plat_config = pm805_plat_config,
};

#ifdef CONFIG_SAMSUNG_JACK
/* Samsung headset driver data KSND */
static struct sec_jack_zone sec_jack_zones[] = {
	{
		/* adc == 0, default to 3pole if it stays
		 * in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 0,
		.delay_ms = 20,
		.check_count = 2,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 0 < adc <= 350, unstable zone, default to 3pole if it stays
		 * in this range for a 100ms (20ms delays, 5 samples)
		 */
		.adc_high = 350,
		.delay_ms = 20,
		.check_count = 5,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 350 < adc <= 500, unstable zone, default to 4pole if it
		 * stays in this range for 200ms (20ms delays, 10 samples)
		 */
		.adc_high = 500,
		.delay_ms = 20,
		.check_count = 10,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* 500 < adc <= 1399, default to 4 pole if it stays */
		/* in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 1399,
		.delay_ms = 20,
		.check_count = 2,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 1400, unstable zone, default to 3pole if it stays
		 * in this range for a second (10ms delays, 100 samples)
		 */
		.adc_high = 0x7fffffff,
		.delay_ms = 10,
		.check_count = 100,
		.jack_type = SEC_HEADSET_3POLE,
	},
};

/* to support 3-buttons earjack */
static struct sec_jack_buttons_zone sec_jack_buttons_zones[] = {
	{
		/* 0 <= adc <=60, stable zone */
		.code		= KEY_MEDIA,
		.adc_low	= 0,
		.adc_high	= 60,
	},
	{
		/* 61 <= adc <= 110, stable zone */
		.code		= KEY_VOLUMEUP,
		.adc_low	= 61,
		.adc_high	= 110,
	},
	{
		/* 111 <= adc <= 240, stable zone */
		.code		= KEY_VOLUMEDOWN,
		.adc_low	= 111,
		.adc_high	= 240,
	},
};

static struct sec_jack_platform_data sec_jack_pdata = {
	.headset_flag = 0,
	.mic_set_power = mic_set_power,
#ifdef SEC_USE_ANLOGDOCK_DEVICE
	.dock_audiopath_ctrl = sm5502_dock_audiopath_ctrl,
	.chgpump_ctrl = sm5502_chgpump_ctrl,
	.usb_switch_register_notify = usb_switch_register_notify,
	.usb_switch_unregister_notify = usb_switch_unregister_notify,
#endif /* SEC_USE_ANLOGDOCK_DEVICE */
	.zones = sec_jack_zones,
	.num_zones = ARRAY_SIZE(sec_jack_zones),
	.buttons_zones = sec_jack_buttons_zones,
	.num_buttons_zones = ARRAY_SIZE(sec_jack_buttons_zones),
	.press_release_th = 330,
#ifdef SAMSUNG_JACK_SW_WATERPROOF
	.ear_reselector_zone	= 1050,
#endif /* SAMSUNG_JACK_SW_WATERPROOF */
};
#else
static struct pm822_headset_pdata pm822_headset = {
	.headset_flag = 0,
	.mic_set_power = mic_set_power,
	.hook_press_th = 49,
	.vol_up_press_th = 110,
	.vol_down_press_th = 240,
	.mic_det_th = 350,
	.press_release_th = 450,
};
#endif

static struct pm822_vibrator_pdata vibrator_pdata = {
	.min_timeout = 10,
};

#if defined(CONFIG_FUELGAUGE_88PM822)
struct pm822_bat_pdata pm822_bat = {
};

static struct sec_battery_platform_data sec_pm80x_bat = {
	.capacity_max = 1000,
	.capacity_min = 0,
};
#endif

static struct pm822_platform_data pm822_pdata = {
	.regulator		= pm822_regulator_data,
	.power_page_addr	= 0x31,
	.gpadc_page_addr	= 0x32,
	.num_regulators		= ARRAY_SIZE(pm822_regulator_data),
	.rtc			= &pm822_rtc,
	.vibrator		= &vibrator_pdata,
	.dvc			= &pm822_dvc,
#if defined(CONFIG_FUELGAUGE_88PM822)
#if defined(CONFIG_BATTERY_SAMSUNG)
	.fuelgauge_data		= &sec_battery_pdata,
#else
	.bat			= &pm822_bat,
#endif
#endif
	.usb			= &pm822_usb,
#ifdef CONFIG_SAMSUNG_JACK
	.headset 		= &sec_jack_pdata,
#else
	.headset		= &pm822_headset,
#endif
	.plat_config		= pm822_plat_config,
	.irq_flags		= IRQF_ONESHOT,
	.batt_gp_nr		= PM822_GPADC2,
};

static void pm822_dvctable_init(void)
{
	unsigned int *vol_table;
	/* dvc only support 4 lvl voltage*/
	unsigned int vol_tbsize = 4;
	unsigned int index, max_vl, lowest_rate;

	vol_table = kmalloc(vol_tbsize * sizeof(unsigned int), GFP_KERNEL);
	if (!vol_table) {
		pr_err("%s failed to malloc vol table!\n", __func__);
		return ;
	}

	max_vl = pxa988_get_vl_num();
	max_vl = (max_vl > 4) ? 4 : max_vl;
	for (index = 0; index < max_vl; index++)
		vol_table[index] = pxa988_get_vl(index) * 1000;

	lowest_rate = pxa988_get_vl(0);
	while (index < 4)
		vol_table[index++] = lowest_rate * 1000;

	pm822_dvc.vol_val = vol_table;
	pm822_dvc.size = vol_tbsize;
	return ;
}

#elif defined(CONFIG_MFD_88PM800)
static struct pm80x_usb_pdata pm80x_usb = {
	.vbus_gpio = PM800_GPIO2,
	.id_gpadc = PM800_NO_GPADC,
};


static struct pm80x_rtc_pdata pm80x_rtc = {
	.vrtc	= 1,
#ifdef CONFIG_RTC_DRV_SA1100
	.sync	= sync_time_to_soc,
#endif
};

static struct pm80x_dvc_pdata pm80x_dvc = {
	.dvc1		= MFP_PIN_GPIO43,
	.dvc2		= MFP_PIN_GPIO44,
	.gpio_dvc	= 1,
};

static struct pm80x_bat_pdata pm80x_bat = {
};

static int pm800_plat_config(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int data;
	u8 i;
	if (!chip || !pdata || !chip->regmap || !chip->subchip
	    || !chip->subchip->regmap_power) {
		pr_err("%s:chip or pdata is not availiable!\n", __func__);
		return -EINVAL;
	}

	/* RESET_OUTn, RTC_RESET_MODE =0 */
	regmap_write(chip->regmap, PM800_RTC_MISC1, 0xb0);

	/* Set internal digital sleep voltage as 1.2V */
	regmap_write(chip->regmap, PM800_LOW_POWER1, 0x0);
	/* Enable 32Khz-out-3 low jitter XO_LJ = 1 */
	regmap_write(chip->regmap, PM800_LOW_POWER2, 0x20);

	/* Enabele LDO and BUCK clock gating in lpm */
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG3, 0x80);
	/* Enable reference group sleep mode */
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, 0x80);

	/* Enable 32Khz-out-from XO 1, 2, 3 all enabled */
	regmap_write(chip->regmap, PM800_RTC_MISC2, 0x2a);

	/* Enable voltage change in pmic, POWER_HOLD = 1 */
	regmap_write(chip->regmap, PM800_WAKEUP1, 0x80);

	/*
	 * Block wakeup attempts when VSYS rises above
	 * VSYS_UNDER_RISE_TH1, or power off may fail
	 */
	regmap_read(chip->regmap,PM800_RTC_MISC5, &data);
	data |= 0x1;
	regmap_write(chip->regmap,PM800_RTC_MISC5, data);

	/* Enable GPADC sleep mode */
	regmap_write(chip->subchip->regmap_gpadc,
		     PM800_GPADC_MISC_CONFIG2, 0x71);
	/* Enlarge GPADC off slots */
	regmap_write(chip->subchip->regmap_gpadc, 0x08, 0x0f);

	/* Set buck1 sleep mode as 0.8V */
	regmap_write(chip->subchip->regmap_power, PM800_SLEEP_BUCK1, 0x10);
	/* Set buck1 audio mode as 0.8V and trun on LDO(7 or 18)*/
	regmap_write(chip->subchip->regmap_power, PM800_AUDIO_MODE_CONFIG1,
			0x90);
	/* Enable buck sleep mode */
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP1, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP2, 0x2);

	/* Enable LDO sleep mode */
	regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP1, 0xa8);
	regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP2, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP3, 0xab);

	regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP4, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP5, 0xaa);

	/*WIFI may power on in sleep mode, so set sleep voltage 3.3V*/
	regmap_write(chip->subchip->regmap_power, PM800_LDO9, 0xff);

#ifdef GPS_LDO_POWER
	/*set LDO19 for GPS sleep voltage 1.8V*/
	regmap_write(chip->subchip->regmap_power, PM800_LDO19, 0x11);
#endif
	/*
	 * Set buck4 as 2Mhz
	 *  base page:reg 0xd0.7 = 1
	 *            reg 0x50 = 0xc0
	 *            reg 0x52 OSC_DIV_SEL[3] = 1,
	 *            reg 0x53 OSC_2X_EN[3] = 0
	 * buck4 voltage will be set by CP
	 */
	regmap_read(chip->regmap, PM800_RTC_CONTROL, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_RTC_CONTROL, data);

	/*
	 * Base page 0x50 and 0x55 should be set to 0x0C to allow PMIC
	 * Buck clock and digital clocks to be locked to the 32KHz clock,
	 * but make sure USE_XO field (Bas page 0xD0.7) is previously set.
	 *
	 * Once you set 0x0C, if you read back you will read 0x0D, as the
	 * LSB is a Read Only bit representing the ..ock.?fla which will
	 * be set shortly after bit 1 of the same register is set to 0.
	 */
	data = 0x0C;
	regmap_write(chip->regmap, OSC_CNTRL1, data);
	regmap_write(chip->regmap, OSC_CNTRL6, data);

	/* Forcing the clock of the bucks to be active also during sleep */
	regmap_read(chip->regmap, OSC_CNTRL3, &data);
	data |= (1 << 4) | (1 << 7);
	regmap_write(chip->regmap, OSC_CNTRL3, data);

	if (cpu_is_pxa988()) {
		/* Set the 4 regs of buck4 as 1.8v */
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4, 0x54);
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4_1, 0x54);
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4_2, 0x54);
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4_3, 0x54);
	}

	if (cpu_is_pxa986()) {
		/* Set the 4 regs of buck4 as 1.85v */
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4, 0x55);
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4_1, 0x55);
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4_2, 0x55);
		regmap_write(chip->subchip->regmap_power,
			     PM800_BUCK4_3, 0x55);
	}

	/*
	 * Set ldo5 as 3.3V in active and sleep mode
	 * for pxa986
	 */
	if (cpu_is_pxa986()) {
		regmap_read(chip->subchip->regmap_power,
			    PM800_LDO5, &data);
		data |= 0xff;
		regmap_write(chip->subchip->regmap_power,
			     PM800_LDO5, data);
	}



	/* BUCK enable 0x50, BUCK1, 2, 3, 4 */
	regmap_write(chip->subchip->regmap_power, 0x50, 0x0f);
	/* LDO enable 0x51, 0x52, 0x53, LDO1, 3, 5, 7, 8 */
	regmap_write(chip->subchip->regmap_power, 0x51, 0xD4);
	regmap_write(chip->subchip->regmap_power, 0x52, 0x20);
	regmap_write(chip->subchip->regmap_power, 0x53, 0x02);

	/* Dump power-down log */
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG1, &data);
	pr_info("PowerDW Log1 0x%x: 0x%x\n", PM800_POWER_DOWN_LOG1, data);
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG2, &data);
	pr_info("PowerDW Log2 0x%x: 0x%x\n", PM800_POWER_DOWN_LOG2, data);
	/* Clear power-down log */
	regmap_write(chip->regmap, PM800_POWER_DOWN_LOG1, 0xff);
	regmap_write(chip->regmap, PM800_POWER_DOWN_LOG2, 0xff);

	if (dvc_flag) {
		int num = pxa988_get_vl_num();
		/* Write svc level values except level 0 */
		for (i = num - 1; i > 0 ; i--) {
			data = pm800_extern_write(PM80X_POWER_PAGE,
				0x3c + i, (pxa988_get_vl(i) - 600) * 10 / 125);
			if (data < 0) {
				printk(KERN_ERR "SVC table writting failed !\n");
				return -1;
			}
		}
	}
	return 0;
}

static struct pm80x_headset_pdata pm80x_headset = {
	.headset_flag = 0,
	.mic_set_power = mic_set_power,
	.hook_press_th = 61,
	.vol_up_press_th = 121,
	.vol_down_press_th = 340,
	.mic_det_th = 365,
	.press_release_th = 355,
};

static struct pm80x_vibrator_pdata vibrator_pdata = {
	.min_timeout = 10,
};

static struct pm80x_platform_data pm800_info = {
	.headset = &pm80x_headset,
	.power_page_addr = 0x31,	/* POWER */
	.gpadc_page_addr = 0x32,	/* GPADC */
	.test_page_addr = 0x37,		/* TEST */
	.irq_mode = 0,	/* 0: clear IRQ by read */

	.num_regulators = ARRAY_SIZE(pm800_regulator_data),
	.regulator = pm800_regulator_data,
	.vibrator = &vibrator_pdata,
	.rtc = &pm80x_rtc,
	.dvc = &pm80x_dvc,
	.bat = &pm80x_bat,
	.usb = &pm80x_usb,
	.plat_config = pm800_plat_config,
};

static int pm805_plat_config(struct pm80x_chip *chip,
		struct pm80x_platform_data *pdata)
{
	int data;
	if (!chip || !pdata || !chip->regmap) {
		pr_err("%s:chip or pdata is not availiable!\n", __func__);
		return -EINVAL;
	}

	/* power up */
	regmap_read(chip->regmap, 0x01, &data);
	data |= 0x3;
	regmap_write(chip->regmap, 0x01, data);
	msleep(1);
	regmap_write(chip->regmap, 0x30, 0x00);
	return 0;
}

static struct pm80x_platform_data pm805_info = {
	.irq_mode = 0,

	.plat_config = pm805_plat_config,

};


static void pm800_dvctable_init(void)
{
	unsigned int *vol_table;
	/* dvc only support 4 lvl voltage*/
	unsigned int vol_tbsize = 4;
	unsigned int index, max_vl, lowest_rate;

	vol_table = kmalloc(vol_tbsize * sizeof(unsigned int), GFP_KERNEL);
	if (!vol_table) {
		pr_err("%s failed to malloc vol table!\n", __func__);
		return ;
	}

	max_vl = pxa988_get_vl_num();
	max_vl = (max_vl > 4) ? 4 : max_vl;
	for (index = 0; index < max_vl; index++)
		vol_table[index] = pxa988_get_vl(index) * 1000;

	lowest_rate = pxa988_get_vl(0);
	while (index < 4)
		vol_table[index++] = lowest_rate * 1000;

	pm80x_dvc.vol_val = vol_table;
	pm80x_dvc.size	= vol_tbsize;
	return ;
}
#endif
/* compass */

#if defined(CONFIG_SENSORS_BMM050)
static struct bosch_sensor_specific bss_bmm = {
	.name = "bmc150",
	.place = (int)0x05,
};
static struct i2c_board_info i2c_bmm050={
	I2C_BOARD_INFO("bmm050", 0x12),
	.platform_data = &bss_bmm,
};
#endif //CONFIG_SENSORS_BMM050

#if defined(CONFIG_INPUT_MPU6500)
static void mpu_power_on(bool on)
{
	pr_info("%s : %s, but no action\n", __func__, (on)?"on":"off");
}

static struct mpu6k_input_platform_data mpu6500_pdata = {
	.power_on = mpu_power_on,
	.orientation = {0, 1, 0,
			-1, 0, 0,
			0, 0, 1},
	.acc_cal_path = "/efs/calibration_data",
	.gyro_cal_path = "/efs/gyro_cal_data",
	.gpio_gyro_irq = mfp_to_gpio(GPIO010_GPIO_10),
};

static struct mpu6k_input_platform_data mpu6500_pdata_rev01 = {
	.power_on = mpu_power_on,
	.orientation = {1, 0, 0,
			0, 1, 0,
			0, 0, 1},
	.acc_cal_path = "/efs/calibration_data",
	.gyro_cal_path = "/efs/gyro_cal_data",
	.gpio_gyro_irq = mfp_to_gpio(GPIO010_GPIO_10),
};

static struct i2c_board_info i2c_mpu6500[]={
	{
	I2C_BOARD_INFO("mpu6500_input", 0x62),
	.platform_data = &mpu6500_pdata,
	//.irq = MMP_GPIO_TO_IRQ(mfp_to_gpio(GPIO010_GPIO_10)),
	},
};

static struct pxa_i2c_board_gpio i2c_mpu6500_gpio[] = {
	{
		.type = "mpu6500_input",
		.gpio = mfp_to_gpio(GPIO010_GPIO_10),
	}
};
#endif //CONFIG_INPUT_MPU6500

#if defined(CONFIG_SENSORS_HSCDTD008A)
static struct i2c_board_info i2c_hscd008a={
	I2C_BOARD_INFO("hscd_i2c", 0x0c),
};
#endif //CONFIG_INPUT_MPU6500

#if defined(CONFIG_SENSORS_BMA2X2)
static struct i2c_board_info i2c_bma2x2={
	I2C_BOARD_INFO("bma2x2", 0x10),
#ifdef CONFIG_INPUT_BMA2x2_ACC_ALERT_INT
//	.platform_data = &bma2x2_pdata,
#else
	.platform_data = &bss_bmm,
#endif
};
#endif //CONFIG_SENSORS_BMA2X2

#if defined(CONFIG_SENSORS_GP2A002S)

int gp2a_device_power(bool on){
	static struct regulator *prox_3_0;
	static struct regulator *proxLed;
	static bool bFirst=1;
	static bool bStatus=0;

	if(bFirst)
	{
		//LDO Power On=============
		if (!prox_3_0) {
			prox_3_0 = regulator_get(NULL, "v_proxy_3v");
			if (IS_ERR(prox_3_0)) {
				pr_err("%s regulator get error!\n", __func__);
				prox_3_0 = NULL;
				return -1;
			}
		}
		regulator_set_voltage(prox_3_0, 3000000, 3000000);

		if (!proxLed) {
			proxLed = regulator_get(NULL, "v_proxy_led_3v3");
			if (IS_ERR(proxLed)) {
				pr_err("%s regulator get error!\n", __func__);
				proxLed = NULL;
				return -1;
			}
		}
		regulator_set_voltage(proxLed, 3300000, 3300000);
		bFirst=0;
	}

	if(on == bStatus)
		return 0;

	if(on){
		regulator_enable(prox_3_0);
		msleep(2);

		regulator_enable(proxLed);
		msleep(2);

		bStatus = 1;
	}
	else{
		regulator_disable(proxLed);
		regulator_disable(prox_3_0);
		bStatus = 0;
	}

	return 0;
}

static struct gp2a_platform_data gp2a_dev_data={
	.power = gp2a_device_power,
	.p_out = mfp_to_gpio(GPIO092_GPIO_92),
};

static struct i2c_board_info i2c_gp2a[]={
	{
		I2C_BOARD_INFO("gp2a", 0x44),
		.platform_data = &gp2a_dev_data,
	},
};

static struct pxa_i2c_board_gpio i2c_gp2a_gpio[] = {
	{
		.type = "gp2a",
		.gpio = mfp_to_gpio(GPIO092_GPIO_92),
	},
};
#endif

#if defined(CONFIG_SENSORS_GP2A_PROXIMITY)
static int gp2a_device_power(bool on)
{
	pr_info("%s : %s\n", __func__, (on)?"on":"off");
	return 0;
}

static struct gp2a_platform_data gp2a_dev_data={
	.power = gp2a_device_power,
	.p_out = mfp_to_gpio(GPIO092_GPIO_92),
};

static struct i2c_board_info i2c_gp2a[]={
	{
		I2C_BOARD_INFO("gp2a", 0x44),
		.platform_data = &gp2a_dev_data,
	},
};
#endif

#if defined(CONFIG_SENSORS_GP2A030)
enum {
	GP2AP020 = 0,
	GP2AP030,
};

static void gp2ap030_power_onoff(bool on)
{
	static struct regulator *prox_3_0;
		static bool bpFirst=1;
		static bool bpStatus=0;

		if(bpFirst)
		{
			//LDO Power On=============
			if (!prox_3_0) {
				prox_3_0 = regulator_get(NULL, "v_proxy_3v");
				if (IS_ERR(prox_3_0)) {
					pr_err("%s regulator get error!\n", __func__);
					prox_3_0 = NULL;
					return -1;
				}
			}
			regulator_set_voltage(prox_3_0, 3000000, 3000000);

			bpFirst=0;
		}

		if(on == bpStatus)
			return 0;

		if(on){
			regulator_enable(prox_3_0);
			msleep(2);


			bpStatus = 1;
		}
		else{
			regulator_disable(prox_3_0);
			bpStatus = 0;
		}

		return 0;

}

static void gp2ap030_led_onoff(bool on)
{
	static struct regulator *proxLed;
	static bool blFirst=1;
	static bool blStatus=0;

	if(blFirst)
	{
		//LDO Power On=============
		if (!proxLed) {
			proxLed = regulator_get(NULL, "v_proxy_led_3v3");
			if (IS_ERR(proxLed)) {
				pr_err("%s regulator get error!\n", __func__);
				proxLed = NULL;
				return -1;
			}
		}
		regulator_set_voltage(proxLed, 3300000, 3300000);
		blFirst=0;
	}

	if(on == blStatus)
		return 0;

	if(on){
		regulator_enable(proxLed);
		msleep(2);

		blStatus = 1;
	}
	else{
		regulator_disable(proxLed);
		blStatus = 0;
	}

	return 0;
}


static struct gp2ap030_pdata gp2ap030_pdata = {
	.p_out = mfp_to_gpio(GPIO092_GPIO_92),
    	.power_on = gp2ap030_power_onoff,
    	.led_on	= gp2ap030_led_onoff,
	.version = GP2AP030,
	.prox_cal_path = "/efs/prox_cal"
};

static struct i2c_board_info i2c_gp2a={
	I2C_BOARD_INFO("gp2a030", 0x72>>1),
	.platform_data = &gp2ap030_pdata,
};
#endif

#if defined(CONFIG_SENSORS_BMA2X2) \
 || defined(CONFIG_SENSORS_BMM050) \
 || defined(CONFIG_SENSORS_GP2A002S) \
 || defined(CONFIG_SENSORS_GP2A030) \
 || defined(CONFIG_INPUT_MPU6500) \
 || defined(CONFIG_SENSORS_GP2A_PROXIMITY)
static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= mfp_to_gpio(GPIO036_GPIO_36),
	.scl_pin		= mfp_to_gpio(GPIO035_GPIO_35),
	.udelay			= 2,
	.timeout = 100,
};
static struct platform_device i2c_gpio_device = {
	.name	= "i2c-gpio",
	.id	= 5,
	.dev	= {
		.platform_data = &i2c_gpio_data,
	},
};
#endif

#if defined(CONFIG_NFC_PN547)
static struct i2c_gpio_platform_data i2c_gpio_data_nfc = {
	.sda_pin		= mfp_to_gpio(GPIO034_GPIO_34),
	.scl_pin		= mfp_to_gpio(GPIO033_GPIO_33),
	.udelay			= 2,
	.timeout = 100,
};
static struct platform_device i2c_gpio_device_nfc = {
	.name	= "i2c-gpio",
	.id	= 9,
	.dev	= {
		.platform_data	= &i2c_gpio_data_nfc,
	},
};
#endif

#if defined(CONFIG_SENSORS_HSCDTD008A)
static struct platform_device alps_pdata = {
	.name = "alps-input",
	.id = -1,
};
#endif

static struct pxa_i2c_board_gpio emeidkb_pwr_i2c_gpio[] = {
	{
		.type = "88PM805",
		.gpio = mfp_to_gpio(GPIO124_GPIO_124),
	},
};

static struct i2c_board_info emeidkb_pwr_i2c_info[] = {
#if defined(CONFIG_MFD_88PM822)
	{
		.type		= "88PM822",
		.addr		= 0x30,
		.irq		= IRQ_PXA988_PMIC,
		.platform_data	= &pm822_pdata,
	},
	{
		.type = "88PM805",
		.addr = 0x38,
		.platform_data = &pm805_info,
	},
#elif defined(CONFIG_MFD_88PM800)
	{
		.type		= "88PM800",
		.addr		= 0x30,
		.irq		= IRQ_PXA988_PMIC,
		.platform_data	= &pm800_info,
	},
	{
		.type = "88PM805",
		.addr = 0x38,
		.platform_data = &pm805_info,
	},
#elif defined(CONFIG_MFD_D2199)
	{
		// for D2199 PMIC driver
		.type		= "d2199",
		.addr		= D2199_PMIC_I2C_ADDR,
		.platform_data = &d2199_pdata,
		.irq = IRQ_PXA988_PMIC,
	},
#endif
};

static struct i2c_board_info emeidkb_pwr_i2c_info_rev00[] = {
#if defined(CONFIG_MFD_88PM822)
	{
		.type		= "88PM822",
		.addr		= 0x30,
		.irq		= IRQ_PXA988_PMIC,
		.platform_data	= &pm822_pdata,
	},
	{
		.type = "88PM805",
		.addr = 0x38,
		.platform_data = &pm805_info,
	},
#elif defined(CONFIG_MFD_88PM800)
	{
		.type		= "88PM800",
		.addr		= 0x30,
		.irq		= IRQ_PXA988_PMIC,
		.platform_data	= &pm800_info,
	},
	{
		.type = "88PM805",
		.addr = 0x38,
		.platform_data = &pm805_info,
	},
#elif defined(CONFIG_MFD_D2199)
	{
		// for D2199 PMIC driver
		.type		= "d2199",
		.addr		= D2199_PMIC_I2C_ADDR,
		.platform_data = &d2199_pdata_rev00,
		.irq = IRQ_PXA988_PMIC,
	},
#endif
};

#if defined(CONFIG_SM5502_MUIC)
static struct i2c_gpio_platform_data i2c_sm5502_bus_data = {
	.sda_pin = mfp_to_gpio(GPIO050_GPIO_50),
	.scl_pin = mfp_to_gpio(GPIO049_GPIO_49),
	.udelay  = 3,
	.timeout = 100,
};

static struct platform_device i2c_sm5502_bus_device = {
	.name		= "i2c-gpio",
	.id		= 7, /* pxa92x-i2c are bus 0, 1 so start at 2 */
	.dev = {
		.platform_data = &i2c_sm5502_bus_data,
	}
};
static struct sm5502_platform_data sm5502_info = {
	.charger_cb = sec_charger_cb,
};
static struct i2c_board_info __initdata sm5502_i2c_devices[] = {
	{
		I2C_BOARD_INFO("sm5502", 0x25),
		.platform_data	= &sm5502_info,
	},
};
static struct pxa_i2c_board_gpio sm5502_i2c_gpio[] = {
	{
		.type = "sm5502",
		.gpio = mfp_to_gpio(GPIO093_GPIO_93),
	},
};
#endif


#if defined(CONFIG_BQ24157_CHARGER)
static struct i2c_gpio_platform_data i2c_bq24157_bus_data = {
	.sda_pin = mfp_to_gpio(GPIO017_GPIO_17),
	.scl_pin = mfp_to_gpio(GPIO016_GPIO_16),
	.udelay  = 3,
	.timeout = 100,
};

static struct platform_device i2c_bq24157_bus_device = {
	.name		= "i2c-gpio",
	.id		= 8, /* pxa92x-i2c are bus 0, 1 so start at 2 */
	.dev = {
		.platform_data = &i2c_bq24157_bus_data,
	}
};

static struct bq24157_platform_data  bq24157_charger_info = {
	.cd = mfp_to_gpio(GPIO098_GPIO_98),
};

static struct i2c_board_info __initdata bq24157_i2c_devices[] = {
	{
		I2C_BOARD_INFO("bq24157_6A", 0x6A),
		.platform_data	= &bq24157_charger_info,
	},
};

static struct pxa_i2c_board_gpio bq24157_i2c_gpio[] = {
	{
		.type = "bq24157_6A",
		.gpio = mfp_to_gpio(GPIO008_GPIO_8),
	},
};
#endif

#if defined(CONFIG_I2C_RT9455)
#define RT9455_INT_PIN (81)
static void rt9455_general_callback(int event)
{
	pr_info("%s: general_callback event value = %02x\n", __func__, event);
}

static void rt9455_charger_callback(int event)
{
	pr_info("%s: charger_callback event value = %02x\n", __func__, event);
}

static void rt9455_boost_callback(int event)
{
	pr_info("%s: boost_callback event value = %02x\n", __func__, event);
}

static void rt9455_reset_callback(int event)
{
	RT_DBG("%s: reset_callback event value = %02x\n", __func__, event);
	// Eventlist
	// 0x80 RT9455_EVENT_BEFORE_RST
	// 0x40 RT9455_EVENT_AFTER_RST
}

static struct rt9455_platform_data rt9455_platform_data = {
	.intr_gpio = RT9455_INT_PIN,
	// Default we dont't mask any thing
	// If you change the default value, please also set write_irq_mask flag
	// to 1, in the driver, it will write the irq_mask to chip register.
	.write_irq_mask = 1,
	.irq_mask = {
		.mask1 = {
			.bitfield = {
				.BATABM = 0,
				.VINOVPIM = 0,
				.TSDM = 0,
			},
		},
		.mask2 = {
			.bitfield = {
				.CHDPMIM = 1,
				.CHTREGIM = 0,
				.CH32MIM = 0,
				.CHRCHGIM = 0,
				.CHTERMIM = 0,
				.CHBATOVIM = 0,
				.CHRVPIM = 0,
			},
		},
		.mask3 = {
			.bitfield = {
				.BST32SIM = 0,
				.BSTLOWVIM = 0,
				.BSTOLIM = 0,
				.BSTVIMOVIM = 0,
			},
		},
	},
	// Below the default control register value
	// If you change the default value, please also set write_init_ctrlval flag
	// to 1, in driver it will write the init_ctrlval to chip register.
	.write_init_ctrlval = 1,
	.init_ctrlval = {
		.ctrl2 = {
			.bitfield = {
				.OPA_MODE = OPA_CHARGER_MODE,
				.HZ_BIT = 0,
				.IAICR_INT = IAICR_INTERNAL,
				.TE = 1,
				.HIGH_OCP = OCP_2P5A,
				.SEL_SWFREQ = SEL_FREQ_1P5MHz,
				.IAICR = IAICR_LIMIT_1A,
			},
		},
		.ctrl3 = {
			.bitfield = {
				.OTG_EN = 0,
				.OTG_PL = OTGPL_ACTIVE_HIGH,
				.VOREG = 0x2A,
			},
		},
		.ctrl5 = {
			.bitfield = {
				.IEOC = IEOC_20P,
				.IPREC = IPREC_60MA,
				.VDPM = VDPM_4P5V,
				.TMR_EN = 0,
			},
		},
		.ctrl6 = {
			.bitfield = {
				.VPREC = VPREC_2P4V,
				.ICHRG = ICHRG_20MV,
			},
		},
		.ctrl7 = {
			.bitfield = {
				.VMREG = 0x0f,
				.CHG_EN = 1,
				.BATD_EN = 0,
			},
		},
	},
	.callbacks = {
		.boost_callback = rt9455_boost_callback,
		.charger_callback = rt9455_charger_callback,
		.general_callback = rt9455_general_callback,
		.reset_callback = rt9455_reset_callback,
	},
};

static struct i2c_gpio_platform_data i2c_rt9455_bus_data = {
	.sda_pin = mfp_to_gpio(GPIO017_GPIO_17),
	.scl_pin = mfp_to_gpio(GPIO016_GPIO_16),
	.udelay  = 3,
	.timeout = 100,
};

static struct platform_device i2c_rt9455_bus_device = {
	.name		= "i2c-gpio",
	.id		= 8, /* pxa92x-i2c are bus 0, 1 so start at 2 */
	.dev = {
		.platform_data = &i2c_rt9455_bus_data,
	}
};

static struct i2c_board_info __initdata rt9455_i2c_devices[] = {
	{
		I2C_BOARD_INFO(RT9455_DEVICE_NAME, 0x22),
		.platform_data	= &rt9455_platform_data,
	},
};

static struct pxa_i2c_board_gpio rt9455_i2c_gpio[] = {
	{
		.type = RT9455_DEVICE_NAME,
		.gpio = mfp_to_gpio(GPIO008_GPIO_8),
	},
};

#endif /* CONFIG_I2C_RT9455 */


#if defined(CONFIG_RT8969)||defined(CONFIG_RT8973)
static void usb_attach(uint8_t attached)
{
    if(attached)
    {
#if defined(CONFIG_BATTERY_SAMSUNG)
		pxa_usb_notify(PXA_USB_DEV_OTG, EVENT_VBUS, 0);
#endif
#if defined(CONFIG_SPA)
        rt8973_spa_usb_attached();
#endif
    }
    else
    {
#if defined(CONFIG_BATTERY_SAMSUNG)
		pxa_usb_notify(PXA_USB_DEV_OTG, EVENT_VBUS, 0);
#endif
#if defined(CONFIG_SPA)
        rt8973_spa_usb_detached();
#endif
    }
    //printk(attached?"USB attached\n":"USB detached\n");
}
static void uart_attach(uint8_t attached)
{


    printk(attached?"UART attached\n":"UART detached\n");
}

static void charger_attach(uint8_t attached)
{
    if(attached)
    {
#if defined(CONFIG_SPA)
        rt8973_spa_ta_attached();
#endif
    }
    else
    {
#if defined(CONFIG_SPA)
        rt8973_spa_ta_detached();
#endif
    }
//    printk(attached?"Charger attached\n":"Charger detached\n");
}

static void jig_attach(uint8_t attached,uint8_t factory_mode)
{
	int usb_on = 0;
    switch(factory_mode)
    {
        case RTMUSC_FM_BOOT_OFF_UART:
    	    printk("JIG BOOT OFF UART\n");
        break;
        case RTMUSC_FM_BOOT_OFF_USB:
			usb_on = 1;
            printk("JIG BOOT OFF USB\n");
        break;
        case RTMUSC_FM_BOOT_ON_UART:
            printk("JIG BOOT ON UART\n");
        break;
        case RTMUSC_FM_BOOT_ON_USB:
			usb_on = 1;
            printk("JIG BOOT ON USB\n");
        break;
        default:
        ;
    }
    //printk(attached?"Jig attached\n":"Jig detached\n");
    if(attached)
    {
#if defined(CONFIG_SPA)
        if(usb_on)
            rt8973_spa_usb_attached();

        rt8973_spa_jig_attached();
#endif
    }
    else
    {
#if defined(CONFIG_SPA)
        if(usb_on)
            rt8973_spa_usb_detached();

        rt8973_spa_jig_detached();
#endif
    }

}

static void over_temperature(uint8_t detected)
{
    printk("over temperature detected = %d!\n",detected);
}
#ifdef CONFIG_RT8969
static void charging_complete(void)
{
    printk("charging complete\n");
}
#endif
static void over_voltage(uint8_t detected)
{

    if(detected)
    {
#if defined(CONFIG_SPA)
        rt8973_spa_ovp_detected();
#endif
    }
    else
    {
#if defined(CONFIG_SPA)
        rt8973_spa_ovp_released();
#endif
    }

    //printk("over voltage = %d\n",(int32_t)detected);
}

static void otg_attach(uint8_t attached)
{
   printk(attached?"OTG attached\n":"OTG detached\n");
}

static void ret8973_reset(void)
{
    printk("RT8973 reset\n");
}
extern void sec_charger_cb(u8 cable_type);
static struct rtmus_platform_data __initdata rtmus_pdata = {
    .usb_callback = &usb_attach,
    .uart_callback = &uart_attach,
    .charger_callback = &charger_attach,
    .jig_callback = &jig_attach,
    .over_temperature_callback = &over_temperature,
#ifdef CONFIG_RT8969
    .charging_complete_callback = &charging_complete,
#else
    .charging_complete_callback = NULL,
#endif
    .over_voltage_callback = &over_voltage,
    .otg_callback = &otg_attach,
    .reset_callback = &ret8973_reset,
#ifdef CONFIG_BATTERY_SAMSUNG
	.sec_charger_callback = &sec_charger_cb,
#endif
};
#endif

#if defined(CONFIG_RT8969)||defined(CONFIG_RT8973)
static struct i2c_gpio_platform_data i2c_rt8973_bus_data = {
        .sda_pin = mfp_to_gpio(GPIO050_GPIO_50),
        .scl_pin = mfp_to_gpio(GPIO049_GPIO_49),
        .udelay  = 3,
        .timeout = 100,
};


static struct platform_device i2c_rt8973_bus_device = {
        .name           = "i2c-gpio",
        .id             = 7, /* pxa92x-i2c are bus 0, 1 so start at 2 */
        .dev = {
                .platform_data = &i2c_rt8973_bus_data,
        }
};


static struct i2c_board_info __initdata rt8973_i2c_devices[] = {
        {
                I2C_BOARD_INFO("rt8973", 0x28>>1),
                .platform_data  = &rtmus_pdata,
                .irq = MMP_GPIO_TO_IRQ(mfp_to_gpio(GPIO093_GPIO_93)),
        },
};
#endif




#if defined(CONFIG_STC3115_FUELGAUGE)
static struct stc311x_platform_data stc3115_platform_data = {
        .battery_online = NULL,
        .charger_online = NULL,		// used in stc311x_get_status()
        .charger_enable = NULL,		// used in stc311x_get_status()
        .power_supply_register = NULL,
        .power_supply_unregister = NULL,
		.Vmode= 0,       /*REG_MODE, BIT_VMODE 1=Voltage mode, 0=mixed mode */
		.Alm_SOC = 10,      /* SOC alm level %*/
		.Alm_Vbat = 3600,   /* Vbat alm level mV*/
  		.CC_cnf = 400,      /* nominal CC_cnf, coming from battery characterisation*/
  		.VM_cnf = 405,      /* nominal VM cnf , coming from battery characterisation*/
  		.Cnom = 2000,       /* nominal capacity in mAh, coming from battery characterisation*/
		.Rsense = 10,       /* sense resistor mOhms*/
  		.RelaxCurrent = 100, /* current for relaxation in mA (< C/20) */
		.Adaptive = 1,     /* 1=Adaptive mode enabled, 0=Adaptive mode disabled */

		.CapDerating[6] = 190,   /* capacity derating in 0.1%, for temp = -20C */
  		.CapDerating[5] = 70,   /* capacity derating in 0.1%, for temp = -10C */
		.CapDerating[4] = 30,   /* capacity derating in 0.1%, for temp = 0C */
		.CapDerating[3] = 0,   /* capacity derating in 0.1%, for temp = 10C */
		.CapDerating[2] = 0,   /* capacity derating in 0.1%, for temp = 25C */
		.CapDerating[1] = -20,   /* capacity derating in 0.1%, for temp = 40C */
		.CapDerating[0] = -40,   /* capacity derating in 0.1%, for temp = 60C */

  		.OCVOffset[15] = -123,    /* OCV curve adjustment */
		.OCVOffset[14] = -30,   /* OCV curve adjustment */
		.OCVOffset[13] = -12,    /* OCV curve adjustment */
		.OCVOffset[12] = -27,    /* OCV curve adjustment */
		.OCVOffset[11] = 0,    /* OCV curve adjustment */
		.OCVOffset[10] = -27,    /* OCV curve adjustment */
		.OCVOffset[9] = 4,     /* OCV curve adjustment */
		.OCVOffset[8] = 1,      /* OCV curve adjustment */
		.OCVOffset[7] = 7,      /* OCV curve adjustment */
		.OCVOffset[6] = 9,    /* OCV curve adjustment */
		.OCVOffset[5] = 9,    /* OCV curve adjustment */
		.OCVOffset[4] = 16,     /* OCV curve adjustment */
		.OCVOffset[3] = 33,    /* OCV curve adjustment */
		.OCVOffset[2] = 34,     /* OCV curve adjustment */
		.OCVOffset[1] = 46,    /* OCV curve adjustment */
		.OCVOffset[0] = -3,     /* OCV curve adjustment */

		 .OCVOffset2[15] = -109,    /* OCV curve adjustment */
		.OCVOffset2[14] = -86,   /* OCV curve adjustment */
		.OCVOffset2[13] = -59,    /* OCV curve adjustment */
		.OCVOffset2[12] = -59,    /* OCV curve adjustment */
		.OCVOffset2[11] = -29,    /* OCV curve adjustment */
		.OCVOffset2[10] = -46,    /* OCV curve adjustment */
		.OCVOffset2[9] = -8,     /* OCV curve adjustment */
		.OCVOffset2[8] = 0,      /* OCV curve adjustment */
		.OCVOffset2[7] = -2,      /* OCV curve adjustment */
		.OCVOffset2[6] = -6,    /* OCV curve adjustment */
		.OCVOffset2[5] = -7,    /* OCV curve adjustment */
		.OCVOffset2[4] = -9,     /* OCV curve adjustment */
		.OCVOffset2[3] = 19,    /* OCV curve adjustment */
		.OCVOffset2[2] = 44,     /* OCV curve adjustment */
		.OCVOffset2[1] = 81,    /* OCV curve adjustment */
		.OCVOffset2[0] = 0,     /* OCV curve adjustment */
		/*if the application temperature data is preferred than the STC3115 temperature*/
		.ExternalTemperature = NULL, /*External temperature fonction, return C*/
		.ForceExternalTemperature = 0, /* 1=External temperature, 0=STC3115 temperature */
};
static struct i2c_gpio_platform_data i2c_stc3115_bus_data = {
      .sda_pin = mfp_to_gpio(MFP_PIN_GPIO88),
	  .scl_pin = mfp_to_gpio(MFP_PIN_GPIO87),
	  .udelay  = 3,  //// brian :3
	  .timeout = 100,
};
static struct platform_device i2c_stc3115_bus_device = {
	.name	= "i2c-gpio",
	.id		= 6,
	.dev		= {
		.platform_data = &i2c_stc3115_bus_data,
	}
};
static struct i2c_board_info __initdata stc3115_i2c_devices[] = {
	{
		.type		= "stc3115_fuelgauge",
		.addr		= 0x70,
		.platform_data = &stc3115_platform_data,
	},
};
static struct pxa_i2c_board_gpio stc3115_i2c_gpio[] = {
	{
		.type = "stc3115_fuelgauge",
		.gpio = mfp_to_gpio(GPIO095_GPIO_95),
	},
};
#endif

#if defined(CONFIG_SPA)
static struct spa_platform_data spa_info = {
	.use_fuelgauge = 1,
	.battery_capacity = 1650,
	.VF_low	= 50,
	.VF_high = 600,
};
static struct platform_device Sec_BattMonitor = {
	.name		= "Sec_BattMonitor",
	.id		= -1,
	.dev		= {
		.platform_data = &spa_info,
	},
};
#endif
static struct i2c_board_info emeidkb_i2c_info[] = {
};

#ifdef CONFIG_PN547_NFC
static struct pn544_i2c_platform_data pn547_pdata = {
//	.conf_gpio = pn544_conf_gpio,
	.irq_gpio = mfp_to_gpio(GPIO091_GPIO_NFC_IRQ),
	.ven_gpio = mfp_to_gpio(GPIO126_GPIO_NFC_EN),
	.firm_gpio = mfp_to_gpio(GPIO127_GPIO_NFC_FIRMWARE),
};
#endif
static struct i2c_board_info emeidkb_i2c2_info[] = {
#ifdef CONFIG_PN547_NFC
	{
		I2C_BOARD_INFO("pn547", 0x2b),
		.irq = MMP_GPIO_TO_IRQ(mfp_to_gpio(GPIO091_GPIO_NFC_IRQ)),
		.platform_data = &pn547_pdata,
	},
#endif
};

#if defined(CONFIG_NFC_PN547)
static struct pn547_i2c_platform_data pn547_pdata = {
	.irq_gpio = mfp_to_gpio(GPIO091_GPIO_91),
	.ven_gpio = mfp_to_gpio(GPIO126_GPIO_126),
	.firm_gpio = mfp_to_gpio(GPIO127_GPIO_127),
};

static struct i2c_board_info i2c_pn547={
	I2C_BOARD_INFO("pn547", 0x2b),
	.irq = MMP_GPIO_TO_IRQ(mfp_to_gpio(GPIO091_GPIO_NFC_IRQ)),
	.platform_data = &pn547_pdata,
};
#endif

/*
 * workaround for reset i2c bus
 * i2c0: GPIO53 -> SCL, GPIO54 -> SDA,
 * i2c1: GPIO87 -> SCL, GPIO88 -> SDA,
 */
static void i2c_pxa_bus_reset(int i2c_adap_id)
{
	unsigned long mfp_pin[2];
	int ccnt;
	unsigned long scl, sda;

	unsigned long i2c0_mfps[] = {
		GPIO053_CAM_SCL,		/* SCL */
		GPIO054_CAM_SDA,		/* SDA */
	};

	unsigned long i2c1_mfps[] = {
		GPIO087_GPIO_87,		/* SCL */
		GPIO088_GPIO_88,		/* SDA */
	};
	if (i2c_adap_id == 0) {
		scl = MFP_PIN_GPIO53;
		sda = MFP_PIN_GPIO54;
	} else if (i2c_adap_id == 1) {
		scl = MFP_PIN_GPIO87;
		sda = MFP_PIN_GPIO88;
	} else {
		pr_err("i2c bus num error!\n");
		return;
	}
	if (gpio_request(scl, "SCL")) {
		pr_err("Failed to request GPIO for SCL pin!\n");
		goto out0;
	}
	if (gpio_request(sda, "SDA")) {
		pr_err("Failed to request GPIO for SDA pin!\n");
		goto out_sda0;
	}
	/* set mfp pins to gpios */
	mfp_pin[0] = mfp_read(scl);
	mfp_pin[1] = mfp_read(sda);
	if (i2c_adap_id == 0)
		mfp_config(ARRAY_AND_SIZE(i2c0_mfps));
	if (i2c_adap_id == 1)
		mfp_config(ARRAY_AND_SIZE(i2c1_mfps));

	gpio_direction_input(sda);
	for (ccnt = 20; ccnt; ccnt--) {
		gpio_direction_output(scl, ccnt & 0x01);
		udelay(4);
	}
	gpio_direction_output(scl, 0);
	udelay(4);
	gpio_direction_output(sda, 0);
	udelay(4);
	/* stop signal */
	gpio_direction_output(scl, 1);
	udelay(4);
	gpio_direction_output(sda, 1);
	udelay(4);
	if (i2c_adap_id == 0) {
		mfp_write(MFP_PIN_GPIO53, mfp_pin[0]);
		mfp_write(MFP_PIN_GPIO54, mfp_pin[1]);
	}
	if (i2c_adap_id == 1) {
		mfp_write(MFP_PIN_GPIO87, mfp_pin[0]);
		mfp_write(MFP_PIN_GPIO88, mfp_pin[1]);
	}
	gpio_free(sda);
out_sda0:
	gpio_free(scl);
out0:
	return;
}
// for TSP i2c operation (switch gpio or hw i2c func)
void i2c1_pin_changed(int gpio)
{
	unsigned long i2c1_g_mfps[] = {
		GPIO087_GPIO_87,		/* SCL */
		GPIO088_GPIO_88,		/* SDA */
	};

	unsigned long i2c1_hw_mfps[] = {
		GPIO087_CI2C_SCL_2,		/* SCL */
		GPIO088_CI2C_SDA_2,		/* SDA */
	};

	if(gpio)
		mfp_config(ARRAY_AND_SIZE(i2c1_g_mfps));
	else
		mfp_config(ARRAY_AND_SIZE(i2c1_hw_mfps));
	return;
}

static struct i2c_pxa_platform_data emeidkb_ci2c_pdata = {
	.fast_mode		 = 1,
	/* ilcr:fs mode b17~9=0x23,about 390K, standard mode b8~0=0x9f,97K */
	.ilcr		= 0x082C469F,
	/* iwcr:b5~0=b01010 recommended value according to spec*/
	.iwcr		= 0x0000142A,
	.i2c_bus_reset		= i2c_pxa_bus_reset,
};

static struct i2c_pxa_platform_data emeidkb_ci2c2_pdata = {
	.fast_mode		 = 1,
	/* ilcr:fs mode b17~9=0x23,about 390K, standard mode b8~0=0x9f,97K */
	.ilcr		= 0x082C469F,
	/* iwcr:b5~0=b01010 recommended value according to spec*/
	.iwcr		= 0x0000142A,
	.i2c_bus_reset		= i2c_pxa_bus_reset,
};

static struct i2c_pxa_platform_data emeidkb_pwr_i2c_pdata = {
	.fast_mode		 = 1,
	/* ilcr:fs mode b17~9=0x23,about 390K, standard mode b8~0=0x9f,97K */
	.ilcr		= 0x082C469F,
	/* iwcr:b5~0=b01010 recommended value according to spec*/
	.iwcr		= 0x0000142A,
	.hardware_lock		= pxa988_ripc_lock,
	.hardware_unlock	= pxa988_ripc_unlock,
	.hardware_trylock	= pxa988_ripc_trylock,
};

static DEFINE_SPINLOCK(bl_lock);

#if defined(CONFIG_BACKLIGHT_KTD253)
static struct platform_device ktd253_device  = {
	.name	= "panel",
};
#endif

#ifdef CONFIG_USB_MV_UDC
static char *pxa988_usb_clock_name[] = {
	[0] = "UDCCLK",
};

static struct mv_usb_platform_data emeidkb_usb_pdata = {
	.clknum		= 1,
	.clkname	= pxa988_usb_clock_name,
	.id		= PXA_USB_DEV_OTG,
	.extern_attr	= MV_USB_HAS_VBUS_DETECTION,
	.mode		= MV_USB_MODE_DEVICE,
	.phy_init	= pxa_usb_phy_init,
	.phy_deinit	= pxa_usb_phy_deinit,
};
#endif /* CONFIG_USB_MV_UDC */

#ifdef CONFIG_MMC_SDHCI_PXAV3
#define MFP_WIB_PDn		(GPIO051_GPIO_51 | MFP_PULL_FLOAT)
#define MFP_WIB_RESETn		(GPIO095_GPIO_95 | MFP_PULL_FLOAT)

static void emeidkb_sdcard_signal(unsigned int set)
{
	int vol = set;

	pxa988_aib_mmc1_iodomain(vol);
}

static struct wakeup_source wlan_dat1_wakeup;
static struct work_struct wlan_wk;

static void wlan_edge_wakeup(struct work_struct *work)
{
	/*
	 * it is handled in SDIO driver instead, so code not need now
	 * but temparally keep the code here,it may be used for debug
	 */
#if 0
	unsigned int sec = 3;

	/*
	 * Why use a workqueue to call this function?
	 *
	 * As now dat1_edge_wakeup is called just after CPU exist LPM,
	 * and if __pm_wakeup_event is called before syscore_resume,
	 * WARN_ON(timekeeping_suspended) will happen in ktime_get in
	 * /kernel/time/timekeeping.c
	 *
	 * So we create a workqueue to fix this issue
	 */
	__pm_wakeup_event(&wlan_dat1_wakeup, 1000 * sec);
	printk(KERN_INFO "SDIO wake up+++\n");
#endif
}

static void dat1_edge_wakeup(int irq, void *pRsv)
{
	queue_work(system_wq, &wlan_wk);
}

static struct gpio_edge_desc gpio_edge_sdio_dat1 = {
	.mfp = MFP_PIN_GPIO39,
	.gpio = mfp_to_gpio(MFP_PIN_GPIO39),
	/*
	 * SDIO Spec difine falling as SDIO interrupt, but set BOTH edge
	 * should be more safe to wake up.
	 */
	.type = MFP_LPM_EDGE_BOTH,
	.handler = dat1_edge_wakeup,
};

static void wlan_wakeup_init(void)
{
	 INIT_WORK(&wlan_wk, wlan_edge_wakeup);
	 wakeup_source_init(&wlan_dat1_wakeup,
		"wifi_hs_wakeups");
}

#ifdef CONFIG_SD8XXX_RFKILL
static void emeidkb_8787_set_power(unsigned int on)
{
	static struct regulator *wib_3v3;
	static int enabled;
	unsigned char data;


	if (!wib_3v3) {
		wib_3v3 = regulator_get(NULL, "v_wib_3v3");
		if (IS_ERR(wib_3v3)) {
			wib_3v3 = NULL;
			printk(KERN_ERR "get v_wib_3v3 failed %s %d\n",
				__func__, __LINE__);
			return;
		}
	}

	if (on && !enabled) {
		regulator_set_voltage(wib_3v3, 3300000, 3300000);
		regulator_enable(wib_3v3);
		enabled = 1;

		/* Only when SD8787 are active (power on),
		 * it is meanful to enable the edge wakeup
		 */
		mmp_gpio_edge_add(&gpio_edge_sdio_dat1);

		/*disable buck2 sleep mode when wifi power on*/
#if !defined(CONFIG_MFD_D2199_BRINGUP_RECHECK) //DLG
#if defined(CONFIG_MFD_88PM822)
		pm822_extern_setbits(PM822_POWER_PAGE, PM822_BUCK_SLP1,
					PM822_BUCK2_SLP1_MASK, PM822_BUCK2_SLP1_MASK);
#else
		pm800_extern_setbits(PM80X_POWER_PAGE, PM800_BUCK_SLP1,
					PM800_BUCK2_SLP1_MASK, PM800_BUCK2_SLP1_MASK);
#endif
#endif
	}

	if (!on && enabled) {
		regulator_disable(wib_3v3);
		enabled = 0;

		mmp_gpio_edge_del(&gpio_edge_sdio_dat1);

		/*enable buck2 sleep mode when wifi power off*/
#if !defined(CONFIG_MFD_D2199_BRINGUP_RECHECK) //DLG
#if defined(CONFIG_MFD_88PM822)
		pm822_extern_setbits(PM822_POWER_PAGE, PM822_BUCK_SLP1,
					PM822_BUCK2_SLP1_MASK, PM822_BUCK2_SLP1_UNMASK);
#else
		pm800_extern_setbits(PM80X_POWER_PAGE, PM800_BUCK_SLP1,
					PM800_BUCK2_SLP1_MASK, PM800_BUCK2_SLP1_UNMASK);
#endif
#endif
	}
}
#endif

static struct sdhci_pxa_dtr_data sd_dtr_data[] = {
	{
		.timing		= MMC_TIMING_LEGACY, /* < 25MHz */
		.preset_rate	= PXA_SDH_DTR_26M,
		.src_rate	= PXA_SDH_DTR_52M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR12, /* 25MHz */
		.preset_rate	= PXA_SDH_DTR_26M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR25, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_SD_HS, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_UHS_DDR50, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR50, /* 100MHz */
		.preset_rate	= PXA_SDH_DTR_104M,
		.src_rate	= PXA_SDH_DTR_208M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR104, /* 208MHz */
		.preset_rate	= PXA_SDH_DTR_208M,
		.src_rate	= PXA_SDH_DTR_416M,
	},
		/*
		 * end of sdhc dtr table
		 * set as the default src rate
		 */
	{
		.timing		= MMC_TIMING_MAX,
		.preset_rate	= PXA_SDH_DTR_PS_NONE,
		.src_rate	= PXA_SDH_DTR_208M,
	},
};

static struct sdhci_pxa_dtr_data sdio_dtr_data[] = {
	{
		.timing		= MMC_TIMING_LEGACY, /* < 25MHz */
		.preset_rate	= PXA_SDH_DTR_26M,
		.src_rate	= PXA_SDH_DTR_52M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR12, /* 25MHz */
		.preset_rate	= PXA_SDH_DTR_26M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR25, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_SD_HS, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_45M,
		.src_rate	= PXA_SDH_DTR_89M,
	},
	{
		.timing		= MMC_TIMING_UHS_DDR50, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR50, /* 100MHz */
		.preset_rate	= PXA_SDH_DTR_104M,
		.src_rate	= PXA_SDH_DTR_208M,
	},
	{
		.timing		= MMC_TIMING_UHS_SDR104, /* 208MHz */
		.preset_rate	= PXA_SDH_DTR_208M,
		.src_rate	= PXA_SDH_DTR_416M,
	},
	{
		.timing		= MMC_TIMING_MAX,
		.preset_rate	= PXA_SDH_DTR_PS_NONE,
		.src_rate	= PXA_SDH_DTR_89M,
	},
};
static struct sdhci_pxa_dtr_data emmc_dtr_data[] = {
	{
		.timing		= MMC_TIMING_LEGACY, /* < 25MHz */
		.preset_rate	= PXA_SDH_DTR_26M,
		.src_rate	= PXA_SDH_DTR_52M,
	},
	{
		.timing		= MMC_TIMING_MMC_HS, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_UHS_DDR50, /* 50MHz */
		.preset_rate	= PXA_SDH_DTR_52M,
		.src_rate	= PXA_SDH_DTR_104M,
	},
	{
		.timing		= MMC_TIMING_MMC_HS200, /* 208MHz */
		.preset_rate	= PXA_SDH_DTR_156M,
		.src_rate	= PXA_SDH_DTR_156M,
	},
	{
		.timing		= MMC_TIMING_MAX,
		.preset_rate	= PXA_SDH_DTR_PS_NONE,
		.src_rate	= PXA_SDH_DTR_208M,
	},
};



/* For emeiDKB, MMC1(SDH1) used for SD/MMC Card slot */
static struct sdhci_pxa_platdata pxa988_sdh_platdata_mmc1 = {
	.flags		= PXA_FLAG_ENABLE_CLOCK_GATING,
	.clk_delay_cycles	= 0x25,
	.host_caps_disable	= MMC_CAP_UHS_SDR12
					|MMC_CAP_UHS_SDR25
					|MMC_CAP_UHS_SDR50
					|MMC_CAP_UHS_DDR50
					|MMC_CAP_UHS_SDR104,
	.host_caps2	= MMC_CAP2_DETECT_ON_ERR,
	.quirks			= SDHCI_QUIRK_INVERTED_WRITE_PROTECT,
	.signal_vol_change	= emeidkb_sdcard_signal,
	.cd_type	 = PXA_SDHCI_CD_PERMANENT,
	.dtr_data		= sd_dtr_data,
};

/* For emeiDKB, MMC2(SDH2) used for WIB card */
static struct sdhci_pxa_platdata pxa988_sdh_platdata_mmc2 = {
	.flags          = PXA_FLAG_WAKEUP_HOST
				| PXA_FLAG_EN_PM_RUNTIME
				| PXA_FLAG_DISABLE_PROBE_CDSCAN,
	.cd_type	 = PXA_SDHCI_CD_EXTERNAL,
	.quirks2	= SDHCI_QUIRK2_HOLDSUSPEND_AFTER_REQUEST,
	.pm_caps	= MMC_PM_KEEP_POWER,
	.dtr_data	= sdio_dtr_data,
};

/* For emeiDKB, MMC3(SDH3) used for eMMC */
static struct sdhci_pxa_platdata pxa988_sdh_platdata_mmc3 = {
	.flags		= PXA_FLAG_ENABLE_CLOCK_GATING
				| PXA_FLAG_SD_8_BIT_CAPABLE_SLOT
				| PXA_FLAG_EN_PM_RUNTIME,
	.cd_type	 = PXA_SDHCI_CD_PERMANENT,
	.clk_delay_cycles	= 0xF,
	.host_caps	= MMC_CAP_1_8V_DDR,
	.dtr_data	= emmc_dtr_data,
};

static void __init emeidkb_init_mmc(void)
{
#ifdef CONFIG_SD8XXX_RFKILL
	int WIB_PDn = mfp_to_gpio(MFP_WIB_PDn);
	int WIB_RESETn = mfp_to_gpio(MFP_WIB_RESETn);

	if (!gpio_request(WIB_PDn, "WIB_PDn")) {
		gpio_direction_output(WIB_PDn, 0);
		gpio_free(WIB_PDn);
	}

	if (!gpio_request(WIB_RESETn, "WIB_RSTn")) {
		gpio_direction_output(WIB_RESETn, 0);
		gpio_free(WIB_RESETn);
	}

	add_sd8x_rfkill_device(WIB_PDn, WIB_RESETn,
			&pxa988_sdh_platdata_mmc2.pmmc,
			emeidkb_8787_set_power);
#endif

	/*
	 * Note!!
	 *  The regulator can't be used here, as this is called in arch_init
	 */

	/* HW MMC3(sdh3) used for eMMC, and register first */
	pxa988_add_sdh(3, &pxa988_sdh_platdata_mmc3);

	/* HW MMC1(sdh1) used for SD/MMC card */
	pxa988_sdh_platdata_mmc1.flags	 = PXA_FLAG_EN_PM_RUNTIME
				| PXA_FLAG_ENABLE_CLOCK_GATING | PXA_FLAG_NEW_RX_CFG_REG;

	pxa988_add_sdh(1, &pxa988_sdh_platdata_mmc1);

	/* HW MMC2(sdh2) used for SDIO(WIFI/BT/FM module), and register last */
	pxa988_add_sdh(2, &pxa988_sdh_platdata_mmc2);
	wlan_wakeup_init();
}
#else
static void __init emeidkb_init_mmc(void)
{

}
#endif /* CONFIG_MMC_SDHCI_PXAV3 */


#if defined(CONFIG_VIDEO_MMP)
static int pxa988_cam_clk_init(struct device *dev)
{
	struct mmp_cam_pdata *data = dev->platform_data;
	int ret;

	data->clk[0] = devm_clk_get(dev, "CCICFUNCLK");
	if (IS_ERR(data->clk[0])) {
		dev_err(dev, "Could not get function clk\n");
		ret = PTR_ERR(data->clk[0]);
		return ret;
	}

	data->clk[1] = devm_clk_get(dev, "CCICAXICLK");
	if (IS_ERR(data->clk[1])) {
		dev_err(dev, "Could not get AXI clk\n");
		ret = PTR_ERR(data->clk[1]);
		return ret;
	}

	data->clk[2] = devm_clk_get(dev, "LCDCIHCLK");
	if (IS_ERR(data->clk[2])) {
		dev_err(dev, "Could not get lcd/ccic AHB clk\n");
		ret = PTR_ERR(data->clk[2]);
		return ret;
	}

	data->clk[3] = devm_clk_get(dev, "CCICPHYCLK");
	if (IS_ERR(data->clk[3])) {
		dev_err(dev, "Could not get PHY clk\n");
		ret = PTR_ERR(data->clk[3]);
		return ret;
	}

	return 0;
}

static void pxa988_cam_set_clk(struct device *dev, int on)
{
	struct mmp_cam_pdata *data = dev->platform_data;
	int mipi = on & MIPI_ENABLE;

	on &= POWER_ON;
	if (on) {
		clk_enable(data->clk[2]);
		clk_enable(data->clk[1]);
		clk_enable(data->clk[0]);
		if (mipi)
			clk_enable(data->clk[3]);
	} else {
		if (mipi)
			clk_disable(data->clk[3]);
		clk_disable(data->clk[0]);
		clk_disable(data->clk[1]);
		clk_disable(data->clk[2]);
	}
}
#endif

#if 0
struct mmp_cam_pdata mv_cam_data = {
	.name = "EMEI",
	.dma_burst = 64,
	.mclk_min = 24,
	.mclk_src = 3,
	.mclk_div = 13,
//	.init_pin = pxa988_cam_pin_init,
	.init_clk = pxa988_cam_clk_init,
	.enable_clk = pxa988_cam_set_clk,
};

static struct i2c_board_info dkb_i2c_camera[] = {
	{
		I2C_BOARD_INFO("ov2659", 0x30),
	},
	{
		I2C_BOARD_INFO("ov5640", 0x3c),
	},
};


static int camera_sensor_power(struct device *dev, int on)
{
	unsigned int cam_pwr;
	unsigned int cam_reset;
	static struct regulator *v_sensor;

	if (!v_sensor) {
		v_sensor = regulator_get(NULL, "v_cam_avdd");
		if (IS_ERR(v_sensor)) {
			v_sensor = NULL;
			pr_err(KERN_ERR "Enable v_ldo16 failed!\n");
			return -EIO;

		}
	}

	cam_pwr = mfp_to_gpio(GPIO082_GPIO_CAM_PD_SUB);
	cam_reset = mfp_to_gpio(GPIO083_GPIO_CAM_RST_SUB);

	if (cam_pwr) {
		if (gpio_request(cam_pwr, "CAM_PWR")) {
			printk(KERN_ERR "Request GPIO failed,"
					"gpio: %d\n", cam_pwr);
			return -EIO;
		}
	}
	if (gpio_request(cam_reset, "CAM_RESET")) {
		printk(KERN_ERR "Request GPIO failed,"
			"gpio: %d\n", cam_reset);
		return -EIO;
	}

	if (on) {
		regulator_set_voltage(v_sensor, 2800000, 2800000);
		regulator_enable(v_sensor);
		msleep(20);
		gpio_direction_output(cam_pwr, 0);
		mdelay(1);
		gpio_direction_output(cam_reset, 0);
		mdelay(1);
		gpio_direction_output(cam_reset, 1);
		mdelay(1);
	} else {
		gpio_direction_output(cam_reset, 0);
		mdelay(1);
		gpio_direction_output(cam_reset, 1);
		gpio_direction_output(cam_pwr, 1);
		regulator_disable(v_sensor);
	}

	gpio_free(cam_pwr);
	gpio_free(cam_reset);

	return 0;
}

static struct sensor_board_data ov2659_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_FRONT | SENSOR_RES_LOW,
	.bus_type	= V4L2_MBUS_PARALLEL,
	.bus_flag	= 0,	/* EMEI DKB connection don't change polarity of
				 * PCLK/HSYNC/VSYNC signal */
	.plat		= &mv_cam_data,
};

static struct soc_camera_link iclink_ov2659_dvp = {
	.bus_id         = 0,            /* Must match with the camera ID */
	.power          = camera_sensor_power,
	.board_info     = &dkb_i2c_camera[0],
	.i2c_adapter_id = 0,
	.module_name    = "ov2659",
	.priv		= &ov2659_data,
	.flags		= 0,	/* controller driver should copy priv->bus_flag
				 * here, so soc_camera_apply_board_flags can
				 * take effect */
};

static struct platform_device dkb_ov2659_dvp = {
	.name   = "soc-camera-pdrv",
	.id     = 0,
	.dev    = {
		.platform_data = &iclink_ov2659_dvp,
	},
};

#if defined(CONFIG_SOC_CAMERA_OV5640) || defined(CONFIG_SOC_CAMERA_OV5640_ECS)
static int ov5640_sensor_power(struct device *dev, int on)
{
	static struct regulator *af_vcc;
	static struct regulator *avdd;
	int cam_reset;
	int pwdn = mfp_to_gpio(GPIO080_GPIO_CAM_PD_MAIN);
	int ret = 0;

	cam_reset = mfp_to_gpio(GPIO081_GPIO_CAM_RST_MAIN);

	if (gpio_request(pwdn, "CAM_ENABLE_LOW")) {
		ret = -EIO;
		goto out;
	}

	if (gpio_request(cam_reset, "CAM_RESET_LOW")) {
		ret = -EIO;
		goto out_rst;
	}

	if (!af_vcc) {
		af_vcc = regulator_get(dev, "v_cam_af");
		if (IS_ERR(af_vcc)) {
			ret = -EIO;
			goto out_af_vcc;
		}
	}

	if (!avdd) {
		avdd = regulator_get(dev, "v_cam_avdd");
		if (IS_ERR(avdd)) {
			ret =  -EIO;
			goto out_avdd;
		}
	}

	switch (on) {
	case POWER_OFF:
		gpio_direction_output(cam_reset, 0);
		mdelay(1);
		gpio_direction_output(cam_reset, 1);

		regulator_disable(avdd);
		regulator_disable(af_vcc);

		gpio_direction_output(pwdn, 1);
		mdelay(1);
		break;
	case POWER_ON:
		regulator_set_voltage(af_vcc, 2800000, 2800000);
		regulator_enable(af_vcc);
		regulator_set_voltage(avdd, 2800000, 2800000);
		regulator_enable(avdd);
		mdelay(5);
		gpio_direction_output(pwdn, 0);
		mdelay(1);
		gpio_direction_output(cam_reset, 0);
		mdelay(1);
		gpio_direction_output(cam_reset, 1);
		mdelay(20);
		break;
	case POWER_SAVING:
		gpio_direction_output(pwdn, 1);
		mdelay(1);
		break;
	case POWER_RESTORE:
		gpio_direction_output(pwdn, 0);
		mdelay(1);
		break;
	default:
		dev_err(dev, "unknown sensor power operation!\n");
		break;
	}

	gpio_free(cam_reset);
	gpio_free(pwdn);
	return 0;

out_avdd:
	avdd = NULL;
	regulator_put(af_vcc);
out_af_vcc:
	af_vcc = NULL;
	gpio_free(cam_reset);
out_rst:
	gpio_free(pwdn);
out:
	return ret;
}

static struct sensor_board_data ov5640_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_BACK | SENSOR_RES_HIGH,
	.bus_type	= V4L2_MBUS_CSI2,
	.bus_flag	= V4L2_MBUS_CSI2_2_LANE, /* ov5640 used 2 lanes */
	.dphy		= {0x0D06, 0x33, 0x0900},
	.mipi_enabled	= 0,
	.plat		= &mv_cam_data,
};

static struct soc_camera_link iclink_ov5640_mipi = {
	.bus_id         = 0,            /* Must match with the camera ID */
	.power          = ov5640_sensor_power,
	.board_info     = &dkb_i2c_camera[1],
	.i2c_adapter_id = 0,
	.module_name    = "ov5640",
	.priv		= &ov5640_data,
};

static struct platform_device dkb_ov5640_mipi = {
	.name   = "soc-camera-pdrv",
	.id     = 1,
	.dev    = {
		.platform_data = &iclink_ov5640_mipi,
	},
};
#endif
#endif
static struct platform_device *dkb_platform_devices[] = {
#if defined(CONFIG_SOC_CAMERA_OV2659)
	&dkb_ov2659_dvp,
#endif
#if defined(CONFIG_SOC_CAMERA_OV5640) || defined(CONFIG_SOC_CAMERA_OV5640_ECS)
	&dkb_ov5640_mipi,
#endif
};


#ifdef CONFIG_VPU_DEVFREQ
static struct devfreq_frequency_table *vpu_freq_table;

static struct devfreq_platform_data devfreq_vpu_pdata = {
	.clk_name = "VPUCLK",
};

static struct platform_device pxa988_device_vpudevfreq = {
	.name = "devfreq-vpu",
	.id = -1,
};

static void __init pxa988_init_device_vpudevfreq(void)
{
	u32 i = 0;
	u32 vpu_freq_num = pxa988_get_vpu_op_num();

	vpu_freq_table = kmalloc(sizeof(struct devfreq_frequency_table) * \
					(vpu_freq_num + 1), GFP_KERNEL);
	if (!vpu_freq_table)
		return;

	for (i = 0; i < vpu_freq_num; i++) {
		vpu_freq_table[i].index = i;
		vpu_freq_table[i].frequency = pxa988_get_vpu_op_rate(i);
	}
	vpu_freq_table[i].index = i;
	vpu_freq_table[i].frequency = DEVFREQ_TABLE_END;

	devfreq_vpu_pdata.freq_table = vpu_freq_table;

	pxa988_device_vpudevfreq.dev.platform_data = (void *)&devfreq_vpu_pdata;
	platform_device_register(&pxa988_device_vpudevfreq);
}
#endif

#ifdef CONFIG_DDR_DEVFREQ
static struct devfreq_frequency_table *ddr_freq_table;

static struct devfreq_pm_qos_table ddr_freq_qos_table[] = {
	/* list all possible frequency level here */
	{
		.freq = 208000,
		.qos_value = DDR_CONSTRAINT_LVL0,
	},
	{
		.freq = 312000,
		.qos_value = DDR_CONSTRAINT_LVL1,
	},
	{
		.freq = 400000,
		.qos_value = DDR_CONSTRAINT_LVL2,
	},
	{
		.freq = 533000,
		.qos_value = DDR_CONSTRAINT_LVL3,
	},
	{0, 0},
};


static struct devfreq_platform_data devfreq_ddr_pdata = {
	.clk_name = "ddr",
	.interleave_is_on = 0,	/* only one mc */
};

static struct platform_device pxa988_device_ddrdevfreq = {
	.name = "devfreq-ddr",
	.id = -1,
};

static void __init pxa988_init_device_ddrdevfreq(void)
{
	u32 i = 0;
	u32 ddr_freq_num = pxa988_get_ddr_op_num();

	ddr_freq_table = kmalloc(sizeof(struct devfreq_frequency_table) * \
					(ddr_freq_num + 1), GFP_KERNEL);
	if (!ddr_freq_table)
		return;

	for (i = 0; i < ddr_freq_num; i++) {
		ddr_freq_table[i].index = i;
		ddr_freq_table[i].frequency = pxa988_get_ddr_op_rate(i);
	}
	ddr_freq_table[i].index = i;
	ddr_freq_table[i].frequency = DEVFREQ_TABLE_END;

	devfreq_ddr_pdata.freq_table = ddr_freq_table;
	devfreq_ddr_pdata.hw_base[0] =  DMCU_VIRT_BASE;
	devfreq_ddr_pdata.hw_base[1] =  DMCU_VIRT_BASE;

	if (cpu_is_pxa986() && (!cpu_is_z1z2()))
		devfreq_ddr_pdata.qos_list = ddr_freq_qos_table;

	pxa988_device_ddrdevfreq.dev.platform_data = (void *)&devfreq_ddr_pdata;
	platform_device_register(&pxa988_device_ddrdevfreq);
}
#endif

#if defined(CONFIG_SEC_THERMISTOR)
static struct sec_therm_adc_table adc_temp2_table[] = {
	/* ADC, Temperature */
	{ 1423,  -100 },
	{ 1363,  -90 },
	{ 1303,  -80 },
	{ 1243,  -70 },
	{ 1183,  -60 },
	{ 1123,  -50 },
	{ 1079,  -40 },
	{ 1035,  -30 },
	{ 991,  -20 },
	{ 947,  -10 },
	{ 903,  0 },
	{ 869,  10 },
	{ 835,  20 },
	{ 801,  30 },
	{ 767,  40 },
	{ 734,  50 },
	{ 709,  60 },
	{ 685,  70 },
	{ 661,  80 },
	{ 637,  90 },
	{ 613,  100 },
	{ 590,  110 },
	{ 568,  120 },
	{ 546,  130 },
	{ 524,  140 },
	{ 502,  150 },
	{ 485,  160 },
	{ 468,  170 },
	{ 451,  180 },
	{ 434,  190 },
	{ 417,  200 },
	{ 402,  210 },
	{ 386,  220 },
	{ 370,  230 },
	{ 354,  240 },
	{ 339,  250 },
	{ 327,  260 },
	{ 316,  270 },
	{ 305,  280 },
	{ 294,  290 },
	{ 283,  300 },
	{ 274,  310 },
	{ 265,  320 },
	{ 256,  330 },
	{ 247,  340 },
	{ 238,  350 },
	{ 230,  360 },
	{ 223,  370 },
	{ 216,  380 },
	{ 209,  390 },
	{ 202,  400 },
	{ 196,  410 },
	{ 190,  420 },
	{ 185,  430 },
	{ 179,  440 },
	{ 174,  450 },
	{ 168,  460 },
	{ 163,  470 },
	{ 157,  480 },
	{ 152,  490 },
	{ 147,  500 },
	{ 142,  510 },
	{ 138,  520 },
	{ 133,  530 },
	{ 129,  540 },
	{ 124,  550 },
	{ 120,  560 },
	{ 116,  570 },
	{ 112,  580 },
	{ 108,  590 },
	{ 105,  600 },
	{ 102,  610 },
	{ 99,  620 },
	{ 96,  630 },
	{ 93,  640 },
	{ 91,  650 },
	{ 88,  660 },
	{ 86,  670 },
	{ 84,  680 },
	{ 82,  690 },
	{ 80,  700 },
	{ 77,  710 },
	{ 75,  720 },
	{ 72,  730 },
	{ 70,  740 },
	{ 67,  750 },
};

static struct sec_therm_platform_data sec_therm_pdata = {
	.adc_arr_size	= ARRAY_SIZE(adc_temp2_table),
#if defined(CONFIG_MFD_D2199)
	.adc_channel	= D2199_ADC_TEMPERATURE_2,
#else
	.adc_channel	= PM800_GPADC1_MEAS1, /* GoldenVE TEMP_RF */
#endif
	.adc_table	= adc_temp2_table,
};

static struct platform_device sec_device_thermistor = {
	.name = "sec-thermistor",
	.id = -1,
	.dev.platform_data = &sec_therm_pdata,
};
#endif /* CONFIG_SEC_THERMISTOR */

#ifdef CONFIG_LEDS_RT8547
static struct rt8547_platform_data rt8547_pdata = {
	.flen_gpio = 20,   /* GPIO_20 */
	.flset_gpio = 97,  /* GPIO_97 */
	.strobe_current = STROBE_CURRENT_1000MA,
	.torch_current = TORCH_CURRENT_100MA,
	.strobe_timing = 0x0f, //please refer to the data sheet
};
static struct platform_device leds_rt8547_device = {
	.name = "leds-rt8547",
	.id = -1,
	.dev = {
		.platform_data = &rt8547_pdata,
	},
};
#endif /* #ifdef CONFIG_LEDS_RT8547 */

/* clk usage desciption */
MMP_HW_DESC(fb, "pxa168-fb", 0, PM_QOS_CPUIDLE_BLOCK_DDR_VALUE, "LCDCIHCLK");
struct mmp_hw_desc *emei_dkb_hw_desc[] __initdata = {
	&mmp_device_hw_fb,
};

#define MFP_RF_EN              GPIO010_GPIO_10
static int waruba_enable_RF(void)
{

       int RF_EN = mfp_to_gpio(MFP_RF_EN);

       if (!gpio_request(RF_EN, "RF_EN")) {
               gpio_direction_output(RF_EN, 1);

               gpio_free(RF_EN);
       }
	   else
	   	return -1;

	   return 0;

}

#ifdef CONFIG_PROC_FS

#ifdef GPS_LDO_POWER
static int gps_enable_control(int flag)
{
        static struct regulator *gps_regulator = NULL;
        static int f_enabled = 0;
        printk("[GPS] LDO control : %s\n", flag ? "ON" : "OFF");

        if (flag && (!f_enabled)) {
                      gps_regulator = regulator_get(NULL, "v_gps_1v8");
                      if (IS_ERR(gps_regulator)) {
                                   gps_regulator = NULL;
                                   return EIO;
                      } else {
                                   regulator_set_voltage(gps_regulator, 1800000, 1800000);
                                   regulator_enable(gps_regulator);
                      }
                      f_enabled = 1;
        }
        if (f_enabled && (!flag))
        {
                      if (gps_regulator) {
                                   regulator_disable(gps_regulator);
                                   regulator_put(gps_regulator);
                                   gps_regulator = NULL;
                      }
                      f_enabled = 0;
        }
        return 0;
}
#endif

static void gps_eclk_ctrl(int on)
{
	mfp_cfg_t gps_eclk_en = GPIO089_GPS_CLK;
	mfp_cfg_t gps_eclk_dis = GPIO089_AP_AGPS_CLK_DIS;

	if (on)
		mfp_config(&gps_eclk_en, 1);
	else
		mfp_config(&gps_eclk_dis, 1);
}

/* GPS: power on/off control */
static void gps_power_on(void)
{
	unsigned int gps_rst_n,gps_on, gps_clk;
	gps_eclk_ctrl(0);
#ifdef CONFIG_SEC_GPIO_DVS
	static int is_initdvs_done = 0;
#endif

	gps_rst_n = mfp_to_gpio(GPIO005_AP_AGPS_RESET);
	if (gpio_request(gps_rst_n, "gpio_gps_rst")) {
		pr_err("Request GPIO failed, gpio: %d\n", gps_rst_n);
		return;
	}
	gps_on = mfp_to_gpio(GPIO052_AP_AGPS_ONOFF);
	if (gpio_request(gps_on, "gpio_gps_on")) {
		pr_err("Request GPIO failed,gpio: %d\n", gps_on);
		goto out;
	}
	gps_clk = mfp_to_gpio(GPIO089_AP_AGPS_CLK_DIS);
	if (gpio_request(gps_clk, "gpio_gps_clk")) {
		pr_err("Request GPIO failed,gpio: %d\n", gps_clk);
		goto out3;
	}

	gpio_direction_output(gps_clk, 0);
	gpio_direction_output(gps_rst_n, 0);
	gpio_direction_output(gps_on, 0);
#ifdef GPS_LDO_POWER
	gps_enable_control(1);
#endif
	mdelay(10);
//	gpio_direction_output(gps_rst_n, 1);
	mdelay(10);
//	gpio_direction_output(gps_on, 1);

	pr_info("gps chip powered on\n");

	gpio_free(gps_clk);
out3:
	gpio_free(gps_on);
out:
	gpio_free(gps_rst_n);

#ifdef CONFIG_SEC_GPIO_DVS
	/************************ Caution !!! ****************************/
	/* This function must be located in appropriate INIT position
	 * in accordance with the specification of each BB vendor.
	 */
	/************************ Caution !!! ****************************/
	if(!is_initdvs_done) {
		gpio_dvs_check_initgpio();
		is_initdvs_done = 1;
	}
#endif

	return;
}

static void gps_power_off(void)
{
	unsigned int gps_rst_n, gps_on, gps_clk;
	gps_eclk_ctrl(0);


	/* hardcode */

	gps_on = mfp_to_gpio(GPIO052_AP_AGPS_ONOFF);
	if (gpio_request(gps_on, "gpio_gps_on")) {
		pr_err("Request GPIO failed,gpio: %d\n", gps_on);
		return;
	}

	gps_rst_n = mfp_to_gpio(GPIO005_AP_AGPS_RESET);
	if (gpio_request(gps_rst_n, "gpio_gps_rst")) {
		pr_debug("Request GPIO failed, gpio: %d\n", gps_rst_n);
		goto out2;
	}

	gps_clk = mfp_to_gpio(GPIO089_AP_AGPS_CLK_DIS);
	if (gpio_request(gps_clk, "gpio_gps_clk")) {
		pr_debug("Request GPIO failed, gpio: %d\n", gps_clk);
		goto out4;
	}

	gpio_direction_output(gps_clk, 0);
	gpio_direction_output(gps_rst_n, 0);
	gpio_direction_output(gps_on, 0);
#ifdef GPS_LDO_POWER
	gps_enable_control(0);
#endif
	pr_info("gps chip powered off\n");

	gpio_free(gps_clk);
out4:
	gpio_free(gps_rst_n);
out2:
	gpio_free(gps_on);
	return;
}

static void gps_reset(int flag)
{
	unsigned int gps_rst_n;

	gps_rst_n = mfp_to_gpio(GPIO005_AP_AGPS_RESET);
	if (gpio_request(gps_rst_n, "gpio_gps_rst")) {
		pr_err("Request GPIO failed, gpio: %d\n", gps_rst_n);
		return;
	}

	gpio_direction_output(gps_rst_n, flag);
	gpio_free(gps_rst_n);
	printk(KERN_INFO "gps chip reset with %s\n", flag ? "ON" : "OFF");
}

static void gps_on_off(int flag)
{
	unsigned int gps_on;

	gps_on = mfp_to_gpio(GPIO052_AP_AGPS_ONOFF);
	if (gpio_request(gps_on, "gpio_gps_on")) {
		pr_err("Request GPIO failed, gpio: %d\n", gps_on);
		return;
	}

	gpio_direction_output(gps_on, flag);
	gpio_free(gps_on);
	printk(KERN_INFO "gps chip onoff with %s\n", flag ? "ON" : "OFF");
}

#define SIRF_STATUS_LEN	16
static char sirf_status[SIRF_STATUS_LEN] = "off";

static ssize_t sirf_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len = strlen(sirf_status);

	sprintf(page, "%s\n", sirf_status);
	return len + 1;
}

static ssize_t sirf_write_proc(struct file *filp,
		const char *buff, size_t len, loff_t *off)
{
	char messages[256];
	int flag, ret;
	char buffer[7];

	if (len > 255)
		len = 255;

	memset(messages, 0, sizeof(messages));

	if (!buff || copy_from_user(messages, buff, len))
		return -EFAULT;

	if (strlen(messages) > (SIRF_STATUS_LEN - 1)) {
		pr_warning("[ERROR] messages too long! (%d) %s\n",
			strlen(messages), messages);
		return -EFAULT;
	}

	if (strncmp(messages, "off", 3) == 0) {
		strcpy(sirf_status, "off");
		gps_power_off();
	} else if (strncmp(messages, "on", 2) == 0) {
		strcpy(sirf_status, "on");
		gps_power_on();
	} else if (strncmp(messages, "reset", 5) == 0) {
		strcpy(sirf_status, messages);
		ret = sscanf(messages, "%s %d", buffer, &flag);
		if (ret == 2)
			gps_reset(flag);
	} else if (strncmp(messages, "sirfon", 6) == 0) {
		strcpy(sirf_status, messages);
		ret = sscanf(messages, "%s %d", buffer, &flag);
		if (ret == 2)
			gps_on_off(flag);
	} else if (strncmp(messages, "eclk", 4) == 0) {
		ret = sscanf(messages, "%s %d", buffer, &flag);
		if (ret == 2)
			gps_eclk_ctrl(flag);
	} else
		pr_info("usage: echo {on/off} > /proc/driver/sirf\n");

	return len;
}

static void create_sirf_proc_file(void)
{
	struct proc_dir_entry *sirf_proc_file = NULL;

	/*
	 * CSR and Marvell GPS lib will both use this file
	 * "/proc/drver/gps" may be modified in future
	 */
	sirf_proc_file = create_proc_entry("driver/sirf", 0644, NULL);
	if (!sirf_proc_file) {
		pr_err("sirf proc file create failed!\n");
		return;
	}

	sirf_proc_file->read_proc = sirf_read_proc;
	sirf_proc_file->write_proc = (write_proc_t  *)sirf_write_proc;
	gps_power_off();
}

static ssize_t pcm_mfp_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int mfp = mfp_read(mfp_to_gpio(GPIO025_GPIO_INPUT));

	mfp &= 0x7;

	sprintf(page, "GPIO25~28 are configured to %s\n",
			mfp ? "GSSP" : "GPIO input");

	return strlen(page) + 1;
}

static ssize_t pcm_mfp_write_proc(struct file *filp,
				const char *buff, size_t len, loff_t *off)
{
	char a;
	unsigned long i, j, gpio;
	unsigned long gssp_mfp[2][4] = {
		{
			GPIO025_GPIO_INPUT,
			GPIO026_GPIO_INPUT,
			GPIO027_GPIO_INPUT,
			GPIO028_GPIO_INPUT,
		},
		{
			GPIO025_GSSP_SCLK,	/* PCM_CLK */
			GPIO026_GSSP_SFRM,	/* PCM_SYNC */
			GPIO027_GSSP_TXD,	/* PCM_TXD */
			GPIO028_GSSP_RXD,	/* PCM_RXD */
		},
	};

	if (copy_from_user(&a, buff, 1))
		return -EINVAL;
	switch (a) {
	case '0':
		i = 0;
		pr_debug("Switch GPIO25~28 to GPIO input\n");
		break;
	case '1':
		i = 1;
		pr_debug("Switch GPIO25~28 to GSSP function\n");
		break;
	default:
		pr_err("[PCM_MFP] Error: invalid configuration\n");
		return len;
	}

	mfp_config(ARRAY_AND_SIZE(gssp_mfp[i]));

	for (j = 0; j < ARRAY_SIZE(gssp_mfp[i]); j++) {
		gpio = mfp_to_gpio(gssp_mfp[i][j]);
		gpio_request(gpio, NULL);
		gpio_direction_input(gpio);
		gpio_free(gpio);
	}

	return len;
}

static void create_pcm_mfp_proc_file(void)
{
	struct proc_dir_entry *proc_file = NULL;

	proc_file = create_proc_entry("driver/pcm_mfp", 0644, NULL);
	if (!proc_file) {
		pr_err("%s: create proc file failed\n", __func__);
		return;
	}

	proc_file->write_proc = (write_proc_t *)pcm_mfp_write_proc;
	proc_file->read_proc = (read_proc_t *)pcm_mfp_read_proc;

}

#endif

static struct timer_list uart_constraint_timer;
static struct pm_qos_request uart_lpm_cons;
static const char uart_cons_name[] = "uart rx pad";
static void uart_add_constraint(int mfp, void *unused)
{
	if (!mod_timer(&uart_constraint_timer, jiffies + 3 * HZ))
		pm_qos_update_request(&uart_lpm_cons,
			PM_QOS_CPUIDLE_BLOCK_DDR_VALUE);
}

static void uart_timer_handler(unsigned long data)
{
	pm_qos_update_request(&uart_lpm_cons,
		PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
}

struct gpio_edge_desc uart_rx_pad = {
	.mfp = MFP_PIN_GPIO47, /* ap UART rx */
	.handler = uart_add_constraint,
};

#if defined(CONFIG_MFD_88PM822)
#define PM822_SW_PDOWN			(1 << 5)

extern unsigned int lpcharge;
extern int i2c_set_pio_mode(void);
static void emei_dkb_poweroff(void)
{
	unsigned char data;
#ifdef CONFIG_BATTERY_SAMSUNG
	static char *pss[] = {"battery"};
	int i;
	bool charger_present = false;
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	/*
	 * If we have a charger connected and we're powering off,
	 * reboot into charge-only mode.
	 */

	for (i = 0; i < ARRAY_SIZE(pss); i++) {
		psy = power_supply_get_by_name(pss[i]);
		if (!psy)
			continue;

		ret = psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);

		if (!ret && (val.intval != POWER_SUPPLY_TYPE_BATTERY)) {
			charger_present = true;
			break;
		}
	}

	preempt_enable();
	if (charger_present) {
		pr_info("Restarting Device with TA ....\n");
		data = pm822_extern_read(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER);
		data &= ~(PMIC_GENERAL_USE_REBOOT_DN_MASK);
		data |= PMIC_GENERAL_USE_BOOT_BY_CHG;
		data |= PMIC_GENERAL_USE_SHUTDOWN_BY_POWEROFF;
		pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);

		/* PMIC Watchdog Kick to prepare for AP Watchdog failure */
		pr_info("%d; enable PMIC watchdog\n", __LINE__);
		data = pm822_extern_read(PM822_BASE_PAGE, PM822_WAKEUP1);
		pr_info("PM822_WAKEUP1 Reg(0x%02x) is 0x%02x\n", PM822_WAKEUP1,data);
		data |= PM822_WAKEUP1_WD_MODE;
		pm822_extern_write(PM822_BASE_PAGE, PM822_WAKEUP1, data);

		data = pm822_extern_read(PM822_BASE_PAGE, PM822_WAKEUP2);
		pr_info("PM822_WAKEUP2 Reg(0x%02x) is 0x%02x\n", PM822_WAKEUP2,data);
		data &= ~(PM822_WD_TIMER_ACT_MASK);
		data |= PM822_WD_TIMER_ACT_4S;
		pm822_extern_write(PM822_BASE_PAGE, PM822_WAKEUP2, data);
		pr_info("0x%02x is written to PM822_WAKEUP2 Reg(0x%02x)\n",data, PM822_WAKEUP2);

		pm822_extern_write(PM822_BASE_PAGE, PM822_WATCHDOG_REG, PM822_WD_EN);
		data = pm822_extern_read(PM822_BASE_PAGE, PM822_WATCHDOG_REG);
		pr_info("WATCHDOG Reg(0x%02x) is 0x%02x\n", PM822_WATCHDOG_REG,data);

		machine_restart("ta");
	}
#endif

	pr_info("turning off power....\n");

	/* save power off reason */
	data = PMIC_GENERAL_USE_BOOT_BY_NONE | PMIC_GENERAL_USE_SHUTDOWN_BY_POWEROFF;
	pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
	data = pm822_extern_read(PM822_BASE_PAGE, PM822_WAKEUP1);
	pm822_extern_write(PM822_BASE_PAGE, PM822_WAKEUP1,
			   data | PM822_SW_PDOWN);
}
#elif defined(CONFIG_MFD_88PM800)
#define PM800_SW_PDOWN			(1 << 5)
static void emei_dkb_poweroff(void)
{
	unsigned char data;
	pr_info("turning off power....\n");

	preempt_enable();
        /* save power off reason */
	pm800_extern_write(PM80X_BASE_PAGE, PM800_USER_DATA3, PMIC_GENERAL_USE_BOOT_BY_NONE);
	data = pm800_extern_read(PM80X_BASE_PAGE, PM800_WAKEUP1);
	pm800_extern_write(PM80X_BASE_PAGE, PM800_WAKEUP1,
			   data | PM800_SW_PDOWN);
}
#endif

#ifdef CONFIG_ANDROID_RAM_CONSOLE
static struct platform_device pxa988_device_ramconsole = {
	.name = "ram_console",
	.id = -1,
};
#endif

extern int is_panic;
static int reboot_notifier_func(struct notifier_block *this,
		unsigned long code, void *p)
{
	char *cmd = p;
	unsigned char data;
	unsigned char pmic_download_register = 0;
	unsigned char pmic_register = 0;

	pr_info("reboot notifier: %s\n", cmd);
#if defined(CONFIG_MFD_88PM822)
	/* need to write reset reason according to cmd */
	data = pm822_extern_read(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER);
	data &= ~(PMIC_GENERAL_USE_REBOOT_DN_MASK);

	if (cmd) {
		if (!strcmp(cmd, "recovery")) {
			pr_info("Device will enter recovery mode on next booting\n");
			data |= PMIC_GENERAL_USE_BOOT_BY_FULL_RESET;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd, "recovery_done")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_RECOVERY_DONE;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd, "arm11_fota")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_FOTA;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd, "alarm")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_RTC_ALARM;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd, "debug0x4f4c")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_LOW;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd, "debug0x494d")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_MID;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if(!strcmp(cmd, "debug0x4948")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_HIGH;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd, "GlobalActions restart")) {
			data |= PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		} else if (!strcmp(cmd,"download")) {
			pmic_download_register = PMIC_GENERAL_DOWNLOAD_MODE_FUS + DOWNLOAD_FUS_SUD_BASE;
			data |= pmic_download_register;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
			pr_info("pmic_download_register FUS : %d ,read result : 0x%02x \n",
				pmic_download_register, pm822_extern_read(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER));
		} else if (!strncmp(cmd,"sud", 3)) {
			/* Value : 21 ~ 29 */
			pmic_download_register = cmd[3] - '0' + DOWNLOAD_FUS_SUD_BASE;
			data |= pmic_download_register;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
			pr_info("pmic_download_register SUDDLMOD : %d, read result : 0x%02x \n",
				pmic_download_register, pm822_extern_read(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER));
		} else {
			data |= PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET;
			pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
		}

	} else {
		data |= PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET;
		pm822_extern_write(PM822_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, data);
	}
	if (code != SYS_POWER_OFF) {
		pr_info("enable PMIC watchdog\n");
		data = pm822_extern_read(PM822_BASE_PAGE, PM822_WAKEUP1);
		pr_info("PM822_WAKEUP1 Reg(0x%02x) is 0x%02x\n", PM822_WAKEUP1,data);
		data |= PM822_WAKEUP1_WD_MODE;
		pm822_extern_write(PM822_BASE_PAGE, PM822_WAKEUP1, data);

		data = pm822_extern_read(PM822_BASE_PAGE, PM822_WAKEUP2);
		pr_info("PM822_WAKEUP2 Reg(0x%02x) is 0x%02x\n", PM822_WAKEUP2,data);
		data &= ~(PM822_WD_TIMER_ACT_MASK);
		data |= PM822_WD_TIMER_ACT_8S;
		pm822_extern_write(PM822_BASE_PAGE, PM822_WAKEUP2, data);
		pr_info("0x%02x is written to PM822_WAKEUP2 Reg(0x%02x)\n",data, PM822_WAKEUP2);

		pm822_extern_write(PM822_BASE_PAGE, PM822_WATCHDOG_REG, PM822_WD_EN);
		data = pm822_extern_read(PM822_BASE_PAGE, PM822_WATCHDOG_REG);
		pr_info("WATCHDOG Reg(0x%02x) is 0x%02x\n", PM822_WATCHDOG_REG,data);
	}
#elif defined(CONFIG_MFD_88PM800)
	if (cmd && !strcmp(cmd, "recovery")) {
		data = pm800_extern_read(PM80X_BASE_PAGE, 0xef);
		pm800_extern_write(PM80X_BASE_PAGE, 0xef, data | 0x1);
	} else {
		data = pm800_extern_read(PM80X_BASE_PAGE, 0xef);
		pm800_extern_write(PM80X_BASE_PAGE, 0xef, data & 0xfe);
	}

	if (cmd) {
		if (!strcmp(cmd, "recovery"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_FULL_RESET);
		else if (!strcmp(cmd, "recovery_done"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_RECOVERY_DONE);
		else if (!strcmp(cmd, "arm11_fota"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_FOTA);
		else if (!strcmp(cmd, "alarm"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_RTC_ALARM);
		else if(!strcmp(cmd, "debug0x4f4c"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_LOW);
		else if(!strcmp(cmd, "debug0x494d"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_MID);
		else if(!strcmp(cmd, "debug0x4948"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_HIGH);
		else if (!strcmp(cmd, "GlobalActions restart"))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET);
#ifdef CONFIG_ONECHIP_DUAL_MODEM
		else if (!strncmp(cmd, "swsel",5))
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_SET_SWITCH_SEL | cmd[5]-'0');
#endif
		else if (!strcmp(cmd,"download"))
		{
			pmic_download_register = PMIC_GENERAL_DOWNLOAD_MODE_FUS;
			pmic_register =(pmic_download_register<<4)&0xF0;
			pm800_extern_write(PM80X_BASE_PAGE, PM800_USER_DATA6, pmic_register);
			pr_info("pmic_download_register FUS : %d ,read result : 0x%02x \n",
				pmic_download_register, pm800_extern_read(PM80X_BASE_PAGE, PM800_USER_DATA6));
		}
		else if (!strncmp(cmd,"sud", 3))
		{
			pmic_download_register = cmd[3] - '0';
			pmic_register = (pmic_download_register << 4) & 0xF0;
			pm800_extern_write(PM80X_BASE_PAGE, PM800_USER_DATA6, pmic_register);
			pr_info("pmic_download_register SUDDLMOD : %d, read result : 0x%02x \n",
				pmic_download_register, pm800_extern_read(PM80X_BASE_PAGE, PM800_USER_DATA6));
		}
		else
			pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET);

	}
	else
		pm800_extern_write(PM80X_BASE_PAGE, PMIC_GENERAL_USE_REGISTER, PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET);


	if (code != SYS_POWER_OFF) {
		data = pm800_extern_read(PM80X_BASE_PAGE, 0xef);
		/* this bit is for charger server */
		pm800_extern_write(PM80X_BASE_PAGE, 0xef, data | 0x2);
	}

#elif defined (CONFIG_MFD_D2199)
	if (cmd && !strcmp(cmd, "recovery")) {
		d2199_extern_reg_read(D2199_GP_ID_1_REG, &data);
		d2199_extern_reg_write(D2199_GP_ID_1_REG, data | 0x1);
	} else {
		d2199_extern_reg_read(D2199_GP_ID_1_REG, &data);
		d2199_extern_reg_write(D2199_GP_ID_1_REG, data & 0xfe);
	}

	if (cmd) {
		if (!strcmp(cmd, "recovery"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_FULL_RESET);
		else if (!strcmp(cmd, "recovery_done"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_RECOVERY_DONE);
		else if (!strcmp(cmd, "arm11_fota"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_FOTA);
		else if (!strcmp(cmd, "alarm"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_RTC_ALARM);
		else if(!strcmp(cmd, "debug0x4f4c"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_LOW);
		else if(!strcmp(cmd, "debug0x494d"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_MID);
		else if(!strcmp(cmd, "debug0x4948"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_DEBUGLEVEL_HIGH);
		else if (!strcmp(cmd, "GlobalActions restart"))
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET);
		else if (!strcmp(cmd,"download"))
		{
			pr_info("pmic_download_register FUS : %d \n",pmic_download_register);
			pmic_download_register = PMIC_GENERAL_DOWNLOAD_MODE_FUS;
			pmic_register =(pmic_download_register<<4)&0xF0;
			d2199_extern_reg_write(D2199_GP_ID_1_REG, pmic_register);
		}
		else if (!strncmp(cmd,"sud", 3))
		{
			pmic_download_register = ((char*)cmd)[3] - '0';
			pmic_register = (pmic_download_register << 4) & 0xF0;
			d2199_extern_reg_write(D2199_GP_ID_1_REG, pmic_register);
			pr_info("pmic_download_register SUDDLMOD : %d \n", pmic_download_register);
		}
		else
			d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET);

	}
	else
		d2199_extern_reg_write(D2199_GP_ID_0_REG, PMIC_GENERAL_USE_BOOT_BY_INTENDED_RESET);

	if (code != SYS_POWER_OFF) {
		d2199_extern_reg_read(D2199_GP_ID_1_REG, &data);
		/* this bit is for charger server */
		d2199_extern_reg_write(D2199_GP_ID_1_REG, data | 0x2);
	}
#endif

	is_panic = 0;

	return 0;
}

static struct notifier_block reboot_notifier = {
	.notifier_call = reboot_notifier_func,
};

#if defined(CONFIG_MFD_D2199)
extern void d2199_system_poweroff(void);
#endif

void mfp_config_init_done(void)
{
	pr_info("%s\n", __func__);
}

static void __init emeidkb_init(void)
{
	int i;

#ifdef CONFIG_SEC_DEBUG
	sec_debug_init();
#endif
	for (i = 0; i < ARRAY_SIZE(emei_dkb_hw_desc); i++)
		mmp_device_hw_register(emei_dkb_hw_desc[i]);

	mfp_config(ARRAY_AND_SIZE(emeidkb_pin_config));

	if (dvc_flag)
		mfp_config(ARRAY_AND_SIZE(dvc_pin_config));

	mfp_config_init_done();

#if defined(CONFIG_MFD_D2199)
	pm_power_off = d2199_system_poweroff;
#else
	pm_power_off = emei_dkb_poweroff;
#endif
	register_reboot_notifier(&reboot_notifier);

	/* Uart1, AP kernel console and debug */
	pxa988_add_uart(1);
	/* Uart2, GPS */
	pxa988_add_uart(2);

	emeidkb_init_mmc();

	printk("system_rev is %d\n",  system_rev);

#ifdef CONFIG_ANDROID_RAM_CONSOLE
	platform_device_register(&pxa988_device_ramconsole);
#endif

	/* soc-rtc */
	platform_device_register(&pxa988_device_rtc);
	/* backlight */
#ifdef CONFIG_BACKLIGHT_KTD253
#define HX8369B_PANEL_BOE3	(0x55BCF0)
	if (panel_id == HX8369B_PANEL_BOE3) {
		ktd_lcd_backlight_data.brt_table = ktd_brt_table_boe;
		ktd_lcd_backlight_data.sz_table = ARRAY_SIZE(ktd_brt_table_boe);
	}
	platform_device_register(&ktd_device);
#endif


#if defined(CONFIG_MFD_88PM822)
	/* set pm822 dvc information,must before pm822 init */
	if (!dvc_flag)
		pm822_dvctable_init();
	else {
		pm822_dvc.reg_dvc = 1;
		pm822_dvc.set_dvc = dvc_set_voltage;
		pm822_dvc.write_reg = PMUM_DVC_AP;
		pm822_dvc.read_reg = PMUM_DVC_STATUS;
	}
#elif defined(CONFIG_MFD_88PM800)
	/* set pm800 dvc information,must before pm800 init */
	if (!dvc_flag)
		pm800_dvctable_init();
	else {
		pm80x_dvc.gpio_dvc = 0;
		pm80x_dvc.reg_dvc = 1;
		pm80x_dvc.set_dvc = dvc_set_voltage;
		pm80x_dvc.write_reg = PMUM_DVC_AP;
		pm80x_dvc.read_reg = PMUM_DVC_STATUS;
	}
#elif defined(CONFIG_D2199_DVC)
	//printk("[WS][DVFS][%s]-dvc_flag[%d]\n", __func__, dvc_flag);

	if (!dvc_flag)
		d2199_dvctable_init();
	else {
		d2199_dvc.gpio_dvc = 0;
		d2199_dvc.reg_dvc = 1;
		d2199_dvc.set_dvc = d2199_dvc_set_voltage;
		d2199_dvc.write_reg = PMUM_DVC_AP;
		d2199_dvc.read_reg = PMUM_DVC_STATUS;
	}
#endif

	pxa988_add_twsi(0, &emeidkb_ci2c_pdata,
			ARRAY_AND_SIZE(emeidkb_i2c_info));
	pxa988_add_twsi(1, &emeidkb_ci2c2_pdata,
			ARRAY_AND_SIZE(emeidkb_i2c2_info));
	if (system_rev == 0x00) {
		pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(emeidkb_pwr_i2c_gpio),
			ARRAY_AND_SIZE(emeidkb_pwr_i2c_info_rev00));
		pxa988_add_twsi(2, &emeidkb_pwr_i2c_pdata,
				ARRAY_AND_SIZE(emeidkb_pwr_i2c_info_rev00));
	} else {
		pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(emeidkb_pwr_i2c_gpio),
			ARRAY_AND_SIZE(emeidkb_pwr_i2c_info));
		pxa988_add_twsi(2, &emeidkb_pwr_i2c_pdata,
				ARRAY_AND_SIZE(emeidkb_pwr_i2c_info));
	}

#if defined(CONFIG_SENSORS_GP2A002S)
	pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(i2c_gp2a_gpio),
				ARRAY_AND_SIZE(i2c_gp2a));
#endif
#if defined (CONFIG_STC3115_FUELGAUGE)
	 pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(stc3115_i2c_gpio),
				ARRAY_AND_SIZE(stc3115_i2c_devices));
#endif
#if defined(CONFIG_SM5502_MUIC)
	 pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(sm5502_i2c_gpio),
				ARRAY_AND_SIZE(sm5502_i2c_devices));
#endif
#if defined(CONFIG_MFD_RT5033)
		pxa986_golden_mfd_init();
#endif
#if defined(CONFIG_BATTERY_SAMSUNG)
	pxa986_golden_battery_init();
#endif
	if ((board_id == 0x0) || (board_id >= 0x3)) {
#if defined(CONFIG_BQ24157_CHARGER)
         pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(bq24157_i2c_gpio),
				ARRAY_AND_SIZE(bq24157_i2c_devices));
#endif
	} else {
#if defined(CONFIG_I2C_RT9455)
         pxa_init_i2c_gpio_irq(ARRAY_AND_SIZE(rt9455_i2c_gpio),
				ARRAY_AND_SIZE(rt9455_i2c_devices));
#endif
	}

	golden_input_init();

#if defined (CONFIG_STC3115_FUELGAUGE)
	platform_device_register(&i2c_stc3115_bus_device);
	i2c_register_board_info(6, ARRAY_AND_SIZE(stc3115_i2c_devices));
#endif
#if defined(CONFIG_SM5502_MUIC)
	platform_device_register(&i2c_sm5502_bus_device);
	i2c_register_board_info(7, ARRAY_AND_SIZE(sm5502_i2c_devices));
#endif
#if defined(CONFIG_RT8969)||defined(CONFIG_RT8973)
        platform_device_register(&i2c_rt8973_bus_device);
        i2c_register_board_info(7, ARRAY_AND_SIZE(rt8973_i2c_devices));
#endif

	if ((board_id == 0x0) || (board_id >= 0x3)) {
#if defined(CONFIG_BQ24157_CHARGER)
	platform_device_register(&i2c_bq24157_bus_device);
	i2c_register_board_info(8, ARRAY_AND_SIZE(bq24157_i2c_devices));
#endif
	} else {
#if defined(CONFIG_I2C_RT9455)
	platform_device_register(&i2c_rt9455_bus_device);
	i2c_register_board_info(8, ARRAY_AND_SIZE(rt9455_i2c_devices));
#endif
        }


	/* add audio device: sram, ssp2, squ(tdma), pxa-ssp, mmp-pcm */
	pxa988_add_asram(&pxa988_asram_info);
	pxa988_add_ssp(1);
	pxa988_add_ssp(4);
	platform_device_register(&pxa988_device_squ);
	platform_device_register(&pxa988_device_asoc_platform);
	platform_device_register(&pxa988_device_asoc_ssp1);
	platform_device_register(&pxa988_device_asoc_gssp);
	platform_device_register(&pxa988_device_asoc_pcm);
	platform_device_register(&emei_dkb_audio_device);
#if 0
	/* off-chip devices */
	platform_add_devices(ARRAY_AND_SIZE(dkb_platform_devices));
#endif
#ifdef CONFIG_FB_PXA168
	emeidkb_add_lcd_mipi();
	if (has_feat_video_replace_graphics_dma())
		emeidkb_add_tv_out();
#endif

#ifdef CONFIG_UIO_CODA7542
	pxa_register_coda7542();
#endif

#ifdef CONFIG_USB_MV_UDC
	pxa988_device_udc.dev.platform_data = &emeidkb_usb_pdata;
	platform_device_register(&pxa988_device_udc);
#endif

#ifdef CONFIG_MACH_GOLDEN
	/* For following apse codebase, I did not want to modify too many board-aruba files
	 *  so I try to add a little code to apply to apse's code, such as mv_cam_pdata init.
	 *  By Vincent Wan.
	*/
	mv_cam_data_forssg.init_clk = pxa988_cam_clk_init,
	mv_cam_data_forssg.enable_clk = pxa988_cam_set_clk,
	init_samsung_cam();
#endif

#if 0
#if defined(CONFIG_VIDEO_MMP)
	pxa988_add_cam(&mv_cam_data);
#endif

#if defined(CONFIG_VIDEO_MVISP) && defined(CONFIG_UIO_MVISP)
	pxa988_init_dxoisp();
#endif
#endif

#ifdef CONFIG_LEDS_RT8547
	platform_device_register(&leds_rt8547_device);
#endif /* #ifdef CONFIG_LEDS_RT8547 */
#if defined(CONFIG_SPA)
        platform_device_register(&Sec_BattMonitor);
#endif

#if defined(CONFIG_SENSORS_BMA2X2) \
 || defined(CONFIG_SENSORS_BMM050) \
 || defined(CONFIG_SENSORS_GP2A002S) \
 || defined(CONFIG_SENSORS_GP2A030) \
 || defined(CONFIG_INPUT_MPU6500) \
 || defined(CONFIG_SENSORS_HSCDTD008A) \
 || defined(CONFIG_SENSORS_GP2A_PROXIMITY)
	platform_device_register(&i2c_gpio_device);
#endif

#if defined(CONFIG_SENSORS_HSCDTD008A)
	platform_device_register(&alps_pdata );
#endif

#if defined(CONFIG_SENSORS_BMA2X2)
	i2c_register_board_info(5, &i2c_bma2x2, 1);
#endif
#if defined(CONFIG_SENSORS_BMM050)
	i2c_register_board_info(5, &i2c_bmm050, 1);
#endif
#if defined(CONFIG_SENSORS_GP2A002S) \
 || defined(CONFIG_SENSORS_GP2A030) \
 || defined(CONFIG_SENSORS_GP2A_PROXIMITY)
	i2c_register_board_info(5, &i2c_gp2a, 1);
#endif

#if defined(CONFIG_INPUT_MPU6500)
	pr_info("%s : system rev = %d\n", __func__, system_rev);
	if (system_rev > 0)
		i2c_mpu6500[0].platform_data = &mpu6500_pdata_rev01;
	i2c_register_board_info(5, &i2c_mpu6500, 1);
#endif
#if defined(CONFIG_SENSORS_HSCDTD008A)
	i2c_register_board_info(5, &i2c_hscd008a, 1);
#endif

#if defined(CONFIG_NFC_PN547)
	platform_device_register(&i2c_gpio_device_nfc);
	i2c_register_board_info(9, &i2c_pn547, 1);
#endif

#if !defined(CONFIG_INPUT_MPU6500)
	waruba_enable_RF();
#endif

#ifdef CONFIG_VPU_DEVFREQ
	pxa988_init_device_vpudevfreq();
#endif

#ifdef CONFIG_DDR_DEVFREQ
	pxa988_init_device_ddrdevfreq();
#endif

#ifdef CONFIG_PXA9XX_ACIPC
	platform_device_register(&pxa9xx_device_acipc);
#endif
#ifdef CONFIG_SEC_THERMISTOR
	platform_device_register(&sec_device_thermistor);
#endif
	pxa988_add_thermal();

#ifdef CONFIG_PROC_FS
	/* create proc for gps GPS control */
	create_sirf_proc_file();
	/* create proc for gssp mfp control */
	create_pcm_mfp_proc_file();
#endif
	/* add uart pad wakeup */
	mmp_gpio_edge_add(&uart_rx_pad);
	uart_lpm_cons.name = uart_cons_name;
	pm_qos_add_request(&uart_lpm_cons,
		PM_QOS_CPUIDLE_BLOCK, PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
	init_timer(&uart_constraint_timer);
	uart_constraint_timer.function = uart_timer_handler;

	/* If we have a full configuration then disable any regulators
	 * which are not in use or always_on. */
	regulator_has_full_constraints();
}

MACHINE_START(GOLDEN, "PXA988")
	.map_io		= mmp_map_io,
	.init_early	= pxa988_init_early,
	.init_irq	= pxa988_init_irq,
	.timer		= &pxa988_timer,
	.reserve	= pxa988_reserve,
	.handle_irq	= gic_handle_irq,
	.init_machine	= emeidkb_init,
	.restart	= mmp_arch_reset,
MACHINE_END
