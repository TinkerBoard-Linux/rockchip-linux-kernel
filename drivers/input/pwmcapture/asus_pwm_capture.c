/* ASUS PWM Capture Mode Driver for Tinker Board 3. */
/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include "asus_pwm_capture.h"

static int latest_freq_hz ;
static int latest_high_point;
static int latest_low_point ;

static struct kobject *getpwm_kob;

static ssize_t get_pwm_freq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", latest_freq_hz);

}

static ssize_t reset_pwm_freq(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n)
{
	if (*(buf) == '0')
		latest_freq_hz = 0;
	return n;
}

static ssize_t get_pwm_high_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", latest_high_point);

}

static ssize_t get_pwm_low_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", latest_low_point);

}

static struct kobj_attribute pwm_freq_attribute = __ATTR(pwm_freq, 0644, get_pwm_freq_show, reset_pwm_freq);
static struct kobj_attribute get_pwm_high_attribute = __ATTR(get_pwm_high, 0444, get_pwm_high_show, NULL);
static struct kobj_attribute get_pwm_low_attribute = __ATTR(get_pwm_low, 0444, get_pwm_low_show, NULL);

static struct attribute *getpwm_attrs[] = {
	&pwm_freq_attribute.attr,
	&get_pwm_high_attribute.attr,
	&get_pwm_low_attribute.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = getpwm_attrs,
};

struct asus_capture_data {
	void __iomem *base;
	int state;
	int irq;
	int asus_pwm_id;
	int handle_cpu_id;
	unsigned long period;
	unsigned long temp_period;
	int pwm_freq_pstime;
	struct input_dev *input;
	struct timer_list timer;
	struct tasklet_struct capture_tasklet;
	struct wake_lock capture_wake_lock;
};

static void asus_pwm_capture_work(unsigned long  data)
{
	struct asus_capture_data *drvData;

	drvData = (struct asus_capture_data *)data;
	switch (drvData->state) {
	case RMC_IDLE: {
		;
		break;
	}
	case RMC_PRELOAD: {
		mod_timer(&drvData->timer, jiffies + msecs_to_jiffies(140));
		if ((drvData->period > RK_PWM_TIME_PRE_MIN) &&
		    (drvData->period < RK_PWM_TIME_PRE_MAX)) {
			drvData->state = RMC_USERCODE;
		} else {
			drvData->state = RMC_PRELOAD;
		}
		break;
	}
	default:
	break;
	}
}

static void asus_pwm_capture_timer(struct timer_list *t)
{
	struct asus_capture_data *drvData = from_timer(drvData, t, timer);

	drvData->state = RMC_PRELOAD;
}

static irqreturn_t asus_pwm_capture_irq(int irq, void *dev_id)
{
	struct asus_capture_data *drvData = dev_id;
	int val;
	int temp_hpr;
	int temp_lpr;
	unsigned int id = drvData->asus_pwm_id;

	if (id > 3)
		return IRQ_NONE;
	val = readl_relaxed(drvData->base + PWM_REG_INTSTS(id));

	if ((val & PWM_CH_INT(id)) == 0)
		return IRQ_NONE;
	if ((val & PWM_CH_POL(id)) == 0) {
		temp_hpr = readl_relaxed(drvData->base + PWM_REG_HPR);
		//pr_info("hpr=%d\n", temp_hpr);
		temp_lpr = readl_relaxed(drvData->base + PWM_REG_LPR);
		//pr_info("lpr=%d\n", temp_lpr);
		drvData->temp_period = drvData->pwm_freq_pstime * (temp_hpr + temp_lpr) / 1000;
		latest_high_point = temp_hpr;
		latest_low_point = temp_lpr;
		latest_freq_hz = 1000000000 / drvData->temp_period;
		//pr_info("freq=%d\n", latest_freq_hz);
	}
	writel_relaxed(PWM_CH_INT(id), drvData->base + PWM_REG_INTSTS(id));
	if (drvData->state == RMC_PRELOAD)
		wake_lock_timeout(&drvData->capture_wake_lock, HZ);
	return IRQ_HANDLED;
}

static int asus_pwm_capture_wakeup_init(struct platform_device *pdev)
{
	struct asus_capture_data *drvData = platform_get_drvdata(pdev);
	int val, min_temp, max_temp;
	unsigned int pwm_id = drvData->asus_pwm_id;
	int version;
	int ret = -1;

	version = readl_relaxed(drvData->base + RK_PWM_VERSION_ID(pwm_id));
	//dev_info(&pdev->dev, "pwm version is 0x%x\n", version & 0xffff0000);
	if (((version >> 24) & 0xFF) < 2) {
		dev_info(&pdev->dev, "pwm version is less v2.0\n");
		goto end;
	}

	val = readl_relaxed(drvData->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_DISABLE;
	writel_relaxed(val, drvData->base + PWM_REG_CTRL);

	//preloader low min:8000us, max:10000us
	min_temp = RK_PWM_TIME_PRE_MIN_LOW * 1 / drvData->pwm_freq_pstime;
	max_temp = RK_PWM_TIME_PRE_MAX_LOW * 1 / drvData->pwm_freq_pstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, drvData->base + PWM_REG_PWRMATCH_LPRE(pwm_id));

	//preloader higt min:4000us, max:5000us
	min_temp = RK_PWM_TIME_PRE_MIN * 1 / drvData->pwm_freq_pstime;
	max_temp = RK_PWM_TIME_PRE_MAX * 1 / drvData->pwm_freq_pstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, drvData->base + PWM_REG_PWRMATCH_HPRE(pwm_id));

	//logic 0/1 low min:480us, max 700us
	min_temp = RK_PWM_TIME_BIT_MIN_LOW * 1 / drvData->pwm_freq_pstime;
	max_temp = RK_PWM_TIME_BIT_MAX_LOW * 1 / drvData->pwm_freq_pstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, drvData->base + PWM_REG_PWRMATCH_LD(pwm_id));

	//logic 0 higt min:480us, max 700us
	min_temp = RK_PWM_TIME_BIT0_MIN * 1 / drvData->pwm_freq_pstime;
	max_temp = RK_PWM_TIME_BIT0_MAX * 1 / drvData->pwm_freq_pstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, drvData->base + PWM_REG_PWRMATCH_HD_ZERO(pwm_id));

	//logic 1 higt min:1300us, max 2000us
	min_temp = RK_PWM_TIME_BIT1_MIN * 1 / drvData->pwm_freq_pstime;
	max_temp = RK_PWM_TIME_BIT1_MAX * 1 / drvData->pwm_freq_pstime;
	val = (max_temp & 0xffff) << 16 | (min_temp & 0xffff);
	writel_relaxed(val, drvData->base + PWM_REG_PWRMATCH_HD_ONE(pwm_id));

	val = readl_relaxed(drvData->base + PWM_REG_INT_EN(pwm_id));
	val = (val & 0xFFFFFF7F) | PWM_PWR_INT_ENABLE;
	writel_relaxed(val, drvData->base + PWM_REG_INT_EN(pwm_id));

	val = CH3_PWRKEY_ENABLE;
	writel_relaxed(val, drvData->base + PWM_REG_PWRMATCH_CTRL(pwm_id));

	val = readl_relaxed(drvData->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel_relaxed(val, drvData->base + PWM_REG_CTRL);
end:
	return ret;
}

static void asus_pwm_capture_int(void __iomem *pwm_base, uint pwm_id, int ctrl)
{
	int val;

	if (pwm_id > 3)
		return;
	val = readl_relaxed(pwm_base + PWM_REG_INT_EN(pwm_id));
	if (ctrl) {
		val |= PWM_CH_INT_ENABLE(pwm_id);
		writel_relaxed(val, pwm_base + PWM_REG_INT_EN(pwm_id));
	} else {
		val &= ~PWM_CH_INT_ENABLE(pwm_id);
	}
	writel_relaxed(val, pwm_base + PWM_REG_INT_EN(pwm_id));
}

static int asus_pwm_capture_hw_init(void __iomem *pwm_base, uint pwm_id)
{
	int val;

	if (pwm_id > 3)
		return -1;
	//1. disabled pwm
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_DISABLE;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	//2. capture mode
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFF9) | PWM_MODE_CAPTURE;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	//set clk div, clk div to 64
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFF0001FF) | PWM_DIV1;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	//4. enabled pwm int
	asus_pwm_capture_int(pwm_base, pwm_id, PWM_INT_ENABLE);
	//5. enabled pwm
	val = readl_relaxed(pwm_base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel_relaxed(val, pwm_base + PWM_REG_CTRL);
	return 0;
}

static int asus_pwm_probe(struct platform_device *pdev)
{
	struct asus_capture_data *drvData;
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;
	struct input_dev *input;
	struct clk *clk;
	struct clk *p_clk;
	struct cpumask cpumask;
	int irq;
	int ret;
	int cpu_id;
	int pwm_id;
	int pwm_freq_to_ksec;
	int count;

	pr_info("asus rk3568 pwm capture mode init\n");
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "no memory resources defined\n");
		return -ENODEV;
	}
	drvData = devm_kzalloc(&pdev->dev, sizeof(struct asus_capture_data),
			     GFP_KERNEL);
	if (!drvData) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	drvData->state = RMC_PRELOAD;
	drvData->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(drvData->base))
		return PTR_ERR(drvData->base);
	count = of_property_count_strings(np, "clock-names");
	if (count == 2) {
		clk = devm_clk_get(&pdev->dev, "pwm");
		p_clk = devm_clk_get(&pdev->dev, "pclk");
	} else {
		clk = devm_clk_get(&pdev->dev, NULL);
		p_clk = clk;
	}
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get bus clk: %d\n", ret);
		return ret;
	}
	if (IS_ERR(p_clk)) {
		ret = PTR_ERR(p_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get periph clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable bus clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(p_clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable bus periph clk: %d\n", ret);
		goto error_clk;
	}
	platform_set_drvdata(pdev, drvData);

	input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		pr_err("failed to allocate input device\n");
		ret = -ENOMEM;
		goto error_pclk;
	}
	input->name = pdev->name;
	input->phys = "gpio-keys/pwmcapture";
	input->dev.parent = &pdev->dev;
	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x524b;
	input->id.product = 0x0006;
	input->id.version = 0x0100;
	drvData->input = input;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto error_pclk;
	}
	drvData->irq = irq;
	of_property_read_u32(np, "asus_pwm_id", &pwm_id);
	pwm_id %= 4;
	drvData->asus_pwm_id = pwm_id;
	if (pwm_id > 3) {
		dev_err(&pdev->dev, "pwm id error\n");
		goto error_pclk;
	}
	//pr_info("pwm_capture: pwm id=0x%x\n", pwm_id);
	of_property_read_u32(np, "handle_cpu_id", &cpu_id);
	drvData->handle_cpu_id = cpu_id;
	//pr_info("pwm_capture: handle cpu id=0x%x\n", cpu_id);

	tasklet_init(&drvData->capture_tasklet, asus_pwm_capture_work,
		     (unsigned long)drvData);

	ret = input_register_device(input);
	if (ret)
		dev_err(&pdev->dev, "register input device err, ret=%d\n", ret);
	input_set_capability(input, EV_KEY, KEY_WAKEUP);
	device_init_wakeup(&pdev->dev, 1);
	enable_irq_wake(irq);
	timer_setup(&drvData->timer, asus_pwm_capture_timer, 0);
	wake_lock_init(&drvData->capture_wake_lock,
		       WAKE_LOCK_SUSPEND, "asus_pwm_capture");
	cpumask_clear(&cpumask);
	cpumask_set_cpu(cpu_id, &cpumask);
	irq_set_affinity_hint(irq, &cpumask);
	ret = devm_request_irq(&pdev->dev, irq, asus_pwm_capture_irq,
			       IRQF_NO_SUSPEND, "asus_pwm_capture_irq", drvData);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", irq);
		goto error_irq;
	}

	pwm_freq_to_ksec = clk_get_rate(clk) / 1000;
	drvData->pwm_freq_pstime = 1000000000 / pwm_freq_to_ksec;
	//dev_info(&pdev->dev, "asus_pwm_freq_pstime=%d\n", drvData->pwm_freq_pstime);
	asus_pwm_capture_hw_init(drvData->base, pwm_id);

	ret = asus_pwm_capture_wakeup_init(pdev);
	if (!ret) {
		dev_err(&pdev->dev, "Controller support pwm capture\n");
		goto end;
	}

	getpwm_kob = kobject_create_and_add("pwmcapture_sysfs", kernel_kobj);
	if (!getpwm_kob)
		return -ENOMEM;

	ret = sysfs_create_group(getpwm_kob, &attr_group);
	if (ret)
		kobject_put(getpwm_kob);

	pr_info("asus rk3568 pwm capture mode init success!\n");
end:
	return 0;
error_irq:
	wake_lock_destroy(&drvData->capture_wake_lock);
error_pclk:
	clk_unprepare(p_clk);
error_clk:
	clk_unprepare(clk);
	return ret;
}

static int asus_pwm_remove(struct platform_device *pdev)
{
	kobject_put(getpwm_kob);
	return 0;
}
static const struct of_device_id asus_pwm_of_match[] = {
	{ .compatible =  "asus,pwm-capture"},
	{ }
};

MODULE_DEVICE_TABLE(of, asus_pwm_of_match);

static struct platform_driver asus_pwm_driver = {
	.driver = {
		.name = "asus-pwm-capture",
		.of_match_table = asus_pwm_of_match,
	},
	.remove = asus_pwm_remove,
};

module_platform_driver_probe(asus_pwm_driver, asus_pwm_probe);

MODULE_DESCRIPTION("ASUS PWM Capture Mode Driver for TinkerBoard 3");
MODULE_LICENSE("GPL");
