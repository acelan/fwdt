/*
 * FWDT driver
 *
 * Copyright(C) 2012 Canonical Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <acpi/acpi_drivers.h>
#include <linux/pci.h>

MODULE_AUTHOR("Alex Hung");
MODULE_DESCRIPTION("FWDT Driver");
MODULE_LICENSE("GPL");

static int __devinit fwdt_setup(struct platform_device *device);
static int __exit fwdt_remove(struct platform_device *device);

static struct platform_driver fwdt_driver = {
	.driver = {
		.name = "fwdt",
		.owner = THIS_MODULE,
	},
	.probe = fwdt_setup,
	.remove = fwdt_remove,
};

static struct platform_device *fwdt_platform_dev;

static u16 iob_addr;
static ssize_t iob_read_address(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "0x%04x\n", iob_addr);
}

static ssize_t iob_write_address(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	iob_addr = simple_strtoul(buf, NULL, 16) & 0xFFFF;
	
	return count;
}

static DEVICE_ATTR(iob_address, S_IRUGO | S_IWUSR, iob_read_address, iob_write_address);

static ssize_t iob_read_data(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "0x%02x\n", inb(iob_addr));
}

static ssize_t iob_write_data(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	u8 data;

	data = simple_strtoul(buf, NULL, 16);
	outb(data, iob_addr);
	
	return count;
}

static DEVICE_ATTR(iob_data, S_IRUGO | S_IWUSR, iob_read_data, iob_write_data);

static struct {
	u16 vendor_id;
	u16 device_id;
	u8 reg_offset;
} pci_dev_info;

static ssize_t pci_read_config_data(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct pci_dev *pdev = NULL;
	int data;

	pdev = pci_get_subsys(pci_dev_info.vendor_id, pci_dev_info.device_id,
				PCI_ANY_ID, PCI_ANY_ID, NULL);
	if (pdev == NULL) {
		pr_info("pci device [%x:%x] is not found\n", 
			pci_dev_info.vendor_id, pci_dev_info.device_id);
		return -EINVAL;
	}

	pci_read_config_dword(pdev, pci_dev_info.reg_offset, &data);

	return sprintf(buf, "0x%08x\n", data);;
}

static ssize_t pci_write_config_data(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct pci_dev *pdev = NULL;
	int data;

	data = simple_strtoul(buf, NULL, 16) & 0xFFFFFFFF;
	pdev = pci_get_subsys(pci_dev_info.vendor_id, pci_dev_info.device_id,
				PCI_ANY_ID, PCI_ANY_ID, NULL);
	pci_write_config_dword(pdev, pci_dev_info.reg_offset, data);
	
	return count;
}

static DEVICE_ATTR(pci_data, S_IRUGO | S_IWUSR, pci_read_config_data, pci_write_config_data);

static ssize_t pci_read_config_offset(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%x\n", pci_dev_info.reg_offset);;
}

static ssize_t pci_write_config_offset(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	pci_dev_info.reg_offset = simple_strtoul(buf, NULL, 16) & 0xFF;
	return count;
}

static DEVICE_ATTR(pci_reg, S_IRUGO | S_IWUSR, pci_read_config_offset, pci_write_config_offset);

static ssize_t pci_read_hardware_ids(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	u32 pci_id;

	pci_id= (pci_dev_info.vendor_id << 16) + (pci_dev_info.device_id);

	return sprintf(buf, "0x%08x\n", pci_id);;
}

static ssize_t pci_write_hardware_ids(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	long pci_id;

	pci_id = simple_strtoul(buf, NULL, 16);
	pci_dev_info.device_id = (pci_id & 0xFFFF0000) >> 16;
	pci_dev_info.vendor_id = (pci_id & 0x0000FFFF);

	return count;
}

static DEVICE_ATTR(pci_id, S_IRUGO | S_IWUSR, pci_read_hardware_ids, pci_write_hardware_ids);

static int ec_offset;
static ssize_t acpi_read_ec_data(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int ret;
	u8 data;

	ret = ec_read(ec_offset, &data);
	if (ret)
		return -EINVAL;

	return sprintf(buf, "%x\n", data);;
}

static ssize_t acpi_write_ec_data(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret;
	u8 data;

	data = simple_strtoul(buf, NULL, 16);
	ret = ec_write(ec_offset, data);
	if (ret)
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(ec_data, S_IRUGO | S_IWUSR, acpi_read_ec_data, acpi_write_ec_data);

static ssize_t acpi_read_ec_addr(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "0x%02x\n", ec_offset);;
}

static ssize_t acpi_write_ec_addr(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	ec_offset = simple_strtoul(buf, NULL, 16);
	return count;
}

static DEVICE_ATTR(ec_addr, S_IRUGO | S_IWUSR, acpi_read_ec_addr, acpi_write_ec_addr);

static void cleanup_sysfs(struct platform_device *device)
{
	 device_remove_file(&device->dev, &dev_attr_iob_address);
	 device_remove_file(&device->dev, &dev_attr_iob_data);
	 device_remove_file(&device->dev, &dev_attr_pci_id);
	 device_remove_file(&device->dev, &dev_attr_pci_reg);
	 device_remove_file(&device->dev, &dev_attr_pci_data);
	 device_remove_file(&device->dev, &dev_attr_ec_addr);
	 device_remove_file(&device->dev, &dev_attr_ec_data);
}

static int __devinit fwdt_setup(struct platform_device *device)
{
	int err;

	err = device_create_file(&device->dev, &dev_attr_iob_address);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_iob_data);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_pci_id);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_pci_reg);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_pci_data);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_ec_addr);
	if (err)
		goto add_sysfs_error;
	err = device_create_file(&device->dev, &dev_attr_ec_data);
	if (err)
		goto add_sysfs_error;

	return 0;

add_sysfs_error:
	cleanup_sysfs(device);
	return err;
}

static int __exit fwdt_remove(struct platform_device *device)
{
	cleanup_sysfs(device);
	return 0;
}

static int __init fwdt_init(void)
{
	int err;
	pr_info("initializing fwdt module\n");

	err = platform_driver_register(&fwdt_driver);
	if (err)
		goto err_driver_reg;
	fwdt_platform_dev = platform_device_alloc("fwdt", -1);
	if (!fwdt_platform_dev) {
		err = -ENOMEM;
		goto err_device_alloc;
	}
	err = platform_device_add(fwdt_platform_dev);
	if (err)
		goto err_device_add;

	return 0;

err_device_add:
	platform_device_put(fwdt_platform_dev);
err_device_alloc:
	platform_driver_unregister(&fwdt_driver);
err_driver_reg:

	return err;
}

static void __exit fwdt_exit(void)
{
	pr_info("exiting fwdt module\n");
	if (fwdt_platform_dev) {
		platform_device_unregister(fwdt_platform_dev);
		platform_driver_unregister(&fwdt_driver);
	} 
}

module_init(fwdt_init);
module_exit(fwdt_exit);
