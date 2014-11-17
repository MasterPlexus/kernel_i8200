/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * Created for samsung by Vincent Wan <zswan@marvell.com>,2012/03/31
 */
#include <linux/platform_device.h>
#include <mach/camera.h>
#include <linux/io.h>
#include <linux/pm_qos.h>
#include "../../../drivers/media/video/mmp_camera.h"

#if 1//def CAM_DEBUG 
#define Cam_Printk(msg...) printk(msg)	
#else
#define Cam_Printk(msg...)
#endif

enum {
	ID_S5K4ECGX	= 0,
};

enum cam_power {
	CAM_POWER_OFF	= 0,
	CAM_POWER_ON	= 1
};

/* for s5k sensor power sequence workaround */
struct mmp_camera_dev;
struct pxa_i2c ;
struct soc_camera_device;

struct samsung_camera_data {
	void (*disable_clk)(struct mmp_camera_dev *pdev);
	void (*enable_clk)(struct mmp_camera_dev *pdev);
	void (*i2c_pxa_reset)(struct pxa_i2c *i2c);
	struct pxa_i2c *i2c;
	struct mmp_camera_dev *pcdev;
	struct soc_camera_device *icd;
	int skip_frames;
};

extern struct samsung_camera_data samsung_camera;
extern void  init_samsung_cam(void);
extern struct mmp_cam_pdata mv_cam_data_forssg;
