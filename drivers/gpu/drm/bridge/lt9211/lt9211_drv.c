/*
 * Analog Devices lt9211 driver
 *
 * Copyright 2012 Anaslog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/slab.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>

#include "lt9211.h"

#include <linux/i2c.h>
#include <linux/of_gpio.h>

#define DRIVER_NAME "lt9211"

static int connect_lt9211 = 2;
/*
int display_debug_timing[8]= {0};
u32 gPixelclk = 0xFF;
u32 gHSR = 0xFF;
u32 gHBPR = 0xFF;
u32 gHDISP = 0xFF;
u32 gHFPR = 0xFF;
u32 gVSR= 0xFF;
u32 gVBPR = 0xFF;
u32 gVDISP = 0xFF;
u32 gVFPR = 0xFF;
*/
int lt9211_is_connected(void)
{
	printk(KERN_INFO "%s  lt9211 connect = %d\n", __func__, connect_lt9211);
	if(connect_lt9211 == 2) {
		connect_lt9211 = 0;
		return 2;
	}
	return connect_lt9211;
}
EXPORT_SYMBOL_GPL(lt9211_is_connected);

void lt9211_lvds_power_off(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s \n", __func__);
	if (lt9211->lvds_vdd_en_gpio) {
		gpiod_set_value_cansleep(lt9211->lvds_vdd_en_gpio, 0);
	}
	return;
}
EXPORT_SYMBOL_GPL(lt9211_lvds_power_off);


void lt9211_lvds_power_on(struct lt9211_data *lt9211)
{

	printk(KERN_INFO "%s \n", __func__);
	if (lt9211->lvds_vdd_en_gpio) {
		gpiod_set_value_cansleep(lt9211->lvds_vdd_en_gpio, 1);
	}

	return;
}
EXPORT_SYMBOL_GPL(lt9211_lvds_power_on);

uint8_t lt9211_read(struct i2c_client *client, int reg)
{
	int ret;
	uint8_t val = 0;
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at reg=0x%02x\n", reg);
		return 0;
	}
	val = ret;
	if(Debug)
		printk("lt9211_read reg=0x%x\n", reg);
	return val;
}

int lt9211_write(struct i2c_client *client, uint8_t reg, uint8_t val)
{
	int ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret)
		dev_err(&client->dev, "failed to write at reg=0x%02x, val=0x%02x\n", reg, val);
	else {
		if(Debug)
			printk("lt9211_write reg=0x%x\n", reg);
	}
	return ret;
}

void lt9211_chipid(struct lt9211_data *lt9211)
{
	uint8_t getid[3] = {0};
	int i;
	lt9211_write(lt9211->client, 0xFF, 0x81);//register bank
		for (i = 0; i < sizeof(getid) /sizeof(uint8_t); i++) {
		getid[i] = lt9211_read(lt9211->client, i);
	}
}

void lt9211_systemint(struct lt9211_data *lt9211)
{
	/* system clock init */
	lt9211_write(lt9211->client, 0xff, 0x82);
	lt9211_write(lt9211->client, 0x01, 0x18);

	lt9211_write(lt9211->client, 0xff, 0x86);
	lt9211_write(lt9211->client, 0x06, 0x61);
	lt9211_write(lt9211->client, 0x07, 0xa8); //fm for sys_clk

	lt9211_write(lt9211->client, 0xff, 0x87); //Init txpll regiseter table default value is incorrect
	lt9211_write(lt9211->client, 0x14, 0x08); //default value
	lt9211_write(lt9211->client, 0x15, 0x00); //default value
	lt9211_write(lt9211->client, 0x18, 0x0f);
	lt9211_write(lt9211->client, 0x22, 0x08); //default value
	lt9211_write(lt9211->client, 0x23, 0x00); //default value
	lt9211_write(lt9211->client, 0x26, 0x0f);
}

void lt9211_mipirx_logic_rst(struct lt9211_data *lt9211)
{
    lt9211_write(lt9211->client,0xff,0x81);
    lt9211_write(lt9211->client,0x0a,0xc0);
    lt9211_write(lt9211->client,0x20,0xbf);  //mipi rx div logic reset,for portb input
    msleep(10);
    lt9211_write(lt9211->client,0x0a,0xc1);
    lt9211_write(lt9211->client,0x20,0xff);
}

void lt9211_mipirxphy(struct lt9211_data *lt9211)
{
	lt9211_write(lt9211->client, 0xff, 0x85);
	lt9211_write(lt9211->client, 0x88, 0x50);
	lt9211_write(lt9211->client, 0xff, 0xd0);
	/* Set Mipi Lanes */
	if(lt9211->lvds_output & OUTPUT_MIPI_1_LANE) {
		lt9211_write(lt9211->client, 0x00, MIPI_1_LANE); // 1 : 1 Lane
	} else if(lt9211->lvds_output & OUTPUT_MIPI_2_LANE) {
		lt9211_write(lt9211->client, 0x00, MIPI_2_LANE); // 2 : 2 Lane
	} else if(lt9211->lvds_output & OUTPUT_MIPI_3_LANE) {
		lt9211_write(lt9211->client, 0x00, MIPI_3_LANE); // 3 : 3 Lane
	} else {
		lt9211_write(lt9211->client, 0x00, MIPI_4_LANE); // 0 : 4 Lane
	}
	/* Mipi rx phy */
	lt9211_write(lt9211->client, 0xff, 0x82);
	lt9211_write(lt9211->client, 0x02, 0x44); //port A mipi rx enable
	/*port A/B input 8205/0a bit6_4:EQ current setting*/
	lt9211_write(lt9211->client, 0x05, 0x36); //port A CK lane swap  0x32--0x36 for WYZN Glassbit2- Port A mipi/lvds rx s2p input clk select: 1 = From outer path.
	lt9211_write(lt9211->client, 0x0d, 0x26); //bit6_4:Port B Mipi/lvds rx abs refer current  0x26 0x76
	lt9211_write(lt9211->client, 0x17, 0x0c);
	lt9211_write(lt9211->client, 0x1d, 0x0c);

	lt9211_write(lt9211->client, 0x0a, 0x81);//eq control for LIEXIN  horizon line display issue 0xf7->0x80
	lt9211_write(lt9211->client, 0x0b, 0x00);//eq control  0x77->0x00
#ifdef _Mipi_PortA_
	/*port a*/
	lt9211_write(lt9211->client, 0x07, 0x9f); //port clk enable  （if only open Portb, poart a's lane0 clk need open）
	lt9211_write(lt9211->client, 0x08, 0xfc); //port lprx enable
#endif
#ifdef _Mipi_PortB_
	/*port a*/
	lt9211_write(lt9211->client, 0x07, 0x9f); //port clk enable  （if only open Portb, poart a's lane0 clk need open）
	lt9211_write(lt9211->client, 0x08, 0xfc); //port lprx enable
	/*port b*/
	lt9211_write(lt9211->client, 0x0f, 0x9F); //port clk enable
	lt9211_write(lt9211->client, 0x10, 0xfc); //port lprx enable
	lt9211_write(lt9211->client, 0x04, 0xa1);
#endif
	/*port diff swap*/
	lt9211_write(lt9211->client, 0x09, 0x01); //port a diff swap
	lt9211_write(lt9211->client, 0x11, 0x01); //port b diff swap

	/*port lane swap*/
	lt9211_write(lt9211->client, 0xff, 0x86);
	lt9211_write(lt9211->client, 0x33, 0x1b); //port a lane swap 1b:no swap
	lt9211_write(lt9211->client, 0x34, 0x1b); //port b lane swap 1b:no swap

}

void lt9211_mipirxdigital(struct lt9211_data *lt9211)
{
	lt9211_write(lt9211->client, 0xff, 0x86);
#ifdef _Mipi_PortA_
	lt9211_write(lt9211->client, 0x30, 0x85); //mipirx HL swap
#endif

#ifdef _Mipi_PortB_
	lt9211_write(lt9211->client, 0x30, 0x8f); //mipirx HL swap
#endif
	lt9211_write(lt9211->client, 0xff, 0xD8);
#ifdef _Mipi_PortA_
	lt9211_write(lt9211->client, 0x16, 0x00); //mipirx HL swap bit7- 0:portAinput
#endif

#ifdef _Mipi_PortB_
	lt9211_write(lt9211->client, 0x16, 0x80); //mipirx HL swap bit7- portBinput
#endif

	lt9211_write(lt9211->client, 0xff, 0xd0);
	lt9211_write(lt9211->client, 0x43, 0x12); //rpta mode enable,ensure da_mlrx_lptx_en=0

	lt9211_write(lt9211->client, 0x02, MIPI_SETTLE_VALUE); //mipi rx controller	//settle value

}

void lt9211_setvideotiming(struct lt9211_data *lt9211, struct video_timing *video_format)
{
	msleep(100);
	lt9211_write(lt9211->client, 0xff, 0xd0);
	lt9211_write(lt9211->client, 0x0d,(uint8_t)(video_format->vtotal>>8)); //vtotal[15:8]
	lt9211_write(lt9211->client, 0x0e,(uint8_t)(video_format->vtotal)); //vtotal[7:0]
	lt9211_write(lt9211->client, 0x0f,(uint8_t)(video_format->vact>>8)); //vactive[15:8]
	lt9211_write(lt9211->client, 0x10,(uint8_t)(video_format->vact)); //vactive[7:0]
	lt9211_write(lt9211->client, 0x15,(uint8_t)(video_format->vs)); //vs[7:0]
	lt9211_write(lt9211->client, 0x17,(uint8_t)(video_format->vfp>>8)); //vfp[15:8]
	lt9211_write(lt9211->client, 0x18,(uint8_t)(video_format->vfp)); //vfp[7:0]

	lt9211_write(lt9211->client, 0x11,(uint8_t)(video_format->htotal>>8)); //htotal[15:8]
	lt9211_write(lt9211->client, 0x12,(uint8_t)(video_format->htotal)); //htotal[7:0]
	lt9211_write(lt9211->client, 0x13,(uint8_t)(video_format->hact>>8)); //hactive[15:8]
	lt9211_write(lt9211->client, 0x14,(uint8_t)(video_format->hact)); //hactive[7:0]
	lt9211_write(lt9211->client, 0x16,(uint8_t)(video_format->hs)); //hs[7:0]
	lt9211_write(lt9211->client, 0x19,(uint8_t)(video_format->hfp>>8)); //hfp[15:8]
	lt9211_write(lt9211->client, 0x1a,(uint8_t)(video_format->hfp)); //hfp[7:0]

}

void lt9211_timingset(struct lt9211_data *lt9211)
{
	uint16_t hact;
	uint16_t vact;
	uint8_t fmt;
	uint8_t pa_lpn = 0;
	struct video_timing lvds_timing = {0};

   	lt9211_mipirx_logic_rst(lt9211);
	msleep(500);//500-->100
	lt9211_write(lt9211->client, 0xff, 0xd0);
	hact = ((lt9211_read(lt9211->client, 0x82))<<8) + lt9211_read(lt9211->client, 0x83);
	hact = hact/3;
	fmt = (lt9211_read(lt9211->client, 0x84) &0x0f);
	vact = ((lt9211_read(lt9211->client, 0x85))<<8) + lt9211_read(lt9211->client, 0x86);
	pa_lpn = lt9211_read(lt9211->client, 0x9c);
	printk(KERN_INFO "%s - hact: %u vact: %u fmt: %x pa_lpn: %x\n", __func__, hact, vact, fmt, pa_lpn);
	printk(KERN_INFO "%s - clk: %lu hact: %u hfp: %u hs: %u hbp: %u vact: %u  vfp: %u vs: %u vbp: %u\n", __func__,
	lt9211->vm.pixelclock,
	lt9211->vm.hactive,
	lt9211->vm.hfront_porch,
	lt9211->vm.hsync_len,
	lt9211->vm.hback_porch,
	lt9211->vm.vactive,
	lt9211->vm.vfront_porch,
	lt9211->vm.vsync_len,
	lt9211->vm.vback_porch);

	lvds_timing.hfp = lt9211->vm.hfront_porch;
	lvds_timing.hs = lt9211->vm.hsync_len;
	lvds_timing.hbp = lt9211->vm.hback_porch;
	lvds_timing.hact = lt9211->vm.hactive;
	lvds_timing.htotal = lvds_timing.hfp + lvds_timing.hs + lvds_timing.hbp + lvds_timing.hact;
	lvds_timing.vfp = lt9211->vm.vfront_porch;
	lvds_timing.vs = lt9211->vm.vsync_len;
	lvds_timing.vbp = lt9211->vm.vback_porch;
	lvds_timing.vact = lt9211->vm.vactive;
	lvds_timing.vtotal = lvds_timing.vfp + lvds_timing.vs + lvds_timing.vbp + lvds_timing.vact;
	lvds_timing.pclk_khz = (lt9211->vm.pixelclock)/1000+1000;

	msleep(100);
	lt9211_setvideotiming(lt9211, &lvds_timing);
	if ((hact == lvds_timing.hact ) && ( vact == lvds_timing.vact ))
	{
		//lt9211_setvideotiming(lt9211, &lvds_timing);
		printk(KERN_INFO "%s - correct timing!!\n", __func__);
	} else {
		printk(KERN_INFO "%s - video_none\n", __func__);
	}

}

void lt9211_mipirxpll(struct lt9211_data *lt9211)
{
	uint32_t  pclk_khz;
	/* dessc pll */
	lt9211_write(lt9211->client, 0xff, 0x82);
	lt9211_write(lt9211->client, 0x2d, 0x48);

	pclk_khz = (lt9211->vm.pixelclock)/1000;
	if(pclk_khz < 44000) {
		lt9211_write(lt9211->client, 0x35, PIXCLK_22M_44M); /*0x83*/
	} else if(pclk_khz < 88000) {
		lt9211_write(lt9211->client, 0x35, PIXCLK_44M_88M); /*0x82*/
	} else if(pclk_khz < 176000) {
		lt9211_write(lt9211->client, 0x35, PIXCLK_88M_176M); /*0x81*/
	} else if(pclk_khz < 352000) {
		lt9211_write(lt9211->client, 0x35, PIXCLK_LARGER_THAN_176M); /*0x80*/
	}

}

void lt9211_mipipcr(struct lt9211_data *lt9211)
{
	uint8_t loopx;
	uint8_t pcr_m;

	lt9211_write(lt9211->client, 0xff, 0xd0);
	lt9211_write(lt9211->client, 0x0c ,0x60);  //fifo position
	lt9211_write(lt9211->client, 0x1c, 0x60);  //fifo position
	lt9211_write(lt9211->client, 0x24, 0x70);  //pcr mode( de hs vs)

	lt9211_write(lt9211->client, 0x2d, 0x30); //M up limit
	lt9211_write(lt9211->client, 0x31, 0x0a); //M down limit

	/*stage1 hs mode*/
	lt9211_write(lt9211->client, 0x25, 0xf0);  //line limit
	lt9211_write(lt9211->client, 0x2a, 0x30);  //step in limit
	lt9211_write(lt9211->client, 0x21, 0x4f);  //hs_step
	lt9211_write(lt9211->client, 0x22, 0x00);

	/*stage2 hs mode*/
	lt9211_write(lt9211->client, 0x1e, 0x01);  //RGD_DIFF_SND[7:4],RGD_DIFF_FST[3:0]
	lt9211_write(lt9211->client, 0x23, 0x80);  //hs_step
	/*stage2 de mode*/
	lt9211_write(lt9211->client, 0x0a, 0x02); //de adjust pre line
	lt9211_write(lt9211->client, 0x38, 0x02); //de_threshold 1
	lt9211_write(lt9211->client, 0x39, 0x04); //de_threshold 2
	lt9211_write(lt9211->client, 0x3a, 0x08); //de_threshold 3
	lt9211_write(lt9211->client, 0x3b, 0x10); //de_threshold 4

	lt9211_write(lt9211->client, 0x3f, 0x04); //de_step 1
	lt9211_write(lt9211->client, 0x40, 0x08); //de_step 2
	lt9211_write(lt9211->client, 0x41, 0x10); //de_step 3
	lt9211_write(lt9211->client, 0x42, 0x20); //de_step 4

	lt9211_write(lt9211->client, 0x2b, 0xa0); //stable out
	//msleep(100);
	lt9211_write(lt9211->client, 0xff, 0xd0);   //enable HW pcr_m
	pcr_m = lt9211_read(lt9211->client, 0x26);
	pcr_m &= 0x7f;
	lt9211_write(lt9211->client, 0x26, pcr_m);
	lt9211_write(lt9211->client, 0x27, 0x0f);

	lt9211_write(lt9211->client, 0xff, 0x81);  //pcr reset
	lt9211_write(lt9211->client, 0x20, 0xbf); // mipi portB div issue
	lt9211_write(lt9211->client, 0x20, 0xff);
	msleep(5);
	lt9211_write(lt9211->client, 0x0B, 0x6F);
	lt9211_write(lt9211->client, 0x0B, 0xFF);


	msleep(800);//800->120
	for(loopx = 0; loopx < 10; loopx++) { //Check pcr_stable 10
		msleep(200);
		lt9211_write(lt9211->client, 0xff, 0xd0);
		if((lt9211_read(lt9211->client, 0x87)) & 0x08) {
			printk(KERN_INFO "%s - LT9211 pcr stable\n", __func__);
			break;
		} else {
			printk(KERN_INFO "%s - LT9211 pcr unstable!!!!\n", __func__);
		}
	}


	lt9211_write(lt9211->client, 0xff, 0xd0);
	printk(KERN_INFO "%s - LT9211 pcr_stable_M=%x\n", __func__, ((lt9211_read(lt9211->client, 0x94)) & 0x7F));//print M value
}

void lt9211_txpll(struct lt9211_data *lt9211)
{
	uint8_t loopx;

	if( (lt9211->lvds_output & OUTPUT_LVDS_1_PORT ) || (lt9211->lvds_output & OUTPUT_LVDS_2_PORT) )
	{
		lt9211_write(lt9211->client, 0xff, 0x82);
		lt9211_write(lt9211->client, 0x36, 0x01); //b7:txpll_pd

		if( lt9211->lvds_output & OUTPUT_LVDS_1_PORT ) {
			lt9211_write(lt9211->client, 0x37, 0x29);
		} else {
			lt9211_write(lt9211->client, 0x37, 0x2a);
		}
		lt9211_write(lt9211->client, 0x38, 0x06);
		lt9211_write(lt9211->client, 0x39, 0x30);
		lt9211_write(lt9211->client, 0x3a, 0x8e);

		lt9211_write(lt9211->client, 0xFF, 0x81);
		lt9211_write(lt9211->client, 0x20, 0xF7);// LVDS Txpll soft reset
		lt9211_write(lt9211->client, 0x20, 0xFF);

		lt9211_write(lt9211->client, 0xff, 0x87);
		lt9211_write(lt9211->client, 0x37, 0x14);
		lt9211_write(lt9211->client, 0x13, 0x00);
		lt9211_write(lt9211->client, 0x13, 0x80);
		msleep(100);
		for(loopx = 0; loopx < 10; loopx++) {  //Check Tx PLL cal

			lt9211_write(lt9211->client, 0xff, 0x87);
			if((lt9211_read(lt9211->client, 0x1f)) & 0x80) {
				if((lt9211_read(lt9211->client, 0x20)) & 0x80) {
					printk(KERN_INFO "%s - pll lock\n", __func__);
				} else {
					printk(KERN_INFO "%s - pll unlocked\n", __func__);
        		}
        		printk(KERN_INFO "%s - pll cal done\n", __func__);
        		break;
        	} else {
        		printk(KERN_INFO "%s - pll unlocked\n", __func__);
        	}
		}
	}
	printk(KERN_INFO "%s - system success\n", __func__);
}

void lt9211_txphy(struct lt9211_data *lt9211)
{

	if(lt9211->test_pattern_en) {
		lt9211_write(lt9211->client, 0xff, 0x82);
		lt9211_write(lt9211->client, 0x62, 0x00); //ttl output disable
		if( lt9211->lvds_output & OUTPUT_LVDS_2_PORT ) {
			lt9211_write(lt9211->client, 0x3b, 0xb8);
		}
		else {
			lt9211_write(lt9211->client, 0x3b, 0x38);  //dual-port lvds tx phy
		}
		lt9211_write(lt9211->client, 0x3e, 0x92);
		lt9211_write(lt9211->client, 0x3f, 0x48);
		lt9211_write(lt9211->client, 0x40, 0x31);
		lt9211_write(lt9211->client, 0x43, 0x80);
		lt9211_write(lt9211->client, 0x44, 0x00);
		lt9211_write(lt9211->client, 0x45, 0x00);
		lt9211_write(lt9211->client, 0x49, 0x00);
		lt9211_write(lt9211->client, 0x4a, 0x01);
		lt9211_write(lt9211->client, 0x4e, 0x00);
		lt9211_write(lt9211->client, 0x4f, 0x00);
		lt9211_write(lt9211->client, 0x50, 0x00);
		lt9211_write(lt9211->client, 0x53, 0x00);
		lt9211_write(lt9211->client, 0x54, 0x01);

		lt9211_write(lt9211->client, 0xff, 0x86);
		lt9211_write(lt9211->client, 0x46, 0x10);
		if( lt9211->lvds_output & OUTPUT_LVDS_2_PORT ) {
			printk(KERN_INFO "%s LVDS Output Port Swap!\n", __func__);
			lt9211_write(lt9211->client, 0x46, 0x40);
		}
		lt9211_write(lt9211->client, 0xff, 0x81);
		lt9211_write(lt9211->client, 0x20, 0x79); //ADD 3.3 TX PHY RESET & mlrx mltx calib reset
		lt9211_write(lt9211->client, 0x20, 0xff);
	} else {
		lt9211_write(lt9211->client, 0xff, 0x82);
		lt9211_write(lt9211->client, 0x23, 0x02);//disable the BTA

		if( (lt9211->lvds_output & OUTPUT_LVDS_1_PORT ) || (lt9211->lvds_output & OUTPUT_LVDS_2_PORT) )
		{
			/* dual-port lvds tx phy */
			lt9211_write(lt9211->client, 0x62, 0x00); //ttl output disable

			if( lt9211->lvds_output & OUTPUT_LVDS_2_PORT ) {
				lt9211_write(lt9211->client, 0x3b, 0x88);//disable lvds output
			} else {
				lt9211_write(lt9211->client, 0x3b, 0x08);//disable lvds output
			}
		}
		lt9211_write(lt9211->client, 0x3e, 0x92);
		lt9211_write(lt9211->client, 0x3f, 0x48);
		lt9211_write(lt9211->client, 0x40, 0x31);
		lt9211_write(lt9211->client, 0x43, 0x80);
		lt9211_write(lt9211->client, 0x44, 0x00);
		lt9211_write(lt9211->client, 0x45, 0x00);
		lt9211_write(lt9211->client, 0x49, 0x00);
		lt9211_write(lt9211->client, 0x4a, 0x01);
		lt9211_write(lt9211->client, 0x4e, 0x00);
		lt9211_write(lt9211->client, 0x4f, 0x00);
		lt9211_write(lt9211->client, 0x50, 0x00);
		lt9211_write(lt9211->client, 0x53, 0x00);
		lt9211_write(lt9211->client, 0x54, 0x01);

		lt9211_write(lt9211->client, 0xff, 0x81);
		lt9211_write(lt9211->client, 0x20, 0x79); //TX PHY RESET & mlrx mltx calib reset
		lt9211_write(lt9211->client, 0x20, 0xff);
	}

}

void lt9211_txdigital(struct lt9211_data *lt9211)
{
	if( (lt9211->lvds_output & OUTPUT_LVDS_1_PORT ) || (lt9211->lvds_output & OUTPUT_LVDS_2_PORT) ) {
		printk(KERN_INFO "%s - ----LT9211 set to OUTPUT_LVDS----\n", __func__);
		lt9211_write(lt9211->client, 0xff, 0x85); /* lvds tx controller */
		lt9211_write(lt9211->client, 0x59, 0x50); //bit4-LVDSTX Display color depth set 1-8bit, 0-6bit;
		if( lt9211->lvds_output & OUTPUT_FORMAT_VESA ) {
        	printk(KERN_INFO "%s - Data Format: VESA\n", __func__);
        	lt9211_write(lt9211->client, 0x59, (lt9211_read(lt9211->client, 0x59) & 0x7f));
    	} else if( lt9211->lvds_output & OUTPUT_FORMAT_JEIDA ) {
        	printk(KERN_INFO "%s - Data Format: JEIDA\n", __func__);
        	lt9211_write(lt9211->client, 0x59, (lt9211_read(lt9211->client, 0x59) | 0x80));
    	}
    	if( lt9211->lvds_output & OUTPUT_BITDEPTH_666 ) {
        	printk(KERN_INFO "%s - ColorDepth: 6Bit\n", __func__);
        	lt9211_write(lt9211->client, 0x59, (lt9211_read(lt9211->client, 0x59) & 0xef));
    	} else if( lt9211->lvds_output & OUTPUT_BITDEPTH_888 ) {
        	printk(KERN_INFO "%s - ColorDepth: 8Bit\n", __func__);
        	lt9211_write(lt9211->client, 0x59, (lt9211_read(lt9211->client, 0x59) | 0x10));
    	}
    	if( lt9211->lvds_output & OUTPUT_SYNC_MODE ) {
        	printk(KERN_INFO "%s - LVDS_MODE: Sync Mode\n", __func__);
        	lt9211_write(lt9211->client, 0x59, (lt9211_read(lt9211->client, 0x59) & 0xdf));
    	} else if( lt9211->lvds_output & OUTPUT_DE_MODE ) {
        	printk(KERN_INFO "%s - LVDS_MODE: De Mode\n", __func__);
        	lt9211_write(lt9211->client, 0x59, (lt9211_read(lt9211->client, 0x59) | 0x20));
    	}

		lt9211_write(lt9211->client, 0x5a, 0xaa);
		lt9211_write(lt9211->client, 0x5b, 0xaa);
		if( lt9211->lvds_output & OUTPUT_LVDS_2_PORT ) {
			lt9211_write(lt9211->client, 0x5c, 0x03);	//lvdstx port sel 01:dual;00:single
		} else {
			lt9211_write(lt9211->client, 0x5c, 0x00);
		}

		lt9211_write(lt9211->client, 0xa1, 0x77);
		lt9211_write(lt9211->client, 0xff, 0x86);
		lt9211_write(lt9211->client, 0x40, 0x40); //tx_src_sel
		/*port src sel*/
		lt9211_write(lt9211->client, 0x41, 0x34);
		lt9211_write(lt9211->client, 0x42, 0x10);
		lt9211_write(lt9211->client, 0x43, 0x23); //pt0_tx_src_sel
		lt9211_write(lt9211->client, 0x44, 0x41);
		lt9211_write(lt9211->client, 0x45, 0x02); //pt1_tx_src_scl
	}
}

void lt9211_clockcheckdebug(struct lt9211_data *lt9211)
{
	uint32_t  fm_value;
	lt9211_write(lt9211->client, 0xff, 0x86);
	if(lt9211->test_pattern_en) {
		lt9211_write(lt9211->client, 0x00,0x12);
		msleep(100);
		fm_value = 0;
		fm_value = (lt9211_read(lt9211->client, 0x08) &(0x0f));
		fm_value = (fm_value<<8) ;
		fm_value = fm_value + lt9211_read(lt9211->client, 0x09);
		fm_value = (fm_value<<8) ;
		fm_value = fm_value + lt9211_read(lt9211->client, 0x0a);
		printk(KERN_INFO "%s - lvds pixclk:  %u\n", __func__, fm_value);
	} else {
		lt9211_write(lt9211->client, 0x00, 0x01);
		msleep(300);
		fm_value = 0;
		fm_value = ((lt9211_read(lt9211->client, 0x08)) & (0x0f));
		fm_value = (fm_value<<8) ;
		fm_value = fm_value + lt9211_read(lt9211->client, 0x09);
		fm_value = (fm_value<<8) ;
		fm_value = fm_value + lt9211_read(lt9211->client, 0x0a);
		printk(KERN_INFO "%s - mipi input byte clock: %d\n", __func__, fm_value);

		lt9211_write(lt9211->client, 0xff, 0x86);
		lt9211_write(lt9211->client, 0x00, 0x0a);
		msleep(300);
		fm_value = 0;
		fm_value = ((lt9211_read(lt9211->client, 0x08)) & (0x0f));
		fm_value = (fm_value<<8) ;
		fm_value = fm_value + lt9211_read(lt9211->client, 0x09);
		fm_value = (fm_value<<8) ;
		fm_value = fm_value + lt9211_read(lt9211->client, 0x0a);
		printk(KERN_INFO "%s - dessc pixel clock: %d\n", __func__, fm_value);
	}
}

void lt9211_videocheckdebug(struct lt9211_data *lt9211)
{

	uint8_t sync_polarity;
	uint16_t hact, vact;
	uint16_t hs, vs;
	uint16_t hbp, vbp;
	uint16_t htotal, vtotal;
	uint16_t hfp, vfp;

	lt9211_write(lt9211->client, 0xff, 0x86);
	sync_polarity = lt9211_read(lt9211->client, 0x70);
	vs = lt9211_read(lt9211->client, 0x71);

	hs = lt9211_read(lt9211->client, 0x72);
	hs = (hs<<8) + lt9211_read(lt9211->client, 0x73);

	vbp = lt9211_read(lt9211->client, 0x74);
	vfp = lt9211_read(lt9211->client, 0x75);

	hbp = lt9211_read(lt9211->client, 0x76);
	hbp = (hbp<<8) + lt9211_read(lt9211->client, 0x77);

	hfp = lt9211_read(lt9211->client, 0x78);
	hfp = (hfp<<8) + lt9211_read(lt9211->client, 0x79);

	vtotal = lt9211_read(lt9211->client, 0x7A);
	vtotal = (vtotal<<8) + lt9211_read(lt9211->client, 0x7B);

	htotal = lt9211_read(lt9211->client, 0x7C);
	htotal = (htotal<<8) + lt9211_read(lt9211->client, 0x7D);

	vact = lt9211_read(lt9211->client, 0x7E);
	vact = (vact<<8)+ lt9211_read(lt9211->client, 0x7F);

	hact = lt9211_read(lt9211->client, 0x80);
	hact = (hact<<8) + lt9211_read(lt9211->client, 0x81);

	printk(KERN_INFO "%s - sync_polarity = %x\n", __func__, sync_polarity);

	printk(KERN_INFO "%s - hfp: %d hs: %d hbp: %d hact: %d htotal: %d\n", __func__, hfp, hs, hbp, hact, htotal);

	printk(KERN_INFO "%s - vfp: %d vs: %d vbp: %d vact: %d vtotal: %d\n", __func__,vfp, vs, vbp, vact, vtotal);

}

void lt9211_lvds_tx_logic_rst(struct lt9211_data *lt9211)
{
	lt9211_write(lt9211->client, 0xff, 0x81);
	lt9211_write(lt9211->client, 0x0d, 0xfb); //LVDS TX LOGIC RESET
	msleep(10);
	lt9211_write(lt9211->client, 0x0d, 0xff); //LVDS TX LOGIC RESET  RELEASE
}

void lt9211_lvds_tx_en(struct lt9211_data *lt9211)
{
	lt9211_write(lt9211->client, 0xff, 0x82);

	if( lt9211->lvds_output & OUTPUT_LVDS_2_PORT ) {
	    lt9211_write(lt9211->client, 0x3b, 0xb8);//dual-port lvds output Enable
	} else {
	    lt9211_write(lt9211->client, 0x3b, 0x38);//signal-port lvds output Enable
	}
}

void lt9211_pattern(struct lt9211_data *lt9211)
{
	uint32_t  pclk_khz;
	uint8_t dessc_pll_post_div;
	uint32_t  pcr_m, pcr_k;
										 //hfp,hs,hbp,hact,htotal,vfp,vs,vbp,vact,vtotal,pixclk Khz
	//struct video_timing video_format  = {96, 64, 96, 1920, 2176, 8, 4, 8, 1080, 1100, 144000};//LM215WF3_SLN1_1920x1080_60Hz
	//struct video_timing video_format  = {80, 34, 80, 1366, 1560, 15, 8, 15, 768, 806, 76000};//LM215WF3_SLN1_1920x1080_60Hz

	struct video_timing video_format = {0};
	video_format.hfp = lt9211->vm.hfront_porch;
	video_format.hs = lt9211->vm.hsync_len;
	video_format.hbp = lt9211->vm.hback_porch;
	video_format.hact = lt9211->vm.hactive;
	video_format.htotal = video_format.hfp + video_format.hs + video_format.hbp + video_format.hact;
	video_format.vfp = lt9211->vm.vfront_porch;
	video_format.vs = lt9211->vm.vsync_len;
	video_format.vbp = lt9211->vm.vback_porch;
	video_format.vact = lt9211->vm.vactive;
	video_format.vtotal = video_format.vfp + video_format.vs + video_format.vbp + video_format.vact;
	video_format.pclk_khz = (lt9211->vm.pixelclock)/1000;

	printk(KERN_INFO "%s - clk: %u hact: %u hfp: %u hs: %u hbp: %u vact: %u  vfp: %u vs: %u vbp: %u\n", __func__,
		video_format.pclk_khz,
		video_format.hact,
		video_format.hfp,
		video_format.hs,
		video_format.hbp,
		video_format.vact,
		video_format.vfp,
		video_format.vs,
		video_format.vbp);


	pclk_khz = video_format.pclk_khz;

	lt9211_write(lt9211->client, 0xff, 0xf9);
	lt9211_write(lt9211->client, 0x3e, 0x80);

	lt9211_write(lt9211->client, 0xff, 0x85);
	lt9211_write(lt9211->client, 0x88, 0xc0);   //0x90:TTL RX-->Mipi TX  ; 0xd0:lvds RX->MipiTX  0xc0:Chip Video pattern gen->Lvd TX

	lt9211_write(lt9211->client, 0xa1, 0x74);
	lt9211_write(lt9211->client, 0xa2, 0xff);

	lt9211_write(lt9211->client, 0xa3,(uint8_t)((video_format.hs+video_format.hbp)/256));
	lt9211_write(lt9211->client, 0xa4,(uint8_t)((video_format.hs+video_format.hbp)%256));//h_start

	lt9211_write(lt9211->client, 0xa5,(uint8_t)((video_format.vs+video_format.vbp)%256));//v_start

	lt9211_write(lt9211->client, 0xa6,(uint8_t)(video_format.hact/256));
	lt9211_write(lt9211->client, 0xa7,(uint8_t)(video_format.hact%256)); //hactive

	lt9211_write(lt9211->client, 0xa8,(uint8_t)(video_format.vact/256));
	lt9211_write(lt9211->client, 0xa9,(uint8_t)(video_format.vact%256));  //vactive

	lt9211_write(lt9211->client, 0xaa,(uint8_t)(video_format.htotal/256));
	lt9211_write(lt9211->client, 0xab,(uint8_t)(video_format.htotal%256));//htotal

	lt9211_write(lt9211->client, 0xac,(uint8_t)(video_format.vtotal/256));
	lt9211_write(lt9211->client, 0xad,(uint8_t)(video_format.vtotal%256));//vtotal

	lt9211_write(lt9211->client, 0xae,(uint8_t)(video_format.hs/256));
	lt9211_write(lt9211->client, 0xaf,(uint8_t)(video_format.hs%256));   //hsa

	lt9211_write(lt9211->client, 0xb0,(uint8_t)(video_format.vs%256));    //vsa

	//dessc pll to generate pixel clk
	lt9211_write(lt9211->client, 0xff, 0x82); //dessc pll
	lt9211_write(lt9211->client, 0x2d, 0x48); //pll ref select xtal

	if(pclk_khz < 44000) {
		lt9211_write(lt9211->client, 0x35, 0x83);
		dessc_pll_post_div = 16;
	} else if(pclk_khz < 88000) {
		lt9211_write(lt9211->client, 0x35, 0x82);
		dessc_pll_post_div = 8;
	} else if(pclk_khz < 176000) {
		lt9211_write(lt9211->client, 0x35, 0x81);
		dessc_pll_post_div = 4;
	} else if(pclk_khz < 352000) {
		lt9211_write(lt9211->client, 0x35, 0x80);
		dessc_pll_post_div = 0;
	}

	pcr_m = (pclk_khz * dessc_pll_post_div) /25;
	pcr_k = pcr_m%1000;
	pcr_m = pcr_m/1000;

	pcr_k <<= 14;

	//pixel clk
	lt9211_write(lt9211->client, 0xff, 0xd0); //pcr
	lt9211_write(lt9211->client, 0x2d, 0x7f);
	lt9211_write(lt9211->client, 0x31, 0x00);

	lt9211_write(lt9211->client, 0x26, 0x80|((uint8_t)pcr_m));
	lt9211_write(lt9211->client, 0x27,(uint8_t)((pcr_k>>16)&0xff)); //K
	lt9211_write(lt9211->client, 0x28,(uint8_t)((pcr_k>>8)&0xff)); //K
	lt9211_write(lt9211->client, 0x29,(uint8_t)(pcr_k&0xff)); //K
}

static void ConvertBoard_power_on(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s \n", __func__);
	if (lt9211->pwr_source_gpio) {
		gpiod_set_value_cansleep(lt9211->pwr_source_gpio, 1);
		msleep(10);
	}
}

static void ConvertBoard_power_off(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s \n", __func__);
	if (lt9211->pwr_source_gpio) {
		gpiod_set_value_cansleep(lt9211->pwr_source_gpio, 0);
	}
}

static void lt9211_chip_enable(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s \n", __func__);
	if(!lt9211->powered) {
		if (lt9211->lt9211_en_gpio) {
			gpiod_set_value_cansleep(lt9211->lt9211_en_gpio, 1);
			msleep(10);//chip enable to init lt9211
		}
	}

	lt9211->powered = true;
}

static void lt9211_chip_shutdown(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s \n", __func__);
	if(lt9211->powered) {
		if (lt9211->lt9211_en_gpio) {
			gpiod_set_value_cansleep(lt9211->lt9211_en_gpio, 0);
			msleep(10);
		}
	}

	lt9211->powered = false;
}

static void lt9211_init_seq(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s \n", __func__);
	lt9211_chipid(lt9211);
	lt9211_systemint(lt9211);

	lt9211_mipirxphy(lt9211);
	lt9211_mipirxdigital(lt9211);
	lt9211_timingset(lt9211);
	lt9211_mipirxpll(lt9211);

	lt9211_mipipcr(lt9211);
	msleep(10);
	lt9211_txpll(lt9211);
	lt9211_lvds_power_on(lt9211);
	msleep(lt9211->t1 + 20);
	lt9211_txphy(lt9211);
	lt9211_txdigital(lt9211);

	lt9211_lvds_tx_logic_rst(lt9211);    //LVDS TX LOGIC RESET
	lt9211_lvds_tx_en(lt9211);           //LVDS TX output enable

	lt9211_clockcheckdebug(lt9211);
	lt9211_videocheckdebug(lt9211);
}

static void lt9211_lvds_pattern_config(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s\n", __func__);
	if(lt9211->test_pattern_en) {
		lt9211_chipid(lt9211);
		lt9211_systemint(lt9211);

		lt9211_pattern(lt9211);
		lt9211_write(lt9211->client, 0xff, 0x81);
		lt9211_write(lt9211->client, 0xff, 0x81);//ADD 3.3 LVDS TX LOGIC RESET

		lt9211_txpll(lt9211);
		lt9211_lvds_power_on(lt9211);
		msleep(lt9211->t1 + 20);
		lt9211_txphy(lt9211);
		lt9211_txdigital(lt9211);

		lt9211_write(lt9211->client, 0xff, 0x81);
		lt9211_write(lt9211->client, 0x0D, 0xff);//ADD 3.3 LVDS TX LOGIC RESET  RELEASE

		lt9211_clockcheckdebug(lt9211);
		lt9211_videocheckdebug(lt9211);
	}
}

static int lt9211_get_modes(struct lt9211_data *lt9211,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	u32 bus_format = lt9211->bus_format;//MEDIA_BUS_FMT_RGB888_1X24;
	//u32 *bus_flags = &connector->display_info.bus_flags;//nxp
	int ret;

	printk(KERN_INFO "%s +\n", __func__);
	mode = drm_mode_create(connector->dev);
	if (!mode) {
		printk(KERN_INFO "%s :Failed to create display mode!\n", __func__);
		return 0;
	}

	drm_display_mode_from_videomode(&lt9211->vm, mode);
	mode->width_mm = lt9211->width_mm;
	mode->height_mm = lt9211->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_probed_add(connector, mode);
	//drm_mode_connector_list_update(connector);//nxp
   	drm_mode_connector_list_update(connector, true);
	connector->display_info.width_mm = lt9211->width_mm;
	connector->display_info.height_mm = lt9211->height_mm;
	connector->display_info.bpc = lt9211->bpc;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);
	if (ret) {
		printk(KERN_ERR "%s return ret=%d\n",  __func__, ret);
		return ret;
	}

	//drm_mode_probed_add(connector, mode);
 	printk(KERN_INFO "%s -\n", __func__);
	return 1;
}

static enum drm_connector_status
lt9211_detect(struct lt9211_data *lt9211)
{
	#define ID_REGISTERS_SZIE (3)
	enum drm_connector_status status = lt9211->status;
	uint8_t id[ID_REGISTERS_SZIE] = {0x18, 0x01, 0xe4};
	uint8_t getid[3] = {0};
	int i;

	if (status == connector_status_connected)
		return status;

	lt9211_write(lt9211->client, 0xFF, 0x81);
	for (i = 0; i < sizeof(getid) /sizeof(uint8_t); i++) {
		getid[i] = lt9211_read(lt9211->client, i);
	}

	if (!memcmp(id, getid, sizeof(id) /sizeof(uint8_t))) {
		printk(KERN_INFO "lt9211_detect successful\n");
		status = connector_status_connected;
	} else {
		printk(KERN_ERR "lt9211_detect fail, 0x%x 0x%x 0x%x\n",getid[0], getid[1], getid[2]);
		status = connector_status_disconnected;
	}

	lt9211->status = status;

	printk(KERN_INFO "%s lt9211->status=%d\n", __func__, lt9211->status);

	return lt9211->status;
}

static int lt9211_mode_valid(struct lt9211_data *lt9211,
			      struct drm_display_mode *mode)
{
	  printk(KERN_INFO "%s: hdisplay =%d, vdisplay = %d, clock=%d\n", __func__,
			mode->hdisplay,mode->vdisplay,mode->clock);

	return MODE_OK;
}

static void lt9211_mode_set(struct lt9211_data *lt9211,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adj_mode)
{
	printk(KERN_INFO "%s \n", __func__);
/*
	if (display_debug_timing[7] == 0xA8) {
			gPixelclk = display_debug_timing[0] ;
			gHFPR =  display_debug_timing[1];
			gHSR = display_debug_timing[2];
			gHBPR =  display_debug_timing[3];
			gVFPR =  display_debug_timing[4];
			gVSR=  display_debug_timing[5];
			gVBPR = display_debug_timing[6];
			gHDISP = mode->hdisplay;
			gVDISP = mode->vdisplay;

			adj_mode->clock = gPixelclk/1000;
			adj_mode->hdisplay = gHDISP;
			adj_mode->hsync_start = gHDISP + gHFPR;
			adj_mode->hsync_end = gHDISP + gHFPR + gHSR ;
			adj_mode->htotal  = gHDISP + gHFPR + gHSR + gHBPR ;
			adj_mode->vdisplay = gVDISP;
			adj_mode->vsync_start = gVDISP + gVFPR;
			adj_mode->vsync_end = gVDISP + gVFPR + gVSR ;
			adj_mode->vtotal = gVDISP + gVFPR + gVSR + gVBPR ;

			mode->clock = gPixelclk/1000;
			mode->hdisplay = gHDISP;
			mode->hsync_start = gHDISP + gHFPR;
			mode->hsync_end = gHDISP + gHFPR + gHSR ;
			mode->htotal  = gHDISP + gHFPR + gHSR + gHBPR ;
			mode->vdisplay = gVDISP;
			mode->vsync_start = gVDISP + gVFPR;
			mode->vsync_end = gVDISP + gVFPR + gVSR ;
			mode->vtotal = gVDISP + gVFPR + gVSR + gVBPR ;

			lt9211->vm.pixelclock = gPixelclk;
			lt9211->vm.hactive = gHDISP;
			lt9211->vm.hfront_porch = gHFPR;
			lt9211->vm.hsync_len = gHSR;
			lt9211->vm.hback_porch = gHBPR;
			lt9211->vm.vactive = gVDISP;
			lt9211->vm.vfront_porch = gVFPR;
			lt9211->vm.vsync_len = gVSR;
			lt9211->vm.vback_porch = gVBPR;

		}
*/
	drm_mode_copy(&lt9211->curr_mode, adj_mode);
	printk(KERN_INFO "%s %d %d %d %d %d %d %d %d %d\n", __func__, adj_mode->clock, adj_mode->hdisplay, adj_mode->hsync_start, adj_mode->hsync_end,
		adj_mode->htotal, adj_mode->vdisplay, adj_mode->vsync_start, adj_mode->vsync_end, adj_mode->vtotal);
	printk(KERN_INFO "%s %d %d %d %d %d %d %d %d %d\n", __func__, mode->clock, mode->hdisplay, mode->hsync_start, mode->hsync_end, mode->htotal,
		mode->vdisplay, mode->vsync_start, mode->vsync_end, mode->vtotal);
}

/* Connector funcs */
static struct lt9211_data *connector_to_lt9211(struct drm_connector *connector)
{
	return container_of(connector, struct lt9211_data, connector);
}


static int lt9211_connector_get_modes(struct drm_connector *connector)
{
	struct lt9211_data *lt9211 = connector_to_lt9211(connector);
	printk(KERN_INFO "%s \n", __func__);
	return lt9211_get_modes(lt9211, connector);
}

static enum drm_mode_status
lt9211_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct lt9211_data *lt9211 = connector_to_lt9211(connector);

	return lt9211_mode_valid(lt9211, mode);
}

static struct drm_connector_helper_funcs lt9211_connector_helper_funcs = {
	.get_modes = lt9211_connector_get_modes,
	.mode_valid = lt9211_connector_mode_valid,
};

static enum drm_connector_status
lt9211_connector_detect(struct drm_connector *connector, bool force)
{
	struct lt9211_data *lt9211 = connector_to_lt9211(connector);

	return lt9211_detect(lt9211);
}

static const struct drm_connector_funcs lt9211_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = lt9211_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* Bridge funcs */
static struct lt9211_data *bridge_to_lt9211(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9211_data, bridge);
}

void lt9211_bridge_enable(struct drm_bridge *bridge)
{
	struct lt9211_data *lt9211 = bridge_to_lt9211(bridge);

	printk(KERN_INFO "%s \n", __func__);
	if (lt9211->enabled)
		return;

	//lt9211_lvds_power_on(lt9211);
	//msleep(lt9211->t1);//lvds power on to lvds signal
	lt9211_chip_enable(lt9211);
	if(!lt9211->test_pattern_en)
		lt9211_init_seq(lt9211);
	else
		lt9211_lvds_pattern_config(lt9211);
	msleep(lt9211->t2);//lvds signal to turn on backlight

	if (lt9211->backlight) {
        lt9211->backlight->props.power = FB_BLANK_UNBLANK;
        backlight_update_status(lt9211->backlight);
    }

	lt9211->enabled = true;

	return;
}

static void lt9211_bridge_disable(struct drm_bridge *bridge)
{
	struct lt9211_data *lt9211 = bridge_to_lt9211(bridge);

	printk(KERN_INFO "%s \n", __func__);
	if (!lt9211->enabled)
		return;

	if (lt9211->backlight) {
        lt9211->backlight->props.power = FB_BLANK_POWERDOWN;
        backlight_update_status(lt9211->backlight);
		msleep(lt9211->t3);//backlight power off to stop lvds signal
    }

	lt9211_chip_shutdown(lt9211);
	msleep(lt9211->t4);//stop lvds signal to turn VCC off
	lt9211_lvds_power_off(lt9211);
	msleep(lt9211->t5);//lvds power off to turn on lvds power
	lt9211->enabled = false;
	return;
}

static void lt9211_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adj_mode)
{
	struct lt9211_data *lt9211 = bridge_to_lt9211(bridge);

	lt9211_mode_set(lt9211, mode, adj_mode);
}

int lt9211_attach_dsi(struct lt9211_data *lt9211)
{
	struct device *dev = lt9211->dev;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	int ret = 0;
	const struct mipi_dsi_device_info info = { .type = "lt9211",
						   				.channel = 0,
										//.node = NULL;
										.node = lt9211->bridge.of_node,
						 				};
	printk(KERN_INFO "%s +\n", __func__);

	host = of_find_mipi_dsi_host_by_node(lt9211->host_node);
	if (!host) {
		dev_err(dev, "failed to find dsi host\n");
		return -EPROBE_DEFER;
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		ret = PTR_ERR(dsi);
		goto err_dsi_device;
	}

	lt9211->dsi = dsi;

	dsi->lanes = lt9211->dsi_lanes;
	dsi->format = lt9211->format;
	dsi->mode_flags = lt9211->mode_flags;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		goto err_dsi_attach;
	}

	printk(KERN_INFO "%s -\n", __func__);

	return 0;

err_dsi_attach:
	mipi_dsi_device_unregister(dsi);
err_dsi_device:
	return ret;
}

void lt9211_detach_dsi(struct lt9211_data *lt9211)
{
	printk(KERN_INFO "%s +\n", __func__);
	mipi_dsi_detach(lt9211->dsi);
	mipi_dsi_device_unregister(lt9211->dsi);
}

static int lt9211_bridge_attach(struct drm_bridge *bridge)
{
	struct lt9211_data *lt9211 = bridge_to_lt9211(bridge);
	int ret;

	printk(KERN_INFO "%s +\n", __func__);

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	lt9211->connector.polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(bridge->dev, &lt9211->connector,
				 &lt9211_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		DRM_ERROR("Failed to initialize connector with drm\n");
		return ret;
	}
	drm_connector_helper_add(&lt9211->connector,
				 &lt9211_connector_helper_funcs);
	drm_mode_connector_attach_encoder(&lt9211->connector, bridge->encoder);

	//ret = lt9211_attach_dsi(lt9211);

	printk(KERN_INFO "%s dsi->mode_flags=0x%lx-\n", __func__, lt9211->dsi->mode_flags);
	return ret;
}

static const struct drm_bridge_funcs lt9211_bridge_funcs = {
	.enable = lt9211_bridge_enable,
	.disable = lt9211_bridge_disable,
	.mode_set = lt9211_bridge_mode_set,
	.attach = lt9211_bridge_attach,
};

static const struct display_timing asus_lt9211_default_mode = {
	.pixelclock = { 143700000, 143700000, 143700000 },
	.hactive = { 1920, 1920, 1920},
	.hfront_porch = { 80, 80, 80 },
	.hsync_len = { 88, 88, 88 },
	.hback_porch = {88, 88, 88 },
	.vactive = { 1080, 1080, 1080 },
	.vfront_porch = { 4, 4, 4 },
	.vsync_len = { 8, 8, 8 },
	.vback_porch = { 8, 8, 8 },
	.flags = DISPLAY_FLAGS_HSYNC_LOW |
			DISPLAY_FLAGS_VSYNC_LOW |
			DISPLAY_FLAGS_DE_HIGH |
			DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};


static int lt9211_parse_dt(struct device_node *np,
			   struct lt9211_data *data)
{
	struct device_node *endpoint, *backlight;
	struct device *dev = data->dev;
	int ret;
	data->lvds_output = 0;
	printk(KERN_INFO "%s +\n", __func__);

	endpoint = of_graph_get_next_endpoint(np, NULL);
	if (!endpoint) {
		dev_err(dev, "error:lt9211_parse_dt can not get next_endpoint\n");
		return -ENODEV;
	} else {
		printk("lt9211_parse_dt  endpoint->name=%s type=%s full_name=%s\n", endpoint->name, endpoint->type, endpoint->full_name);
	}
	data->host_node = of_graph_get_remote_port_parent(endpoint);
	if (!data->host_node) {
		of_node_put(endpoint);
		return -ENODEV;
	}else {
		printk("lt9211_parse_dt endpoint->name=%s type=%s full_name=%s\n", data->host_node->name, data->host_node->type, data->host_node->full_name);
	}

	ret = of_property_read_u32(dev->of_node, "dsi-lanes", &data->dsi_lanes);
	if(data->dsi_lanes == 1) {
		data->lvds_output |= OUTPUT_MIPI_1_LANE;
	} else if(data->dsi_lanes == 2) {
		data->lvds_output |= OUTPUT_MIPI_2_LANE;
	} else if(data->dsi_lanes == 3) {
		data->lvds_output |= OUTPUT_MIPI_3_LANE;
	} else if(data->dsi_lanes == 4) {
		data->lvds_output |= OUTPUT_MIPI_4_LANE;
	} else {
		dev_err(dev, "Invalid dsi-lanes: %d\n", data->dsi_lanes);
		return -EINVAL;
	}

	ret = of_property_read_u32(dev->of_node, "bus-format", &data->bus_format);
	ret = of_property_read_u32(dev->of_node, "bpc", &data->bpc);
	ret = of_property_read_u32(dev->of_node, "dsi,flags", &data->mode_flags);
	ret = of_property_read_u32(dev->of_node, "dsi,format", &data->format);

	printk(KERN_INFO "lt9211_parse_dt bus_format=%x bpc=%u format =%u mode_flags=%u dsi_lanes=%u\n",data->bus_format, data->bpc, data->format, data->mode_flags, data->dsi_lanes);

	ret = of_property_read_u32(np, "t1", &data->t1);
	ret = of_property_read_u32(np, "t2", &data->t2);
	ret = of_property_read_u32(np, "t3", &data->t3);
	ret = of_property_read_u32(np, "t4", &data->t4);
	ret = of_property_read_u32(np, "t5", &data->t5);

	printk("lt9211_parse_dt T1=%d T2=%d T3=%d T4=%d T5=%d\n", data->t1, data->t2, data->t3, data->t4, data->t5);

	ret = of_get_videomode(np, &data->vm, 0);

	printk(KERN_INFO "%s - clk: %lu hact: %u hfp: %u hs: %u hbp: %u vact: %u vfp: %u vs: %u vbp: %u\n", __func__,
		data->vm.pixelclock,
		data->vm.hactive,
		data->vm.hfront_porch,
		data->vm.hsync_len,
		data->vm.hback_porch,
		data->vm.vactive,
		data->vm.vfront_porch,
		data->vm.vsync_len,
		data->vm.vback_porch);

	if (ret < 0) {
		videomode_from_timing(&asus_lt9211_default_mode, &data->vm);
	}

	ret = of_property_read_u32(np, "lvds-width-mm", &data->width_mm);
	ret = of_property_read_u32(np, "lvds-height-mm", &data->height_mm);

	ret = of_property_read_u32(np, "lvds-format", &data->lvds_format);

	if(data->lvds_format == 1) {
		data->lvds_output |= OUTPUT_FORMAT_JEIDA;
	} else if(data->lvds_format == 2) {
		data->lvds_output |= OUTPUT_FORMAT_VESA;
	} else {
		dev_err(dev, "Invalid lvds-format: %d\n", data->lvds_format);
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "lvds-bpp", &data->lvds_bpp);

	if(data->lvds_bpp == 24) {
		data->lvds_output |= OUTPUT_BITDEPTH_888;
	} else {
		data->lvds_output |= OUTPUT_BITDEPTH_666;
	}

	data->dual_link = of_property_read_bool(np, "dual-link");
	if(data->dual_link) {
		data->lvds_output |= OUTPUT_LVDS_2_PORT;
	} else {
		data->lvds_output |= OUTPUT_LVDS_1_PORT;
	}

	if(of_property_read_bool(np, "de-mode")) {
		data->lvds_output |= OUTPUT_DE_MODE;
	} else {
		data->lvds_output |= OUTPUT_SYNC_MODE;
	}

	data->test_pattern_en = of_property_read_bool(np, "test-pattern");

	printk(KERN_INFO "lt9211_parse_dt lvds-format=%u lvds-bpp=%u test-pattern=%s\n", data->lvds_format, data->lvds_bpp, data->test_pattern_en? "true" : "false");

	data->lt9211_en_gpio = devm_gpiod_get_optional(dev, "EN",  GPIOD_OUT_LOW);
	if (IS_ERR(data->lt9211_en_gpio)) {
		printk(KERN_INFO "lt9211_parse_dt: failed to get EN GPIO \n");
	}

	data->lvds_vdd_en_gpio = devm_gpiod_get_optional(dev, "lvds_vdd_en", GPIOD_OUT_LOW);
	if (IS_ERR(data->lvds_vdd_en_gpio)) {
		printk(KERN_INFO "lt9211_parse_dt: failed to get lvds_vdd_en_gpio\n");
	}

	data->pwr_source_gpio = devm_gpiod_get_optional(dev, "pwr_source", GPIOD_OUT_LOW);
	if (IS_ERR(data->pwr_source_gpio)) {
		printk(KERN_INFO "lt9211_parse_dt: failed to get  pwr_source gpio\n");
	}

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		data->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!data->backlight) {
			printk(KERN_ERR "lt9211_parse_dt: failed to find backlight\n");
		}
	} else {
		printk(KERN_ERR "lt9211_parse_dt: failed to parse backlight handle\n");
	}

	printk(KERN_INFO "%s -\n", __func__);

	return ret;
}

static int lt9211_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct lt9211_data *lt9211;
	struct device *dev = &i2c->dev;
	int ret;

	printk(KERN_INFO "%s +\n", __func__);
	if (!dev->of_node)
		return -EINVAL;

	lt9211 = devm_kzalloc(dev, sizeof(struct lt9211_data), GFP_KERNEL);
	if (!lt9211)
		return -ENOMEM;

	dev->driver_data = lt9211;
	lt9211->dev = dev;
	lt9211->client = i2c;
	lt9211->enabled = false;
	lt9211->powered = false;
	lt9211->status = connector_status_disconnected;
	lt9211->test_pattern_en = false;


	ret = lt9211_parse_dt(dev->of_node, lt9211);
	if (ret)
		return ret;

	ConvertBoard_power_on(lt9211);
	lt9211_chip_enable(lt9211);
	lt9211_detect(lt9211);

	if (lt9211->status == connector_status_connected) {
		printk(KERN_INFO "%s : lt9211 is connected!\n", __func__);
		connect_lt9211 = 1;
	} else {
		printk(KERN_INFO "%s : lt9211 is disconnected!\n", __func__);
		connect_lt9211 = 0;
		ret = -ENODEV;
		return ret;
	}

	i2c_set_clientdata(i2c, lt9211);

	lt9211->bridge.funcs = &lt9211_bridge_funcs;
	lt9211->bridge.of_node = dev->of_node;
	drm_bridge_add(&lt9211->bridge);

	lt9211_attach_dsi(lt9211);

	printk(KERN_INFO "%s -\n", __func__);

	return 0;
}

static int lt9211_remove(struct i2c_client *i2c)
{
	struct lt9211_data *lt9211 = i2c_get_clientdata(i2c);

	lt9211_bridge_disable(&lt9211->bridge);
	lt9211_detach_dsi(lt9211);
	drm_bridge_remove(&lt9211->bridge);

	return 0;
}

static void  lt9211_shutdown(struct i2c_client *i2c)
{
	struct lt9211_data *lt9211 = i2c_get_clientdata(i2c);

	printk(KERN_INFO "%s\n", __func__);

	lt9211_bridge_disable(&lt9211->bridge);

	ConvertBoard_power_off(lt9211);

	return;
}

static const struct i2c_device_id lt9211_i2c_ids[] = {
	{ "lt9211"},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lt9211_i2c_ids);

static const struct of_device_id lt9211_of_ids[] = {
	{ .compatible = "asus,lt9211",},
	{ }
};
MODULE_DEVICE_TABLE(of, lt9211_of_ids);

static struct mipi_dsi_driver lt9211_dsi_driver = {
	.driver.name = "lt9211",
};

static struct i2c_driver lt9211_driver = {
	.driver = {
		.name = "lt9211",
		.of_match_table = lt9211_of_ids,
	},
	.id_table = lt9211_i2c_ids,
	.probe = lt9211_probe,
	.remove = lt9211_remove,
	.shutdown = lt9211_shutdown,
};

static int __init lt9211_init(void)
{
	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_register(&lt9211_dsi_driver);

	return i2c_add_driver(&lt9211_driver);
}
module_init(lt9211_init);

static void __exit lt9211_exit(void)
{
	i2c_del_driver(&lt9211_driver);

	if (IS_ENABLED(CONFIG_DRM_MIPI_DSI))
		mipi_dsi_driver_unregister(&lt9211_dsi_driver);
}
module_exit(lt9211_exit);

MODULE_AUTHOR("ASUS");
MODULE_DESCRIPTION("lt9211 mipi to lvds driver");
MODULE_LICENSE("GPL");
