/*
 * Driver for NXP PCF85263 real-time clock.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on the rtc-pcf85363 rtc driver by Eric Nelson.
 * Back-ported/written for kernel version 4.9.78-ti-r94 on 2023-12-18
 * Not tested on PCF85363.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/bcd.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

/*
 * Date/Time registers
 */
#define DT_100THS	0x00
#define DT_SECS		0x01
#define DT_MINUTES	0x02
#define DT_HOURS	0x03
#define DT_DAYS		0x04
#define DT_WEEKDAYS	0x05
#define DT_MONTHS	0x06
#define DT_YEARS	0x07

/*
 * Alarm registers
 */
#define DT_SECOND_ALM1	0x08
#define DT_MINUTE_ALM1	0x09
#define DT_HOUR_ALM1	0x0a
#define DT_DAY_ALM1	0x0b
#define DT_MONTH_ALM1	0x0c
#define DT_MINUTE_ALM2	0x0d
#define DT_HOUR_ALM2	0x0e
#define DT_WEEKDAY_ALM2	0x0f
#define DT_ALARM_EN	0x10

/*
 * Time stamp registers
 */
#define DT_TIMESTAMP1	0x11
#define DT_TIMESTAMP2	0x17
#define DT_TIMESTAMP3	0x1d
#define DT_TS_MODE	0x23

/*
 * control registers
 */
#define CTRL_OFFSET	0x24
#define CTRL_OSCILLATOR	0x25
#define CTRL_BATTERY	0x26
#define CTRL_PIN_IO	0x27
#define CTRL_FUNCTION	0x28
#define CTRL_INTA_EN	0x29
#define CTRL_INTB_EN	0x2a
#define CTRL_FLAGS	0x2b
#define CTRL_RAMBYTE	0x2c
#define CTRL_WDOG	0x2d
#define CTRL_STOP_EN	0x2e
#define CTRL_RESETS	0x2f
#define CTRL_RAM	0x40

#define ALRM_SEC_A1E	BIT(0)
#define ALRM_MIN_A1E	BIT(1)
#define ALRM_HR_A1E	BIT(2)
#define ALRM_DAY_A1E	BIT(3)
#define ALRM_MON_A1E	BIT(4)
#define ALRM_MIN_A2E	BIT(5)
#define ALRM_HR_A2E	BIT(6)
#define ALRM_DAY_A2E	BIT(7)

#define INT_WDIE	BIT(0)
#define INT_BSIE	BIT(1)
#define INT_TSRIE	BIT(2)
#define INT_A2IE	BIT(3)
#define INT_A1IE	BIT(4)
#define INT_OIE		BIT(5)
#define INT_PIE		BIT(6)
#define INT_ILP		BIT(7)

#define FLAGS_TSR1F	BIT(0)
#define FLAGS_TSR2F	BIT(1)
#define FLAGS_TSR3F	BIT(2)
#define FLAGS_BSF	BIT(3)
#define FLAGS_WDF	BIT(4)
#define FLAGS_A1F	BIT(5)
#define FLAGS_A2F	BIT(6)
#define FLAGS_PIF	BIT(7)

#define PIN_IO_INTAPM	GENMASK(1, 0)
#define PIN_IO_INTA_CLK	0
#define PIN_IO_INTA_BAT	1
#define PIN_IO_INTA_OUT	2
#define PIN_IO_INTA_HIZ	3

#define STOP_EN_STOP	BIT(0)

#define RESET_CPR	0xa4

#define NVRAM_SIZE	0x01

static struct i2c_driver pcf85263_driver;

struct pcf85263 {
	struct rtc_device	*rtc;
	struct regmap		*regmap;
};

static int pcf85263_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf85263 *pcf85263 = dev_get_drvdata(dev);
	unsigned char buf[DT_YEARS + 1];
	int ret, len = sizeof(buf);

	/* read the RTC date and time registers all at once */
	ret = regmap_bulk_read(pcf85263->regmap, DT_100THS, buf, len);
	if (ret) {
		dev_err(dev, "%s: error %d\n", __func__, ret);
		return ret;
	}

	tm->tm_year = bcd2bin(buf[DT_YEARS]);
	/* adjust for 1900 base of rtc_time */
	tm->tm_year += 100;

	tm->tm_wday = buf[DT_WEEKDAYS] & 7;
	buf[DT_SECS] &= 0x7F;
	tm->tm_sec = bcd2bin(buf[DT_SECS]);
	buf[DT_MINUTES] &= 0x7F;
	tm->tm_min = bcd2bin(buf[DT_MINUTES]);
	tm->tm_hour = bcd2bin(buf[DT_HOURS]);
	tm->tm_mday = bcd2bin(buf[DT_DAYS]);
	tm->tm_mon = bcd2bin(buf[DT_MONTHS]) - 1;

	return 0;
}

static int pcf85263_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf85263 *pcf85263 = dev_get_drvdata(dev);
	unsigned char tmp[11];
	unsigned char *buf = &tmp[2];
	int ret;

	tmp[0] = STOP_EN_STOP;
	tmp[1] = RESET_CPR;

	buf[DT_100THS] = 0;
	buf[DT_SECS] = bin2bcd(tm->tm_sec);
	buf[DT_MINUTES] = bin2bcd(tm->tm_min);
	buf[DT_HOURS] = bin2bcd(tm->tm_hour);
	buf[DT_DAYS] = bin2bcd(tm->tm_mday);
	buf[DT_WEEKDAYS] = tm->tm_wday;
	buf[DT_MONTHS] = bin2bcd(tm->tm_mon + 1);
	buf[DT_YEARS] = bin2bcd(tm->tm_year % 100);

	ret = regmap_bulk_write(pcf85263->regmap, CTRL_STOP_EN,
				tmp, 2);
	if (ret)
		return ret;

	ret = regmap_bulk_write(pcf85263->regmap, DT_100THS,
				buf, sizeof(tmp) - 2);
	if (ret)
		return ret;

	return regmap_write(pcf85263->regmap, CTRL_STOP_EN, 0);
}

static const struct rtc_class_ops rtc_ops = {
	.read_time	= pcf85263_rtc_read_time,
	.set_time	= pcf85263_rtc_set_time,
};

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x2f,
};

static int pcf85263_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct pcf85263 *pcf85263;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf85263 = devm_kzalloc(&client->dev, sizeof(struct pcf85263),
				GFP_KERNEL);
	if (!pcf85263)
		return -ENOMEM;

	pcf85263->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(pcf85263->regmap)) {
		dev_err(&client->dev, "regmap allocation failed\n");
		return PTR_ERR(pcf85263->regmap);
	}

	i2c_set_clientdata(client, pcf85263);

	pcf85263->rtc = devm_rtc_device_register(&client->dev,
			pcf85263_driver.driver.name,
			&rtc_ops,
			THIS_MODULE);

	return PTR_ERR_OR_ZERO(pcf85263->rtc);
}

static const struct i2c_device_id dev_ids[] = {
	{ "pcf85263", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, dev_ids);

static struct i2c_driver pcf85263_driver = {
	.driver	= {
		.name	= "pcf85263",
	},
	.probe		= pcf85263_probe,
	.id_table 	= dev_ids,
};

module_i2c_driver(pcf85263_driver);

MODULE_AUTHOR("Alan Morris");
MODULE_DESCRIPTION("pcf85263 I2C RTC driver");
MODULE_LICENSE("GPL");
