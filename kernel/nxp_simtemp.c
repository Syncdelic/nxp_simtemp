// SPDX-License-Identifier: GPL-2.0
#include "nxp_simtemp.h"

#include <linux/compiler.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kstrtox.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

static const char * const simtemp_mode_names[] = {
	"normal",
	"noisy",
	"ramp",
};

#define SIMTEMP_TEMP_MIN_MC   20000
#define SIMTEMP_TEMP_MAX_MC   80000
#define SIMTEMP_TEMP_STEP_MC   800

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define simtemp_timer_shutdown(timer) timer_shutdown_sync(timer)
#else
#define simtemp_timer_shutdown(timer) del_timer_sync(timer)
#endif

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

static inline struct simtemp_device *simtemp_from_timer(struct timer_list *timer)
{
	return (struct simtemp_device *)((char *)timer - offsetof(struct simtemp_device, sample_timer));
}

static inline struct simtemp_device *simtemp_from_misc(struct miscdevice *misc)
{
	return (struct simtemp_device *)((char *)misc - offsetof(struct simtemp_device, miscdev));
}

static bool simtemp_buffer_has_data(const struct simtemp_device *sim)
{
	return READ_ONCE(sim->ring_count) > 0U;
}

static unsigned long simtemp_delay_jiffies(const struct simtemp_device *sim)
{
	unsigned int ms = READ_ONCE(sim->sampling_ms);
	unsigned long delay = msecs_to_jiffies(ms);
	return delay ? delay : 1UL;
}

static void simtemp_restart_timer(struct simtemp_device *sim)
{
	unsigned long delay;

	if (READ_ONCE(sim->stopping))
		return;

	delay = simtemp_delay_jiffies(sim);
	mod_timer(&sim->sample_timer, jiffies + delay);
}

static void simtemp_set_mode(struct simtemp_device *sim, enum simtemp_mode mode)
{
	WRITE_ONCE(sim->mode, mode);
	WRITE_ONCE(sim->ramp_increasing, true);
	if (mode == SIMTEMP_MODE_RAMP)
		WRITE_ONCE(sim->last_temp_mc, SIMTEMP_TEMP_MIN_MC);
}

static enum simtemp_mode simtemp_mode_from_string(const char *str)
{
	int i;

	for (i = 0; i < SIMTEMP_MODE_MAX; i++) {
		if (sysfs_streq(str, simtemp_mode_names[i]))
			return i;
	}

	return SIMTEMP_MODE_MAX;
}

static s32 simtemp_generate_temp(struct simtemp_device *sim)
{
	enum simtemp_mode mode = READ_ONCE(sim->mode);
	s32 temp = READ_ONCE(sim->last_temp_mc);

	switch (mode) {
	case SIMTEMP_MODE_NORMAL: {
		s32 delta = (s32)(get_random_u32() % (2 * SIMTEMP_TEMP_STEP_MC + 1)) -
			 SIMTEMP_TEMP_STEP_MC;
		temp += delta;
		break;
	}
	case SIMTEMP_MODE_NOISY: {
		s32 delta = (s32)(get_random_u32() % (6 * SIMTEMP_TEMP_STEP_MC + 1)) -
			 (3 * SIMTEMP_TEMP_STEP_MC);
		temp += delta;
		break;
	}
	case SIMTEMP_MODE_RAMP:
	default: {
		bool ramp_up = READ_ONCE(sim->ramp_increasing);

		if (ramp_up)
			temp += SIMTEMP_TEMP_STEP_MC;
		else
			temp -= SIMTEMP_TEMP_STEP_MC;
		if (temp >= SIMTEMP_TEMP_MAX_MC) {
			temp = SIMTEMP_TEMP_MAX_MC;
			ramp_up = false;
		} else if (temp <= SIMTEMP_TEMP_MIN_MC) {
			temp = SIMTEMP_TEMP_MIN_MC;
			ramp_up = true;
		}
		WRITE_ONCE(sim->ramp_increasing, ramp_up);
		break;
	}
	}

	temp = clamp_t(s32, temp, SIMTEMP_TEMP_MIN_MC, SIMTEMP_TEMP_MAX_MC);
	WRITE_ONCE(sim->last_temp_mc, temp);

	return temp;
}

static void simtemp_push_sample(struct simtemp_device *sim,
					const struct simtemp_sample *sample)
{
	unsigned long flags;

	spin_lock_irqsave(&sim->buf_lock, flags);
	if (sim->ring_count == SIMTEMP_RING_DEPTH) {
		const struct simtemp_sample *old = &sim->ring[sim->tail];
		if ((old->flags & SIMTEMP_SAMPLE_FLAG_THRESHOLD_ALERT) &&
		    sim->alert_count > 0U)
			sim->alert_count--;
		sim->tail = (sim->tail + 1U) % SIMTEMP_RING_DEPTH;
	} else {
		sim->ring_count++;
	}

	sim->ring[sim->head] = *sample;
	sim->head = (sim->head + 1U) % SIMTEMP_RING_DEPTH;

	sim->updates++;

	sim->pending_events |= SIMTEMP_EVENT_SAMPLE;
	if (sample->flags & SIMTEMP_SAMPLE_FLAG_THRESHOLD_ALERT) {
		sim->alert_count++;
		sim->alerts++;
		sim->pending_events |= SIMTEMP_EVENT_THRESHOLD;
	}

	spin_unlock_irqrestore(&sim->buf_lock, flags);
	wake_up_interruptible(&sim->waitq);
}

static void simtemp_timer_cb(struct timer_list *t)
{
	struct simtemp_device *sim = simtemp_from_timer(t);
	struct simtemp_sample sample = { 0 };
	s32 temp;

	temp = simtemp_generate_temp(sim);
	sample.timestamp_ns = ktime_get_real_ns();
	sample.temp_mc = temp;
	sample.flags = SIMTEMP_SAMPLE_FLAG_NEW_SAMPLE;
	if (temp >= READ_ONCE(sim->threshold_mc))
		sample.flags |= SIMTEMP_SAMPLE_FLAG_THRESHOLD_ALERT;

	simtemp_push_sample(sim, &sample);

	if (!READ_ONCE(sim->stopping))
		simtemp_restart_timer(sim);
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

	simtemp_restart_timer(sim);

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
static ssize_t mode_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sim;
	enum simtemp_mode mode;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	mutex_lock(&sim->lock);
	mode = sim->mode;
	if (mode >= SIMTEMP_MODE_MAX)
		mode = SIMTEMP_MODE_NORMAL;
	mutex_unlock(&sim->lock);

	return sysfs_emit(buf, "%s\n", simtemp_mode_names[mode]);
}

static ssize_t mode_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct simtemp_device *sim;
	enum simtemp_mode mode;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	mode = simtemp_mode_from_string(buf);
	if (mode >= SIMTEMP_MODE_MAX) {
		unsigned long flags;

		spin_lock_irqsave(&sim->buf_lock, flags);
		sim->errors++;
		spin_unlock_irqrestore(&sim->buf_lock, flags);
		dev_warn(sim->dev, "invalid mode request: %.*s\n", (int)count, buf);
		return -EINVAL;
	}

	mutex_lock(&sim->lock);
	simtemp_set_mode(sim, mode);
	mutex_unlock(&sim->lock);

	return count;
}
static DEVICE_ATTR_RW(mode);

static ssize_t stats_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct simtemp_device *sim;
	unsigned long flags;
	u32 updates, alerts, errors;

	sim = simtemp_from_classdev(dev);
	if (sim == NULL)
		return -ENODEV;

	spin_lock_irqsave(&sim->buf_lock, flags);
	updates = sim->updates;
	alerts = sim->alerts;
	errors = sim->errors;
	spin_unlock_irqrestore(&sim->buf_lock, flags);

	return sysfs_emit(buf, "updates=%u alerts=%u errors=%u\n",
			 updates, alerts, errors);
}
static DEVICE_ATTR_RO(stats);

static void simtemp_parse_dt(struct simtemp_device *sim)
{
	struct device *dev = sim->dev;
	struct device_node *np = dev->of_node;
	u32 val;
	const char *mode_str;
	enum simtemp_mode mode;

	if (!np)
		return;

	if (!of_property_read_u32(np, "sampling-ms", &val)) {
		u32 clamped = clamp_t(u32, val,
				    SIMTEMP_SAMPLING_MS_MIN, SIMTEMP_SAMPLING_MS_MAX);

		if (clamped != val)
			dev_warn(dev, "sampling-ms clamped to %u ms (was %u)\n",
				 clamped, val);
		sim->sampling_ms = clamped;
	}

	if (!of_property_read_u32(np, "threshold-mC", &val))
		sim->threshold_mc = (s32)val;

	if (!of_property_read_string(np, "mode", &mode_str)) {
		mode = simtemp_mode_from_string(mode_str);
		if (mode >= SIMTEMP_MODE_MAX) {
			unsigned long flags;

			spin_lock_irqsave(&sim->buf_lock, flags);
			sim->errors++;
			spin_unlock_irqrestore(&sim->buf_lock, flags);
			dev_warn(dev, "invalid mode '%s' in DT, defaulting to %s\n",
				 mode_str, simtemp_mode_names[SIMTEMP_DEFAULT_MODE]);
			simtemp_set_mode(sim, SIMTEMP_DEFAULT_MODE);
		} else {
			simtemp_set_mode(sim, mode);
		}
	}
}


static struct attribute *simtemp_attrs[] = {
	&dev_attr_sampling_ms.attr,
	&dev_attr_threshold_mC.attr,
	&dev_attr_mode.attr,
	&dev_attr_stats.attr,
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

static struct simtemp_device *simtemp_from_file(struct file *file)
{
	return file->private_data;
}

static int simtemp_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct simtemp_device *sim = simtemp_from_misc(misc);

	file->private_data = sim;

	return 0;
}

static ssize_t simtemp_read(struct file *file, char __user *buf, size_t count,
			    loff_t *ppos)
{
	struct simtemp_device *sim = simtemp_from_file(file);
	struct simtemp_sample sample;
	unsigned long flags;

	if (count < sizeof(sample))
		return -EINVAL;

	if (!(file->f_flags & O_NONBLOCK)) {
		int ret = wait_event_interruptible(sim->waitq,
						      sim->stopping || simtemp_buffer_has_data(sim));
		if (ret)
			return ret;
	} else if (!simtemp_buffer_has_data(sim)) {
		return -EAGAIN;
	}

	if (sim->stopping && !simtemp_buffer_has_data(sim))
		return 0;

	spin_lock_irqsave(&sim->buf_lock, flags);
	if (!sim->ring_count) {
		spin_unlock_irqrestore(&sim->buf_lock, flags);
		return sim->stopping ? 0 : -EAGAIN;
	}

	sample = sim->ring[sim->tail];
	sim->tail = (sim->tail + 1U) % SIMTEMP_RING_DEPTH;
	sim->ring_count--;
	if (sim->ring_count == 0U)
		sim->pending_events &= ~SIMTEMP_EVENT_SAMPLE;
	if ((sample.flags & SIMTEMP_SAMPLE_FLAG_THRESHOLD_ALERT) &&
	    sim->alert_count > 0U) {
		sim->alert_count--;
		if (sim->alert_count == 0U)
			sim->pending_events &= ~SIMTEMP_EVENT_THRESHOLD;
	}
	spin_unlock_irqrestore(&sim->buf_lock, flags);

	if (copy_to_user(buf, &sample, sizeof(sample))) {
		unsigned long err_flags;
		spin_lock_irqsave(&sim->buf_lock, err_flags);
		sim->errors++;
		spin_unlock_irqrestore(&sim->buf_lock, err_flags);
		return -EFAULT;
	}

	return sizeof(sample);
}

static __poll_t simtemp_poll(struct file *file, poll_table *wait)
{
	struct simtemp_device *sim = simtemp_from_file(file);
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(file, &sim->waitq, wait);

	spin_lock_irqsave(&sim->buf_lock, flags);
	if (sim->ring_count)
		mask |= POLLIN | POLLRDNORM;
	if (sim->pending_events & SIMTEMP_EVENT_THRESHOLD)
		mask |= POLLPRI;
	if (sim->stopping)
		mask |= POLLHUP;
	spin_unlock_irqrestore(&sim->buf_lock, flags);

	return mask;
}

static const struct file_operations simtemp_fops = {
	.owner	= THIS_MODULE,
	.open	= simtemp_open,
	.read	= simtemp_read,
	.poll	= simtemp_poll,
	.llseek = noop_llseek,
};

static int simtemp_probe(struct platform_device *pdev)
{
	struct simtemp_device *sim;
	int ret;

	sim = devm_kzalloc(&pdev->dev, sizeof(*sim), GFP_KERNEL);
	if (sim == NULL)
		return -ENOMEM;

	mutex_init(&sim->lock);
	spin_lock_init(&sim->buf_lock);
	init_waitqueue_head(&sim->waitq);
	timer_setup(&sim->sample_timer, simtemp_timer_cb, 0);

	sim->dev = &pdev->dev;
	sim->class_dev = NULL;
	sim->sampling_ms = SIMTEMP_DEFAULT_SAMPLING_MS;
	sim->threshold_mc = SIMTEMP_DEFAULT_THRESHOLD_MC;
	sim->head = 0U;
	sim->tail = 0U;
	sim->ring_count = 0U;
	sim->pending_events = 0U;
	sim->alert_count = 0U;
	sim->updates = 0U;
	sim->alerts = 0U;
	sim->errors = 0U;
	sim->stopping = false;
	sim->last_temp_mc = SIMTEMP_DEFAULT_THRESHOLD_MC;
	simtemp_set_mode(sim, SIMTEMP_DEFAULT_MODE);

	simtemp_parse_dt(sim);

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

	strscpy(sim->chardev_name, SIMTEMP_DRIVER_NAME, sizeof(sim->chardev_name));
	sim->miscdev.minor = MISC_DYNAMIC_MINOR;
	sim->miscdev.name = sim->chardev_name;
	sim->miscdev.fops = &simtemp_fops;
	sim->miscdev.parent = &pdev->dev;
	sim->miscdev.mode = 0660;

	ret = misc_register(&sim->miscdev);
	if (ret) {
		simtemp_sysfs_unregister(sim);
		ida_free(&simtemp_ida, sim->id);
		mutex_destroy(&sim->lock);
		return ret;
	}

	platform_set_drvdata(pdev, sim);

	simtemp_restart_timer(sim);

	dev_info(&pdev->dev,
		 "%s probed%s (sampling=%u ms threshold=%d mC)\n",
		 SIMTEMP_DRIVER_NAME,
		 (pdev->dev.of_node != NULL) ? " (DT match)" : " (name match)",
		 sim->sampling_ms, sim->threshold_mc);

	return 0;
}

static int simtemp_remove_int(struct platform_device *pdev)
{
	struct simtemp_device *sim;

	sim = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, NULL);

	if (sim != NULL) {
		WRITE_ONCE(sim->stopping, true);
		wake_up_interruptible(&sim->waitq);
		simtemp_timer_shutdown(&sim->sample_timer);
		misc_deregister(&sim->miscdev);
		simtemp_sysfs_unregister(sim);
		ida_free(&simtemp_ida, sim->id);
		mutex_destroy(&sim->lock);
	}

    dev_info(&pdev->dev, "%s remove\n", SIMTEMP_DRIVER_NAME);

	return 0;
}

static void simtemp_remove_void(struct platform_device *pdev)
{
	(void)simtemp_remove_int(pdev);
}

#ifdef __GENKSYMS__
#define SIMTEMP_REMOVE_INIT simtemp_remove_int
#else
#define SIMTEMP_REMOVE_INIT \
	(_Generic(((struct platform_driver *)0)->remove, \
		 int (*)(struct platform_device *): simtemp_remove_int, \
		 void (*)(struct platform_device *): simtemp_remove_void))
#endif

static const struct of_device_id simtemp_of_match[] = {
	{ .compatible = SIMTEMP_COMPATIBLE },
	{ }
};
MODULE_DEVICE_TABLE(of, simtemp_of_match);

static struct platform_driver simtemp_driver = {
	.probe = simtemp_probe,
	.remove = SIMTEMP_REMOVE_INIT,
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
MODULE_DESCRIPTION("NXP Simulated Temperature Sensor (data path skeleton)");
