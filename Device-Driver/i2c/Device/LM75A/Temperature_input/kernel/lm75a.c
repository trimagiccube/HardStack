/*
 * LM75A Device Driver
 *
 * (C) 2019.10.22 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/input.h>
#include <linux/platform_device.h>

/* lm75a on DTS
 *
 * arch/arm/boot/dts/bcm2711-rpi-4-b.dts
 *
 * &i2c1 {
 *        lm75a@48 {
 *               compatible = "BiscuitOS,lm75a";
 *               reg = <0x48>;
 *        };
 * };
 */

/* I2C Device Name */
#define DEV_NAME	"lm75a"
#define SLAVE_I2C_ADDR	0x48

#define TEMP_MAX	100
#define TEMP_MIN	0
#define INPUT_PERIOD	1000 /* 1000ms -> 1s */

#define LM75A_TEMP_REG		0x00
#define LM75A_CONF_REG		0x01
#define LM75A_THYST_REG		0x02
#define LM75A_TOS_REG		0x03

/* Mode with configure register */
#define LM75A_SHUT_DOWN		0x01
#define LM75A_NORMAL		0x00
#define LM75A_OS_INTR		0x02
#define LM75A_OS_COMP		0x00
#define LM75A_OS_ACTIVE_HIGH	0x04
#define LM75A_OS_ACTIVE_LOW	0x00

#define __unused		__attribute__((unused))

/* private data */
struct lm75a_pdata
{
	struct input_dev *input;
	struct timer_list timer;
	struct i2c_client *client;
	struct work_struct wq;
};

/* Configuration Register Read
 *
 * SDA LINE
 *
 *
 *  S                                     S
 *  T                                     T               R               S
 *  A                                     A               E               T
 *  R                                     R               A               O
 *  T                                     T               D               P
 * +-+-+ +-+ +-+-+-+ + +-+-+-+-+-+-+-+-+-+-+-+ +-+ +-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * | | | | | |     | | |*              | | | | | | |     | | |  ...  | | | |
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * +-+ +-+ +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ +-+ +-+-+-+-+ +-+-+-+-+-+-+ +-+
 *    M           L R A M             L A   M           L   A  Data n   N
 *    S           S / C S             S C   S           S   C  (8bits)  O
 *    B           B W K B             B K   B           B   K
 * |                                   |                                A
 * | <-------------------------------> |                                C
 *              DUMMY WRITE                                             K
 *    
 *
 * (* = DON't CARE bit for 1K)
 *    
 *
 * A random read requires a "dummy" byte write sequence to load in the
 * data word address. Once the device address word and data word address
 * are clocked in and acknowledged by the EEPROM, the microcontroller
 * must generate another start condition. The microcontroller now initiates
 * a current address read by sending a device address with the read/write
 * select bit high. The EEPROM acknowledges the device address and serially
 * clocks out the data word. The microcontroller does not respond with a
 * zero but does generate a following stop condition.
 */
static int __unused lm75a_read(struct i2c_client *client, 
				unsigned char offset, unsigned char *buf) 
{
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].addr	= client->addr;
	msgs[0].flags	= client->flags;
	msgs[0].len	= 1;
	msgs[0].buf	= &offset;

	msgs[1].addr	= client->addr;
	msgs[1].flags	= I2C_M_RD;
	msgs[1].len	= 1;
	msgs[1].buf	= buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (2 != ret)
		printk(KERN_ERR "Loss packet %d on Random Read\n", ret);
	return ret;
}

/* Temp or Tos or Thyst Register read
 *
 * SDA LINE
 *
 *
 *          R                           S
 *          E             A             T
 * DEVICE   A             C             O
 * ADDRESS  D             K             P
 * - - - - +-+ +-+-+-+-+-+ +-+-+-+-+-+-+-+
 *       | | | | | | | | | | | | | | | | |
 *         | | |  ...  | | |  ...  | | | |
 *       | | | | | | | | | | | | | | | | |
 * - - - - + +-+-+-+-+-+-+-+-+-+-+-+-+ +-+
 *          R A   MSB         LSB     N
 *          / C                       O
 *          W K
 *                                    A
 *                                    C
 *                                    K
 *
 *
 * Sequential reads are initated by either a current address read or a
 * random address read. After the microcontroller receives a data word,
 * it responds with an acknowledge. As long as the EEPROM receives an 
 * acknowledge, it will continue to increment the data word address and
 * serially clock out sequential data words. When the memory address limit
 * is reached, the data word address will "roll over" and sequential read
 * will continue. The sequential read operation is terminated when the 
 * microcontroller does not respond when a zero but does generate a
 * following stop condition.
 */
static int __unused lm75a_2bytes_read(struct i2c_client *client, 
				unsigned char offset, unsigned char *buf)
{
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].addr	= client->addr;
	msgs[0].flags	= client->flags & I2C_M_TEN;
	msgs[0].len	= 1;
	msgs[0].buf	= &offset;

	msgs[1].addr	= client->addr;
	msgs[1].flags	= I2C_M_RD;
	msgs[1].len	= 2;
	msgs[1].buf	= buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (2 != ret)
		printk(KERN_ERR "Loss packet %d on Sequen Read\n", ret);
	return ret;
}

/* Presetn Pointer Read
 * 
 *           S
 *           T               R                                   S
 *           A               E                                   T
 *           R               A                                   O
 *           T               D                                   P
 *          +-+-+ +-+ +-+-+-+-+ +-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+-+
 *          | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * SDA LINE | | | | | |     | | |             | |             | | |
 *          | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 *          +-+ +-+ +-+-+-+-+ +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ +-+
 *             M           L R A       MSB     A    LSB        N
 *             S           S / C               C               O
 *             B           B W K               K
 *                                                             A
 *                                                             C
 *                                                             K
 *
 * The internal data word address counter maintains the last address
 * accessed during the last read or write operation, incremented by one,
 * This address stays valid between operations as long as the chip power
 * is maintained. The address "roll over" during read is from the last
 * of the last memory page to the first byte of the first page. The
 * address "roll over" during write is from the last byte of the current
 * page to the first byte of the same page.
 *
 * Once the device address with the read/write select bit set to one is
 * clocked in and acknowledged by the EEPROM, the current address data
 * word is serially clocked out. The microcontroller does not respond
 * with an input zero but does generated a following stop condition.
 *
 */
static int __unused lm75a_present_read(struct i2c_client *client, 
							unsigned char *buf)
{
	struct i2c_msg msgs;
	int ret;

	msgs.addr	= client->addr;
	msgs.flags	= I2C_M_RD;
	msgs.len	= 2;
	msgs.buf	= buf;

	ret = i2c_transfer(client->adapter, &msgs, 1);
	if (1 != ret)
		printk("Loss packet %d on Current Address Read\n", ret);
	return ret;
}

/* Configuration Register Write
 *
 *
 *  S               W
 *  T               R                                       S
 *  A               I                                       T
 *  R  DEVICE       T                                       O
 *  T ADDRESS       E    WORD ADDRESS          DATA         P
 * +-+-+ +-+ +-+-+-+ + +-+-+-+-+-+-+-+-+ +-+-+-+-+-+-+-+-+ +-+
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * | | | | | |     | | |*              | |               | | |
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * +-+ +-+ +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    M           L R A   M           L A
 *    S           S / C   S           S C
 *    B           B W K   B           B K
 *
 *
 * A write operation requires an 8-bit data word address following the
 * device address and acknowledgment. Upon receipt of this address,
 * the EEPROM will again respond with a zero and then clock in the first
 * 8-bit data word. Following receipt of the 8-bit data word, the EEPROM
 * will output a zero and the addressing device, such as a microcontroller,
 * must terminate the write sequence with a stop condition. At this time
 * the EEPROM enters an internally timed write cycle, t(WR), to the 
 * nonvolatile memory. All inputs are disabled during this write cycle
 * and the EEPROM will not respond until the write is complete.
 *
 */
static int __unused lm75a_write(struct i2c_client *client, 
				unsigned char offset, unsigned char data)
{
	struct i2c_msg msgs;
	unsigned char tmp[2];
	int ret;

	tmp[0]		= offset;
	tmp[1]		= data;
	msgs.addr	= client->addr;
	msgs.flags	= client->flags;
	msgs.len	= 2;
	msgs.buf	= tmp;

	ret = i2c_transfer(client->adapter, &msgs, 1);
	if (1 != ret)
		printk("Loss packet %d on Byte write\n", ret);
	return ret;
}

/* Tos or Thyst Register Write
 * 
 * SDA LINE
 *
 *
 *
 *  S               W
 *  T               R                                         S
 *  A               I                                         T
 *  R  DEVICE       T                                         O
 *  T ADDRESS       E    WORD ADDRESS       MSB       LSB     P
 * +-+-+ +-+ +-+-+-+ + +-+-+-+-+-+-+-+-+ +-+-+-+-+ +-+-+-+-+ +-+
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * | | | | | |     | | |*              | |  ...  | |  ...  | | |
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * +-+ +-+ +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    M           L R A   M           L A         A         A
 *    S           S / C   S           S C         C         C
 *    B           B W K   B           B K         K         K
 *
 *
 * The 1K/2K EEPROM is capable of an 8-byte page write, and the 4K,
 * 8K and 16K devices are capable of 16-byte page writes.
 *
 * A page write is initiated the same as a byte write, but the
 * microcontroller does not send stop condition after the first data 
 * word is clocked in. Instead, after the EEPROM acknowledges receipt
 * of the first data word, the microcontroller can transmit up to seven
 * (1K/2K) or fifteen (4K,8K,16K) more data words. The EEPROM will
 * respond with a zero after each data word received. The microcontroller
 * must terminate the page write sequence with a stop condition.
 *
 * The data wrod address lower three (1K/2K) or four (4K,8K,16K) bits are
 * internally incremented following the receipt of each data word. The
 * higher data word address bits are not incremented, retaining the memory
 * page row location. When the word address, internally generated, reaches
 * the page boundary, the following byte is placed at the begining of the
 * same page. If more then eight (1K/2K) or sixteen (4K/8K/16K) data words
 * are transmitted to the EEPROM, the data word address will "roll over"
 * and previous data will overwritten. 
 *
 */
static int __unused lm75a_2bytes_write(struct i2c_client *client, 
				unsigned char offset, unsigned char *buf)
{
	struct i2c_msg msgs;
	unsigned char *tmp;
	int ret;

	/* kzalloc */
	tmp = kzalloc(3, GFP_KERNEL);
	if (!tmp) {
		printk("Unable to allocate memory\n");
		return -ENOMEM;
	}

	tmp[0] = offset;
	memcpy(&tmp[1], buf, 2);

	msgs.addr	= client->addr;
	msgs.flags	= client->flags;
	msgs.len	= 3;
	msgs.buf	= tmp;

	ret = i2c_transfer(client->adapter, &msgs, 1);
	if (1 != ret)
		printk("Loss packet %d on Page write\n", ret);
	kfree(tmp);
	return ret;
}

/* Cover 11bit data to a integer dataa */
static inline int lm75a_temperature(unsigned char msb, unsigned char lsb)
{
	return (msb << 3) | ((lsb >> 5) & 0x7);
}

/* work queue handler */
static void wq_isr(struct work_struct *work)
{
	struct lm75a_pdata *pdata;
	unsigned char buf[2];
	int temp;

	pdata = container_of(work, struct lm75a_pdata, wq);
	
	/* Read from LM75A */
	memset(buf, 0, 2);
	lm75a_2bytes_read(pdata->client, LM75A_TEMP_REG, buf);
	temp = lm75a_temperature(buf[0], buf[1]);

	/* report event */
	input_report_rel(pdata->input, REL_X, temp);
	input_sync(pdata->input);
}

/* Timer interrupt handler */
static void Timer_handler(struct timer_list *unused)
{
	struct lm75a_pdata *pdata;

	pdata = container_of(unused, struct lm75a_pdata, timer);
	
	/* To do low speed LM75A */
	schedule_work(&pdata->wq);

	/* Timer: Setup Timeout */
	pdata->timer.expires = jiffies + msecs_to_jiffies(INPUT_PERIOD);
	/* Timer: Register */
	add_timer(&pdata->timer);
}

/* input event open */
static int lm75a_open(struct input_dev *input)
{
	struct lm75a_pdata *pdata = input_get_drvdata(input);

	/* Timer */
	timer_setup(&pdata->timer, Timer_handler, 0);
	pdata->timer.expires = jiffies + msecs_to_jiffies(INPUT_PERIOD);
	add_timer(&pdata->timer);

	return 0;
}

/* input event close */
static void lm75a_close(struct input_dev *input)
{
	struct lm75a_pdata *pdata = input_get_drvdata(input);

	/* Timer */
	del_timer(&pdata->timer);
}

/* Probe: (LDD) Initialize Device */
static int lm75a_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct lm75a_pdata *pdata;
	struct input_dev *input;
	int ret;

	/* Build private data */
	pdata = (struct lm75a_pdata *)kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		printk("Error: System no free memory.\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Build input device */
	input = devm_input_allocate_device(&client->dev);
	if (!input) {
		printk("Error: allocate input device.\n");
		ret = -ENOMEM;
		goto err_input_dev;
	}

	/* Setup input information */
	input_set_drvdata(input, pdata);
	input->name		= DEV_NAME;
	input->open		= lm75a_open;
	input->close		= lm75a_close;
	input->id.bustype	= BUS_HOST;

	/* Setup event */
	input->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_REL);
	input_set_capability(input, EV_REL, REL_X);

	pdata->input = input;
	pdata->client = client;
	i2c_set_clientdata(client, pdata);

	ret = input_register_device(input);
	if (ret) {
		printk("ERROR: Register input device.\n");
		goto err_input_register;
	}

	/* build a work-queue to deal with low speed lm75a */
	INIT_WORK(&pdata->wq, wq_isr);

	/* LM75a mode setup and configuration */
	lm75a_write(client, LM75A_CONF_REG, LM75A_NORMAL | LM75A_OS_COMP);

	return 0;

err_input_register:
	input_free_device(input);
err_input_dev:
	kfree(pdata);
err_alloc:
	return ret;
}

/* Remove: (LDD) Remove Device (Module) */
static int lm75a_remove(struct i2c_client *client)
{
	struct lm75a_pdata *pdata = i2c_get_clientdata(client);

	/* release */
	input_unregister_device(pdata->input);
	input_free_device(pdata->input);
	kfree(pdata);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static struct of_device_id lm75a_match_table[] = {
	{ .compatible = "BiscuitOS,lm75a", },
	{ },
};

static const struct i2c_device_id lm75a_id[] = {
	{ DEV_NAME, SLAVE_I2C_ADDR },
	{ },
};

static struct i2c_driver lm75a_driver = {
	.driver = {
		.name = DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lm75a_match_table,
	},
	.probe	= lm75a_probe,
	.remove	= lm75a_remove,
	.id_table = lm75a_id,
};

module_i2c_driver(lm75a_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BiscuitOS <buddy.zhang@aliyun.com>");
MODULE_DESCRIPTION("LM75A Temperature Device Driver Module");
