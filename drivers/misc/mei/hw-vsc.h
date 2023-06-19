/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#ifndef _MEI_HW_VSC_H_
#define _MEI_HW_VSC_H_

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/mei.h>

#include "mei_dev.h"

/* IPSC */
#define VSC_MAGIC_NUM 0x49505343
/* FVCS */
#define VSC_FILE_MAGIC 0x46564353
/* IWFS */
#define VSC_FW_MAGIC 0x49574653

#define VSC_ROM_PKG_SIZE 256
#define VSC_FW_PKG_SIZE 512

/* memory size is 0x51000000 */
#define VSC_IMG_MAX_LOC (0x51000000 - 1)
#define VSC_FW_MAX_SIZE 0x200000
#define VSC_SKU_CONFIG_LOC 0x5001A000
#define VSC_SKU_MAX_SIZE 4100

#define VSC_IMG_DMA_ENABLE_OPTION BIT(0)

#define VSC_SIG_SIZE 384
#define VSC_PUBKEY_SIZE 384
#define VSC_CSSHEADER_SIZE 128

enum {
	VSC_CMD_QUERY,
	VSC_CMD_DL_SET,
	VSC_CMD_DL_START,
	VSC_CMD_DL_CONT,
	VSC_CMD_DUMP_MEM,
	VSC_CMD_SET_REG,
	VSC_CMD_PRINT_ROM_VERSION,
	VSC_CMD_WRITE_FLASH,
	VSC_CMD_RESERVED,
};

enum vsc_image_type {
	VSC_IMG_DEBUG,
	VSC_IMG_BOOTLOADER,
	VSC_IMG_EM7D,
	VSC_IMG_ARCSEM,
	VSC_IMG_ACE_RUNTIME,
	VSC_IMG_ACE_VISION,
	VSC_IMG_ACE_CONFIG,
	VSC_IMG_SKU_CONFIG,
};

/* Firmware Image count define */
#define VSC_IMG_ACEV_ACECNF 2
#define VSC_IMG_BOOT_ARC_EM7D 3
#define VSC_IMG_BOOT_ARC_ACER_EM7D 4
#define VSC_IMG_BOOT_ARC_ACER_ACEV_EM7D 5
#define VSC_IMG_BOOT_ARC_ACER_ACEV_ACECNF_EM7D 6
#define VSC_IMG_ARC_ACER_ACEV_ACECNF_EM7D (VSC_IMG_BOOT_ARC_ACER_ACEV_ACECNF_EM7D - 1)
#define VSC_IMG_CNT_MAX VSC_IMG_BOOT_ARC_ACER_ACEV_ACECNF_EM7D

enum {
	VSC_TOKEN_BOOTLOADER_REQ = 1,
	VSC_TOKEN_FIRMWARE_REQ,
	VSC_TOKEN_DOWNLOAD_CONT,
	VSC_TOKEN_DUMP_RESP,
	VSC_TOKEN_DUMP_CONT,
	VSC_TOKEN_SKU_CONFIG_REQ,
	VSC_TOKEN_ERROR,
	VSC_TOKEN_DUMMY,
	VSC_TOKEN_CAM_STATUS_RESP,
	VSC_TOKEN_CAM_BOOT,
};

#define VSC_MAX_SVN_VALUE 0xFFFFFFFE

#define VSC_EFUSE1_ADDR (0xE0030000 + 0x038)
#define VSC_STRAP_ADDR (0xE0030000 + 0x100)

#define VSC_SI_MAINSTEPPING_VERSION_MASK GENMASK(7, 4)
#define VSC_SI_MAINSTEPPING_VERSION_A 0
#define VSC_SI_MAINSTEPPING_VERSION_B 1
#define VSC_SI_MAINSTEPPING_VERSION_C 2

#define VSC_SI_SUBSTEPPING_VERSION_MASK GENMASK(3, 0)
#define VSC_SI_SUBSTEPPING_VERSION_0 0
#define VSC_SI_SUBSTEPPING_VERSION_0_PRIME 1
#define VSC_SI_SUBSTEPPING_VERSION_1 2
#define VSC_SI_SUBSTEPPING_VERSION_1_PRIME 3

#define VSC_SI_STRAP_KEY_SRC_MASK BIT(16)

#define VSC_SI_STRAP_KEY_SRC_DEBUG 0
#define VSC_SI_STRAP_KEY_SRC_PRODUCT 1

#define VSC_MEI_MAX_MSG_SIZE 512


/* vsc image fragment type of each sub-module */
enum vsc_img_frag_idx {
	VSC_BOOT_IMG_FRAG,
	VSC_ARC_SEM_IMG_FRAG,
	VSC_ACER_IMG_FRAG,
	VSC_ACEV_IMG_FRAG,
	VSC_ACEC_IMG_FRAG,
	VSC_EM7D_IMG_FRAG,
	VSC_SKU_CONF_FRAG,
	VSC_FRAG_MAX,
};

struct vsc_img_frag {
	enum vsc_image_type type;
	u32 location;
	const u8 *data;
	u32 size;
};

struct vsc_rom_master_frame {
	u32 magic;
	u8 cmd;
	union {
		/* download start */
		struct {
			u8 img_type;
			u16 option;
			u32 img_len;
			u32 img_loc;
			u32 crc;
			u8 res[];
		} __packed dl_start;
		/* download set */
		struct {
			u8 option;
			u16 img_cnt;
			u32 payload[];
		} __packed dl_set;
		/* download continue */
		struct {
			u8 end_flag;
			u16 len;
			/* 8 is the offset of payload */
			u8 payload[VSC_ROM_PKG_SIZE - 8];
		} __packed dl_cont;
		/* dump memory */
		struct {
			u8 res;
			u16 len;
			u32 addr;
			u8 payload[];
		} __packed dump_mem;
		/* set register */
		struct {
			u8 res[3];
			u32 addr;
			u32 val;
			u8 payload[];
		} __packed set_reg;
		/* 5 is the offset of padding */
		u8 padding[VSC_ROM_PKG_SIZE - 5];
	} data;
} __packed;
static_assert(sizeof(struct vsc_rom_master_frame) == VSC_ROM_PKG_SIZE);

struct vsc_fw_master_frame {
	u32 magic;
	u8 cmd;
	union {
		struct {
			u16 option;
			u8 img_type;
			u32 img_len;
			u32 img_loc;
			u32 crc;
			u8 res[];
		} __packed dl_start;
		struct {
			u16 option;
			u8 img_cnt;
			u32 payload[];
		} __packed dl_set;
		struct {
			u32 addr;
			u8 len;
			u8 payload[];
		} __packed dump_mem;
		struct {
			u32 addr;
			u32 val;
			u8 payload[];
		} __packed set_reg;
		struct {
			u8 resv[3];
			u32 check_sum;
			u8 payload[];
		} __packed boot;
		/* 5 is the offset of padding */
		u8 padding[VSC_FW_PKG_SIZE - 5];
	} data;
} __packed;
static_assert(sizeof(struct vsc_fw_master_frame) == VSC_FW_PKG_SIZE);

/* fw download continue frame */
struct vsc_master_frame_fw_cont {
	u8 payload[VSC_FW_PKG_SIZE];
} __packed;

struct vsc_rom_slave_token {
	u32 magic;
	u8 token;
	u8 type;
	u8 res[2];
	u8 payload[];
} __packed;

struct vsc_boot_img {
	u32 magic;
	u32 option;
	u32 image_count;
	u32 image_loc[VSC_IMG_CNT_MAX];
} __packed;
#define VSC_BOOT_IMG_OPTION_MASK GENMASK(15, 0)

struct vsc_sensor_img {
	u32 magic;
	u32 option;
	u32 image_count;
	u32 image_loc[VSC_IMG_ACEV_ACECNF];
} __packed;

/* bootloader sign */
struct vsc_btl_sign {
	u32 magic;
	u32 image_size;
	u8 image[];
} __packed;

struct vsc_fw_manifest {
	u32 svn;
	u32 header_ver;
	u32 comp_flags;
	u32 comp_name;
	u32 comp_vendor_name;
	u32 module_size;
	u32 module_addr;
} __packed;

struct vsc_fw_sign {
	u32 magic;
	u32 image_size;
	u8 image[];
} __packed;

struct host_timestamp {
	u64 realtime;
	u64 boottime;
};

struct vsc_boot_fw {
	u32 main_ver;
	u32 sub_ver;
	u32 key_src;
	u32 svn;

	/* buf used to transfer rom pkg or fw pkg */
	char tx_buf[VSC_FW_PKG_SIZE];
	char rx_buf[VSC_FW_PKG_SIZE];
	/* FirmwareBootFile */
	char fw_file_name[256];
	/* PkgBootFile */
	char sensor_file_name[256];
	/* SkuConfigBootFile */
	char sku_cnf_file_name[256];

	u16 fw_option;
	u8 fw_cnt;

	/* vsc fw image fragment for each vsc module */
	struct vsc_img_frag frags[VSC_FRAG_MAX];
};

struct mei_vsc_hw {
	struct vsctp *tp;
	struct auxiliary_device *auxdev;
	u32 rx_len;

	/* 4byte hdr size*/
	char tx_buf[VSC_MEI_MAX_MSG_SIZE + sizeof(struct mei_msg_hdr)];
	char rx_buf[VSC_MEI_MAX_MSG_SIZE + sizeof(struct mei_msg_hdr)];
	atomic_t write_lock_cnt;
	struct vsc_boot_fw fw;
	bool host_ready;
	bool fw_ready;

	/* mutex protecting communication with firmware */
	bool disconnect;
	char cam_sensor_name[32];
};

#define to_vsc_hw(dev) ((struct mei_vsc_hw *)((dev)->hw))
struct mei_device *mei_vsc_dev_init(struct device *parent);
void mei_vsc_event_cb(void *context);

#endif
