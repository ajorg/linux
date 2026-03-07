// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * kd027_lcd.c - STARTEK KD027 LCD bit-bang SPI init driver
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct kd027_lcd {
	struct gpio_desc *rst_pin;
	struct gpio_desc *cs_pin;
	struct gpio_desc *sck_pin;
	struct gpio_desc *sda_pin;
	unsigned char init_seq[100];
	unsigned char suspend_seq[20];
	unsigned char resume_seq[20];
	int init_seq_len;
	int suspend_seq_len;
	int resume_seq_len;
	struct proc_dir_entry *proc;
};

struct kd027_lcd * lcd_data;

static void kd027_write_lcd(unsigned char data)
{
	unsigned char i;

	for(i = 0; i < 8; i++) {
		if (data & 0x80)
			gpiod_set_value(lcd_data->sda_pin, 1);
		else
			gpiod_set_value(lcd_data->sda_pin, 0);
		gpiod_set_value(lcd_data->sck_pin, 0);
		gpiod_set_value(lcd_data->sck_pin, 1);
		data <<= 1;
	}
}

static void kd027_write_cmd_data(unsigned char c, unsigned char d)
{
	gpiod_set_value(lcd_data->cs_pin, 0);
	kd027_write_lcd(c);
	kd027_write_lcd(d);
	gpiod_set_value(lcd_data->cs_pin, 1);
}

static void kd027_init(void)
{
	int i;
	for(i = 0; i < lcd_data->init_seq_len/2; i++) {
		kd027_write_cmd_data(lcd_data->init_seq[i * 2], lcd_data->init_seq[i * 2 + 1]);
	}
}

#ifdef CONFIG_PROC_FS
static ssize_t kd027_proc_read(struct file *file, char __user *buf, size_t size, loff_t *loff)
{
	return 0;
}

static ssize_t kd027_proc_write(struct file *file, const char __user *buf, size_t size, loff_t *loff)
{
	struct kd027_lcd *lcd = PDE_DATA(file_inode(file));
	char local_buffer[64];
	long cmd, data;
	char *tmp;

	if (size >= sizeof(local_buffer))
		size = sizeof(local_buffer) - 1;

	if (copy_from_user(local_buffer, buf, size))
		return -EFAULT;

	local_buffer[size] = '\0';
	if (kstrtol(local_buffer, 16, &cmd))
		return -EINVAL;
	tmp = strchr(local_buffer, ' ');
	if (tmp) {
		if (kstrtol(tmp + 1, 16, &data))
			return -EINVAL;
		gpiod_set_value(lcd->cs_pin, 0);
		kd027_write_lcd(cmd);
		kd027_write_lcd(data);
		gpiod_set_value(lcd->cs_pin, 1);
	}

	return size;
}

static const struct proc_ops kd027_proc_fops = {
	.proc_open	= simple_open,
	.proc_read	= kd027_proc_read,
	.proc_write	= kd027_proc_write,
	.proc_lseek	= noop_llseek,
};

static void kd027_proc_init(struct kd027_lcd *lcd)
{
	lcd->proc = proc_create_data("driver/lcd", 0644, NULL,
				     &kd027_proc_fops, lcd);
}
#else
static inline void kd027_proc_init(struct kd027_lcd *lcd) {}
#endif /* CONFIG_PROC_FS */

static int kd027_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;

	if ( !np) {
		dev_err(&pdev->dev,
			"failed to find platform data or device tree node.\n");
		return -ENODEV;
	}

	lcd_data = devm_kzalloc(&pdev->dev, sizeof(*lcd_data), GFP_KERNEL);
	if (lcd_data == NULL)
		return -ENOMEM;

	lcd_data->init_seq_len =  of_property_read_variable_u8_array(np, "init-seq", lcd_data->init_seq, 1, 100);
	lcd_data->suspend_seq_len =  of_property_read_variable_u8_array(np, "suspend-seq", lcd_data->suspend_seq, 1, 20);
	lcd_data->resume_seq_len =  of_property_read_variable_u8_array(np, "resume-seq", lcd_data->resume_seq, 1, 20);

	lcd_data->rst_pin = devm_gpiod_get(dev, "lcd-rst", GPIOD_OUT_HIGH);
	if (IS_ERR(lcd_data->rst_pin)) {
		ret = PTR_ERR(lcd_data->rst_pin);

		if (ret != -EPROBE_DEFER) {
			dev_err(dev,
				"Error: The gpios parameter is missing or invalid.\n");
		}
		return ret;
	}

	lcd_data->cs_pin = devm_gpiod_get(dev, "lcd-cs", GPIOD_OUT_HIGH);
	if (IS_ERR(lcd_data->cs_pin)) {
		ret = PTR_ERR(lcd_data->cs_pin);

		if (ret != -EPROBE_DEFER) {
			dev_err(dev,
				"Error: The gpios parameter is missing or invalid.\n");
		}
		return ret;
	}

	lcd_data->sck_pin = devm_gpiod_get(dev, "lcd-sck", GPIOD_OUT_HIGH);
	if (IS_ERR(lcd_data->sck_pin)) {
		ret = PTR_ERR(lcd_data->sck_pin);

		if (ret != -EPROBE_DEFER) {
			dev_err(dev,
				"Error: The gpios parameter is missing or invalid.\n");
		}
		return ret;
	}

	lcd_data->sda_pin = devm_gpiod_get(dev, "lcd-sda", GPIOD_OUT_HIGH);
	if (IS_ERR(lcd_data->sda_pin)) {
		ret = PTR_ERR(lcd_data->sda_pin);

		if (ret != -EPROBE_DEFER) {
			dev_err(dev,
				"Error: The gpios parameter is missing or invalid.\n");
		}
		return ret;
	}

	kd027_init();
	kd027_proc_init(lcd_data);
	platform_set_drvdata(pdev, lcd_data);

	return 0;
}

static int kd027_remove(struct platform_device *pdev)
{
	struct kd027_lcd *lcd = platform_get_drvdata(pdev);

	proc_remove(lcd->proc);
	return 0;
}

static int kd027_suspend(struct platform_device * pdev, pm_message_t state)
{
	int i;
	for(i = 0; i < lcd_data->suspend_seq_len/2; i++) {
		kd027_write_cmd_data(lcd_data->suspend_seq[i * 2], lcd_data->suspend_seq[i * 2 + 1]);
	}
	return 0;
}

static int kd027_resume(struct platform_device * pdev)
{
	int i;
	for(i = 0; i < lcd_data->resume_seq_len/2; i++) {
		kd027_write_cmd_data(lcd_data->resume_seq[i * 2], lcd_data->resume_seq[i * 2 + 1]);
	}
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id kd027_of_match[] = {
	{ .compatible = "kd027-lcd" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, kd027_of_match);
#endif

static struct platform_driver kd027_device_driver = {
	.probe		= kd027_probe,
	.remove		= kd027_remove,
	.suspend 		= kd027_suspend,
	.resume 		= kd027_resume,
	.driver		= {
		.name		= "kd027-lcd",
		.of_match_table = of_match_ptr(kd027_of_match),
	},
};

module_platform_driver(kd027_device_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KD027 Driver");

