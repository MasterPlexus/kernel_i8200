#include <linux/kernel.h>
#include <linux/clk.h>
#include <plat/clock.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <mach/features.h>
#include <mach/regs-apmu.h>
#include "pxa168fb_common.h"
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <plat/clock.h>
#include <linux/fb.h>

/* buffer management:
 *    filterBufList: list return to upper layer which indicates buff is free
 *    freelist: list indicates buff is free
 *    waitlist: wait queue which indicates "using" buffer, will be writen in
 *              DMA register
 *    current: buffer on showing
 * Operation:
 *    flip: if !waitlist[0] || !waitlist[1] enqueue to waiting list;
 *          else enqueue the  waitlist[0] to freelist, new buf to waitlist
 *    get freelist: return freelist
 *    eof intr: enqueue current to freelist; dequeue waitlist[0] to current;
 *    buffers are protected spin_lock_irq disable/enable
 *    suspend: when lcd is suspend, move all buffers as "switched",
 *             but don't really set hw register.
 */

static int check_yuv_status(FBVideoMode videoMode, struct pxa168fb_info *fbi)
{
	u32 x;

	if (fbi->vid) {     /* now in video layer */
		x = dma_ctrl_read(fbi->id, 0);

		if (!((videoMode & 0xf00) >> 8)) {
			if ((x & 0xf0000)>>16 == 0x5) {
				/* graphic layer broadcast YUV */
				pr_warning(" vid layer: dma_ctrl0 0x%x\n", x);
				return -EFAULT;
			}
		}
	} else {
		x = dma_ctrl_read(fbi->id, 0);

		if (!((videoMode & 0xf00) >> 8)) {
			if ((x & 0xf00000)>>20 >= 0x5
					&& (x & 0xf00000)>>20 <= 0x7) {
				pr_warning(" gfx layer: dma_ctrl0 0x%x\n", x);
				return -EFAULT; /* video layer broadcast YUV */
			}
		}
	}

	return 0;
}

static void ovlysurface_clear_pitch(struct _sOvlySurface *surface)
{
	surface->viewPortInfo.yPitch = 0;
	surface->viewPortInfo.uPitch = 0;
	surface->viewPortInfo.vPitch = 0;
}

static void update_surface(struct _sOvlySurface *surface)
{
	int tmp;

	if (surface->viewPortInfo.rotation == 90 ||
		surface->viewPortInfo.rotation == 270) {
		surface->viewPortInfo.rotation = 360 -
		surface->viewPortInfo.rotation;
		tmp = surface->viewPortInfo.srcWidth;
		surface->viewPortInfo.srcWidth =
		surface->viewPortInfo.srcHeight;
		surface->viewPortInfo.srcHeight = tmp;
		switch (surface->videoMode) {
		case FB_VMODE_YUV422PACKED:
			surface->videoMode = FB_VMODE_YUV422PACKED_IRE_90_270;
			surface->viewPortInfo.yuv_format = 1;
			ovlysurface_clear_pitch(surface);
			break;
		case FB_VMODE_YUV422PACKED_SWAPUV:
			surface->videoMode = FB_VMODE_YUV422PACKED_IRE_90_270;
			surface->viewPortInfo.yuv_format = 2;
			ovlysurface_clear_pitch(surface);
			break;
		case FB_VMODE_YUV422PACKED_SWAPYUorV:
			surface->videoMode = FB_VMODE_YUV422PACKED_IRE_90_270;
			surface->viewPortInfo.yuv_format = 4;
			ovlysurface_clear_pitch(surface);
			break;
		default:
			surface->viewPortInfo.yuv_format = 0;
		}
	}
}

int unsupport_format(struct pxa168fb_info *fbi, struct _sViewPortInfo
		viewPortInfo, FBVideoMode videoMode)
{
	if (check_yuv_status(videoMode, fbi) < 0)
		return 1;

	if ((viewPortInfo.rotation == 0) || (viewPortInfo.rotation == 1)) {
		if (!fbi->vid) {
			/* In graphic layer now */
			switch (videoMode) {
			case FB_VMODE_YUV422PLANAR:
			case FB_VMODE_YUV420PLANAR:
				pr_err("Planar is not supported!\n");
				return 1;
			default:
				break;
			}
		}

		return 0;
	}

	if (viewPortInfo.srcHeight == 1080) {
		pr_err("1080P rotation is not supported!\n");
		return 1;
	}
	if (viewPortInfo.srcHeight == 720) {
		if (viewPortInfo.rotation == 180) {
			pr_err("720p rotation 180 is not supported!\n");
			return 1;
		}
	}

	switch (videoMode) {
	case FB_VMODE_YUV422PLANAR:
	case FB_VMODE_YUV422PLANAR_SWAPUV:
	case FB_VMODE_YUV422PLANAR_SWAPYUorV:
	case FB_VMODE_YUV420PLANAR:
	case FB_VMODE_YUV420PLANAR_SWAPUV:
	case FB_VMODE_YUV420PLANAR_SWAPYUorV:
		pr_err("Planar is not supported!\n");
		return 1;
	default:
		break;
	}
	return 0;
}

int convert_pix_fmt(u32 vmode)
{
/*	pr_info("vmode=%d\n", vmode); */
	switch (vmode) {
	case FB_VMODE_YUV422PACKED:
		return PIX_FMT_YUV422PACK;
	case FB_VMODE_YUV422PACKED_SWAPUV:
		return PIX_FMT_YVU422PACK;
	case FB_VMODE_YUV422PLANAR:
		return PIX_FMT_YUV422PLANAR;
	case FB_VMODE_YUV422PLANAR_SWAPUV:
		return PIX_FMT_YVU422PLANAR;
	case FB_VMODE_YUV420PLANAR:
		return PIX_FMT_YUV420PLANAR;
	case FB_VMODE_YUV420PLANAR_SWAPUV:
		return PIX_FMT_YVU420PLANAR;
	case FB_VMODE_YUV420SEMIPLANAR:
		return PIX_FMT_YUV420SEMIPLANAR;
	case FB_VMODE_YUV420SEMIPLANAR_SWAPUV:
		return PIX_FMT_YVU420SEMIPLANAR;
	case FB_VMODE_YUV422PACKED_SWAPYUorV:
		return PIX_FMT_YUYV422PACK;
	case FB_VMODE_YUV422PACKED_IRE_90_270:
		return PIX_FMT_YUV422PACK_IRE_90_270;
	case FB_VMODE_RGB565:
		return PIX_FMT_RGB565;
	case FB_VMODE_BGR565:
		return PIX_FMT_BGR565;
	case FB_VMODE_RGB1555:
		return PIX_FMT_RGB1555;
	case FB_VMODE_BGR1555:
		return PIX_FMT_BGR1555;
	case FB_VMODE_RGB888PACK:
		return PIX_FMT_RGB888PACK;
	case FB_VMODE_BGR888PACK:
		return PIX_FMT_BGR888PACK;
	case FB_VMODE_RGBA888:
		return PIX_FMT_RGBA888;
	case FB_VMODE_BGRA888:
		return PIX_FMT_BGRA888;
	case FB_VMODE_RGB888A:
		return PIX_FMT_RGB888A;
	case FB_VMODE_BGR888A:
		return PIX_FMT_BGR888A;
	case FB_VMODE_RGB888UNPACK:
		return PIX_FMT_RGB888UNPACK;
	case FB_VMODE_BGR888UNPACK:
		return PIX_FMT_BGR888UNPACK;
	case FB_VMODE_YUV422PLANAR_SWAPYUorV:
	case FB_VMODE_YUV420PLANAR_SWAPYUorV:
	default:
		return -1;
	}
}

void *pxa168fb_alloc_framebuffer(size_t size, dma_addr_t *dma)
{
	int nr, i = 0;
	struct page **pages;
	void *start;

	size = PAGE_ALIGN(size);
	nr = size >> PAGE_SHIFT;
	start = alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
	if (start == NULL)
		return NULL;

	*dma = virt_to_phys(start);

	memset(start, 0x0, size);
	/* invalidate the buffer before vmap as noncacheable */
	flush_cache_all();
	outer_flush_range(*dma, *dma + size);

	pages = vmalloc(sizeof(struct page *) * nr);
	if (pages == NULL) {
		free_pages_exact(start, size);
		return NULL;
	}

	while (i < nr) {
		pages[i] = phys_to_page(*dma + (i << PAGE_SHIFT));
		i++;
	}
	start = vmap(pages, nr, 0, pgprot_writecombine(pgprot_kernel));

	vfree(pages);
	return start;
}

void pxa168fb_free_framebuffer(size_t size, void *vaddr, dma_addr_t *dma)
{
	int nr, i = 0;
	struct page *page;

	vunmap(vaddr);

	size = PAGE_ALIGN(size);
	nr = size >> PAGE_SHIFT;
	page = phys_to_page(*dma);

	while (i < nr) {
		__free_page(page++);
		i++;
	}

	return;
}

int set_pix_fmt(struct fb_var_screeninfo *var, int pix_fmt)
{
	switch (pix_fmt) {
	case PIX_FMT_RGB565:
		var->bits_per_pixel = 16;
		var->red.offset = 11;    var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 0;    var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		break;
	case PIX_FMT_BGR565:
		var->bits_per_pixel = 16;
		var->red.offset = 0;     var->red.length = 5;
		var->green.offset = 5;   var->green.length = 6;
		var->blue.offset = 11;   var->blue.length = 5;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		break;
	case PIX_FMT_RGB1555:
		var->bits_per_pixel = 16;
		var->red.offset = 10;    var->red.length = 5;
		var->green.offset = 5;   var->green.length = 5;
		var->blue.offset = 0;    var->blue.length = 5;
		var->transp.offset = 15; var->transp.length = 1;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 5 << 20;
		break;
	case PIX_FMT_BGR1555:
		var->bits_per_pixel = 16;
		var->red.offset = 0;     var->red.length = 5;
		var->green.offset = 5;   var->green.length = 5;
		var->blue.offset = 10;   var->blue.length = 5;
		var->transp.offset = 15; var->transp.length = 1;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 5 << 20;
		break;
	case PIX_FMT_RGB888PACK:
		var->bits_per_pixel = 24;
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 6 << 20;
		break;
	case PIX_FMT_BGR888PACK:
		var->bits_per_pixel = 24;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 16;   var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 6 << 20;
		break;
	case PIX_FMT_RGB888UNPACK:
		var->bits_per_pixel = 32;
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 7 << 20;
		break;
	case PIX_FMT_BGR888UNPACK:
		var->bits_per_pixel = 32;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 16;   var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 7 << 20;
		break;
	case PIX_FMT_RGBA888:
		var->bits_per_pixel = 32;
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 8 << 20;
		break;
	case PIX_FMT_BGRA888:
		var->bits_per_pixel = 32;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 16;   var->blue.length = 8;
		var->transp.offset = 24; var->transp.length = 8;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 8 << 20;
		break;
	case PIX_FMT_RGB888A:
		var->bits_per_pixel = 32;
		var->red.offset = 24;    var->red.length = 8;
		var->green.offset = 16;   var->green.length = 8;
		var->blue.offset = 8;    var->blue.length = 8;
		var->transp.offset = 0; var->transp.length = 8;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 0xA << 20;
		break;
	case PIX_FMT_BGR888A:
		var->bits_per_pixel = 32;
		var->red.offset = 8;     var->red.length = 8;
		var->green.offset = 16;   var->green.length = 8;
		var->blue.offset = 24;   var->blue.length = 8;
		var->transp.offset = 0; var->transp.length = 8;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 0xA << 20;
		break;
	case PIX_FMT_YUYV422PACK:
		var->bits_per_pixel = 16;
		var->red.offset = 8;     var->red.length = 16;
		var->green.offset = 4;   var->green.length = 16;
		var->blue.offset = 0;   var->blue.length = 16;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 9 << 20;
		break;
	case PIX_FMT_YVU422PACK:
		var->bits_per_pixel = 16;
		var->red.offset = 0;     var->red.length = 16;
		var->green.offset = 8;   var->green.length = 16;
		var->blue.offset = 12;   var->blue.length = 16;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 9 << 20;
		break;
	case PIX_FMT_YUV422PACK:
		var->bits_per_pixel = 16;
		var->red.offset = 4;     var->red.length = 16;
		var->green.offset = 12;   var->green.length = 16;
		var->blue.offset = 0;    var->blue.length = 16;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 9 << 20;
		break;
	case PIX_FMT_PSEUDOCOLOR:
		var->bits_per_pixel = 8;
		var->red.offset = 0;     var->red.length = 8;
		var->green.offset = 0;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 0;  var->transp.length = 0;
		break;
	case PIX_FMT_YUV422PLANAR:
		var->bits_per_pixel = 16;
		var->red.offset = 8;    var->red.length = 8;
		var->green.offset = 4;   var->green.length = 4;
		var->blue.offset = 0;   var->blue.length = 4;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 3 << 20;
		break;
	case PIX_FMT_YVU422PLANAR:
		var->bits_per_pixel = 16;
		var->red.offset = 0;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 4;
		var->blue.offset = 12;   var->blue.length = 4;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 3 << 20;
		break;
	case PIX_FMT_YUV420PLANAR:
		var->bits_per_pixel = 12;
		var->red.offset = 0;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 2;
		var->blue.offset = 10;   var->blue.length = 2;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 4 << 20;
		break;
	case PIX_FMT_YVU420PLANAR:
		var->bits_per_pixel = 12;
		var->red.offset = 0;    var->red.length = 8;
		var->green.offset = 10;   var->green.length = 2;
		var->blue.offset = 8;   var->blue.length = 2;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 4 << 20;
		break;
	case PIX_FMT_YUV420SEMIPLANAR:
		var->bits_per_pixel = 12;
		var->red.offset = 4;    var->red.length = 8;
		var->green.offset = 0;   var->green.length = 2;
		var->blue.offset = 2;   var->blue.length = 2;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 0xB << 20;
		break;
	case PIX_FMT_YVU420SEMIPLANAR:
		var->bits_per_pixel = 12;
		var->red.offset = 4;    var->red.length = 8;
		var->green.offset = 2;   var->green.length = 2;
		var->blue.offset = 0;   var->blue.length = 2;
		var->transp.offset = 0;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 0xB << 20;
		break;
	/*
	 * YUV422 Packed will be YUV444 Packed after
	 * IRE 90 and 270 degree rotation
	 */
	case PIX_FMT_YUV422PACK_IRE_90_270:
		var->bits_per_pixel = 32;
		var->red.offset = 16;    var->red.length = 8;
		var->green.offset = 8;   var->green.length = 8;
		var->blue.offset = 0;    var->blue.length = 8;
		var->transp.offset = 24;  var->transp.length = 0;
		var->nonstd &= ~0xff0fffff;
		var->nonstd |= 7 << 20;
		break;
	default:
		return  -EINVAL;
	}

	return 0;
}

int determine_best_pix_fmt(struct fb_var_screeninfo *var,
			 struct pxa168fb_info *fbi)
{
	unsigned char pxa_format;

	/* compatibility switch: if var->nonstd MSB is 0xAA then skip to
	 * using the nonstd variable to select the color space
	 */
	if (fbi->compat_mode != 0x2625) {

		/*
		 * Pseudocolor mode?
		 */
		if (var->bits_per_pixel == 8)
			return PIX_FMT_PSEUDOCOLOR;
		/*
		 * Check for YUV422PACK.
		 */
		if (var->bits_per_pixel == 16 && var->red.length == 16 &&
			var->green.length == 16 && var->blue.length == 16) {
			if (var->red.offset >= var->blue.offset)
				return (var->red.offset == 4)
					? PIX_FMT_YUV422PACK : PIX_FMT_YUYV422PACK;
			else
				return PIX_FMT_YVU422PACK;
		}
		/*
		 * Check for YUV422PLANAR.
		 */
		if (var->bits_per_pixel == 16 && var->red.length == 8 &&
			var->green.length == 4 && var->blue.length == 4 &&
			fbi->vid)
			return (var->red.offset >= var->blue.offset)
				? PIX_FMT_YUV422PLANAR : PIX_FMT_YVU422PLANAR;
		/*
		 * Check for YUV420PLANAR, YUV420SEMIPLANAR.
		 */
		if (var->bits_per_pixel == 12 && var->red.length == 8 &&
			var->green.length == 2 && var->blue.length == 2 &&
			 fbi->vid) {
			if (var->red.offset == 0)
				return (var->green.offset <= var->blue.offset)
					? PIX_FMT_YUV420PLANAR : PIX_FMT_YVU420PLANAR;
			else
				return (var->green.offset <= var->blue.offset)
					? PIX_FMT_YUV420SEMIPLANAR : PIX_FMT_YVU420SEMIPLANAR;
		}
		/*
		 * Check for 565/1555.
		 */
		if (var->bits_per_pixel == 16 && var->red.length <= 5 &&
		    var->green.length <= 6 && var->blue.length <= 5) {
			if (var->transp.length == 0)
				return (var->red.offset >= var->blue.offset)
					? PIX_FMT_RGB565 : PIX_FMT_BGR565;
			else if (var->transp.length == 1 && var->green.length <= 5)
				return (var->red.offset >= var->blue.offset)
					? PIX_FMT_RGB1555 : PIX_FMT_BGR1555;
			/* fall through */
		}

		/*
		 * Check for 888/A888/888A.
		 */
		if (var->bits_per_pixel <= 32 && var->red.length <= 8 &&
			var->green.length <= 8 && var->blue.length <= 8) {
			if (var->bits_per_pixel == 24 &&
				var->transp.length == 0)
				return (var->red.offset >= var->blue.offset)
					? PIX_FMT_RGB888PACK : PIX_FMT_BGR888PACK;
			else if (var->bits_per_pixel == 32) {
				if (0 == var->transp.offset)
					return (var->red.offset >= var->blue.offset)
						? PIX_FMT_RGB888UNPACK : PIX_FMT_BGR888UNPACK;
				else if (8 == var->transp.length) {
					if (24 == var->transp.offset)
						return (var->red.offset >= var->blue.offset)
							? PIX_FMT_RGBA888 : PIX_FMT_BGRA888;
					else
						return (var->red.offset >= var->blue.offset)
							? PIX_FMT_RGB888A : PIX_FMT_BGR888A;
				} else
					return PIX_FMT_YUV422PACK_IRE_90_270;
			}
			/* fall through */
		}
	} else {

		pxa_format = (var->nonstd >> 20) & 0xf;

		switch (pxa_format) {
		case 0:
			return PIX_FMT_RGB565;
		case 3:
			if (fbi->vid)
				return PIX_FMT_YUV422PLANAR;
			break;
		case 4:
			if (fbi->vid)
				return PIX_FMT_YUV420PLANAR;
			break;
		case 5:
			return PIX_FMT_RGB1555;
		case 6:
			return PIX_FMT_RGB888PACK;
		case 7:
			return PIX_FMT_RGB888UNPACK;
		case 8:
			return PIX_FMT_RGBA888;
		case 9:
			return PIX_FMT_YUV422PACK;
		case 0xA:
			return PIX_FMT_RGB888A;
		case 0xB:
			if (fbi->vid)
				return PIX_FMT_YUV420SEMIPLANAR;
			break;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

int pxa168fb_check_var(struct fb_var_screeninfo *var, struct fb_info *fi)
{
	struct pxa168fb_info *fbi = fi->par;
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	int pix_fmt;

	dev_dbg(fi->dev, "Enter %s\n", __func__);
	if (var->bits_per_pixel == 8) {
		pr_info("bits per pixel too small\n");
		return -EINVAL;
	}

	/* compatibility mode: if the MSB of var->nonstd is 0xAA then
	 * set xres_virtual and yres_virtual to xres and yres.
	 */

	if ((var->nonstd >> 24) == 0xAA)
		fbi->compat_mode = 0x2625;
	else if ((var->nonstd >> 24) == 0x55)
		fbi->compat_mode = 0x0;

	/*
	 * Basic geometry sanity checks.
	 */

	if (var->xoffset + var->xres > var->xres_virtual) {
		pr_err("ERROR: xoffset(%d) + xres(%d) is greater than "
			"xres_virtual(%d)\n", var->xoffset, var->xres,
			var->xres_virtual);
		return -EINVAL;
	}
	if (var->yoffset + var->yres > var->yres_virtual) {
		pr_err("ERROR: yoffset(%d) + yres(%d) is greater than "
			"yres_virtual(%d)\n", var->yoffset, var->yres,
			var->yres_virtual);
		return -EINVAL;
	}

	if (var->xres + var->right_margin +
	    var->hsync_len + var->left_margin > 3500) {
		pr_err("ERROR: var->xres(%d) + var->right_margin(%d) + "
			"var->hsync_len(%d) + var->left_margin(%d) > 2048",
			var->xres, var->right_margin, var->hsync_len,
			var->left_margin);
		return -EINVAL;
	}
	if (var->yres + var->lower_margin +
	    var->vsync_len + var->upper_margin > 2500) {
		pr_err("var->yres(%d) + var->lower_margin(%d) + "
			"var->vsync_len(%d) + var->upper_margin(%d) > 2048",
			var->yres, var->lower_margin, var->vsync_len,
			var->upper_margin);
		return -EINVAL;
	}

	/*
	 * Check size of framebuffer.
	 */
	if (mi->mmap && (var->xres_virtual * var->yres_virtual *
	    (var->bits_per_pixel >> 3) > fbi->fb_size)) {
		pr_err("xres_virtual(%d) * yres_virtual(%d) * "
			"(bits_per_pixel(%d) >> 3) > max_fb_size(%d)",
			var->xres_virtual, var->yres_virtual,
			var->bits_per_pixel, fbi->fb_size);
		return -EINVAL;
	}

	/*
	 * Select most suitable hardware pixel format.
	 */
	pix_fmt = determine_best_pix_fmt(var, fbi);
	dev_dbg(fi->dev, "%s determine_best_pix_fmt returned: %d\n",
		 __func__, pix_fmt);
	if (pix_fmt < 0)
		return pix_fmt;

	return 0;
}

int check_surface(struct fb_info *fi, struct _sOvlySurface *surface)
{
	struct pxa168fb_info *fbi = (struct pxa168fb_info *)fi->par;
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct fb_var_screeninfo *var = &fi->var;
	FBVideoMode new_mode = surface->videoMode;
	struct _sViewPortInfo *new_info = &surface->viewPortInfo;
	struct _sViewPortOffset *new_offset = &surface->viewPortOffset;
	struct _sVideoBufferAddr *new_addr = &surface->videoBufferAddr;
	int ret = 0;

	dev_dbg(fi->dev, "Enter %s\n", __func__);

	/* check view port settings. */
	if (new_info && memcmp(&fbi->surface.viewPortInfo, new_info,
			sizeof(struct _sViewPortInfo))) {
		if (!(new_addr && new_addr->startAddr[0])) {
			if (mi->mmap && (((new_info->srcWidth *
			new_info->srcHeight * var->bits_per_pixel / 8) * 2)
			> fbi->fb_size)) {
				pr_err("%s: requested memory buffer size %d"
					"exceed the max limit %d!\n", __func__,
				(new_info->srcWidth * new_info->srcHeight
				 * var->bits_per_pixel / 4), fbi->fb_size);
				return -1;
			}
		}
		fbi->surface.viewPortInfo = *new_info;
		ret |= UPDATE_VIEW;
	}

	/* check mode */
	if (new_mode >= 0 && fbi->surface.videoMode != new_mode) {
		fbi->surface.videoMode = new_mode;
		fbi->pix_fmt = convert_pix_fmt(new_mode);
		set_pix_fmt(var, fbi->pix_fmt);
		ret |= UPDATE_MODE;
	}

	/* Check offset	 */
	if (new_offset && memcmp(&fbi->surface.viewPortOffset, new_offset,
		sizeof(struct _sViewPortOffset))) {
		fbi->surface.viewPortOffset.xOffset = new_offset->xOffset;
		fbi->surface.viewPortOffset.yOffset = new_offset->yOffset;
		ret |= UPDATE_VIEW;
	}

	/* Check buffer address */
	if (new_addr && new_addr->startAddr[0] &&
		(fbi->surface.videoBufferAddr.startAddr[0] !=
		 new_addr->startAddr[0])) {
		/*check overlay buffer address and pitch alignment*/
		if (((unsigned long)new_addr->startAddr[0] & 63) &&
			(fbi->surface.viewPortInfo.yPitch & 7) &&
			(fbi->surface.viewPortInfo.uPitch & 7)) {
			printk(KERN_WARNING "Ovly: the memory base 0x%08lx is"
			" not 64 bytes aligned, pitch is not 8 bytes aligned,"
			" video playback maybe wrong!\n",
			(unsigned long)new_addr->startAddr[0]);
		}
		memcpy(fbi->surface.videoBufferAddr.startAddr,
			 new_addr->startAddr, sizeof(new_addr->startAddr));
		ret |= UPDATE_ADDR;
	}

	return ret;
}

int check_modex_active(struct pxa168fb_info *fbi)
{
	int active;

	if (!fbi)
		return 0;

	active = fbi->active && fbi->dma_on;

	/* disable all layers if in suspend */
	if (!gfx_info.fbi[0]->active)
		active = 0;

	pr_debug("%s fbi[%d] vid %d fbi->active %d"
		" dma_on %d: %d\n", __func__, fbi->id,
		fbi->vid, fbi->active, fbi->dma_on, active);
	return active;
}

void collectFreeBuf(struct pxa168fb_info *fbi,
		u8 *filterList[][3], struct regshadow_list *reglist)
{
	struct regshadow_list *shadowreg_list = 0;
	struct regshadow *shadowreg = 0;
	struct list_head *pos, *n;
	int i = 0;

	if (!filterList || !reglist)
		return;

	if (fbi->debug == 1)
		printk(KERN_DEBUG"%s fbi %d vid %d\n",
			 __func__, fbi->id, fbi->vid);

	list_for_each_safe(pos, n, &reglist->dma_queue) {
		shadowreg_list = list_entry(pos, struct regshadow_list,
			dma_queue);
		list_del(pos);
		shadowreg = &shadowreg_list->shadowreg;

		if (shadowreg) {
			/* save ptrs which need to be freed.*/
			memcpy(filterList[i], &shadowreg->paddr0,
				 3 * sizeof(u8 *));
			if (fbi->debug == 1)
				printk(KERN_DEBUG"buf %x will be returned\n",
				 shadowreg->paddr0[0]);
		}

		i++;

		if (i >= MAX_QUEUE_NUM)
			break;

		kfree(shadowreg_list);
	}
}

void clearFilterBuf(u8 *ppBufList[][3], int iFlag)
{
	/* Check null pointer. */
	if (!ppBufList)
		return;
	if (RESET_BUF & iFlag)
		memset(ppBufList, 0, 3 * MAX_QUEUE_NUM * sizeof(u8 *));
}

void buf_clear(struct regshadow_list *reglist, int iFlag)
{
	struct regshadow_list *shadowreg_list;
	struct list_head *pos, *n;

	/* Check null pointer. */
	if (!reglist)
		return;

	/* free */
	if (FREE_ENTRY & iFlag) {
		list_for_each_safe(pos, n, &reglist->dma_queue) {
			shadowreg_list = list_entry(pos, struct regshadow_list,
				dma_queue);
			list_del(pos);
			kfree(shadowreg_list);
		}
	}
}

void clear_buffer(struct pxa168fb_info *fbi)
{
	unsigned long flags;

	spin_lock_irqsave(&fbi->buf_lock, flags);
	clearFilterBuf(fbi->filterBufList, RESET_BUF);
	buf_clear(&fbi->buf_freelist, FREE_ENTRY);
	buf_clear(&fbi->buf_waitlist, FREE_ENTRY);
	kfree(fbi->buf_current);
	fbi->buf_current = 0;
	spin_unlock_irqrestore(&fbi->buf_lock, flags);
}

void buf_endframe(void *point)
{
	struct pxa168fb_info *fbi = (struct pxa168fb_info *)point;
	struct regshadow_list *shadowreg_list = 0;
	struct regshadow *shadowreg;

	if (list_empty(&fbi->buf_waitlist.dma_queue))
		return;

	shadowreg_list = list_first_entry(&fbi->buf_waitlist.dma_queue,
					struct regshadow_list, dma_queue);
	shadowreg = &shadowreg_list->shadowreg;
	list_del(&shadowreg_list->dma_queue);

	/* Update new surface settings */
	pxa168fb_set_regs(fbi, shadowreg);

	if (fbi->buf_current)
		list_add_tail(&fbi->buf_current->dma_queue,
				&fbi->buf_freelist.dma_queue);
	fbi->buf_current = shadowreg_list;
}

#if 0
static int get_list_count(struct regshadow_list *reglist)
{
	struct list_head *pos, *n;
	int count = 0;

	list_for_each_safe(pos, n, &reglist->dma_queue)
		count++;
	return count;
}
#endif

/*
 * flip buffer vsync would only do flip for user allocated buffer
 * and wait vsync to make sure buffer flipped on
 * only user allocated buffer is supported
 */
int flip_buffer_vsync(struct fb_info *info, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct pxa168fb_info *fbi = (struct pxa168fb_info *)info->par;
	struct regshadow shadowreg;
	struct _sOvlySurface surface;
	u8 *start_addr[3], *input_data;
	int ret;

	memset(&shadowreg, 0, sizeof(shadowreg));
	mutex_lock(&fbi->access_ok);

	/* Get user-mode data. */
	if (copy_from_user(&surface, argp,
				sizeof(struct _sOvlySurface))
		&& unsupport_format(fbi, surface.viewPortInfo,
				surface.videoMode))
		goto failed;

	update_surface(&surface);

	start_addr[0] = surface.videoBufferAddr.startAddr[0];
	input_data = surface.videoBufferAddr.inputData;

	if (fbi->debug == 1)
		pr_info("%s: fbi %d vid %d flip buf %p\n",
			__func__, fbi->id, fbi->vid, start_addr[0]);
	/*
	 * Has DMA addr?
	 */
	if (!start_addr[0] || (input_data)) {
		pr_err("%s only support DMA buffer\n", __func__);
		goto failed;
	}

	ret = check_surface(info, &surface);
	if (ret < 0) {
		pr_err("fbi %d (line %d): vid %d, check surface"
			" return error\n", fbi->id, __LINE__, fbi->vid);
		goto failed;
	}

	pxa168fb_set_var(info, &shadowreg, ret);
	/* we update immediately if only addr update */
	if (shadowreg.flags == UPDATE_ADDR)
		pxa168fb_set_regs(fbi, &shadowreg);
	else
		memcpy(&fbi->shadowreg, &shadowreg, sizeof(shadowreg));

	mutex_unlock(&fbi->access_ok);

	wait_for_vsync(fbi, SYNC_SELF);

	return 0;

failed:
	mutex_unlock(&fbi->access_ok);
	return -EFAULT;
}

int flip_buffer(struct fb_info *info, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct pxa168fb_info *fbi = (struct pxa168fb_info *)info->par;
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	unsigned long flags;
	struct regshadow_list *shadowreg_list = 0, *list;
	struct regshadow *shadowreg = 0;
	struct _sOvlySurface surface;
	u8 *start_addr[3], *input_data;
	u32 length;
	int ret = 0;

	mutex_lock(&fbi->access_ok);

	/* Get user-mode data. */
	if (copy_from_user(&surface, argp,
				sizeof(struct _sOvlySurface))) {
		mutex_unlock(&fbi->access_ok);
		return -EFAULT;
	}
	if (unsupport_format(fbi, surface.viewPortInfo, surface.videoMode)) {
		mutex_unlock(&fbi->access_ok);
		return -EFAULT;
	}

	shadowreg_list = kzalloc(sizeof(struct regshadow_list), GFP_KERNEL);
	if (shadowreg_list == NULL) {
		mutex_unlock(&fbi->access_ok);
		return -EFAULT;
	}
	shadowreg = &shadowreg_list->shadowreg;

	update_surface(&surface);

	length = surface.videoBufferAddr.length;
	start_addr[0] = surface.videoBufferAddr.startAddr[0];
	input_data = surface.videoBufferAddr.inputData;

	if (fbi->debug == 1)
		printk(KERN_DEBUG"fbi %d vid %d flip buf %p\n",
				fbi->id, fbi->vid, start_addr[0]);
	/*
	 * Has DMA addr?
	 */
	if (start_addr[0] && (!input_data)) {
		spin_lock_irqsave(&fbi->buf_lock, flags);
#if 0
		if (get_list_count(&fbi->buf_waitlist) >= 2) {
			/*if there are more than two frames in waitlist, dequeue
			*the older frame and enqueue it to freelist,
			*then enqueue this frame to waitlist*/
#else
		while (!list_empty(&fbi->buf_waitlist.dma_queue)) {
			/* free the waitlist elements if any */
#endif
			list = list_first_entry(&fbi->buf_waitlist.dma_queue,
				struct regshadow_list, dma_queue);
			list_del(&list->dma_queue);
			list_add_tail(&list->dma_queue,
				&fbi->buf_freelist.dma_queue);
			memset(&fbi->surface, 0, sizeof(fbi->surface));
			fbi->surface.videoMode = -1;
		}
		ret = check_surface(info, &surface);
		if (ret >= 0) {
			pxa168fb_set_var(info, shadowreg, ret);
			if (ret) {
				/* we update immediately if only addr update */
				if (shadowreg->flags == UPDATE_ADDR)
					pxa168fb_set_regs(fbi, shadowreg);
				list_add_tail(&shadowreg_list->dma_queue,
					&fbi->buf_waitlist.dma_queue);
				ret = 0;
			} else
				/* enqueue the repeated buffer to freelist */
				list_add_tail(&shadowreg_list->dma_queue,
					&fbi->buf_freelist.dma_queue);
		} else {
			pr_err("fbi %d (line %d): vid %d, check surface"
				"return error\n", fbi->id, __LINE__, fbi->vid);
			ret = -EFAULT;
		}
		spin_unlock_irqrestore(&fbi->buf_lock, flags);
	} else {
		if (!mi->mmap) {
			pr_err("fbi %d(line %d): input err, mmap is not"
				" supported\n", fbi->id, __LINE__);
			kfree(shadowreg_list);
			mutex_unlock(&fbi->access_ok);
			return -EINVAL;
		}

		ret = check_surface(info, &surface);
		if (ret >= 0) {
			pxa168fb_set_var(info, shadowreg, ret);
			ret = 0;
		} else {
			pr_err("fbi %d (line %d): vid %d, check surface"
				"return error\n", fbi->id, __LINE__, fbi->vid);
			kfree(shadowreg_list);
			mutex_unlock(&fbi->access_ok);
			return -EFAULT;
		}

		/* copy buffer */
		if (input_data) {
			if (NEED_VSYNC(fbi))
				wait_for_vsync(fbi, SYNC_SELF);
			/* if support hw DMA, replace this. */
			if (copy_from_user(fbi->fb_start,
						input_data, length)){
				kfree(shadowreg_list);
				mutex_unlock(&fbi->access_ok);
				return -EFAULT;
			}
			kfree(shadowreg_list);
			mutex_unlock(&fbi->access_ok);
			return 0;
		}

		/*
		 * if it has its own physical address,
		 * switch to this memory. don't support YUV planar format
		 * with split YUV buffers. but below code seems have no
		 * chancee to execute. - FIXME
		 */
		if (start_addr[0]) {
			pxa168fb_free_framebuffer(fbi->fb_size, fbi->fb_start,
					&fbi->fb_start_dma);

			fbi->fb_start = __va(start_addr[0]);
			fbi->fb_size = length;
			fbi->fb_start_dma =
				(dma_addr_t)__pa(fbi->fb_start);
			info->fix.smem_start = fbi->fb_start_dma;
			info->fix.smem_len = fbi->fb_size;
			info->screen_base = fbi->fb_start;
			info->screen_size = fbi->fb_size;
		}
	}

	kfree(shadowreg_list);
	mutex_unlock(&fbi->access_ok);

	return ret;
}

static void free_buf(struct pxa168fb_info *fbi)
{
	struct list_head *pos, *n;

	/* put all buffers into free list */
	list_for_each_safe(pos, n, &fbi->buf_waitlist.dma_queue) {
		list_del(pos);
		list_add_tail(pos, &fbi->buf_freelist.dma_queue);
	}

	if (fbi->buf_current) {
		list_add_tail(&fbi->buf_current->dma_queue,
			&fbi->buf_freelist.dma_queue);
		fbi->buf_current = 0;
	}

	memset(&fbi->surface, 0, sizeof(struct _sOvlySurface));
	fbi->surface.videoMode = -1;
}

int get_freelist(struct fb_info *info, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct pxa168fb_info *fbi = (struct pxa168fb_info *)info->par;
	unsigned long flags;

	if (fbi->debug == 1)
		printk(KERN_DEBUG"fbi %d vid %d get freelist\n",
				 fbi->id, fbi->vid);

	spin_lock_irqsave(&fbi->buf_lock, flags);

	/* when lcd is suspend, move all buffers as "switched"*/
	if (!(gfx_info.fbi[fbi->id]->active))
		buf_endframe(fbi);
	/* when video layer dma is off, free all buffers */
	if (!fbi->dma_on)
		free_buf(fbi);

	/* Collect expired frame to list */
	collectFreeBuf(fbi, fbi->filterBufList, &fbi->buf_freelist);
	spin_unlock_irqrestore(&fbi->buf_lock, flags);

	if (copy_to_user(argp, fbi->filterBufList,
				3*MAX_QUEUE_NUM*sizeof(u8 *))) {
		return -EFAULT;
	}
	clearFilterBuf(fbi->filterBufList, RESET_BUF);

	if (fbi->debug == 1)
		printk(KERN_DEBUG"fbi %d vid %d get freelist end\n",
				fbi->id, fbi->vid);

	return 0;
}

void set_dma_active(struct pxa168fb_info *fbi)
{
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct lcd_regs *regs = get_regs(fbi->id);
	struct fb_var_screeninfo *v = &gfx_info.fbi[fbi->id]->fb_info->var;
	struct _sVideoBufferAddr *new_addr = &fbi->surface.videoBufferAddr;
	u32 flag = fbi->vid ? CFG_DMA_ENA_MASK : CFG_GRA_ENA_MASK;
	u32 enable = fbi->vid ? CFG_DMA_ENA(1) : CFG_GRA_ENA(1);
	u32 value, dma1, v_size_dst, screen_active, active = 0;

	if ((unsigned long)new_addr->startAddr[0] || mi->mmap ||
		(!fbi->vid && fb_share && fbi->id == 1))
		active = check_modex_active(fbi);

	value = active ? enable : 0;

	/* don't enalbe graphic dma for pxa988 z1/z2 series */
	if (has_feat_video_replace_graphics_dma() && !fbi->vid)
		value = 0;

	/* if video layer is full screen without alpha blending
	 * then turn off graphics dma to save bandwidth */
	if (fbi->vid && active) {
		dma1 = dma_ctrl_read(fbi->id, 1);
		dma1 &= (CFG_ALPHA_MODE_MASK | CFG_ALPHA_MASK);
		if (dma1 == (CFG_ALPHA_MODE(2) | CFG_ALPHA(0xff))) {
			v_size_dst = readl(&regs->v_size_z);
			screen_active = readl(&regs->screen_active);
			if (v->vmode & FB_VMODE_INTERLACED) {
				screen_active = (screen_active & 0xffff) |
					(((screen_active >> 16) << 1) << 16);
			}
			if (v_size_dst == screen_active) {
				flag |= CFG_GRA_ENA_MASK;
				value &= ~CFG_GRA_ENA_MASK;
			} else if (check_modex_active(gfx_info.fbi[fbi->id])) {
				flag |= CFG_GRA_ENA_MASK;
				value |= CFG_GRA_ENA_MASK;
			}
		}
	}

	dma_ctrl_set(fbi->id, 0, flag, value);

#ifdef CONFIG_EOF_FC_WORKAROUND
	if (!atomic_read(&displayon) && (value != 0))
		atomic_set(&displayon, 1);
#endif

	pr_debug("%s fbi %d: vid %d mask %x vaule %x fbi->active %d\
		 new_addr %lu\n", __func__, fbi->id, fbi->vid, flag,
		 value, fbi->active, (unsigned long)new_addr->startAddr[0]);
}

int dispd_dma_enabled(struct pxa168fb_info *fbi)
{
	if (irqs_disabled() || !fbi->active)
		return 0;

	/* check whether display done irq enabled */
	if (!(readl(fbi->reg_base + SPU_IRQ_ENA) &
		display_done_imask(fbi->id)))
		return 0;

	/* check whether path clock is disabled */
	if (lcd_clk_get(fbi->id, clk_sclk) & SCLK_DISABLE)
		return 0;

	/* in modex dma may not be enabled */
	return dma_ctrl_read(fbi->id, 0) & (fbi->vid ?
		CFG_DMA_ENA_MASK : CFG_GRA_ENA_MASK);
}

void clear_dispd_irq(struct pxa168fb_info *fbi)
{
	int isr = readl(fbi->reg_base + SPU_IRQ_ISR);

	if ((isr & display_done_imask(fbi->id))) {
		irq_status_clear(fbi->id, display_done_imask(fbi->id));
		pr_info("fbi %d irq miss, clear isr %x\n", fbi->id, isr);
	}
}

void irq_mask_eof(int id)
{
#if defined(CONFIG_CPU_PXA988) || defined(CONFIG_CPU_PXA1088)
	struct pxa168fb_info *fbi = gfx_info.fbi[id];
	int irq_mask = display_done_imask(fbi->id);

#ifdef CONFIG_DISP_DFC
	irq_mask |= vsync_imask(fbi->id);
#endif
	spin_lock(&fbi->var_lock);
	if (atomic_dec_and_test(&fbi->irq_en_count)) {
		if (fbi->active) {
			irq_mask_set(fbi->id, irq_mask, 0);
			if (!has_feat_video_replace_graphics_dma())
				pm_qos_update_request(&fbi->qos_idle,
					PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
		} else {
			fbi->irq_mask &= ~irq_mask;
			pr_err("%s: LCD is suspended, do nothing\n", __func__);
		}
	}
	spin_unlock(&fbi->var_lock);

	dev_dbg(fbi->dev, "mask eof intr: irq enable count %d\n",
		atomic_read(&fbi->irq_en_count));
#endif

}

void irq_unmask_eof(int id)
{
#if defined(CONFIG_CPU_PXA988) || defined(CONFIG_CPU_PXA1088)
	struct pxa168fb_info *fbi = gfx_info.fbi[id];
	int irq_mask = display_done_imask(fbi->id);
#ifdef CONFIG_DISP_DFC
	irq_mask |= vsync_imask(fbi->id);
#endif

	spin_lock(&fbi->var_lock);
	if (!atomic_read(&fbi->irq_en_count)) {
		if (fbi->active) {
			if (!has_feat_video_replace_graphics_dma())
				pm_qos_update_request(&fbi->qos_idle,
					PM_QOS_CPUIDLE_BLOCK_AXI_VALUE);
			irq_status_clear(fbi->id, irq_mask);
			irq_mask_set(fbi->id, irq_mask, irq_mask);
		} else {
			fbi->irq_mask |= irq_mask;
			pr_err("%s: LCD is suspended, do nothing\n", __func__);
		}
	}
	atomic_inc(&fbi->irq_en_count);
	spin_unlock(&fbi->var_lock);

	dev_dbg(fbi->dev, "unmask eof intr: irq enable count %d\n",
		atomic_read(&fbi->irq_en_count));
#endif
}

void wait_for_vsync(struct pxa168fb_info *fbi, unsigned char param)
{
	struct fbi_info *info = (fbi->vid) ? &ovly_info : &gfx_info;
	int ret = 0;

	pr_debug("fbi->id %d vid: %d\n", fbi->id, fbi->vid);
#ifdef VSYNC_DSI_CMD
	mutex_lock(&fbi->vsync_mutex);
#endif

	irq_unmask_eof(fbi->id);

	switch (param) {
	case SYNC_SELF:
		atomic_set(&fbi->w_intr, 0);
		ret = wait_event_interruptible_timeout(gfx_info.fbi[0]->w_intr_wq,
				atomic_read(&fbi->w_intr), HZ/20);
		if (!ret)
			clear_dispd_irq(fbi);
		break;
	case SYNC_PANEL:
		atomic_set(&info->fbi[0]->w_intr, 0);
		ret = wait_event_interruptible_timeout(gfx_info.fbi[0]->w_intr_wq,
				atomic_read(&info->fbi[0]->w_intr), HZ/20);
		if (!ret)
			clear_dispd_irq(info->fbi[0]);
		break;
	case SYNC_TV:
		atomic_set(&info->fbi[1]->w_intr, 0);
		ret = wait_event_interruptible_timeout(gfx_info.fbi[0]->w_intr_wq,
				atomic_read(&info->fbi[1]->w_intr), HZ/20);
		if (!ret)
			clear_dispd_irq(info->fbi[1]);
		break;
	case SYNC_PANEL_TV:
		atomic_set(&info->fbi[0]->w_intr, 0);
		atomic_set(&info->fbi[1]->w_intr, 0);
		ret = wait_event_interruptible_timeout(gfx_info.fbi[0]->w_intr_wq,
				atomic_read(&info->fbi[0]->w_intr) &&
				atomic_read(&info->fbi[1]->w_intr), HZ/20);
		if (!ret) {
			if (atomic_read(&info->fbi[0]->w_intr) == 0)
				clear_dispd_irq(info->fbi[0]);
			if (atomic_read(&info->fbi[1]->w_intr) == 0)
				clear_dispd_irq(info->fbi[1]);
		}
		break;
	default:
		break;
	}
	irq_mask_eof(fbi->id);
#ifdef VSYNC_DSI_CMD
	mutex_unlock(&fbi->vsync_mutex);
#endif
}

enum VSYNC_JOB_TYPE {
	VSYNC_DSI_SEND_COMMAND = 1,
	VSYNC_DSI_READ_COMMAND,
	VSYNC_DSI_LANE_RESET,
	END_OF_TYPE
};

struct VSYNC_JOB {
	enum VSYNC_JOB_TYPE type;
	void *data;
};

static struct VSYNC_JOB *todo_job;

int wait_for_vsync_job(struct pxa168fb_info *fbi, struct VSYNC_JOB *job)
{
	int ret = 0;
	unsigned long flags;
	unsigned long time;
 #ifdef VSYNC_DSI_CMD
	mutex_lock(&fbi->vsync_mutex);
#endif
	spin_lock_irqsave(&fbi->job_lock, flags);
	todo_job = job;
	spin_unlock_irqrestore(&fbi->job_lock, flags);
	//printk("%s : +Queing job %d\n", __func__, job ? job->type : -1);
	irq_unmask_eof(fbi->id);
	atomic_set(&fbi->w_intr1, 0);
	time = jiffies;
	ret = wait_event_interruptible_timeout(gfx_info.fbi[0]->w_intr_wq1,
		atomic_read(&fbi->w_intr1), HZ/20);
	time = jiffies - time;
	if (!ret) {
			clear_dispd_irq(fbi);
		printk("%s, timeout!(type:%d, elapsed:%dms)\n",
				__func__, job->type, jiffies_to_msecs(time));
		printk("%s, irq_mask =0x%08x, active=%d, dma_on=%d, output_on=%d\n", __func__, vsync_imask(fbi->id),!!(fbi->active), !!(fbi->dma_on), !!(fbi->output_on));
		;
	}
	spin_lock_irqsave(&fbi->job_lock, flags);
	todo_job = NULL;
	spin_unlock_irqrestore(&fbi->job_lock, flags);
	//printk("%s : -Queing job\n", __func__);
	irq_mask_eof(fbi->id);
#ifdef VSYNC_DSI_CMD
	mutex_unlock(&fbi->vsync_mutex);
#endif
	return ret;
}

void reset_lanes(struct pxa168fb_info *fbi)
{
	panel_dma_ctrl(0);
	dsi_lanes_enable(fbi, 0);
	udelay(5);
	dsi_lanes_enable(fbi, 1);
	udelay(5);
	panel_dma_ctrl(1);
}

void dsi_lanes_reset(struct pxa168fb_info *fbi)
{
	struct VSYNC_JOB job;
	if (!fbi) return;
	//printk("Reset DSI lanes\n");
	mutex_lock(&fbi->cmd_mutex);
	job.type = VSYNC_DSI_LANE_RESET;
	job.data = NULL;
	if (fbi->active) wait_for_vsync_job(fbi, &job);
	//else printk("fbi->active : %d\n", fbi->active);
	mutex_unlock(&fbi->cmd_mutex);
	return;
}
void dsi_lanes_check(struct pxa168fb_info *fbi)
{
	//struct VSYNC_JOB job;
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct dsi_info *di = (struct dsi_info *)mi->phy_info;
	struct dsi_regs *dsi;
	unsigned int irq_status, rx0_status,rx_ctrl1,lcd1_status_0,count=0;
#if 0 // Force reset testing
	static int count2 = 20;
#endif
	if (!di) {
		pr_err("%s: no dsi info available\n", __func__);
		return;
	}
	dsi = (struct dsi_regs *)di->regs;
retry:
	irq_status = readl(&dsi->irq_status);
	rx0_status = readl(&dsi->rx0_status);
	rx_ctrl1 = readl(&dsi->rx_ctrl1);
	lcd1_status_0 = readl(&dsi->lcd1.status_0);
#if 0 // Force reset testing
	count2--;
	if (count2 < 7)
	{
		lcd1_status_0 = 0xE0000000;
		if (count2 == 0) count2=20;
	}
#endif
	if ((lcd1_status_0 & 0xE0000000)== 0xE0000000) // Check DPHY_STOP_STATE_BYTE, and Ready/Request check
	{
					// check it 5 times, normal state, can have stop state.
					if (count++ < 5)
					{
									msleep(5);
									goto retry;
					}
	} else return;
	if ((lcd1_status_0 & 0xE0000000)== 0xE0000000) // Check DPHY_STOP_STATE_BYTE, and Ready/Request check
	{
		//printk("Begin 0x%08x,0x%08x,0x%08x,0x%08x\n",lcd1_status_0,irq_status,rx0_status,rx_ctrl1);
		dsi_lanes_reset(fbi);
		//printk("Finish 0x%08x,0x%08x,0x%08x,0x%08x\n",lcd1_status_0,irq_status,rx0_status,rx_ctrl1);
	}
	return;
}

void dsi_lanes_debug(struct pxa168fb_info *fbi)
{

	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct dsi_info *di = (struct dsi_info *)mi->phy_info;
	struct dsi_regs *dsi;
	unsigned int irq_status, rx0_status,rx_ctrl1,lcd1_status_0,count=0;
	if (!di) {
		pr_err("%s: no dsi info available\n", __func__);
		return;
	}
	dsi = (struct dsi_regs *)di->regs;
	irq_status = readl(&dsi->irq_status);
	rx0_status = readl(&dsi->rx0_status);
	rx_ctrl1 = readl(&dsi->rx_ctrl1);
	lcd1_status_0 = readl(&dsi->lcd1.status_0);
	printk("ESD.isr : 0x%08x\n",readl(fbi->reg_base + SPU_IRQ_ISR));
	printk("ESD.phy 0x%08x,0x%08x,0x%08x,0x%08x\n",lcd1_status_0,irq_status,rx0_status,rx_ctrl1);
}
struct VSYNC_DSI_TX_CMD_DATA {
	struct pxa168fb_info *fbi;
	struct dsi_cmd_desc *cmds;
	int count;
	u8 *buffer;
	u8 *packet_len;
};
void pxa168_dsi_cmd_array_tx(struct pxa168fb_info *fbi, struct dsi_cmd_desc cmds[],
		int count)
{
#ifdef VSYNC_DSI_CMD
	u8 *buffer, *packet_len;
	int ret = 0;
	buffer = kzalloc(DSI_MAX_DATA_BYTES*count, GFP_KERNEL);
	packet_len = kmalloc(count, GFP_KERNEL);

	if (!buffer) {
		printk(KERN_WARNING"Cannot get dsi cmd buffer\n");
		if (packet_len) {
			kfree(packet_len);
			packet_len = NULL;
		}
	} else {
		if (!packet_len) {
			printk(KERN_WARNING"Cannot get dsi cmd buffer\n");
			kfree(buffer);
			buffer = NULL;
		}
		//printk("Use prepareing method\n");
	}

	mutex_lock(&fbi->cmd_mutex);
	if (buffer && packet_len) {
		int loop;
		struct VSYNC_JOB job;
		struct VSYNC_DSI_TX_CMD_DATA txcmd_data;

		dsi_prepare_cmd_array_tx(fbi, cmds, count,buffer, packet_len);
		printk("%s : fbi->active = %d fbi->output_on = %d\n", __func__, fbi->active,fbi->output_on);
		if (fbi->active && fbi->output_on) {
			txcmd_data.fbi = fbi;
			txcmd_data.cmds = cmds;
			txcmd_data.count = count;
			txcmd_data.buffer = buffer;
			txcmd_data.packet_len = packet_len;
			job.type = VSYNC_DSI_SEND_COMMAND;
			job.data = &txcmd_data;
			ret = wait_for_vsync_job(fbi, &job);
			if (!ret) // request occured during non fb emitting period
			{
				printk("%s : timout send directly...\n", __func__);
				for(loop = 0; loop < count; loop++)
					dsi_send_prepared_cmd_tx(fbi, cmds[loop], buffer+DSI_MAX_DATA_BYTES*loop, packet_len[loop]);
			}
		} else {
			for(loop = 0; loop < count; loop++)
				dsi_send_prepared_cmd_tx(fbi, cmds[loop], buffer+DSI_MAX_DATA_BYTES*loop, packet_len[loop]);
		}
		kfree(buffer);
		kfree(packet_len);
	} else {
		if (fbi->active) wait_for_vsync_job(fbi, NULL); // dummy call
		panel_dma_ctrl(0);
		dsi_cmd_array_tx(fbi, cmds, count);
		panel_dma_ctrl(1);
	}
	mutex_unlock(&fbi->cmd_mutex);
#else
	dsi_cmd_array_tx(fbi, cmds, count);
#endif
}

void pxa168_dsi_cmd_array_rx(struct pxa168fb_info *fbi, struct dsi_buf *dbuf,
		struct dsi_cmd_desc cmds[], int count)
{
#ifdef VSYNC_DSI_CMD
	u8 *buffer, *packet_len;
	int ret;

	buffer = kzalloc(DSI_MAX_DATA_BYTES*count, GFP_KERNEL);
	packet_len = kmalloc(count, GFP_KERNEL);

	if (!buffer) {
		printk(KERN_WARNING"Cannot get dsi cmd buffer\n");
		if (packet_len) {
			kfree(packet_len);
			packet_len = NULL;
		}
	} else {
		if (!packet_len) {
			printk(KERN_WARNING"Cannot get dsi cmd buffer\n");
			kfree(buffer);
			buffer = NULL;
		}
		//printk("Use prepareing method\n");
	}

	mutex_lock(&fbi->cmd_mutex);
	if (buffer && packet_len) {
		int loop;
		struct VSYNC_JOB job;
		struct VSYNC_DSI_TX_CMD_DATA txcmd_data;
		dsi_prepare_cmd_array_tx(fbi, cmds, count,buffer, packet_len);
		printk("%s : fbi->active = %d fbi->output_on = %d\n", __func__, fbi->active,fbi->output_on);
		if (fbi->active && fbi->output_on) {
			txcmd_data.fbi = fbi;
			txcmd_data.cmds = cmds;
			txcmd_data.count = count;
			txcmd_data.buffer = buffer;
			txcmd_data.packet_len = packet_len;
			job.type = VSYNC_DSI_SEND_COMMAND;
			job.data = &txcmd_data;
			ret=wait_for_vsync_job(fbi, &job);
			if (!ret) // request occured during non fb emitting period
			{
				for(loop = 0; loop < count; loop++)
					dsi_send_prepared_cmd_tx(fbi, cmds[loop], buffer+DSI_MAX_DATA_BYTES*loop, packet_len[loop]);
			}
		} else {
			for(loop = 0; loop < count; loop++)
				dsi_send_prepared_cmd_tx(fbi, cmds[loop], buffer+DSI_MAX_DATA_BYTES*loop, packet_len[loop]);
		}
		kfree(buffer);
		kfree(packet_len);
	} else {
		if (fbi->active) wait_for_vsync_job(fbi, NULL); // dummy call
		panel_dma_ctrl(0);
		dsi_cmd_array_tx(fbi, cmds, count);
		panel_dma_ctrl(1);
	}
	dsi_cmd_array_rx_process(fbi, dbuf);
	mutex_unlock(&fbi->cmd_mutex);
#else
	dsi_cmd_array_rx(fbi,dbuf,cmds,count);
#endif
}

void pxa168fb_list_init(struct pxa168fb_info *fbi)
{
	INIT_LIST_HEAD(&fbi->buf_freelist.dma_queue);
	INIT_LIST_HEAD(&fbi->buf_waitlist.dma_queue);
	fbi->buf_current = 0;
}

void pxa168fb_misc_update(struct pxa168fb_info *fbi)
{
	struct pxa168fb_vdma_info *lcd_vdma = 0;

	lcd_vdma = request_vdma(fbi->id, fbi->vid);
	if (lcd_vdma) {
		vdma_info_update(lcd_vdma, fbi->active, fbi->dma_on, fbi->pix_fmt,
				fbi->surface.viewPortInfo.rotation,
				fbi->surface.viewPortInfo.yuv_format);
		pxa688_vdma_config(lcd_vdma);
	}
	if (fbi->vid && vid_vsmooth)
		pxa688fb_vsmooth_set(fbi->id, 1, vid_vsmooth);
	if (!fbi->vid) {
		pxa688fb_partdisp_update(fbi->id);
		if (gfx_vsmooth)
			pxa688fb_vsmooth_set(fbi->id, 0, gfx_vsmooth);
	}
}

void set_start_address(struct fb_info *info, int xoffset, int yoffset,
			struct regshadow *shadowreg)
{
	struct pxa168fb_info *fbi = info->par;
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct fb_var_screeninfo *var = &info->var;
	unsigned long addr_y0 = 0, addr_u0 = 0, addr_v0 = 0;
	struct _sVideoBufferAddr *new_addr = &fbi->surface.videoBufferAddr;
	int  pixel_offset;

	dev_dbg(info->dev, "Enter %s\n", __func__);

	if ((unsigned long)new_addr->startAddr[0]) {
		memcpy(&shadowreg->paddr0, new_addr->startAddr,
			 sizeof(new_addr->startAddr));
		if (fbi->debug == 1)
			printk(KERN_DEBUG"%s: buffer updated to %x\n",
				 __func__, (int)new_addr->startAddr[0]);
	} else {
		if (!mi->mmap) {
			pr_debug("fbi %d(line %d): input err, mmap is not"
				" supported\n", fbi->id, __LINE__);
			return;
		}
		pixel_offset = (yoffset * var->xres_virtual) + xoffset;
		addr_y0 = fbi->fb_start_dma + (pixel_offset *
			(var->bits_per_pixel >> 3));
		if ((fbi->pix_fmt >= 12) && (fbi->pix_fmt <= 15))
			addr_u0 = addr_y0 + var->xres * var->yres;

		if ((fbi->pix_fmt >> 1) == 6)
			addr_v0 = addr_u0 + (var->xres * var->yres >> 1);
		else if ((fbi->pix_fmt >> 1) == 7)
			addr_v0 = addr_u0 + (var->xres * var->yres >> 2);

		shadowreg->paddr0[0] = addr_y0;
		shadowreg->paddr0[1] = addr_u0;
		shadowreg->paddr0[2] = addr_v0;
	}
}

void set_dma_control0(struct pxa168fb_info *fbi, struct regshadow *shadowreg)
{
	struct pxa168fb_mach_info *mi;
	u32 x = 0, pix_fmt;

	dev_dbg(fbi->fb_info->dev, "Enter %s\n", __func__);

	mi = fbi->dev->platform_data;
	pix_fmt = fbi->pix_fmt;
	shadowreg->yuv420sp_ctrl = 0;

	/* If we are in a pseudo-color mode, we need to enable
	 * palette lookup  */
	if (pix_fmt == PIX_FMT_PSEUDOCOLOR)
		x |= dma_palette(1);

	/* Configure hardware pixel format */
	x |= dma_fmt(fbi->vid, (pix_fmt & ~0x1000) >> 1);

	/*
	 * color format in memory:
	 * PXA168/PXA910:
	 * PIX_FMT_YUV422PACK: UYVY(CbY0CrY1)
	 * PIX_FMT_YUV422PLANAR: YUV
	 * PIX_FMT_YUV420PLANAR: YUV
	 */
	if (((pix_fmt & ~0x1000) >> 1) == 5) {
		/* YUV422PACK, YVU422PACK, YUYV422PACK */
		x |= dma_csc(fbi->vid, 1);
		x |= dma_swaprb(fbi->vid, mi->panel_rbswap);
		if (pix_fmt & 0x1000)
			/* YUYV422PACK */
			x |= dma_swapyuv(fbi->vid, 1);
		else
			/* YVU422PACK */
			x |= dma_swapuv(fbi->vid, pix_fmt & 1);
	} else if (pix_fmt >= 12) {
		/* PLANAR, SEMIPLANAR, YUV422PACK_IRE_90_270, PSEUDOCOLOR */
		if (!fbi->vid)
			pr_err("%s fmt %d not supported on graphics layer...\n",
				__func__, pix_fmt);
		x |= dma_csc(fbi->vid, 1);
		if (pix_fmt == PIX_FMT_YUV420SEMIPLANAR ||
			pix_fmt == PIX_FMT_YVU420SEMIPLANAR)
			/* YUV420SP format owns independent UV swap ctrl */
			shadowreg->yuv420sp_ctrl = swap_spuv(fbi->id);
		else
			x |= dma_swapuv(fbi->vid, pix_fmt & 1);
		x |= dma_swaprb(fbi->vid, (mi->panel_rbswap));
	} else {
		/* RGB formats */
		/* Check red and blue pixel swap.
		 * 1. source data swap. BGR[M:L] rather than RGB[M:L]
		 *    is stored in memeory as source format.
		 * 2. panel output data swap
		 */
		x |= dma_swaprb(fbi->vid, ((pix_fmt & 1) ^ 1) ^
			 (mi->panel_rbswap));
	}

	shadowreg->dma_ctrl0 = x;
}

void set_screen(struct pxa168fb_info *fbi, struct regshadow *shadowreg)
{
	struct fb_var_screeninfo *var = &fbi->fb_info->var;
	struct _sOvlySurface *surface = &fbi->surface;
	struct _sVideoBufferAddr *new_addr = &fbi->surface.videoBufferAddr;
	u32 xres, yres, xres_z, yres_z, xres_virtual, bits_per_pixel;
	u32 left = 0, top = 0, pitch[3], x;

	var = &fbi->fb_info->var;
	xres = var->xres; yres = var->yres;
	xres_z = var->xres; yres_z = var->yres;
	xres_virtual = var->xres_virtual;
	bits_per_pixel = var->bits_per_pixel;

	/* xres_z = total - left - right */
	xres_z = xres_z - (left << 1);
	/* yres_z = yres_z - top - bottom */
	yres_z = yres_z - (top << 1);

	if ((unsigned long)new_addr->startAddr[0]) {
		xres = surface->viewPortInfo.srcWidth;
		yres = surface->viewPortInfo.srcHeight;
		var->xres_virtual = surface->viewPortInfo.srcWidth;
		var->yres_virtual = surface->viewPortInfo.srcHeight * 2;
		xres_virtual = surface->viewPortInfo.srcWidth;

		xres_z = surface->viewPortInfo.zoomXSize;
		yres_z = surface->viewPortInfo.zoomYSize;

		left = surface->viewPortOffset.xOffset;
		top = surface->viewPortOffset.yOffset;

		pr_debug("surface: xres %d xres_z %d"
			" yres %d yres_z %d\n left %d top %d\n",
			xres, xres_z, yres, yres_z, left, top);
	}

	dev_dbg(fbi->fb_info->dev, "adjust: xres %d xres_z %d"
		" yres %d yres_z %d\n left %d top %d\n",
		xres, xres_z, yres, yres_z, left, top);

	pitch[0] = surface->viewPortInfo.yPitch;
	pitch[1] = surface->viewPortInfo.uPitch;
	pitch[2] = surface->viewPortInfo.vPitch;

	if (((fbi->pix_fmt & ~0x1000) >> 1) < 6) {
		pitch[0] = pitch[0] ? pitch[0] : (xres_virtual *
				 bits_per_pixel >> 3);
		pitch[1] = pitch[2] = 0;
	} else {
		pitch[0] = pitch[0] ? pitch[0] : xres;
		pitch[1] = pitch[1] ? pitch[1] : xres >> 1;
		pitch[2] = pitch[2] ? pitch[2] : xres >> 1;
	}
	/* start address on screen */
	shadowreg->start_point = (top << 16) | left;
	/* resolution, src size */
	shadowreg->src_size = (yres << 16) | xres;
	/* resolution, dst size */
	shadowreg->dst_size = (yres_z << 16) | xres_z;
	/* pitch, pixels per line */
	shadowreg->pitch[0] = pitch[0] & 0xFFFF;
	shadowreg->pitch[1] = pitch[2] << 16 | pitch[1];

	/* enable two-level zoom down if the ratio exceed 2 */
	if (fbi->vid && xres_z && var->bits_per_pixel) {
		int shift = (fbi->id == 1) ? 22 : 20;
		u32 reg = (fbi->id == 2) ? LCD_PN2_LAYER_ALPHA_SEL1 :\
			 LCD_AFA_ALL2ONE;

		x = readl(fbi->reg_base + reg);
		if (!(var->xres & 0x1) && ((var->xres >> 1) >= xres_z))
			x |= 1 << shift;
		else
			x &= ~(1 << shift);
		shadowreg->zoom = x;
	}
}

int pxa168fb_set_var(struct fb_info *info, struct regshadow *shadowreg,
		 u32 flags)
{
	struct pxa168fb_info *fbi = info->par;

	/* Configure global panel parameters. */
	if (flags & UPDATE_VIEW)
		set_screen(fbi, shadowreg);
	if (flags & UPDATE_MODE)
		set_dma_control0(fbi, shadowreg);
	/* buffer return to user in shadowreg_list->shadowreg */
	set_start_address(info, info->var.xoffset,
		info->var.yoffset, shadowreg);
	shadowreg->flags = flags;
	return 0;
}

void pxa168fb_set_regs(struct pxa168fb_info *fbi, struct regshadow *shadowreg)
{
	struct lcd_regs *regs = get_regs(fbi->id);

	/* when lcd is suspend, read or write lcd controller's
	* register is not effective, so just return*/
	if (!(gfx_info.fbi[fbi->id]->active)) {
		printk(KERN_DEBUG"LCD is not active, don't touch hardware\n");
		return;
	}

	if (shadowreg->flags & UPDATE_ADDR) {
		if (fbi->vid) {
			writel(shadowreg->paddr0[0], &regs->v_y0);
			writel(shadowreg->paddr0[1], &regs->v_u0);
			writel(shadowreg->paddr0[2], &regs->v_v0);
		} else
			writel(shadowreg->paddr0[0], &regs->g_0);
		shadowreg->flags &= ~UPDATE_ADDR;
	}

	if (shadowreg->flags & UPDATE_MODE) {
		dma_ctrl_set(fbi->id, 0, dma_mask(fbi->vid),
				 shadowreg->dma_ctrl0);
		if (fbi->vid)
			yuvsp_fmt_ctrl(yuvsp_mask(fbi->id),
				shadowreg->yuv420sp_ctrl);
		shadowreg->flags &= ~UPDATE_MODE;
	}

	if (shadowreg->flags & UPDATE_VIEW) {
		if (fbi->vid) {
			/* start address on screen */
			writel(shadowreg->start_point, &regs->v_start);
			/* pitch, pixels per line */
			writel(shadowreg->pitch[0], &regs->v_pitch_yc);
			writel(shadowreg->pitch[1], &regs->v_pitch_uv);
			/* resolution, src size */
			writel(shadowreg->src_size, &regs->v_size);
			/* resolution, dst size */
			writel(shadowreg->dst_size, &regs->v_size_z);

			writel(shadowreg->zoom, fbi->reg_base +
			 ((fbi->id == 2) ? (LCD_PN2_LAYER_ALPHA_SEL1) :
			 (LCD_AFA_ALL2ONE)));
		} else {
			/* start address on screen */
			writel(shadowreg->start_point, &regs->g_start);
			/* pitch, pixels per line */
			writel(shadowreg->pitch[0], &regs->g_pitch);
			/* resolution, src size */
			writel(shadowreg->src_size, &regs->g_size);
			/* resolution, dst size */
			writel(shadowreg->dst_size, &regs->g_size_z);
		}
		shadowreg->flags &= ~UPDATE_VIEW;
	}

	set_dma_active(fbi);
	pxa168fb_misc_update(fbi);
}

irqreturn_t pxa168_fb_isr(int id)
{
	struct pxa168fb_info *fbi;
	struct regshadow *shadowreg;
	int vid;
	struct timespec vsync_time;
	unsigned long flags;

	/* First do video layer update, then graphics layer */
	for (vid = 1; vid >= 0; vid--) {
		fbi = vid ? (ovly_info.fbi[id]) : (gfx_info.fbi[id]);
		if (!fbi)
			continue;

		shadowreg = &fbi->shadowreg;
		if (shadowreg && shadowreg->flags)
			pxa168fb_set_regs(fbi, shadowreg);

		if (atomic_read(&fbi->op_count)) {
			spin_lock(&fbi->buf_lock);
			/* do buffer switch for video flip */
			buf_endframe(fbi);
			spin_unlock(&fbi->buf_lock);
		}

		/* wake up queue, only use one queue for all layer */
		if (atomic_read(&fbi->w_intr) == 0) {
			atomic_set(&fbi->w_intr, 1);
			wake_up_all(&gfx_info.fbi[0]->w_intr_wq);
		}

		/* wake up queue, for some operations which must be done
		 * after vsync happen, so it should be done by sequence */
		if (atomic_read(&fbi->w_intr1) == 0) {
			atomic_set(&fbi->w_intr1, 1);
			spin_lock_irqsave(&fbi->job_lock, flags);
			if (todo_job) {
				switch(todo_job->type) {
					case VSYNC_DSI_SEND_COMMAND :
						{
							int loop;
							struct VSYNC_DSI_TX_CMD_DATA *txcmd_data = (struct VSYNC_DSI_TX_CMD_DATA *)todo_job->data;
							for(loop = 0; loop < txcmd_data->count; loop++) {
									dsi_send_prepared_cmd_tx(txcmd_data->fbi,
											txcmd_data->cmds[loop],
											txcmd_data->buffer+DSI_MAX_DATA_BYTES*loop,
											txcmd_data->packet_len[loop]);
							}
						}
						break;
					case VSYNC_DSI_READ_COMMAND :
						break;
					case VSYNC_DSI_LANE_RESET :
						reset_lanes(fbi);
						break;
					default :
						break;
				}
				todo_job = NULL;
			}
			spin_unlock_irqrestore(&fbi->job_lock, flags);
			wake_up(&gfx_info.fbi[0]->w_intr_wq1);
		}
		atomic_set(&fbi->vsync_cnt, 2);

		if (fbi->vsync_en) {
			/* Get time stamp of vsync */
			ktime_get_ts(&vsync_time);
			fbi->vsync_ts_nano = ((uint64_t)vsync_time.tv_sec)
				* 1000 * 1000 * 1000 +
				((uint64_t)vsync_time.tv_nsec);
			/* notify sysfs in work queue */
			queue_work(fbi->workqueue, &fbi->vsync_work);
		}
	}
	return IRQ_HANDLED;
}

/*****************************************************************************/
static int pxa168fb_regs_dump(int id, char *buf)
{
	struct pxa168fb_info *fbi = gfx_info.fbi[0];
	struct lcd_regs *regs = get_regs(id);
	int s = 0, f = DUMP_SPRINTF;

	mvdisp_dump(f, "register base: 0x%p\n", fbi->reg_base);
	mvdisp_dump(f, "  video layer\n");
	mvdisp_dump(f, "\tv_y0        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_y0) & 0xfff, readl(&regs->v_y0));
	mvdisp_dump(f, "\tv_u0        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_u0) & 0xfff, readl(&regs->v_u0));
	mvdisp_dump(f, "\tv_v0        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_v0) & 0xfff, readl(&regs->v_v0));
	mvdisp_dump(f, "\tv_c0        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_c0) & 0xfff, readl(&regs->v_c0));
	mvdisp_dump(f, "\tv_y1        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_y1) & 0xfff, readl(&regs->v_y1));
	mvdisp_dump(f, "\tv_u1        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_u1) & 0xfff, readl(&regs->v_u1));
	mvdisp_dump(f, "\tv_v1        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_v1) & 0xfff, readl(&regs->v_v1));
	mvdisp_dump(f, "\tv_c1        ( @%3x ) 0x%x\n",
		 (int)(&regs->v_c1) & 0xfff, readl(&regs->v_c1));
	mvdisp_dump(f, "\tv_pitch_yc  ( @%3x ) 0x%x\n",
		 (int)(&regs->v_pitch_yc) & 0xfff, readl(&regs->v_pitch_yc));
	mvdisp_dump(f, "\tv_pitch_uv  ( @%3x ) 0x%x\n",
		 (int)(&regs->v_pitch_uv) & 0xfff, readl(&regs->v_pitch_uv));
	mvdisp_dump(f, "\tv_start     ( @%3x ) 0x%x\n",
		 (int)(&regs->v_start) & 0xfff, readl(&regs->v_start));
	mvdisp_dump(f, "\tv_size      ( @%3x ) 0x%x\n",
		 (int)(&regs->v_size) & 0xfff, readl(&regs->v_size));
	mvdisp_dump(f, "\tv_size_z    ( @%3x ) 0x%x\n",
		 (int)(&regs->v_size_z) & 0xfff, readl(&regs->v_size_z));

	mvdisp_dump(f, "  graphic layer\n");
	mvdisp_dump(f, "\tg_0         ( @%3x ) 0x%x\n",
		(int)(&regs->g_0) & 0xfff, readl(&regs->g_0));
	mvdisp_dump(f, "\tg_1         ( @%3x ) 0x%x\n",
		(int)(&regs->g_1) & 0xfff, readl(&regs->g_1));
	mvdisp_dump(f, "\tg_pitch     ( @%3x ) 0x%x\n",
		(int)(&regs->g_pitch) & 0xfff, readl(&regs->g_pitch));
	mvdisp_dump(f, "\tg_start     ( @%3x ) 0x%x\n",
		(int)(&regs->g_start) & 0xfff, readl(&regs->g_start));
	mvdisp_dump(f, "\tg_size      ( @%3x ) 0x%x\n",
		(int)(&regs->g_size) & 0xfff, readl(&regs->g_size));
	mvdisp_dump(f, "\tg_size_z    ( @%3x ) 0x%x\n",
		(int)(&regs->g_size_z) & 0xfff, readl(&regs->g_size_z));

	mvdisp_dump(f, "  hardware cursor\n");
	mvdisp_dump(f, "\thc_start    ( @%3x ) 0x%x\n",
		(int)(&regs->hc_start) & 0xfff, readl(&regs->hc_start));
	mvdisp_dump(f, "\thc_size     ( @%3x ) 0x%x\n",
		(int)(&regs->hc_size) & 0xfff, readl(&regs->hc_size));

	mvdisp_dump(f, "  screen\n");
	mvdisp_dump(f, "\tscreen_size     ( @%3x ) 0x%x\n",
		(int)(&regs->screen_size) & 0xfff,
		 readl(&regs->screen_size));
	mvdisp_dump(f, "\tscreen_active   ( @%3x ) 0x%x\n",
		(int)(&regs->screen_active) & 0xfff,
		 readl(&regs->screen_active));
	mvdisp_dump(f, "\tscreen_h_porch  ( @%3x ) 0x%x\n",
		(int)(&regs->screen_h_porch) & 0xfff,
		 readl(&regs->screen_h_porch));
	mvdisp_dump(f, "\tscreen_v_porch  ( @%3x ) 0x%x\n",
		(int)(&regs->screen_v_porch) & 0xfff,
		 readl(&regs->screen_v_porch));

	mvdisp_dump(f, "  color\n");
	mvdisp_dump(f, "\tblank_color     ( @%3x ) 0x%x\n",
		 (int)(&regs->blank_color) & 0xfff,
		 readl(&regs->blank_color));
	mvdisp_dump(f, "\thc_Alpha_color1 ( @%3x ) 0x%x\n",
		 (int)(&regs->hc_Alpha_color1) & 0xfff,
		 readl(&regs->hc_Alpha_color1));
	mvdisp_dump(f, "\thc_Alpha_color2 ( @%3x ) 0x%x\n",
		 (int)(&regs->hc_Alpha_color2) & 0xfff,
		 readl(&regs->hc_Alpha_color2));
	mvdisp_dump(f, "\tv_colorkey_y    ( @%3x ) 0x%x\n",
		 (int)(&regs->v_colorkey_y) & 0xfff,
		 readl(&regs->v_colorkey_y));
	mvdisp_dump(f, "\tv_colorkey_u    ( @%3x ) 0x%x\n",
		 (int)(&regs->v_colorkey_u) & 0xfff,
		 readl(&regs->v_colorkey_u));
	mvdisp_dump(f, "\tv_colorkey_v    ( @%3x ) 0x%x\n",
		 (int)(&regs->v_colorkey_v) & 0xfff,
		 readl(&regs->v_colorkey_v));

	mvdisp_dump(f, "  control\n");
	mvdisp_dump(f, "\tvsync_ctrl      ( @%3x ) 0x%x\n",
		 (int)(&regs->vsync_ctrl) & 0xfff,
		 readl(&regs->vsync_ctrl));
	mvdisp_dump(f, "\tdma_ctrl0       ( @%3x ) 0x%x\n",
		 (int)(dma_ctrl(0, id)) & 0xfff,
		 readl(fbi->reg_base + dma_ctrl0(id)));
	mvdisp_dump(f, "\tdma_ctrl1       ( @%3x ) 0x%x\n",
		 (int)(dma_ctrl(1, id)) & 0xfff,
		 readl(fbi->reg_base + dma_ctrl1(id)));
	mvdisp_dump(f, "\tintf_ctrl       ( @%3x ) 0x%x\n",
		 (int)(intf_ctrl(id)) & 0xfff,
		 readl(fbi->reg_base + intf_ctrl(id)));
	mvdisp_dump(f, "\tirq_enable      ( @%3x ) 0x%8x\n",
		 (int)(SPU_IRQ_ENA) & 0xfff,
		 readl(fbi->reg_base + SPU_IRQ_ENA));
	mvdisp_dump(f, "\tirq_status      ( @%3x ) 0x%8x\n",
		 (int)(SPU_IRQ_ISR) & 0xfff,
		 readl(fbi->reg_base + SPU_IRQ_ISR));
	mvdisp_dump(f, "\tclk_sclk        ( @%3x ) 0x%x\n",
		 (int)(clk_reg(id, clk_sclk)) & 0xfff,
		 readl(clk_reg(id, clk_sclk)));
	if (clk_reg(id, clk_tclk))
		mvdisp_dump(f, "\tclk_tclk        ( @%3x ) 0x%x\n",
			(int)(clk_reg(id, clk_tclk)) & 0xfff,
			readl(clk_reg(id, clk_tclk)));
	mvdisp_dump(f, "\tclk_lvds        ( @%3x ) 0x%x\n",
		 (int)(clk_reg(id, clk_lvds_rd)) & 0xfff,
		 readl(clk_reg(id, clk_lvds_rd)));

	/* TV path registers */
	if (id == 1) {
		mvdisp_dump(f, "\ntv path interlace related:\n");
		mvdisp_dump(f, "\tv_h_total         ( @%3x ) 0x%8x\n",
			(int)(LCD_TV_V_H_TOTAL_FLD) & 0xfff,
			readl(fbi->reg_base + LCD_TV_V_H_TOTAL_FLD));
		mvdisp_dump(f, "\tv_porch           ( @%3x ) 0x%8x\n",
			(int)(LCD_TV_V_PORCH_FLD) & 0xfff,
			readl(fbi->reg_base + LCD_TV_V_PORCH_FLD));
		mvdisp_dump(f, "\tvsync_ctrl        ( @%3x ) 0x%8x\n",
			(int)(LCD_TV_SEPXLCNT_FLD) & 0xfff,
			readl(fbi->reg_base + LCD_TV_SEPXLCNT_FLD));
	}

	mvdisp_dump(f, "\n");
	return s;
}

/************************************************************************/
static size_t lcd_help(char *buf)
{
	int s = 0, f = DUMP_SPRINTF;

	mvdisp_dump(f, "commands:\n");
	mvdisp_dump(f, " - dump path(pn/tv/pn2:0/1/2) registers, var info\n");
	mvdisp_dump(f, "\tcat lcd\n");
	mvdisp_dump(f, " - dump all display controller registers\n");
	mvdisp_dump(f, "\techo l > /proc/pxa168fb\n");
	mvdisp_dump(f, " - dump register @ [offset_hex]\n");
	mvdisp_dump(f, "\techo -0x[offset_hex] > lcd\n");
	mvdisp_dump(f, " - set register @ [offset_hex] with [value_hex]\n");
	mvdisp_dump(f, "\techo 0x[value_hex] > lcd\n");
	mvdisp_dump(f, " - count path(pn/tv/pn2:[0/1/2]) interrupts"
			" within 10s\n");
	mvdisp_dump(f, "\techo v[path:0/1/2] > lcd\n");
	mvdisp_dump(f, " - enable[1]/disable[0] error interrupts dump"
			" (underflow/axi error) at run time\n");
	mvdisp_dump(f, "\techo e[en/dis:1/0] > lcd\n");
	mvdisp_dump(f, " - enable[1]/disable[0] flip/free buffers info"
			" dump (print level KERN_DEBUG)\n");
	mvdisp_dump(f, "\techo d[en/dis:1/0] > lcd\n");

	return s;
}

static ssize_t lcd_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct pxa168fb_info *fbi = dev_get_drvdata(dev);
	struct fb_var_screeninfo *var = &fbi->fb_info->var;
	int s = 0, f = DUMP_SPRINTF;

	mvdisp_dump(f, "path %d:\n\tactive %d, dma_on %d\n",
		fbi->id, fbi->active, fbi->dma_on);
	if (!fbi->vid)
		mvdisp_dump(f, "\tpath frm time %luus\n", fbi->frm_usec);
	if (fbi->clk)
		mvdisp_dump(f, "\tclk_src %luMhz\n",
			clk_get_rate(fbi->clk)/1000000);
	if (fbi->path_clk)
		mvdisp_dump(f, "\tpath_clk_src %luMhz\n",
			clk_get_rate(fbi->path_clk)/1000000);
	if (fbi->phy_clk)
		mvdisp_dump(f, "\tphy_clk_src %luMhz\n",
			clk_get_rate(fbi->phy_clk)/1000000);
	mvdisp_dump(f, "\tgfx_udflow %d, vid_udflow %d, axi_err %d\n",
		gfx_udflow_count, vid_udflow_count, axi_err_count);
	mvdisp_dump(f, "\tirq_retry_count %d\n", irq_retry_count);
	mvdisp_dump(f, "\tdebug %d, DBG_VSYNC_PATH %d, DBG_ERR_IRQ %d\n",
		fbi->debug, DBG_VSYNC_PATH, DBG_ERR_IRQ);

	mvdisp_dump(f, "var info:\n");
	mvdisp_dump(f, "\txres              %4d yres              %4d\n",
		var->xres, var->yres);
	mvdisp_dump(f, "\txres_virtual      %4d yres_virtual      %4d\n",
		var->xres_virtual, var->yres_virtual);
	mvdisp_dump(f, "\txoffset           %4d yoffset           %4d\n",
		var->xoffset, var->yoffset);
	mvdisp_dump(f, "\tleft_margin(hbp)  %4d right_margin(hfp) %4d\n",
		var->left_margin, var->right_margin);
	mvdisp_dump(f, "\tupper_margin(vbp) %4d lower_margin(vfp) %4d\n",
		var->upper_margin, var->lower_margin);
	mvdisp_dump(f, "\thsync_len         %4d vsync_len         %4d\n",
		var->hsync_len,	var->vsync_len);
	mvdisp_dump(f, "\tbits_per_pixel    %d\n", var->bits_per_pixel);
	mvdisp_dump(f, "\tpixclock          %d\n", var->pixclock);
	mvdisp_dump(f, "\tsync              0x%x\n", var->sync);
	mvdisp_dump(f, "\tvmode             0x%x\n", var->vmode);
	mvdisp_dump(f, "\trotate            0x%x\n\n", var->rotate);

	s += pxa168fb_regs_dump(fbi->id, buf + s);
#ifdef CONFIG_PXA688_MISC
	if (fbi->id == fb_vsmooth) {
		mvdisp_dump(f, "=== vertical smooth path %d by filter %d==\n",
			fb_vsmooth, fb_filter);
		s += pxa168fb_regs_dump(fb_filter, buf + s);
	}
#endif

	s += lcd_help(buf + s);

	return s;
}

static u32 mvdisp_reg;
static ssize_t lcd_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct pxa168fb_info *fbi = dev_get_drvdata(dev);
	u32 addr = (u32)fbi->reg_base, i, tmp;
	char vol[30];
	struct clk *lcdclk;

	if (size > 30) {
		pr_err("%s size = %d > max 30 chars\n", __func__, size);
		return size;
	}else if ('c' == buf[0]) {
		pr_info("\ndisplay controller regs\n");
		memcpy(vol, buf+1, size-1);
		lcdclk = clk_get(NULL, "LCDCLK");
		if ((int) simple_strtoul(vol, NULL, 16) != 0)
		{
			printk("enable lcdclk\n");
			lcdclk->ops->enable(lcdclk);
		}
		else
		{
			printk("disable lcdclk\n");
			lcdclk->ops->disable(lcdclk);
		}
		return size;
	}

	if ('d' == buf[0]) {
		memcpy(vol, (void *)((u32)buf + 1), size - 1);
		fbi->debug = (int) simple_strtoul(vol, NULL, 10);
		/* fbi->debug usage:
		 *	1: show flip/get freelist sequence
		 */
		return size;
	} else if ('e' == buf[0]) {
		memcpy(vol, (void *)((u32)buf + 1), size - 1);
		tmp = (int) simple_strtoul(vol, NULL, 10) << DBG_ERR_SHIFT;
		if (tmp != DBG_ERR_IRQ) {
			debug_flag &= ~DBG_ERR_MASK; debug_flag |= tmp;
		}
		return size;
	} else if ('v' == buf[0]) {
		if (size > 2) {
			memcpy(vol, (void *)((u32)buf + 1), size - 1);
			tmp = (int) simple_strtoul(vol, NULL, 10) << DBG_VSYNC_SHIFT;
		} else
			tmp = fbi->id;
		if (tmp != DBG_VSYNC_PATH) {
			debug_flag &= ~DBG_VSYNC_MASK; debug_flag |= tmp;
		}
		vsync_check_count();
		return size;
	} else if ('-' == buf[0]) {
		memcpy(vol, buf+1, size-1);
		mvdisp_reg = (int) simple_strtoul(vol, NULL, 16);
		pr_info("reg @ 0x%x: 0x%x\n", mvdisp_reg,
			__raw_readl(addr + mvdisp_reg));
		return size;
	} else if ('0' == buf[0] && 'x' == buf[1]) {
		/* set the register value */
		tmp = (int)simple_strtoul(buf, NULL, 16);
		__raw_writel(tmp, addr + mvdisp_reg);
		pr_info("set reg @ 0x%x: 0x%x\n", mvdisp_reg,
			__raw_readl(addr + mvdisp_reg));
		return size;
	} else if ('l' == buf[0]) {
		pr_info("\ndisplay controller regs\n");
		for (i = 0; i < 0x300; i += 4) {
			if (!(i % 16))
				printk("\n0x%3x: ", i);
			printk(" %8x", __raw_readl(addr + i));
		}
		pr_info("\n");
		return size;
	}

	return size;
}

DEVICE_ATTR(lcd, S_IRUGO | S_IWUSR, lcd_show, lcd_store);

#ifdef CONFIG_DISP_DFC
atomic_t disp_dfc = ATOMIC_INIT(0);
atomic_t disp_pll3 = ATOMIC_INIT(0);
atomic_t disp_pll1 = ATOMIC_INIT(0);
#define ROUND_RATE_RANGE_MHZ 	2  /* unit: MHz */
#define DFC_RETRY_TIMEOUT 	60
#define MHZ_TO_HZ 		1000000

enum {
	PLL1_416 = 0,
	PLL1_624,
	PLL3,
};

static void recalculate_disp_clk(struct pxa168fb_info *fbi,
				 u32 divider, u32 *reg_val)
{
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct dsi_info *di = (struct dsi_info *)mi->phy_info;
	u32  bclk_div, pclk_div, ratio;

	ratio = ((di->lanes > 2) ? 4 : 8);
	*reg_val = lcd_clk_get(fbi->id, clk_sclk);
	*reg_val &= ~(SCLK_SOURCE_SELECT_MASK |
		 DSI1_BITCLK_DIV_MASK |
		 CLK_INT_DIV_MASK);
	bclk_div = divider;
	pclk_div = divider * ratio;
	*reg_val |= DSI1_BITCLK_DIV(bclk_div) | CLK_INT_DIV(pclk_div);
}

/*
 * trigger display dynamic frequency change, retry DFC_RETRY_TIMEOUT times,
 * if still failed, then give up this dfc.
 */
static int disp_dfc_commit(struct pxa168fb_info *fbi, struct clk *best_parent,
			   int *timeout)
{
	int ret = 0, count = 0;
	unsigned long flags;

	atomic_set(&disp_dfc, 1);
	do {
		wait_for_vsync(fbi, SYNC_SELF);
		count++;
	} while (atomic_read(&disp_dfc) && (count < DFC_RETRY_TIMEOUT));

	spin_lock_irqsave(&fbi->dfc_lock, flags);
	if (unlikely(count == DFC_RETRY_TIMEOUT) && atomic_read(&disp_dfc)) {
		ret = -EINVAL;
		atomic_set(&disp_dfc, 0);
	}
	spin_unlock_irqrestore(&fbi->dfc_lock, flags);

	if (!ret) {
		clk_reparent(fbi->phy_clk->parent->parent, best_parent);
		clk_reparent(fbi->path_clk->parent->parent, best_parent);
	}

	*timeout = count;
	return ret;
}

static int disp_sclk_switch_to_pll1(struct pxa168fb_info *fbi, u32 pll1,
				    u32 divider, int *timeout)
{
	int ret = 0;
	unsigned int reg_val = 0;
	struct clk *best_parent = NULL, *pll3_clk = NULL;
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;

	mi->sclk_rst = __raw_readl((void __iomem *)APMU_LCD);
	mi->sclk_rst &= ~(1 << 9);
	switch (pll1) {
	case PLL1_624:
		best_parent = clk_get(NULL, "pll1_624");
		mi->sclk_rst &= ~(1 << 6);
		break;
	case PLL1_416:
	default:
		best_parent = clk_get(NULL, "pll1_416");
		mi->sclk_rst |= 1 << 6;
		break;
	}

	recalculate_disp_clk(fbi, divider, &reg_val);
	mi->sclk_div = reg_val | SCLK_SOURCE_SELECT(1);

	/* trigger display frequency change*/
	atomic_set(&disp_pll1, 1);
	ret = disp_dfc_commit(fbi, best_parent, timeout);
	if (!ret) {
		atomic_set(&disp_pll1, 0);
		if(atomic_read(&disp_pll3)) {
			atomic_set(&disp_pll3, 0);
			pll3_clk = clk_get(NULL, "pll3");
			clk_disable(pll3_clk);
			/* restore pll3 to default rate */
			dynamic_change_pll3(0);
		}
		pr_info("%s: %s, divider %u\n", __func__,
			pll1 ? "624":"416", divider);
	}
	return ret;
}

static int disp_sclk_pll3_to_pll1(struct pxa168fb_info *fbi, u32 rate,
				  int *timeout)
{
	unsigned long parent_rate, now,bestl = 0, besth = ULONG_MAX;
	unsigned int bestdivl = 0, bestdivh = 0, div = 0;
	unsigned int parent_selh = 0, parent_sell = 0, parent_sel = 0;
	unsigned int i, j, maxdiv = 15;
	u32 pll1_op_table[2] = {416, 624};
	int ret = 0;

	for (i = 0; i < 2; i++) {
		parent_rate = pll1_op_table[i];
		for (j = 1; j <= maxdiv; j++) {
			now = parent_rate / j;
			/* condition to select a best closest rate >= rate */
			if (now >= rate && now < besth) {
				bestdivh = j;
				besth = now;
				parent_selh = i;
			/* condition to select a best closest rate <= rate */
			} else if (now <= rate && now > bestl) {
				bestdivl = j;
				bestl = now;
				parent_sell = i;
			}
		}
	}

	now = ((besth - rate) <= (rate - bestl)) ? besth : bestl;
	if (now == besth) {
		div = bestdivh;
		parent_sel = parent_selh;
	} else {
		div = bestdivl;
		parent_sel = parent_sell;
	}

	if (!div)
		div = 1;

	ret = disp_sclk_switch_to_pll1(fbi, parent_sel, div, timeout);

	return ret;
}

static bool is_pll3_used_by_disp(int id)
{
	u32 reg_val;

	reg_val = lcd_clk_get(id, clk_sclk);
	/* if pll3 was currently used by disp, first change to pll1 */
	if ((reg_val & SCLK_SOURCE_SELECT_MASK) == SCLK_SOURCE_DSI_PLL)
		return true;
	else
		return false;
}

static int mmp_disp_dfc(struct pxa168fb_info *fbi, u32 rate)
{
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct clk *pll3_clk = NULL, *best_parent = NULL;
	u32 tmp, div, real_rate, i, j, reg_val;
	u32 op_table[3] = {416, 624, 0};
	static u32 old_rate = 1;
	int ret = 0, count = 0;
	bool pll3_to_pll3 = false;

	if (!mi->no_legacy_clk) {
		pr_err("legacy clock dfc not implemented now!\n");
		return -EINVAL;
	}
	mutex_lock(&fbi->access_ok);
	if (!fbi->active) {
		pr_info("LCD enter suspend, don't do disp dfc\n");
		mutex_unlock(&fbi->access_ok);
		return -EINVAL;
	}

	if ((rate >= (old_rate - ROUND_RATE_RANGE_MHZ) &&
	     rate <= (old_rate + ROUND_RATE_RANGE_MHZ)) || !rate) {
		pr_err("new rate is %uMHz or around the old rate %uMHz\n",
			rate, old_rate);
		mutex_unlock(&fbi->access_ok);
		return -EINVAL;
	}

	/*
	 * We are trying to restore disp freq to default, if current clk
	 * src is pll1, first restore pll3 to default freq then do disp dfc.
	 * If current clk src is pll3, pll3 will be restored after dfc.
	 */
	if ((rate == mi->sclk_default) && !atomic_read(&disp_pll3))
		dynamic_change_pll3(0);

	best_parent = pll3_clk = clk_get(NULL, "pll3");
	/* pll3 frequency */
	op_table[2] = clk_get_rate(best_parent) / MHZ_TO_HZ;

	tmp = 0xff;
	for (i = 0; i < ARRAY_SIZE(op_table); i++) {
		for (j = 1; j < 15; j++) {
			real_rate = op_table[i] / j;
			if (rate > real_rate + ROUND_RATE_RANGE_MHZ)
				break;
			else if ((rate <= (real_rate + ROUND_RATE_RANGE_MHZ)) &&
				 (rate >= (real_rate - ROUND_RATE_RANGE_MHZ))) {
				/* round clock rate in 1MHz range */
				tmp = real_rate;
				break;
			}
		}

		if (tmp != 0xff)
			break;
	}
#if 0
	/* can't find a suitable clock source */
	if (tmp == 0xff)
		i = 0xff;
#endif

	switch (i) {
	case PLL1_416:
	case PLL1_624:
		ret = disp_sclk_switch_to_pll1(fbi, i, j, &count);
		break;
	case PLL3:
		if(is_pll3_used_by_disp(fbi->id))
			pll3_to_pll3 = true;
		else
			pll3_to_pll3= false;

		recalculate_disp_clk(fbi, j, &reg_val);
		mi->sclk_div = reg_val | SCLK_SOURCE_SELECT(3);
		clk_enable(pll3_clk);
		atomic_set(&disp_pll3, 1);

		ret = disp_dfc_commit(fbi, best_parent, &count);
		if (ret) {
			/* if disp dfc failed, disable pll3 */
			clk_disable(pll3_clk);
			/* previous sclk is pll1, restore pll3 to default rate */
			if (!pll3_to_pll3) {
				atomic_set(&disp_pll3, 0);
				dynamic_change_pll3(0);
			}
		} else if (atomic_read(&disp_pll1))
			atomic_set(&disp_pll1, 0);
		break;
	default:
		/* need to dynamic change pll3, firt ensure no one use pll3 */
		reg_val = lcd_clk_get(fbi->id, clk_sclk);
		/* if pll3 was currently used by disp, first change to pll1 */
		if ((reg_val & SCLK_SOURCE_SELECT_MASK) ==
		    SCLK_SOURCE_DSI_PLL) {
			div = (reg_val & DSI1_BITCLK_DIV_MASK) >> 8;
			old_rate = clk_get_rate(pll3_clk) / div / MHZ_TO_HZ;
			ret = disp_sclk_pll3_to_pll1(fbi, old_rate, &count);
		}
		if (ret)
			goto out;

		/* then re-configure the pll3 clock */
		dynamic_change_pll3(rate);
		clk_enable(pll3_clk);
		/* round off the divider */
		div = (clk_get_rate(pll3_clk) / MHZ_TO_HZ+ rate / 2) / rate;
		recalculate_disp_clk(fbi, div, &reg_val);
		mi->sclk_div = reg_val | SCLK_SOURCE_SELECT(3);
		atomic_set(&disp_pll3, 1);
		/* switch disp sclk from pll1 to pll3 */
		ret = disp_dfc_commit(fbi, best_parent, &count);
		if (ret) {
			/* if disp dfc failed, disable pll3 */
			atomic_set(&disp_pll3, 0);
			clk_disable(pll3_clk);
			/* restore pll3 to default rate */
			dynamic_change_pll3(0);
		} else if (atomic_read(&disp_pll1))
			atomic_set(&disp_pll1, 0);

		real_rate = clk_get_rate(pll3_clk) / div / MHZ_TO_HZ;
		break;
	}
out:
	if (!ret) {
		old_rate = real_rate;

		pr_info("disp_dfc was done in %d vblank time, new rate %uMHz,"
			 " clk_src pll%d %luMHz, sclk div 0x%x, rst 0x%x\n",
			 count, real_rate, (u32)(atomic_read(&disp_pll3) ? 3 : 1),
			 clk_get_rate(best_parent) / MHZ_TO_HZ,
			 mi->sclk_div, mi->sclk_rst);
	} else {
		pr_err("disp dfc from %dMHz to %dMHz failed for %d times\n",
			old_rate, rate, DFC_RETRY_TIMEOUT);
	}

	mutex_unlock(&fbi->access_ok);
	return ret;
}

static ssize_t lcd_freq_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct pxa168fb_info *fbi = dev_get_drvdata(dev);
	struct pxa168fb_mach_info *mi = fbi->dev->platform_data;
	struct dsi_info *di = (struct dsi_info *)mi->phy_info;
	u32 pixel_clk, phy_clk;
	int s = 0;

	mutex_lock(&fbi->access_ok);
	if (mi->no_legacy_clk) {
		if (mi->phy_type & (DSI | DSI2DPI | LVDS)) {
			phy_clk = clk_get_rate(fbi->phy_clk);
			pixel_clk = phy_clk * di->lanes / di->bpp;
			s += sprintf(buf, "real path clock %uMhz,"
				" phy clock %uMhz, pixclock %uMhz\n",
				(u32) (clk_get_rate(fbi->path_clk) / MHZ_TO_HZ),
				(phy_clk / MHZ_TO_HZ), pixel_clk / MHZ_TO_HZ);
		} else {
			pixel_clk = clk_get_rate(fbi->path_clk);
			s += sprintf(buf, "real path clock %uMhz\n",
				 pixel_clk / MHZ_TO_HZ);
		}
	} else
		s += sprintf(buf, "legacy:real path clock %uMhz\n",
			(u32) clk_get_rate(fbi->clk) / MHZ_TO_HZ);
	mutex_unlock(&fbi->access_ok);
	return s;
}
static ssize_t lcd_freq_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct pxa168fb_info *fbi = dev_get_drvdata(dev);
	u32 rate;

	rate = (int)simple_strtoul(buf, NULL, 10);
	mmp_disp_dfc(fbi, rate);
	return size;
}

DEVICE_ATTR(freq, S_IRUGO | S_IWUSR, lcd_freq_show, lcd_freq_store);

#endif
