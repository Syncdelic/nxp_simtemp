// SPDX-License-Identifier: GPL-2.0
#include "nxp_simtemp.h"
#include "nxp_simtemp_ioctl.h"

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/version.h>

static bool force_create_dev;
module_param(force_create_dev, bool, 0444);
MODULE_PARM_DESC(force_create_dev,
		"Create a temporary platform_device on load (for x86 dev)");

static DEFINE_IDA(simtemp_ida);
static struct class *simtemp_class;
static struct platform_device *simtemp_pdev;

static struct simtemp_device *simtemp_from_classdev(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static ssize_t sampling_ms_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sim;
	u32 sampling;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	mutex_lock(&sim->lock);
	sampling = sim->sampling_ms;
	mutex_unlock(&sim->lock);

	return sysfs_emit(buf, "%u\n", sampling);
}

static ssize_t sampling_ms_store(struct device *dev,
			struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct simtemp_device *sim;
	unsigned int value;
	unsigned int clamped;
	int ret;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &value);
	if (ret != 0)
		return ret;

	clamped = clamp_t(unsigned int, value,
			 SIMTEMP_SAMPLING_MS_MIN, SIMTEMP_SAMPLING_MS_MAX);

	mutex_lock(&sim->lock);
	if (clamped != value)
		dev_warn(sim->dev,
			 "sampling_ms clamped to %u ms (was %u)\n",
			 clamped, value);
	sim->sampling_ms = clamped;
	mutex_unlock(&sim->lock);

	return count;
}
static DEVICE_ATTR_RW(sampling_ms);

static ssize_t threshold_mC_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sim;
	s32 threshold;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	mutex_lock(&sim->lock);
	threshold = sim->threshold_mc;
	mutex_unlock(&sim->lock);

	return sysfs_emit(buf, "%d\n", threshold);
}

static ssize_t threshold_mC_store(struct device *dev,
			struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct simtemp_device *sim;
	int value;
	int ret;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &value);
	if (ret != 0)
		return ret;

	mutex_lock(&sim->lock);
	sim->threshold_mc = value;
	mutex_unlock(&sim->lock);

	return count;
}
static DEVICE_ATTR_RW(threshold_mC);

static struct attribute *simtemp_attrs[] = {
	&dev_attr_sampling_ms.attr,
	&dev_attr_threshold_mC.attr,
	NULL,
};

static const struct attribute_group simtemp_group = {
	.attrs = simtemp_attrs,
};

static const struct attribute_group *simtemp_groups[] = {
	&simtemp_group,
	NULL,
};

int simtemp_sysfs_register(struct simtemp_device *sim)
{
	struct device *class_dev;

	class_dev = device_create_with_groups(simtemp_class, sim->dev, MKDEV(0, 0),
					       sim, simtemp_groups,
					       SIMTEMP_DEVICE_NAME_FMT, sim->id);
	if (IS_ERR(class_dev))
		return PTR_ERR(class_dev);

	sim->class_dev = class_dev;

	return 0;
}

void simtemp_sysfs_unregister(struct simtemp_device *sim)
{
	if (IS_ERR_OR_NULL(sim->class_dev))
		return;

	device_unregister(sim->class_dev);
	sim->class_dev = NULL;
}

static int simtemp_probe(struct platform_device *pdev)
{
	struct simtemp_device *sim;
	int ret;

	sim = devm_kzalloc(&pdev->dev, sizeof(*sim), GFP_KERNEL);
	if (sim == NULL)
		return -ENOMEM;

	mutex_init(&sim->lock);
	sim->dev = &pdev->dev;
	sim->class_dev = NULL;
	sim->sampling_ms = SIMTEMP_DEFAULT_SAMPLING_MS;
	sim->threshold_mc = SIMTEMP_DEFAULT_THRESHOLD_MC;

	ret = ida_alloc(&simtemp_ida, GFP_KERNEL);
	if (ret < 0) {
		mutex_destroy(&sim->lock);
		return ret;
	}

	sim->id = ret;

	ret = simtemp_sysfs_register(sim);
	if (ret < 0) {
		ida_free(&simtemp_ida, sim->id);
		mutex_destroy(&sim->lock);
		return ret;
	}

	platform_set_drvdata(pdev, sim);

	dev_info(&pdev->dev,
		 "%s probed%s (sampling=%u ms threshold=%d mC)\n",
		 SIMTEMP_DRIVER_NAME,
		 (pdev->dev.of_node != NULL) ? " (DT match)" : " (name match)",
		 sim->sampling_ms, sim->threshold_mc);

	return 0;
}

static void simtemp_remove(struct platform_device *pdev)
{
	struct simtemp_device *sim;

	sim = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);

	if (sim != NULL) {
		simtemp_sysfs_unregister(sim);
		ida_free(&simtemp_ida, sim->id);
		mutex_destroy(&sim->lock);
	}

	dev_info(&pdev->dev, "%s remove\n", SIMTEMP_DRIVER_NAME);
}

static const struct of_device_id simtemp_of_match[] = {
	{ .compatible = SIMTEMP_COMPATIBLE },
	{ }
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static struct platform_driver simtemp_driver = {
	.probe = simtemp_probe,
	.remove = simtemp_remove,
	.driver = {
		.name = SIMTEMP_DRIVER_NAME,
		.of_match_table = simtemp_of_match,
	},
};

static int __init simtemp_init(void)
{
	int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	simtemp_class = class_create(SIMTEMP_CLASS_NAME);
#else
	simtemp_class = class_create(THIS_MODULE, SIMTEMP_CLASS_NAME);
#endif
	if (IS_ERR(simtemp_class))
		return PTR_ERR(simtemp_class);

	ret = platform_driver_register(&simtemp_driver);
	if (ret != 0) {
		class_destroy(simtemp_class);
		simtemp_class = NULL;
		return ret;
	}

	if (force_create_dev != false) {
		simtemp_pdev = platform_device_register_simple(SIMTEMP_DRIVER_NAME,
								  -1, NULL, 0);
		if (IS_ERR(simtemp_pdev)) {
			ret = PTR_ERR(simtemp_pdev);
			pr_err("%s: failed to create temp platform_device: %d\n",
			       SIMTEMP_DRIVER_NAME, ret);
			platform_driver_unregister(&simtemp_driver);
			class_destroy(simtemp_class);
			simtemp_class = NULL;
			return ret;
		}
		pr_info("%s: temporary platform_device created (no DT)\n",
			SIMTEMP_DRIVER_NAME);
	}

	return 0;
}

static void __exit simtemp_exit(void)
{
	if ((simtemp_pdev != NULL) && !IS_ERR(simtemp_pdev)) {
		platform_device_unregister(simtemp_pdev);
		pr_info("%s: temporary platform_device removed\n",
			SIMTEMP_DRIVER_NAME);
		simtemp_pdev = NULL;
	}

	platform_driver_unregister(&simtemp_driver);
	ida_destroy(&simtemp_ida);

	if (simtemp_class != NULL) {
		class_destroy(simtemp_class);
		simtemp_class = NULL;
	}
}

module_init(simtemp_init);
module_exit(simtemp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rodrigo Rea");
MODULE_DESCRIPTION("NXP Simulated Temperature Sensor (skeleton with optional self-device)");
