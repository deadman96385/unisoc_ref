/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drm_atomic_helper.h>
#include <linux/component.h>
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <video/mipi_display.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>
#include "sprd_dpu.h"
#include "sprd_panel.h"
#include "dsi/sprd_dsi_api.h"
#include "sysfs/sysfs_display.h"
#ifdef CONFIG_LATTICE_FPGA
#include "sprd_spi_md6000_initial_impl_double_mipi.h"
#include "sprd_spi_md6000.h"
#endif
#ifdef CONFIG_GOWIN_FPGA
struct regulator *fpga3v3;
#endif
struct regulator *lcd3v3;
struct sprd_oled *oled_xr;
#define SPRD_MIPI_DSI_FMT_DSC 0xff
extern int sprd_close_brightness(void);
static DEFINE_MUTEX(panel_lock);

static DEFINE_MUTEX(notifier_lock); 
static RAW_NOTIFIER_HEAD(panel_notifier);

int panel_notifier_register(struct notifier_block *nb)
{
	int err;

	mutex_lock(&notifier_lock);
	err = raw_notifier_chain_register(&panel_notifier, nb);
	mutex_unlock(&notifier_lock);

	return err;
}

int panel_notifier_unregister(struct notifier_block *nb)
{
	int err;

	mutex_lock(&notifier_lock);
	err = raw_notifier_chain_unregister(&panel_notifier, nb);
	mutex_unlock(&notifier_lock);

	return err;
}

static void panel_notifier_event(unsigned long event, void *data)
{
	mutex_lock(&notifier_lock);
	raw_notifier_call_chain(&panel_notifier, event, data);
	mutex_unlock(&notifier_lock);
}

const char *lcd_name;
static int __init lcd_name_get(char *str)
{
	if (str != NULL)
		lcd_name = str;
	DRM_INFO("lcd name from uboot: %s\n", lcd_name);
	return 0;
}
__setup("lcd_name=", lcd_name_get);

static inline struct sprd_panel *to_sprd_panel(struct drm_panel *panel)
{
	return container_of(panel, struct sprd_panel, base);
}


struct sprd_panel *fpga_panel;
static struct kobject *fpga_ctrl_kobj = NULL;
static int fpga_disable_ldo(void)
{
#ifdef CONFIG_LATTICE_FPGA
   // sprd_close_brightness();
   // mdelay(20);
   // gpiod_direction_output(fpga_panel->info.reset_gpio, 0);
   // mdelay(2);
   // regulator_disable(lcd3v3);
   // mdelay(1);
   // gpiod_direction_output(fpga_panel->info.lcd1v8_gpio, 0);
#endif

#ifdef  CONFIG_GOWIN_FPGA
	sprd_close_gowin_brightness();
    gpiod_direction_output(fpga_panel->info.fpgarst_gpio,0);
    regulator_disable(lcd3v3);
    gpiod_direction_output(fpga_panel->info.lcd1v8_gpio, 0);
    regulator_disable(fpga3v3);
    gpiod_direction_output(fpga_panel->info.fpga1v8_gpio, 0);
    gpiod_direction_output(fpga_panel->info.fpga1v2_gpio, 0);
#endif
    return 0;
}
static ssize_t fpga_disable_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t count)   
{
    unsigned int i;

    if (kstrtouint(buf, 10, &i))
        return -EINVAL;

    if (i == 1)
        fpga_disable_ldo();
    else
        return -EINVAL;

    return count;

}

static DEVICE_ATTR(fpga_disable, 0664, NULL, fpga_disable_store);

static struct attribute *fpga_sysfs_attrs[] = {
    &dev_attr_fpga_disable.attr,
    NULL,
};

static struct attribute_group fpga_attr_group = {
    .attrs = fpga_sysfs_attrs,
};

static int fpga_sysfs_init(void)
{
    fpga_ctrl_kobj = kobject_create_and_add("fpga", NULL);
    if (!fpga_ctrl_kobj){
        printk("Create fpga_sysfs_init failed!\n");
        return -ENOMEM;
    }
    return sysfs_create_group(fpga_ctrl_kobj, &fpga_attr_group);
}
static int sprd_panel_send_cmds(struct mipi_dsi_device *dsi,
				const void *data, int size)
{
	struct sprd_panel *panel;
	const struct dsi_cmd_desc *cmds = data;
	u16 len;

	if ((cmds == NULL) || (dsi == NULL))
		return -EINVAL;

	panel = mipi_dsi_get_drvdata(dsi);

	while (size > 0) {
		len = (cmds->wc_h << 8) | cmds->wc_l;

		if (panel->info.use_dcs)
			mipi_dsi_dcs_write_buffer(dsi, cmds->payload, len);
		else
			mipi_dsi_generic_write(dsi, cmds->payload, len);

		if (cmds->wait)
			msleep(cmds->wait);
		cmds = (const struct dsi_cmd_desc *)(cmds->payload + len);
		size -= (len + 4);
	}

	return 0;
}

static int sprd_panel_unprepare(struct drm_panel *p)
{
    struct sprd_panel *panel = to_sprd_panel(p);
    struct gpio_timing *timing;
    int items, i;

    DRM_INFO("%s()\n", __func__);

    panel_notifier_event(1,0);
#ifdef CONFIG_GOWIN_FPGA
    gpiod_direction_output(panel->info.reset_gpio, 0);
    mdelay(2);
    regulator_disable(lcd3v3);
    mdelay(1);
    gpiod_direction_output(panel->info.lcd1v8_gpio, 0);
    mdelay(5);
    regulator_disable(fpga3v3);
    mdelay(5);
    regulator_disable(lcd3v3);
    mdelay(5);
#endif
    if (panel->info.avee_gpio) {
		gpiod_direction_output(panel->info.avee_gpio, 0);
		mdelay(5);
	}

	if (panel->info.avdd_gpio) {
		gpiod_direction_output(panel->info.avdd_gpio, 0);
		mdelay(5);
	}

        if (panel->info.fpga1v2_gpio) {
                gpiod_direction_output(panel->info.fpga1v2_gpio, 0);
                mdelay(5);
        }
	if (panel->info.fpga1v8_gpio) {
		gpiod_direction_output(panel->info.fpga1v8_gpio, 0);
		mdelay(5);
	}

	if (panel->info.lcd1v8_gpio) {
		gpiod_direction_output(panel->info.lcd1v8_gpio, 0);
		mdelay(5);
	}
#ifdef CONFIG_LATTICE_FPGA
 //   regulator_disable(lcd3v3);
 //   mdelay(5);
#endif
#if 0	
	if(panel->info.lcdtp3v3_gpio) {
    		gpiod_direction_output(panel->info.lcdtp3v3_gpio, 0);
		mdelay(5);
    	}
#endif
	if (panel->info.reset_gpio) {
		items = panel->info.rst_off_seq.items;
		timing = panel->info.rst_off_seq.timing;
		for (i = 0; i < items; i++) {
			gpiod_direction_output(panel->info.reset_gpio,
						timing[i].level);
			mdelay(timing[i].delay);
		}
	}
#ifdef CONFIG_LATTICE_FPGA
	spi_md6000_suspend();
	mdelay(5);
#endif
#ifdef CONFIG_GOWIN_FPGA
    if (panel->info.fpgarst_gpio) {
        gpiod_direction_output(panel->info.fpgarst_gpio,0);
        mdelay(5);
    }
#endif
	regulator_disable(panel->supply);
	return 0;
}

static int sprd_panel_prepare(struct drm_panel *p)
{
	struct sprd_panel *panel = to_sprd_panel(p);
	struct gpio_timing *timing;
	int items, i, ret;

	DRM_INFO("%s()\n", __func__);

    panel_notifier_event(2,0);
    ret = regulator_enable(panel->supply);
    if (ret < 0)
        DRM_ERROR("enable lcd regulator failed\n");

	if (panel->info.avdd_gpio) {
		gpiod_direction_output(panel->info.avdd_gpio, 1);
		mdelay(5);
	}

	if (panel->info.avee_gpio) {
		gpiod_direction_output(panel->info.avee_gpio, 1);
		mdelay(5);
	}
#ifdef CONFIG_LATTICE_FPGA
	spi_md6000_resume();
	mdelay(5);
#endif
    if (panel->info.fpga1v2_gpio) {
        gpiod_direction_output(panel->info.fpga1v2_gpio, 1);
        mdelay(5);
    }
#ifdef CONFIG_GOWIN_FPGA
    regulator_enable(fpga3v3);
    mdelay(5);
#endif
    if (panel->info.fpga1v8_gpio) {
        gpiod_direction_output(panel->info.fpga1v8_gpio, 1);
        mdelay(5);
    }


    if (panel->info.lcd1v8_gpio) {
        gpiod_direction_output(panel->info.lcd1v8_gpio, 1);
        mdelay(5);
    }
    regulator_enable(lcd3v3);
   
#if 0
	if(panel->info.lcdtp3v3_gpio) {
    		gpiod_direction_output(panel->info.lcdtp3v3_gpio, 1);
		mdelay(5);
    	}
#endif
	if (panel->info.reset_gpio) {
		items = panel->info.rst_on_seq.items;
		timing = panel->info.rst_on_seq.timing;
		for (i = 0; i < items; i++) {
			gpiod_direction_output(panel->info.reset_gpio,
						timing[i].level);
			mdelay(timing[i].delay);
		}
	}
#ifdef CONFIG_GOWIN_FPGA
    if (panel->info.fpgarst_gpio) {
        gpiod_direction_output(panel->info.fpgarst_gpio,1);
        mdelay(5);
    }
#endif
#ifdef CONFIG_LATTICE_FPGA
    printk("zhangming start\n");
	sprd_spi_md6000_load_bitfile(bitfile_double_mipi, sizeof(bitfile_double_mipi));
    printk("zhangming end\n");
#endif
	return 0;
}

static int sprd_panel_disable(struct drm_panel *p)
{
	struct sprd_panel *panel = to_sprd_panel(p);

	DRM_INFO("%s()\n", __func__);

	mutex_lock(&panel_lock);
			mdelay(50);
			sprd_panel_send_cmds(panel->slave,
			panel->info.cmds[CMD_CODE_SLEEP_IN],
			panel->info.cmds_len[CMD_CODE_SLEEP_IN]);
			mdelay(50);
			sprd_panel_send_cmds(panel->slave,
			panel->info.cmds[CMD_CODE_SLEEP_IN2],
			panel->info.cmds_len[CMD_CODE_SLEEP_IN2]);
	/*
	 * FIXME:
	 * The cancel work should be executed before DPU stop,
	 * otherwise the esd check will be failed if the DPU
	 * stopped in video mode and the DSI has not change to
	 * CMD mode yet. Since there is no VBLANK timing for
	 * LP cmd transmission.
	 */
	if (panel->esd_work_pending) {
		cancel_delayed_work_sync(&panel->esd_work);
		panel->esd_work_pending = false;
	}

	if (panel->backlight) {
		panel->backlight->props.power = FB_BLANK_POWERDOWN;
		panel->backlight->props.state |= BL_CORE_FBBLANK;
		backlight_update_status(panel->backlight);
	}

	sprd_panel_send_cmds(panel->slave,
			     panel->info.cmds[CMD_CODE_SLEEP_IN],
			     panel->info.cmds_len[CMD_CODE_SLEEP_IN]);
	panel->is_enabled = false;
	mutex_unlock(&panel_lock);

	return 0;
}

//int sprd_resume_backlight_set(struct drm_panel *p)
//{
//    struct sprd_panel *panel = to_sprd_panel(p);
//    mutex_lock(&panel_lock);
//    if (panel->backlight) {
//        panel->backlight->props.power = FB_BLANK_UNBLANK;
//        panel->backlight->props.state &= ~BL_CORE_FBBLANK;
//        backlight_update_status(panel->backlight);
//    }
//    mutex_unlock(&panel_lock);
//    return 0;
//
//}
static int flag = 0;
static int sprd_panel_enable(struct drm_panel *p)
{
	struct sprd_panel *panel = to_sprd_panel(p);
    int i;
	DRM_INFO("%s()\n", __func__);
    

    mutex_lock(&panel_lock);
    /*
       sprd_panel_send_cmds(panel->slave,
       panel->info.cmds[CMD_CODE_INIT],
       panel->info.cmds_len[CMD_CODE_INIT]);
    */
    for(i = 0; i < 5; i++)
    {
        if (panel->backlight) {
            panel->backlight->props.power = FB_BLANK_UNBLANK;
            panel->backlight->props.state &= ~BL_CORE_FBBLANK;
            backlight_update_status(panel->backlight);
        }
    }

	if (panel->info.esd_check_en) {
		schedule_delayed_work(&panel->esd_work,
				      msecs_to_jiffies(1000));
		panel->esd_work_pending = true;
	}
    flag = 1;
	panel->is_enabled = true;
	mutex_unlock(&panel_lock);

	return 0;
}

static int sprd_panel_get_modes(struct drm_panel *p)
{
	struct drm_display_mode *mode;
	struct sprd_panel *panel = to_sprd_panel(p);
	struct device_node *np = panel->slave->dev.of_node;
	u32 surface_width = 0, surface_height = 0;
	int i, mode_count = 0;

	DRM_INFO("%s()\n", __func__);
	mode = drm_mode_duplicate(p->drm, &panel->info.mode);
	if (!mode) {
		DRM_ERROR("failed to alloc mode %s\n", panel->info.mode.name);
		return 0;
	}
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(p->connector, mode);
	mode_count++;

	for (i = 1; i < panel->info.num_buildin_modes; i++)	{
		mode = drm_mode_duplicate(p->drm,
			&(panel->info.buildin_modes[i]));
		if (!mode) {
			DRM_ERROR("failed to alloc mode %s\n",
				panel->info.buildin_modes[i].name);
			return 0;
		}
		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_DEFAULT;
		drm_mode_probed_add(p->connector, mode);
		mode_count++;
	}

	of_property_read_u32(np, "sprd,surface-width", &surface_width);
	of_property_read_u32(np, "sprd,surface-height", &surface_height);
	if (surface_width && surface_height) {
                struct videomode vm = {};

		vm.hactive = surface_width;
		vm.vactive = surface_height;
		vm.pixelclock = surface_width * surface_height * 60;

		mode = drm_mode_create(p->drm);

		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_BUILTIN |
			DRM_MODE_TYPE_CRTC_C;
		mode->vrefresh = 60;
		drm_display_mode_from_videomode(&vm, mode);
		drm_mode_probed_add(p->connector, mode);
		mode_count++;
	}

	p->connector->display_info.width_mm = panel->info.mode.width_mm;
	p->connector->display_info.height_mm = panel->info.mode.height_mm;

	return mode_count;
}

static const struct drm_panel_funcs sprd_panel_funcs = {
	.get_modes = sprd_panel_get_modes,
	.enable = sprd_panel_enable,
	.disable = sprd_panel_disable,
	.prepare = sprd_panel_prepare,
	.unprepare = sprd_panel_unprepare,
};

static int sprd_panel_esd_check(struct sprd_panel *panel)
{
	struct panel_info *info = &panel->info;
	u8 read_val = 0;

	/* FIXME: we should enable HS cmd tx here */
	mipi_dsi_set_maximum_return_packet_size(panel->slave, 1);
	mipi_dsi_dcs_read(panel->slave, info->esd_check_reg,
			  &read_val, 1);

	/*
	 * TODO:
	 * Should we support multi-registers check in the future?
	 */
	if (read_val != info->esd_check_val) {
		DRM_ERROR("esd check failed, read value = 0x%02x\n",
			  read_val);
		return -EINVAL;
	}

	return 0;
}

static int sprd_panel_te_check(struct sprd_panel *panel)
{
	static int te_wq_inited;
	struct sprd_dpu *dpu;
	int ret;
	bool irq_occur = false;

	if (!panel->base.connector ||
	    !panel->base.connector->encoder ||
	    !panel->base.connector->encoder->crtc) {
		return 0;
	}

	dpu = container_of(panel->base.connector->encoder->crtc,
		struct sprd_dpu, crtc);

	if (!te_wq_inited) {
		init_waitqueue_head(&dpu->ctx.te_wq);
		te_wq_inited = 1;
		dpu->ctx.evt_te = false;
		DRM_INFO("%s init te waitqueue\n", __func__);
	}

	/* DPU TE irq maybe enabled in kernel */
	if (!dpu->ctx.is_inited)
		return 0;

	dpu->ctx.te_check_en = true;

	/* wait for TE interrupt */
	ret = wait_event_interruptible_timeout(dpu->ctx.te_wq,
		dpu->ctx.evt_te, msecs_to_jiffies(500));
	if (!ret) {
		/* double check TE interrupt through dpu_int_raw register */
		if (dpu->core && dpu->core->check_raw_int) {
			down(&dpu->ctx.refresh_lock);
			if (dpu->ctx.is_inited)
				irq_occur = dpu->core->check_raw_int(&dpu->ctx,
					DISPC_INT_TE_MASK);
			up(&dpu->ctx.refresh_lock);
			if (!irq_occur) {
				DRM_ERROR("TE esd timeout.\n");
				ret = -1;
			} else
				DRM_WARN("TE occur, but isr schedule delay\n");
		} else {
			DRM_ERROR("TE esd timeout.\n");
			ret = -1;
		}
	}

	dpu->ctx.te_check_en = false;
	dpu->ctx.evt_te = false;

	return ret < 0 ? ret : 0;
}

static void sprd_panel_esd_work_func(struct work_struct *work)
{
	struct sprd_panel *panel = container_of(work, struct sprd_panel,
						esd_work.work);
	struct panel_info *info = &panel->info;
	int ret;

	if (info->esd_check_mode == ESD_MODE_REG_CHECK)
		ret = sprd_panel_esd_check(panel);
	else if (info->esd_check_mode == ESD_MODE_TE_CHECK)
		ret = sprd_panel_te_check(panel);
	else {
		DRM_ERROR("unknown esd check mode:%d\n", info->esd_check_mode);
		return;
	}

	if (ret && panel->base.connector && panel->base.connector->encoder) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		encoder = panel->base.connector->encoder;
		funcs = encoder->helper_private;
		panel->esd_work_pending = false;

		if (!encoder->crtc || (encoder->crtc->state &&
		    !encoder->crtc->state->active)) {
			DRM_INFO("skip esd recovery during panel suspend\n");
			return;
		}

		DRM_INFO("====== esd recovery start ========\n");
		funcs->disable(encoder);
		funcs->enable(encoder);
		DRM_INFO("======= esd recovery end =========\n");
	} else
		schedule_delayed_work(&panel->esd_work,
			msecs_to_jiffies(info->esd_check_period));
}

static int sprd_panel_gpio_request(struct device *dev,
			struct sprd_panel *panel)
{
	panel->info.avdd_gpio = devm_gpiod_get_optional(dev,
					"avdd", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.avdd_gpio))
		DRM_WARN("can't get panel avdd gpio: %ld\n",
				 PTR_ERR(panel->info.avdd_gpio));

	panel->info.avee_gpio = devm_gpiod_get_optional(dev,
					"avee", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.avee_gpio))
		DRM_WARN("can't get panel avee gpio: %ld\n",
				 PTR_ERR(panel->info.avee_gpio));

	panel->info.reset_gpio = devm_gpiod_get_optional(dev,
					"reset", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.reset_gpio))
		DRM_WARN("can't get panel reset gpio: %ld\n",
				 PTR_ERR(panel->info.reset_gpio));

	panel->info.lcd1v8_gpio = devm_gpiod_get_optional(dev,
					"lcd1v8", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.lcd1v8_gpio))
		DRM_WARN("can't get panel lcd1v8 gpio: %ld\n",
				 PTR_ERR(panel->info.lcd1v8_gpio));
	panel->info.fpga1v8_gpio= devm_gpiod_get_optional(dev,
					"fpga1v8", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.fpga1v8_gpio))
		DRM_WARN("can't get panel fpga1v8 gpio: %ld\n",
				 PTR_ERR(panel->info.fpga1v8_gpio));
	panel->info.fpga1v2_gpio= devm_gpiod_get_optional(dev,
					"fpga1v2", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.fpga1v2_gpio))
		DRM_WARN("can't get panel fpga1v2 gpio: %ld\n",
				 PTR_ERR(panel->info.fpga1v2_gpio));
#ifdef CONFIG_GOWIN_FPGA
	panel->info.fpgarst_gpio= devm_gpiod_get_optional(dev,
					"fpgarst", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.fpgarst_gpio))
		DRM_WARN("can't get panel fpgarst_gpiogpio: %ld\n",
				 PTR_ERR(panel->info.fpgarst_gpio));
#endif

#if 0
	panel->info.lcdtp3v3_gpio = devm_gpiod_get_optional(dev,
					"lcdtp3v3", GPIOD_ASIS);
	if (IS_ERR_OR_NULL(panel->info.lcdtp3v3_gpio))
		DRM_WARN("can't get panel lcd1v8 gpio: %ld\n",
				 PTR_ERR(panel->info.lcdtp3v3_gpio));
#endif
	return 0;
}

static int of_parse_reset_seq(struct device_node *np,
				struct panel_info *info)
{
	struct property *prop;
	int bytes, rc;
	u32 *p;

	prop = of_find_property(np, "sprd,reset-on-sequence", &bytes);
	if (!prop) {
		DRM_ERROR("sprd,reset-on-sequence property not found\n");
		return -EINVAL;
	}

	p = kzalloc(bytes, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	rc = of_property_read_u32_array(np, "sprd,reset-on-sequence",
					p, bytes / 4);
	if (rc) {
		DRM_ERROR("parse sprd,reset-on-sequence failed\n");
		kfree(p);
		return rc;
	}

	info->rst_on_seq.items = bytes / 8;
	info->rst_on_seq.timing = (struct gpio_timing *)p;

	prop = of_find_property(np, "sprd,reset-off-sequence", &bytes);
	if (!prop) {
		DRM_ERROR("sprd,reset-off-sequence property not found\n");
		return -EINVAL;
	}

	p = kzalloc(bytes, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	rc = of_property_read_u32_array(np, "sprd,reset-off-sequence",
					p, bytes / 4);
	if (rc) {
		DRM_ERROR("parse sprd,reset-off-sequence failed\n");
		kfree(p);
		return rc;
	}

	info->rst_off_seq.items = bytes / 8;
	info->rst_off_seq.timing = (struct gpio_timing *)p;

	return 0;
}

static int of_parse_buildin_modes(struct panel_info *info,
	struct device_node *lcd_node)
{
	int i, rc, num_timings;
	struct device_node *timings_np;


	timings_np = of_get_child_by_name(lcd_node, "display-timings");
	if (!timings_np) {
		DRM_ERROR("%s: can not find display-timings node\n",
			lcd_node->name);
		return -ENODEV;
	}

	num_timings = of_get_child_count(timings_np);
	if (num_timings == 0) {
		/* should never happen, as entry was already found above */
		DRM_ERROR("%s: no timings specified\n", lcd_node->name);
		goto done;
	}

	info->buildin_modes = kzalloc(sizeof(struct drm_display_mode) *
				num_timings, GFP_KERNEL);

	for (i = 0; i < num_timings; i++) {
		rc = of_get_drm_display_mode(lcd_node,
			&info->buildin_modes[i], NULL, i);
		if (rc) {
			DRM_ERROR("get display timing failed\n");
			goto entryfail;
		}

		info->buildin_modes[i].width_mm = info->mode.width_mm;
		info->buildin_modes[i].height_mm = info->mode.height_mm;
		info->buildin_modes[i].vrefresh = info->mode.vrefresh;
	}
	info->num_buildin_modes = num_timings;
	DRM_INFO("info->num_buildin_modes = %d\n", num_timings);
	goto done;

entryfail:
	kfree(info->buildin_modes);
done:
	of_node_put(timings_np);

	return 0;
}

static int of_parse_oled_cmds(struct sprd_oled *oled,
		const void *data, int size)
{
	const struct dsi_cmd_desc *cmds = data;
	struct dsi_cmd_desc *p;
	u16 len;
	int i, total;

	if (cmds == NULL)
		return -EINVAL;

	/*
	 * TODO:
	 * Currently, we only support the same length cmds
	 * for oled brightness level. So we take the first
	 * cmd payload length as all.
	 */
	len = (cmds->wc_h << 8) | cmds->wc_l;
	total =  size / (len + 4);

	p = (struct dsi_cmd_desc *)kzalloc(size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memcpy(p, cmds, size);
	for (i = 0; i < total; i++) {
		oled->cmds[i] = p;
		p = (struct dsi_cmd_desc *)(p->payload + len);
	}

	oled->cmds_total = total;
	oled->cmd_len = len + 4;

	return 0;
}

#if 0
int sprd_close_brightness(void)
{
    struct sprd_panel *panel = oled_xr->panel;

    mutex_lock(&panel_lock);

    mdelay(25);
    sprd_panel_send_cmds(panel->slave,
            oled_xr->cmds[0],
            oled_xr->cmd_len);
    sprd_panel_send_cmds(panel->slave,
            panel->info.cmds[CMD_CODE_SLEEP_IN],
            panel->info.cmds_len[CMD_CODE_SLEEP_IN]);
    mdelay(25);
    sprd_panel_send_cmds(panel->slave,
            panel->info.cmds[CMD_CODE_SLEEP_IN2],
            panel->info.cmds_len[CMD_CODE_SLEEP_IN2]);
    mdelay(5);
    


    mutex_unlock(&panel_lock);

    return 0;


}
#endif


int sprd_close_gowin_brightness(void)
{
    int i = 0;
    struct sprd_panel *panel = oled_xr->panel;

    mutex_lock(&panel_lock);
    for(i = 0; i++; i< 10)
    {
        sprd_panel_send_cmds(panel->slave,
                oled_xr->cmds[0],
                oled_xr->cmd_len);
    }

    mutex_unlock(&panel_lock);

    return 0;


}

static int sprd_oled_set_brightness(struct backlight_device *bdev)
{
	int level, brightness;
	struct sprd_oled *oled = bl_get_data(bdev);
	struct sprd_panel *panel = oled->panel;
    int i;

	mutex_lock(&panel_lock);
	if (!panel->is_enabled) {
		mutex_unlock(&panel_lock);
		DRM_WARN("oled panel has been powered off\n");
		return -ENXIO;
	}

	brightness = bdev->props.brightness;
	level = brightness * oled->max_level / 255;
   
    DRM_INFO("%s level: %d\n", __func__, level);

	sprd_panel_send_cmds(panel->slave,
			     panel->info.cmds[CMD_OLED_REG_LOCK],
			     panel->info.cmds_len[CMD_OLED_REG_LOCK]);

    if (oled->cmds_total == 1) {
        oled->cmds[0]->payload[1] = level;
        sprd_panel_send_cmds(panel->slave,
                oled->cmds[0],
                oled->cmd_len);
    } else {
#ifdef CONFIG_GOWIN_FPGA
        if(flag == 1){
            for (i = 0; i < 10; i++)
                sprd_panel_send_cmds(panel->slave,
                        oled->cmds[level],
                        oled->cmd_len);
            flag = 0;
        } else {
            if(level == 0)
            {
                for(i = 0; i< 10; i++)
                    sprd_panel_send_cmds(panel->slave,
                            oled->cmds[level],
                            oled->cmd_len);
            } else {
                sprd_panel_send_cmds(panel->slave,
                        oled->cmds[level],
                        oled->cmd_len);
            }
        }
#endif
#ifdef CONFIG_LATTICE_FPGA
        if(flag == 1){
            for(i = 0;i<2;i++)
            {
                mdelay(25);
                sprd_panel_send_cmds(panel->slave,
                        oled->cmds[level],
                        oled->cmd_len);
            }
            flag = 0;
        } else {
            if(level == 0)
            {

                mdelay(50);
                sprd_panel_send_cmds(panel->slave,
                        oled->cmds[level],
                        oled->cmd_len);
                mdelay(5);

            } else {
                sprd_panel_send_cmds(panel->slave,
                        oled->cmds[level],
                        oled->cmd_len);

            }
        }
#endif

    }
    sprd_panel_send_cmds(panel->slave,
            panel->info.cmds[CMD_OLED_REG_UNLOCK],
			     panel->info.cmds_len[CMD_OLED_REG_UNLOCK]);

	mutex_unlock(&panel_lock);

	return 0;
}

static const struct backlight_ops sprd_oled_backlight_ops = {
	.update_status = sprd_oled_set_brightness,
};

static int sprd_oled_backlight_init(struct sprd_panel *panel)
{
	struct sprd_oled *oled;
	struct device_node *oled_node;
	struct panel_info *info = &panel->info;
	const void *p;
	int bytes, rc;
	u32 temp;

	oled_node = of_get_child_by_name(info->of_node,
				"oled-backlight");
	if (!oled_node)
		return 0;

	oled = devm_kzalloc(&panel->dev,
			sizeof(struct sprd_oled), GFP_KERNEL);
	if (!oled)
		return -ENOMEM;

	oled->bdev = devm_backlight_device_register(&panel->dev,
			"sprd_backlight", &panel->dev, oled,
			&sprd_oled_backlight_ops, NULL);
	if (IS_ERR(oled->bdev)) {
		DRM_ERROR("failed to register oled backlight ops\n");
		return PTR_ERR(oled->bdev);
	}

	p = of_get_property(oled_node, "brightness-levels", &bytes);
	if (p) {
		info->cmds[CMD_OLED_BRIGHTNESS] = p;
		info->cmds_len[CMD_OLED_BRIGHTNESS] = bytes;
	} else
		DRM_ERROR("can't find brightness-levels property\n");

	p = of_get_property(oled_node, "sprd,reg-lock", &bytes);
	if (p) {
		info->cmds[CMD_OLED_REG_LOCK] = p;
		info->cmds_len[CMD_OLED_REG_LOCK] = bytes;
	} else
		DRM_INFO("can't find sprd,reg-lock property\n");

	p = of_get_property(oled_node, "sprd,reg-unlock", &bytes);
	if (p) {
		info->cmds[CMD_OLED_REG_UNLOCK] = p;
		info->cmds_len[CMD_OLED_REG_UNLOCK] = bytes;
	} else
		DRM_INFO("can't find sprd,reg-unlock property\n");

	rc = of_property_read_u32(oled_node, "default-brightness-level", &temp);
	if (!rc)
		oled->bdev->props.brightness = temp;
	else
		oled->bdev->props.brightness = 25;

	rc = of_property_read_u32(oled_node, "sprd,max-level", &temp);
	if (!rc)
		oled->max_level = temp;
	else
		oled->max_level = 255;

	oled->bdev->props.max_brightness = 255;
	oled->panel = panel;
	of_parse_oled_cmds(oled,
			panel->info.cmds[CMD_OLED_BRIGHTNESS],
			panel->info.cmds_len[CMD_OLED_BRIGHTNESS]);
    oled_xr= oled;
	DRM_INFO("%s() ok\n", __func__);

	return 0;
}

int sprd_panel_parse_lcddtb(struct device_node *lcd_node,
	struct sprd_panel *panel)
{
	u32 val;
	struct panel_info *info = &panel->info;
	int bytes, rc;
	const void *p;
	const char *str;

	if (!lcd_node) {
		DRM_ERROR("Lcd node from dtb is Null\n");
		return -ENODEV;
	}
	info->of_node = lcd_node;

	rc = of_property_read_u32(lcd_node, "sprd,dsi-work-mode", &val);
	if (!rc) {
		if (val == SPRD_DSI_MODE_CMD)
			info->mode_flags = 0;
		else if (val == SPRD_DSI_MODE_VIDEO_BURST)
			info->mode_flags = MIPI_DSI_MODE_VIDEO |
					   MIPI_DSI_MODE_VIDEO_BURST;
		else if (val == SPRD_DSI_MODE_VIDEO_SYNC_PULSE)
			info->mode_flags = MIPI_DSI_MODE_VIDEO |
					   MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
		else if (val == SPRD_DSI_MODE_VIDEO_SYNC_EVENT)
			info->mode_flags = MIPI_DSI_MODE_VIDEO;
	} else {
		DRM_ERROR("dsi work mode is not found! use video mode\n");
		info->mode_flags = MIPI_DSI_MODE_VIDEO |
				   MIPI_DSI_MODE_VIDEO_BURST;
	}

	if (of_property_read_bool(lcd_node, "sprd,dsi-non-continuous-clock"))
		info->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS;

	rc = of_property_read_u32(lcd_node, "sprd,dsi-lane-number", &val);
	if (!rc)
		info->lanes = val;
	else
		info->lanes = 4;

	rc = of_property_read_string(lcd_node, "sprd,dsi-color-format", &str);
	if (rc)
		info->format = MIPI_DSI_FMT_RGB888;
	else if (!strcmp(str, "rgb888"))
		info->format = MIPI_DSI_FMT_RGB888;
	else if (!strcmp(str, "rgb666"))
		info->format = MIPI_DSI_FMT_RGB666;
	else if (!strcmp(str, "rgb666_packed"))
		info->format = MIPI_DSI_FMT_RGB666_PACKED;
	else if (!strcmp(str, "rgb565"))
		info->format = MIPI_DSI_FMT_RGB565;
	else if (!strcmp(str, "dsc"))
		info->format = SPRD_MIPI_DSI_FMT_DSC;
	else
		DRM_ERROR("dsi-color-format (%s) is not supported\n", str);

	rc = of_property_read_u32(lcd_node, "sprd,width-mm", &val);
	if (!rc)
		info->mode.width_mm = val;
	else
		info->mode.width_mm = 68;

	rc = of_property_read_u32(lcd_node, "sprd,height-mm", &val);
	if (!rc)
		info->mode.height_mm = val;
	else
		info->mode.height_mm = 121;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-enable", &val);
	if (!rc)
		info->esd_check_en = val;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-mode", &val);
	if (!rc)
		info->esd_check_mode = val;
	else
		info->esd_check_mode = 1;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-period", &val);
	if (!rc)
		info->esd_check_period = val;
	else
		info->esd_check_period = 1000;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-register", &val);
	if (!rc)
		info->esd_check_reg = val;
	else
		info->esd_check_reg = 0x0A;

	rc = of_property_read_u32(lcd_node, "sprd,esd-check-value", &val);
	if (!rc)
		info->esd_check_val = val;
	else
		info->esd_check_val = 0x9C;

	if (of_property_read_bool(lcd_node, "sprd,use-dcs-write"))
		info->use_dcs = true;
	else
		info->use_dcs = false;

	rc = of_parse_reset_seq(lcd_node, info);
	if (rc)
		DRM_ERROR("parse lcd reset sequence failed\n");

	p = of_get_property(lcd_node, "sprd,initial-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_INIT] = p;
		info->cmds_len[CMD_CODE_INIT] = bytes;
	} else
		DRM_ERROR("can't find sprd,initial-command property\n");

	p = of_get_property(lcd_node, "sprd,sleep-in1-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_SLEEP_IN] = p;
		info->cmds_len[CMD_CODE_SLEEP_IN] = bytes;
	} else
		DRM_ERROR("can't find sprd,sleep-in1-command property\n");
	p = of_get_property(lcd_node, "sprd,sleep-in2-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_SLEEP_IN2] = p;
		info->cmds_len[CMD_CODE_SLEEP_IN2] = bytes;
	} else
		DRM_ERROR("can't find sprd,sleep-in2-command property\n");


	p = of_get_property(lcd_node, "sprd,sleep-out-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_SLEEP_OUT] = p;
		info->cmds_len[CMD_CODE_SLEEP_OUT] = bytes;
	} else
		DRM_ERROR("can't find sprd,sleep-out-command property\n");

	rc = of_get_drm_display_mode(lcd_node, &info->mode, 0,
				     OF_USE_NATIVE_MODE);
	if (rc) {
		DRM_ERROR("get display timing failed\n");
		return rc;
	}

	info->mode.vrefresh = drm_mode_vrefresh(&info->mode);
	of_parse_buildin_modes(info, lcd_node);

	return 0;
}

/*
 * FIXME:
 * Adding the following interfaces is just for fbdev emulation support.
 * Put panel into component match lists. drm just bind after panel probe.
 */
static int sprd_panel_bind(struct device *dev,
			struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);
	return 0;
}

static void sprd_panel_unbind(struct device *dev,
			struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);
}

static const struct component_ops panel_component_ops = {
	.bind	= sprd_panel_bind,
	.unbind	= sprd_panel_unbind,
};


static int sprd_panel_parse_dt(struct device_node *np, struct sprd_panel *panel)
{
	struct device_node *lcd_node;
	int rc;
	const char *str;
	char lcd_path[60];

	rc = of_property_read_string(np, "sprd,force-attached", &str);
	if (!rc)
		lcd_name = str;

	sprintf(lcd_path, "/lcds/%s", lcd_name);
	lcd_node = of_find_node_by_path(lcd_path);
	if (!lcd_node) {
		DRM_ERROR("%pOF: could not find %s node\n", np, lcd_name);
		return -ENODEV;
	}
	rc = sprd_panel_parse_lcddtb(lcd_node, panel);
	if (rc)
		return rc;

	return 0;
}

static int sprd_panel_device_create(struct device *parent,
				    struct sprd_panel *panel)
{
	panel->dev.class = display_class;
	panel->dev.parent = parent;
	panel->dev.of_node = panel->info.of_node;
	dev_set_name(&panel->dev, "panel0");
	dev_set_drvdata(&panel->dev, panel);

	return device_register(&panel->dev);
}

static int sprd_panel_probe(struct mipi_dsi_device *slave)
{
	int ret;
	struct sprd_panel *panel;
	struct device_node *bl_node;



#ifdef CONFIG_GOWIN_FPGA
    fpga3v3 = regulator_get(NULL, "vddldo2");
    if (IS_ERR(fpga3v3) || !fpga3v3) {
        printk("get gowin 3v3 failed, return!\n");
        return -EFAULT;
    }
    regulator_set_voltage(fpga3v3, 3300000, 3300000);
    ret = regulator_enable(fpga3v3);
    if(ret != 0)
        printk("gowin %s: some error happen, fail to enable regulator!\n", __func__);
#endif
    lcd3v3 = regulator_get(NULL, "vddldo1");
    if (IS_ERR(lcd3v3) || !lcd3v3) {
        printk("get lcd 3v3 failed, return!\n");
        return -EFAULT;
    }
    regulator_set_voltage(lcd3v3, 3300000, 3300000);
    ret = regulator_enable(lcd3v3);
    if(ret != 0)
        printk("lcd %s: some error happen, fail to enable regulator!\n", __func__);
   
	panel = devm_kzalloc(&slave->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	bl_node = of_parse_phandle(slave->dev.of_node,
					"sprd,backlight", 0);
	if (bl_node) {
		panel->backlight = of_find_backlight_by_node(bl_node);
		of_node_put(bl_node);

		if (panel->backlight) {
			panel->backlight->props.state &= ~BL_CORE_FBBLANK;
			panel->backlight->props.power = FB_BLANK_UNBLANK;
			backlight_update_status(panel->backlight);
		} else {
			DRM_WARN("backlight is not ready, panel probe deferred\n");
			return -EPROBE_DEFER;
		}
	} else
		DRM_WARN("backlight node not found\n");

	panel->supply = devm_regulator_get(&slave->dev, "power");
	if (IS_ERR(panel->supply)) {
		if (PTR_ERR(panel->supply) == -EPROBE_DEFER)
			DRM_ERROR("regulator driver not initialized, probe deffer\n");
		else
			DRM_ERROR("can't get regulator: %ld\n", PTR_ERR(panel->supply));

		return PTR_ERR(panel->supply);
	}

	INIT_DELAYED_WORK(&panel->esd_work, sprd_panel_esd_work_func);

	ret = sprd_panel_parse_dt(slave->dev.of_node, panel);
	if (ret) {
		DRM_ERROR("parse panel info failed\n");
		return ret;
	}

	ret = sprd_panel_gpio_request(&slave->dev, panel);
	if (ret) {
		DRM_WARN("gpio is not ready, panel probe deferred\n");
		return -EPROBE_DEFER;
	}

	ret = sprd_panel_device_create(&slave->dev, panel);
	if (ret) {
		DRM_ERROR("panel device create failed\n");
		return ret;
	}

	ret = sprd_oled_backlight_init(panel);
	if (ret) {
		DRM_ERROR("oled backlight init failed\n");
		return ret;
	}

	panel->base.dev = &panel->dev;
	panel->base.funcs = &sprd_panel_funcs;
	drm_panel_init(&panel->base);

	ret = drm_panel_add(&panel->base);
	if (ret) {
		DRM_ERROR("drm_panel_add() failed\n");
		return ret;
	}

	slave->lanes = panel->info.lanes;
	slave->format = panel->info.format;
	slave->mode_flags = panel->info.mode_flags;

	ret = mipi_dsi_attach(slave);
	if (ret) {
		DRM_ERROR("failed to attach dsi panel to host\n");
		drm_panel_remove(&panel->base);
		return ret;
	}
	panel->slave = slave;

	sprd_panel_sysfs_init(&panel->dev);
	mipi_dsi_set_drvdata(slave, panel);

	/*
	 * FIXME:
	 * The esd check work should not be scheduled in probe
	 * function. It should be scheduled in the enable()
	 * callback function. But the dsi encoder will not call
	 * drm_panel_enable() the first time in encoder_enable().
	 */
	if (panel->info.esd_check_en) {
		schedule_delayed_work(&panel->esd_work,
				      msecs_to_jiffies(2000));
		panel->esd_work_pending = true;
	}

	panel->is_enabled = true;

    DRM_INFO("panel driver probe success\n");
    fpga_panel = panel;
    fpga_sysfs_init();
    return component_add(&slave->dev, &panel_component_ops);
}

static int sprd_panel_remove(struct mipi_dsi_device *slave)
{
	struct sprd_panel *panel = mipi_dsi_get_drvdata(slave);
	int ret;

	DRM_INFO("%s()\n", __func__);

	component_del(&slave->dev, &panel_component_ops);

	sprd_panel_disable(&panel->base);
	sprd_panel_unprepare(&panel->base);

	ret = mipi_dsi_detach(slave);
	if (ret < 0)
		DRM_ERROR("failed to detach from DSI host: %d\n", ret);

	drm_panel_detach(&panel->base);
	drm_panel_remove(&panel->base);

	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "sprd,generic-mipi-panel", },
	{ }
};
MODULE_DEVICE_TABLE(of, panel_of_match);

static struct mipi_dsi_driver sprd_panel_driver = {
	.driver = {
		.name = "sprd-mipi-panel-drv",
		.of_match_table = panel_of_match,
	},
	.probe = sprd_panel_probe,
	.remove = sprd_panel_remove,
};
module_mipi_dsi_driver(sprd_panel_driver);

MODULE_AUTHOR("Leon He <leon.he@unisoc.com>");
MODULE_DESCRIPTION("SPRD MIPI DSI Panel Driver");
MODULE_LICENSE("GPL v2");
