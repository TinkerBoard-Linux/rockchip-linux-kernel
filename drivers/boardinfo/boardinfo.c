#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>

#include "tb3-setting.h"
#include "tb3n-setting.h"

static const char *model, *board;

static int projectid = -1, boardid = -1, ddrid = -1, emmcid = -1, odmid = -1;

static const struct of_device_id of_boardinfo_match[] = {
	{ .compatible = "boardinfo", },
	{ .compatible = "RK3568-ADC1-PCBID", },
	{ .compatible = "RK3568-ADC3-RAMID", },
	{ .compatible = "RK3568-ADC4-ODMID", },
	{ .compatible = "RK3568-ADC5-PRJID", },
	{ .compatible = "RK3566-ADC1-PCBID", },
	{ .compatible = "RK3566-ADC3-PRJID", },
	{},
};
MODULE_DEVICE_TABLE(of, of_boardinfo_match);

static int boardinfo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *compatible;
	int ret;

	if (device_property_read_string(dev, "compatible", &compatible)) {
		printk("[boardinfo] Failed to read compatible");
		return -ENODEV;
	}
	printk("boardinfo: initialized %s\n", compatible);

	if (strcmp(compatible, "boardinfo") == 0) {
		if (device_property_read_string(dev, "model", &model))
			model = "unknow";

		if (device_property_read_string(dev, "board", &board))
			board = "unknow";

		if (!strcmp(model, "rk3566"))
			ret = tb3_gpios(dev, &emmcid, &ddrid);
		else if (!strcmp(model, "rk3568"))
			ret = tb3n_gpios(dev, &emmcid);
		else
			ret = 0;
	} else {
		if (!strcmp(model, "rk3566"))
			ret = tb3_adcs(dev, compatible, board, &boardid, &projectid);
		else if (!strcmp(model, "rk3568"))
			ret = tb3n_adcs(dev, compatible, &boardid, &projectid, &ddrid, &odmid);
		else
			ret = 0;
	}

	if (ret < 0)
		return -EPROBE_DEFER;

	return 0;
}

int get_board_model(void)
{
	if (!strcmp(model, "rk3566"))
		return 3566;
	else if (!strcmp(model, "rk3568"))
		return 3568;
	else
		return -1;
}
EXPORT_SYMBOL_GPL(get_board_model);

int get_pcbid(void)
{
	return boardid;
}
EXPORT_SYMBOL_GPL(get_pcbid);

int get_prjid(void)
{
	return projectid;
}
EXPORT_SYMBOL_GPL(get_prjid);

int get_ramid(void)
{
	return ddrid;
}
EXPORT_SYMBOL_GPL(get_ramid);

int get_emmcid(void)
{
	return emmcid;
}
EXPORT_SYMBOL_GPL(get_emmcid);

int get_odmid(void)
{
	return odmid;
}
EXPORT_SYMBOL_GPL(get_odmid);

static int boardinfo_remove(struct platform_device *pdev)
{
	if (!strcmp(model, "rk3566"))
		tb3_gpios_free();
	else if (!strcmp(model, "rk3568"))
		tb3n_gpios_free();

	return 0;
}

static struct platform_driver boardinfo_driver = {
	.probe		= boardinfo_probe,
	.remove		= boardinfo_remove,
	.driver = {
		.name	= "boardinfo",
		.of_match_table = of_match_ptr(of_boardinfo_match),
	},
};

module_platform_driver(boardinfo_driver);

MODULE_ALIAS("platform:boardinfo");
MODULE_AUTHOR("Frank Chiang <frank_chiang@asus.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver to set Board Information");
