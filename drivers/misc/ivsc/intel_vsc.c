// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Intel Corporation */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/vsc.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#include "intel_vsc.h"

#define ACE_PRIVACY_ON 2

struct intel_vsc {
	spinlock_t lock;
	struct mutex mutex;

	void *csi;
	struct vsc_csi_ops *csi_ops;
	uint16_t csi_registerred;

	void *ace;
	struct vsc_ace_ops *ace_ops;
	uint16_t ace_registerred;

	uint32_t lane_num;
	int sensor_on;

	struct acpi_device *sensor;
	char sensor_name[64];
	struct vsc_regulator {
		char regulator_name[64];
		char supply_name[64];
		struct regulator_dev *rdev;
		struct regulator_desc rdesc;
	} regulator;

	struct vsc_clock {
		struct clk *clk;
		struct clk_hw hw;
		struct clk_lookup *cl;
		unsigned long rate;
	} clock;
};

static struct intel_vsc vsc;

static int intel_vsc_register_regulator(struct device *dev);
static int intel_vsc_register_clock(struct device *dev);
int check_component_ready(struct device *dev)
{
	int ret = -1;
	unsigned long flags;

	spin_lock_irqsave(&vsc.lock, flags);
	pr_info("%s %d\n", __func__, __LINE__);
	if (vsc.ace_registerred && vsc.csi_registerred) {
		pr_info("%s %d\n", __func__, __LINE__);
		vsc.sensor = acpi_dev_get_first_consumer_dev(
			ACPI_COMPANION(dev->parent));
		if (!vsc.sensor)
			return -ENODEV;

		ret = intel_vsc_register_regulator(dev);
		pr_info("%s %d %d\n", __func__, __LINE__, ret);
		ret = intel_vsc_register_clock(dev);
		pr_info("%s %d %d\n", __func__, __LINE__, ret);

		ret = 0;
	}

	spin_unlock_irqrestore(&vsc.lock, flags);

	return ret;
}

static void update_camera_status(struct vsc_camera_status *status,
				 struct camera_status *s)
{
	if (status && s) {
		status->owner = s->camera_owner;
		status->exposure_level = s->exposure_level;
		status->status = VSC_PRIVACY_OFF;

		if (s->privacy_stat == ACE_PRIVACY_ON)
			status->status = VSC_PRIVACY_ON;
	}
}

int vsc_register_ace(struct device *dev, void *ace, struct vsc_ace_ops *ops)
{
	unsigned long flags;

	if (ace && ops) {
		if (ops->ipu_own_camera && ops->ace_own_camera) {
			spin_lock_irqsave(&vsc.lock, flags);

			vsc.ace = ace;
			vsc.ace_ops = ops;
			vsc.ace_registerred = true;
			spin_unlock_irqrestore(&vsc.lock, flags);
			check_component_ready(dev);

			return 0;
		}
	}

	pr_err("register ace failed\n");
	return -1;
}
EXPORT_SYMBOL_GPL(vsc_register_ace);

void vsc_unregister_ace(void)
{
	unsigned long flags;

	spin_lock_irqsave(&vsc.lock, flags);

	vsc.ace_registerred = false;

	spin_unlock_irqrestore(&vsc.lock, flags);
}
EXPORT_SYMBOL_GPL(vsc_unregister_ace);

int vsc_register_csi(struct device *dev, void *csi, struct vsc_csi_ops *ops)
{
	unsigned long flags;

	if (csi && ops) {
		if (ops->set_privacy_callback && ops->set_owner &&
		    ops->set_mipi_conf) {
			spin_lock_irqsave(&vsc.lock, flags);

			vsc.csi = csi;
			vsc.csi_ops = ops;
			vsc.csi_registerred = true;
			spin_unlock_irqrestore(&vsc.lock, flags);
			check_component_ready(dev);

			return 0;
		}
	}

	pr_err("register csi failed\n");
	return -1;
}
EXPORT_SYMBOL_GPL(vsc_register_csi);

void vsc_unregister_csi(void)
{
	unsigned long flags;

	spin_lock_irqsave(&vsc.lock, flags);

	vsc.csi_registerred = false;

	spin_unlock_irqrestore(&vsc.lock, flags);
}
EXPORT_SYMBOL_GPL(vsc_unregister_csi);

int vsc_acquire_camera_sensor(struct vsc_mipi_config *config,
			      vsc_privacy_callback_t callback, void *handle,
			      struct vsc_camera_status *status)
{
	int ret;
	struct camera_status s;
	struct mipi_conf conf = { 0 };

	struct vsc_csi_ops *csi_ops;
	struct vsc_ace_ops *ace_ops;

	if (!config)
		return -EINVAL;
	pr_info("%s %d\n", __func__, __LINE__);
	// ret = check_component_ready();
	// if (ret < 0) {
	// 	pr_info("intel vsc not ready\n");
	// 	return -EAGAIN;
	// }

	mutex_lock(&vsc.mutex);
	/* no need check component again here */

	csi_ops = vsc.csi_ops;
	ace_ops = vsc.ace_ops;

	csi_ops->set_privacy_callback(vsc.csi, callback, handle);

	ret = ace_ops->ipu_own_camera(vsc.ace, &s);
	if (ret) {
		pr_err("ipu own camera failed\n");
		goto err;
	}
	update_camera_status(status, &s);

	ret = csi_ops->set_owner(vsc.csi, CSI_IPU);
	if (ret) {
		pr_err("ipu own csi failed\n");
		goto err;
	}

	conf.lane_num = config->lane_num;
	conf.freq = config->freq;
	ret = csi_ops->set_mipi_conf(vsc.csi, &conf);
	if (ret) {
		pr_err("config mipi failed\n");
		goto err;
	}

err:
	mutex_unlock(&vsc.mutex);
	msleep(100);
	return ret;
}
EXPORT_SYMBOL_GPL(vsc_acquire_camera_sensor);

int vsc_release_camera_sensor(struct vsc_camera_status *status)
{
	int ret;
	struct camera_status s;

	struct vsc_csi_ops *csi_ops;
	struct vsc_ace_ops *ace_ops;

	// ret = check_component_ready();
	// if (ret < 0) {
	// 	pr_info("intel vsc not ready\n");
	// 	return -EAGAIN;
	// }
	pr_info("%s %d\n", __func__, __LINE__);
	mutex_lock(&vsc.mutex);
	/* no need check component again here */

	csi_ops = vsc.csi_ops;
	ace_ops = vsc.ace_ops;

	csi_ops->set_privacy_callback(vsc.csi, NULL, NULL);

	ret = csi_ops->set_owner(vsc.csi, CSI_FW);
	if (ret) {
		pr_err("vsc own csi failed\n");
		goto err;
	}

	ret = ace_ops->ace_own_camera(vsc.ace, &s);
	if (ret) {
		pr_err("vsc own camera failed\n");
		goto err;
	}
	update_camera_status(status, &s);

err:
	mutex_unlock(&vsc.mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(vsc_release_camera_sensor);

static int sensor_is_poweron(struct regulator_dev *dev)
{
	return vsc.sensor_on;
}

static int sensor_power_on(struct regulator_dev *dev)
{
	struct vsc_mipi_config conf;
	struct vsc_camera_status status;
	int ret;

	dev_info(&dev->dev, "power on vsc.sensor.\n");
	conf.lane_num = vsc.lane_num;
	/* frequency unit 100k */
	conf.freq = vsc.clock.rate / 100000;
	ret = vsc_acquire_camera_sensor(&conf, NULL, NULL, &status);
	if (ret) {
		dev_err(&dev->dev, "acquire vsc failed\n");
		return ret;
	}
	vsc.sensor_on = 1;
	return 0;
}

static int sensor_power_off(struct regulator_dev *dev)
{
	struct vsc_camera_status status;
	int ret;

	dev_info(&dev->dev, "power off vsc.sensor.\n");
	ret = vsc_release_camera_sensor(&status);
	if (ret) {
		dev_err(&dev->dev, "release vsc failed\n");
		return ret;
	}
	vsc.sensor_on = 0;
	return 0;
}

struct regulator_ops rops = {
	.enable = sensor_power_on,
	.disable = sensor_power_off,
	.is_enabled = sensor_is_poweron,
};

struct mipi_camera_link_ssdb {
	u8 version;
	u8 sku;
	u8 guid_csi2[16];
	u8 devfunction;
	u8 bus;
	u32 dphylinkenfuses;
	u32 clockdiv;
	u8 link;
	u8 lanes;
	u32 csiparams[10];
	u32 maxlanespeed;
	u8 sensorcalibfileidx;
	u8 sensorcalibfileidxInMBZ[3];
	u8 romtype;
	u8 vcmtype;
	u8 platforminfo;
	u8 platformsubinfo;
	u8 flash;
	u8 privacyled;
	u8 degree;
	u8 mipilinkdefined;
	u32 mclkspeed;
	u8 controllogicid;
	u8 reserved1[3];
	u8 mclkport;
	u8 reserved2[13];
} __packed;

static int get_mipi_lanes(struct acpi_device *adev)
{
	struct mipi_camera_link_ssdb ssdb;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	if (!adev) {
		pr_info("Not ACPI device\n");
		return -ENODEV;
	}

	status = acpi_evaluate_object(adev->handle, "SSDB", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		pr_info("ACPI fail: %d\n", -ENODEV);
		return -ENODEV;
	}

	obj = buffer.pointer;
	if (!obj || obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length > sizeof(ssdb)) {
		pr_info("Couldn't locate ACPI buffer\n");
		ret = -EINVAL;
		goto out_free_buff;
	}

	memcpy(&ssdb, obj->buffer.pointer, obj->buffer.length);
	pr_info("ssdb.lanes:%d ssdb.mclkspeed:%d ssdb.clockdiv:%d ssdb.maxlanespeed:%d \n",
		ssdb.lanes, ssdb.mclkspeed, ssdb.clockdiv, ssdb.maxlanespeed);

out_free_buff:
	kfree(buffer.pointer);
	if (ret)
		return ret;

	return ssdb.lanes;
}

#define DEFULT_LINK_FREQ_400MHZ 400000000ULL
static int intel_vsc_register_regulator(struct device *dev)
{
	struct regulator_config cfg = {};
	struct regulator_init_data init_data = {};
	struct regulator_consumer_supply supply_map;

	pr_info("%s %d\n", __func__, __LINE__);

	pr_info("vsc.sensor name %s\n", acpi_dev_name(vsc.sensor));
	vsc.lane_num = get_mipi_lanes(vsc.sensor);

	snprintf(vsc.regulator.regulator_name,
		 sizeof(vsc.regulator.regulator_name), "%s-regulator",
		 acpi_dev_name(vsc.sensor));
	snprintf(vsc.regulator.supply_name, sizeof(vsc.regulator.supply_name),
		 "cvf-cam-supply");
	snprintf(vsc.sensor_name, sizeof(vsc.sensor_name), "i2c-%s", acpi_dev_name(vsc.sensor));

	init_data.constraints.valid_ops_mask = REGULATOR_CHANGE_STATUS;
	init_data.num_consumer_supplies = 1;
	supply_map.dev_name = vsc.sensor_name;
	supply_map.supply = vsc.regulator.supply_name;
	init_data.consumer_supplies = &supply_map;

	vsc.regulator.rdesc.name = vsc.regulator.regulator_name;
	vsc.regulator.rdesc.type = REGULATOR_VOLTAGE;
	vsc.regulator.rdesc.owner = THIS_MODULE;
	vsc.regulator.rdesc.ops = &rops;

	cfg.dev = dev;
	cfg.init_data = &init_data;
	vsc.regulator.rdev = regulator_register(&vsc.regulator.rdesc, &cfg);
	if (IS_ERR(vsc.regulator.rdev)) {
		pr_err("failed to register regulator\n");
		return PTR_ERR(vsc.regulator.rdev);
	}

	return 0;
}

#define to_vsc_clk(hw) container_of(hw, struct vsc_clock, hw)
static unsigned long intel_vsc_clk_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct vsc_clock *clk = to_vsc_clk(hw);

	pr_info("%s %d %d\n", __func__, __LINE__, clk->rate);

	return clk->rate;
}

static int intel_vsc_clk_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	pr_info("%s %d \n", __func__, __LINE__);
	/* Just return the same rate without modifying it */
	return 0;
}

static int intel_vsc_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct vsc_clock *clk = to_vsc_clk(hw);

	pr_info("%s %d %d\n", __func__, __LINE__, rate);
	clk->rate = rate;
	return 0;
}

static const struct clk_ops intel_vsc_clk_rate_ops = {
	.recalc_rate = intel_vsc_clk_recalc_rate,
	.determine_rate = intel_vsc_clk_determine_rate,
	.set_rate = intel_vsc_clk_set_rate,
};

static int intel_vsc_register_clock(struct device *dev)
{
	int ret = 0;
	struct clk_init_data init = {
		.ops = &intel_vsc_clk_rate_ops,
		.flags = CLK_GET_RATE_NOCACHE,
	};

	init.name = kasprintf(GFP_KERNEL, "%s-clk", acpi_dev_name(vsc.sensor));
	if (!init.name)
		return -ENOMEM;

	/* default csi clock rate */
	vsc.clock.rate = DEFULT_LINK_FREQ_400MHZ;
	vsc.clock.hw.init = &init;
	vsc.clock.clk = clk_register(dev, &vsc.clock.hw);
	if (IS_ERR(vsc.clock.clk)) {
		ret = PTR_ERR(vsc.clock.clk);
		goto out_free_init_name;
	}

	vsc.clock.cl = clkdev_create(vsc.clock.clk, NULL, vsc.sensor_name);
	if (!vsc.clock.cl) {
		ret = -ENOMEM;
		goto err_unregister_clk;
	}

	kfree(init.name);
	return 0;

err_unregister_clk:
	clk_unregister(vsc.clock.clk);
out_free_init_name:
	kfree(init.name);
	return ret;
}

static int __init intel_vsc_init(void)
{
	memset(&vsc, 0, sizeof(struct intel_vsc));

	spin_lock_init(&vsc.lock);
	mutex_init(&vsc.mutex);

	vsc.csi_registerred = false;
	vsc.ace_registerred = false;

	return 0;
}

static void __exit intel_vsc_exit(void)
{
}

module_init(intel_vsc_init);
module_exit(intel_vsc_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("post: mei_csi mei_ace");
MODULE_DESCRIPTION("Device driver for Intel VSC");
