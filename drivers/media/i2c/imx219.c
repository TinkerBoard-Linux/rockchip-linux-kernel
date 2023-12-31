// SPDX-License-Identifier: GPL-2.0
/*
 * A V4L2 driver for Sony IMX219 cameras.
 * Copyright (C) 2019, Raspberry Pi (Trading) Ltd
 *
 * Based on Sony imx258 camera driver
 * Copyright (C) 2018 Intel Corporation
 *
 * DT / fwnode changes, and regulator / GPIO control taken from imx214 driver
 * Copyright 2018 Qtechnology A/S
 *
 * Flip handling taken from the Sony IMX319 driver.
 * Copyright (C) 2018 Intel Corporation
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/rk-camera-module.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <asm/unaligned.h>

#define IMX219_REG_VALUE_08BIT		1
#define IMX219_REG_VALUE_16BIT		2

#define IMX219_TABLE_END		0xffff
#define IMX219_REG_MODE_SELECT		0x0100
#define IMX219_MODE_STANDBY		0x00
#define IMX219_MODE_STREAMING		0x01

/* Chip ID */
#define IMX219_REG_CHIP_ID		0x0000
#define IMX219_CHIP_ID			0x0219

/* External clock frequency is 24.0M */
#define IMX219_XCLK_FREQ		24000000

/* Pixel rate is fixed at 182.4M for all the modes */
#define IMX219_PIXEL_RATE		182400000

#define IMX219_DEFAULT_LINK_FREQ	456000000

/* V_TIMING internal */
#define IMX219_REG_VTS			0x0160
#define IMX219_VTS_15FPS		0x0dc6
#define IMX219_VTS_30FPS_1080P		0x06e3
#define IMX219_VTS_30FPS_BINNED		0x06e3
#define IMX219_VTS_30FPS_640x480	0x06e3
#define IMX219_VTS_MAX			0xffff

#define IMX219_VBLANK_MIN		4

#define IMX219_NAME			"imx219"
/*Frame Length Line*/
#define IMX219_FLL_MIN			0x08a6
#define IMX219_FLL_MAX			0xffff
#define IMX219_FLL_STEP			1
#define IMX219_FLL_DEFAULT		0x0c98

/* HBLANK control - read only */
#define IMX219_PPL_DEFAULT		3448

/* IMX219 supported geometry */
#define IMX219_TABLE_END		0xffff
#define IMX219_ANALOGUE_GAIN_MULTIPLIER	256
#define IMX219_ANALOGUE_GAIN_MIN	(1 * IMX219_ANALOGUE_GAIN_MULTIPLIER)
#define IMX219_ANALOGUE_GAIN_MAX	(11 * IMX219_ANALOGUE_GAIN_MULTIPLIER)
#define IMX219_ANALOGUE_GAIN_DEFAULT	(2 * IMX219_ANALOGUE_GAIN_MULTIPLIER)

/* In dB*256 */
#define IMX219_DIGITAL_GAIN_MIN		256
#define IMX219_DIGITAL_GAIN_MAX		43663
#define IMX219_DIGITAL_GAIN_DEFAULT	256

#define IMX219_DIGITAL_EXPOSURE_MIN	0
#define IMX219_DIGITAL_EXPOSURE_MAX	4095
#define IMX219_DIGITAL_EXPOSURE_DEFAULT	1575

#define IMX219_EXP_LINES_MARGIN	4

static const s64 link_freq_menu_items[] = {
	456000000,
};

struct imx219_reg {
	u16 addr;
	u8 val;
};

struct imx219_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	const struct imx219_reg *reg_list;
};

/* MCLK:24MHz  3280x2464  21.2fps   MIPI LANE2 */
static const struct imx219_reg imx219_init_tab_3280_2464_21fps[] = {
	{0x30EB, 0x05},		/* Access Code for address over 0x3000 */
	{0x30EB, 0x0C},		/* Access Code for address over 0x3000 */
	{0x300A, 0xFF},		/* Access Code for address over 0x3000 */
	{0x300B, 0xFF},		/* Access Code for address over 0x3000 */
	{0x30EB, 0x05},		/* Access Code for address over 0x3000 */
	{0x30EB, 0x09},		/* Access Code for address over 0x3000 */
	{0x0114, 0x01},		/* CSI_LANE_MODE[1:0} */
	{0x0128, 0x00},		/* DPHY_CNTRL */
	{0x012A, 0x18},		/* EXCK_FREQ[15:8] */
	{0x012B, 0x00},		/* EXCK_FREQ[7:0] */
//	{0x015A, 0x01},		/* INTEG TIME[15:8] */
//	{0x015B, 0xF4},		/* INTEG TIME[7:0] */
	{0x0160, 0x09},		/* FRM_LENGTH_A[15:8] */
	{0x0161, 0xD7},		/* FRM_LENGTH_A[7:0] */
	{0x0162, 0x0D},		/* LINE_LENGTH_A[15:8] */
	{0x0163, 0x78},		/* LINE_LENGTH_A[7:0] */
	{0x0260, 0x09},		/* FRM_LENGTH_B[15:8] */
	{0x0261, 0xC4},		/* FRM_LENGTH_B[7:0] */
	{0x0262, 0x0D},		/* LINE_LENGTH_B[15:8] */
	{0x0263, 0x78},		/* LINE_LENGTH_B[7:0] */
	{0x0170, 0x01},		/* X_ODD_INC_A[2:0] */
	{0x0171, 0x01},		/* Y_ODD_INC_A[2:0] */
	{0x0270, 0x01},		/* X_ODD_INC_B[2:0] */
	{0x0271, 0x01},		/* Y_ODD_INC_B[2:0] */
	{0x0174, 0x00},		/* BINNING_MODE_H_A */
	{0x0175, 0x00},		/* BINNING_MODE_V_A */
	{0x0274, 0x00},		/* BINNING_MODE_H_B */
	{0x0275, 0x00},		/* BINNING_MODE_V_B */
	{0x018C, 0x0A},		/* CSI_DATA_FORMAT_A[15:8] */
	{0x018D, 0x0A},		/* CSI_DATA_FORMAT_A[7:0] */
	{0x028C, 0x0A},		/* CSI_DATA_FORMAT_B[15:8] */
	{0x028D, 0x0A},		/* CSI_DATA_FORMAT_B[7:0] */
	{0x0301, 0x05},		/* VTPXCK_DIV */
	{0x0303, 0x01},		/* VTSYCK_DIV */
	{0x0304, 0x03},		/* PREPLLCK_VT_DIV[3:0] */
	{0x0305, 0x03},		/* PREPLLCK_OP_DIV[3:0] */
	{0x0306, 0x00},		/* PLL_VT_MPY[10:8] */
	{0x0307, 0x39},		/* PLL_VT_MPY[7:0] */
	{0x0309, 0x0A},		/* OPPXCK_DIV[4:0] */
	{0x030B, 0x01},		/* OPSYCK_DIV */
	{0x030C, 0x00},		/* PLL_OP_MPY[10:8] */
	{0x030D, 0x72},		/* PLL_OP_MPY[7:0] */
	{0x455E, 0x00},		/* CIS Tuning */
	{0x471E, 0x4B},		/* CIS Tuning */
	{0x4767, 0x0F},		/* CIS Tuning */
	{0x4750, 0x14},		/* CIS Tuning */
	{0x47B4, 0x14},		/* CIS Tuning */
	{IMX219_TABLE_END, 0x00}
};

/* MCLK:24MHz  1920x1080  30fps   MIPI LANE2 */
static const struct imx219_reg imx219_init_tab_1920_1080_30fps[] = {
	{0x30EB, 0x05},
	{0x30EB, 0x0C},
	{0x300A, 0xFF},
	{0x300B, 0xFF},
	{0x30EB, 0x05},
	{0x30EB, 0x09},
	{0x0114, 0x01},
	{0x0128, 0x00},
	{0x012A, 0x18},
	{0x012B, 0x00},
	{0x0160, 0x06},
	{0x0161, 0xE6},
	{0x0162, 0x0D},
	{0x0163, 0x78},
	{0x0164, 0x02},
	{0x0165, 0xA8},
	{0x0166, 0x0A},
	{0x0167, 0x27},
	{0x0168, 0x02},
	{0x0169, 0xB4},
	{0x016A, 0x06},
	{0x016B, 0xEB},
	{0x016C, 0x07},
	{0x016D, 0x80},
	{0x016E, 0x04},
	{0x016F, 0x38},
	{0x0170, 0x01},
	{0x0171, 0x01},
	{0x0174, 0x00},
	{0x0175, 0x00},
	{0x018C, 0x0A},
	{0x018D, 0x0A},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0304, 0x03},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0x39},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0x72},
	{0x455E, 0x00},
	{0x471E, 0x4B},
	{0x4767, 0x0F},
	{0x4750, 0x14},
	{0x4540, 0x00},
	{0x47B4, 0x14},
	{IMX219_TABLE_END, 0x00}
};

static const struct imx219_reg start[] = {
	{0x0100, 0x01},		/* mode select streaming on */
	{IMX219_TABLE_END, 0x00}
};

static const struct imx219_reg stop[] = {
	{0x0100, 0x00},		/* mode select streaming off */
	{IMX219_TABLE_END, 0x00}
};

static const char * const imx219_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"PN9"
};

enum {
	TEST_PATTERN_DISABLED,
	TEST_PATTERN_SOLID_BLACK,
	TEST_PATTERN_SOLID_WHITE,
	TEST_PATTERN_SOLID_RED,
	TEST_PATTERN_SOLID_GREEN,
	TEST_PATTERN_SOLID_BLUE,
	TEST_PATTERN_COLOR_BAR,
	TEST_PATTERN_FADE_TO_GREY_COLOR_BAR,
	TEST_PATTERN_PN9,
	TEST_PATTERN_16_SPLIT_COLOR_BAR,
	TEST_PATTERN_16_SPLIT_INVERTED_COLOR_BAR,
	TEST_PATTERN_COLUMN_COUNTER,
	TEST_PATTERN_INVERTED_COLUMN_COUNTER,
	TEST_PATTERN_PN31,
	TEST_PATTERN_MAX
};

static const char *const tp_qmenu[] = {
	"Disabled",
	"Solid Black",
	"Solid White",
	"Solid Red",
	"Solid Green",
	"Solid Blue",
	"Color Bar",
	"Fade to Grey Color Bar",
	"PN9",
	"16 Split Color Bar",
	"16 Split Inverted Color Bar",
	"Column Counter",
	"Inverted Column Counter",
	"PN31",
};

/*
 * The supported formats.
 * This table MUST contain 4 entries per format, to cover the various flip
 * combinations in the order
 * - no flip
 * - h flip
 * - v flip
 * - h&v flips
 */
static const u32 codes[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
};

/* Mode configs */
static const struct imx219_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 0x0d78 - IMX219_EXP_LINES_MARGIN,
		.vts_def = 0x06E6,
		.reg_list = imx219_init_tab_1920_1080_30fps,
	},
	{
		.width = 3280,
		.height = 2464,
		.max_fps = {
			.numerator = 10000,
			.denominator = 210000,
		},
		.hts_def = 0x0d78 - IMX219_EXP_LINES_MARGIN,
		.vts_def = 0x09d7,
		.reg_list = imx219_init_tab_3280_2464_21fps,
	},
};

struct imx219 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt fmt;
	struct clk *xclk; /* system clock to IMX219 */
	u32 xclk_freq;
	struct v4l2_rect crop_rect;

	struct gpio_desc *reset_gpio;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	int hflip;
	int vflip;
	u8 analogue_gain;
	u16 digital_gain;	/* bits 11:0 */
	u16 exposure_time;
	u16 test_pattern;
	u16 test_pattern_solid_color_r;
	u16 test_pattern_solid_color_gr;
	u16 test_pattern_solid_color_b;
	u16 test_pattern_solid_color_gb;
	/* Current mode */
	const struct imx219_mode *cur_mode;

	/*
	 * Mutex for serialized access:
	 * Protect sensor module set pad format and start/stop streaming safely.
	 */
	struct mutex mutex;

	u32 cfg_num;
	u16 cur_vts;
	u32 module_index;
	const char *module_facing;
	const char *module_name;
	const char *len_name;

	struct gpio_desc        *enable_gpio;
};

static inline struct imx219 *to_imx219(struct v4l2_subdev *_sd)
{
	return container_of(_sd, struct imx219, sd);
}

static int reg_read(struct i2c_client *client, const u16 addr)
{
	u8 buf[2] = {addr >> 8, addr & 0xff};
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr  = client->addr,
			.flags = 0,
			.len   = 2,
			.buf   = buf,
		}, {
			.addr  = client->addr,
			.flags = I2C_M_RD,
			.len   = 1,
			.buf   = buf,
		},
	};

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0) {
		dev_warn(&client->dev, "Reading register %x from %x failed\n",
			 addr, client->addr);
		return ret;
	}

	return buf[0];
}

static int reg_write(struct i2c_client *client, const u16 addr, const u8 data)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	u8 tx[3];
	int ret;

	msg.addr = client->addr;
	msg.buf = tx;
	msg.len = 3;
	msg.flags = 0;
	tx[0] = addr >> 8;
	tx[1] = addr & 0xff;
	tx[2] = data;
	ret = i2c_transfer(adap, &msg, 1);
	mdelay(2);

	return ret == 1 ? 0 : -EIO;
}

static int reg_write_table(struct i2c_client *client,
			   const struct imx219_reg table[])
{
	const struct imx219_reg *reg;
	int ret;

	for (reg = table; reg->addr != IMX219_TABLE_END; reg++) {
		ret = reg_write(client, reg->addr, reg->val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void imx219_set_default_format(struct imx219 *imx219)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = &imx219->fmt;
	fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
	fmt->width = supported_modes[0].width;
	fmt->height = supported_modes[0].height;
	fmt->field = V4L2_FIELD_NONE;
}

static int imx219_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx219 *imx219 = to_imx219(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);

	mutex_lock(&imx219->mutex);

	/* Initialize try_fmt */
	try_fmt->width = supported_modes[0].width;
	try_fmt->height = supported_modes[0].height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx219->mutex);

	return 0;
}

/* V4L2 ctrl operations */
static int imx219_s_ctrl_test_pattern(struct v4l2_ctrl *ctrl)
{
	struct imx219 *priv =
	    container_of(ctrl->handler, struct imx219, ctrl_handler);

	switch (ctrl->val) {
	case TEST_PATTERN_DISABLED:
		priv->test_pattern = 0x0000;
		break;
	case TEST_PATTERN_SOLID_BLACK:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0000;
		priv->test_pattern_solid_color_gr = 0x0000;
		priv->test_pattern_solid_color_b = 0x0000;
		priv->test_pattern_solid_color_gb = 0x0000;
		break;
	case TEST_PATTERN_SOLID_WHITE:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0fff;
		priv->test_pattern_solid_color_gr = 0x0fff;
		priv->test_pattern_solid_color_b = 0x0fff;
		priv->test_pattern_solid_color_gb = 0x0fff;
		break;
	case TEST_PATTERN_SOLID_RED:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0fff;
		priv->test_pattern_solid_color_gr = 0x0000;
		priv->test_pattern_solid_color_b = 0x0000;
		priv->test_pattern_solid_color_gb = 0x0000;
		break;
	case TEST_PATTERN_SOLID_GREEN:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0000;
		priv->test_pattern_solid_color_gr = 0x0fff;
		priv->test_pattern_solid_color_b = 0x0000;
		priv->test_pattern_solid_color_gb = 0x0fff;
		break;
	case TEST_PATTERN_SOLID_BLUE:
		priv->test_pattern = 0x0001;
		priv->test_pattern_solid_color_r = 0x0000;
		priv->test_pattern_solid_color_gr = 0x0000;
		priv->test_pattern_solid_color_b = 0x0fff;
		priv->test_pattern_solid_color_gb = 0x0000;
		break;
	case TEST_PATTERN_COLOR_BAR:
		priv->test_pattern = 0x0002;
		break;
	case TEST_PATTERN_FADE_TO_GREY_COLOR_BAR:
		priv->test_pattern = 0x0003;
		break;
	case TEST_PATTERN_PN9:
		priv->test_pattern = 0x0004;
		break;
	case TEST_PATTERN_16_SPLIT_COLOR_BAR:
		priv->test_pattern = 0x0005;
		break;
	case TEST_PATTERN_16_SPLIT_INVERTED_COLOR_BAR:
		priv->test_pattern = 0x0006;
		break;
	case TEST_PATTERN_COLUMN_COUNTER:
		priv->test_pattern = 0x0007;
		break;
	case TEST_PATTERN_INVERTED_COLUMN_COUNTER:
		priv->test_pattern = 0x0008;
		break;
	case TEST_PATTERN_PN31:
		priv->test_pattern = 0x0009;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int imx219_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(sd);
	u8 reg = 0x00;
	int ret;

	if (!enable)
		return reg_write_table(client, stop);

	ret = reg_write_table(client, priv->cur_mode->reg_list);
	if (ret)
		return ret;

	/* Handle crop */
	ret = reg_write(client, 0x0164, priv->crop_rect.left >> 8);
	ret |= reg_write(client, 0x0165, priv->crop_rect.left & 0xff);
	ret |= reg_write(client, 0x0166, (priv->crop_rect.left + priv->crop_rect.width - 1) >> 8);
	ret |= reg_write(client, 0x0167, (priv->crop_rect.left + priv->crop_rect.width - 1) & 0xff);
	ret |= reg_write(client, 0x0168, priv->crop_rect.top >> 8);
	ret |= reg_write(client, 0x0169, priv->crop_rect.top & 0xff);
	ret |= reg_write(client, 0x016A, (priv->crop_rect.top + priv->crop_rect.height - 1) >> 8);
	ret |= reg_write(client, 0x016B, (priv->crop_rect.top + priv->crop_rect.height - 1) & 0xff);
	ret |= reg_write(client, 0x016C, priv->crop_rect.width >> 8);
	ret |= reg_write(client, 0x016D, priv->crop_rect.width & 0xff);
	ret |= reg_write(client, 0x016E, priv->crop_rect.height >> 8);
	ret |= reg_write(client, 0x016F, priv->crop_rect.height & 0xff);

	if (ret)
		return ret;

	/* Handle flip/mirror */
	if (priv->hflip)
		reg |= 0x1;
	if (priv->vflip)
		reg |= 0x2;

	ret = reg_write(client, 0x0172, reg);
	if (ret)
		return ret;

	/* Handle test pattern */
	if (priv->test_pattern) {
		ret = reg_write(client, 0x0600, priv->test_pattern >> 8);
		ret |= reg_write(client, 0x0601, priv->test_pattern & 0xff);
		ret |= reg_write(client, 0x0602,
				 priv->test_pattern_solid_color_r >> 8);
		ret |= reg_write(client, 0x0603,
				 priv->test_pattern_solid_color_r & 0xff);
		ret |= reg_write(client, 0x0604,
				 priv->test_pattern_solid_color_gr >> 8);
		ret |= reg_write(client, 0x0605,
				 priv->test_pattern_solid_color_gr & 0xff);
		ret |= reg_write(client, 0x0606,
				 priv->test_pattern_solid_color_b >> 8);
		ret |= reg_write(client, 0x0607,
				 priv->test_pattern_solid_color_b & 0xff);
		ret |= reg_write(client, 0x0608,
				 priv->test_pattern_solid_color_gb >> 8);
		ret |= reg_write(client, 0x0609,
				 priv->test_pattern_solid_color_gb & 0xff);
		ret |= reg_write(client, 0x0620, priv->crop_rect.left >> 8);
		ret |= reg_write(client, 0x0621, priv->crop_rect.left & 0xff);
		ret |= reg_write(client, 0x0622, priv->crop_rect.top >> 8);
		ret |= reg_write(client, 0x0623, priv->crop_rect.top & 0xff);
		ret |= reg_write(client, 0x0624, priv->crop_rect.width >> 8);
		ret |= reg_write(client, 0x0625, priv->crop_rect.width & 0xff);
		ret |= reg_write(client, 0x0626, priv->crop_rect.height >> 8);
		ret |= reg_write(client, 0x0627, priv->crop_rect.height & 0xff);
	} else {
		ret = reg_write(client, 0x0600, 0x00);
		ret |= reg_write(client, 0x0601, 0x00);
	}

	priv->cur_vts = priv->cur_mode->vts_def - IMX219_EXP_LINES_MARGIN;
	if (ret)
		return ret;

	return reg_write_table(client, start);
}

static int imx219_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx219 *priv =
	    container_of(ctrl->handler, struct imx219, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	u8 reg;
	int ret;
	u16 gain = 256;
	u16 a_gain = 256;
	u16 d_gain = 1;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		priv->hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		priv->vflip = ctrl->val;
		break;

	case V4L2_CID_ANALOGUE_GAIN:
	case V4L2_CID_GAIN:
		/*
		 * hal transfer (gain * 256)  to kernel
		 * than divide into analog gain & digital gain in kernel
		 */

		gain = ctrl->val;
		if (gain < 256)
			gain = 256;
		if (gain > 43663)
			gain = 43663;
		if (gain >= 256 && gain <= 2728) {
			a_gain = gain;
			d_gain = 1 * 256;
		} else {
			a_gain = 2728;
			d_gain = (gain * 256) / a_gain;
		}

		/*
		 * Analog gain, reg range[0, 232], gain value[1, 10.66]
		 * reg = 256 - 256 / again
		 * a_gain here is 256 multify
		 * so the reg = 256 - 256 * 256 / a_gain
		 */
		priv->analogue_gain = (256 - (256 * 256) / a_gain);
		if (a_gain < 256)
			priv->analogue_gain = 0;
		if (priv->analogue_gain > 232)
			priv->analogue_gain = 232;

		/*
		 * Digital gain, reg range[256, 4095], gain rage[1, 16]
		 * reg = dgain * 256
		 */
		priv->digital_gain = d_gain;
		if (priv->digital_gain < 256)
			priv->digital_gain = 256;
		if (priv->digital_gain > 4095)
			priv->digital_gain = 4095;

		/*
		 * for bank A and bank B switch
		 * exposure time , gain, vts must change at the same time
		 * so the exposure & gain can reflect at the same frame
		 */

		ret = reg_write(client, 0x0157, priv->analogue_gain);
		ret |= reg_write(client, 0x0158, priv->digital_gain >> 8);
		ret |= reg_write(client, 0x0159, priv->digital_gain & 0xff);

		return ret;

	case V4L2_CID_EXPOSURE:
		priv->exposure_time = ctrl->val;

		ret = reg_write(client, 0x015a, priv->exposure_time >> 8);
		ret |= reg_write(client, 0x015b, priv->exposure_time & 0xff);
		return ret;

	case V4L2_CID_TEST_PATTERN:
		return imx219_s_ctrl_test_pattern(ctrl);

	case V4L2_CID_VBLANK:
		if (ctrl->val < priv->cur_mode->vts_def)
			ctrl->val = priv->cur_mode->vts_def;
		if ((ctrl->val - IMX219_EXP_LINES_MARGIN) != priv->cur_vts)
			priv->cur_vts = ctrl->val - IMX219_EXP_LINES_MARGIN;
		ret = reg_write(client, 0x0160, ((priv->cur_vts >> 8) & 0xff));
		ret |= reg_write(client, 0x0161, (priv->cur_vts & 0xff));
		return ret;

	default:
		return -EINVAL;
	}
	/* If enabled, apply settings immediately */
	reg = reg_read(client, 0x0100);
	if ((reg & 0x1f) == 0x01)
		imx219_set_stream(&priv->sd, 1);

	return 0;
}

static int imx219_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= (ARRAY_SIZE(codes) / 4))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int imx219_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = supported_modes[fse->index].height;

	return 0;
}

static int imx219_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx219 *priv = to_imx219(sd);

	if (fie->index >= priv->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static void imx219_reset_colorspace(struct v4l2_mbus_framefmt *fmt)
{
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  fmt->colorspace,
							  fmt->ycbcr_enc);
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
}

static void imx219_update_pad_format(struct imx219 *imx219,
				     const struct imx219_mode *mode,
				     struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	imx219_reset_colorspace(&fmt->format);
}

static int __imx219_get_pad_format(struct imx219 *imx219,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_format *fmt)
{
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(&imx219->sd, cfg, fmt->pad);
		/* update the code which could change due to vflip or hflip: */
		try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format = *try_fmt;
	} else {
		imx219_update_pad_format(imx219, imx219->cur_mode, fmt);
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	}

	return 0;
}

static int imx219_get_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct imx219 *imx219 = to_imx219(sd);
	int ret;

	mutex_lock(&imx219->mutex);
	ret = __imx219_get_pad_format(imx219, cfg, fmt);
	mutex_unlock(&imx219->mutex);

	return ret;
}

static int imx219_get_reso_dist(const struct imx219_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx219_mode *imx219_find_best_fit(
					struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = imx219_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx219_set_pad_format(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct imx219 *priv = to_imx219(sd);
	const struct imx219_mode *mode;
	s64 h_blank, v_blank, pixel_rate;
	u32 fps = 0;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	mode = imx219_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	priv->cur_mode = mode;
	h_blank = mode->hts_def - mode->width;
	__v4l2_ctrl_modify_range(priv->hblank, h_blank,
					h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(priv->vblank, v_blank,
					v_blank,
					1, v_blank);
	fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator,
		mode->max_fps.numerator);
	pixel_rate = mode->vts_def * mode->hts_def * fps;
	__v4l2_ctrl_modify_range(priv->pixel_rate, pixel_rate,
					pixel_rate, 1, pixel_rate);

	/* reset crop window */
	priv->crop_rect.left = 1640 - (mode->width / 2);
	if (priv->crop_rect.left < 0)
		priv->crop_rect.left = 0;
	priv->crop_rect.top = 1232 - (mode->height / 2);
	if (priv->crop_rect.top < 0)
		priv->crop_rect.top = 0;
	priv->crop_rect.width = mode->width;
	priv->crop_rect.height = mode->height;

	return 0;
}

static int imx219_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx219 *priv = to_imx219(sd);
	const struct imx219_mode *mode = priv->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}

/* V4L2 subdev core operations */
static int imx219_s_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct imx219 *priv = to_imx219(sd);

	if (on)	{
		dev_dbg(&client->dev, "imx219 power on\n");
		clk_prepare_enable(priv->xclk);
	} else if (!on) {
		dev_dbg(&client->dev, "imx219 power off\n");
		clk_disable_unprepare(priv->xclk);
	}

	return 0;
}

static void imx219_get_module_inf(struct imx219 *imx219,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, IMX219_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, imx219->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, imx219->len_name, sizeof(inf->base.lens));
}

static long imx219_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx219 *imx219 = to_imx219(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		imx219_get_module_inf(imx219, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx219_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx219_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = imx219_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_ctrl_ops imx219_ctrl_ops = {
	.s_ctrl = imx219_set_ctrl,
};

static const struct v4l2_subdev_core_ops imx219_core_ops = {
	//.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	//.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.s_power = imx219_s_power,
	.ioctl = imx219_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx219_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx219_video_ops = {
	.s_stream = imx219_set_stream,
	.g_frame_interval = imx219_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx219_pad_ops = {
	.enum_mbus_code = imx219_enum_mbus_code,
	.get_fmt = imx219_get_pad_format,
	.set_fmt = imx219_set_pad_format,
	//.get_selection = imx219_get_selection,
	.enum_frame_size = imx219_enum_frame_size,
	.enum_frame_interval = imx219_enum_frame_interval,
};

static const struct v4l2_subdev_ops imx219_subdev_ops = {
	.core = &imx219_core_ops,
	.video = &imx219_video_ops,
	.pad = &imx219_pad_ops,
};

static const struct v4l2_subdev_internal_ops imx219_internal_ops = {
	.open = imx219_open,
};

static int imx219_video_probe(struct i2c_client *client)
{
	u16 model_id;
	u32 lot_id;
	u16 chip_id;
	int ret;

	/* Check and show model, lot, and chip ID. */
	ret = reg_read(client, 0x0000);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Model ID (high byte)\n");
		goto done;
	}
	model_id = ret << 8;

	ret = reg_read(client, 0x0001);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Model ID (low byte)\n");
		goto done;
	}
	model_id |= ret;

	ret = reg_read(client, 0x0004);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Lot ID (high byte)\n");
		goto done;
	}
	lot_id = ret << 16;

	ret = reg_read(client, 0x0005);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Lot ID (mid byte)\n");
		goto done;
	}
	lot_id |= ret << 8;

	ret = reg_read(client, 0x0006);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Lot ID (low byte)\n");
		goto done;
	}
	lot_id |= ret;

	ret = reg_read(client, 0x000D);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Chip ID (high byte)\n");
		goto done;
	}
	chip_id = ret << 8;

	ret = reg_read(client, 0x000E);
	if (ret < 0) {
		dev_err(&client->dev, "Failure to read Chip ID (low byte)\n");
		goto done;
	}
	chip_id |= ret;

	if (model_id != 0x0219) {
		dev_err(&client->dev, "Model ID: %x not supported!\n",
			model_id);
		ret = -ENODEV;
		goto done;
	}
	dev_info(&client->dev,
		 "Model ID 0x%04x, Lot ID 0x%06x, Chip ID 0x%04x\n",
		 model_id, lot_id, chip_id);
done:
	return ret;
}

/* Initialize control handlers */
static int imx219_init_controls(struct imx219 *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	const struct imx219_mode *mode = priv->cur_mode;
	s64 pixel_rate, h_blank, v_blank;
	int ret;
	u32 fps = 0;

	v4l2_ctrl_handler_init(&priv->ctrl_handler, 10);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	/* exposure */
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN,
			  IMX219_ANALOGUE_GAIN_MIN,
			  IMX219_ANALOGUE_GAIN_MAX,
			  1, IMX219_ANALOGUE_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_GAIN,
			  IMX219_DIGITAL_GAIN_MIN,
			  IMX219_DIGITAL_GAIN_MAX, 1,
			  IMX219_DIGITAL_GAIN_DEFAULT);
	v4l2_ctrl_new_std(&priv->ctrl_handler, &imx219_ctrl_ops,
			  V4L2_CID_EXPOSURE,
			  IMX219_DIGITAL_EXPOSURE_MIN,
			  IMX219_DIGITAL_EXPOSURE_MAX, 1,
			  IMX219_DIGITAL_EXPOSURE_DEFAULT);

	/* blank */
	h_blank = mode->hts_def - mode->width;
	priv->hblank = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_HBLANK,
			  h_blank, h_blank, 1, h_blank);
	v_blank = mode->vts_def - mode->height;
	priv->vblank = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_VBLANK,
			  v_blank, v_blank, 1, v_blank);

	/* freq */
	v4l2_ctrl_new_int_menu(&priv->ctrl_handler, NULL, V4L2_CID_LINK_FREQ,
			       0, 0, link_freq_menu_items);
	fps = DIV_ROUND_CLOSEST(mode->max_fps.denominator,
		mode->max_fps.numerator);
	pixel_rate = mode->vts_def * mode->hts_def * fps;
	priv->pixel_rate = v4l2_ctrl_new_std(&priv->ctrl_handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	v4l2_ctrl_new_std_menu_items(&priv->ctrl_handler, &imx219_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(tp_qmenu) - 1, 0, 0, tp_qmenu);

	priv->sd.ctrl_handler = &priv->ctrl_handler;
	if (priv->ctrl_handler.error) {
		dev_err(&client->dev, "Error %d adding controls\n",
			priv->ctrl_handler.error);
		ret = priv->ctrl_handler.error;
		goto error;
	}

	ret = v4l2_ctrl_handler_setup(&priv->ctrl_handler);
	if (ret < 0) {
		dev_err(&client->dev, "Error %d setting default controls\n",
			ret);
		goto error;
	}

	return 0;
error:
	v4l2_ctrl_handler_free(&priv->ctrl_handler);
	return ret;
}

static void imx219_free_controls(struct imx219 *imx219)
{
	v4l2_ctrl_handler_free(imx219->sd.ctrl_handler);
	mutex_destroy(&imx219->mutex);
}

static int imx219_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct imx219 *priv;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "Enter imx219_probe()");

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &priv->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &priv->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &priv->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &priv->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	priv->xclk = devm_clk_get(&client->dev, "xvclk");
	if (IS_ERR(priv->xclk)) {
		dev_info(&client->dev, "Error %ld getting clock\n",
			 PTR_ERR(priv->xclk));
		return -EPROBE_DEFER;
	}

	priv->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->enable_gpio))
		dev_warn(dev, "Failed to get enable_gpios\n");

	/* 1920 * 1080 by default */
	priv->cur_mode = &supported_modes[0];
	priv->cfg_num = ARRAY_SIZE(supported_modes);
	priv->crop_rect.left = 680;
	priv->crop_rect.top = 692;
	priv->crop_rect.width = priv->cur_mode->width;
	priv->crop_rect.height = priv->cur_mode->height;

	v4l2_i2c_subdev_init(&priv->sd, client, &imx219_subdev_ops);

	ret = imx219_init_controls(priv);
	if (ret)
		return ret;

	ret = imx219_video_probe(client);
	if (ret < 0)
		return ret;

	/* Initialize subdev */
	priv->sd.internal_ops = &imx219_internal_ops;
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
	priv->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	/* Initialize source pad */
	priv->pad.flags = MEDIA_PAD_FL_SOURCE;

	/* Initialize default format */
	imx219_set_default_format(priv);

	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pad);
	if (ret) {
		dev_err(dev, "failed to init entity pads: %d\n", ret);
		goto error_handler_free;
	}

	sd = &priv->sd;

	memset(facing, 0, sizeof(facing));
	if (strcmp(priv->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 priv->module_index, facing,
		 IMX219_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret < 0) {
		dev_err(dev, "failed to register sensor sub-device: %d\n", ret);
		goto error_media_entity;
	}

	dev_info(&client->dev, "%s probe done.\n", sd->name);

	return 0;

error_media_entity:
	media_entity_cleanup(&priv->sd.entity);

error_handler_free:
	imx219_free_controls(priv);

	return ret;
}

static int imx219_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx219 *imx219 = to_imx219(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	imx219_free_controls(imx219);

	return 0;
}

static const struct of_device_id imx219_dt_ids[] = {
	{ .compatible = "sony,imx219" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx219_dt_ids);

static struct i2c_driver imx219_i2c_driver = {
	.driver = {
		.name = IMX219_NAME,
		.of_match_table	= imx219_dt_ids,
	},
	.probe_new = imx219_probe,
	.remove = imx219_remove,
};

module_i2c_driver(imx219_i2c_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com");
MODULE_DESCRIPTION("Sony IMX219 sensor driver");
MODULE_LICENSE("GPL v2");
