// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/overflow.h>
#include <linux/swap.h>

#include "hw-vsc.h"
#include "spi-vsctp.h"

#define MEI_HW_START_POLL_DELAY_US (50 * USEC_PER_MSEC)
#define MEI_HW_START_POLL_TIMEOUT_US (200 * USEC_PER_MSEC)

static int mei_vsc_read_raw(struct mei_vsc_hw *hw, u8 *buf, u32 max_len, u32 *len)
{
	struct host_timestamp ts;

	ts.realtime = ktime_to_ns(ktime_get_real());
	ts.boottime = ktime_to_ns(ktime_get_boottime());

	return vsctp_xfer(hw->tp, VSCTP_CMD_READ, &ts, sizeof(ts), buf, max_len, len);
}

static int mei_vsc_write_raw(struct mei_vsc_hw *hw, u8 *buf, u32 len)
{
	u8 status;
	int rx_len;

	return vsctp_xfer(hw->tp, VSCTP_CMD_WRITE, buf, len, &status, sizeof(status), &rx_len);
}

/* %s is prod and sensor name, need to be get and format in runtime */
static const char *fw_name_template[3] = {
	"vsc/soc_a1%s/ivsc_fw_a1%s.bin",
	"vsc/soc_a1%s/ivsc_pkg_%s_0_a1%s.bin",
	"vsc/soc_a1%s/ivsc_skucfg_%s_0_1_a1%s.bin",
};

#define FW_NAME_SUFFIX "_prod"

enum { 
	IVSC_FW_INDEX,
	IVSC_PKG_INDEX,
	IVSC_SKUCFG_INDEX,
};

static int check_silicon(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_rom_master_frame *frame = (struct vsc_rom_master_frame *)hw->fw.tx_buf;
	struct vsc_rom_slave_token *token = (struct vsc_rom_slave_token *)hw->fw.rx_buf;
	int ret;
	u32 efuse1;
	u32 strap;
	char suffix[32] = "";

	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_DUMP_MEM;

	frame->data.dump_mem.addr = VSC_EFUSE1_ADDR;
	frame->data.dump_mem.len = sizeof(efuse1);

	ret = vsctp_rom_xfer(hw->tp, frame, token, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	if (token->token == VSC_TOKEN_ERROR) {
		dev_err(dev->dev, "dump efuse failed, token error %d\n", token->token);
		return ret;
	}

	memset(frame, 0, sizeof(*frame));
	memset(token, 0, sizeof(*token));
	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_RESERVED;
	ret = vsctp_rom_xfer(hw->tp, frame, token, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	if (token->token != VSC_TOKEN_DUMP_RESP) {
		dev_err(dev->dev, "reserved cmd token not valid %d\n", token->token);
		return -EIO;
	}

	efuse1 = *(u32 *)token->payload;

	/* to check the silicon main and sub version */
	hw->fw.main_ver = FIELD_GET(VSC_SI_MAINSTEPPING_VERSION_MASK, efuse1);
	if (hw->fw.main_ver != VSC_SI_MAINSTEPPING_VERSION_A) {
		dev_err(dev->dev, "silicon main version error(%d)\n", hw->fw.main_ver);
		return -EINVAL;
	}

	hw->fw.sub_ver = FIELD_GET(VSC_SI_SUBSTEPPING_VERSION_MASK, efuse1);
	if (hw->fw.sub_ver != VSC_SI_SUBSTEPPING_VERSION_0 &&
	    hw->fw.sub_ver != VSC_SI_SUBSTEPPING_VERSION_1) {
		dev_err(dev->dev, "silicon sub version error(%d)\n", hw->fw.sub_ver);
		return -EINVAL;
	}

	/* to get the silicon strap key: debug or production ? */
	memset(frame, 0, sizeof(*frame));
	memset(token, 0, sizeof(*token));
	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_DUMP_MEM;
	frame->data.dump_mem.addr = VSC_STRAP_ADDR;
	frame->data.dump_mem.len = sizeof(strap);

	ret = vsctp_rom_xfer(hw->tp, frame, token, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	if (token->token == VSC_TOKEN_ERROR) {
		dev_err(dev->dev, "get strap failed invalid token\n");
		return -EIO;
	}

	memset(frame, 0, sizeof(*frame));
	memset(token, 0, sizeof(*token));
	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_RESERVED;
	ret = vsctp_rom_xfer(hw->tp, frame, token, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	if (token->token != VSC_TOKEN_DUMP_RESP) {
		dev_err(dev->dev, "invalid token %d\n", token->token);
		return -EIO;
	}

	strap = *(u32 *)token->payload;
	/* to check the silicon strap key source */
	hw->fw.key_src = FIELD_GET(VSC_SI_STRAP_KEY_SRC_MASK, strap);

	dev_dbg(dev->dev, "silicon version check done: %s%s\n",
		hw->fw.sub_ver == VSC_SI_SUBSTEPPING_VERSION_0 ? "A0" : "A1",
		hw->fw.key_src == VSC_SI_STRAP_KEY_SRC_DEBUG ? "" : FW_NAME_SUFFIX);
	if (hw->fw.sub_ver != VSC_SI_SUBSTEPPING_VERSION_1)
		return -ENOTSUPP;

	if (hw->fw.key_src != VSC_SI_STRAP_KEY_SRC_DEBUG)
		strcpy(suffix, FW_NAME_SUFFIX);

	snprintf(hw->fw.fw_file_name, sizeof(hw->fw.fw_file_name), fw_name_template[IVSC_FW_INDEX],
		 suffix, suffix);
	snprintf(hw->fw.sensor_file_name, sizeof(hw->fw.sensor_file_name),
		 fw_name_template[IVSC_PKG_INDEX], suffix, hw->cam_sensor_name, suffix);
	snprintf(hw->fw.sku_cnf_file_name, sizeof(hw->fw.sku_cnf_file_name),
		 fw_name_template[IVSC_SKUCFG_INDEX], suffix, hw->cam_sensor_name, suffix);

	return 0;
}

static int parse_main_fw(struct mei_device *dev, const struct firmware *fw)
{
	struct vsc_boot_img *img = (struct vsc_boot_img *)fw->data;
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	struct vsc_img_frag *bootl_frag = &hw->fw.frags[VSC_BOOT_IMG_FRAG];
	struct vsc_img_frag *arcsem_frag = &hw->fw.frags[VSC_ARC_SEM_IMG_FRAG];
	struct vsc_img_frag *acer_frag = &hw->fw.frags[VSC_ACER_IMG_FRAG];
	struct vsc_img_frag *em7d_frag = &hw->fw.frags[VSC_EM7D_IMG_FRAG];

	struct vsc_fw_sign *vsc_fw[VSC_IMG_CNT_MAX];

	struct vsc_btl_sign *bootloader;
	struct vsc_fw_sign *arc_sem;
	struct vsc_fw_sign *em7d;
	struct vsc_fw_sign *ace_run = NULL;
	struct vsc_fw_manifest *man;
	int i;

	if (!img || img->magic != VSC_FILE_MAGIC) {
		dev_err(dev->dev, "image file error\n");
		return -EINVAL;
	}

	if (img->image_count != VSC_IMG_BOOT_ARC_ACER_EM7D) {
		dev_err(dev->dev, "image count error: image_count=0x%x\n", img->image_count);
		return -EINVAL;
	}

	/* only two lower bytes are used */
	hw->fw.fw_option = FIELD_GET(VSC_BOOT_IMG_OPTION_MASK, img->option);
	/* image not include bootloader */
	hw->fw.fw_cnt = img->image_count - 1;

	bootloader = (struct vsc_btl_sign *)(img->image_loc + img->image_count);
	if ((u8 *)bootloader > fw->data + fw->size)
		return -EINVAL;

	if (bootloader->magic != VSC_FW_MAGIC) {
		dev_err(dev->dev,
			"bootloader signed magic error! magic number 0x%08x, image size 0x%08x\n",
			bootloader->magic, bootloader->image_size);
		return -EINVAL;
	}

	man = (struct vsc_fw_manifest *)((char *)bootloader->image + bootloader->image_size -
					 VSC_SIG_SIZE - sizeof(struct vsc_fw_manifest) -
					 VSC_CSSHEADER_SIZE);
	if (man->svn == VSC_MAX_SVN_VALUE)
		hw->fw.svn = VSC_MAX_SVN_VALUE;
	else if (hw->fw.svn == 0)
		hw->fw.svn = man->svn;

	/* currently only support silicon version A0 | A1 */
	if ((hw->fw.sub_ver == VSC_SI_SUBSTEPPING_VERSION_0 && hw->fw.svn != VSC_MAX_SVN_VALUE) ||
	    (hw->fw.sub_ver == VSC_SI_SUBSTEPPING_VERSION_1 && hw->fw.svn == VSC_MAX_SVN_VALUE)) {
		dev_err(dev->dev, "silicon version and image svn not matched(A%s:0x%x)\n",
			hw->fw.sub_ver == VSC_SI_SUBSTEPPING_VERSION_0 ? "0" : "1", hw->fw.svn);
		return -EINVAL;
	}

	vsc_fw[0] = (struct vsc_fw_sign *)(bootloader->image + bootloader->image_size);
	for (i = 1; i < img->image_count - 1; i++) {
		vsc_fw[i] = (struct vsc_fw_sign *)(vsc_fw[i - 1]->image + vsc_fw[i - 1]->image_size);

		if ((u8 *)vsc_fw[i] > fw->data + fw->size)
			return -EINVAL;

		if (vsc_fw[i]->magic != VSC_FW_MAGIC) {
			dev_err(dev->dev,
				"FW (%d/%d) magic error! magic number 0x%08x, image size 0x%08x\n",
				i, img->image_count, vsc_fw[i]->magic, vsc_fw[i]->image_size);

			return -EINVAL;
		}
	}

	arc_sem = vsc_fw[0];
	ace_run = vsc_fw[1];
	em7d = vsc_fw[2];

	bootl_frag->data = bootloader->image;
	bootl_frag->size = bootloader->image_size;
	bootl_frag->location = img->image_loc[0];
	bootl_frag->type = VSC_IMG_BOOTLOADER;
	if (!bootl_frag->location)
		return -EINVAL;

	arcsem_frag->data = arc_sem->image;
	arcsem_frag->size = arc_sem->image_size;
	arcsem_frag->location = img->image_loc[1];
	arcsem_frag->type = VSC_IMG_ARCSEM;
	if (!arcsem_frag->location)
		return -EINVAL;

	acer_frag->data = ace_run->image;
	acer_frag->size = ace_run->image_size;
	acer_frag->location = img->image_loc[2];
	acer_frag->type = VSC_IMG_ACE_RUNTIME;
	if (!acer_frag->location)
		return -EINVAL;

	em7d_frag->data = em7d->image;
	em7d_frag->size = em7d->image_size;
	/* em7d is the last firmware */
	em7d_frag->location = img->image_loc[3];
	em7d_frag->type = VSC_IMG_EM7D;
	if (!em7d_frag->location)
		return -EINVAL;

	return 0;
}

static int parse_sensor_fw(struct mei_device *dev, const struct firmware *fw)
{
	struct vsc_boot_img *img = (struct vsc_boot_img *)fw->data;
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_img_frag *acer_frag = &hw->fw.frags[VSC_ACER_IMG_FRAG];
	struct vsc_img_frag *acev_frag = &hw->fw.frags[VSC_ACEV_IMG_FRAG];
	struct vsc_img_frag *acec_frag = &hw->fw.frags[VSC_ACEC_IMG_FRAG];
	struct vsc_fw_sign *ace_vis;
	struct vsc_fw_sign *ace_conf;

	if (!img || img->magic != VSC_FILE_MAGIC || img->image_count < VSC_IMG_ACEV_ACECNF ||
	    img->image_count > VSC_IMG_CNT_MAX)
		return -EINVAL;

	hw->fw.fw_cnt += img->image_count;
	if (hw->fw.fw_cnt > VSC_IMG_CNT_MAX)
		return -EINVAL;

	ace_vis = (struct vsc_fw_sign *)(img->image_loc + img->image_count);
	ace_conf = (struct vsc_fw_sign *)(ace_vis->image + ace_vis->image_size);

	if (ace_vis->magic != VSC_FW_MAGIC) {
		dev_err(dev->dev,
			"ACE vision signed magic error! magic number 0x%08x, image size 0x%08x\n",
			ace_vis->magic, ace_vis->image_size);
		return -EINVAL;
	}

	acev_frag->data = ace_vis->image;
	acev_frag->size = ace_vis->image_size;
	acev_frag->location = ALIGN(acer_frag->location + acer_frag->size, SZ_4K);
	acev_frag->type = VSC_IMG_ACE_VISION;
	if (img->image_loc[0] && acev_frag->location != img->image_loc[0]) {
		dev_err(dev->dev,
			"ACE vision image location error. img->image_loc[0]=0x%x, calculated is 0x%x\n",
			img->image_loc[0], acev_frag->location);
		/* when location mismatch, use the one from image file. */
		acev_frag->location = img->image_loc[0];
	}

	if (ace_conf->magic != VSC_FW_MAGIC) {
		dev_err(dev->dev,
			"ACE config signed magic error! magic number 0x%08x, image size 0x%08x\n",
			ace_conf->magic, ace_conf->image_size);
		return -EINVAL;
	}

	acec_frag->data = ace_conf->image;
	acec_frag->size = ace_conf->image_size;
	acec_frag->location = ALIGN(acev_frag->location + acev_frag->size, SZ_4K);
	acec_frag->type = VSC_IMG_ACE_CONFIG;
	if (img->image_loc[1] && acec_frag->location != img->image_loc[1]) {
		dev_err(dev->dev,
			"ACE vision image location error. img->image_loc[1]=0x%x, calculated is 0x%x\n",
			img->image_loc[1], acec_frag->location);
		/* when location mismatch, use the one from image file. */
		acec_frag->location = img->image_loc[1];
	}

	return 0;
}

static int parse_sku_cnf_fw(struct mei_device *dev, const struct firmware *fw)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_img_frag *skucnf_frag = &hw->fw.frags[VSC_SKU_CONF_FRAG];

	if (fw->size <= sizeof(u32))
		return -EINVAL;

	skucnf_frag->data = fw->data;
	skucnf_frag->size = *((u32 *)fw->data) + sizeof(u32);
	skucnf_frag->type = VSC_IMG_SKU_CONFIG;
	/* SKU config use fixed location */
	skucnf_frag->location = VSC_SKU_CONFIG_LOC;
	if (fw->size != skucnf_frag->size || fw->size > VSC_SKU_MAX_SIZE) {
		dev_err(dev->dev,
			"sku config file size is not config size + 4, config size=0x%x, file size=0x%zx\n",
			skucnf_frag->size, fw->size);
		return -EINVAL;
	}

	return 0;
}

static u32 sum_crc(void *data, int size)
{
	int i;
	u32 crc = 0;

	for (i = 0; i < size; i++)
		crc += *((u8 *)data + i);

	return crc;
}

static int load_boot(struct mei_device *dev, const void *data, int size)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_rom_master_frame *frame = (struct vsc_rom_master_frame *)hw->fw.tx_buf;
	const char *ptr = data;
	u32 remain = size;
	int ret;
	u16 len;

	if (!data || !size)
		return -EINVAL;

	while (remain > 0) {
		len = min_t(u16, remain, sizeof(frame->data.dl_cont.payload));
		memset(frame, 0, sizeof(*frame));
		frame->magic = VSC_MAGIC_NUM;
		frame->cmd = VSC_CMD_DL_CONT;

		frame->data.dl_cont.len = len;
		frame->data.dl_cont.end_flag = (remain == len);
		memcpy(frame->data.dl_cont.payload, ptr, len);

		ret = vsctp_rom_xfer(hw->tp, frame, NULL, VSC_ROM_PKG_SIZE);
		if (ret)
			return ret;

		ptr += len;
		remain -= len;
	}

	return 0;
}

static int load_bootloader(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_rom_master_frame *frame = (struct vsc_rom_master_frame *)hw->fw.tx_buf;
	struct vsc_rom_slave_token *token = (struct vsc_rom_slave_token *)hw->fw.rx_buf;
	struct vsc_img_frag *frag = &hw->fw.frags[VSC_BOOT_IMG_FRAG];
	int ret;

	if (!frag->size)
		return -EINVAL;

	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_QUERY;
	ret = vsctp_rom_xfer(hw->tp, frame, token, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	if (token->token != VSC_TOKEN_BOOTLOADER_REQ && token->token != VSC_TOKEN_DUMP_RESP) {
		dev_err(dev->dev, "failed to load bootloader, invalid token 0x%x\n", token->token);
		return -EINVAL;
	}

	memset(frame, 0, sizeof(*frame));

	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_DL_START;
	frame->data.dl_start.img_type = frag->type;
	frame->data.dl_start.img_len = frag->size;
	frame->data.dl_start.img_loc = frag->location;
	frame->data.dl_start.option = hw->fw.fw_option;
	frame->data.dl_start.crc =
		sum_crc(frame, offsetof(struct vsc_rom_master_frame, data.dl_start.crc));
	ret = vsctp_rom_xfer(hw->tp, frame, NULL, VSC_ROM_PKG_SIZE);
	if (ret)
		return ret;

	ret = load_boot(dev, frag->data, frag->size);
	if (ret)
		dev_err(dev->dev, "failed to load bootloader, err : 0x%0x\n", ret);

	return ret;
}

static int load_fw_bin(struct mei_device *dev, const void *data, int size)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_master_frame_fw_cont *frame = (struct vsc_master_frame_fw_cont *)hw->fw.tx_buf;
	const u8 *ptr = data;
	int ret;
	u32 remain = size;
	u32 len;

	if (!data || !size)
		return -EINVAL;

	while (remain > 0) {
		len = min_t(u32, remain, VSC_FW_PKG_SIZE);
		memset(frame, 0, sizeof(*frame));
		memcpy(frame->payload, ptr, len);

		ret = vsctp_rom_xfer(hw->tp, frame, NULL, VSC_FW_PKG_SIZE);
		if (ret) {
			dev_err(dev->dev, "transfer failed\n");
			break;
		}

		ptr += len;
		remain -= len;
	}

	return ret;
}

static int load_fw_frag(struct mei_device *dev, struct vsc_img_frag *frag, int type)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_fw_master_frame *frame = (struct vsc_fw_master_frame *)hw->fw.tx_buf;
	int ret;

	memset(frame, 0, sizeof(*frame));
	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_DL_START;
	frame->data.dl_start.img_type = type;
	frame->data.dl_start.img_len = frag->size;
	frame->data.dl_start.img_loc = frag->location;
	frame->data.dl_start.option = hw->fw.fw_option;
	frame->data.dl_start.crc =
		sum_crc(frame, offsetof(struct vsc_fw_master_frame, data.dl_start.crc));
	ret = vsctp_rom_xfer(hw->tp, frame, NULL, VSC_FW_PKG_SIZE);
	if (ret)
		return ret;

	return load_fw_bin(dev, frag->data, frag->size);
}

static int load_fw(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct vsc_fw_master_frame *frame = (struct vsc_fw_master_frame *)hw->fw.tx_buf;
	struct vsc_img_frag *frag;
	int index = 0;
	int ret;
	int i;

	/* send dl_set frame */
	memset(frame, 0, sizeof(*frame));

	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_CMD_DL_SET;
	frame->data.dl_set.option = hw->fw.fw_option;
	frame->data.dl_set.img_cnt = hw->fw.fw_cnt;

	for (i = 1; i <= VSC_EM7D_IMG_FRAG; i++) {
		frag = &hw->fw.frags[i];
		if (!frag->size)
			continue;

		frame->data.dl_set.payload[index++] = frag->location;
		frame->data.dl_set.payload[index++] = frag->size;
	}

	frame->data.dl_set.payload[hw->fw.fw_cnt * 2] =
		sum_crc(frame, offsetof(struct vsc_fw_master_frame,
					     data.dl_set.payload[hw->fw.fw_cnt * 2]));

	ret = vsctp_rom_xfer(hw->tp, frame, NULL, VSC_FW_PKG_SIZE);
	if (ret)
		return ret;

	for (i = 1; i < VSC_FRAG_MAX; i++) {
		frag = &hw->fw.frags[i];
		if (!frag->size)
			return -EINVAL;

		ret = load_fw_frag(dev, frag, frag->type);
	}

	memset(frame, 0, sizeof(*frame));
	frame->magic = VSC_MAGIC_NUM;
	frame->cmd = VSC_TOKEN_CAM_BOOT;
	frame->data.boot.check_sum =
		sum_crc(frame, offsetof(struct vsc_fw_master_frame, data.dl_start.crc));

	return vsctp_rom_xfer(hw->tp, frame, NULL, VSC_FW_PKG_SIZE);
}

static int init_hw(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	const struct firmware *main_fw;
	const struct firmware *sensor_fw;
	const struct firmware *sku_cnf_fw;
	int ret;

	ret = check_silicon(dev);
	if (ret)
		return ret;

	ret = request_firmware(&main_fw, hw->fw.fw_file_name, dev->dev);
	if (ret) {
		dev_err(dev->dev, "file not found %s\n", hw->fw.fw_file_name);
		return ret;
	}

	ret = parse_main_fw(dev, main_fw);
	if (ret) {
		dev_err(dev->dev, "parse fw %s failed\n", hw->fw.fw_file_name);
		goto release_main;
	}

	ret = request_firmware(&sensor_fw, hw->fw.sensor_file_name, dev->dev);
	if (ret) {
		dev_err(dev->dev, "file not found %s\n", hw->fw.sensor_file_name);
		goto release_main;
	}

	ret = parse_sensor_fw(dev, sensor_fw);
	if (ret) {
		dev_err(dev->dev, "parse fw %s failed\n", hw->fw.sensor_file_name);
		goto release_sensor;
	}

	ret = request_firmware(&sku_cnf_fw, hw->fw.sku_cnf_file_name, dev->dev);
	if (ret) {
		dev_err(dev->dev, "file not found %s\n", hw->fw.sku_cnf_file_name);
		goto release_sensor;
	}

	ret = parse_sku_cnf_fw(dev, sku_cnf_fw);
	if (ret) {
		dev_err(dev->dev, "parse fw %s failed\n", hw->fw.sensor_file_name);
		goto release_cnf;
	}

	ret = load_bootloader(dev);
	if (ret)
		goto release_cnf;

	ret = load_fw(dev);
	if (ret)
		goto release_cnf;

	return 0;

release_cnf:
	release_firmware(sku_cnf_fw);
release_sensor:
	release_firmware(sensor_fw);
release_main:
	release_firmware(main_fw);

	return ret;
}

/**
 * mei_vsc_fw_status - read vsc fw status
 *
 * @dev: mei device
 * @fw_status: fw status
 *
 * Return: 0 on success, error otherwise
 */
static int mei_vsc_fw_status(struct mei_device *dev, struct mei_fw_status *fw_status)
{
	if (!fw_status)
		return -EINVAL;

	fw_status->count = 0;

	return 0;
}

/**
 * mei_vsc_pg_state  - translate internal pg state
 *   to the mei power gating state
 *
 * @dev:  mei device
 *
 * Return: MEI_PG_OFF
 */
static inline enum mei_pg_state mei_vsc_pg_state(struct mei_device *dev)
{
	return MEI_PG_OFF;
}

/**
 * mei_vsc_intr_enable - enables mei device interrupts
 *
 * @dev: the device structure
 */
static void mei_vsc_intr_enable(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	vsctp_intr_enable(hw->tp);
}

/**
 * mei_vsc_intr_disable - disables mei device interrupts
 *
 * @dev: the device structure
 */
static void mei_vsc_intr_disable(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	vsctp_intr_disable(hw->tp);
}

/**
 * mei_vsc_intr_clear - clear and stop interrupts
 *
 * @dev: the device structure
 */
static void mei_vsc_intr_clear(struct mei_device *dev)
{
}

/**
 * mei_vsc_synchronize_irq - wait for pending IRQ handlers
 *
 * @dev: the device structure
 */
static void mei_vsc_synchronize_irq(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	vsctp_intr_synchronize(hw->tp);
}

/**
 * mei_vsc_hw_config - configure hw dependent settings
 *
 * @dev: mei device
 *
 * Return:
 *  0 on success, <0 otherwise
 */
static int mei_vsc_hw_config(struct mei_device *dev)
{
	return 0;
}

/**
 * mei_vsc_host_set_ready - enable device
 *
 * @dev: mei device
 */
static void mei_vsc_host_set_ready(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	hw->host_ready = true;
}

/**
 * mei_vsc_host_is_ready - check whether the host has turned ready
 *
 * @dev: mei device
 * Return: bool
 */
static bool mei_vsc_host_is_ready(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	return hw->host_ready;
}

/**
 * mei_vsc_hw_is_ready - check whether the me(hw) has turned ready
 *
 * @dev: mei device
 * Return: bool
 */
static bool mei_vsc_hw_is_ready(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	return hw->fw_ready;
}

/**
 * mei_vsc_hw_start - hw start routine
 *
 * @dev: mei device
 * Return: 0 on success, error otherwise
 */
static int mei_vsc_hw_start(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	u8 buf;
	int len;
	int ret;

	mei_vsc_host_set_ready(dev);
	mei_vsc_intr_enable(dev);

	ret = read_poll_timeout(mei_vsc_read_raw, ret, !ret, MEI_HW_START_POLL_DELAY_US,
				MEI_HW_START_POLL_TIMEOUT_US, true, hw, &buf, sizeof(buf), &len);
	if (ret) {
		dev_err(dev->dev, "wait fw ready failed ret %d\n", ret);
		return ret;
	}

	hw->fw_ready = true;

	return 0;
}

/**
 * mei_vsc_hbuf_is_ready - checks if host buf is empty.
 *
 * @dev: the device structure
 *
 * Return: true if empty, false - otherwise.
 */
static bool mei_vsc_hbuf_is_ready(struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);

	return atomic_read(&hw->write_lock_cnt) == 0;
}

/**
 * mei_vsc_hbuf_empty_slots - counts write empty slots.
 *
 * @dev: the device structure
 *
 * Return:  empty slots count
 */
static int mei_vsc_hbuf_empty_slots(struct mei_device *dev)
{
	return VSC_MEI_MAX_MSG_SIZE / MEI_SLOT_SIZE;
}

/**
 * mei_vsc_hbuf_depth - returns size of data to read.
 *
 * @dev: the device structure
 *
 * Return: size of data to read
 */
static u32 mei_vsc_hbuf_depth(const struct mei_device *dev)
{
	return VSC_MEI_MAX_MSG_SIZE / MEI_SLOT_SIZE;
}

/**
 * mei_vsc_write - writes a message to FW.
 *
 * @dev: the device structure
 * @hdr: header of message
 * @hdr_len: header length in bytes: must be multiplication of a slot (4bytes)
 * @data: payload
 * @data_len: payload length in bytes
 *
 * Return: 0 if success, <0 - otherwise.
 */
static int mei_vsc_write(struct mei_device *dev, const void *hdr, size_t hdr_len, const void *data,
			 size_t data_len)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	int ret;
	char *buf = hw->tx_buf;

	if (!hdr || !data || hdr_len & 0x3 || data_len > VSC_MEI_MAX_MSG_SIZE) {
		dev_err(dev->dev, "error write msg hdr_len %zu data_len %zu\n", hdr_len, data_len);
		return -EINVAL;
	}

	atomic_inc(&hw->write_lock_cnt);
	memcpy(buf, hdr, hdr_len);
	memcpy(buf + hdr_len, data, data_len);

	ret = mei_vsc_write_raw(hw, buf, hdr_len + data_len);
	if (ret)
		dev_err(dev->dev, MEI_HDR_FMT "hdr_len %zu data len %zu\n",
			MEI_HDR_PRM((struct mei_msg_hdr *)hdr), hdr_len, data_len);

	atomic_dec_if_positive(&hw->write_lock_cnt);

	return ret;
}

/**
 * mei_vsc_read - read vsc message
 *
 * @dev: the device structure
 *
 * Return: mei hdr value (u32), 0 if error.
 */
static inline u32 mei_vsc_read(const struct mei_device *dev)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	int ret;

	ret = mei_vsc_read_raw(hw, hw->rx_buf, sizeof(hw->rx_buf), &hw->rx_len);
	if (ret || hw->rx_len < sizeof(u32))
		return 0;

	return *(u32 *)hw->rx_buf;
}

/**
 * mei_vsc_count_full_read_slots - counts read full slots.
 *
 * @dev: the device structure
 *
 * Return: -EOVERFLOW if overflow, otherwise filled slots count
 */
static int mei_vsc_count_full_read_slots(struct mei_device *dev)
{
	return VSC_MEI_MAX_MSG_SIZE / MEI_SLOT_SIZE;
}

/**
 * mei_vsc_read_slots - reads a message from mei device.
 *
 * @dev: the device structure
 * @buf: message buf will be written
 * @len: message size will be read
 *
 * Return: always 0
 */
static int mei_vsc_read_slots(struct mei_device *dev, unsigned char *buf, unsigned long len)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct mei_msg_hdr *hdr;

	hdr = (struct mei_msg_hdr *)hw->rx_buf;
	if (len != hdr->length || hdr->length + sizeof(*hdr) != hw->rx_len)
		return -EINVAL;

	memcpy(buf, hw->rx_buf + sizeof(*hdr), len);

	return 0;
}

/**
 * mei_vsc_pg_in_transition - is device now in pg transition
 *
 * @dev: the device structure
 *
 * Return: true if in pg transition, false otherwise
 */
static bool mei_vsc_pg_in_transition(struct mei_device *dev)
{
	return dev->pg_event >= MEI_PG_EVENT_WAIT && dev->pg_event <= MEI_PG_EVENT_INTR_WAIT;
}

/**
 * mei_vsc_pg_is_enabled - detect if PG is supported by HW
 *
 * @dev: the device structure
 *
 * Return: true is pg supported, false otherwise
 */
static bool mei_vsc_pg_is_enabled(struct mei_device *dev)
{
	return false;
}

/**
 * mei_vsc_hw_reset - resets fw.
 *
 * @dev: the device structure
 * @intr_enable: if interrupt should be enabled after reset.
 *
 * Return: 0 on success an error code otherwise
 */
static int mei_vsc_hw_reset(struct mei_device *dev, bool intr_enable)
{
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	int ret;

	vsctp_reset(hw->tp);

	if (hw->disconnect)
		return 0;

	mei_vsc_intr_disable(dev);
	ret = init_hw(dev);
	if (ret)
		return -ENODEV;

	return 0;
}

void mei_vsc_event_cb(void *context)
{
	struct mei_device *dev = context;
	struct mei_vsc_hw *hw = to_vsc_hw(dev);
	struct list_head cmpl_list;
	s32 slots;
	int rets = 0;
	pr_info("%s %d\n", __func__, __LINE__);
	if (dev->dev_state == MEI_DEV_INITIALIZING || dev->dev_state == MEI_DEV_RESETTING)
		return;
	pr_info("%s %d\n", __func__, __LINE__);

	/* initialize our complete list */
	mutex_lock(&dev->device_lock);
	INIT_LIST_HEAD(&cmpl_list);

reread:
	while (vsctp_need_read(hw->tp)) {
			pr_info("%s %d\n", __func__, __LINE__);

		/* check slots available for reading */
		slots = mei_count_full_read_slots(dev);
		rets = mei_irq_read_handler(dev, &cmpl_list, &slots);

		/*
		 * There is a race between VSC MEI write and interrupt delivery:
		 * Not all data is always available immediately after the
		 * interrupt, so try to read again on the next interrupt.
		 */
		if (rets == -ENODATA)
			goto end;

		if (rets && dev->dev_state != MEI_DEV_RESETTING &&
		    dev->dev_state != MEI_DEV_POWER_DOWN) {
			dev_err(dev->dev, "mei_irq_read_handler ret = %d.\n", rets);
			schedule_work(&dev->reset_work);
			goto end;
		}
	}

	pr_info("%s %d\n", __func__, __LINE__);

	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
	rets = mei_irq_write_handler(dev, &cmpl_list);

	dev->hbuf_is_ready = mei_hbuf_is_ready(dev);
	mei_irq_compl_handler(dev, &cmpl_list);
	pr_info("%s %d\n", __func__, __LINE__);

	if (vsctp_need_read(hw->tp))
		goto reread;
end:
	mutex_unlock(&dev->device_lock);
		pr_info("%s %d\n", __func__, __LINE__);

}

static const struct mei_hw_ops mei_vsc_hw_ops = {
	.fw_status = mei_vsc_fw_status,
	.pg_state = mei_vsc_pg_state,

	.host_is_ready = mei_vsc_host_is_ready,
	.hw_is_ready = mei_vsc_hw_is_ready,
	.hw_reset = mei_vsc_hw_reset,
	.hw_config = mei_vsc_hw_config,
	.hw_start = mei_vsc_hw_start,

	.pg_in_transition = mei_vsc_pg_in_transition,
	.pg_is_enabled = mei_vsc_pg_is_enabled,

	.intr_clear = mei_vsc_intr_clear,
	.intr_enable = mei_vsc_intr_enable,
	.intr_disable = mei_vsc_intr_disable,
	.synchronize_irq = mei_vsc_synchronize_irq,

	.hbuf_free_slots = mei_vsc_hbuf_empty_slots,
	.hbuf_is_ready = mei_vsc_hbuf_is_ready,
	.hbuf_depth = mei_vsc_hbuf_depth,
	.write = mei_vsc_write,

	.rdbuf_full_slots = mei_vsc_count_full_read_slots,
	.read_hdr = mei_vsc_read,
	.read = mei_vsc_read_slots,
};

/**
 * mei_vsc_dev_init - allocates and initializes the mei device structure
 *
 * @parent: device associated with physical device (auxiliary device)
 *
 * Return: The mei_device pointer on success, error code on failure.
 */
struct mei_device *mei_vsc_dev_init(struct device *parent)
{
	struct mei_device *dev;
	struct mei_vsc_hw *hw;

	dev = devm_kzalloc(parent, struct_size(dev, hw, sizeof(*hw)), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	hw = to_vsc_hw(dev);
	mei_device_init(dev, parent, false, &mei_vsc_hw_ops);
	dev->fw_f_fw_ver_supported = 0;
	dev->kind = 0;
	atomic_set(&hw->write_lock_cnt, 0);

	return dev;
}
