// SPDX-License-Identifier: GPL-2.0-only
/* 
 * Samsung amb670yf01 AMOLED panel driver.
 *
 * Copyright (c) 2023 Nia Espera <a5b6@riseup.net>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/backlight.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>

#define L2_ACCESS_ENABLE	0xf0, 0x5a, 0x5a
#define L2_ACCESS_DISABLE	0xf0, 0xa5, 0xa5
#define L3_ACCESS_ENABLE	0xf1, 0x5a, 0x5a
#define L3_ACCESS_DISABLE	0xf1, 0xa5, 0xa5
#define ADFR_ACCESS_ENABLE	0xfc, 0x5a, 0x5a
#define ADFR_ACCESS_DISABLE	0xfc, 0xa5, 0xa5
#define UPDATE_REFRESH_RATE	0xf7, 0x0f

/* Values taken from downstream oplus sources. */
#define DSC_CONFIG_POPULATE_OPLUS(dsc_cfg)	\
	int i;					\
	dsc_cfg->line_buf_depth = 11;		\
	dsc_cfg->bits_per_component = 10;	\
	dsc_cfg->convert_rgb = true;		\
	dsc_cfg->slice_width = 720;		\
	dsc_cfg->slice_height = 24;		\
	dsc_cfg->slice_count = 2;		\
	dsc_cfg->simple_422 = false;		\
	dsc_cfg->pic_width = 1440;		\
	dsc_cfg->pic_height = 3216;		\
	dsc_cfg->rc_tgt_offset_high = 3;	\
	dsc_cfg->rc_tgt_offset_low = 3;		\
	dsc_cfg->bits_per_pixel = 8 << 4;	\
	dsc_cfg->rc_edge_factor = 6;		\
	dsc_cfg->rc_quant_incr_limit1 = 15;	\
	dsc_cfg->rc_quant_incr_limit0 = 15;	\
	dsc_cfg->initial_xmit_delay = 512;	\
	dsc_cfg->initial_dec_delay = 646;	\
	dsc_cfg->block_pred_enable = true;	\
	dsc_cfg->first_line_bpg_offset = 13;	\
	dsc_cfg->initial_offset = 6144;		\
	for (i = 0; i < DSC_NUM_BUF_RANGES - 1; i++) \
		dsc_cfg->rc_buf_thresh[i] = rc_buf_thresh[i];	\
	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {		\
		dsc_cfg->rc_range_params[i].range_min_qp = rc_min_qp[i];	\
		dsc_cfg->rc_range_params[i].range_max_qp = rc_max_qp[i];	\
		dsc_cfg->rc_range_params[i].range_bpg_offset = rc_ofs_qp[i];	\
	} \
	dsc_cfg->rc_model_size = 8192;			\
	dsc_cfg->flatness_min_qp = 7;			\
	dsc_cfg->flatness_max_qp = 16;			\
	dsc_cfg->initial_scale_value = 32;		\
	dsc_cfg->scale_increment_interval = 643;	\
	dsc_cfg->scale_decrement_interval = 10;		\
	dsc_cfg->nfl_bpg_offset = 1158;			\
	dsc_cfg->slice_bpg_offset = 814;		\
	dsc_cfg->final_offset = 4336;			\
	dsc_cfg->vbr_enable = false;			\
	dsc_cfg->mux_word_size = 48;			\
	dsc_cfg->slice_chunk_size = 720;		\
	dsc_cfg->rc_bits = 2557;			\
	dsc_cfg->dsc_version_minor = 1;			\
	dsc_cfg->dsc_version_major = 1;			\
	dsc_cfg->native_422 = false; 			\
	dsc_cfg->native_420 = false;			\
	/* These are only needed for native 4:2:0 */	\
	dsc_cfg->second_line_bpg_offset = 0;		\
	dsc_cfg->nsl_bpg_offset = 0;			\
	dsc_cfg->second_line_offset_adj = 0;

static char rc_buf_thresh[DSC_NUM_BUF_RANGES - 1] = {
	0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62,
	0x69, 0x70, 0x77, 0x79, 0x7b, 0x7d, 0x7e
};

/* XXX: Double-check this */
static char rc_min_qp[DSC_NUM_BUF_RANGES] = {
	0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16
};

static char rc_max_qp[DSC_NUM_BUF_RANGES] = {
	8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17
};

static char rc_ofs_qp[DSC_NUM_BUF_RANGES] = {
	2, 0, 0, 62, 60, 58, 56, 56, 56, 54, 54, 52, 52, 52, 52
};

struct amb670yf01_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config *dsc;
	struct regulator *supply_vdd;
	struct regulator *supply_vddio;
	struct regulator *supply_avdd;
	struct gpio_desc *reset_gpio;
	const struct drm_display_mode *mode;
	bool prepared;
};

static const struct drm_display_mode amb670yf01_120hz_1440_mode = {
	.clock = (1440 + 88 + 8 + 61) * (3216 + 8 * 3) * 120 / 1000,
		 //171500000 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 88,
	.hsync_end = 1440 + 88 + 8,
	.htotal = 1440 + 88 + 8 + 61,
	.vdisplay = 3216,
	.vsync_start = 3216 + 8,
	.vsync_end = 3216 + 8 * 2,
	.vtotal = 3216 + 8 * 3,
	.width_mm = 73,
	.height_mm = 160,
};

static inline
struct amb670yf01_panel *to_amb670yf01_panel(struct drm_panel *panel)
{
	return container_of(panel, struct amb670yf01_panel, panel);
}

static void amb670yf01_panel_reset(struct amb670yf01_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int amb670yf01_panel_on(struct amb670yf01_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	struct drm_dsc_picture_parameter_set *pps;
	int ret;

	pps = devm_kzalloc(dev, sizeof(*pps), GFP_KERNEL);
	if (!pps)
		return -ENOMEM;

	/* XXX: Make this more vendor-agnostic */

	//dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	drm_dsc_pps_payload_pack(pps, ctx->dsc);
	
	ret = mipi_dsi_picture_parameter_set(dsi, pps);
	kfree(pps);
	if (ret < 0) {
 		dev_err(dev, "Failed to transmit PPS: %d\n", ret);
	 	return ret;
	}
	/* DSC enablement, with PPS */
	mipi_dsi_dcs_write_seq(dsi, 0x9d, 0x01);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	usleep_range(6000, 7000);

	/* TSP HSYNC settings */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x22, 0xb9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0xa1, 0xb1);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x3a, 0xb9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x26, 0xb9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, UPDATE_REFRESH_RATE);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* TSP SYNC3 settings */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x24, 0xb9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x38, 0xb9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x2a, 0xb9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, UPDATE_REFRESH_RATE);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);
	msleep(121);

	/* FQ CON */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x00, 0x01);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* VSYNC on */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* Column/page address setting */
	ret = mipi_dsi_dcs_set_column_address(dsi, 0x0000, 0x059f);
	if (ret < 0) {
		dev_err(dev, "Failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0x0000, 0x0c8f);
	if (ret < 0) {
		dev_err(dev, "Failed to set page address: %d\n", ret);
		return ret;
	}

	/* Scaler setting (none, no scaling for full res) */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* "Pre-charge setting", whatever that is */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x2b, 0xf6);
	mipi_dsi_dcs_write_seq(dsi, 0xf6, 0x60, 0x63, 0x69);
	mipi_dsi_dcs_write_seq(dsi, UPDATE_REFRESH_RATE);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* HLPM mode for power saving */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x46, 0xf4);
	mipi_dsi_dcs_write_seq(dsi, 0xf4, 0x08);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* "DCDC setting for AOD"? */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x18, 0xb1);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x05);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* Brightness & dimming */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x63, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x0c, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x63, 0x00);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/*
	 * We want to stay in dim mode, i.e. no HBM. For global HBM on,
	 * |= 0xc0; for local HBM, |= 0x10; for no dimming, &=~0x08.
	 */
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x28);

	/* Enable ADFR (proprietary refresh rate switching) */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, ADFR_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xfd, 0x4a);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x18, 0xf2);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x1b, 0x50);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x74, 0xbd);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x78);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x01, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x10, 0xf6);
	mipi_dsi_dcs_write_seq(dsi, 0xf6, 0xaa);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x3f, 0xf6);
	mipi_dsi_dcs_write_seq(dsi, 0xf6, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x45, 0xf6);
	mipi_dsi_dcs_write_seq(dsi, 0xf6, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x6c, 0xbd);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x6f, 0xbd);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x45, 0x65, 0x65, 0x65);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x06, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x0a, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x0e, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0xff, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x13, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0xff, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x18, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x04, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x02, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0xbd, 0x21, 0x82);
	mipi_dsi_dcs_write_seq(dsi, UPDATE_REFRESH_RATE);
	mipi_dsi_dcs_write_seq(dsi, ADFR_ACCESS_DISABLE);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* ACL on */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	/* Early exit & manual/automatic mode-related settings */
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_ENABLE);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x52, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x54, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x00);
	mipi_dsi_dcs_write_seq(dsi, L2_ACCESS_DISABLE);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int amb670yf01_panel_off(struct amb670yf01_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(21);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(101);

	return 0;
}

static int amb670yf01_panel_prepare(struct drm_panel *panel)
{
	struct amb670yf01_panel *ctx = to_amb670yf01_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ctx->dsc->pic_width = ctx->mode->hdisplay;
	ctx->dsc->pic_height = ctx->mode->vdisplay;
	drm_dsc_compute_rc_parameters(ctx->dsc);

	ret = regulator_enable(ctx->supply_vdd);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(ctx->supply_vddio);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}
	
	ret = regulator_enable(ctx->supply_avdd);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}

	amb670yf01_panel_reset(ctx);

	ret = amb670yf01_panel_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}
	
	msleep(28);

	ctx->prepared = true;
	return 0;
}

static int amb670yf01_panel_unprepare(struct drm_panel *panel)
{
	struct amb670yf01_panel *ctx = to_amb670yf01_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = amb670yf01_panel_off(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	regulator_disable(ctx->supply_vdd);
	regulator_disable(ctx->supply_vddio);
	regulator_disable(ctx->supply_avdd);

	ctx->prepared = false;
	return 0;
}

static int amb670yf01_panel_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct amb670yf01_panel *ctx = to_amb670yf01_panel(panel);

	mode = drm_mode_duplicate(connector->dev, ctx->mode);
	if (!mode)
		return -ENOMEM;
	
	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs amb670yf01_panel_funcs = {
	.prepare = amb670yf01_panel_prepare,
	.unprepare = amb670yf01_panel_unprepare,
	.get_modes = amb670yf01_panel_get_modes,
};

static int amb670yf01_panel_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;
	
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int amb670yf01_panel_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops amb670yf01_panel_bl_ops = {
	.update_status = amb670yf01_panel_bl_update_status,
	.get_brightness = amb670yf01_panel_bl_get_brightness,
};

static struct backlight_device *
amb670yf01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2047,
		/* Panel can go higher in high brightness mode */
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &amb670yf01_panel_bl_ops, &props);
}

static int amb670yf01_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct amb670yf01_panel *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->mode = of_device_get_match_data(dev);

	if (!ctx->mode) {
		dev_err(dev, "Missing device mode\n");
		return -ENODEV;
	}

	ctx->supply_vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->supply_vdd))
		return dev_err_probe(dev, PTR_ERR(ctx->supply_vdd),
				     "Failed to get vdd regulator\n");
	
	ctx->supply_vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->supply_vddio))
		return dev_err_probe(dev, PTR_ERR(ctx->supply_vddio),
				     "Failed to get vddio regulator\n");
	
	ctx->supply_avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ctx->supply_avdd))
		return dev_err_probe(dev, PTR_ERR(ctx->supply_avdd),
				     "Failed to get avdd regulator\n");
	
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM |
			  MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &amb670yf01_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = amb670yf01_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ctx->dsc = devm_kzalloc(dev, sizeof(*ctx->dsc), GFP_KERNEL);
	if (!ctx->dsc)
		return -ENOMEM;

	dsi->slice_per_pkt = 2;
	/*
	ctx->dsc->dsc_version_major = 1;
	ctx->dsc->dsc_version_minor = 2;
	ctx->dsc->slice_height = 24;
	ctx->dsc->slice_width = 720;
	ctx->dsc->slice_count = 2;
	ctx->dsc->bits_per_component = 10;
	ctx->dsc->bits_per_pixel = 8 << 4;
	ctx->dsc->block_pred_enable = true;
	//drm_dsc_compute_rc_parameters(ctx->dsc);
	*/
	DSC_CONFIG_POPULATE_OPLUS(ctx->dsc);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = ctx->dsc;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void amb670yf01_panel_remove(struct mipi_dsi_device *dsi)
{
	struct amb670yf01_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id amb670yf01_panel_of_match[] = {
	/* XXX: Support multiple frequencies? */
	{ /* OnePlus 9 Pro */
		.compatible = "samsung,amb670yf01",
		.data = &amb670yf01_120hz_1440_mode,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, amb670yf01_panel_of_match);

static struct mipi_dsi_driver amb670yf01_panel_driver = {
	.probe = amb670yf01_panel_probe,
	.remove = amb670yf01_panel_remove,
	.driver = {
		.name = "panel-amb670yf01",
		.of_match_table = amb670yf01_panel_of_match,
	},
};

module_mipi_dsi_driver(amb670yf01_panel_driver);

MODULE_AUTHOR("Nia Espera <a5b6@riseup.net>");
MODULE_DESCRIPTION("DRM driver for Samsung AMOLED DSI panels found in OnePlus 9 Pro phones");
MODULE_LICENSE("GPL v2");

