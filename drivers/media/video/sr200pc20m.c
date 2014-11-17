/*
 * A V4L2 driver for siliconfile SR200PC20M cameras.
 * 
 * Copyright 2006 One Laptop Per Child Association, Inc.  Written
 * by Jonathan Corbet with substantial inspiration from Mark
 * McClelland's ovcamchip code.
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *jpeg
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 * 
 * Create SR200PC20M driver from SR030PC30 driver by
 * Vincent Wan <zswan@marvell.com> for Marvell CS02 CU project.
 */
#include <linux/init.h>
#include <linux/module.h>



#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <mach/camera.h>

#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>

#include <media/v4l2-subdev.h>
#include <mach/gpio.h>
#include <mach/camera.h>

#include "sr200pc20m.h"

#if defined(CONFIG_MACH_GOYA)
#include "sr200pc20m_regs_goya.h"
#else
#include "sr200pc20m_regs_50hz.h"
#endif

MODULE_AUTHOR("Jonathan Corbet <corbet@lwn.net>");
MODULE_DESCRIPTION("A low-level driver for siliconfile SR200PC20M sensors");
MODULE_LICENSE("GPL");

#define to_sr200pc20m(sd)		container_of(sd, struct sr200pc20m_info, subdev)

#define sr200pc20m_WRT_LIST(B, A)	\
	sr200pc20m_i2c_wrt_list(B, A, (sizeof(A) / sizeof(A[0])), #A);


static const struct sr200pc20m_datafmt sr200pc20m_colour_fmts[] = {
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
};


#define CAM_DEBUG 

#ifdef CAM_DEBUG 
#define Cam_Printk(msg...) printk(msg)	
#else
#define Cam_Printk
#endif

/*
 * Basic window sizes.  These probably belong somewhere more globally
 * useful.
 */

static int sr200pc20m_cam_state;

#define QXGA_WIDTH		2048
#define QXGA_HEIGHT	1536

#define Wide2_4M_WIDTH		2048
#define Wide2_4M_HEIGHT	1232

#define UXGA_WIDTH		1600
#define UXGA_HEIGHT     	1200

#define Wide1_5M_WIDTH		1600
#define Wide1_5M_HEIGHT	 960

#define HD_WIDTH		1280
#define HD_HEIGHT		720

#define WIDTH_800	800
#define HEIGHT_600	600

#define Wide4K_WIDTH	800
#define Wide4K_HEIGHT	480

#define WIDTH_704		704
#define HEIGHT_576		576

#define VGA_WIDTH		640
#define VGA_HEIGHT		480

#define CIF_WIDTH		352
#define CIF_HEIGHT		288

#define QVGA_WIDTH		320
#define QVGA_HEIGHT	240

#define QCIF_WIDTH		176
#define QCIF_HEIGHT		144

#define QQVGA_WIDTH	160
#define QQVGA_HEIGHT	120

/*
 * Our nominal (default) frame rate.
 */

int sr200pc20m_s_exif_info(struct i2c_client *client);
static int sr200pc20m_regs_table_write(struct i2c_client *c, char *name);
static int sr200pc20m_t_fps(struct i2c_client *client, int value);
static void sr200pc20m_s_framerate( struct i2c_client *c, int fps, int preview_size );

#define SR200PC20M_FRAME_RATE 30

#define REG_MIDH	0x1c	/* Manuf. ID high */

#define   CMATRIX_LEN 6

/*Heron Tuning*/
//#define CONFIG_LOAD_FILE

#define FALSE 0
#define TRUE 1

static int gDTP_flag = FALSE;
static int g_Framerate = 0;


/*
 * Information we maintain about a known sensor.
 */

struct sr200pc20m_sensor sr200pc20m = {
	.timeperframe = {
		.numerator    = 1,
		.denominator  = 24,
	},
	.fps			= 24,
	//.bv			= 0,
	.state			= SR200PC20M_STATE_PREVIEW,
	.mode			= SR200PC20M_MODE_CAMERA,
	.preview_size		= PREVIEW_SIZE_800_600,
	.capture_size		= CAPTURE_SIZE_1600_1200,
	.detect			= SENSOR_NOT_DETECTED,
	.focus_mode		= SR200PC20M_AF_SET_NORMAL,
	.effect			= EFFECT_OFF,
	.iso			= ISO_AUTO,
	.photometry		= METERING_CENTER,
	.ev			= EV_DEFAULT,
	//.wdr			= SR200PC20M_WDR_OFF,
	.contrast		= CONTRAST_DEFAULT,
	.saturation		= SATURATION_DEFAULT,
	.sharpness		= SHARPNESS_DEFAULT,
	.wb			= WB_AUTO,
	//.isc 			= SR200PC20M_ISC_STILL_OFF,
	.scene			= SCENE_OFF,
	.aewb			= AWB_AE_UNLOCK,
	//.antishake		= SR200PC20M_ANTI_SHAKE_OFF,
	//.flash_capture	= SR200PC20M_FLASH_CAPTURE_OFF,
	//.flash_movie		= SR200PC20M_FLASH_MOVIE_OFF,
	.quality		= QUALITY_SUPERFINE, 
	//.zoom			= SR200PC20M_ZOOM_1P00X,
	.thumb_offset		= 0,
	.yuv_offset		= 0,
	.jpeg_main_size		= 0,
	.jpeg_main_offset	= 0,
	.jpeg_thumb_size	= 0,
	.jpeg_thumb_offset	= 0,
	.jpeg_postview_offset	= 0, 
	.jpeg_capture_w		= JPEG_CAPTURE_WIDTH,
	.jpeg_capture_h		= JPEG_CAPTURE_HEIGHT,
	.check_dataline		= 0,
	.exif_info={
		.exposure_time.denominal =0,
		.exposure_time.inumerator =0,
		.iso_speed_rationg =0,
			},
	.cam_mode           =  CAMERA_MODE,
};

extern struct sr200pc20m_platform_data sr200pc20m_platform_data0;

struct sr200pc20m_format_struct;  /* coming later */
struct sr200pc20m_info {
	struct sr200pc20m_format_struct *fmt;  /* Current format */
	unsigned char sat;		/* Saturation value */
	int hue;			/* Hue value */
	struct v4l2_subdev subdev;
	int model;	/* V4L2_IDENT_xxx* codes from v4l2-chip-ident.h */
	u32 pixfmt;
	struct i2c_client *client;
	struct soc_camera_device icd;

};

static int sr200pc20m_write_byte(struct i2c_client *c, unsigned char reg,
		unsigned char value)
{
	int retry = 3, ret;
	
	if (reg == 0xfe)
	{
		msleep(value);  /* Wait for reset to run */
		return 0;
	}
	
to_retry:
	ret = i2c_smbus_write_byte_data(c, reg, value);
	if (ret < 0) {
			printk("<##############################>ret : %d , retry: %d \n", ret, retry);
			if (retry > 0) {
					retry --;
					goto to_retry;
				}
			}
	return ret;
}

/**
 * sr200pc20m_i2c_read_multi: Read (I2C) multiple bytes to the camera sensor
 * @client: pointer to i2c_client
 * @cmd: command register
 * @w_data: data to be written
 * @w_len: length of data to be written
 * @r_data: buffer where data is read
 * @r_len: number of bytes to read
 *
 * Returns 0 on success, <0 on error
 */

/**
 * sr200pc20m_i2c_read: Read (I2C) multiple bytes to the camera sensor
 * @client: pointer to i2c_client
 * @cmd: command register
 * @data: data to be read
 *
 * Returns 0 on success, <0 on error
 */

extern camera_antibanding_val; /*default : CAM_BANDFILTER_50HZ = 2*/

int camera_antibanding_get (void) {
		return camera_antibanding_val;
}

static int sr200pc20m_copy_files_for_60hz(void){

#define COPY_FROM_60HZ_TABLE(TABLE_NAME, ANTI_BANDING_SETTING) \
	memcpy (TABLE_NAME, TABLE_NAME##_##ANTI_BANDING_SETTING, \
			sizeof(TABLE_NAME))

	printk("%s: Enter \n",__func__);

	COPY_FROM_60HZ_TABLE (sr200pc20m_Init_Reg, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_preview_640_480, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_preview_704_576, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_preview_800_600, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_15fps, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_24fps, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_7fps, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_modeOff, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_SceneOff, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Landscape, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Party, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Sunset, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Dawn, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Fall, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Nightshot_Normal, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Nightshot_Dark, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Backlight, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_Candle, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_640x480_24fps, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_704x576_24fps, 60hz);
	COPY_FROM_60HZ_TABLE (sr200pc20m_recording_800x600_24fps, 60hz);

	printk("%s: copy done!\n", __func__);
}

static int sr200pc20m_check_table_size_for_60hz(void){
#define IS_SAME_NUM_OF_ROWS(TABLE_NAME) \
	(sizeof(TABLE_NAME) == sizeof(TABLE_NAME##_60hz))

	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Init_Reg) ) return (-1);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_preview_640_480) ) return (-2);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_preview_704_576) ) return (-3);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_preview_800_600) ) return (-4);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_15fps) ) return (-5);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_24fps) ) return (-6);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_7fps) ) return (-7);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_modeOff) ) return (-8);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_SceneOff) ) return (-9);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Landscape) ) return (-10);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Party) ) return (-11);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Sunset) ) return (-12);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Dawn) ) return (-13);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Fall) ) return (-14);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Nightshot_Normal) ) return (-15);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Nightshot_Dark) ) return (-16);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Backlight) ) return (-17);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_Candle) ) return (-18);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_640x480_24fps) ) return (-19);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_704x576_24fps) ) return (-20);
	if ( !IS_SAME_NUM_OF_ROWS(sr200pc20m_recording_800x600_24fps) ) return (-21);

	printk("%s: Success !\n", __func__);
	return 0;
}




static int sr200pc20m_i2c_read( struct i2c_client *client, unsigned char subaddr, unsigned char *data)
{
	unsigned char buf[1];
	struct i2c_msg msg = {client->addr, 0, 1, buf};

	int err = 0;
	buf[0] = subaddr;

	if (!client->adapter)
		return -EIO;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (unlikely(err < 0))
		return -EIO;

	msg.flags = I2C_M_RD;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (unlikely(err < 0))
		return -EIO;
	/*
	 * Data comes in Little Endian in parallel mode; So there
	 * is no need for byte swapping here
	 */

	*data = buf[0];

	return err;
}

static int32_t sr200pc20m_i2c_write_16bit( struct i2c_client *client, u16 packet)
{
	int32_t rc = -EFAULT;
	int retry_count = 0;

	unsigned char buf[2];

	struct i2c_msg msg;

	buf[0] = (u8) (packet >> 8);
	buf[1] = (u8) (packet & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = buf;

#if defined(CAM_I2C_DEBUG)
	printk("I2C CHIP ID=0x%x, DATA 0x%x 0x%x\n",
			client->addr, buf[0], buf[1]);
#endif

	do {
		rc = i2c_transfer(client->adapter, &msg, 1);
		if (rc == 1)
			return 0;
		retry_count++;
		printk("i2c transfer failed, retrying %x err:%d\n",
		       packet, rc);
		msleep(3);

	} while (retry_count <= 5);

	return 0;
}

static int sr200pc20m_i2c_wrt_list( struct i2c_client *client, const u16 *regs,
	int size, char *name)
{
	int i;
	u8 m_delay = 0;

	u16 temp_packet;


	CAM_DEBUG("%s, size=%d", name, size);
	for (i = 0; i < size; i++) {
		temp_packet = regs[i];

		if ((temp_packet & SR200PC20M_DELAY) == SR200PC20M_DELAY) {
			m_delay = temp_packet & 0xFF;
			printk("delay = %d", m_delay*10);
			msleep(m_delay*10);/*step is 10msec*/
			continue;
		}

		if (sr200pc20m_i2c_write_16bit(client,temp_packet) < 0) {
			printk("fail(0x%x, 0x%x:%d)",
					client->addr, temp_packet, i);
			return -EIO;
		}
		/*udelay(10);*/
	}

	return 0;
}

static int sr200pc20m_reg_read_and_check(struct i2c_client *client, 
								unsigned char pagemode, unsigned char addr)
{
	unsigned char val = 0xFF;

	sr200pc20m_write_byte(client,0x03,pagemode);//Vincent add here, for p0
	sr200pc20m_i2c_read(client, addr, &val);	
	
	printk("-----------sr200pc20m_reg_read_check------pagemode:0x%x, reg addr:0x%x, value:0x%x------\n", pagemode, addr, val);

	return val;
}


static int sr200pc20m_read(struct i2c_client *c, u16 reg, u16 *value)
{
	int ret;

	ret = i2c_smbus_read_byte_data(c, reg);
	if (ret >= 0)
		*value = (unsigned char) ret;
	return ret;
}

static int sr200pc20m_write(struct i2c_client *c, u16 reg,  u16 value)
{
	int ret=0; 

	if(reg == 0xff)
	{
		msleep(value*10);  /* Delay 100ms */
		return 0;
	}

	ret = i2c_smbus_write_byte_data(c, reg, value);

	return ret;
}

static int sr200pc20m_write_regs(struct i2c_client *c, tagCamReg16_t *vals, u32 reg_length, char *name)
{
	int i = 0, ret=0;

#ifdef CONFIG_LOAD_FILE
	printk(KERN_NOTICE "======[Length %d : Line %d]====== \n", reg_length, __LINE__);
	ret = sr200pc20m_regs_table_write(c, name);
#else

	for (i = 0; i < reg_length; i++) {
		ret = sr200pc20m_write(c, vals[i].value>>8, vals[i].value & 0x00FF);
		if (ret < 0){
			printk(KERN_NOTICE "======[sr200pc20m_write_array %d]====== \n", ret );	
			return ret;
		}
	}
	return -1;
#endif
}

#ifdef CONFIG_LOAD_FILE

#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

static char *sr200pc20m_regs_table = NULL;

static int sr200pc20m_regs_table_size;

static int sr200pc20m_regs_table_init(void)
{
	struct file *filp;

	char *dp;
	long l;
	loff_t pos;
	int ret;
	mm_segment_t fs = get_fs();

	printk("***** %s %d\n", __func__, __LINE__);

	set_fs(get_ds());

	filp = filp_open("/sdcard/sr200pc20m_regs_50hz.h", O_RDONLY, 0);
	if (IS_ERR(filp)) {
		printk("***** file open error\n");
		return 1;
	}
	else
		printk(KERN_ERR "***** File is opened \n");
	

	l = filp->f_path.dentry->d_inode->i_size;	

	printk("l = %ld\n", l);

	dp = kmalloc(l, GFP_KERNEL);
	if (dp == NULL) {
		printk("*****Out of Memory\n");
		filp_close(filp, current->files);
		return 1;
	}
 
	pos = 0;

	memset(dp, 0, l);

	ret = vfs_read(filp, (char __user *)dp, l, &pos);
	if (ret != l) {
		printk("*****Failed to read file ret = %d\n", ret);
		kfree(dp);
		filp_close(filp, current->files);
		return 1;
	}

	filp_close(filp, current->files);
	set_fs(fs);

	sr200pc20m_regs_table = dp;
	sr200pc20m_regs_table_size = l;
	*((sr200pc20m_regs_table + sr200pc20m_regs_table_size) - 1) = '\0';

	printk("*****Compeleted %s %d\n", __func__, __LINE__);
	return 0;
}

void sr200pc20m_regs_table_exit(void)
{
	/* release allocated memory when exit preview */
	if (sr200pc20m_regs_table) {
		kfree(sr200pc20m_regs_table);
		sr200pc20m_regs_table = NULL;
		sr200pc20m_regs_table_size = 0;
	}
	else
		printk("*****sr200pc20m_regs_table is already null\n");
	
	printk("*****%s done\n", __func__);

}

static int sr200pc20m_regs_table_write(struct i2c_client *c, char *name)
{
	char *start, *end, *reg;//, *data;	
	unsigned short value;
	char data_buf[7];

	value = 0;

	printk("*****%s entered.\n", __func__);

	*(data_buf + 6) = '\0';

	start = strstr(sr200pc20m_regs_table, name);

	end = strstr(start, "};");

	while (1) {	
		/* Find Address */	
		reg = strstr(start,"0x");		
		if (reg)
			start = (reg + 7);
		if ((reg == NULL) || (reg > end))
			break;

		/* Write Value to Address */	
			memcpy(data_buf, (reg), 6);	
			value = (unsigned short)simple_strtoul(data_buf, NULL, 16); 
			//printk("value 0x%04x\n", value);

			{
				if(sr200pc20m_write(c, value>>8, value & 0x00FF) < 0 )
				{
					printk("<=PCAM=> %s fail on sensor_write\n", __func__);
				}
			}
	}
	printk(KERN_ERR "***** Writing [%s] Ended\n",name);

	return 0;
}

#endif  /* CONFIG_LOAD_FILE */

static int sr200pc20m_detect(struct i2c_client *client)
{
	unsigned char ID = 0xFFFF;
	int rc = 0;

	printk("-----------sr200pc20m_detect------client->addr:0x%x------\n", client->addr);
	
	ID = sr200pc20m_reg_read_and_check(client, 0x00, 0x04);
	
	if(ID == 0xb4) 
	{
		printk(SR200PC20M_MOD_NAME"========================================\n");
		printk(SR200PC20M_MOD_NAME"   [2M CAM] Sensor ID : 0x%04X\n", ID);
		printk(SR200PC20M_MOD_NAME"========================================\n");
	} 
	else 
	{
		printk(SR200PC20M_MOD_NAME"-------------------------------------------------\n");
		printk(SR200PC20M_MOD_NAME"   [2M CAM] Sensor Detect Failure !!\n");
		printk(SR200PC20M_MOD_NAME"   ID : 0x%04X[ID should be 0xc2]\n", ID);
		printk(SR200PC20M_MOD_NAME"-------------------------------------------------\n");
		return -EINVAL;
	}	
	if (camera_antibanding_get() == 3) { /* CAM_BANDFILTER_60HZ  = 3 */
		rc = sr200pc20m_check_table_size_for_60hz();
		if(rc != 0) {
			printk(KERN_ERR "%s: Fail - the table num is %d \n", __func__, rc);
		}
		sr200pc20m_copy_files_for_60hz();
	}
	return 0;
}

static void sr200pc20m_reset(struct i2c_client *client)
{
	msleep(1);
}

static int sr200pc20m_init(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	int result =0;
	
#ifdef CONFIG_LOAD_FILE
	result = sr200pc20m_regs_table_init();
	if (result > 0)
	{		
		Cam_Printk(KERN_ERR "***** sr200pc20m_regs_table_init  FAILED. Check the Filie in MMC\n");
		return result;
	}
	result =0;
#endif
	
	sr200pc20m_write_regs(c,sr200pc20m_Init_Reg,ARRAY_SIZE(sr200pc20m_Init_Reg),"sr200pc20m_Init_Reg");
	//sr200pc20m_write_regs(c,sr200pc20m_50hz_setting,ARRAY_SIZE(sr200pc20m_50hz_setting),"sr200pc20m_50hz_setting");
	g_Framerate = 0;

	sensor->state 		= SR200PC20M_STATE_PREVIEW;
	sensor->mode 		= SR200PC20M_MODE_CAMERA;
	sensor->effect		= EFFECT_OFF;
	sensor->iso 		= ISO_AUTO;	
	sensor->photometry	= METERING_CENTER;	
	sensor->ev		= EV_DEFAULT;
	sensor->contrast	= CONTRAST_DEFAULT;
	sensor->saturation	= SATURATION_DEFAULT;	
	sensor->sharpness	= SHARPNESS_DEFAULT;
	sensor->wb		= WB_AUTO;
	sensor->scene		= SCENE_OFF;
	sensor->quality		= QUALITY_SUPERFINE;
	sensor->fps			= FPS_auto;
	sensor->pix.width		=VGA_WIDTH;
	sensor->pix.height		=VGA_HEIGHT;
	sensor->pix.pixelformat = V4L2_PIX_FMT_YUV420;
	sensor->initial			= SR200PC20M_STATE_INITIAL;
	
	Cam_Printk(KERN_NOTICE "===sr200pc20m_init===[%s  %d]====== \n", __FUNCTION__, __LINE__);

	return result;
}


/*
 * Store information about the video data format.  The color matrix
 * is deeply tied into the format, so keep the relevant values here.
 * The magic matrix nubmers come from OmniVision.
 */
static struct sr200pc20m_format_struct {
	__u8 *desc;
	__u32 pixelformat;
	int bpp;   /* bits per pixel */
} sr200pc20m_formats[] = {
	{
		.desc		= "YUYV 4:2:2",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.bpp		= 16,
	},
	{
		.desc		= "YUYV422 planar",
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
		.bpp		= 16,
	},
	{
		.desc           = "YUYV 4:2:0",
		.pixelformat    = V4L2_PIX_FMT_YUV420,
		.bpp            = 12,
	},
	{
		.desc           = "JFIF JPEG",
		.pixelformat    = V4L2_PIX_FMT_JPEG,
		.bpp            = 16,
	},
};
#define N_SR200PC20M_FMTS ARRAY_SIZE(sr200pc20m_formats)
/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sr200pc20m_win_size {
	int	width;
	int	height;
} sr200pc20m_win_sizes[] = {
	/* QXGA */
	{
		.width		= QXGA_WIDTH,
		.height		= QXGA_HEIGHT,
	},
	/* 2048x1232 */
	{
		.width		= Wide2_4M_WIDTH,
		.height		= Wide2_4M_HEIGHT,
	},
	/* UXGA */
	{
		.width		= UXGA_WIDTH,
		.height		= UXGA_HEIGHT,
	},
	/* 1280x720 */
	{
		.width		= HD_WIDTH,
		.height		= HD_HEIGHT,
	},
	/* 800x600 */
	{
		.width		= WIDTH_800,
		.height		= HEIGHT_600,
	},
	/* 800x480 */
	{
		.width		= Wide4K_WIDTH,
		.height		= Wide4K_HEIGHT,
	},
	/* 720x480 */
	{
		.width		= 720,
		.height		= 480,
	},
	/* 704x576 */
	{
		.width		= WIDTH_704,
		.height		= HEIGHT_576,
	},
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
	},
	/* CIF */
	{
		.width		= CIF_WIDTH,
		.height		= CIF_HEIGHT,
	},
	/* QVGA */
	{
		.width		= QVGA_WIDTH,
		.height		= QVGA_HEIGHT,
	},
	/* QCIF */
	{
		.width		= QCIF_WIDTH,
		.height		= QCIF_HEIGHT,
	},
	/* QQVGA */
	{
		.width		= QQVGA_WIDTH,
		.height		= QQVGA_HEIGHT,
	},


};

static struct sr200pc20m_win_size  sr200pc20m_win_sizes_jpeg[] = {
	/* QXGA */
	{
		.width		= QXGA_WIDTH,
		.height		= QXGA_HEIGHT,
	},
	/* 2048x1232 */
	{
		.width		= Wide2_4M_WIDTH,
		.height		= Wide2_4M_HEIGHT,
	},
	/* UXGA */
	{
		.width		= UXGA_WIDTH,
		.height		= UXGA_HEIGHT,
	},
	/* 1280x720 */
	{
		.width		= HD_WIDTH,
		.height		= HD_HEIGHT,
	},
	/* 800x600 */
	{
		.width		= WIDTH_800,
		.height		= HEIGHT_600,
	},
	/* 800x480 */
	{
		.width		= Wide4K_WIDTH,
		.height		= Wide4K_HEIGHT,
	},
	/* 720x480 */
	{
		.width		= 720,
		.height		= 480,
	},
	/* 704x576 */
	{
		.width		= WIDTH_704,
		.height		= HEIGHT_576,
	},
	/* VGA */
	{
		.width		= VGA_WIDTH,
		.height		= VGA_HEIGHT,
	},
	/* CIF */
	{
		.width		= CIF_WIDTH,
		.height		= CIF_HEIGHT,
	},
	/* QVGA */
	{
		.width		= QVGA_WIDTH,
		.height		= QVGA_HEIGHT,
	},
	/* QCIF */
	{
		.width		= QCIF_WIDTH,
		.height		= QCIF_HEIGHT,
	},
	/* QQVGA */
	{
		.width		= QQVGA_WIDTH,
		.height		= QQVGA_HEIGHT,
	},
};

/* Find a data format by a pixel code in an array */
static const struct sr200pc20m_datafmt *sr200pc20m_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct sr200pc20m_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

#define N_WIN_SIZES (ARRAY_SIZE(sr200pc20m_win_sizes))


static int sr200pc20m_querycap(struct i2c_client *c, struct v4l2_capability *argp)
{
	if(!argp){
		printk(KERN_ERR" argp is NULL %s %d \n", __FUNCTION__, __LINE__);
		return -EINVAL;
	}
	strcpy(argp->driver, "sr200pc20m");
	strcpy(argp->card, "TD/TTC");
	return 0;
}

static int sr200pc20m_enum_fmt(struct v4l2_subdev *sd,
		unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sr200pc20m_colour_fmts))
		return -EINVAL;
	*code = sr200pc20m_colour_fmts[index].code;
	return 0;
}

static int sr200pc20m_enum_fsizes(struct v4l2_subdev *sd,
				struct v4l2_frmsizeenum *fsize)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (!fsize)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;

	/* abuse pixel_format, in fact, it is xlate->code*/
	switch (fsize->pixel_format) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
		if (fsize->index >= ARRAY_SIZE(sr200pc20m_win_sizes)) {
			dev_warn(&client->dev,
				"sr200pc20m unsupported size %d!\n", fsize->index);
			return -EINVAL;
		}
		fsize->discrete.height = sr200pc20m_win_sizes[fsize->index].height;
		fsize->discrete.width = sr200pc20m_win_sizes[fsize->index].width;
		break;

	default:
		dev_err(&client->dev, "sr200pc20m unsupported format!\n");
		return -EINVAL;
	}
	return 0;
}

static int sr200pc20m_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct sr200pc20m_datafmt *fmt;
	int i;

	fmt = sr200pc20m_find_datafmt(mf->code, sr200pc20m_colour_fmts,
				   ARRAY_SIZE(sr200pc20m_colour_fmts));
	if (!fmt) {
		dev_err(&client->dev, "sr200pc20m unsupported color format!\n");
		return -EINVAL;
	}

	mf->field = V4L2_FIELD_NONE;

	switch (mf->code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
		/* enum the supported sizes*/
		for (i = 0; i < ARRAY_SIZE(sr200pc20m_win_sizes); i++)
			if (mf->width == sr200pc20m_win_sizes[i].width
				&& mf->height == sr200pc20m_win_sizes[i].height)
				break;

		if (i >= ARRAY_SIZE(sr200pc20m_win_sizes)) {
			dev_err(&client->dev, "sr200pc20m unsupported window"
				"size, w%d, h%d!\n", mf->width, mf->height);
			return -EINVAL;
		}
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		break;

	default:
		dev_err(&client->dev, "sr200pc20m doesn't support code"
				"%d\n", mf->code);
		break;
	}
	return 0;
}

/*
 * Set a format.
 */

static int sr200pc20m_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	const struct sr200pc20m_datafmt *fmt;
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value =sensor->preview_size;

	printk("[DHL]sr200pc20m_s_fmt..!!! \n");
	printk("[DHL]mf->code : [%d] \n",mf->code);
	printk("[DHL]mf->width : [%d] \n",mf->width);

	fmt =sr200pc20m_find_datafmt(mf->code,sr200pc20m_colour_fmts,
				   ARRAY_SIZE(sr200pc20m_colour_fmts));
	if (!fmt) {
		dev_err(&c->dev, "sr200pc20m unsupported color format!\n");
		return -EINVAL;
	}
	
	switch (mf->code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
		sensor->pix.pixelformat = V4L2_PIX_FMT_YUV422P;
		if(sr200pc20m_cam_state == SR200PC20M_STATE_PREVIEW)
		{
			switch (mf->width)
			{							
				case VGA_WIDTH:
					sensor->preview_size = PREVIEW_SIZE_640_480;
					if( g_Framerate == 24 ) {
						sr200pc20m_s_framerate(c,24,PREVIEW_SIZE_640_480);
						Cam_Printk(KERN_ERR"choose VGA(640x480) setting + 24 fps \n");
					}
					else {
						sr200pc20m_s_framerate(c,g_Framerate,PREVIEW_SIZE_640_480);
						sr200pc20m_write_regs(c,sr200pc20m_preview_640_480,ARRAY_SIZE(sr200pc20m_preview_640_480),"sr200pc20m_preview_640_480");
						Cam_Printk(KERN_ERR"choose VGA(640x480) setting \n");
					}
					break;

				case WIDTH_704:
					sensor->preview_size = PREVIEW_SIZE_704_576;
					if( g_Framerate == 24 ) {
						sr200pc20m_s_framerate(c,15,PREVIEW_SIZE_704_576);
						sr200pc20m_write_regs(c,sr200pc20m_preview_704_576,ARRAY_SIZE(sr200pc20m_preview_704_576),"sr200pc20m_preview_704_576");			
						Cam_Printk(KERN_ERR"choose VGA(704x576) setting + 24 fps \n");
					}
					else {
						sr200pc20m_s_framerate(c,g_Framerate,PREVIEW_SIZE_704_576);
						sr200pc20m_write_regs(c,sr200pc20m_preview_704_576,ARRAY_SIZE(sr200pc20m_preview_704_576),"sr200pc20m_preview_704_576");			
						Cam_Printk(KERN_ERR"choose mms(704x576) setting \n");
					}
					break;
					
				case WIDTH_800:
					sensor->preview_size = PREVIEW_SIZE_800_600;
					if( g_Framerate == 24 ) {
						sr200pc20m_s_framerate(c,15,PREVIEW_SIZE_800_600);
						sr200pc20m_write_regs(c,sr200pc20m_preview_800_600,ARRAY_SIZE(sr200pc20m_preview_800_600),"sr200pc20m_preview_800_600");
						Cam_Printk(KERN_ERR"choose VGA(800_600) setting + 24 fps \n");
					}
					else {
						sr200pc20m_s_framerate(c,g_Framerate,PREVIEW_SIZE_800_600);
						sr200pc20m_write_regs(c,sr200pc20m_preview_800_600,ARRAY_SIZE(sr200pc20m_preview_800_600),"sr200pc20m_preview_800_600");
						Cam_Printk(KERN_ERR"choose VGA(800_600) setting \n");
					}
					break;

				default:
					printk("\n unsupported size for preview! %s %d w=%d h=%d\n", __FUNCTION__, __LINE__, mf->width, mf->height);
					goto out;
					break;
			    }
			Cam_Printk(KERN_NOTICE "Start to Preview \n");
		}
		else if(sr200pc20m_cam_state == SR200PC20M_STATE_CAPTURE)
		{							
			switch (mf->width) 			
			{			
			case UXGA_WIDTH: // 2M Capture
				sr200pc20m_write_regs(c, sr200pc20m_Capture_1600_1200, ARRAY_SIZE(sr200pc20m_Capture_1600_1200),"sr200pc20m_Capture_1600_1200");					
				Cam_Printk(KERN_ERR"choose 2M(1600x1200) jpeg setting \n");
				break;
			default:
				printk("\n unsupported size for capture! %s %d w=%d h=%d\n", __FUNCTION__, __LINE__, mf->width, mf->height);
				goto out;
				break;
			}

			Cam_Printk(KERN_NOTICE "Start to Capture \n");

		}
	}
	
out:
	return 0;
}

/*
 * Implement G/S_PARM.  There is a "high quality" mode we could try
 * to do someday; for now, we just do the frame rate tweak.
 */
static int sr200pc20m_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	
	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = 1;
	cp->timeperframe.denominator = SR200PC20M_FRAME_RATE;
	
	return 0;
}

static int sr200pc20m_s_parm(struct i2c_client *c, struct v4l2_streamparm *parms)
{
	return 0;
}

static int sr200pc20m_t_saturation(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->saturation;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case SATURATION_MINUS_2:
			sr200pc20m_write_regs(client,sr200pc20m_saturation_m2,ARRAY_SIZE(sr200pc20m_saturation_m2),"sr200pc20m_saturation_m2");	
			break;

		case SATURATION_MINUS_1:
			sr200pc20m_write_regs(client,sr200pc20m_saturation_m1,ARRAY_SIZE(sr200pc20m_saturation_m2),"sr200pc20m_saturation_m2");	
			break;		

		case SATURATION_DEFAULT:
			sr200pc20m_write_regs(client,sr200pc20m_saturation_0,ARRAY_SIZE(sr200pc20m_saturation_0),"sr200pc20m_saturation_0");	
			break;	

		case SATURATION_PLUS_1:
			sr200pc20m_write_regs(client,sr200pc20m_saturation_p1,ARRAY_SIZE(sr200pc20m_saturation_p1),"sr200pc20m_saturation_p1");	
			break;		

		case SATURATION_PLUS_2:
			sr200pc20m_write_regs(client,sr200pc20m_saturation_p2,ARRAY_SIZE(sr200pc20m_saturation_p2),"sr200pc20m_saturation_p2");	
			break;	

		default:
			printk(SR200PC20M_MOD_NAME "Saturation value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->saturation = value;
	Cam_Printk(KERN_NOTICE "%s success [Saturation e:%d]\n",__func__, sensor->saturation);
	return 0;
}

static int sr200pc20m_q_saturation(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_q_saturation is called...\n"); 
	value = sensor->saturation;
	return 0;
}


static int sr200pc20m_t_brightness(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->ev;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case EV_MINUS_4:
			sr200pc20m_write_regs(client,sr200pc20m_bright_m4,ARRAY_SIZE(sr200pc20m_bright_m4),"sr200pc20m_bright_m4");	
			break;

		case EV_MINUS_3:
			sr200pc20m_write_regs(client,sr200pc20m_bright_m3,ARRAY_SIZE(sr200pc20m_bright_m3),"sr200pc20m_bright_m3");	
			break;		

		case EV_MINUS_2:
			sr200pc20m_write_regs(client,sr200pc20m_bright_m2,ARRAY_SIZE(sr200pc20m_bright_m2),"sr200pc20m_bright_m2");	
			break;	

		case EV_MINUS_1:
			sr200pc20m_write_regs(client,sr200pc20m_bright_m1,ARRAY_SIZE(sr200pc20m_bright_m1),"sr200pc20m_bright_m1");	
			break;	
		
		case EV_DEFAULT:
			sr200pc20m_write_regs(client,sr200pc20m_bright_default,ARRAY_SIZE(sr200pc20m_bright_default),"sr200pc20m_bright_default");	
			break;

		case EV_PLUS_1:
			sr200pc20m_write_regs(client,sr200pc20m_bright_p1,ARRAY_SIZE(sr200pc20m_bright_p1),"sr200pc20m_bright_p1");	
			break;

		case EV_PLUS_2:
			sr200pc20m_write_regs(client,sr200pc20m_bright_p2,ARRAY_SIZE(sr200pc20m_bright_p2),"sr200pc20m_bright_p2");	
			break;

		case EV_PLUS_3:
			sr200pc20m_write_regs(client,sr200pc20m_bright_p3,ARRAY_SIZE(sr200pc20m_bright_p3),"sr200pc20m_bright_p3");	
			break;

		case EV_PLUS_4:
			sr200pc20m_write_regs(client,sr200pc20m_bright_p4,ARRAY_SIZE(sr200pc20m_bright_p4),"sr200pc20m_bright_p4");	
			break;

		default:
			printk(SR200PC20M_MOD_NAME "Brightness value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->ev = value;
	Cam_Printk(KERN_NOTICE "%s success [Brightness:%d]\n",__func__, sensor->ev);
	return 0;
}

static int sr200pc20m_q_brightness(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_q_brightness is called...\n"); 
	value = sensor->ev;
	return 0;
}

static int sr200pc20m_t_contrast(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->contrast;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case CONTRAST_MINUS_2:
			sr200pc20m_write_regs(client,sr200pc20m_contrast_m2,ARRAY_SIZE(sr200pc20m_contrast_m2),"sr200pc20m_contrast_m2");	
			break;

		case CONTRAST_MINUS_1:
			sr200pc20m_write_regs(client,sr200pc20m_contrast_m2,ARRAY_SIZE(sr200pc20m_contrast_m2),"sr200pc20m_contrast_m2");	
			break;		

		case CONTRAST_DEFAULT:
			sr200pc20m_write_regs(client,sr200pc20m_contrast_0,ARRAY_SIZE(sr200pc20m_contrast_0),"sr200pc20m_contrast_0");	
			break;	

		case CONTRAST_PLUS_1:
			sr200pc20m_write_regs(client,sr200pc20m_contrast_p1,ARRAY_SIZE(sr200pc20m_contrast_p1),"sr200pc20m_contrast_p1");	
			break;		

		case CONTRAST_PLUS_2:
			sr200pc20m_write_regs(client,sr200pc20m_contrast_p2,ARRAY_SIZE(sr200pc20m_contrast_p2),"sr200pc20m_contrast_p2");	
			break;	

		default:
			printk(SR200PC20M_MOD_NAME "contrast value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->contrast = value;
	Cam_Printk(KERN_NOTICE "%s success [Contrast e:%d]\n",__func__, sensor->contrast);
	return 0;
}

static int sr200pc20m_q_contrast(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_q_contrast is called...\n"); 
	value = sensor->contrast;
	return 0;
}

static int sr200pc20m_t_whitebalance(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->wb;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case WB_AUTO:
			sr200pc20m_write_regs(client,sr200pc20m_wb_auto,ARRAY_SIZE(sr200pc20m_wb_auto),"sr200pc20m_wb_auto");	
			break;

		case WB_DAYLIGHT:
			sr200pc20m_write_regs(client,sr200pc20m_wb_sunny,ARRAY_SIZE(sr200pc20m_wb_sunny),"sr200pc20m_wb_sunny");	
			break;		

		case WB_CLOUDY:
			sr200pc20m_write_regs(client,sr200pc20m_wb_cloudy,ARRAY_SIZE(sr200pc20m_wb_cloudy),"sr200pc20m_wb_cloudy");	
			break;	

		case WB_FLUORESCENT:
			sr200pc20m_write_regs(client,sr200pc20m_wb_fluorescent,ARRAY_SIZE(sr200pc20m_wb_fluorescent),"sr200pc20m_wb_fluorescent");	
			break;	
		
		case WB_INCANDESCENT:
			sr200pc20m_write_regs(client,sr200pc20m_wb_incandescent,ARRAY_SIZE(sr200pc20m_wb_incandescent),"sr200pc20m_wb_incandescent");	
			break;

		default:
			printk(SR200PC20M_MOD_NAME "White Balance value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->wb = value;
	Cam_Printk(KERN_NOTICE "%s success [White Balance e:%d]\n",__func__, sensor->wb);
	return 0;
}

static int sr200pc20m_q_whitebalance(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_get_whitebalance is called...\n"); 
	value = sensor->wb;
	return 0;
}

static int sr200pc20m_t_effect(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->effect;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case EFFECT_OFF:
			sr200pc20m_write_regs(client,sr200pc20m_effect_none,ARRAY_SIZE(sr200pc20m_effect_none),"sr200pc20m_effect_none");	
			break;

		case EFFECT_MONO:
			sr200pc20m_write_regs(client,sr200pc20m_effect_gray,ARRAY_SIZE(sr200pc20m_effect_gray),"sr200pc20m_effect_gray");	
			break;		

		case EFFECT_SEPIA:
			sr200pc20m_write_regs(client,sr200pc20m_effect_sepia,ARRAY_SIZE(sr200pc20m_effect_sepia),"sr200pc20m_effect_sepia");	
			break;	

		case EFFECT_NEGATIVE:
			sr200pc20m_write_regs(client,sr200pc20m_effect_negative,ARRAY_SIZE(sr200pc20m_effect_negative),"sr200pc20m_effect_negative");	
			break;	

		default:
			printk(SR200PC20M_MOD_NAME "Sketch value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->effect = value;
	Cam_Printk(KERN_NOTICE "%s success [Effect e:%d]\n",__func__, sensor->effect);
	return 0;
}

static int sr200pc20m_q_effect(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_q_effect is called...\n"); 
	value = sensor->effect;
	return 0;
}


static int sr200pc20m_scene_nightshot(struct i2c_client *client) {
	int ret;
	u32 Exptime = 0;
	u32 Expmax = 0;
	u16 ae_value1 = 0;
	u16 ae_value2 = 0;
	u16 ae_value3 = 0;

	sr200pc20m_write(client,0x03, 0x20);
	sr200pc20m_write(client,0x10, 0x0c);
	Cam_Printk(KERN_NOTICE "sr200pc20m_Nighmode_check() \r\n");
	
	sr200pc20m_read(client, 0x80, &ae_value1);	
	sr200pc20m_read(client, 0x81, &ae_value2);	
	sr200pc20m_read(client, 0x82, &ae_value3);

	Exptime = ((ae_value1 << 16) | (ae_value2 << 8) | (ae_value3));

	sr200pc20m_read(client, 0x88, &ae_value1);	
	sr200pc20m_read(client, 0x89, &ae_value2);	
	sr200pc20m_read(client, 0x8A, &ae_value3);

	Expmax = ((ae_value1 << 16) | (ae_value2 << 8) | (ae_value3));

	if(Exptime < Expmax) {
		sr200pc20m_write_regs(client,sr200pc20m_Nightshot_Normal,ARRAY_SIZE(sr200pc20m_Nightshot_Normal),"sr200pc20m_Nightshot_Normal");
	} else {
		sr200pc20m_write_regs(client,sr200pc20m_Nightshot_Dark,ARRAY_SIZE(sr200pc20m_Nightshot_Dark),"sr200pc20m_Nightshot_Dark");		
	}

	return 0;
}

static int sr200pc20m_t_scene(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->scene;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	if(value != SCENE_OFF){
		sr200pc20m_write_regs(client,sr200pc20m_SceneOff,ARRAY_SIZE(sr200pc20m_SceneOff),"sr200pc20m_SceneOff");		
		Cam_Printk(KERN_NOTICE "sr200pc20m_scene_off");
		sr200pc20m_t_whitebalance(client, WB_AUTO);
		sr200pc20m_t_effect(client, EFFECT_OFF);
		sr200pc20m_t_brightness(client, EV_DEFAULT);
		}

	switch(value)
	{
		case SCENE_OFF:
			sr200pc20m_write_regs(client,sr200pc20m_SceneOff,ARRAY_SIZE(sr200pc20m_SceneOff),"sr200pc20m_SceneOff");		
			break;
		case SCENE_PORTRAIT:
			sr200pc20m_write_regs(client,sr200pc20m_Portrait,ARRAY_SIZE(sr200pc20m_Portrait),"sr200pc20m_Portrait");		
			break;	
		case SCENE_LANDSCAPE:
			sr200pc20m_write_regs(client,sr200pc20m_Landscape,ARRAY_SIZE(sr200pc20m_Landscape),"sr200pc20m_Landscape");		
			break;	
		case SCENE_SPORTS:
			sr200pc20m_write_regs(client,sr200pc20m_Sports,ARRAY_SIZE(sr200pc20m_Sports),"sr200pc20m_Sports");		
			break;			
		case SCENE_PARTY:
			sr200pc20m_write_regs(client,sr200pc20m_Party,ARRAY_SIZE(sr200pc20m_Party),"sr200pc20m_Party");		
			break;
		case SCENE_BEACH:
			sr200pc20m_write_regs(client,sr200pc20m_Beach ,ARRAY_SIZE(sr200pc20m_Beach),"sr200pc20m_Beach");		
			break;
		case SCENE_SUNSET:
			sr200pc20m_write_regs(client,sr200pc20m_Sunset,ARRAY_SIZE(sr200pc20m_Sunset),"sr200pc20m_Sunset");		
			break;
		case SCENE_DAWN:
			sr200pc20m_write_regs(client,sr200pc20m_Dawn,ARRAY_SIZE(sr200pc20m_Dawn),"sr200pc20m_Dawn");		
			break;
		case SCENE_FALL:
			sr200pc20m_write_regs(client,sr200pc20m_Fall,ARRAY_SIZE(sr200pc20m_Fall),"sr200pc20m_Fall");		
			break;
		case SCENE_BACKLIGHT:
			sr200pc20m_write_regs(client,sr200pc20m_Backlight,ARRAY_SIZE(sr200pc20m_Backlight),"sr200pc20m_Backlight");		
			break;
		case SCENE_FIRE:
			sr200pc20m_write_regs(client,sr200pc20m_Firework,ARRAY_SIZE(sr200pc20m_Firework),"sr200pc20m_Firework");		
			break;
		case SCENE_CANDLE:
			sr200pc20m_write_regs(client,sr200pc20m_Candle,ARRAY_SIZE(sr200pc20m_Candle),"sr200pc20m_Candle");		
			break;
		case SCENE_NIGHT:
			sr200pc20m_scene_nightshot(client);
			break;

		default:
			printk(SR200PC20M_MOD_NAME "Scene value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->scene = value;
	Cam_Printk(KERN_NOTICE "%s success [scene:%d]\n",__func__, sensor->scene);
	return 0;
}

static int sr200pc20m_q_scene(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "s5k4ecgx_get_scene is called...\n"); 
	value = sensor->scene;
	return 0;
}

static int sr200pc20m_t_photometry(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->photometry;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case METERING_MATRIX:
			sr200pc20m_write_regs(client, sr200pc20m_metering_matrix, ARRAY_SIZE(sr200pc20m_metering_matrix),"sr200pc20m_metering_matrix");			
			break;

		case METERING_CENTER:
			sr200pc20m_write_regs(client, sr200pc20m_metering_center, ARRAY_SIZE(sr200pc20m_metering_center),"sr200pc20m_metering_center");			
			break;		

		case METERING_SPOT:
			sr200pc20m_write_regs(client, sr200pc20m_metering_spot, ARRAY_SIZE(sr200pc20m_metering_spot),"sr200pc20m_metering_spot");			
			break;	

		default:
			printk(SR200PC20M_MOD_NAME "ISO value is not supported!!!\n");
		return -EINVAL;
	}

	sensor->photometry = value;
	Cam_Printk(KERN_NOTICE "%s success [PHOTOMERTY e:%d]\n",__func__, sensor->photometry);
	return 0;
}

static int sr200pc20m_q_photometry(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_q_photometry is called...\n"); 
	value = sensor->photometry;
	return 0;
}
static int sr200pc20m_t_fps(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	s32 old_value = (s32)sensor->fps;

	Cam_Printk(KERN_NOTICE "%s is called... [old:%d][new:%d]\n",__func__, old_value, value);

	switch(value)
	{
		case FPS_auto:
			g_Framerate = 0;
			break;		
			
		case FPS_7:
			g_Framerate = 7;
			break;		

		case FPS_15:
			g_Framerate = 15;
			break;		
			
		case FPS_24:
			g_Framerate = 24;
			break;			

		default:
			printk(KERN_NOTICE "quality value is not supported!!!\n");
		return -EINVAL;
	}

	Cam_Printk(KERN_NOTICE "%s success [FPS e:%d]\n",__func__, value);
	return 0;
}

static int sr200pc20m_q_fps(struct i2c_client *client, __s32 *value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_q_fps is called...\n"); 
	value = sensor->fps;
	return 0;
}

static int sr200pc20m_ESD_check(struct i2c_client *client, __s32 *value)
{	
	u16 esd_value1 = 0;
	u16 esd_value2 = 0;
	u16 esd_value3 = 0;

	Cam_Printk(KERN_NOTICE "sr200pc20m_ESD_check() \r\n");
	
	sr200pc20m_read(client, 0x0b, &esd_value1);	
	sr200pc20m_read(client, 0x0c, &esd_value2);	
	sr200pc20m_read(client, 0x0d, &esd_value3);	
	
	if(esd_value1 == 0xaa && esd_value2 == 0xaa && esd_value3 == 0xaa){
		Cam_Printk(KERN_ERR "sr200pc20m_ESD_check() : Camera state ESD_NONE\n");
		*value = ESD_NONE;
	} else {
		Cam_Printk(KERN_ERR "sr200pc20m_ESD_check() : esd_value1 = %x, esd_value2 = %x\n", esd_value1, esd_value2);
		*value = ESD_ERROR;
	}
	
	return 0;

}

static int sr200pc20m_t_dtp_on(struct i2c_client *client)
{
	Cam_Printk(KERN_NOTICE "sr200pc20m_t_dtp_on is called...\n"); 

	gDTP_flag = TRUE;
	
	//sr200pc20m_write_regs(client,sr200pc20m_DTP_On,ARRAY_SIZE(sr200pc20m_DTP_On),"sr200pc20m_DTP_On");	
	return 0;
}

static int sr200pc20m_t_dtp_stop(struct i2c_client *client)
{
	Cam_Printk(KERN_NOTICE "sr200pc20m_t_dtp_stop is called...\n"); 
	sr200pc20m_write_regs(client,sr200pc20m_DTP_Off,ARRAY_SIZE(sr200pc20m_DTP_Off),"sr200pc20m_DTP_Off");	

	return 0;
}

static int sr200pc20m_g_exif_info(struct i2c_client *client,struct v4l2_exif_info *exif_info)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;

	Cam_Printk(KERN_NOTICE "sr200pc20m_g_exif_info is called...\n"); 
	*exif_info = sensor->exif_info;
	return 0;
}

static int sr200pc20m_set_mode(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	
	sensor->mode = value;
	Cam_Printk(KERN_NOTICE, "sr200pc20m_set_mode is called... mode = %d\n", sensor->mode);
	
	return 0;
}


static int sr200pc20m_preview_size(struct i2c_client *client, int value)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
/*
	if(sensor->mode != SR200PC20M_MODE_CAMCORDER)
	{
		switch (value) 
		{
			Cam_Printk(KERN_NOTICE "CAMERA MODE..\n"); 

			case SR200PC20M_PREVIEW_SIZE_176_144:
				sr200pc20m_write_regs(client,sr200pc20m_preview_176_144,ARRAY_SIZE(sr200pc20m_preview_176_144),"sr200pc20m_preview_176_144");	
				break;
			case SR200PC20M_PREVIEW_SIZE_352_288:
				sr200pc20m_write_regs(client,sr200pc20m_preview_352_288,ARRAY_SIZE(sr200pc20m_preview_352_288),"sr200pc20m_preview_352_288");	
				break;
			default:
				printk(SR200PC20M_MOD_NAME "Preview Resolution is not supported! : %d\n",value);
				return 0;
		}
	} 
	else 
	{
		Cam_Printk(KERN_NOTICE "CAMCORDER MODE..\n"); 

		switch (value) 
		{
			Cam_Printk(KERN_NOTICE "CAMERA MODE..\n"); 

			case SR200PC20M_PREVIEW_SIZE_176_144:
				sr200pc20m_write_regs(client,sr200pc20m_preview_176_144,ARRAY_SIZE(sr200pc20m_preview_176_144),"sr200pc20m_preview_176_144");	
				break;
			case SR200PC20M_PREVIEW_SIZE_352_288:
				sr200pc20m_write_regs(client,sr200pc20m_preview_352_288,ARRAY_SIZE(sr200pc20m_preview_352_288),"sr200pc20m_preview_352_288");	
				break;
			default:
				printk(SR200PC20M_MOD_NAME "Preview Resolution is not supported! : %d\n",value);
				return 0;
		}
	}
*/
	return 0;
}


static int sr200pc20m_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	return 0;
}

/* Get chip identification */
static int sr200pc20m_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct sr200pc20m_info *priv = to_sr200pc20m(sd);

	id->ident = priv->model;
	id->revision = 0x0;//priv->revision;

	return 0;
}


static int sr200pc20m_set_still_status(void)
{
	Cam_Printk(KERN_NOTICE "[DHL]sr200pc20m_set_still_status.. \n");

	sr200pc20m_cam_state = SR200PC20M_STATE_CAPTURE;	

	return 0;
}

static int sr200pc20m_set_preview_status(struct i2c_client *client, int value)
{
	Cam_Printk(KERN_NOTICE "[DHL]sr200pc20m_set_preview_status.. \n");

	sr200pc20m_cam_state = SR200PC20M_STATE_PREVIEW;	
	
	return 0;
}

int sr200pc20m_streamon(struct i2c_client *client)
{
	Cam_Printk(KERN_NOTICE "sr200pc20m_streamon is called...\n");  

	if(sr200pc20m_cam_state == SR200PC20M_STATE_CAPTURE)
	{
		sr200pc20m_s_exif_info(client);
	}
	
	if(gDTP_flag == TRUE){
		sr200pc20m_write_regs(client,sr200pc20m_DTP_On,ARRAY_SIZE(sr200pc20m_DTP_On),"sr200pc20m_DTP_On");	
		gDTP_flag=FALSE;
	}

	return 0;
}

static int sr200pc20m_streamoff(struct i2c_client *client)
{
	/* What's wrong with this sensor, it has no stream off function, oh!,Vincent.Wan */
	Cam_Printk(KERN_NOTICE " sr200pc20m_sensor_stop_stream");
	return 0;
}
static int set_stream(struct i2c_client *client, int enable)
{
	int ret = 0;

	if (enable) {
		ret = sr200pc20m_streamon(client);
		if (ret < 0)
			goto out;
	} else {
		ret = sr200pc20m_streamoff(client);
	}
out:
	return ret;
}

static int sr200pc20m_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	ret = set_stream(client, enable);
	if (ret < 0)
		dev_err(&client->dev, "sr200pc20m set stream error\n");
	return ret;
}

static void sr200pc20m_s_framerate( struct i2c_client *c, int fps, int preview_size )
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	
	if( sensor->fps != fps )
	{
		switch( fps )
		{
			case 7:
				sr200pc20m_write_regs(c,sr200pc20m_recording_7fps,ARRAY_SIZE(sr200pc20m_recording_7fps),"sr200pc20m_recording_7fps");	
				break;		
	
			case 15:
				sr200pc20m_write_regs(c,sr200pc20m_recording_15fps,ARRAY_SIZE(sr200pc20m_recording_15fps),"sr200pc20m_recording_15fps");	
				break;		
				
			case 24:
				{
					switch( preview_size )
					{
						case PREVIEW_SIZE_640_480:
							sr200pc20m_write_regs(c,sr200pc20m_recording_24fps,ARRAY_SIZE(sr200pc20m_recording_24fps),"sr200pc20m_recording_24fps");	
							break;
						default:
							sr200pc20m_write_regs(c,sr200pc20m_recording_24fps,ARRAY_SIZE(sr200pc20m_recording_24fps),"sr200pc20m_recording_24fps");	
							break;
					}						
				}
				break;
	
			default:
				sr200pc20m_write_regs(c,sr200pc20m_recording_modeOff,ARRAY_SIZE(sr200pc20m_recording_modeOff),"sr200pc20m_recording_modeOff");	
				break;	
		}

		sr200pc20m_t_whitebalance(c,sensor->wb);
		sr200pc20m_t_brightness(c,sensor->ev);
		sr200pc20m_t_effect(c, sensor->effect);		
		
		sensor->fps = fps;		
	}		
}

static int sr200pc20m_video_probe(struct soc_camera_device *icd,
			      struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sr200pc20m_info *priv = to_sr200pc20m(sd);
	u8 modelhi, modello;
	int ret, i;

	/*
	 * Make sure it's an sr200pc20m
	 */
	//for(i =0;i<3;i++)
	{
		ret = sr200pc20m_detect(client);
		if (!ret) {
			Cam_Printk(KERN_NOTICE "=========siliconfile sr200pc20m sensor detected==========\n");
			goto out;
		}
		
	}

	priv->model = V4L2_IDENT_SR200PC20M;
out:
	return ret;
}

int sr200pc20m_s_exif_info(struct i2c_client *client)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	u32 exposure_time=0;
	u32 exposure_data1=0;
	u32 exposure_data2=0;
	u32 exposure_data3=0;
	u32 iso_gain= 0;
	u32 iso_gain_value= 0;
	u32 iso_value= 0;

	Cam_Printk(KERN_NOTICE "[DHL] EXIF Info.. \r\n");
	Cam_Printk(KERN_NOTICE "sr200pc20m_s_Exposure() \r\n");
	sr200pc20m_write(client,0x03, 0x20);
	sr200pc20m_read(client, 0x80, &exposure_data1);	
	sr200pc20m_read(client, 0x81, &exposure_data2);	
	sr200pc20m_read(client, 0x82, &exposure_data3);	

	exposure_time = 24000000 / ((exposure_data1<<19)|(exposure_data2<<11)|(exposure_data3<<3));

	Cam_Printk(KERN_NOTICE "[DHL]shutter_speed : %d \r\n",exposure_time);

	sensor->exif_info.exposure_time.inumerator=1;
	sensor->exif_info.exposure_time.denominal=exposure_time;

	Cam_Printk(KERN_NOTICE "sr200pc20m_s_ISO() \r\n");
	sr200pc20m_write(client,0x03, 0x20);
	sr200pc20m_read(client, 0xb0, &iso_gain);	
	iso_gain_value=((iso_gain*100)/32)+50;

	if(iso_gain_value < 114)
		iso_value = 50;
	else if(iso_gain_value < 214) 
		iso_value = 100;
	else if(iso_gain_value < 264) 
		iso_value = 200;
	else if(iso_gain_value < 752) 
		iso_value = 400;
	else
		iso_value = 800;

	sensor->exif_info.iso_speed_rationg=iso_value;

	return 0;
}

static int sr200pc20m_s_thumbnail_size(struct i2c_client *client, struct v4l2_pix_format *thumbnail)
{
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	struct v4l2_pix_format* pix = &sensor->thumbnail;
	pix->width= thumbnail->width;
	pix->height= thumbnail->height;
	int retval = 0;
	
	Cam_Printk(KERN_NOTICE "sr200pc20m_s_thumbnail_size is called...(Width %d Height %d)\n",pix->width,pix->height);

	return retval;
}

static int sr200pc20m_g_register(struct v4l2_subdev *sd,  struct v4l2_dbg_register * reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	
	Cam_Printk(KERN_NOTICE "sr200pc20m_g_register() \r\n");
	Cam_Printk(KERN_NOTICE "[DHL] Value : %d\n", reg->reg);

	switch (reg->reg) {
		case V4L2_CID_GET_EXIF_EXPOSURETIME_DENOMINAL:
		{
			struct sr200pc20m_sensor *sensor = &sr200pc20m;
			printk( "[DHL]V4L2_CID_GET_EXIF_EXPOSURETIME_DENOMINAL.. \n");
			reg->val = (__s64)sensor->exif_info.exposure_time.denominal;
			break;
		}
		case V4L2_CID_GET_EXIF_ISO_SPEED:
		{
			struct sr200pc20m_sensor *sensor = &sr200pc20m;
			printk( "[DHL]V4L2_CID_GET_EXIF_ISO_SPEED.. \n");
			reg->val = (__s64)sensor->exif_info.iso_speed_rationg;
			break;
		}
		case V4L2_CID_GET_EXIF_FLASH:
		{
			struct sr200pc20m_sensor *sensor = &sr200pc20m;
			printk( "[DHL]V4L2_CID_GET_EXIF_FLASH.. \n");
			reg->val = 0;
			break;
		}
		case V4L2_CID_GET_FLASH_STATUS:
			printk( "[DHL]V4L2_CID_GET_FLASH_STATUS.. \n");
			reg->val = 0;
			break;
		case V4L2_CID_ESD_CHECK:
		{
			struct v4l2_control ESDCtrl;
			printk( "[DHL]V4L2_CID_ESD_CHECK.. \n");
			sr200pc20m_ESD_check(client, &ESDCtrl);
			reg->val = (__s64)ESDCtrl.value;
			break;
		}
	default:
		return -EINVAL;
	}

	return 0;
}

static int sr200pc20m_s_register(struct v4l2_subdev *sd,  struct v4l2_dbg_register * reg)
{	
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20m_sensor *sensor = &sr200pc20m;
	int retval = 0;

	Cam_Printk(KERN_NOTICE "ioctl_s_ctrl is called...(%d)\n", reg->reg);

	switch (reg->reg) 
	{ 
		case V4L2_CID_BRIGHTNESS:
			retval = sr200pc20m_t_brightness(client,reg->val);
			break;
		case V4L2_CID_DO_WHITE_BALANCE:
			retval = sr200pc20m_t_whitebalance(client,reg->val);
			break;
		case V4L2_CID_EFFECT:
			retval = sr200pc20m_t_effect(client,reg->val);
			break;
		case V4L2_CID_CONTRAST:
			retval = sr200pc20m_t_contrast(client,reg->val);
			break;
		case V4L2_CID_SATURATION:
			retval = sr200pc20m_t_saturation(client,reg->val);
			break;		
		case V4L2_CID_SCENE:
			retval = sr200pc20m_t_scene(client,reg->val);
			break;
		case V4L2_CID_PHOTOMETRY:
			retval = sr200pc20m_t_photometry(client,reg->val);
			break;
		case V4L2_CID_FPS:
			retval = sr200pc20m_t_fps(client,reg->val);
			break;
		case V4L2_CID_CAMERA_CHECK_DATALINE:
			retval = sr200pc20m_t_dtp_on(client);
			break;	
		case V4L2_CID_CAMERA_CHECK_DATALINE_STOP:
			retval = sr200pc20m_t_dtp_stop(client); 
			break;			
		case V4L2_CID_CAMERA_PREVIEW_SIZE:
			retval = sr200pc20m_preview_size(client,reg->val); 
			break;
		case V4L2_CID_SET_STILL_STATUS:
			retval = sr200pc20m_set_still_status();
			break;
		case V4L2_CID_SET_PREVIEW_STATUS:
			retval = sr200pc20m_set_preview_status(client, reg->val);
			break;		
				
		default:
			Cam_Printk(SR200PC20M_MOD_NAME "[id]Invalid value is ordered!!!\n");
			break;
	}
	return retval;
}

static struct i2c_device_id sr200pc20m_idtable[] = {
	{ "samsung_mainsensor", 1 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sr200pc20m_idtable);

static int sr200pc20m_g_mbus_config(struct v4l2_subdev *sd, struct v4l2_mbus_config *cfg)
{
      cfg->type = V4L2_MBUS_CSI2;
      cfg->flags = V4L2_MBUS_CSI2_1_LANE;

      return 0;
} 


static struct v4l2_subdev_core_ops sr200pc20m_core_ops = {
	//.g_ctrl			= sr200pc20m_g_ctrl,
	.s_ctrl			= sr200pc20m_s_ctrl,
	.init				= sr200pc20m_init,
	.g_chip_ident		= sr200pc20m_g_chip_ident,
	.g_register		= sr200pc20m_g_register,
	.s_register		= sr200pc20m_s_register,

};

static struct v4l2_subdev_video_ops sr200pc20m_video_ops = {
	.s_stream		= sr200pc20m_s_stream,
	.s_mbus_fmt		= sr200pc20m_s_fmt,
	.try_mbus_fmt		= sr200pc20m_try_fmt,
	.enum_mbus_fmt		= sr200pc20m_enum_fmt,
	.enum_mbus_fsizes	= sr200pc20m_enum_fsizes,
	.g_parm				= sr200pc20m_g_parm,
	.s_parm				= sr200pc20m_s_parm,
	.g_mbus_config	= sr200pc20m_g_mbus_config,
};

static struct v4l2_subdev_ops sr200pc20m_subdev_ops = {
	.core			= &sr200pc20m_core_ops,
	.video			= &sr200pc20m_video_ops,
};

/*
 * i2c_driver function
 */


static int sr200pc20m_command(struct i2c_client *client, unsigned int cmd, void *arg)
{


	switch (cmd) { 
		case VIDIOC_DBG_G_CHIP_IDENT:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_DBG_G_CHIP_IDENT\n");
			return v4l2_chip_ident_i2c_client(client, arg, V4L2_IDENT_SR200PC20M, 0);		
		case VIDIOC_INT_RESET:
			sr200pc20m_reset(client);
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_INT_RESET\n");
			return 0;
		case VIDIOC_QUERYCAP:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_QUERYCAP\n");
			return sr200pc20m_querycap(client, (struct v4l2_capability *) arg);
		case VIDIOC_ENUM_FRAMESIZES:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_ENUM_FRAMESIZES\n");
			return sr200pc20m_enum_fsizes(client, (struct v4l2_frmsizeenum *) arg);
		case VIDIOC_TRY_FMT:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_TRY_FMT\n");
			return sr200pc20m_try_fmt(client, (struct v4l2_format *) arg);
		case VIDIOC_S_FMT:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_S_FMT\n");
			return sr200pc20m_s_fmt(client, (struct v4l2_format *) arg);
		case VIDIOC_S_CTRL:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_S_CTRL\n");
			return sr200pc20m_s_ctrl(client, (struct v4l2_control *) arg);
		case VIDIOC_S_PARM:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_S_PARM\n");
			return sr200pc20m_s_parm(client, (struct v4l2_streamparm *) arg);
		case VIDIOC_G_PARM:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_G_PARM\n");
			return sr200pc20m_g_parm(client, (struct v4l2_streamparm *) arg);
		case VIDIOC_STREAMON:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_STREAMON\n");
			return sr200pc20m_streamon(client);
		case VIDIOC_STREAMOFF:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_STREAMOFF\n");
			return sr200pc20m_streamoff(client);
		case VIDIOC_DBG_G_REGISTER:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_DBG_G_REGISTER\n");
			return sr200pc20m_g_register(client, (struct v4l2_dbg_register *) arg);
		case VIDIOC_DBG_S_REGISTER:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_DBG_S_REGISTER\n");
			return sr200pc20m_s_register(client, (struct v4l2_dbg_register *) arg);
		case VIDIOC_G_EXIF:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_G_EXIF\n");
			return sr200pc20m_g_exif_info(client, (struct v4l2_exif_info *) arg);
		case VIDIOC_S_THUMBNAIL:
			Cam_Printk(KERN_NOTICE " sr200pc20m_command : VIDIOC_S_THUMBNAIL\n");
			return sr200pc20m_s_thumbnail_size(client, (struct v4l2_pix_format *) arg);
	}
	return -EINVAL;
}

static int sr200pc20m_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct sr200pc20m_info *priv;
	struct soc_camera_device *icd	= client->dev.platform_data;
	struct soc_camera_link *icl;
	int ret;

	printk("------------sr200pc20m_probe--------------\n");

	if (!icd) {
		dev_err(&client->dev, "Missing soc-camera data!\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(struct sr200pc20m_info), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev, "Failed to allocate private data!\n");
		return -ENOMEM;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &sr200pc20m_subdev_ops);

	ret = sr200pc20m_video_probe(icd, client);
	if (ret < 0) {
		kfree(priv);
	}
	
	printk("------------sr200pc20m_probe---return --ret = %d---------\n", ret);
	return ret;
}

static int sr200pc20m_remove(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver sr200pc20m_driver = {
	.driver = {
		.name	= "samsung_mainsensor",
	},
	.id_table       = sr200pc20m_idtable,	
	.command	= sr200pc20m_command,
	.probe		= sr200pc20m_probe,
	.remove		= sr200pc20m_remove,
};

/*
 * Module initialization
 */
static int __init sr200pc20m_mod_init(void)
{
	int ret =0;
	Cam_Printk(KERN_NOTICE "siliconfile sr200pc20m sensor driver, at your service\n");
	ret = i2c_add_driver(&sr200pc20m_driver);
	Cam_Printk(KERN_NOTICE "siliconfile sr200pc20m :%d \n ",ret);
	return ret;
}

static void __exit sr200pc20m_mod_exit(void)
{
	i2c_del_driver(&sr200pc20m_driver);
}

module_init(sr200pc20m_mod_init);
module_exit(sr200pc20m_mod_exit);

