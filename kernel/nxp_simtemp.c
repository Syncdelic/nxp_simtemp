// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/init.h>

static bool force_create_dev;
module_param(force_create_dev, bool, 0444);
MODULE_PARM_DESC(force_create_dev, "Create a temporary platform_device on load (for x86 dev)");

static struct platform_device *simtemp_pdev;

/* --- Driver proper --- */
static int simtemp_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "nxp_simtemp skeleton probed%s\n",
	         pdev->dev.of_node ? " (DT match)" : " (name match)");
	return 0;
}

/* modern kernels expect void remove */
static void simtemp_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "nxp_simtemp remove\n");
}

static const struct of_device_id simtemp_of_match[] = {
	{ .compatible = "nxp,simtemp" },
	{ }
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static struct platform_driver simtemp_driver = {
	.probe  = simtemp_probe,
	.remove = simtemp_remove,
	.driver = {
		.name = "nxp_simtemp",
		.of_match_table = simtemp_of_match,
	},
};

/* --- Manual init/exit so we can also create a device when asked --- */
static int __init simtemp_init(void)
{
	int ret = platform_driver_register(&simtemp_driver);
	if (ret)
		return ret;

	if (force_create_dev) {
		/* Name-based bind to our driver (no DT on x86) */
		simtemp_pdev = platform_device_register_simple("nxp_simtemp", -1, NULL, 0);
		if (IS_ERR(simtemp_pdev)) {
			ret = PTR_ERR(simtemp_pdev);
			pr_err("nxp_simtemp: failed to create temp platform_device: %d\n", ret);
			platform_driver_unregister(&simtemp_driver);
			return ret;
		}
		pr_info("nxp_simtemp: temporary platform_device created (no DT)\n");
	}
	return 0;
}

static void __exit simtemp_exit(void)
{
	if (simtemp_pdev && !IS_ERR(simtemp_pdev)) {
		platform_device_unregister(simtemp_pdev);
		pr_info("nxp_simtemp: temporary platform_device removed\n");
	}
	platform_driver_unregister(&simtemp_driver);
}

module_init(simtemp_init);
module_exit(simtemp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rodrigo Rea");
MODULE_DESCRIPTION("NXP Simulated Temperature Sensor (skeleton with optional self-device)");
