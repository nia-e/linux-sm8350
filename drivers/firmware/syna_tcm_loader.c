#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/rmi.h>
#include <linux/delay.h>

struct fw_block {
	unsigned char *data;
	unsigned int size;
	unsigned int addr;
};

struct tcm_firmware {
	struct fw_block boot_cfg;
	struct fw_block app_fw;
	struct fw_block app_cfg;
	struct fw_block disp_cfg;
};

enum tcm_mode {
	MODE_BL,
	MODE_APP,
};

static int rmi_is_tcm(struct rmi_device *rmi) {
	/* TODO */
	return 0;
}

static int syna_tcm_switch_mode(struct rmi_device *rmi, enum tcm_mode mode)
{
	unsigned char *response;
	unsigned int 
	int ret;

	mutex_lock(rmi->dev->mutex);
	/* TODO */
	mutex_unlock(rmi->dev->mutex);

}

static int syna_tcm_fw_load(struct rmi_device *rmi,
			    const struct tcm_firmware *fw)
{
	int ret;

	ret = syna_tcm_switch_mode(struct rmi_device *rmi, MODE_BL);
	if (!ret) {
		dev_err(dev, "Failed to switch into bootloader mode: %d\n", ret);
		return ret;
	}

	/* XXX: Write the 4 fw imgs */
}

static int syna_tcm_fw_init(struct rmi_device *rmi)
{
	struct tcm_firmware *fw;
	struct device *dev = rmi->dev;
	int ret;

	ret = rmi_is_tcm(rmi);
	if (ret) {
		dev_err(dev, "RMI device is not TCM; aborting fw load: %d\n", ret);
		return -ENODEV;
	}

	fw = devm_kzalloc(dev, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	ret = syna_tcm_fw_load(rmi, const struct tcm_firmware *fw)
}

static const struct of_device_id syna_tcm_fw_match {
	/* XXX: See if this is actually generic before upstreaming */
	{
		.compatible = "syna,tcm-fw-loader",
	},
	{},
};
