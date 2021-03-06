/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * Created for samsung by Vincent Wan <zswan@marvell.com>,2012/03/31
 * Add dual camera architecture, add subsensor demo, sr030pc30 support, by Vincent Wan, 2013/2/16.
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
#include <linux/mfd/88pm80x.h>
#include <linux/regulator/machine.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/sd8x_rfkill.h>
#include <linux/regmap.h>
#include <linux/mfd/88pm80x.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/pm_qos.h>
#include <linux/clk.h>
#include <linux/lps331ap.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/workqueue.h>
#include <linux/i2c-gpio.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>

#include <mach/addr-map.h>
#include <mach/clock-pxa988.h>
#include <mach/mmp_device.h>
#include <mach/mfp-pxa986-cs02.h>
#include <mach/irqs.h>
#include <mach/isl29043.h>
#include <mach/pxa988.h>
#include <mach/soc_coda7542.h>
#include <mach/regs-rtc.h>
#include <mach/regs-ciu.h>
#include <plat/pxa27x_keypad.h>
#include <plat/pm.h>
#include <media/soc_camera.h>
#include <mach/isp_dev.h>
#include <mach/gpio-edge.h>
#include <mach/samsung_camera_cs02.h>
#include "common.h"

struct class *camera_class; 

/* Camera sensor PowerDowN pins */
static int pwd_main1, pwd_main2,pwd_sub_en, pwd_sub_rst;


/* ONLY for parallel camera, SSG never use it currently, but __MUST__ keep it here */

static int pxa988_parallel_cam_pin_init(struct device *dev, int on)
{
	return 0;
}

struct mmp_cam_pdata mv_cam_data_forssg = {
	.name = "samsung_camera",
	.dma_burst = 64,
	.mclk_min = 24,
	.mclk_src = 3,
	.mclk_div = 13,
	.init_pin = pxa988_parallel_cam_pin_init,
};

/* ONLY for SSG S5K camera, __MUST__ keep it here */
struct samsung_camera_data samsung_camera ;

#define MCLK_PIN 0
#define GPIO_PIN 1
#define I2C_PIN 2

static int switch_mclk_gpio, switch_i2c_gpioa, switch_i2c_gpiob;
static int  switch_mclk_gpio_mfp(int pinid) {

	unsigned int val;

	unsigned long mclk_mfps[] = {
		GPIO077_CAM_MCLK | MFP_LPM_DRIVE_LOW,
	};

	unsigned long gpio_mfps[] = {
		GPIO077_GPIO_77
	};

	switch_mclk_gpio = mfp_to_gpio(GPIO077_GPIO_77);

	if (pinid == MCLK_PIN) {
			mfp_config(ARRAY_AND_SIZE(mclk_mfps));
			val = mfp_read(switch_mclk_gpio);
			printk("--------MCLK pin Switchs TO mclk,  MFP Setting is :0x%x---------", val);
	} if (pinid == GPIO_PIN) {
			mfp_config(ARRAY_AND_SIZE(gpio_mfps));
			val = mfp_read(switch_mclk_gpio);
			printk("--------MCLK pin Switchs TO gpio,  MFP Setting is :0x%x---------", val);

			if (switch_mclk_gpio && gpio_request(switch_mclk_gpio, "switch_mclk_gpio77")) {
			printk(KERN_ERR "------switch_mclk_gpio_mfp-------Request GPIO failed,"
					"gpio: %d\n", switch_mclk_gpio);
			switch_mclk_gpio = 0;
			return -1;
	}
		gpio_direction_output(switch_mclk_gpio, 0);

		gpio_free(switch_mclk_gpio);

	}

	return 0;

}

static int  switch_i2c_gpio_mfp(int pinid) {

	unsigned int val,val1;

	unsigned long i2c_mfps[] = {
		GPIO079_CAM_SCL | MFP_LPM_FLOAT,
		GPIO080_CAM_SDA | MFP_LPM_FLOAT,
	};

	unsigned long gpio_mfps[] = {
		GPIO079_GPIO_79,
		GPIO080_GPIO_80,
	};

	switch_i2c_gpioa = mfp_to_gpio(GPIO079_GPIO_79);
	switch_i2c_gpiob = mfp_to_gpio(GPIO080_GPIO_80);

	if (pinid == I2C_PIN) {
			mfp_config(ARRAY_AND_SIZE(i2c_mfps));
			val = mfp_read(switch_i2c_gpioa);
			val1 = mfp_read(switch_i2c_gpiob);
			printk("--------Pins Switchs TO I2C,  MFP  Setting is GPIO079 :0x%x, GPIO080 :0x%x---------", val, val1);
	} if (pinid == GPIO_PIN) {
			mfp_config(ARRAY_AND_SIZE(gpio_mfps));
			val = mfp_read(switch_i2c_gpioa);
			val1 = mfp_read(switch_i2c_gpiob);
			printk("--------Pins Switchs TO GPIO,  MFP  Setting is GPIO079 :0x%x, GPIO080 :0x%x---------", val, val1);

			if (switch_i2c_gpioa && gpio_request(switch_i2c_gpioa, "switch_mclk_gpio79")) {
				printk(KERN_ERR "------switch_i2c_gpio_mfp-------Request GPIO failed,"
						"gpio: %d\n", switch_i2c_gpioa);
				switch_i2c_gpioa = 0;
				return -1;
			}

			if (switch_i2c_gpiob && gpio_request(switch_i2c_gpiob, "switch_mclk_gpio80")) {
				printk(KERN_ERR "------switch_i2c_gpio_mfp-------Request GPIO failed,"
						"gpio: %d\n", switch_i2c_gpiob);
				switch_i2c_gpiob = 0;
				return -1;
			}
			
		gpio_direction_output(switch_i2c_gpioa, 0);
		gpio_direction_output(switch_i2c_gpiob, 0);

		gpio_free(switch_i2c_gpioa);
		gpio_free(switch_i2c_gpiob);

	}

	return 0;

}


static void pxa_ccic_enable_mclk(struct mmp_camera_dev *pcdev, 
									enum v4l2_mbus_type bus_type)
{
	struct mmp_cam_pdata *pdata = pcdev->pdev->dev.platform_data;
	struct device *dev = &pcdev->pdev->dev;
	int ctrl1 = 0;
	int mipi;

	switch_mclk_gpio_mfp(MCLK_PIN);

	if (bus_type == V4L2_MBUS_CSI2)
		mipi = MIPI_ENABLE;
	else
		mipi = MIPI_DISABLE;

	pdata->enable_clk(dev, mipi | POWER_ON);
	ccic_reg_write(pcdev, REG_CLKCTRL,
			(pdata->mclk_src << 29) | pdata->mclk_div);
	dev_dbg(dev, "camera: set sensor mclk = %d MHz\n", pdata->mclk_min);

	switch (pdata->dma_burst) {
	case 128:
		ctrl1 = C1_DMAB128;
		break;
	case 256:
		ctrl1 = C1_DMAB256;
		break;
	}
	ccic_reg_write(pcdev, REG_CTRL1, ctrl1 | C1_RESERVED | C1_DMAPOSTED);
	if (bus_type != V4L2_MBUS_CSI2)
		ccic_reg_write(pcdev, REG_CTRL3, 0x4);
}

static void pxa_ccic_disable_mclk(struct mmp_camera_dev *pcdev,
										enum v4l2_mbus_type bus_type)
{
	struct mmp_cam_pdata *pdata = pcdev->pdev->dev.platform_data;
	struct device *dev = &pcdev->pdev->dev;
	int mipi;

	ccic_reg_write(pcdev, REG_CLKCTRL, 0x0);
	/*
	 * Bit[5:1] reserved and should not be changed
	 */
	ccic_reg_write(pcdev, REG_CTRL1, C1_RESERVED);

	if (bus_type == V4L2_MBUS_CSI2)
		mipi = MIPI_ENABLE;
	else
		mipi = MIPI_DISABLE;

	pdata->enable_clk(dev, mipi | POWER_OFF);

	switch_mclk_gpio_mfp(GPIO_PIN);
}

static int subsensor_pin_init(void){

	return 0;
}

static int sr352_pin_init(void){

	pwd_main1 = mfp_to_gpio(MFP_PIN_GPIO14);
	pwd_main2 = mfp_to_gpio(MFP_PIN_GPIO15);
		
	if( (!pwd_main1) || (! pwd_main2)) {
		printk(KERN_ERR "mfp_to_gpio  failed,"
				"gpio: pwd_main1 :%d, pwd_main2:%d\n", pwd_main1, pwd_main2);	
		return -1;
	}

	/* Camera hold these GPIO forever, should not be accquired by others */
	if (pwd_main1 && gpio_request(pwd_main1, "CAM_HI_SENSOR_PWD_14")) {
		printk(KERN_ERR "Request GPIO failed,"
				"gpio: %d\n", pwd_main1);
		pwd_main1 = 0;
		return -1;
	}
	if (pwd_main2 && gpio_request(pwd_main2, "CAM_HI_SENSOR_PWD_15")) {
		printk(KERN_ERR "Request GPIO failed,"
				"gpio: %d\n", pwd_main2);
		pwd_main2 = 0;
		return -1;
}

	return 0;
}

static int sr352_power(struct device *dev, int flag)
{
	static struct regulator *vcamera_ana;         // SENSOR IO : 1.8V
	static struct regulator *vcamera_ana_1;     // SENSOR AVDD : 2.8V
	static struct regulator *vcamera_vbuck5;   // 5M CORE : 1.2V
	                                                                   // VT CORE : 1.8v //pwd_sub_en_power

	if (!vcamera_vbuck5) {
		vcamera_vbuck5 = regulator_get(dev, "v_cam_c");
		if (IS_ERR(vcamera_vbuck5)) {
			vcamera_vbuck5 = NULL;
			pr_err(KERN_ERR "Enable vcamera_vbuck5 failed!\n");
			return -EIO;
		}
	}

	if (!vcamera_ana) {
		vcamera_ana = regulator_get(dev, "v_cam_io");
		if (IS_ERR(vcamera_ana)) {
			vcamera_ana = NULL;
			pr_err(KERN_ERR "Enable vcamera_ana failed!\n");
			return -EIO;
		}
	}

	if (!vcamera_ana_1) {
		vcamera_ana_1 = regulator_get(dev, "v_cam_avdd");
		if (IS_ERR(vcamera_ana_1)) {
			vcamera_ana_1 = NULL;
			pr_err(KERN_ERR "Enable vcamera_ana_1 failed!\n");
			return -EIO;
		}
	}
	
	if (flag) {
		
		Cam_Printk("---camera power ON ----------\n");
		/* Sensor IO : 1.8V ON */
		regulator_set_voltage(vcamera_ana, 1800000, 1800000);
		regulator_enable(vcamera_ana);
		/* Sensor AVDD : 2.8V ON */
		regulator_set_voltage(vcamera_ana_1, 2800000, 2800000);
		regulator_enable(vcamera_ana_1);
		/* Sensor Core : 1.2V ON */
		regulator_set_voltage(vcamera_vbuck5, 1250000, 1250000);
		regulator_enable(vcamera_vbuck5);

		msleep(2);
		
		Cam_Printk("#####[DHL] camera power ON DONE #####\n");
		
		/* Ccic Mclk enbale, enable/disable clk api is in mmp_camera.c */
		pxa_ccic_enable_mclk(samsung_camera.pcdev, V4L2_MBUS_CSI2);	

		/* Sensor STBY Enable */
		gpio_direction_output(pwd_main1, 1);
		msleep(2);

		/* Sensor Reset Enable*/		
		gpio_direction_output(pwd_main2, 0);	
		msleep(10);
		gpio_direction_output(pwd_main2, 1);	

		
		Cam_Printk("#####[DHL] camera power Sequence DONE #####\n");

		/*for s5k power off maybe pull down the i2c data pin, so we have to reset i2c controller */
		samsung_camera.i2c_pxa_reset(samsung_camera.i2c);
		
		Cam_Printk("#####[DHL] camera power ONSequence DONE #####\n");
	}else {
		Cam_Printk("---camera power OFF ----------\n");
		/* Sensor Reset Disable*/		
		gpio_direction_output(pwd_main2, 0);	
		msleep(2);
		
		/* Ccic Mclk enbale, enable/disable clk api is in mmp_camera.c */
		pxa_ccic_disable_mclk(samsung_camera.pcdev, V4L2_MBUS_CSI2);
		
		/* Sensor STBY Disable */
		gpio_direction_output(pwd_main1, 0);	

		/*  Sensor Core : 1.2V OFF  */
		regulator_disable(vcamera_vbuck5);

		/* Sensor AVDD : 2.8V OFF */
		regulator_disable(vcamera_ana_1);
		
		/* Sensor IO : 1.8V OFF */
		regulator_disable(vcamera_ana);

	}

	return 0;
}


static struct sensor_board_data sensor_data[] = {
	[ID_SR352] = {
		.mount_pos	= SENSOR_USED | SENSOR_POS_BACK | SENSOR_RES_HIGH,
		.bus_type	= V4L2_MBUS_CSI2,
		.bus_flag	= V4L2_MBUS_CSI2_1_LANE, /* s5k used 2 lanes */
		.dphy		= {0x1023, 0x11, 0x0900},
		.mipi_enabled	= 0,
		.dphy3_algo = 0,
		.plat		= &mv_cam_data_forssg,
	},
};

static struct i2c_board_info camera_i2c[] = {
	{
		I2C_BOARD_INFO("samsung_mainsensor", (0x40 >> 1)),
	},

};

static struct soc_camera_link iclink[] = {
	[ID_SR352] = {
		.bus_id			= 0, /* Must match with the camera ID */
		.board_info		= &camera_i2c[0],
		.i2c_adapter_id		= 0,
		.power = sr352_power,
		.flags = V4L2_MBUS_CSI2_1_LANE,
		.module_name		= "samsung_mainsensor",
		.priv		= &sensor_data[ID_SR352],
	},
};

static struct platform_device __attribute__((unused)) camera[] = {
	[ID_SR352] = {
		.name	= "soc-camera-pdrv",
		.id	= 1,
		.dev	= {
			.platform_data = &iclink[ID_SR352],
		},
	},	
};

static struct platform_device *camera_platform_devices[] = {
#if defined(CONFIG_SOC_CAMERA_SR352)
	&camera[ID_SR352],
#endif
};


 ssize_t rear_camera_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("rear_camera_type: SOC\n");

	return sprintf(buf, "SOC");
}
 
  ssize_t rear_camerafw_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("rear_camerafw_type: SR352\n");

	return sprintf(buf, "SR352");
}

 ssize_t rear_camera_vendorid(struct device *dev, struct device_attribute *attr, char *buf)
{

    printk("rear_camera_vendorid\n");

    return sprintf(buf, "0xffff\n");
}
static DEVICE_ATTR(rear_type, S_IRUGO | S_IWUSR | S_IWGRP | S_IXOTH,  rear_camera_type, NULL);
static DEVICE_ATTR(rear_camfw, S_IRUGO | S_IWUSR | S_IWGRP | S_IXOTH,  rear_camerafw_type, NULL);
static DEVICE_ATTR(rear_vendorid, S_IRUGO | S_IWUSR | S_IWGRP | S_IXOTH,  rear_camera_vendorid, NULL);

void __init init_samsung_cam(void)
{
	int ret;
	struct device *dev_r;

	ret= sr352_pin_init();
	if (ret) {
		Cam_Printk("---error!!!!-----sr352_pin_init --failed!!!--------\n");
		return;
	}

	platform_add_devices(ARRAY_AND_SIZE(camera_platform_devices));
	pxa988_add_cam(&mv_cam_data_forssg);

	camera_class = class_create(THIS_MODULE, "camera");
	
	if (IS_ERR(camera_class)) 
	{
	 printk("Failed to create camera_class!\n");
	 return PTR_ERR( camera_class );
        }
	
	dev_r = device_create(camera_class, NULL, 0, "%s", "rear");		
	if (device_create_file(dev_r, &dev_attr_rear_type) < 0)
	 printk("Failed to create device file(%s)!\n", dev_attr_rear_type.attr.name);
	if (device_create_file(dev_r, &dev_attr_rear_camfw) < 0)
	 printk("Failed to create device file(%s)!\n", dev_attr_rear_camfw.attr.name);
	if (device_create_file(dev_r, &dev_attr_rear_vendorid) < 0)
	 printk("Failed to create device file(%s)!\n", dev_attr_rear_vendorid.attr.name);

}
