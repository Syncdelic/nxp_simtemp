/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

static int simtemp_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "nxp_simtemp skeleton probed\n");
	return 0;
}

static int simtemp_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "nxp_simtemp remove\n");
	return 0;
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
module_platform_driver(simtemp_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rodrigo Rea");
MODULE_DESCRIPTION("NXP Simulated Temperature Sensor (skeleton)");
