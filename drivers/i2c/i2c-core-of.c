// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux I2C core OF support code
 *
 * Copyright (C) 2008 Jochen Friedrich <jochen@scram.de>
 * based on a previous patch from Jon Smirl <jonsmirl@gmail.com>
 *
 * Copyright (C) 2013, 2018 Wolfram Sang <wsa@the-dreams.de>
 */

#include <dt-bindings/i2c/i2c.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>

#include "i2c-core.h"

// 使用struct device_node 提供的信息对i2c_board_info 实例化。
int of_i2c_get_board_info(struct device *dev, struct device_node *node,
			  struct i2c_board_info *info)
{
	u32 addr;
	int ret;

	memset(info, 0, sizeof(*info));

	if (of_modalias_node(node, info->type, sizeof(info->type)) < 0) {
		dev_err(dev, "of_i2c: modalias failure on %pOF\n", node);
		return -EINVAL;
	}
	// 读取node的"reg"的属性值到 addr，读取不到就推出。
	ret = of_property_read_u32(node, "reg", &addr);
	if (ret) {
		dev_err(dev, "of_i2c: invalid reg on %pOF\n", node);
		return ret;
	}

	// 如果标识是10bit add.
	if (addr & I2C_TEN_BIT_ADDRESS) {
		addr &= ~I2C_TEN_BIT_ADDRESS;
		info->flags |= I2C_CLIENT_TEN;
	}
	// 如果标识自己是从设备地址
	if (addr & I2C_OWN_SLAVE_ADDRESS) {
		addr &= ~I2C_OWN_SLAVE_ADDRESS;
		info->flags |= I2C_CLIENT_SLAVE;
	}

	info->addr = addr;
	info->of_node = node;
	// 查找属性"host-notify",想使用I2C主机notify.
	if (of_property_read_bool(node, "host-notify"))
		info->flags |= I2C_CLIENT_HOST_NOTIFY;
	// 查找属性"wakeup-source,若可以醒来.
	if (of_get_property(node, "wakeup-source", NULL))
		info->flags |= I2C_CLIENT_WAKE;

	return 0;
}
EXPORT_SYMBOL_GPL(of_i2c_get_board_info);

// 通过 device_node 实例化一个i2c设备，依附适配器adap后并注册到i2c总线。
static struct i2c_client *of_i2c_register_device(struct i2c_adapter *adap,
						 struct device_node *node)
{
	struct i2c_client *client;
	struct i2c_board_info info;
	int ret;

	dev_dbg(&adap->dev, "of_i2c: register %pOF\n", node);

	// 使用struct device_node 提供的信息对i2c_board_info 实例化。
	ret = of_i2c_get_board_info(&adap->dev, node, &info);
	if (ret)
		return ERR_PTR(ret);

	// 实例化一个i2c设备，依附适配器adap后并注册到i2c总线。
	client = i2c_new_device(adap, &info);
	if (!client) {
		dev_err(&adap->dev, "of_i2c: Failure registering %pOF\n", node);
		return ERR_PTR(-EINVAL);
	}
	return client;
}

// 创建本适配器上预先声明的未实例化的设备节点。实例化为i2c设备(i2c_client)并注册。
void of_i2c_register_devices(struct i2c_adapter *adap)
{
	struct device_node *bus, *node;
	struct i2c_client *client;

	/* Only register child devices if the adapter has a node pointer set */
	// 如果适配器有节点指针集，则只注册子设备
	if (!adap->dev.of_node)
		return;

	dev_dbg(&adap->dev, "of_i2c: walking child nodes\n");

	// 根据给定父节点的名称"i2c-bus"查找子节点,
	bus = of_get_child_by_name(adap->dev.of_node, "i2c-bus");
	if (!bus)
		bus = of_node_get(adap->dev.of_node);
	// 遍历获得其可用的（使能）的子节点。
	for_each_available_child_of_node(bus, node) {
		// 若设备已经创建，就continue
		if (of_node_test_and_set_flag(node, OF_POPULATED))
			continue;
		// 若设备设备未创建, 通过 device_node 实例化一个i2c设备，依附适配器adap后并注册到i2c总线。
		client = of_i2c_register_device(adap, node);
		if (IS_ERR(client)) {
			dev_err(&adap->dev,
				 "Failed to create I2C device for %pOF\n",
				 node);
			of_node_clear_flag(node, OF_POPULATED);
		}
	}

	of_node_put(bus);
}

static int of_dev_node_match(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

static int of_dev_or_parent_node_match(struct device *dev, const void *data)
{
	if (dev->of_node == data)
		return 1;

	if (dev->parent)
		return dev->parent->of_node == data;

	return 0;
}

/* must call put_device() when done with returned i2c_client device */
// 当使用返回的i2c_client设备时，必须调用put_device().
// 通过设备节点获得设备结构体。
struct i2c_client *of_find_i2c_device_by_node(struct device_node *node)
{
	struct device *dev;
	struct i2c_client *client;

	dev = bus_find_device(&i2c_bus_type, NULL, node, of_dev_node_match);
	if (!dev)
		return NULL;

	client = i2c_verify_client(dev);
	if (!client)
		put_device(dev);

	return client;
}
EXPORT_SYMBOL(of_find_i2c_device_by_node);

/* must call put_device() when done with returned i2c_adapter device */
// 当使用返回的i2c_adapter设备时，必须调用put_device()。
// 通过设备节点获得适配器结构体。
struct i2c_adapter *of_find_i2c_adapter_by_node(struct device_node *node)
{
	struct device *dev;
	struct i2c_adapter *adapter;

	dev = bus_find_device(&i2c_bus_type, NULL, node,
			      of_dev_or_parent_node_match);
	if (!dev)
		return NULL;

	adapter = i2c_verify_adapter(dev);
	if (!adapter)
		put_device(dev);

	return adapter;
}
EXPORT_SYMBOL(of_find_i2c_adapter_by_node);

/* must call i2c_put_adapter() when done with returned i2c_adapter device */
// 通过设备节点获得它的适配器
struct i2c_adapter *of_get_i2c_adapter_by_node(struct device_node *node)
{
	struct i2c_adapter *adapter;

	adapter = of_find_i2c_adapter_by_node(node);
	if (!adapter)
		return NULL;

	if (!try_module_get(adapter->owner)) {
		put_device(&adapter->dev);
		adapter = NULL;
	}

	return adapter;
}
EXPORT_SYMBOL(of_get_i2c_adapter_by_node);

static const struct of_device_id*
i2c_of_match_device_sysfs(const struct of_device_id *matches,
				  struct i2c_client *client)
{
	const char *name;

	for (; matches->compatible[0]; matches++) {
		/*
		 * Adding devices through the i2c sysfs interface provides us
		 * a string to match which may be compatible with the device
		 * tree compatible strings, however with no actual of_node the
		 * of_match_device() will not match
		 */
		// 通过i2c sysfs接口添加设备，为我们提供一个可能与设备树兼容的字符串进行匹配，
		// 但是如果没有实际的of_node, of_match_device()将不匹配
		if (sysfs_streq(client->name, matches->compatible))
			return matches;

		name = strchr(matches->compatible, ',');
		if (!name)
			name = matches->compatible;
		else
			name++;

		if (sysfs_streq(client->name, name))
			return matches;
	}

	return NULL;
}

//  检查i2c设备（ struct i2c_client） 是否在i2c设备树匹配列表（struct of_device_id ）中。
//  是返回of_device_id指针。否返回NULL.
const struct of_device_id
*i2c_of_match_device(const struct of_device_id *matches,
		     struct i2c_client *client)
{
	const struct of_device_id *match;

	if (!(client && matches))
		return NULL;

	match = of_match_device(matches, &client->dev); // ==> \drivers\of\device.c
	
	if (match)
		return match;

	return i2c_of_match_device_sysfs(matches, client);
}
EXPORT_SYMBOL_GPL(i2c_of_match_device);

#if IS_ENABLED(CONFIG_OF_DYNAMIC)
static int of_i2c_notify(struct notifier_block *nb, unsigned long action,
			 void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct i2c_adapter *adap;
	struct i2c_client *client;

	switch (of_reconfig_get_state_change(action, rd)) {
	case OF_RECONFIG_CHANGE_ADD:
		adap = of_find_i2c_adapter_by_node(rd->dn->parent);
		if (adap == NULL)
			return NOTIFY_OK;	/* not for us */

		if (of_node_test_and_set_flag(rd->dn, OF_POPULATED)) {
			put_device(&adap->dev);
			return NOTIFY_OK;
		}

		client = of_i2c_register_device(adap, rd->dn);
		put_device(&adap->dev);

		if (IS_ERR(client)) {
			dev_err(&adap->dev, "failed to create client for '%pOF'\n",
				 rd->dn);
			of_node_clear_flag(rd->dn, OF_POPULATED);
			return notifier_from_errno(PTR_ERR(client));
		}
		break;
	case OF_RECONFIG_CHANGE_REMOVE:
		/* already depopulated? */
		if (!of_node_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* find our device by node */
		client = of_find_i2c_device_by_node(rd->dn);
		if (client == NULL)
			return NOTIFY_OK;	/* no? not meant for us */

		/* unregister takes one ref away */
		i2c_unregister_device(client);

		/* and put the reference of the find */
		put_device(&client->dev);
		break;
	}

	return NOTIFY_OK;
}

struct notifier_block i2c_of_notifier = {
	.notifier_call = of_i2c_notify,
};
#endif /* CONFIG_OF_DYNAMIC */
