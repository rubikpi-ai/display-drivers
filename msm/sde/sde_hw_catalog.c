// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/pm_qos.h>
#if __has_include(<soc/qcom/of_common.h>)
#include <soc/qcom/of_common.h>
#else
#include "qcom_display_internal.h"
#endif

#include "sde_hw_mdss.h"
#include "sde_hw_catalog.h"
#include "sde_hw_catalog_format.h"
#include "sde_kms.h"
#include "sde_hw_uidle.h"
#include "sde_connector.h"

/*************************************************************
 * MACRO DEFINITION
 *************************************************************/

/**
 * Max hardware block in certain hardware. For ex: sspp pipes
 * can have QSEED, pcc, igc, pa, csc, qos entries, etc. This count is
 * 64 based on software design. It should be increased if any of the
 * hardware block has more subblocks.
 */
#define MAX_SDE_HW_BLK  64

/* each entry will have register address and bit offset in that register */
#define MAX_BIT_OFFSET 2

/* max table size for dts property lists, increase if tables grow larger */
#define MAX_SDE_DT_TABLE_SIZE 64

/* default line width for sspp, mixer, ds (input), dsc, wb */
#define DEFAULT_SDE_LINE_WIDTH 2048

/* default output line width for ds */
#define DEFAULT_SDE_OUTPUT_LINE_WIDTH 2560

/* max mixer blend stages */
#define DEFAULT_SDE_MIXER_BLENDSTAGES 7

/*
 * max bank bit for macro tile and ubwc format.
 * this value is left shifted and written to register
 */
#define DEFAULT_SDE_HIGHEST_BANK_BIT 0x02

/* No UBWC */
#define DEFAULT_SDE_UBWC_NONE 0x0

/* default ubwc static config register value */
#define DEFAULT_SDE_UBWC_STATIC 0x0

/* default ubwc swizzle register value */
#define DEFAULT_SDE_UBWC_SWIZZLE 0x0

/* default ubwc macrotile mode value */
#define DEFAULT_SDE_UBWC_MACROTILE_MODE 0x0

/* default hardware block size if dtsi entry is not present */
#define DEFAULT_SDE_HW_BLOCK_LEN 0x100

/* total number of intf - dp, dsi, hdmi */
#define INTF_COUNT			3

#define MAX_UPSCALE_RATIO		20
#define MAX_DOWNSCALE_RATIO		4
#define SSPP_UNITY_SCALE		1

#define MAX_DOWNSCALE_RATIO_INROT_NOPD_RT_NUMERATOR	11
#define MAX_DOWNSCALE_RATIO_INROT_NOPD_RT_DENOMINATOR	5
#define MAX_DOWNSCALE_RATIO_INROT_PD_RT_NUMERATOR	4
#define MAX_DOWNSCALE_RATIO_INROT_PD_RT_DENOMINATOR	1
#define MAX_DOWNSCALE_RATIO_INROT_NRT_DEFAULT		4

#define MAX_PRE_ROT_HEIGHT_INLINE_ROT_DEFAULT	1088

#define MAX_HORZ_DECIMATION		4
#define MAX_VERT_DECIMATION		4

#define MAX_SPLIT_DISPLAY_CTL		2
#define MAX_PP_SPLIT_DISPLAY_CTL	1

#define MDSS_BASE_OFFSET		0x0

#define ROT_LM_OFFSET			3
#define LINE_LM_OFFSET			5
#define LINE_MODE_WB_OFFSET		2

/**
 * these configurations are decided based on max mdp clock. It accounts
 * for max and min display resolution based on virtual hardware resource
 * support.
 */
#define MAX_DISPLAY_HEIGHT_WITH_DECIMATION		2160
#define MAX_DISPLAY_HEIGHT				5760
#define MIN_DISPLAY_HEIGHT				0
#define MIN_DISPLAY_WIDTH				0

/* maximum XIN halt timeout in usec */
#define VBIF_XIN_HALT_TIMEOUT		0x4000

#define DEFAULT_PIXEL_RAM_SIZE		(50 * 1024)

/* access property value based on prop_type and hardware index */
#define PROP_VALUE_ACCESS(p, i, j)		((p + i)->value[j])

/*
 * access element within PROP_TYPE_BIT_OFFSET_ARRAYs based on prop_type,
 * hardware index and offset array index
 */
#define PROP_BITVALUE_ACCESS(p, i, j, k)	((p + i)->bit_value[j][k])

#define DEFAULT_SBUF_HEADROOM		(20)
#define DEFAULT_SBUF_PREFILL		(128)

/*
 * Default parameter values
 */
#define DEFAULT_MAX_BW_HIGH			7000000
#define DEFAULT_MAX_BW_LOW			7000000
#define DEFAULT_UNDERSIZED_PREFILL_LINES	2
#define DEFAULT_XTRA_PREFILL_LINES		2
#define DEFAULT_DEST_SCALE_PREFILL_LINES	3
#define DEFAULT_MACROTILE_PREFILL_LINES		4
#define DEFAULT_YUV_NV12_PREFILL_LINES		8
#define DEFAULT_LINEAR_PREFILL_LINES		1
#define DEFAULT_DOWNSCALING_PREFILL_LINES	1
#define DEFAULT_CORE_IB_FF			"6.0"
#define DEFAULT_CORE_CLK_FF			"1.0"
#define DEFAULT_COMP_RATIO_RT \
		"NV12/5/1/1.23 AB24/5/1/1.23 XB24/5/1/1.23"
#define DEFAULT_COMP_RATIO_NRT \
		"NV12/5/1/1.25 AB24/5/1/1.25 XB24/5/1/1.25"
#define DEFAULT_MAX_PER_PIPE_BW			2400000
#define DEFAULT_AMORTIZABLE_THRESHOLD		25
#define DEFAULT_NUM_DDR_CHANNELS		4
#define DEFAULT_DRAM_EFFICIENCY			64
#define DEFAULT_MNOC_PORTS			2
#define DEFAULT_AXI_BUS_WIDTH			32
#define DEFAULT_CPU_MASK			0
#define DEFAULT_CPU_DMA_LATENCY			PM_QOS_DEFAULT_VALUE
#define DEFAULT_PPB_BUF_MAX_LINES		4

/* Uidle values */
#define SDE_UIDLE_FAL10_EXIT_CNT 128
#define SDE_UIDLE_FAL10_EXIT_DANGER 4
#define SDE_UIDLE_FAL10_DANGER 6
#define SDE_UIDLE_FAL10_TARGET_IDLE 50
#define SDE_UIDLE_FAL1_TARGET_IDLE 40
#define SDE_UIDLE_FAL1_MAX_THRESHOLD 15
#define SDE_UIDLE_FAL1_MAX_THRESHOLD_EXT_REV_102 255
#define SDE_UIDLE_FAL1_MAX_THRESHOLD_EXT_REV_103 255
#define SDE_UIDLE_FAL1_MAX_THRESHOLD_EXT_REV_104 255
#define SDE_UIDLE_FAL10_THRESHOLD_60 12
#define SDE_UIDLE_FAL10_THRESHOLD_90 13
#define SDE_UIDLE_MAX_DWNSCALE 1500
#define SDE_UIDLE_MAX_FPS_60 60
#define SDE_UIDLE_MAX_FPS_90 90
#define SDE_UIDLE_MAX_FPS_120 120
#define SDE_UIDLE_MAX_FPS_240 240

/* Unmult Offsets */
#define SDE_VIG_UNMULT 0x1EA0
#define SDE_DGM_UNMULT 0x804
#define SDE_DGM_UNMULT_2 0xE04

#define SSPP_GET_REGDMA_BASE(blk_base, top_off) ((blk_base) >= (top_off) ?\
		(blk_base) - (top_off) : (blk_base))

/*************************************************************
 *  DTSI PROPERTY INDEX
 *************************************************************/
enum {
	SDE_HW_VERSION,
	SDE_HW_FENCE_VERSION,
	SDE_HW_PROP_MAX,
};

enum {
	HW_OFF,
	HW_LEN,
	HW_DISP,
	HW_PROP_MAX,
};

enum sde_prop {
	SDE_OFF,
	SDE_LEN,
	SSPP_LINEWIDTH,
	VIG_SSPP_LINEWIDTH,
	SCALING_LINEWIDTH,
	MIXER_LINEWIDTH,
	MIXER_BLEND,
	WB_LINEWIDTH,
	WB_LINEWIDTH_LINEAR,
	BANK_BIT,
	UBWC_VERSION,
	UBWC_STATIC,
	UBWC_SWIZZLE,
	QSEED_SW_LIB_REV,
	QSEED_HW_VERSION,
	CSC_TYPE,
	PANIC_PER_PIPE,
	SRC_SPLIT,
	DIM_LAYER,
	SMART_DMA_REV,
	IDLE_PC,
	WAKEUP_WITH_TOUCH,
	DEST_SCALER,
	SMART_PANEL_ALIGN_MODE,
	MACROTILE_MODE,
	UBWC_BW_CALC_VERSION,
	PIPE_ORDER_VERSION,
	SEC_SID_MASK,
	BASE_LAYER,
	TRUSTED_VM_ENV,
	MAX_TRUSTED_VM_DISPLAYS,
	TVM_INCLUDE_REG,
	IPCC_PROTOCOL_ID,
	SDE_EMULATED_ENV,
	IPCC_CLIENT_DPU_PHYS_ID,
	SDE_PROP_MAX,
};

enum {
	PERF_MAX_BW_LOW,
	PERF_MAX_BW_HIGH,
	PERF_MIN_CORE_IB,
	PERF_MIN_LLCC_IB,
	PERF_MIN_DRAM_IB,
	PERF_CORE_IB_FF,
	PERF_CORE_CLK_FF,
	PERF_COMP_RATIO_RT,
	PERF_COMP_RATIO_NRT,
	PERF_UNDERSIZED_PREFILL_LINES,
	PERF_DEST_SCALE_PREFILL_LINES,
	PERF_MACROTILE_PREFILL_LINES,
	PERF_YUV_NV12_PREFILL_LINES,
	PERF_LINEAR_PREFILL_LINES,
	PERF_DOWNSCALING_PREFILL_LINES,
	PERF_XTRA_PREFILL_LINES,
	PERF_NUM_DDR_CHANNELS,
	PERF_DRAM_EFFICIENCY,
	PERF_AMORTIZABLE_THRESHOLD,
	PERF_NUM_MNOC_PORTS,
	PERF_AXI_BUS_WIDTH,
	PERF_CDP_SETTING,
	PERF_CPU_MASK,
	CPU_MASK_PERF,
	PERF_CPU_DMA_LATENCY,
	PERF_CPU_IRQ_LATENCY,
	PERF_PROP_MAX,
};

enum {
	QOS_REFRESH_RATES,
	QOS_DANGER_LUT,
	QOS_SAFE_LUT,
	QOS_CREQ_LUT,
	QOS_PROP_MAX,
};

enum {
	SSPP_OFF,
	SSPP_SIZE,
	SSPP_TYPE,
	SSPP_XIN,
	SSPP_CLK_CTRL,
	SSPP_CLK_STATUS,
	SSPP_SCALE_SIZE,
	SSPP_VIG_BLOCKS,
	SSPP_DMA_BLOCKS,
	SSPP_EXCL_RECT,
	SSPP_SMART_DMA,
	SSPP_MAX_PER_PIPE_BW,
	SSPP_MAX_PER_PIPE_BW_HIGH,
	SSPP_PROP_MAX,
};

enum {
	VIG_SUBBLOCK_INDEX,
	VIG_TOP_OFF,
	VIG_QSEED_OFF,
	VIG_QSEED_LEN,
	VIG_CSC_OFF,
	VIG_HSIC_PROP,
	VIG_MEMCOLOR_PROP,
	VIG_PCC_PROP,
	VIG_GAMUT_PROP,
	VIG_IGC_PROP,
	VIG_INVERSE_PMA,
	VIG_FP16_IGC_PROP,
	VIG_FP16_GC_PROP,
	VIG_FP16_CSC_PROP,
	VIG_FP16_UNMULT_PROP,
	VIG_UCSC_IGC_PROP,
	VIG_UCSC_GC_PROP,
	VIG_UCSC_CSC_PROP,
	VIG_UCSC_UNMULT_PROP,
	VIG_UCSC_ALPHA_DITHER_PROP,
	VIG_PROP_MAX,
};

enum {
	DMA_SUBBLOCK_INDEX,
	DMA_TOP_OFF,
	DMA_IGC_PROP,
	DMA_GC_PROP,
	DMA_DGM_INVERSE_PMA,
	DMA_CSC_OFF,
	DMA_FP16_IGC_PROP,
	DMA_FP16_GC_PROP,
	DMA_FP16_CSC_PROP,
	DMA_FP16_UNMULT_PROP,
	DMA_UCSC_IGC_PROP,
	DMA_UCSC_GC_PROP,
	DMA_UCSC_CSC_PROP,
	DMA_UCSC_UNMULT_PROP,
	DMA_UCSC_ALPHA_DITHER_PROP,
	DMA_PROP_MAX,
};

enum {
	INTF_OFF,
	INTF_LEN,
	INTF_PREFETCH,
	INTF_TYPE,
	INTF_TE_IRQ,
	INTF_PROP_MAX,
};

enum {
	PP_OFF,
	PP_LEN,
	PP_CWB,
	TE_OFF,
	TE_LEN,
	TE2_OFF,
	TE2_LEN,
	PP_SLAVE,
	DITHER_OFF,
	DITHER_LEN,
	DITHER_VER,
	CWB_DITHER,
	PP_MERGE_3D_ID,
	PP_PROP_MAX,
};

enum {
	DSC_OFF,
	DSC_LEN,
	DSC_PAIR_MASK,
	DSC_REV,
	DSC_ENC,
	DSC_ENC_LEN,
	DSC_CTL,
	DSC_CTL_LEN,
	DSC_422,
	DSC_LINEWIDTH,
	DSC_PROP_MAX,
};

enum {
	VDC_OFF,
	VDC_LEN,
	VDC_REV,
	VDC_ENC,
	VDC_ENC_LEN,
	VDC_CTL,
	VDC_CTL_LEN,
	VDC_PROP_MAX,
};

enum {
	DNSC_BLUR_OFF,
	DNSC_BLUR_LEN,
	DNSC_BLUR_VERSION,
	DNSC_BLUR_GAUS_LUT_OFF,
	DNSC_BLUR_GAUS_LUT_LEN,
	DNSC_BLUR_DITHER_OFF,
	DNSC_BLUR_DITHER_LEN,
	DNSC_BLUR_PROP_MAX,
};

enum {
	DS_TOP_OFF,
	DS_TOP_LEN,
	DS_TOP_INPUT_LINEWIDTH,
	DS_TOP_OUTPUT_LINEWIDTH,
	DS_TOP_PROP_MAX,
};

enum {
	DS_OFF,
	DS_LEN,
	DS_PROP_MAX,
};

enum {
	DSPP_TOP_OFF,
	DSPP_TOP_SIZE,
	DSPP_TOP_PROP_MAX,
};

enum {
	DSPP_OFF,
	DSPP_SIZE,
	DSPP_BLOCKS,
	DSPP_PROP_MAX,
};

enum {
	DSPP_IGC_PROP,
	DSPP_PCC_PROP,
	DSPP_GC_PROP,
	DSPP_HSIC_PROP,
	DSPP_MEMCOLOR_PROP,
	DSPP_SIXZONE_PROP,
	DSPP_GAMUT_PROP,
	DSPP_DITHER_PROP,
	DSPP_HIST_PROP,
	DSPP_VLUT_PROP,
	DSPP_BLOCKS_PROP_MAX,
};

enum {
	AD_OFF,
	AD_VERSION,
	AD_PROP_MAX,
};

enum {
	LTM_OFF,
	LTM_VERSION,
	LTM_PROP_MAX,
};

enum {
	RC_OFF,
	RC_LEN,
	RC_VERSION,
	RC_MEM_TOTAL_SIZE,
	RC_MIN_REGION_WIDTH,
	RC_PROP_MAX,
};

enum {
	SPR_OFF,
	SPR_LEN,
	SPR_VERSION,
	SPR_PROP_MAX,
};

enum {
	DEMURA_OFF,
	DEMURA_LEN,
	DEMURA_VERSION,
	DEMURA_PROP_MAX,
};

enum {
	MIXER_OFF,
	MIXER_LEN,
	MIXER_PAIR_MASK,
	MIXER_BLOCKS,
	MIXER_DISP,
	MIXER_CWB,
	MIXER_DCWB,
	MIXER_PROP_MAX,
};

enum {
	MIXER_GC_PROP,
	MIXER_BLOCKS_PROP_MAX,
};

enum {
	MIXER_BLEND_OP_OFF,
	MIXER_BLEND_PROP_MAX,
};

enum {
	WB_OFF,
	WB_LEN,
	WB_ID,
	WB_XIN_ID,
	WB_CLK_CTRL,
	WB_CLK_STATUS,
	WB_PROP_MAX,
};

enum {
	VBIF_OFF,
	VBIF_LEN,
	VBIF_ID,
	VBIF_DEFAULT_OT_RD_LIMIT,
	VBIF_DEFAULT_OT_WR_LIMIT,
	VBIF_DYNAMIC_OT_RD_LIMIT,
	VBIF_DYNAMIC_OT_WR_LIMIT,
	VBIF_MEMTYPE_0,
	VBIF_MEMTYPE_1,
	VBIF_QOS_RT_REMAP,
	VBIF_QOS_NRT_REMAP,
	VBIF_QOS_CWB_REMAP,
	VBIF_QOS_LUTDMA_REMAP,
	VBIF_QOS_CNOC_REMAP,
	VBIF_QOS_OFFLINE_WB_REMAP,
	VBIF_QOS_WB_ROT_REMAP,
	VBIF_PROP_MAX,
};

enum {
	UIDLE_OFF,
	UIDLE_LEN,
	UIDLE_PROP_MAX,
};

enum {
	REG_DMA_OFF,
	REG_DMA_ID,
	REG_DMA_VERSION,
	REG_DMA_TRIGGER_OFF,
	REG_DMA_BROADCAST_DISABLED,
	REG_DMA_XIN_ID,
	REG_DMA_CLK_CTRL,
	REG_DMA_PROP_MAX
};

enum {
	NOISE_LAYER_OFF,
	NOISE_LAYER_VERSION,
	NOISEL_LAYER_PROP_MAX
};

/*************************************************************
 * dts property definition
 *************************************************************/
enum prop_type {
	PROP_TYPE_BOOL,
	PROP_TYPE_U32,
	PROP_TYPE_U32_ARRAY,
	PROP_TYPE_STRING,
	PROP_TYPE_STRING_ARRAY,
	PROP_TYPE_BIT_OFFSET_ARRAY,
	PROP_TYPE_NODE,
};

struct sde_prop_type {
	/* use property index from enum property for readability purpose */
	u8 id;
	/* it should be property name based on dtsi documentation */
	char *prop_name;
	/**
	 * if property is marked mandatory then it will fail parsing
	 * when property is not present
	 */
	u32  is_mandatory;
	/* property type based on "enum prop_type"  */
	enum prop_type type;
};

struct sde_prop_value {
	u32 value[MAX_SDE_HW_BLK];
	u32 bit_value[MAX_SDE_HW_BLK][MAX_BIT_OFFSET];
};

/**
 * struct sde_dt_props - stores dts properties read from a sde_prop_type table
 * @exists:	Array of bools indicating if the given prop name was present
 * @counts:	Count of the number of valid values for the property
 * @values:	Array storing the count[i] property values
 *
 * Must use the sde_[get|put]_dt_props APIs to allocate/free this object.
 */
struct sde_dt_props {
	bool exists[MAX_SDE_DT_TABLE_SIZE];
	int counts[MAX_SDE_DT_TABLE_SIZE];
	struct sde_prop_value *values;
};

/*************************************************************
 * dts property list
 *************************************************************/
static struct sde_prop_type sde_hw_prop[] = {
	{SDE_HW_VERSION, "qcom,sde-hw-version", false, PROP_TYPE_U32},
	{SDE_HW_FENCE_VERSION, "qcom,hw-fence-sw-version", false, PROP_TYPE_U32},
};

static struct sde_prop_type sde_prop[] = {
	{SDE_OFF, "qcom,sde-off", true, PROP_TYPE_U32},
	{SDE_LEN, "qcom,sde-len", false, PROP_TYPE_U32},
	{SSPP_LINEWIDTH, "qcom,sde-sspp-linewidth", false, PROP_TYPE_U32},
	{VIG_SSPP_LINEWIDTH, "qcom,sde-vig-sspp-linewidth", false, PROP_TYPE_U32},
	{SCALING_LINEWIDTH, "qcom,sde-scaling-linewidth", false, PROP_TYPE_U32},
	{MIXER_LINEWIDTH, "qcom,sde-mixer-linewidth", false, PROP_TYPE_U32},
	{MIXER_BLEND, "qcom,sde-mixer-blendstages", false, PROP_TYPE_U32},
	{WB_LINEWIDTH, "qcom,sde-wb-linewidth", false, PROP_TYPE_U32},
	{WB_LINEWIDTH_LINEAR, "qcom,sde-wb-linewidth-linear",
			false, PROP_TYPE_U32},
	{BANK_BIT, "qcom,sde-highest-bank-bit", false,
			PROP_TYPE_BIT_OFFSET_ARRAY},
	{UBWC_VERSION, "qcom,sde-ubwc-version", false, PROP_TYPE_U32},
	{UBWC_STATIC, "qcom,sde-ubwc-static", false, PROP_TYPE_U32},
	{UBWC_SWIZZLE, "qcom,sde-ubwc-swizzle", false, PROP_TYPE_U32},
	{QSEED_SW_LIB_REV, "qcom,sde-qseed-sw-lib-rev", false,
			PROP_TYPE_STRING},
	{QSEED_HW_VERSION, "qcom,sde-qseed-scalar-version", false,
			PROP_TYPE_U32},
	{CSC_TYPE, "qcom,sde-csc-type", false, PROP_TYPE_STRING},
	{PANIC_PER_PIPE, "qcom,sde-panic-per-pipe", false, PROP_TYPE_BOOL},
	{SRC_SPLIT, "qcom,sde-has-src-split", false, PROP_TYPE_BOOL},
	{DIM_LAYER, "qcom,sde-has-dim-layer", false, PROP_TYPE_BOOL},
	{SMART_DMA_REV, "qcom,sde-smart-dma-rev", false, PROP_TYPE_STRING},
	{IDLE_PC, "qcom,sde-has-idle-pc", false, PROP_TYPE_BOOL},
	{WAKEUP_WITH_TOUCH, "qcom,sde-wakeup-with-touch", false,
			PROP_TYPE_BOOL},
	{DEST_SCALER, "qcom,sde-has-dest-scaler", false, PROP_TYPE_BOOL},
	{SMART_PANEL_ALIGN_MODE, "qcom,sde-smart-panel-align-mode",
			false, PROP_TYPE_U32},
	{MACROTILE_MODE, "qcom,sde-macrotile-mode", false, PROP_TYPE_U32},
	{UBWC_BW_CALC_VERSION, "qcom,sde-ubwc-bw-calc-version", false,
			PROP_TYPE_U32},
	{PIPE_ORDER_VERSION, "qcom,sde-pipe-order-version", false,
			PROP_TYPE_U32},
	{SEC_SID_MASK, "qcom,sde-secure-sid-mask", false, PROP_TYPE_U32_ARRAY},
	{BASE_LAYER, "qcom,sde-mixer-stage-base-layer", false, PROP_TYPE_BOOL},
	{TRUSTED_VM_ENV, "qcom,sde-trusted-vm-env", false, PROP_TYPE_BOOL},
	{MAX_TRUSTED_VM_DISPLAYS, "qcom,sde-max-trusted-vm-displays", false,
			PROP_TYPE_U32},
	{TVM_INCLUDE_REG, "qcom,tvm-include-reg", false, PROP_TYPE_U32_ARRAY},
	{IPCC_PROTOCOL_ID, "qcom,sde-ipcc-protocol-id", false, PROP_TYPE_U32},
	{SDE_EMULATED_ENV, "qcom,sde-emulated-env", false, PROP_TYPE_BOOL},
	{IPCC_CLIENT_DPU_PHYS_ID, "qcom,sde-ipcc-client-dpu-phys-id", false, PROP_TYPE_U32}
};

static struct sde_prop_type sde_perf_prop[] = {
	{PERF_MAX_BW_LOW, "qcom,sde-max-bw-low-kbps", false, PROP_TYPE_U32},
	{PERF_MAX_BW_HIGH, "qcom,sde-max-bw-high-kbps", false, PROP_TYPE_U32},
	{PERF_MIN_CORE_IB, "qcom,sde-min-core-ib-kbps", false, PROP_TYPE_U32},
	{PERF_MIN_LLCC_IB, "qcom,sde-min-llcc-ib-kbps", false, PROP_TYPE_U32},
	{PERF_MIN_DRAM_IB, "qcom,sde-min-dram-ib-kbps", false, PROP_TYPE_U32},
	{PERF_CORE_IB_FF, "qcom,sde-core-ib-ff", false, PROP_TYPE_STRING},
	{PERF_CORE_CLK_FF, "qcom,sde-core-clk-ff", false, PROP_TYPE_STRING},
	{PERF_COMP_RATIO_RT, "qcom,sde-comp-ratio-rt", false,
			PROP_TYPE_STRING},
	{PERF_COMP_RATIO_NRT, "qcom,sde-comp-ratio-nrt", false,
			PROP_TYPE_STRING},
	{PERF_UNDERSIZED_PREFILL_LINES, "qcom,sde-undersizedprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_DEST_SCALE_PREFILL_LINES, "qcom,sde-dest-scaleprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_MACROTILE_PREFILL_LINES, "qcom,sde-macrotileprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_YUV_NV12_PREFILL_LINES, "qcom,sde-yuv-nv12prefill-lines",
			false, PROP_TYPE_U32},
	{PERF_LINEAR_PREFILL_LINES, "qcom,sde-linearprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_DOWNSCALING_PREFILL_LINES, "qcom,sde-downscalingprefill-lines",
			false, PROP_TYPE_U32},
	{PERF_XTRA_PREFILL_LINES, "qcom,sde-xtra-prefill-lines",
			false, PROP_TYPE_U32},
	{PERF_NUM_DDR_CHANNELS, "qcom,sde-num-ddr-channels", false, PROP_TYPE_U32},
	{PERF_DRAM_EFFICIENCY, "qcom,sde-dram-efficiency", false, PROP_TYPE_U32},
	{PERF_AMORTIZABLE_THRESHOLD, "qcom,sde-amortizable-threshold",
			false, PROP_TYPE_U32},
	{PERF_NUM_MNOC_PORTS, "qcom,sde-num-mnoc-ports",
			false, PROP_TYPE_U32},
	{PERF_AXI_BUS_WIDTH, "qcom,sde-axi-bus-width",
			false, PROP_TYPE_U32},
	{PERF_CDP_SETTING, "qcom,sde-cdp-setting", false,
			PROP_TYPE_U32_ARRAY},
	{PERF_CPU_MASK, "qcom,sde-qos-cpu-mask", false, PROP_TYPE_U32},
	{CPU_MASK_PERF, "qcom,sde-qos-cpu-mask-performance", false,
			PROP_TYPE_U32},
	{PERF_CPU_DMA_LATENCY, "qcom,sde-qos-cpu-dma-latency", false,
			PROP_TYPE_U32},
	{PERF_CPU_IRQ_LATENCY, "qcom,sde-qos-cpu-irq-latency", false,
			PROP_TYPE_U32},
};

static struct sde_prop_type sde_qos_prop[] = {
	{QOS_REFRESH_RATES, "qcom,sde-qos-refresh-rates", false,
			PROP_TYPE_U32_ARRAY},
	{QOS_DANGER_LUT, "qcom,sde-danger-lut", false, PROP_TYPE_U32_ARRAY},
	{QOS_SAFE_LUT, "qcom,sde-safe-lut", false, PROP_TYPE_U32_ARRAY},
	{QOS_CREQ_LUT, "qcom,sde-creq-lut", false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type sspp_prop[] = {
	{SSPP_OFF, "qcom,sde-sspp-off", true, PROP_TYPE_U32_ARRAY},
	{SSPP_SIZE, "qcom,sde-sspp-src-size", false, PROP_TYPE_U32},
	{SSPP_TYPE, "qcom,sde-sspp-type", true, PROP_TYPE_STRING_ARRAY},
	{SSPP_XIN, "qcom,sde-sspp-xin-id", true, PROP_TYPE_U32_ARRAY},
	{SSPP_CLK_CTRL, "qcom,sde-sspp-clk-ctrl", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
	{SSPP_CLK_STATUS, "qcom,sde-sspp-clk-status", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
	{SSPP_SCALE_SIZE, "qcom,sde-sspp-scale-size", false, PROP_TYPE_U32},
	{SSPP_VIG_BLOCKS, "qcom,sde-sspp-vig-blocks", false, PROP_TYPE_NODE},
	{SSPP_DMA_BLOCKS, "qcom,sde-sspp-dma-blocks", false, PROP_TYPE_NODE},
	{SSPP_EXCL_RECT, "qcom,sde-sspp-excl-rect", false, PROP_TYPE_U32_ARRAY},
	{SSPP_SMART_DMA, "qcom,sde-sspp-smart-dma-priority", false,
		PROP_TYPE_U32_ARRAY},
	{SSPP_MAX_PER_PIPE_BW, "qcom,sde-max-per-pipe-bw-kbps", false,
		PROP_TYPE_U32_ARRAY},
	{SSPP_MAX_PER_PIPE_BW_HIGH, "qcom,sde-max-per-pipe-bw-high-kbps", false,
		PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type vig_prop[] = {
	[VIG_SUBBLOCK_INDEX] = {VIG_SUBBLOCK_INDEX, "cell-index", false,
                        PROP_TYPE_U32},
	[VIG_TOP_OFF] = {VIG_TOP_OFF, "qcom,sde-vig-top-off", false,
			PROP_TYPE_U32},
	[VIG_QSEED_OFF] = {VIG_QSEED_OFF, "qcom,sde-vig-qseed-off", false,
			PROP_TYPE_U32},
	[VIG_QSEED_LEN] = {VIG_QSEED_LEN, "qcom,sde-vig-qseed-size", false,
			PROP_TYPE_U32},
	[VIG_CSC_OFF] = {VIG_CSC_OFF, "qcom,sde-vig-csc-off", false,
			PROP_TYPE_U32},
	[VIG_HSIC_PROP] = {VIG_HSIC_PROP, "qcom,sde-vig-hsic", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_MEMCOLOR_PROP] = {VIG_MEMCOLOR_PROP, "qcom,sde-vig-memcolor",
			false, PROP_TYPE_U32_ARRAY},
	[VIG_PCC_PROP] = {VIG_PCC_PROP, "qcom,sde-vig-pcc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_GAMUT_PROP] = {VIG_GAMUT_PROP, "qcom,sde-vig-gamut", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_IGC_PROP] = {VIG_IGC_PROP, "qcom,sde-vig-igc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_INVERSE_PMA] = {VIG_INVERSE_PMA, "qcom,sde-vig-inverse-pma", false,
			PROP_TYPE_BOOL},
	[VIG_FP16_IGC_PROP] = {VIG_FP16_IGC_PROP, "qcom,sde-fp16-igc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_FP16_GC_PROP] = {VIG_FP16_GC_PROP, "qcom,sde-fp16-gc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_FP16_CSC_PROP] = {VIG_FP16_CSC_PROP, "qcom,sde-fp16-csc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_FP16_UNMULT_PROP] = {VIG_FP16_UNMULT_PROP, "qcom,sde-fp16-unmult",
			false, PROP_TYPE_U32_ARRAY},
	[VIG_UCSC_IGC_PROP] = {VIG_UCSC_IGC_PROP, "qcom,sde-ucsc-igc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_UCSC_GC_PROP] = {VIG_UCSC_GC_PROP, "qcom,sde-ucsc-gc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_UCSC_CSC_PROP] = {VIG_UCSC_CSC_PROP, "qcom,sde-ucsc-csc", false,
			PROP_TYPE_U32_ARRAY},
	[VIG_UCSC_UNMULT_PROP] = {VIG_UCSC_UNMULT_PROP, "qcom,sde-ucsc-unmult",
			false, PROP_TYPE_U32_ARRAY},
	[VIG_UCSC_ALPHA_DITHER_PROP] = {VIG_UCSC_ALPHA_DITHER_PROP, "qcom,sde-ucsc-alpha-dither",
			false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type dma_prop[] = {
	[DMA_SUBBLOCK_INDEX] = {DMA_SUBBLOCK_INDEX, "cell-index", false,
			PROP_TYPE_U32},
	[DMA_TOP_OFF] = {DMA_TOP_OFF, "qcom,sde-dma-top-off", false,
			PROP_TYPE_U32},
	[DMA_IGC_PROP] = {DMA_IGC_PROP, "qcom,sde-dma-igc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_GC_PROP] = {DMA_GC_PROP, "qcom,sde-dma-gc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_DGM_INVERSE_PMA] = {DMA_DGM_INVERSE_PMA,
			"qcom,sde-dma-inverse-pma", false, PROP_TYPE_BOOL},
	[DMA_CSC_OFF] = {DMA_CSC_OFF, "qcom,sde-dma-csc-off", false,
			PROP_TYPE_U32},
	[DMA_FP16_IGC_PROP] = {DMA_FP16_IGC_PROP, "qcom,sde-fp16-igc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_FP16_GC_PROP] = {DMA_FP16_GC_PROP, "qcom,sde-fp16-gc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_FP16_CSC_PROP] = {DMA_FP16_CSC_PROP, "qcom,sde-fp16-csc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_FP16_UNMULT_PROP] = {DMA_FP16_UNMULT_PROP, "qcom,sde-fp16-unmult",
			false, PROP_TYPE_U32_ARRAY},
	[DMA_UCSC_IGC_PROP] = {DMA_UCSC_IGC_PROP, "qcom,sde-ucsc-igc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_UCSC_GC_PROP] = {DMA_UCSC_GC_PROP, "qcom,sde-ucsc-gc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_UCSC_CSC_PROP] = {DMA_UCSC_CSC_PROP, "qcom,sde-ucsc-csc", false,
			PROP_TYPE_U32_ARRAY},
	[DMA_UCSC_UNMULT_PROP] = {DMA_UCSC_UNMULT_PROP, "qcom,sde-ucsc-unmult",
			false, PROP_TYPE_U32_ARRAY},
	[DMA_UCSC_ALPHA_DITHER_PROP] = {DMA_UCSC_ALPHA_DITHER_PROP, "qcom,sde-ucsc-alpha-dither",
			false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type ctl_prop[] = {
	{HW_OFF, "qcom,sde-ctl-off", true, PROP_TYPE_U32_ARRAY},
	{HW_LEN, "qcom,sde-ctl-size", false, PROP_TYPE_U32},
	{HW_DISP, "qcom,sde-ctl-display-pref", false, PROP_TYPE_STRING_ARRAY},
};

struct sde_prop_type mixer_blend_prop[] = {
	{MIXER_BLEND_OP_OFF, "qcom,sde-mixer-blend-op-off", true,
		PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type mixer_prop[] = {
	{MIXER_OFF, "qcom,sde-mixer-off", true, PROP_TYPE_U32_ARRAY},
	{MIXER_LEN, "qcom,sde-mixer-size", false, PROP_TYPE_U32},
	{MIXER_PAIR_MASK, "qcom,sde-mixer-pair-mask", true,
		PROP_TYPE_U32_ARRAY},
	{MIXER_BLOCKS, "qcom,sde-mixer-blocks", false, PROP_TYPE_NODE},
	{MIXER_DISP, "qcom,sde-mixer-display-pref", false,
		PROP_TYPE_STRING_ARRAY},
	{MIXER_CWB, "qcom,sde-mixer-cwb-pref", false,
		PROP_TYPE_STRING_ARRAY},
	{MIXER_DCWB, "qcom,sde-mixer-dcwb-pref", false,
		PROP_TYPE_STRING_ARRAY},
};

static struct sde_prop_type mixer_blocks_prop[] = {
	{MIXER_GC_PROP, "qcom,sde-mixer-gc", false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type dspp_top_prop[] = {
	{DSPP_TOP_OFF, "qcom,sde-dspp-top-off", true, PROP_TYPE_U32},
	{DSPP_TOP_SIZE, "qcom,sde-dspp-top-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type dspp_prop[] = {
	{DSPP_OFF, "qcom,sde-dspp-off", true, PROP_TYPE_U32_ARRAY},
	{DSPP_SIZE, "qcom,sde-dspp-size", false, PROP_TYPE_U32},
	{DSPP_BLOCKS, "qcom,sde-dspp-blocks", false, PROP_TYPE_NODE},
};

static struct sde_prop_type dspp_blocks_prop[] = {
	{DSPP_IGC_PROP, "qcom,sde-dspp-igc", false, PROP_TYPE_U32_ARRAY},
	{DSPP_PCC_PROP, "qcom,sde-dspp-pcc", false, PROP_TYPE_U32_ARRAY},
	{DSPP_GC_PROP, "qcom,sde-dspp-gc", false, PROP_TYPE_U32_ARRAY},
	{DSPP_HSIC_PROP, "qcom,sde-dspp-hsic", false, PROP_TYPE_U32_ARRAY},
	{DSPP_MEMCOLOR_PROP, "qcom,sde-dspp-memcolor", false,
		PROP_TYPE_U32_ARRAY},
	{DSPP_SIXZONE_PROP, "qcom,sde-dspp-sixzone", false,
		PROP_TYPE_U32_ARRAY},
	{DSPP_GAMUT_PROP, "qcom,sde-dspp-gamut", false, PROP_TYPE_U32_ARRAY},
	{DSPP_DITHER_PROP, "qcom,sde-dspp-dither", false, PROP_TYPE_U32_ARRAY},
	{DSPP_HIST_PROP, "qcom,sde-dspp-hist", false, PROP_TYPE_U32_ARRAY},
	{DSPP_VLUT_PROP, "qcom,sde-dspp-vlut", false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type ad_prop[] = {
	{AD_OFF, "qcom,sde-dspp-ad-off", false, PROP_TYPE_U32_ARRAY},
	{AD_VERSION, "qcom,sde-dspp-ad-version", false, PROP_TYPE_U32},
};

static struct sde_prop_type ltm_prop[] = {
	{LTM_OFF, "qcom,sde-dspp-ltm-off", false, PROP_TYPE_U32_ARRAY},
	{LTM_VERSION, "qcom,sde-dspp-ltm-version", false, PROP_TYPE_U32},
};

static struct sde_prop_type rc_prop[] = {
	{RC_OFF, "qcom,sde-dspp-rc-off", false, PROP_TYPE_U32_ARRAY},
	{RC_LEN, "qcom,sde-dspp-rc-size", false, PROP_TYPE_U32},
	{RC_VERSION, "qcom,sde-dspp-rc-version", false, PROP_TYPE_U32},
	{RC_MEM_TOTAL_SIZE, "qcom,sde-dspp-rc-mem-size", false, PROP_TYPE_U32},
	{RC_MIN_REGION_WIDTH, "qcom,sde-dspp-rc-min-region-width", false, PROP_TYPE_U32},
};

static struct sde_prop_type spr_prop[] = {
	{SPR_OFF, "qcom,sde-dspp-spr-off", false, PROP_TYPE_U32_ARRAY},
	{SPR_LEN, "qcom,sde-dspp-spr-size", false, PROP_TYPE_U32},
	{SPR_VERSION, "qcom,sde-dspp-spr-version", false, PROP_TYPE_U32},
};

static struct sde_prop_type ds_top_prop[] = {
	{DS_TOP_OFF, "qcom,sde-dest-scaler-top-off", false, PROP_TYPE_U32},
	{DS_TOP_LEN, "qcom,sde-dest-scaler-top-size", false, PROP_TYPE_U32},
	{DS_TOP_INPUT_LINEWIDTH, "qcom,sde-max-dest-scaler-input-linewidth",
		false, PROP_TYPE_U32},
	{DS_TOP_OUTPUT_LINEWIDTH, "qcom,sde-max-dest-scaler-output-linewidth",
		false, PROP_TYPE_U32},
};

static struct sde_prop_type ds_prop[] = {
	{DS_OFF, "qcom,sde-dest-scaler-off", false, PROP_TYPE_U32_ARRAY},
	{DS_LEN, "qcom,sde-dest-scaler-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type pp_prop[] = {
	{PP_OFF, "qcom,sde-pp-off", true, PROP_TYPE_U32_ARRAY},
	{PP_LEN, "qcom,sde-pp-size", false, PROP_TYPE_U32},
	{PP_CWB, "qcom,sde-pp-cwb", false, PROP_TYPE_U32_ARRAY},
	{TE_OFF, "qcom,sde-te-off", false, PROP_TYPE_U32_ARRAY},
	{TE_LEN, "qcom,sde-te-size", false, PROP_TYPE_U32},
	{TE2_OFF, "qcom,sde-te2-off", false, PROP_TYPE_U32_ARRAY},
	{TE2_LEN, "qcom,sde-te2-size", false, PROP_TYPE_U32},
	{PP_SLAVE, "qcom,sde-pp-slave", false, PROP_TYPE_U32_ARRAY},
	{DITHER_OFF, "qcom,sde-dither-off", false, PROP_TYPE_U32_ARRAY},
	{DITHER_LEN, "qcom,sde-dither-size", false, PROP_TYPE_U32},
	{DITHER_VER, "qcom,sde-dither-version", false, PROP_TYPE_U32},
	{CWB_DITHER, "qcom,sde-cwb-dither", false, PROP_TYPE_U32_ARRAY},
	{PP_MERGE_3D_ID, "qcom,sde-pp-merge-3d-id", false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type dsc_prop[] = {
	{DSC_OFF, "qcom,sde-dsc-off", false, PROP_TYPE_U32_ARRAY},
	{DSC_LEN, "qcom,sde-dsc-size", false, PROP_TYPE_U32},
	{DSC_PAIR_MASK, "qcom,sde-dsc-pair-mask", false, PROP_TYPE_U32_ARRAY},
	{DSC_REV, "qcom,sde-dsc-hw-rev", false, PROP_TYPE_STRING},
	{DSC_ENC, "qcom,sde-dsc-enc", false, PROP_TYPE_U32_ARRAY},
	{DSC_ENC_LEN, "qcom,sde-dsc-enc-size", false, PROP_TYPE_U32},
	{DSC_CTL, "qcom,sde-dsc-ctl", false, PROP_TYPE_U32_ARRAY},
	{DSC_CTL_LEN, "qcom,sde-dsc-ctl-size", false, PROP_TYPE_U32},
	{DSC_422, "qcom,sde-dsc-native422-supp", false, PROP_TYPE_U32_ARRAY},
	{DSC_LINEWIDTH, "qcom,sde-dsc-linewidth", false, PROP_TYPE_U32},
};

static struct sde_prop_type vdc_prop[] = {
	{VDC_OFF, "qcom,sde-vdc-off", false, PROP_TYPE_U32_ARRAY},
	{VDC_LEN, "qcom,sde-vdc-size", false, PROP_TYPE_U32},
	{VDC_REV, "qcom,sde-vdc-hw-rev", false, PROP_TYPE_STRING},
	{VDC_ENC, "qcom,sde-vdc-enc", false, PROP_TYPE_U32_ARRAY},
	{VDC_ENC_LEN, "qcom,sde-vdc-enc-size", false, PROP_TYPE_U32},
	{VDC_CTL, "qcom,sde-vdc-ctl", false, PROP_TYPE_U32_ARRAY},
	{VDC_CTL_LEN, "qcom,sde-vdc-ctl-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type cdm_prop[] = {
	{HW_OFF, "qcom,sde-cdm-off", false, PROP_TYPE_U32_ARRAY},
	{HW_LEN, "qcom,sde-cdm-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type intf_prop[] = {
	{INTF_OFF, "qcom,sde-intf-off", true, PROP_TYPE_U32_ARRAY},
	{INTF_LEN, "qcom,sde-intf-size", false, PROP_TYPE_U32},
	{INTF_PREFETCH, "qcom,sde-intf-max-prefetch-lines", false,
						PROP_TYPE_U32_ARRAY},
	{INTF_TYPE, "qcom,sde-intf-type", false, PROP_TYPE_STRING_ARRAY},
	{INTF_TE_IRQ, "qcom,sde-intf-tear-irq-off", false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type wb_prop[] = {
	{WB_OFF, "qcom,sde-wb-off", false, PROP_TYPE_U32_ARRAY},
	{WB_LEN, "qcom,sde-wb-size", false, PROP_TYPE_U32},
	{WB_ID, "qcom,sde-wb-id", false, PROP_TYPE_U32_ARRAY},
	{WB_XIN_ID, "qcom,sde-wb-xin-id", false, PROP_TYPE_U32_ARRAY},
	{WB_CLK_CTRL, "qcom,sde-wb-clk-ctrl", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
	{WB_CLK_STATUS, "qcom,sde-wb-clk-status", false,
		PROP_TYPE_BIT_OFFSET_ARRAY},
};

static struct sde_prop_type dnsc_blur_prop[] = {
	{DNSC_BLUR_OFF, "qcom,sde-dnsc-blur-off", false, PROP_TYPE_U32_ARRAY},
	{DNSC_BLUR_LEN, "qcom,sde-dnsc-blur-size", false, PROP_TYPE_U32},
	{DNSC_BLUR_VERSION, "qcom,sde-dnsc-blur-version", false, PROP_TYPE_U32},
	{DNSC_BLUR_GAUS_LUT_OFF, "qcom,sde-dnsc-blur-gaus-lut-off", false, PROP_TYPE_U32_ARRAY},
	{DNSC_BLUR_GAUS_LUT_LEN, "qcom,sde-dnsc-blur-gaus-lut-size", false, PROP_TYPE_U32},
	{DNSC_BLUR_DITHER_OFF, "qcom,sde-dnsc-blur-dither-off", false, PROP_TYPE_U32_ARRAY},
	{DNSC_BLUR_DITHER_LEN, "qcom,sde-dnsc-blur-dither-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type vbif_prop[] = {
	{VBIF_OFF, "qcom,sde-vbif-off", true, PROP_TYPE_U32_ARRAY},
	{VBIF_LEN, "qcom,sde-vbif-size", false, PROP_TYPE_U32},
	{VBIF_ID, "qcom,sde-vbif-id", false, PROP_TYPE_U32_ARRAY},
	{VBIF_DEFAULT_OT_RD_LIMIT, "qcom,sde-vbif-default-ot-rd-limit", false, PROP_TYPE_U32},
	{VBIF_DEFAULT_OT_WR_LIMIT, "qcom,sde-vbif-default-ot-wr-limit", false, PROP_TYPE_U32},
	{VBIF_DYNAMIC_OT_RD_LIMIT, "qcom,sde-vbif-dynamic-ot-rd-limit", false, PROP_TYPE_U32_ARRAY},
	{VBIF_DYNAMIC_OT_WR_LIMIT, "qcom,sde-vbif-dynamic-ot-wr-limit", false, PROP_TYPE_U32_ARRAY},
	{VBIF_MEMTYPE_0, "qcom,sde-vbif-memtype-0", false, PROP_TYPE_U32_ARRAY},
	{VBIF_MEMTYPE_1, "qcom,sde-vbif-memtype-1", false, PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_RT_REMAP, "qcom,sde-vbif-qos-rt-remap", false, PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_NRT_REMAP, "qcom,sde-vbif-qos-nrt-remap", false, PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_CWB_REMAP, "qcom,sde-vbif-qos-cwb-remap", false, PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_LUTDMA_REMAP, "qcom,sde-vbif-qos-lutdma-remap", false, PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_CNOC_REMAP, "qcom,sde-vbif-qos-cnoc-remap", false, PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_OFFLINE_WB_REMAP, "qcom,sde-vbif-qos-offline-wb-remap", false,
		PROP_TYPE_U32_ARRAY},
	{VBIF_QOS_WB_ROT_REMAP, "qcom,sde-vbif-qos-wb-rot-remap", false, PROP_TYPE_U32_ARRAY},
};

static struct sde_prop_type uidle_prop[] = {
	{UIDLE_OFF, "qcom,sde-uidle-off", false, PROP_TYPE_U32},
	{UIDLE_LEN, "qcom,sde-uidle-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type reg_dma_prop[REG_DMA_PROP_MAX] = {
	[REG_DMA_OFF] =  {REG_DMA_OFF, "qcom,sde-reg-dma-off", false,
		PROP_TYPE_U32_ARRAY},
	[REG_DMA_ID] =  {REG_DMA_ID, "qcom,sde-reg-dma-id", false,
		PROP_TYPE_U32_ARRAY},
	[REG_DMA_VERSION] = {REG_DMA_VERSION, "qcom,sde-reg-dma-version",
		false, PROP_TYPE_U32},
	[REG_DMA_TRIGGER_OFF] = {REG_DMA_TRIGGER_OFF,
		"qcom,sde-reg-dma-trigger-off", false,
		PROP_TYPE_U32},
	[REG_DMA_BROADCAST_DISABLED] = {REG_DMA_BROADCAST_DISABLED,
		"qcom,sde-reg-dma-broadcast-disabled", false, PROP_TYPE_BOOL},
	[REG_DMA_XIN_ID] = {REG_DMA_XIN_ID,
		"qcom,sde-reg-dma-xin-id", false, PROP_TYPE_U32},
	[REG_DMA_CLK_CTRL] = {REG_DMA_CLK_CTRL,
		"qcom,sde-reg-dma-clk-ctrl", false, PROP_TYPE_BIT_OFFSET_ARRAY},
};

static struct sde_prop_type merge_3d_prop[] = {
	{HW_OFF, "qcom,sde-merge-3d-off", false, PROP_TYPE_U32_ARRAY},
	{HW_LEN, "qcom,sde-merge-3d-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type qdss_prop[] = {
	{HW_OFF, "qcom,sde-qdss-off", false, PROP_TYPE_U32_ARRAY},
	{HW_LEN, "qcom,sde-qdss-size", false, PROP_TYPE_U32},
};

static struct sde_prop_type demura_prop[] = {
	[DEMURA_OFF] = {DEMURA_OFF, "qcom,sde-dspp-demura-off", false,
			PROP_TYPE_U32_ARRAY},
	[DEMURA_LEN] = {DEMURA_LEN, "qcom,sde-dspp-demura-size", false,
			PROP_TYPE_U32},
	[DEMURA_VERSION] = {DEMURA_VERSION, "qcom,sde-dspp-demura-version",
			false, PROP_TYPE_U32},
};

static struct sde_prop_type noise_layer_prop[] = {
	[NOISE_LAYER_OFF] = {NOISE_LAYER_OFF, "qcom,sde-lm-noise-off",
			false, PROP_TYPE_U32},
	[NOISE_LAYER_VERSION] =  {NOISE_LAYER_VERSION,
		"qcom,sde-lm-noise-version", false, PROP_TYPE_U32},
};

/*************************************************************
 * static API list
 *************************************************************/
static int _sde_lm_noise_parse_dt(struct device_node *np, struct sde_mdss_cfg *sde_cfg);

static int _parse_dt_u32_handler(struct device_node *np,
	char *prop_name, u32 *offsets, int len, bool mandatory)
{
	int rc = -EINVAL;

	if (len > MAX_SDE_HW_BLK) {
		SDE_ERROR(
			"prop: %s tries out of bound access for u32 array read len: %d\n",
				prop_name, len);
		return -E2BIG;
	}

	rc = of_property_read_u32_array(np, prop_name, offsets, len);
	if (rc && mandatory)
		SDE_ERROR("mandatory prop: %s u32 array read len:%d\n",
				prop_name, len);
	else if (rc)
		SDE_DEBUG("optional prop: %s u32 array read len:%d\n",
				prop_name, len);

	return rc;
}

static int _parse_dt_bit_offset(struct device_node *np,
	char *prop_name, struct sde_prop_value *prop_value, u32 prop_index,
	u32 count, bool mandatory)
{
	int rc = 0, len, i, j;
	const u32 *arr;

	arr = of_get_property(np, prop_name, &len);
	if (arr) {
		len /= sizeof(u32);
		len &= ~0x1;

		if (len > (MAX_SDE_HW_BLK * MAX_BIT_OFFSET)) {
			SDE_ERROR(
				"prop: %s len: %d will lead to out of bound access\n",
				prop_name, len / MAX_BIT_OFFSET);
			return -E2BIG;
		}

		for (i = 0, j = 0; i < len; j++) {
			PROP_BITVALUE_ACCESS(prop_value, prop_index, j, 0) =
				be32_to_cpu(arr[i]);
			i++;
			PROP_BITVALUE_ACCESS(prop_value, prop_index, j, 1) =
				be32_to_cpu(arr[i]);
			i++;
		}
	} else {
		if (mandatory) {
			SDE_ERROR("error mandatory property '%s' not found\n",
				prop_name);
			rc = -EINVAL;
		} else {
			SDE_DEBUG("error optional property '%s' not found\n",
				prop_name);
		}
	}

	return rc;
}

static int _validate_dt_entry(struct device_node *np,
	struct sde_prop_type *sde_prop, u32 prop_size, int *prop_count,
	int *off_count)
{
	int rc = 0, i, val;
	struct device_node *snp = NULL;

	if (off_count) {
		*off_count = of_property_count_u32_elems(np,
				sde_prop[0].prop_name);
		if ((*off_count > MAX_BLOCKS) || (*off_count < 0)) {
			if (sde_prop[0].is_mandatory) {
				SDE_ERROR(
					"invalid hw offset prop name:%s count: %d\n",
					sde_prop[0].prop_name, *off_count);
				rc = -EINVAL;
			}
			*off_count = 0;
			memset(prop_count, 0, sizeof(int) * prop_size);
			return rc;
		}
	}

	for (i = 0; i < prop_size; i++) {
		switch (sde_prop[i].type) {
		case PROP_TYPE_U32:
			rc = of_property_read_u32(np, sde_prop[i].prop_name,
				&val);
			if (!rc)
				prop_count[i] = 1;
			break;
		case PROP_TYPE_U32_ARRAY:
			prop_count[i] = of_property_count_u32_elems(np,
				sde_prop[i].prop_name);
			if (prop_count[i] < 0)
				rc = prop_count[i];
			break;
		case PROP_TYPE_STRING_ARRAY:
			prop_count[i] = of_property_count_strings(np,
				sde_prop[i].prop_name);
			if (prop_count[i] < 0)
				rc = prop_count[i];
			break;
		case PROP_TYPE_BIT_OFFSET_ARRAY:
			of_get_property(np, sde_prop[i].prop_name, &val);
			prop_count[i] = val / (MAX_BIT_OFFSET * sizeof(u32));
			break;
		case PROP_TYPE_NODE:
			snp = of_get_child_by_name(np,
					sde_prop[i].prop_name);
			if (!snp)
				rc = -EINVAL;
			break;
		case PROP_TYPE_BOOL:
			/**
			 * No special handling for bool properties here.
			 * They will always exist, with value indicating
			 * if the given key is present or not.
			 */
			prop_count[i] = 1;
			break;
		default:
			SDE_DEBUG("invalid property type:%d\n",
							sde_prop[i].type);
			break;
		}
		SDE_DEBUG(
			"prop id:%d prop name:%s prop type:%d prop_count:%d\n",
			i, sde_prop[i].prop_name,
			sde_prop[i].type, prop_count[i]);

		if (rc && sde_prop[i].is_mandatory &&
		   ((sde_prop[i].type == PROP_TYPE_U32) ||
		    (sde_prop[i].type == PROP_TYPE_NODE))) {
			SDE_ERROR("prop:%s not present\n",
						sde_prop[i].prop_name);
			goto end;
		} else if (sde_prop[i].type == PROP_TYPE_U32 ||
			sde_prop[i].type == PROP_TYPE_BOOL ||
			sde_prop[i].type == PROP_TYPE_NODE) {
			rc = 0;
			continue;
		}

		if (off_count && (prop_count[i] != *off_count) &&
				sde_prop[i].is_mandatory) {
			SDE_ERROR(
				"prop:%s count:%d is different compared to offset array:%d\n",
				sde_prop[i].prop_name,
				prop_count[i], *off_count);
			rc = -EINVAL;
			goto end;
		} else if (off_count && prop_count[i] != *off_count) {
			SDE_DEBUG(
				"prop:%s count:%d is different compared to offset array:%d\n",
				sde_prop[i].prop_name,
				prop_count[i], *off_count);
			rc = 0;
		}
		if (prop_count[i] < 0) {
			prop_count[i] = 0;
			if (sde_prop[i].is_mandatory) {
				SDE_ERROR("prop:%s count:%d is negative\n",
					sde_prop[i].prop_name, prop_count[i]);
				rc = -EINVAL;
			} else {
				rc = 0;
				SDE_DEBUG("prop:%s count:%d is negative\n",
					sde_prop[i].prop_name, prop_count[i]);
			}
		}
	}

end:
	return rc;
}

static int _read_dt_entry(struct device_node *np,
	struct sde_prop_type *sde_prop, u32 prop_size, int *prop_count,
	bool *prop_exists,
	struct sde_prop_value *prop_value)
{
	int rc = 0, i, j;

	for (i = 0; i < prop_size; i++) {
		prop_exists[i] = true;
		switch (sde_prop[i].type) {
		case PROP_TYPE_U32:
			rc = of_property_read_u32(np, sde_prop[i].prop_name,
				&PROP_VALUE_ACCESS(prop_value, i, 0));
			SDE_DEBUG(
				"prop id:%d prop name:%s prop type:%d value:0x%x\n",
				i, sde_prop[i].prop_name,
				sde_prop[i].type,
				PROP_VALUE_ACCESS(prop_value, i, 0));
			if (rc)
				prop_exists[i] = false;
			break;
		case PROP_TYPE_BOOL:
			PROP_VALUE_ACCESS(prop_value, i, 0) =
				of_property_read_bool(np,
					sde_prop[i].prop_name);
			SDE_DEBUG(
				"prop id:%d prop name:%s prop type:%d value:0x%x\n",
				i, sde_prop[i].prop_name,
				sde_prop[i].type,
				PROP_VALUE_ACCESS(prop_value, i, 0));
			break;
		case PROP_TYPE_U32_ARRAY:
			rc = _parse_dt_u32_handler(np, sde_prop[i].prop_name,
				&PROP_VALUE_ACCESS(prop_value, i, 0),
				prop_count[i], sde_prop[i].is_mandatory);
			if (rc && sde_prop[i].is_mandatory) {
				SDE_ERROR(
					"%s prop validation success but read failed\n",
					sde_prop[i].prop_name);
				prop_exists[i] = false;
				goto end;
			} else {
				if (rc)
					prop_exists[i] = false;
				/* only for debug purpose */
				SDE_DEBUG(
					"prop id:%d prop name:%s prop type:%d",
					i, sde_prop[i].prop_name,
					sde_prop[i].type);
				for (j = 0; j < prop_count[i]; j++)
					SDE_DEBUG(" value[%d]:0x%x ", j,
						PROP_VALUE_ACCESS(prop_value, i,
								j));
				SDE_DEBUG("\n");
			}
			break;
		case PROP_TYPE_BIT_OFFSET_ARRAY:
			rc = _parse_dt_bit_offset(np, sde_prop[i].prop_name,
				prop_value, i, prop_count[i],
				sde_prop[i].is_mandatory);
			if (rc && sde_prop[i].is_mandatory) {
				SDE_ERROR(
					"%s prop validation success but read failed\n",
					sde_prop[i].prop_name);
				prop_exists[i] = false;
				goto end;
			} else {
				if (rc)
					prop_exists[i] = false;
				SDE_DEBUG(
					"prop id:%d prop name:%s prop type:%d",
					i, sde_prop[i].prop_name,
					sde_prop[i].type);
				for (j = 0; j < prop_count[i]; j++)
					SDE_DEBUG(
					"count[%d]: bit:0x%x off:0x%x\n", j,
					PROP_BITVALUE_ACCESS(prop_value,
						i, j, 0),
					PROP_BITVALUE_ACCESS(prop_value,
						i, j, 1));
				SDE_DEBUG("\n");
			}
			break;
		case PROP_TYPE_NODE:
			/* Node will be parsed in calling function */
			rc = 0;
			break;
		default:
			SDE_DEBUG("invalid property type:%d\n",
							sde_prop[i].type);
			break;
		}
		rc = 0;
	}

end:
	return rc;
}

/**
 * sde_get_dt_props - allocate and return prop counts, exists & values arrays
 * @np	- device node
 * @prop_max	- <BLK>_PROP_MAX enum, this will be number of values allocated
 * @sde_prop	- pointer to prop table
 * @prop_size	- size of prop table
 * @off_count	- pointer to callers off_count
 *
 * @Returns	- valid pointer or -ve error code (can never return NULL)
 * If a non-NULL off_count pointer is given, the value it points to will be
 * updated with the number of elements in the offset array (entry 0 in table).
 * Caller MUST free this object using sde_put_dt_props after parsing values.
 */
static struct sde_dt_props *sde_get_dt_props(struct device_node *np,
		size_t prop_max, struct sde_prop_type *sde_prop,
		u32 prop_size, u32 *off_count)
{
	struct sde_dt_props *props;
	int rc = -ENOMEM;

	props = kzalloc(sizeof(*props), GFP_KERNEL);
	if (!props)
		return ERR_PTR(rc);

	props->values = kcalloc(prop_max, sizeof(*props->values),
			GFP_KERNEL);
	if (!props->values)
		goto free_props;

	rc = _validate_dt_entry(np, sde_prop, prop_size, props->counts,
			off_count);
	if (rc)
		goto free_vals;

	rc = _read_dt_entry(np, sde_prop, prop_size, props->counts,
		props->exists, props->values);
	if (rc)
		goto free_vals;

	return props;

free_vals:
	kfree(props->values);
free_props:
	kfree(props);
	return ERR_PTR(rc);
}

/* sde_put_dt_props - free an sde_dt_props object obtained with "get" */
static void sde_put_dt_props(struct sde_dt_props *props)
{
	if (!props)
		return;

	kfree(props->values);
	kfree(props);
}

static int _add_to_irq_offset_list(struct sde_mdss_cfg *sde_cfg,
		enum sde_intr_hwblk_type blk_type, u32 instance, u32 offset)
{
	struct sde_intr_irq_offsets *item = NULL;
	bool err = false;

	switch (blk_type) {
	case SDE_INTR_HWBLK_TOP:
		if (instance >= SDE_INTR_TOP_MAX)
			err = true;
		break;
	case SDE_INTR_HWBLK_INTF:
		if (instance >= INTF_MAX)
			err = true;
		break;
	case SDE_INTR_HWBLK_AD4:
		if (instance >= AD_MAX)
			err = true;
		break;
	case SDE_INTR_HWBLK_INTF_TEAR:
		if (instance >= INTF_MAX)
			err = true;
		break;
	case SDE_INTR_HWBLK_LTM:
		if (instance >= LTM_MAX)
			err = true;
		break;
	case SDE_INTR_HWBLK_WB:
		if (instance >= WB_MAX)
			err = true;
		break;
	default:
		SDE_ERROR("invalid hwblk_type: %d", blk_type);
		return -EINVAL;
	}

	if (err) {
		SDE_ERROR("unable to map instance %d for blk type %d",
				instance, blk_type);
		return -EINVAL;
	}

	/* Check for existing list entry */
	item = sde_hw_intr_list_lookup(sde_cfg, blk_type, instance);
	if (IS_ERR_OR_NULL(item)) {
		SDE_DEBUG("adding intr type %d idx %d offset 0x%x\n",
				blk_type, instance, offset);
	} else if (item->base_offset == offset) {
		SDE_INFO("duplicate intr %d/%d offset 0x%x, skipping\n",
				blk_type, instance, offset);
		return 0;
	} else {
		SDE_ERROR("type %d, idx %d in list with offset 0x%x != 0x%x\n",
				blk_type, instance, item->base_offset, offset);
		return -EINVAL;
	}

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		SDE_ERROR("memory allocation failed!\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&item->list);
	item->type = blk_type;
	item->instance_idx = instance;
	item->base_offset = offset;
	list_add_tail(&item->list, &sde_cfg->irq_offset_list);

	return 0;
}

/* VIG color management (VCM) feature setup */
static bool _sde_sspp_setup_vcm(struct sde_sspp_cfg *sspp,
		const struct sde_dt_props *props, const char *name,
		struct sde_pp_blk *blk, u32 type, u32 prop, bool versioned)
{
	bool exists = props->exists[prop];

	if (exists) {
		blk->id = type;
		blk->len = 0;
		set_bit(type, (unsigned long *) &sspp->features_ext);
		blk->base = PROP_VALUE_ACCESS(props->values, prop, 0);
		blk->regdma_base = SSPP_GET_REGDMA_BASE(blk->base, sspp->sblk->top_off);
		snprintf(blk->name, SDE_HW_BLK_NAME_LEN, "%s%u", name,
				sspp->id - SSPP_VIG0);
		if (versioned)
			blk->version = PROP_VALUE_ACCESS(props->values,
					prop, 1);
	} else {
		blk->id = 0;
	}

	return exists;
}

static void _sde_sspp_setup_vigs_pp(struct sde_dt_props *props,
		struct sde_mdss_cfg *sde_cfg, struct sde_sspp_cfg *sspp)
{
	struct sde_sspp_sub_blks *sblk = sspp->sblk;
	if (!props)
		return;

	if (sde_cfg->csc_type == SDE_SSPP_CSC)
		_sde_sspp_setup_vcm(sspp, props, "sspp_csc", &sblk->csc_blk,
				SDE_SSPP_CSC, VIG_CSC_OFF, false);
	else if (sde_cfg->csc_type == SDE_SSPP_CSC_10BIT)
		_sde_sspp_setup_vcm(sspp, props, "sspp_csc", &sblk->csc_blk,
				SDE_SSPP_CSC_10BIT, VIG_CSC_OFF, false);

	_sde_sspp_setup_vcm(sspp, props, "sspp_hsic", &sblk->hsic_blk,
			SDE_SSPP_HSIC, VIG_HSIC_PROP, true);

	_sde_sspp_setup_vcm(sspp, props, "sspp_memcolor", &sblk->memcolor_blk,
			SDE_SSPP_MEMCOLOR, VIG_MEMCOLOR_PROP, true);

	_sde_sspp_setup_vcm(sspp, props, "sspp_pcc", &sblk->pcc_blk,
			SDE_SSPP_PCC, VIG_PCC_PROP, true);

	_sde_sspp_setup_vcm(sspp, props, "sspp_vig_gamut", &sblk->gamut_blk,
			SDE_SSPP_VIG_GAMUT, VIG_GAMUT_PROP, true);

	_sde_sspp_setup_vcm(sspp, props, "sspp_vig_igc", &sblk->igc_blk[0],
			SDE_SSPP_VIG_IGC, VIG_IGC_PROP, true);

	if (props->exists[VIG_INVERSE_PMA]) {
		set_bit(SDE_SSPP_INVERSE_PMA, &sspp->features);
		sblk->unmult_offset[0] = SDE_VIG_UNMULT;
	}
}

static int _sde_sspp_setup_vigs(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int i = 0, j = 0, rc = 0;
	struct sde_dt_props *props[SSPP_SUBBLK_COUNT_MAX] = {NULL, NULL};
	struct sde_dt_props *props_tmp = NULL;
	struct device_node *snp = NULL;
	int vig_count = 0, vcm_count = 0;
	const char *type;

	snp = of_get_child_by_name(np, sspp_prop[SSPP_VIG_BLOCKS].prop_name);
	if (!snp)
		return 0;

	/* Assume sub nodes are in rect order */
	vcm_count = of_get_child_count(snp);
	if (vcm_count > 0) {
		struct device_node *vcm_snp;

		if (vcm_count > SSPP_SUBBLK_COUNT_MAX) {
			SDE_ERROR("exceeded max vcm sub-block count!");
			vcm_count = SSPP_SUBBLK_COUNT_MAX;
		}
		for_each_child_of_node(snp, vcm_snp) {

			props_tmp = sde_get_dt_props(vcm_snp,
					VIG_PROP_MAX, vig_prop,
					ARRAY_SIZE(vig_prop), NULL);
			if (IS_ERR(props_tmp)) {
				rc = PTR_ERR(props_tmp);
				props_tmp = NULL;
				goto end;
			}

			if (!props_tmp->exists[VIG_SUBBLOCK_INDEX]) {
				SDE_ERROR("vcm rect index must be specified!");
				goto end;
			}

			i = PROP_VALUE_ACCESS(props_tmp->values, VIG_SUBBLOCK_INDEX, 0);
			if (i >= SSPP_SUBBLK_COUNT_MAX) {
				SDE_ERROR("invalid vcm rect index: %d", i);
				goto end;
			} else if (props[i] != NULL) {
				SDE_ERROR("vcm rect index must be unique! repeat: %d", i);
				goto end;
			}

			props[i] = props_tmp;
			props_tmp = NULL;
		}
	} else {
		props[0] = sde_get_dt_props(snp, VIG_PROP_MAX, vig_prop,
				ARRAY_SIZE(vig_prop), NULL);
	}

	for (i = 0; i < sde_cfg->sspp_count; ++i) {
		struct sde_sspp_cfg *sspp = sde_cfg->sspp + i;
		struct sde_sspp_sub_blks *sblk = sspp->sblk;

		of_property_read_string_index(np,
				sspp_prop[SSPP_TYPE].prop_name, i, &type);
		if (strcmp(type, "vig"))
			continue;

		sblk->maxlinewidth = sde_cfg->vig_sspp_linewidth;
		sblk->scaling_linewidth = sde_cfg->scaling_linewidth;
		sblk->maxupscale = MAX_UPSCALE_RATIO;
		sblk->maxdwnscale = MAX_DOWNSCALE_RATIO;
		sspp->id = SSPP_VIG0 + vig_count;
		snprintf(sspp->name, SDE_HW_BLK_NAME_LEN, "sspp_%u",
				sspp->id - SSPP_VIG0);
		sspp->clk_ctrl = SDE_CLK_CTRL_VIG0 + vig_count;
		sspp->type = SSPP_TYPE_VIG;
		set_bit(SDE_PERF_SSPP_QOS, &sspp->perf_features);
		if (sde_cfg->vbif_qos_nlvl == 8)
			set_bit(SDE_PERF_SSPP_QOS_8LVL, &sspp->perf_features);
		vig_count++;

		/* Obtain sub block top, or maintain backwards compatibility */
		if (props[0] && props[0]->exists[VIG_TOP_OFF])
			sblk->top_off = PROP_VALUE_ACCESS(props[0]->values, VIG_TOP_OFF, 0);
		else
			sblk->top_off = 0x200;

		sblk->format_list = sde_cfg->vig_formats;
		sblk->virt_format_list = sde_cfg->virt_vig_formats;
		sblk->num_fp16_igc_blk = 0;
		sblk->num_fp16_gc_blk = 0;
		sblk->num_fp16_csc_blk = 0;
		sblk->num_fp16_unmult_blk = 0;

		sblk->num_ucsc_igc_blk = 0;
		sblk->num_ucsc_gc_blk = 0;
		sblk->num_ucsc_csc_blk = 0;
		sblk->num_ucsc_unmult_blk = 0;
		sblk->num_ucsc_alpha_dither_blk = 0;

		for (j = 0; j < SSPP_SUBBLK_COUNT_MAX; j++) {
			if (!props[j])
				continue;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_fp16_igc",
					&sblk->fp16_igc_blk[j],
					SDE_SSPP_FP16_IGC, VIG_FP16_IGC_PROP,
					true))
				sblk->num_fp16_igc_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_fp16_gc",
					&sblk->fp16_gc_blk[j],
					SDE_SSPP_FP16_GC, VIG_FP16_GC_PROP,
					true))
				sblk->num_fp16_gc_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_fp16_csc",
					&sblk->fp16_csc_blk[j],
					SDE_SSPP_FP16_CSC, VIG_FP16_CSC_PROP,
					true))
				sblk->num_fp16_csc_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_fp16_unmult",
					&sblk->fp16_unmult_blk[j],
					SDE_SSPP_FP16_UNMULT,
					VIG_FP16_UNMULT_PROP, true))
				sblk->num_fp16_unmult_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_ucsc_igc",
					&sblk->ucsc_igc_blk[j],
					SDE_SSPP_UCSC_IGC, VIG_UCSC_IGC_PROP,
					true))
				sblk->num_ucsc_igc_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_ucsc_gc",
					&sblk->ucsc_gc_blk[j],
					SDE_SSPP_UCSC_GC, VIG_UCSC_GC_PROP,
					true))
				sblk->num_ucsc_gc_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_ucsc_csc",
					&sblk->ucsc_csc_blk[j],
					SDE_SSPP_UCSC_CSC, VIG_UCSC_CSC_PROP,
					true))
				sblk->num_ucsc_csc_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_ucsc_unmult",
					&sblk->ucsc_unmult_blk[j],
					SDE_SSPP_UCSC_UNMULT,
					VIG_UCSC_UNMULT_PROP, true))
				sblk->num_ucsc_unmult_blk += 1;

			if (_sde_sspp_setup_vcm(sspp, props[j],
					"sspp_vig_ucsc_alpha_dither",
					&sblk->ucsc_alpha_dither_blk[j],
					SDE_SSPP_UCSC_ALPHA_DITHER,
					VIG_UCSC_ALPHA_DITHER_PROP, true))
				sblk->num_ucsc_alpha_dither_blk += 1;
		}

		/* PP + scaling only supported on VIG rect 0 */
		if (props[0] && ((sde_cfg->qseed_sw_lib_rev == SDE_SSPP_SCALER_QSEED2) ||
		    (sde_cfg->qseed_sw_lib_rev == SDE_SSPP_SCALER_QSEED3) ||
		    (sde_cfg->qseed_sw_lib_rev == SDE_SSPP_SCALER_QSEED3LITE))) {
			set_bit(sde_cfg->qseed_sw_lib_rev, &sspp->features);
			sblk->scaler_blk.id = sde_cfg->qseed_sw_lib_rev;
			sblk->scaler_blk.base = PROP_VALUE_ACCESS(
					props[0]->values, VIG_QSEED_OFF, 0);
			sblk->scaler_blk.len = PROP_VALUE_ACCESS(
					props[0]->values, VIG_QSEED_LEN, 0);
			sblk->scaler_blk.regdma_base = SSPP_GET_REGDMA_BASE(sblk->scaler_blk.base,
					sblk->top_off);
			snprintf(sblk->scaler_blk.name, SDE_HW_BLK_NAME_LEN,
					"sspp_scaler%u", sspp->id - SSPP_VIG0);
		}

		_sde_sspp_setup_vigs_pp(props[0], sde_cfg, sspp);

		if (sde_cfg->true_inline_rot_rev > 0) {
			set_bit(SDE_SSPP_TRUE_INLINE_ROT, &sspp->features);
			sblk->in_rot_format_list = sde_cfg->inline_rot_formats;
			sblk->in_rot_maxheight =
					MAX_PRE_ROT_HEIGHT_INLINE_ROT_DEFAULT;
		}

		if (IS_SDE_INLINE_ROT_REV_200(sde_cfg->true_inline_rot_rev) ||
				IS_SDE_INLINE_ROT_REV_201(sde_cfg->true_inline_rot_rev)) {
			set_bit(SDE_SSPP_PREDOWNSCALE, &sspp->features);
			sblk->in_rot_maxdwnscale_rt_num =
				MAX_DOWNSCALE_RATIO_INROT_PD_RT_NUMERATOR;
			sblk->in_rot_maxdwnscale_rt_denom =
				MAX_DOWNSCALE_RATIO_INROT_PD_RT_DENOMINATOR;
			sblk->in_rot_maxdwnscale_nrt =
					MAX_DOWNSCALE_RATIO_INROT_NRT_DEFAULT;
			sblk->in_rot_maxdwnscale_rt_nopd_num =
				MAX_DOWNSCALE_RATIO_INROT_NOPD_RT_NUMERATOR;
			sblk->in_rot_maxdwnscale_rt_nopd_denom =
				MAX_DOWNSCALE_RATIO_INROT_NOPD_RT_DENOMINATOR;
		} else if (IS_SDE_INLINE_ROT_REV_100(
				sde_cfg->true_inline_rot_rev)) {
			sblk->in_rot_maxdwnscale_rt_num =
				MAX_DOWNSCALE_RATIO_INROT_NOPD_RT_NUMERATOR;
			sblk->in_rot_maxdwnscale_rt_denom =
				MAX_DOWNSCALE_RATIO_INROT_NOPD_RT_DENOMINATOR;
			sblk->in_rot_maxdwnscale_nrt =
					MAX_DOWNSCALE_RATIO_INROT_NRT_DEFAULT;
		}

		if (test_bit(SDE_FEATURE_INLINE_DISABLE_CONST_CLR, sde_cfg->features))
			set_bit(SDE_SSPP_INLINE_CONST_CLR, &sspp->features);

	}

end:
	sde_put_dt_props(props_tmp);
	for (i = 0; i < SSPP_SUBBLK_COUNT_MAX; i++)
		sde_put_dt_props(props[i]);

	return rc;
}

static void _sde_sspp_setup_dgm(struct sde_sspp_cfg *sspp,
		const struct sde_dt_props *props, const char *name,
		struct sde_pp_blk *blk, u32 type, u32 prop, bool versioned)
{
	blk->id = type;
	blk->len = 0;
	set_bit(type, &sspp->features);
	blk->base = PROP_VALUE_ACCESS(props->values, prop, 0);
	blk->regdma_base = SSPP_GET_REGDMA_BASE(blk->base, sspp->sblk->top_off);
	snprintf(blk->name, SDE_HW_BLK_NAME_LEN, "%s%u", name,
			sspp->id - SSPP_DMA0);
	if (versioned)
		blk->version = PROP_VALUE_ACCESS(props->values, prop, 1);
}

static int _sde_sspp_setup_dmas(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int i = 0, j;
	int rc = 0, dma_count = 0, dgm_count = 0;
	struct sde_dt_props *props[SSPP_SUBBLK_COUNT_MAX] = {NULL, NULL};
	struct sde_dt_props *props_tmp = NULL;
	struct device_node *snp = NULL;
	const char *type;

	snp = of_get_child_by_name(np, sspp_prop[SSPP_DMA_BLOCKS].prop_name);
	if (snp) {
		dgm_count = of_get_child_count(snp);
		if (dgm_count > 0) {
			struct device_node *dgm_snp;

			if (dgm_count > SSPP_SUBBLK_COUNT_MAX) {
				SDE_ERROR("too many dgm subblocks defined");
				goto end;
			}

			for_each_child_of_node(snp, dgm_snp) {
				props_tmp = sde_get_dt_props(dgm_snp,
						DMA_PROP_MAX, dma_prop,
						ARRAY_SIZE(dma_prop), NULL);
				if (IS_ERR(props_tmp)) {
					rc = PTR_ERR(props_tmp);
					props_tmp = NULL;
					goto end;
				} else if (!props_tmp->exists[DMA_SUBBLOCK_INDEX]) {
					SDE_ERROR("dgm sub-block index must be defined");
					goto end;
				}

				i = PROP_VALUE_ACCESS(props_tmp->values, DMA_SUBBLOCK_INDEX, 0);
				if (i >= SSPP_SUBBLK_COUNT_MAX) {
					SDE_ERROR("dgm sub-block index greater than max: %d", i);
					goto end;
				} else if (props[i] != NULL) {
					SDE_ERROR("dgm sub-block index already defined: %d", i);
					goto end;
				}

				props[i] = props_tmp;
				props_tmp = NULL;
			}
		}
	}

	for (i = 0; i < sde_cfg->sspp_count; ++i) {
		struct sde_sspp_cfg *sspp = sde_cfg->sspp + i;
		struct sde_sspp_sub_blks *sblk = sspp->sblk;

		of_property_read_string_index(np,
				sspp_prop[SSPP_TYPE].prop_name, i, &type);
		if (strcmp(type, "dma"))
			continue;

		sblk->maxupscale = SSPP_UNITY_SCALE;
		sblk->maxdwnscale = SSPP_UNITY_SCALE;
		sblk->format_list = sde_cfg->dma_formats;
		sblk->virt_format_list = sde_cfg->dma_formats;
		sspp->id = SSPP_DMA0 + dma_count;
		sspp->clk_ctrl = SDE_CLK_CTRL_DMA0 + dma_count;
		snprintf(sspp->name, SDE_HW_BLK_NAME_LEN, "sspp_%u",
				sspp->id - SSPP_VIG0);
		sspp->type = SSPP_TYPE_DMA;
		set_bit(SDE_PERF_SSPP_QOS, &sspp->perf_features);
		if (sde_cfg->vbif_qos_nlvl == 8)
			set_bit(SDE_PERF_SSPP_QOS_8LVL, &sspp->perf_features);
		dma_count++;

		/* Obtain sub block top, or maintain backwards compatibility */
		if (props[0] && props[0]->exists[DMA_TOP_OFF])
			sblk->top_off = PROP_VALUE_ACCESS(props[0]->values, DMA_TOP_OFF, 0);
		else
			sblk->top_off = 0x200;

		sblk->num_igc_blk = dgm_count;
		sblk->num_gc_blk = dgm_count;
		sblk->num_dgm_csc_blk = dgm_count;
		for (j = 0; j < SSPP_SUBBLK_COUNT_MAX; j++) {
			if (props[j] == NULL)
				continue;

			if (props[j]->exists[DMA_IGC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
					"sspp_dma_igc", &sblk->igc_blk[j],
					SDE_SSPP_DMA_IGC, DMA_IGC_PROP, true);

			if (props[j]->exists[DMA_GC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
					"sspp_dma_gc", &sblk->gc_blk[j],
					SDE_SSPP_DMA_GC, DMA_GC_PROP, true);

			if (PROP_VALUE_ACCESS(props[j]->values,
					DMA_DGM_INVERSE_PMA, 0)) {
				set_bit(SDE_SSPP_DGM_INVERSE_PMA,
						&sspp->features);
				if (sde_cfg->hw_rev >= SDE_HW_VER_810)
					sblk->unmult_offset[j] = SDE_DGM_UNMULT_2 + j*0x1000;
				else
					sblk->unmult_offset[j] = SDE_DGM_UNMULT + j*0x1000;
			}

			if (props[j]->exists[DMA_CSC_OFF])
				_sde_sspp_setup_dgm(sspp, props[j],
					"sspp_dgm_csc", &sblk->dgm_csc_blk[j],
					SDE_SSPP_DGM_CSC, DMA_CSC_OFF, false);

			if (props[j]->exists[DMA_FP16_IGC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_fp16_igc",
						&sblk->fp16_igc_blk[j],
						SDE_SSPP_FP16_IGC,
						DMA_FP16_IGC_PROP, true);

			if (props[j]->exists[DMA_FP16_GC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_fp16_gc",
						&sblk->fp16_gc_blk[j],
						SDE_SSPP_FP16_GC,
						DMA_FP16_GC_PROP, true);

			if (props[j]->exists[DMA_FP16_CSC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_fp16_csc",
						&sblk->fp16_csc_blk[j],
						SDE_SSPP_FP16_CSC,
						DMA_FP16_CSC_PROP, true);

			if (props[j]->exists[DMA_FP16_UNMULT_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_fp16_unmult",
						&sblk->fp16_unmult_blk[j],
						SDE_SSPP_FP16_UNMULT,
						DMA_FP16_UNMULT_PROP, true);

			if (props[j]->exists[DMA_UCSC_IGC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_ucsc_igc",
						&sblk->ucsc_igc_blk[j],
						SDE_SSPP_UCSC_IGC,
						DMA_UCSC_IGC_PROP, true);

			if (props[j]->exists[DMA_UCSC_GC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_ucsc_gc",
						&sblk->ucsc_gc_blk[j],
						SDE_SSPP_UCSC_GC,
						DMA_UCSC_GC_PROP, true);

			if (props[j]->exists[DMA_UCSC_CSC_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_ucsc_csc",
						&sblk->ucsc_csc_blk[j],
						SDE_SSPP_UCSC_CSC,
						DMA_UCSC_CSC_PROP, true);

			if (props[j]->exists[DMA_UCSC_UNMULT_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_ucsc_unmult",
						&sblk->ucsc_unmult_blk[j],
						SDE_SSPP_UCSC_UNMULT,
						DMA_UCSC_UNMULT_PROP, true);

			if (props[j]->exists[DMA_UCSC_ALPHA_DITHER_PROP])
				_sde_sspp_setup_dgm(sspp, props[j],
						"sspp_dma_ucsc_alpha_dither",
						&sblk->ucsc_alpha_dither_blk[j],
						SDE_SSPP_UCSC_ALPHA_DITHER,
						DMA_UCSC_ALPHA_DITHER_PROP, true);
		}
	}

end:
	for (i = 0; i < SSPP_SUBBLK_COUNT_MAX; i++)
		sde_put_dt_props(props[i]);
	sde_put_dt_props(props_tmp);

	return rc;
}

static void sde_sspp_set_features(struct sde_mdss_cfg *sde_cfg,
		const struct sde_dt_props *props)
{
	int i;

	for (i = 0; i < sde_cfg->sspp_count; ++i) {
		struct sde_sspp_cfg *sspp = sde_cfg->sspp + i;
		struct sde_sspp_sub_blks *sblk = sspp->sblk;

		sblk->maxlinewidth = sde_cfg->max_sspp_linewidth;

		if (sde_cfg->has_line_insertion)
			set_bit(SDE_SSPP_LINE_INSERTION, &sspp->features);
		sblk->smart_dma_priority =
			PROP_VALUE_ACCESS(props->values, SSPP_SMART_DMA, i);
		if (sblk->smart_dma_priority && sde_cfg->smart_dma_rev)
			set_bit(sde_cfg->smart_dma_rev, &sspp->features);

		sblk->src_blk.id = SDE_SSPP_SRC;
		set_bit(SDE_SSPP_SRC, &sspp->features);

		if (test_bit(SDE_FEATURE_CDP, sde_cfg->features))
			set_bit(SDE_PERF_SSPP_CDP, &sspp->perf_features);

		if (sde_cfg->ts_prefill_rev == 1) {
			set_bit(SDE_PERF_SSPP_TS_PREFILL, &sspp->perf_features);
		} else if (sde_cfg->ts_prefill_rev == 2) {
			set_bit(SDE_PERF_SSPP_TS_PREFILL, &sspp->perf_features);
			set_bit(SDE_PERF_SSPP_TS_PREFILL_REC1,
					&sspp->perf_features);
		}

		if (sde_cfg->uidle_cfg.uidle_rev) {
			set_bit(SDE_PERF_SSPP_UIDLE, &sspp->perf_features);
			if (sde_cfg->uidle_cfg.uidle_rev >= SDE_UIDLE_VERSION_1_0_3)
				set_bit(SDE_PERF_SSPP_UIDLE_FILL_LVL_SCALE, &sspp->perf_features);
		}

		if (test_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map))
			set_bit(SDE_PERF_SSPP_SYS_CACHE, &sspp->perf_features);

		if (test_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features))
			set_bit(SDE_SSPP_MULTIRECT_ERROR, &sspp->features);

		if (test_bit(SDE_FEATURE_DECIMATION, sde_cfg->features)) {
			sblk->maxhdeciexp = MAX_HORZ_DECIMATION;
			sblk->maxvdeciexp = MAX_VERT_DECIMATION;
		} else {
			sblk->maxhdeciexp = 0;
			sblk->maxvdeciexp = 0;
		}

		sblk->pixel_ram_size = DEFAULT_PIXEL_RAM_SIZE;

		if (PROP_VALUE_ACCESS(props->values, SSPP_EXCL_RECT, i) == 1)
			set_bit(SDE_SSPP_EXCL_RECT, &sspp->features);

		if (props->exists[SSPP_MAX_PER_PIPE_BW])
			sblk->max_per_pipe_bw = PROP_VALUE_ACCESS(props->values,
					SSPP_MAX_PER_PIPE_BW, i);
		else
			sblk->max_per_pipe_bw = DEFAULT_MAX_PER_PIPE_BW;

		if (props->exists[SSPP_MAX_PER_PIPE_BW_HIGH])
			sblk->max_per_pipe_bw_high =
				PROP_VALUE_ACCESS(props->values,
				SSPP_MAX_PER_PIPE_BW_HIGH, i);
		else
			sblk->max_per_pipe_bw_high = sblk->max_per_pipe_bw;

		if (test_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features))
			set_bit(SDE_SSPP_UBWC_STATS, &sspp->features);

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_900))
			set_bit(SDE_SSPP_SCALER_DE_LPF_BLEND, &sspp->features);
	}
}

static int _sde_sspp_setup_cmn(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0, off_count, i, j;
	struct sde_dt_props *props;
	struct sde_sspp_cfg *sspp;
	struct sde_sspp_sub_blks *sblk;

	props = sde_get_dt_props(np, SSPP_PROP_MAX, sspp_prop,
			ARRAY_SIZE(sspp_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	if (off_count > MAX_BLOCKS) {
		SDE_ERROR("%d off_count exceeds MAX_BLOCKS, limiting to %d\n",
				off_count, MAX_BLOCKS);
		off_count = MAX_BLOCKS;
	}
	sde_cfg->sspp_count = off_count;

	/* create all sub blocks before populating them */
	for (i = 0; i < off_count; i++) {
		sspp = sde_cfg->sspp + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		sspp->sblk = sblk;
	}

	sde_sspp_set_features(sde_cfg, props);

	for (i = 0; i < off_count; i++) {
		sspp = sde_cfg->sspp + i;
		sblk = sspp->sblk;

		sspp->base = PROP_VALUE_ACCESS(props->values, SSPP_OFF, i);
		sspp->len = PROP_VALUE_ACCESS(props->values, SSPP_SIZE, 0);

		snprintf(sblk->src_blk.name, SDE_HW_BLK_NAME_LEN, "sspp_src_%u",
				sspp->id - SSPP_VIG0);

		if (sspp->clk_ctrl >= SDE_CLK_CTRL_MAX) {
			SDE_ERROR("%s: invalid clk ctrl: %d\n",
					sblk->src_blk.name, sspp->clk_ctrl);
			rc = -EINVAL;
			goto end;
		}

		sspp->xin_id = PROP_VALUE_ACCESS(props->values, SSPP_XIN, i);
		sblk->src_blk.len = PROP_VALUE_ACCESS(props->values, SSPP_SIZE,
				0);

		if (!test_bit(SDE_FEATURE_VBIF_CLK_SPLIT, sde_cfg->features)) {
			for (j = 0; j < sde_cfg->mdp_count; j++) {
				sde_cfg->mdp[j].clk_ctrls[sspp->clk_ctrl].reg_off =
						PROP_BITVALUE_ACCESS(props->values,
						SSPP_CLK_CTRL, i, 0);
				sde_cfg->mdp[j].clk_ctrls[sspp->clk_ctrl].bit_off =
						PROP_BITVALUE_ACCESS(props->values,
						SSPP_CLK_CTRL, i, 1);
				sde_cfg->mdp[j].clk_status[sspp->clk_ctrl].reg_off =
						PROP_BITVALUE_ACCESS(props->values,
						SSPP_CLK_STATUS, i, 0);
				sde_cfg->mdp[j].clk_status[sspp->clk_ctrl].bit_off =
						PROP_BITVALUE_ACCESS(props->values,
						SSPP_CLK_STATUS, i, 1);
			}

			SDE_DEBUG("xin:%d ram:%d clk%d:%x/%d\n",
					sspp->xin_id, sblk->pixel_ram_size, sspp->clk_ctrl,
					sde_cfg->mdp[0].clk_ctrls[sspp->clk_ctrl].reg_off,
					sde_cfg->mdp[0].clk_ctrls[sspp->clk_ctrl].bit_off);
		}
	}

end:
	sde_put_dt_props(props);
	return rc;
}

static int sde_sspp_parse_dt(struct device_node *np,
	struct sde_mdss_cfg *sde_cfg)
{
	int rc;

	rc = _sde_sspp_setup_cmn(np, sde_cfg);
	if (rc)
		return rc;

	rc = _sde_sspp_setup_vigs(np, sde_cfg);
	if (rc)
		return rc;

	rc = _sde_sspp_setup_dmas(np, sde_cfg);

	return rc;
}

static int sde_ctl_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int i;
	struct sde_dt_props *props;
	struct sde_ctl_cfg *ctl;
	u32 off_count;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument input param\n");
		return -EINVAL;
	}

	props = sde_get_dt_props(np, HW_PROP_MAX, ctl_prop,
			ARRAY_SIZE(ctl_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	sde_cfg->ctl_count = off_count;

	for (i = 0; i < off_count; i++) {
		const char *disp_pref = NULL;

		ctl = sde_cfg->ctl + i;
		ctl->base = PROP_VALUE_ACCESS(props->values, HW_OFF, i);
		ctl->len = PROP_VALUE_ACCESS(props->values, HW_LEN, 0);
		ctl->id = CTL_0 + i;
		snprintf(ctl->name, SDE_HW_BLK_NAME_LEN, "ctl_%u",
				ctl->id - CTL_0);

		of_property_read_string_index(np,
				ctl_prop[HW_DISP].prop_name, i, &disp_pref);
		if (disp_pref && !strcmp(disp_pref, "primary"))
			set_bit(SDE_CTL_PRIMARY_PREF, &ctl->features);
		if ((i < MAX_SPLIT_DISPLAY_CTL) &&
			!(IS_SDE_CTL_REV_100(sde_cfg->ctl_rev)))
			set_bit(SDE_CTL_SPLIT_DISPLAY, &ctl->features);
		if (i < MAX_PP_SPLIT_DISPLAY_CTL)
			set_bit(SDE_CTL_PINGPONG_SPLIT, &ctl->features);
		if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
			set_bit(SDE_CTL_ACTIVE_CFG, &ctl->features);
		if (SDE_UIDLE_MAJOR(sde_cfg->uidle_cfg.uidle_rev) &&
				sde_cfg->uidle_cfg.uidle_rev < SDE_UIDLE_VERSION_1_0_4)
			set_bit(SDE_CTL_UIDLE, &ctl->features);
		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_700))
			set_bit(SDE_CTL_UNIFIED_DSPP_FLUSH, &ctl->features);
	}

	sde_put_dt_props(props);
	return 0;
}

u32 sde_hw_mixer_set_preference(struct sde_mdss_cfg *sde_cfg, u32 num_lm,
		uint32_t disp_type)
{
	u32 i, cnt = 0, sec_cnt = 0, lm_mask = 0;

	if (disp_type == SDE_CONNECTOR_PRIMARY) {
		for (i = 0; i < sde_cfg->mixer_count; i++) {
			/* Check if lm was previously set for secondary */
			/* Clear pref, primary has higher priority */
			if (sde_cfg->mixer[i].features &
					BIT(SDE_DISP_SECONDARY_PREF)) {
				clear_bit(SDE_DISP_SECONDARY_PREF,
						&sde_cfg->mixer[i].features);
				sec_cnt++;
			}
			clear_bit(SDE_DISP_PRIMARY_PREF,
					&sde_cfg->mixer[i].features);

			/* Set lm for primary pref */
			if (cnt < num_lm) {
				set_bit(SDE_DISP_PRIMARY_PREF,
						&sde_cfg->mixer[i].features);
				lm_mask |=  BIT(sde_cfg->mixer[i].id - 1);
				cnt++;
			}

			/*
			 * When all primary prefs have been set,
			 * and if 2 lms are required for secondary
			 * preference must be set with an lm pair
			 */
			if (cnt == num_lm && sec_cnt > 1 &&
					!test_bit(sde_cfg->mixer[i+1].id,
					&sde_cfg->mixer[i].lm_pair_mask))
				continue;

			/* After primary pref is set, now re apply secondary */
			if (cnt >= num_lm && cnt < (num_lm + sec_cnt)) {
				set_bit(SDE_DISP_SECONDARY_PREF,
						&sde_cfg->mixer[i].features);
				cnt++;
			}
		}
	} else if (disp_type == SDE_CONNECTOR_SECONDARY) {
		for (i = 0; i < sde_cfg->mixer_count; i++) {
			clear_bit(SDE_DISP_SECONDARY_PREF,
					&sde_cfg->mixer[i].features);

			/*
			 * If 2 lms are required for secondary
			 * preference must be set with an lm pair
			 */
			if (cnt == 0 && num_lm > 1 &&
					!test_bit(sde_cfg->mixer[i+1].id,
					&sde_cfg->mixer[i].lm_pair_mask))
				continue;

			if (cnt < num_lm && !(sde_cfg->mixer[i].features &
					BIT(SDE_DISP_PRIMARY_PREF))) {
				set_bit(SDE_DISP_SECONDARY_PREF,
						&sde_cfg->mixer[i].features);
				lm_mask |= BIT(sde_cfg->mixer[i].id - 1);
				cnt++;
			}
		}
	}

	return lm_mask;
}

u64 sde_get_ddr_type(void)
{
	u64 ret = -EINVAL;
	u64 ddr_type;
	struct device_node *root_node = NULL;
	struct device_node *mem_node = NULL;

	root_node = of_find_node_by_path("/");

	if (root_node == NULL) {
		SDE_ERROR("Unable to find root node");
		goto error;
	}

	do {
		mem_node = of_get_next_child(root_node, mem_node);
		if (of_node_name_prefix(mem_node, "memory")) {
			SDE_DEBUG("memory node found with full name %s",
					mem_node->full_name);
			break;
		}
	} while (mem_node != NULL);

	of_node_put(root_node);
	if (mem_node == NULL) {
		SDE_ERROR("memory node not found");
		goto error;
	}

	ret = of_property_read_u64(mem_node, "ddr_device_type", &ddr_type);

	of_node_put(mem_node);
	if (ret < 0) {
		SDE_ERROR("ddr_device_type read failed");
		goto error;
	}

	SDE_DEBUG("DDR Type %lld", ddr_type);
	return ddr_type;
error:
	return ret;

}

static int sde_mixer_parse_dt(struct device_node *np, struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0, i, j;
	u32 off_count, blend_off_count, max_blendstages, lm_pair_mask;
	struct sde_lm_cfg *mixer;
	struct sde_lm_sub_blks *sblk;
	int pp_count, dspp_count, ds_count, mixer_count;
	u32 pp_idx, dspp_idx, ds_idx, merge_3d_idx;
	u32 mixer_base;
	struct device_node *snp = NULL;
	struct sde_dt_props *props, *blend_props, *blocks_props = NULL;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument input param\n");
		return -EINVAL;
	}
	max_blendstages = sde_cfg->max_mixer_blendstages;

	props = sde_get_dt_props(np, MIXER_PROP_MAX, mixer_prop,
			ARRAY_SIZE(mixer_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	pp_count = sde_cfg->pingpong_count;
	dspp_count = sde_cfg->dspp_count;
	ds_count = sde_cfg->ds_count;

	/* get mixer feature dt properties if they exist */
	snp = of_get_child_by_name(np, mixer_prop[MIXER_BLOCKS].prop_name);
	if (snp) {
		blocks_props = sde_get_dt_props(snp, MIXER_PROP_MAX,
				mixer_blocks_prop,
				ARRAY_SIZE(mixer_blocks_prop), NULL);
		if (IS_ERR(blocks_props)) {
			rc = PTR_ERR(blocks_props);
			goto put_props;
		}
	}

	/* get the blend_op register offsets */
	blend_props = sde_get_dt_props(np, MIXER_BLEND_PROP_MAX,
			mixer_blend_prop, ARRAY_SIZE(mixer_blend_prop),
			&blend_off_count);
	if (IS_ERR(blend_props)) {
		rc = PTR_ERR(blend_props);
		goto put_blocks;
	}

	for (i = 0, mixer_count = 0, pp_idx = 0, dspp_idx = 0, ds_idx = 0,
			merge_3d_idx = 0; i < off_count; i++) {
		const char *disp_pref = NULL;
		const char *cwb_pref = NULL;
		const char *dcwb_pref = NULL;
		u32 dummy_mixer_base = 0x0f0f;

		mixer_base = PROP_VALUE_ACCESS(props->values, MIXER_OFF, i);
		if (!mixer_base)
			continue;

		mixer = sde_cfg->mixer + mixer_count;

		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		mixer->sblk = sblk;

		mixer->base = mixer_base;
		mixer->len = !props->exists[MIXER_LEN] ?
				DEFAULT_SDE_HW_BLOCK_LEN :
				PROP_VALUE_ACCESS(props->values, MIXER_LEN, 0);
		mixer->id = LM_0 + i;
		snprintf(mixer->name, SDE_HW_BLK_NAME_LEN, "lm_%u",
				mixer->id - LM_0);

		lm_pair_mask = PROP_VALUE_ACCESS(props->values,
				MIXER_PAIR_MASK, i);
		if (lm_pair_mask)
			mixer->lm_pair_mask = 1 << lm_pair_mask;

		sblk->maxblendstages = max_blendstages;
		sblk->maxwidth = sde_cfg->max_mixer_width;

		for (j = 0; j < blend_off_count; j++)
			sblk->blendstage_base[j] =
				PROP_VALUE_ACCESS(blend_props->values,
						MIXER_BLEND_OP_OFF, j);

		if (test_bit(SDE_FEATURE_SRC_SPLIT, sde_cfg->features))
			set_bit(SDE_MIXER_SOURCESPLIT, &mixer->features);
		if (test_bit(SDE_FEATURE_DIM_LAYER, sde_cfg->features))
			set_bit(SDE_DIM_LAYER, &mixer->features);
		if (test_bit(SDE_FEATURE_COMBINED_ALPHA, sde_cfg->features))
			set_bit(SDE_MIXER_COMBINED_ALPHA, &mixer->features);

		of_property_read_string_index(np,
			mixer_prop[MIXER_DISP].prop_name, i, &disp_pref);
		if (disp_pref && !strcmp(disp_pref, "primary"))
			set_bit(SDE_DISP_PRIMARY_PREF, &mixer->features);

		of_property_read_string_index(np,
			mixer_prop[MIXER_CWB].prop_name, i, &cwb_pref);
		if (cwb_pref && !strcmp(cwb_pref, "cwb"))
			set_bit(SDE_DISP_CWB_PREF, &mixer->features);

		of_property_read_string_index(np,
			mixer_prop[MIXER_DCWB].prop_name, i, &dcwb_pref);
		if (dcwb_pref && !strcmp(dcwb_pref, "dcwb")) {
			set_bit(SDE_DISP_DCWB_PREF, &mixer->features);
			if (mixer->base == dummy_mixer_base) {
				mixer->base = 0x0;
				mixer->len = 0;
				mixer->dummy_mixer = true;
			}
		}

		if (BIT(mixer->id - LM_0) & sde_cfg->cwb_virtual_mixers_mask)
			set_bit(SDE_MIXER_IS_VIRTUAL, &mixer->features);

		mixer->pingpong = pp_count > 0 ? pp_idx + PINGPONG_0
							: PINGPONG_MAX;
		mixer->dspp = dspp_count > 0 ? dspp_idx + DSPP_0
							: DSPP_MAX;
		mixer->ds = ds_count > 0 ? ds_idx + DS_0 : DS_MAX;
		mixer->merge_3d = merge_3d_idx + MERGE_3D_0;
		pp_count--;
		dspp_count--;
		ds_count--;
		pp_idx++;
		dspp_idx++;
		ds_idx++;

		mixer_count++;
		/*
		 * Since each 3dmux is assigned to a pair of LM,
		 * increment this idx only at even LM counts
		 */
		if ((mixer_count & 1) == 0)
			merge_3d_idx++;

		sblk->gc.id = SDE_MIXER_GC;
		if (blocks_props && blocks_props->exists[MIXER_GC_PROP]) {
			sblk->gc.base = PROP_VALUE_ACCESS(blocks_props->values,
					MIXER_GC_PROP, 0);
			sblk->gc.version = PROP_VALUE_ACCESS(
					blocks_props->values, MIXER_GC_PROP,
					1);
			sblk->gc.len = 0;
			set_bit(SDE_MIXER_GC, &mixer->features);
		}
	}
	sde_cfg->mixer_count = mixer_count;
	_sde_lm_noise_parse_dt(np, sde_cfg);

end:
	sde_put_dt_props(blend_props);
put_blocks:
	sde_put_dt_props(blocks_props);
put_props:
	sde_put_dt_props(props);
	return rc;
}

static int sde_intf_parse_dt(struct device_node *np,
						struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[INTF_PROP_MAX], i;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[INTF_PROP_MAX];
	u32 off_count;
	u32 dsi_count = 0, none_count = 0, hdmi_count = 0, dp_count = 0;
	const char *type;
	struct sde_intf_cfg *intf;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(INTF_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, intf_prop, ARRAY_SIZE(intf_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	sde_cfg->intf_count = off_count;

	rc = _read_dt_entry(np, intf_prop, ARRAY_SIZE(intf_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		intf = sde_cfg->intf + i;
		intf->base = PROP_VALUE_ACCESS(prop_value, INTF_OFF, i);
		intf->len = PROP_VALUE_ACCESS(prop_value, INTF_LEN, 0);
		intf->id = INTF_0 + i;
		snprintf(intf->name, SDE_HW_BLK_NAME_LEN, "intf_%u",
				intf->id - INTF_0);

		if (!prop_exists[INTF_LEN])
			intf->len = DEFAULT_SDE_HW_BLOCK_LEN;

		rc = _add_to_irq_offset_list(sde_cfg, SDE_INTR_HWBLK_INTF,
				intf->id, intf->base);
		if (rc)
			goto end;

		intf->prog_fetch_lines_worst_case =
				!prop_exists[INTF_PREFETCH] ?
				sde_cfg->perf.min_prefill_lines :
				PROP_VALUE_ACCESS(prop_value, INTF_PREFETCH, i);

		of_property_read_string_index(np,
				intf_prop[INTF_TYPE].prop_name, i, &type);
		if (!strcmp(type, "dsi")) {
			intf->type = INTF_DSI;
			intf->controller_id = dsi_count;
			dsi_count++;
		} else if (!strcmp(type, "hdmi")) {
			intf->type = INTF_HDMI;
			intf->controller_id = hdmi_count;
			hdmi_count++;
		} else if (!strcmp(type, "dp")) {
			intf->type = INTF_DP;
			intf->controller_id = dp_count;
			dp_count++;
		} else {
			intf->type = INTF_NONE;
			intf->controller_id = none_count;
			none_count++;
		}
		if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
			set_bit(SDE_INTF_INPUT_CTRL, &intf->features);

		if (prop_exists[INTF_TE_IRQ])
			intf->te_irq_offset = PROP_VALUE_ACCESS(prop_value,
					INTF_TE_IRQ, i);

		if (intf->te_irq_offset) {
			rc = _add_to_irq_offset_list(sde_cfg,
					SDE_INTR_HWBLK_INTF_TEAR,
					intf->id, intf->te_irq_offset);
			if (rc)
				goto end;

			set_bit(SDE_INTF_TE, &intf->features);
		}

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_500))
			set_bit(SDE_INTF_STATUS, &intf->features);

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_700))
			set_bit(SDE_INTF_TE_ALIGN_VSYNC, &intf->features);

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_810)) {
			set_bit(SDE_INTF_WD_TIMER, &intf->features);
			set_bit(SDE_INTF_RESET_COUNTER, &intf->features);
			set_bit(SDE_INTF_PANEL_VSYNC_TS, &intf->features);
			set_bit(SDE_INTF_AVR_STATUS, &intf->features);
		}

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_900)) {
			set_bit(SDE_INTF_MDP_VSYNC_TS, &intf->features);
			set_bit(SDE_INTF_WD_JITTER, &intf->features);
		}

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_A00)) {
			set_bit(SDE_INTF_MDP_VSYNC_FC, &intf->features);
			set_bit(SDE_INTF_TE_32BIT, &intf->features);
			set_bit(SDE_INTF_TE_SINGLE_UPDATE, &intf->features);
			set_bit(SDE_INTF_WD_LTJ_CTL, &intf->features);
			set_bit(SDE_INTF_TE_DEASSERT_DETECT, &intf->features);
			set_bit(SDE_INTF_VSYNC_TS_SRC_EN, &intf->features);
			set_bit(SDE_INTF_TE_LEVEL_TRIGGER, &intf->features);
		}
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_wb_parse_dt(struct device_node *np, struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[WB_PROP_MAX], i, j;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[WB_PROP_MAX];
	u32 off_count, major_version;
	struct sde_wb_cfg *wb;
	struct sde_wb_sub_blocks *sblk;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(WB_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, wb_prop, ARRAY_SIZE(wb_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	sde_cfg->wb_count = off_count;

	rc = _read_dt_entry(np, wb_prop, ARRAY_SIZE(wb_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	major_version = SDE_HW_MAJOR(sde_cfg->hw_rev);
	for (i = 0; i < off_count; i++) {
		wb = sde_cfg->wb + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		wb->sblk = sblk;

		wb->base = PROP_VALUE_ACCESS(prop_value, WB_OFF, i);
		wb->id = WB_0 + PROP_VALUE_ACCESS(prop_value, WB_ID, i);
		snprintf(wb->name, SDE_HW_BLK_NAME_LEN, "wb_%u",
				wb->id - WB_0);
		wb->clk_ctrl = SDE_CLK_CTRL_WB0 +
			PROP_VALUE_ACCESS(prop_value, WB_ID, i);
		wb->xin_id = PROP_VALUE_ACCESS(prop_value, WB_XIN_ID, i);

		if (wb->clk_ctrl >= SDE_CLK_CTRL_MAX) {
			SDE_ERROR("%s: invalid clk ctrl: %d\n",
					wb->name, wb->clk_ctrl);
			rc = -EINVAL;
			goto end;
		}

		if (IS_SDE_MAJOR_MINOR_SAME((sde_cfg->hw_rev), SDE_HW_VER_170))
			wb->vbif_idx = VBIF_NRT;
		else
			wb->vbif_idx = VBIF_RT;

		wb->len = PROP_VALUE_ACCESS(prop_value, WB_LEN, 0);
		if (!prop_exists[WB_LEN])
			wb->len = DEFAULT_SDE_HW_BLOCK_LEN;
		sblk->maxlinewidth = sde_cfg->max_wb_linewidth;
		sblk->maxlinewidth_linear = sde_cfg->max_wb_linewidth_linear;

		if (wb->id >= LINE_MODE_WB_OFFSET)
			set_bit(SDE_WB_LINE_MODE, &wb->features);
		else
			set_bit(SDE_WB_BLOCK_MODE, &wb->features);

		if (test_bit(SDE_FEATURE_CDP, sde_cfg->features))
			set_bit(SDE_WB_CDP, &wb->features);

		if (sde_cfg->vbif_qos_nlvl == 8)
			set_bit(SDE_WB_QOS_8LVL, &wb->features);

		if (test_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features))
			set_bit(SDE_WB_UBWC, &wb->features);

		if (test_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features))
			set_bit(SDE_WB_CROP, &wb->features);

		if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
			set_bit(SDE_WB_INPUT_CTRL, &wb->features);

		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_900)) {
			set_bit(SDE_WB_PROG_LINE, &wb->features);
			set_bit(SDE_WB_SYS_CACHE, &wb->features);
		}

		rc = _add_to_irq_offset_list(sde_cfg, SDE_INTR_HWBLK_WB, wb->id, wb->base);

		if (test_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features)) {
			set_bit(SDE_WB_HAS_DCWB, &wb->features);
			if (test_bit(SDE_FEATURE_DUAL_DEDICATED_CWB, sde_cfg->features))
				set_bit(SDE_HW_HAS_DUAL_DCWB, &wb->features);
			if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
				set_bit(SDE_WB_DCWB_CTRL, &wb->features);
			if (major_version >= SDE_HW_MAJOR(SDE_HW_VER_A00)) {
				sde_cfg->cwb_blk_off[0] = 0x67200;
				sde_cfg->cwb_blk_off[1] = 0x7F200;
				sde_cfg->cwb_blk_stride = 0x400;
			} else if (major_version >= SDE_HW_MAJOR(SDE_HW_VER_900)) {
				sde_cfg->cwb_blk_off[0] = 0x67200;
				sde_cfg->cwb_blk_stride = 0x400;
			} else if (major_version >= SDE_HW_MAJOR(SDE_HW_VER_810)) {
				sde_cfg->cwb_blk_off[0] = 0x66A00;
				sde_cfg->cwb_blk_stride = 0x400;
			} else {
				sde_cfg->cwb_blk_off[0] = 0x83000;
				sde_cfg->cwb_blk_stride = 0x100;
			}

			if (test_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features))
				set_bit(SDE_WB_CWB_DITHER_CTRL, &wb->features);

		} else if (test_bit(SDE_FEATURE_CWB, sde_cfg->features)) {
			set_bit(SDE_WB_HAS_CWB, &wb->features);
			if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
				set_bit(SDE_WB_CWB_CTRL, &wb->features);
			if (major_version >= SDE_HW_MAJOR(SDE_HW_VER_700)) {
				sde_cfg->cwb_blk_off[0] = 0x6A200;
				sde_cfg->cwb_blk_stride = 0x1000;
			} else {
				sde_cfg->cwb_blk_off[0] = 0x83000;
				sde_cfg->cwb_blk_stride = 0x100;
			}
		}

		if (!test_bit(SDE_FEATURE_VBIF_CLK_SPLIT, sde_cfg->features)) {
			for (j = 0; j < sde_cfg->mdp_count; j++) {
				sde_cfg->mdp[j].clk_ctrls[wb->clk_ctrl].reg_off =
					PROP_BITVALUE_ACCESS(prop_value,
							WB_CLK_CTRL, i, 0);
				sde_cfg->mdp[j].clk_ctrls[wb->clk_ctrl].bit_off =
					PROP_BITVALUE_ACCESS(prop_value,
							WB_CLK_CTRL, i, 1);
				sde_cfg->mdp[j].clk_status[wb->clk_ctrl].reg_off =
					PROP_BITVALUE_ACCESS(prop_value,
							WB_CLK_STATUS, i, 0);
				sde_cfg->mdp[j].clk_status[wb->clk_ctrl].bit_off =
					PROP_BITVALUE_ACCESS(prop_value,
							WB_CLK_STATUS, i, 1);
			}

			SDE_DEBUG("wb:%d xin:%d vbif:%d clk%d:%x/%d\n", wb->id - WB_0,
					wb->xin_id, wb->vbif_idx, wb->clk_ctrl,
					sde_cfg->mdp[0].clk_ctrls[wb->clk_ctrl].reg_off,
					sde_cfg->mdp[0].clk_ctrls[wb->clk_ctrl].bit_off);
		}

		if (test_bit(SDE_FEATURE_WB_ROTATION, sde_cfg->features)) {
			set_bit(SDE_WB_LINEAR_ROTATION, &wb->features);
			wb->rot_format_list = sde_cfg->wb_rot_formats;
		}

		wb->format_list = sde_cfg->wb_formats;
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_dspp_top_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[DSPP_TOP_PROP_MAX];
	bool prop_exists[DSPP_TOP_PROP_MAX];
	struct sde_prop_value *prop_value = NULL;
	u32 off_count;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(DSPP_TOP_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dspp_top_prop, ARRAY_SIZE(dspp_top_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, dspp_top_prop, ARRAY_SIZE(dspp_top_prop),
		prop_count, prop_exists, prop_value);
	if (rc)
		goto end;

	if (off_count != 1) {
		SDE_ERROR("invalid dspp_top off_count:%d\n", off_count);
		rc = -EINVAL;
		goto end;
	}

	sde_cfg->dspp_top.base =
		PROP_VALUE_ACCESS(prop_value, DSPP_TOP_OFF, 0);
	sde_cfg->dspp_top.len =
		PROP_VALUE_ACCESS(prop_value, DSPP_TOP_SIZE, 0);
	snprintf(sde_cfg->dspp_top.name, SDE_HW_BLK_NAME_LEN, "dspp_top");

end:
	kfree(prop_value);
	return rc;
}

static int _sde_ad_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0;
	int off_count, i;
	struct sde_dt_props *props;

	props = sde_get_dt_props(np, AD_PROP_MAX, ad_prop,
			ARRAY_SIZE(ad_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	sde_cfg->ad_count = off_count;
	if (off_count > sde_cfg->dspp_count) {
		SDE_ERROR("limiting %d AD blocks to %d DSPP instances\n",
				off_count, sde_cfg->dspp_count);
		sde_cfg->ad_count = sde_cfg->dspp_count;
	}

	for (i = 0; i < sde_cfg->dspp_count; i++) {
		struct sde_dspp_cfg *dspp = &sde_cfg->dspp[i];
		struct sde_dspp_sub_blks *sblk = sde_cfg->dspp[i].sblk;

		sblk->ad.id = SDE_DSPP_AD;
		if (!props->exists[AD_OFF])
			continue;

		if (i < off_count) {
			sblk->ad.base = PROP_VALUE_ACCESS(props->values,
					AD_OFF, i);
			sblk->ad.version = PROP_VALUE_ACCESS(props->values,
					AD_VERSION, 0);
			set_bit(SDE_DSPP_AD, &dspp->features);
			rc = _add_to_irq_offset_list(sde_cfg,
					SDE_INTR_HWBLK_AD4, dspp->id,
					dspp->base + sblk->ad.base);
			if (rc)
				goto end;
		}
	}

end:
	sde_put_dt_props(props);
	return rc;
}

static int _sde_ltm_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0;
	int off_count, i;
	struct sde_dt_props *props;

	props = sde_get_dt_props(np, LTM_PROP_MAX, ltm_prop,
			ARRAY_SIZE(ltm_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	sde_cfg->ltm_count = off_count;
	if (off_count > sde_cfg->dspp_count) {
		SDE_ERROR("limiting %d LTM blocks to %d DSPP instances\n",
				off_count, sde_cfg->dspp_count);
		sde_cfg->ltm_count = sde_cfg->dspp_count;
	}

	for (i = 0; i < sde_cfg->dspp_count; i++) {
		struct sde_dspp_cfg *dspp = &sde_cfg->dspp[i];
		struct sde_dspp_sub_blks *sblk = sde_cfg->dspp[i].sblk;

		sblk->ltm.id = SDE_DSPP_LTM;
		if (!props->exists[LTM_OFF])
			continue;

		if (i < off_count) {
			sblk->ltm.base = PROP_VALUE_ACCESS(props->values,
					LTM_OFF, i);
			sblk->ltm.version = PROP_VALUE_ACCESS(props->values,
					LTM_VERSION, 0);
			set_bit(SDE_DSPP_LTM, &dspp->features);
			rc = _add_to_irq_offset_list(sde_cfg,
					SDE_INTR_HWBLK_LTM, dspp->id,
					dspp->base + sblk->ltm.base);
			if (rc)
				goto end;
		}
	}

end:
	sde_put_dt_props(props);
	return rc;
}

static int _sde_dspp_demura_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int off_count, i;
	struct sde_dt_props *props;
	struct sde_dspp_cfg *dspp;
	struct sde_dspp_sub_blks *sblk;

	props = sde_get_dt_props(np, DEMURA_PROP_MAX, demura_prop,
			ARRAY_SIZE(demura_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	sde_cfg->demura_count = off_count;
	if (off_count > sde_cfg->dspp_count) {
		SDE_ERROR("limiting %d demura blocks to %d DSPP instances\n",
				off_count, sde_cfg->dspp_count);
		sde_cfg->demura_count = sde_cfg->dspp_count;
	}

	for (i = 0; i < sde_cfg->dspp_count; i++) {
		dspp = &sde_cfg->dspp[i];
		sblk = sde_cfg->dspp[i].sblk;

		sblk->demura.id = SDE_DSPP_DEMURA;
		if (props->exists[DEMURA_OFF] && i < off_count) {
			sblk->demura.base = PROP_VALUE_ACCESS(props->values,
					DEMURA_OFF, i);
			sblk->demura.len = PROP_VALUE_ACCESS(props->values,
					DEMURA_LEN, 0);
			sblk->demura.version = PROP_VALUE_ACCESS(props->values,
					DEMURA_VERSION, 0);
			set_bit(SDE_DSPP_DEMURA, &dspp->features);
		}
	}

	sde_put_dt_props(props);
	return 0;
}

static int _sde_dspp_spr_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int off_count, i;
	struct sde_dt_props *props;
	struct sde_dspp_cfg *dspp;
	struct sde_dspp_sub_blks *sblk;

	props = sde_get_dt_props(np, SPR_PROP_MAX, spr_prop,
			ARRAY_SIZE(spr_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	sde_cfg->spr_count = off_count;
	if (off_count > sde_cfg->dspp_count) {
		SDE_ERROR("limiting %d spr blocks to %d DSPP instances\n",
				off_count, sde_cfg->dspp_count);
		sde_cfg->spr_count = sde_cfg->dspp_count;
	}

	for (i = 0; i < sde_cfg->dspp_count; i++) {
		dspp = &sde_cfg->dspp[i];
		sblk = sde_cfg->dspp[i].sblk;

		sblk->spr.id = SDE_DSPP_SPR;
		if (props->exists[SPR_OFF] && i < off_count) {
			sblk->spr.base = PROP_VALUE_ACCESS(props->values,
					SPR_OFF, i);
			sblk->spr.len = PROP_VALUE_ACCESS(props->values,
					SPR_LEN, 0);
			sblk->spr.version = PROP_VALUE_ACCESS(props->values,
					SPR_VERSION, 0);
			set_bit(SDE_DSPP_SPR, &dspp->features);
		}
	}

	sde_put_dt_props(props);
	return 0;
}

static int _sde_rc_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int off_count, i;
	struct sde_dt_props *props;

	props = sde_get_dt_props(np, RC_PROP_MAX, rc_prop,
			ARRAY_SIZE(rc_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	sde_cfg->rc_count = off_count;
	if (off_count > sde_cfg->dspp_count) {
		SDE_ERROR("limiting %d RC blocks to %d DSPP instances\n",
				off_count, sde_cfg->dspp_count);
		sde_cfg->rc_count = sde_cfg->dspp_count;
	}

	for (i = 0; i < sde_cfg->dspp_count; i++) {
		struct sde_dspp_cfg *dspp = &sde_cfg->dspp[i];
		struct sde_dspp_sub_blks *sblk = sde_cfg->dspp[i].sblk;

		sblk->rc.id = SDE_DSPP_RC;
		if (!props->exists[RC_OFF])
			continue;

		if (i < off_count) {
			sblk->rc.base = PROP_VALUE_ACCESS(props->values,
					RC_OFF, i);
			sblk->rc.len = PROP_VALUE_ACCESS(props->values,
					RC_LEN, 0);
			sblk->rc.version = PROP_VALUE_ACCESS(props->values,
					RC_VERSION, 0);
			sblk->rc.mem_total_size = PROP_VALUE_ACCESS(
					props->values, RC_MEM_TOTAL_SIZE, 0);

			if (!props->exists[RC_MIN_REGION_WIDTH])
				sblk->rc.min_region_width = 4;
			else
				sblk->rc.min_region_width = PROP_VALUE_ACCESS(
						props->values, RC_MIN_REGION_WIDTH, 0);

			sblk->rc.idx = i;
			set_bit(SDE_DSPP_RC, &dspp->features);
		}
	}

	sde_put_dt_props(props);
	return 0;
}

static int _sde_lm_noise_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int off_count, i;
	struct sde_dt_props *props;

	props = sde_get_dt_props(np, NOISEL_LAYER_PROP_MAX, noise_layer_prop,
			ARRAY_SIZE(noise_layer_prop), &off_count);
	if (IS_ERR(props)) {
		SDE_ERROR("noise: failed to get dt props\n");
		return PTR_ERR(props);
	}

	if (!props->exists[NOISE_LAYER_OFF] ||
		!props->exists[NOISE_LAYER_VERSION]) {
		SDE_INFO("noise: prop doesnt exist %d %d\n",
			props->exists[NOISE_LAYER_OFF],
			props->exists[NOISE_LAYER_VERSION]);
		goto exit;
	}

	for (i = 0; i < sde_cfg->mixer_count; i++) {
		struct sde_lm_cfg *lm = &sde_cfg->mixer[i];
		struct sde_lm_sub_blks *sblk = lm->sblk;
		sblk->nlayer.base = PROP_VALUE_ACCESS(props->values,
					NOISE_LAYER_OFF, 0);
		sblk->nlayer.version = PROP_VALUE_ACCESS(props->values,
					NOISE_LAYER_VERSION, 0);
		sblk->nlayer.len = sizeof(u32);
		set_bit(SDE_MIXER_NOISE_LAYER, &lm->features);
	}
exit:
	sde_put_dt_props(props);
	return 0;
}

static void _sde_init_dspp_sblk(struct sde_dspp_cfg *dspp,
		struct sde_pp_blk *pp_blk, int prop_id, int blk_id,
		struct sde_dt_props *props)
{
	pp_blk->id = prop_id;
	if (props->exists[blk_id]) {
		pp_blk->base = PROP_VALUE_ACCESS(props->values,
				blk_id, 0);
		pp_blk->version = PROP_VALUE_ACCESS(props->values,
				blk_id, 1);
		pp_blk->len = 0;
		set_bit(prop_id, &dspp->features);
	}
}

static int _sde_dspp_sblks_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int i;
	struct device_node *snp = NULL;
	struct sde_dt_props *props;

	snp = of_get_child_by_name(np, dspp_prop[DSPP_BLOCKS].prop_name);
	if (!snp)
		return 0;

	props = sde_get_dt_props(snp, DSPP_BLOCKS_PROP_MAX,
			dspp_blocks_prop, ARRAY_SIZE(dspp_blocks_prop),
			NULL);
	if (IS_ERR(props))
		return PTR_ERR(props);

	for (i = 0; i < sde_cfg->dspp_count; i++) {
		struct sde_dspp_cfg *dspp = &sde_cfg->dspp[i];
		struct sde_dspp_sub_blks *sblk = sde_cfg->dspp[i].sblk;

		_sde_init_dspp_sblk(dspp, &sblk->igc, SDE_DSPP_IGC,
				DSPP_IGC_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->pcc, SDE_DSPP_PCC,
				DSPP_PCC_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->gc, SDE_DSPP_GC,
				DSPP_GC_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->gamut, SDE_DSPP_GAMUT,
				DSPP_GAMUT_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->dither, SDE_DSPP_DITHER,
				DSPP_DITHER_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->hist, SDE_DSPP_HIST,
				DSPP_HIST_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->hsic, SDE_DSPP_HSIC,
				DSPP_HSIC_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->memcolor, SDE_DSPP_MEMCOLOR,
				DSPP_MEMCOLOR_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->sixzone, SDE_DSPP_SIXZONE,
				DSPP_SIXZONE_PROP, props);

		_sde_init_dspp_sblk(dspp, &sblk->vlut, SDE_DSPP_VLUT,
				DSPP_VLUT_PROP, props);
	}

	sde_put_dt_props(props);
	return 0;
}

static int _sde_dspp_cmn_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0;
	int i, off_count;
	struct sde_dt_props *props;
	struct sde_dspp_sub_blks *sblk;

	props = sde_get_dt_props(np, DSPP_PROP_MAX, dspp_prop,
			ARRAY_SIZE(dspp_prop), &off_count);
	if (IS_ERR(props))
		return PTR_ERR(props);

	if (off_count > MAX_BLOCKS) {
		SDE_ERROR("off_count %d exceeds MAX_BLOCKS, limiting to %d\n",
				off_count, MAX_BLOCKS);
		off_count = MAX_BLOCKS;
	}

	sde_cfg->dspp_count = off_count;
	for (i = 0; i < sde_cfg->dspp_count; i++) {
		sde_cfg->dspp[i].base = PROP_VALUE_ACCESS(props->values,
				DSPP_OFF, i);
		sde_cfg->dspp[i].len = PROP_VALUE_ACCESS(props->values,
				DSPP_SIZE, 0);
		sde_cfg->dspp[i].id = DSPP_0 + i;
		snprintf(sde_cfg->dspp[i].name, SDE_HW_BLK_NAME_LEN, "dspp_%d",
				i);

		/* create an empty sblk for each dspp */
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc =  -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		sde_cfg->dspp[i].sblk = sblk;
	}

end:
	sde_put_dt_props(props);
	return rc;
}

static int sde_dspp_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc;

	rc = _sde_dspp_cmn_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_dspp_sblks_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_ad_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_ltm_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_dspp_spr_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_dspp_demura_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_rc_parse_dt(np, sde_cfg);
end:
	return rc;
}

static int sde_ds_parse_dt(struct device_node *np,
			struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[DS_PROP_MAX], top_prop_count[DS_TOP_PROP_MAX], i;
	struct sde_prop_value *prop_value = NULL, *top_prop_value = NULL;
	bool prop_exists[DS_PROP_MAX], top_prop_exists[DS_TOP_PROP_MAX];
	u32 off_count = 0, top_off_count = 0;
	struct sde_ds_cfg *ds;
	struct sde_ds_top_cfg *ds_top = NULL;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	if (!sde_cfg->mdp[0].has_dest_scaler) {
		SDE_DEBUG("dest scaler feature not supported\n");
		rc = 0;
		goto end;
	}

	/* Parse the dest scaler top register offset and capabilities */
	top_prop_value = kzalloc(DS_TOP_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!top_prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, ds_top_prop,
				ARRAY_SIZE(ds_top_prop),
				top_prop_count, &top_off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, ds_top_prop,
			ARRAY_SIZE(ds_top_prop), top_prop_count,
			top_prop_exists, top_prop_value);
	if (rc)
		goto end;

	/* Parse the offset of each dest scaler block */
	prop_value = kcalloc(DS_PROP_MAX,
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, ds_prop, ARRAY_SIZE(ds_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	sde_cfg->ds_count = off_count;

	rc = _read_dt_entry(np, ds_prop, ARRAY_SIZE(ds_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	if (!off_count)
		goto end;

	ds_top = kzalloc(sizeof(struct sde_ds_top_cfg), GFP_KERNEL);
	if (!ds_top) {
		rc = -ENOMEM;
		goto end;
	}

	ds_top->id = DS_TOP;
	snprintf(ds_top->name, SDE_HW_BLK_NAME_LEN, "ds_top_%u",
		ds_top->id - DS_TOP);
	ds_top->base = PROP_VALUE_ACCESS(top_prop_value, DS_TOP_OFF, 0);
	ds_top->len = PROP_VALUE_ACCESS(top_prop_value, DS_TOP_LEN, 0);
	ds_top->maxupscale = MAX_UPSCALE_RATIO;

	ds_top->maxinputwidth = PROP_VALUE_ACCESS(top_prop_value,
			DS_TOP_INPUT_LINEWIDTH, 0);
	if (!top_prop_exists[DS_TOP_INPUT_LINEWIDTH])
		ds_top->maxinputwidth = DEFAULT_SDE_LINE_WIDTH;

	ds_top->maxoutputwidth = PROP_VALUE_ACCESS(top_prop_value,
			DS_TOP_OUTPUT_LINEWIDTH, 0);
	if (!top_prop_exists[DS_TOP_OUTPUT_LINEWIDTH])
		ds_top->maxoutputwidth = DEFAULT_SDE_OUTPUT_LINE_WIDTH;

	for (i = 0; i < off_count; i++) {
		ds = sde_cfg->ds + i;
		ds->top = ds_top;
		ds->base = PROP_VALUE_ACCESS(prop_value, DS_OFF, i);
		ds->id = DS_0 + i;
		ds->len = PROP_VALUE_ACCESS(prop_value, DS_LEN, 0);
		snprintf(ds->name, SDE_HW_BLK_NAME_LEN, "ds_%u",
			ds->id - DS_0);

		if (!prop_exists[DS_LEN])
			ds->len = DEFAULT_SDE_HW_BLOCK_LEN;

		if (sde_cfg->qseed_sw_lib_rev == SDE_SSPP_SCALER_QSEED3)
			set_bit(SDE_SSPP_SCALER_QSEED3, &ds->features);
		else if (sde_cfg->qseed_sw_lib_rev ==
				SDE_SSPP_SCALER_QSEED3LITE)
			set_bit(SDE_SSPP_SCALER_QSEED3LITE, &ds->features);
		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_900)) {
			set_bit(SDE_DS_DE_LPF_BLEND, &ds->features);
			set_bit(SDE_DS_MERGE_CTRL, &ds->features);
		}
	}

end:
	kfree(top_prop_value);
	kfree(prop_value);
	return rc;
};

static int sde_dsc_parse_dt(struct device_node *np,
			struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[MAX_BLOCKS], i;
	struct sde_prop_value *prop_value;
	bool prop_exists[DSC_PROP_MAX];
	u32 off_count, dsc_pair_mask, dsc_rev;
	const char *rev;
	struct sde_dsc_cfg *dsc;
	struct sde_dsc_sub_blks *sblk;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		return -EINVAL;
	}

	prop_value = kzalloc(DSC_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value)
		return -ENOMEM;

	rc = _validate_dt_entry(np, dsc_prop, ARRAY_SIZE(dsc_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	sde_cfg->dsc_count = off_count;

	rc = of_property_read_string(np, dsc_prop[DSC_REV].prop_name, &rev);
	if (!rc && !strcmp(rev, "dsc_1_2"))
		dsc_rev = SDE_DSC_HW_REV_1_2;
	else if (!rc && !strcmp(rev, "dsc_1_1"))
		dsc_rev = SDE_DSC_HW_REV_1_1;
	else
		/* default configuration */
		dsc_rev = SDE_DSC_HW_REV_1_1;

	rc = _read_dt_entry(np, dsc_prop, ARRAY_SIZE(dsc_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	sde_cfg->max_dsc_width = prop_exists[DSC_LINEWIDTH] ?
			PROP_VALUE_ACCESS(prop_value, DSC_LINEWIDTH, 0) :
			DEFAULT_SDE_LINE_WIDTH;

	for (i = 0; i < off_count; i++) {
		dsc = sde_cfg->dsc + i;

		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		dsc->sblk = sblk;

		dsc->base = PROP_VALUE_ACCESS(prop_value, DSC_OFF, i);
		dsc->id = DSC_0 + i;
		dsc->len = PROP_VALUE_ACCESS(prop_value, DSC_LEN, 0);
		snprintf(dsc->name, SDE_HW_BLK_NAME_LEN, "dsc_%u",
				dsc->id - DSC_0);

		if (!prop_exists[DSC_LEN])
			dsc->len = DEFAULT_SDE_HW_BLOCK_LEN;

		if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
			set_bit(SDE_DSC_OUTPUT_CTRL, &dsc->features);

		dsc_pair_mask = PROP_VALUE_ACCESS(prop_value,
				DSC_PAIR_MASK, i);
		if (dsc_pair_mask)
			set_bit(dsc_pair_mask, dsc->dsc_pair_mask);

		if (dsc_rev == SDE_DSC_HW_REV_1_2) {
			sblk->enc.base = PROP_VALUE_ACCESS(prop_value,
					DSC_ENC, i);
			sblk->enc.len = PROP_VALUE_ACCESS(prop_value,
					DSC_ENC_LEN, 0);
			sblk->ctl.base = PROP_VALUE_ACCESS(prop_value,
					DSC_CTL, i);
			sblk->ctl.len = PROP_VALUE_ACCESS(prop_value,
					DSC_CTL_LEN, 0);
			set_bit(SDE_DSC_HW_REV_1_2, &dsc->features);
			if (PROP_VALUE_ACCESS(prop_value, DSC_422, i))
					set_bit(SDE_DSC_NATIVE_422_EN,
						&dsc->features);
			if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_900))
				set_bit(SDE_DSC_4HS, &dsc->features);
			if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_A00))
				set_bit(SDE_DSC_FULL_ICH_PREC, &dsc->features);
			if (sde_cfg->has_reduced_ob_max)
				set_bit(SDE_DSC_REDUCED_OB_MAX, &dsc->features);
		} else {
			set_bit(SDE_DSC_HW_REV_1_1, &dsc->features);
		}
	}

end:
	kfree(prop_value);
	return rc;
};

static int sde_vdc_parse_dt(struct device_node *np,
			struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[MAX_BLOCKS], i;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[VDC_PROP_MAX];
	u32 off_count, vdc_rev;
	const char *rev;
	struct sde_vdc_cfg *vdc;
	struct sde_vdc_sub_blks *sblk;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(VDC_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, vdc_prop, ARRAY_SIZE(vdc_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	sde_cfg->vdc_count = off_count;

	rc = of_property_read_string(np, vdc_prop[VDC_REV].prop_name, &rev);
	if ((rc == -EINVAL) || (rc == -ENODATA)) {
		vdc_rev = SDE_VDC_HW_REV_1_2;
		rc = 0;
	} else if (!rc && !strcmp(rev, "vdc_1_2")) {
		vdc_rev = SDE_VDC_HW_REV_1_2;
		rc = 0;
	} else {
		SDE_ERROR("invalid vdc configuration\n");
		goto end;
	}

	rc = _read_dt_entry(np, vdc_prop, ARRAY_SIZE(vdc_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		vdc = sde_cfg->vdc + i;

		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		vdc->sblk = sblk;

		vdc->base = PROP_VALUE_ACCESS(prop_value, VDC_OFF, i);
		vdc->id = VDC_0 + i;
		vdc->len = PROP_VALUE_ACCESS(prop_value, VDC_LEN, 0);
		snprintf(vdc->name, SDE_HW_BLK_NAME_LEN, "vdc_%u",
				vdc->id - VDC_0);

		if (!prop_exists[VDC_LEN])
			vdc->len = DEFAULT_SDE_HW_BLOCK_LEN;

		sblk->enc.base = PROP_VALUE_ACCESS(prop_value,
			VDC_ENC, i);
		sblk->enc.len = PROP_VALUE_ACCESS(prop_value,
			VDC_ENC_LEN, 0);
		sblk->ctl.base = PROP_VALUE_ACCESS(prop_value,
			VDC_CTL, i);
		sblk->ctl.len = PROP_VALUE_ACCESS(prop_value,
			VDC_CTL_LEN, 0);
		set_bit(vdc_rev, &vdc->features);
	}

end:
	kfree(prop_value);
	return rc;
};

static int sde_cdm_parse_dt(struct device_node *np,
				struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[HW_PROP_MAX], i, j;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[HW_PROP_MAX];
	u32 off_count;
	struct sde_cdm_cfg *cdm;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(HW_PROP_MAX * sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, cdm_prop, ARRAY_SIZE(cdm_prop), prop_count, &off_count);
	if (rc)
		goto end;

	sde_cfg->cdm_count = off_count;

	rc = _read_dt_entry(np, cdm_prop, ARRAY_SIZE(cdm_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		cdm = sde_cfg->cdm + i;
		cdm->base = PROP_VALUE_ACCESS(prop_value, HW_OFF, i);
		cdm->id = CDM_0 + i;
		snprintf(cdm->name, SDE_HW_BLK_NAME_LEN, "cdm_%u", cdm->id - CDM_0);
		cdm->len = PROP_VALUE_ACCESS(prop_value, HW_LEN, 0);

		/* intf3 and wb(s) for cdm block */
		for (j = 0; j < sde_cfg->wb_count; j++)
			cdm->wb_connect |= BIT(sde_cfg->wb[j].id);
		cdm->intf_connect = sde_cfg->intf_count ? BIT(INTF_3) : 0;

		if (IS_SDE_CTL_REV_100(sde_cfg->ctl_rev))
			set_bit(SDE_CDM_INPUT_CTRL, &cdm->features);
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_dnsc_blur_parse_dt(struct device_node *np, struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[DNSC_BLUR_PROP_MAX], i, j;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[DNSC_BLUR_PROP_MAX];
	u32 off_count;
	struct sde_dnsc_blur_cfg *dnsc_blur;
	struct sde_dnsc_blur_sub_blks *sblk;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(DNSC_BLUR_PROP_MAX * sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, dnsc_blur_prop, ARRAY_SIZE(dnsc_blur_prop),
			prop_count, &off_count);
	if (rc)
		goto end;

	if (off_count > DNSC_BLUR_MAX_COUNT) {
		SDE_ERROR("invalid dnsc_blur block count:%d\n", off_count);
		rc = -EINVAL;
		goto end;
	}

	sde_cfg->dnsc_blur_count = off_count;

	rc = _read_dt_entry(np, dnsc_blur_prop, ARRAY_SIZE(dnsc_blur_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < sde_cfg->dnsc_blur_count; i++) {
		dnsc_blur = sde_cfg->dnsc_blur + i;

		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		dnsc_blur->sblk = sblk;

		dnsc_blur->base = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_OFF, i);
		dnsc_blur->id = DNSC_BLUR_0 + i;
		dnsc_blur->len = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_LEN, 0);
		snprintf(dnsc_blur->name, SDE_HW_BLK_NAME_LEN, "dnsc_blur_%u",
				dnsc_blur->id - DNSC_BLUR_0);
		sde_cfg->dnsc_blur_rev = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_VERSION, 0);

		sblk->gaus_lut.base = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_GAUS_LUT_OFF, i);
		sblk->gaus_lut.len = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_GAUS_LUT_LEN, 0);
		snprintf(sblk->gaus_lut.name, SDE_HW_BLK_NAME_LEN, "dnsc_blur_lut_%u",
				dnsc_blur->id - DNSC_BLUR_0);

		sblk->dither.base = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_DITHER_OFF, i);
		sblk->dither.len = PROP_VALUE_ACCESS(prop_value, DNSC_BLUR_DITHER_LEN, 0);
		snprintf(sblk->dither.name, SDE_HW_BLK_NAME_LEN, "dnsc_blur_dit_%u",
				dnsc_blur->id - DNSC_BLUR_0);

		for (j = 0; j < sde_cfg->wb_count; j++)
			dnsc_blur->wb_connect |= BIT(sde_cfg->wb[j].id);
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_uidle_parse_dt(struct device_node *np,
				struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0, prop_count[UIDLE_PROP_MAX];
	bool prop_exists[UIDLE_PROP_MAX];
	struct sde_prop_value *prop_value = NULL;
	u32 off_count;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		return -EINVAL;
	}

	if (!sde_cfg->uidle_cfg.uidle_rev)
		return 0;

	prop_value = kcalloc(UIDLE_PROP_MAX,
		sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value)
		return -ENOMEM;

	rc = _validate_dt_entry(np, uidle_prop, ARRAY_SIZE(uidle_prop),
			prop_count, &off_count);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, uidle_prop, ARRAY_SIZE(uidle_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	if (!prop_exists[UIDLE_LEN] || !prop_exists[UIDLE_OFF]) {
		SDE_DEBUG("offset/len missing, will disable uidle:%d,%d\n",
			prop_exists[UIDLE_LEN], prop_exists[UIDLE_OFF]);
		rc = -EINVAL;
		goto end;
	}

	sde_cfg->uidle_cfg.id = UIDLE;
	sde_cfg->uidle_cfg.base =
		PROP_VALUE_ACCESS(prop_value, UIDLE_OFF, 0);
	sde_cfg->uidle_cfg.len =
		PROP_VALUE_ACCESS(prop_value, UIDLE_LEN, 0);

	/* validate */
	if (!sde_cfg->uidle_cfg.base || !sde_cfg->uidle_cfg.len) {
		SDE_ERROR("invalid reg/len [%d, %d], will disable uidle\n",
			sde_cfg->uidle_cfg.base, sde_cfg->uidle_cfg.len);
		rc = -EINVAL;
	}

end:
	if (rc && sde_cfg->uidle_cfg.uidle_rev) {
		SDE_DEBUG("wrong dt entries, will disable uidle\n");
		sde_cfg->uidle_cfg.uidle_rev = 0;
	}

	kfree(prop_value);
	/* optional feature, so always return success */
	return 0;
}

static int sde_cache_parse_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	struct llcc_slice_desc *slice;
	struct device_node *llcc_node;
	int i;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	const u32 sde_sys_cache_usecase_id[SDE_SYS_CACHE_MAX] = {
		[SDE_SYS_CACHE_DISP] = LLCC_DISP,
		[SDE_SYS_CACHE_DISP_1] = LLCC_DISP_1,
		[SDE_SYS_CACHE_DISP_WB] = LLCC_DISP_WB,
	};
#else
	const u32 sde_sys_cache_usecase_id[SDE_SYS_CACHE_MAX] = {
		[SDE_SYS_CACHE_DISP] = LLCC_DISP,
		[SDE_SYS_CACHE_DISP_1] = 0,
		[SDE_SYS_CACHE_DISP_WB] = 0,
	};
#endif

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		return -EINVAL;
	}

	llcc_node = of_find_node_by_name(NULL, "cache-controller");
	if (!llcc_node) {
		SDE_DEBUG("cache controller missing, will disable img cache\n");
		return 0;
	}

	for (i = 0; i < SDE_SYS_CACHE_MAX; i++) {
		struct sde_sc_cfg *sc_cfg = &sde_cfg->sc_cfg[i];
		u32 usecase_id = 0;

		if (!test_bit(i, sde_cfg->sde_sys_cache_type_map))
			continue;

		usecase_id = sde_sys_cache_usecase_id[i];
		if (!usecase_id) {
			clear_bit(i, sde_cfg->sde_sys_cache_type_map);
			SDE_DEBUG("invalid usecase-id for sys cache:%d\n", i);
			continue;
		}

		slice = llcc_slice_getd(usecase_id);
		if (IS_ERR_OR_NULL(slice)) {
			SDE_ERROR("failed to get system cache %ld\n", PTR_ERR(slice));
			return -EINVAL;
		}

		sc_cfg->llcc_uid = usecase_id;
		sc_cfg->slice = slice;
		sc_cfg->llcc_scid = llcc_get_slice_id(slice);
		sc_cfg->llcc_slice_size = llcc_get_slice_size(slice);
		sde_core_perf_llcc_stale_configure(sde_cfg, slice);

		SDE_DEBUG("img cache:%d usecase_id:%d, scid:%d slice_size:%zu kb\n",
				i, usecase_id, sc_cfg->llcc_scid, sc_cfg->llcc_slice_size);
	}

	return 0;
}

static int _sde_vbif_populate_ot_parsing(struct sde_vbif_cfg *vbif,
	struct sde_prop_value *prop_value, int *prop_count)
{
	int j, k;

	vbif->default_ot_rd_limit = PROP_VALUE_ACCESS(prop_value,
			VBIF_DEFAULT_OT_RD_LIMIT, 0);
	SDE_DEBUG("default_ot_rd_limit=%u\n",
			vbif->default_ot_rd_limit);

	vbif->default_ot_wr_limit = PROP_VALUE_ACCESS(prop_value,
			VBIF_DEFAULT_OT_WR_LIMIT, 0);
	SDE_DEBUG("default_ot_wr_limit=%u\n",
			vbif->default_ot_wr_limit);

	vbif->dynamic_ot_rd_tbl.count =
			prop_count[VBIF_DYNAMIC_OT_RD_LIMIT] / 2;
	SDE_DEBUG("dynamic_ot_rd_tbl.count=%u\n",
			vbif->dynamic_ot_rd_tbl.count);
	if (vbif->dynamic_ot_rd_tbl.count) {
		vbif->dynamic_ot_rd_tbl.cfg = kcalloc(
			vbif->dynamic_ot_rd_tbl.count,
			sizeof(struct sde_vbif_dynamic_ot_cfg),
			GFP_KERNEL);
		if (!vbif->dynamic_ot_rd_tbl.cfg)
			return -ENOMEM;
	}

	for (j = 0, k = 0; j < vbif->dynamic_ot_rd_tbl.count; j++) {
		vbif->dynamic_ot_rd_tbl.cfg[j].pps = (u64)
			PROP_VALUE_ACCESS(prop_value,
			VBIF_DYNAMIC_OT_RD_LIMIT, k++);
		vbif->dynamic_ot_rd_tbl.cfg[j].ot_limit =
			PROP_VALUE_ACCESS(prop_value,
			VBIF_DYNAMIC_OT_RD_LIMIT, k++);
		SDE_DEBUG("dynamic_ot_rd_tbl[%d].cfg=<%llu %u>\n", j,
			vbif->dynamic_ot_rd_tbl.cfg[j].pps,
			vbif->dynamic_ot_rd_tbl.cfg[j].ot_limit);
	}

	vbif->dynamic_ot_wr_tbl.count =
			prop_count[VBIF_DYNAMIC_OT_WR_LIMIT] / 2;
	SDE_DEBUG("dynamic_ot_wr_tbl.count=%u\n",
			vbif->dynamic_ot_wr_tbl.count);
	if (vbif->dynamic_ot_wr_tbl.count) {
		vbif->dynamic_ot_wr_tbl.cfg = kcalloc(
			vbif->dynamic_ot_wr_tbl.count,
			sizeof(struct sde_vbif_dynamic_ot_cfg),
			GFP_KERNEL);
		if (!vbif->dynamic_ot_wr_tbl.cfg)
			return -ENOMEM;
	}

	for (j = 0, k = 0; j < vbif->dynamic_ot_wr_tbl.count; j++) {
		vbif->dynamic_ot_wr_tbl.cfg[j].pps = (u64)
			PROP_VALUE_ACCESS(prop_value,
			VBIF_DYNAMIC_OT_WR_LIMIT, k++);
		vbif->dynamic_ot_wr_tbl.cfg[j].ot_limit =
			PROP_VALUE_ACCESS(prop_value,
			VBIF_DYNAMIC_OT_WR_LIMIT, k++);
		SDE_DEBUG("dynamic_ot_wr_tbl[%d].cfg=<%llu %u>\n", j,
			vbif->dynamic_ot_wr_tbl.cfg[j].pps,
			vbif->dynamic_ot_wr_tbl.cfg[j].ot_limit);
	}

	if (vbif->default_ot_rd_limit || vbif->default_ot_wr_limit ||
			vbif->dynamic_ot_rd_tbl.count ||
			vbif->dynamic_ot_wr_tbl.count)
		set_bit(SDE_VBIF_QOS_OTLIM, &vbif->features);

	return 0;
}

static int _sde_vbif_populate_qos_parsing(struct sde_mdss_cfg *sde_cfg,
	struct sde_vbif_cfg *vbif, struct sde_prop_value *prop_value, int *prop_count)
{
	int i, j, prop_index = VBIF_QOS_RT_REMAP;
	u32 entries;

	for (i = VBIF_RT_CLIENT; ((i < VBIF_MAX_CLIENT) && (prop_index < VBIF_PROP_MAX));
						i++, prop_index++) {
		vbif->qos_tbl[i].count = prop_count[prop_index];
		SDE_DEBUG("qos_tbl[%d].count=%u\n", i, vbif->qos_tbl[i].count);

		entries = 2 * sde_cfg->vbif_qos_nlvl;
		if (vbif->qos_tbl[i].count == entries) {
			vbif->qos_tbl[i].priority_lvl = kcalloc(entries, sizeof(u32), GFP_KERNEL);
			if (!vbif->qos_tbl[i].priority_lvl) {
				vbif->qos_tbl[i].count = 0;
				return -ENOMEM;
			}
		} else if (vbif->qos_tbl[i].count) {
			vbif->qos_tbl[i].count = 0;
			vbif->qos_tbl[i].priority_lvl = NULL;
			SDE_ERROR("invalid qos table for client:%d, prop:%d\n", i, prop_index);
			continue;
		}

		for (j = 0; j < vbif->qos_tbl[i].count; j++) {
			vbif->qos_tbl[i].priority_lvl[j] =
					PROP_VALUE_ACCESS(prop_value, prop_index, j);
			SDE_DEBUG("client:%d, prop:%d, lvl[%d]=%u\n", i, prop_index, j,
					vbif->qos_tbl[i].priority_lvl[j]);
		}

		vbif->qos_tbl[i].count = entries;
		set_bit(SDE_VBIF_QOS_REMAP, &vbif->features);
	}

	return 0;
}

static int _sde_vbif_populate(struct sde_mdss_cfg *sde_cfg,
	struct sde_vbif_cfg *vbif, struct sde_prop_value *prop_value,
	int *prop_count, u32 vbif_len, int i)
{
	int j, k, rc;

	vbif = sde_cfg->vbif + i;
	vbif->base = PROP_VALUE_ACCESS(prop_value, VBIF_OFF, i);
	vbif->len = vbif_len;
	vbif->id = VBIF_0 + PROP_VALUE_ACCESS(prop_value, VBIF_ID, i);
	snprintf(vbif->name, SDE_HW_BLK_NAME_LEN, "vbif_%u",
			vbif->id - VBIF_0);

	SDE_DEBUG("vbif:%d\n", vbif->id - VBIF_0);

	vbif->xin_halt_timeout = VBIF_XIN_HALT_TIMEOUT;

	rc = _sde_vbif_populate_ot_parsing(vbif, prop_value, prop_count);
	if (rc)
		return rc;

	rc = _sde_vbif_populate_qos_parsing(sde_cfg, vbif, prop_value,
			prop_count);
	if (rc)
		return rc;

	vbif->memtype_count = prop_count[VBIF_MEMTYPE_0] +
				prop_count[VBIF_MEMTYPE_1];
	if (vbif->memtype_count > MAX_XIN_COUNT) {
		vbif->memtype_count = 0;
		SDE_ERROR("too many memtype defs, ignoring entries\n");
	}
	for (j = 0, k = 0; j < prop_count[VBIF_MEMTYPE_0]; j++)
		vbif->memtype[k++] = PROP_VALUE_ACCESS(
				prop_value, VBIF_MEMTYPE_0, j);
	for (j = 0; j < prop_count[VBIF_MEMTYPE_1]; j++)
		vbif->memtype[k++] = PROP_VALUE_ACCESS(
				prop_value, VBIF_MEMTYPE_1, j);
	if (test_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features))
		set_bit(SDE_VBIF_DISABLE_SHAREABLE, &vbif->features);

	return 0;
}

static int sde_vbif_parse_dt(struct device_node *np,
				struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[VBIF_PROP_MAX], i;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[VBIF_PROP_MAX];
	u32 off_count, vbif_len;
	struct sde_vbif_cfg *vbif = NULL;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(VBIF_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, vbif_prop, ARRAY_SIZE(vbif_prop),
			prop_count, &off_count);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_DYNAMIC_OT_RD_LIMIT], 1,
			&prop_count[VBIF_DYNAMIC_OT_RD_LIMIT], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_DYNAMIC_OT_WR_LIMIT], 1,
			&prop_count[VBIF_DYNAMIC_OT_WR_LIMIT], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_MEMTYPE_0], 1,
			&prop_count[VBIF_MEMTYPE_0], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_MEMTYPE_1], 1,
			&prop_count[VBIF_MEMTYPE_1], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_RT_REMAP], 1,
			&prop_count[VBIF_QOS_RT_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_NRT_REMAP], 1,
			&prop_count[VBIF_QOS_NRT_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_CWB_REMAP], 1,
			&prop_count[VBIF_QOS_CWB_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_LUTDMA_REMAP], 1,
			&prop_count[VBIF_QOS_LUTDMA_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_CNOC_REMAP], 1,
			&prop_count[VBIF_QOS_CNOC_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_OFFLINE_WB_REMAP], 1,
			&prop_count[VBIF_QOS_OFFLINE_WB_REMAP], NULL);
	if (rc)
		goto end;

	rc = _validate_dt_entry(np, &vbif_prop[VBIF_QOS_WB_ROT_REMAP], 1,
			&prop_count[VBIF_QOS_WB_ROT_REMAP], NULL);
	if (rc)
		goto end;

	sde_cfg->vbif_count = off_count;

	rc = _read_dt_entry(np, vbif_prop, ARRAY_SIZE(vbif_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	vbif_len = PROP_VALUE_ACCESS(prop_value, VBIF_LEN, 0);
	if (!prop_exists[VBIF_LEN])
		vbif_len = DEFAULT_SDE_HW_BLOCK_LEN;

	for (i = 0; i < off_count; i++) {
		rc = _sde_vbif_populate(sde_cfg, vbif, prop_value,
				prop_count, vbif_len, i);
		if (rc)
			goto end;
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_pp_parse_dt(struct device_node *np, struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[PP_PROP_MAX], i;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[PP_PROP_MAX];
	u32 off_count, major_version;
	struct sde_pingpong_cfg *pp;
	struct sde_pingpong_sub_blks *sblk;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(PP_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, pp_prop, ARRAY_SIZE(pp_prop), prop_count,
		&off_count);
	if (rc)
		goto end;

	sde_cfg->pingpong_count = off_count;

	rc = _read_dt_entry(np, pp_prop, ARRAY_SIZE(pp_prop), prop_count,
		prop_exists, prop_value);
	if (rc)
		goto end;

	major_version = SDE_HW_MAJOR(sde_cfg->hw_rev);
	for (i = 0; i < off_count; i++) {
		pp = sde_cfg->pingpong + i;
		sblk = kzalloc(sizeof(*sblk), GFP_KERNEL);
		if (!sblk) {
			rc = -ENOMEM;
			/* catalog deinit will release the allocated blocks */
			goto end;
		}
		pp->sblk = sblk;

		pp->base = PROP_VALUE_ACCESS(prop_value, PP_OFF, i);
		pp->id = PINGPONG_0 + i;
		snprintf(pp->name, SDE_HW_BLK_NAME_LEN, "pingpong_%u",
				pp->id - PINGPONG_0);
		pp->len = PROP_VALUE_ACCESS(prop_value, PP_LEN, 0);

		sblk->te.base = PROP_VALUE_ACCESS(prop_value, TE_OFF, i);
		sblk->te.id = SDE_PINGPONG_TE;
		snprintf(sblk->te.name, SDE_HW_BLK_NAME_LEN, "te_%u",
				pp->id - PINGPONG_0);

		if (major_version < SDE_HW_MAJOR(SDE_HW_VER_500))
			set_bit(SDE_PINGPONG_TE, &pp->features);

		sblk->te2.base = PROP_VALUE_ACCESS(prop_value, TE2_OFF, i);
		if (sblk->te2.base) {
			sblk->te2.id = SDE_PINGPONG_TE2;
			snprintf(sblk->te2.name, SDE_HW_BLK_NAME_LEN, "te2_%u",
					pp->id - PINGPONG_0);
			set_bit(SDE_PINGPONG_TE2, &pp->features);
			set_bit(SDE_PINGPONG_SPLIT, &pp->features);
		}

		if (PROP_VALUE_ACCESS(prop_value, PP_SLAVE, i))
			set_bit(SDE_PINGPONG_SLAVE, &pp->features);

		if (PROP_VALUE_ACCESS(prop_value, PP_CWB, i)) {
			set_bit(SDE_PINGPONG_CWB, &pp->features);
			if (test_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features))
				sde_cfg->dcwb_count++;
		}
		pp->dcwb_id = (sde_cfg->dcwb_count > 0) ? sde_cfg->dcwb_count : DCWB_MAX;

		if (major_version < SDE_HW_MAJOR(SDE_HW_VER_700)) {
			sblk->dsc.base = PROP_VALUE_ACCESS(prop_value,
					DSC_OFF, i);
			if (sblk->dsc.base) {
				sblk->dsc.id = SDE_PINGPONG_DSC;
				snprintf(sblk->dsc.name, SDE_HW_BLK_NAME_LEN,
						"dsc_%u",
						pp->id - PINGPONG_0);
				set_bit(SDE_PINGPONG_DSC, &pp->features);
			}
		}

		if (sde_cfg->ppb_sz_program == SDE_PPB_SIZE_THRU_PINGPONG)
			set_bit(SDE_PINGPONG_SET_SIZE, &pp->features);

		sblk->dither.base = PROP_VALUE_ACCESS(prop_value, DITHER_OFF,
				i);
		if (sblk->dither.base) {
			sblk->dither.id = SDE_PINGPONG_DITHER;
			snprintf(sblk->dither.name, SDE_HW_BLK_NAME_LEN,
					"dither_%u", pp->id);
			set_bit(SDE_PINGPONG_DITHER, &pp->features);
		}
		sblk->dither.len = PROP_VALUE_ACCESS(prop_value, DITHER_LEN, 0);
		sblk->dither.version = PROP_VALUE_ACCESS(prop_value, DITHER_VER,
								0);

		if (test_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features) &&
		    PROP_VALUE_ACCESS(prop_value, CWB_DITHER, i)) {
			set_bit(SDE_PINGPONG_CWB_DITHER, &pp->features);
		}

		if (test_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features))
			set_bit(SDE_PINGPONG_DITHER_LUMA, &pp->features);

		if (prop_exists[PP_MERGE_3D_ID]) {
			set_bit(SDE_PINGPONG_MERGE_3D, &pp->features);
			pp->merge_3d_id = PROP_VALUE_ACCESS(prop_value,
					PP_MERGE_3D_ID, i) + 1;
		}
	}

end:
	kfree(prop_value);
	return rc;
}

static void _sde_top_parse_dt_helper(struct sde_mdss_cfg *cfg,
	struct sde_dt_props *props)
{
	int i;
	u64 ddr_type;

	cfg->max_sspp_linewidth = props->exists[SSPP_LINEWIDTH] ?
			PROP_VALUE_ACCESS(props->values, SSPP_LINEWIDTH, 0) :
			DEFAULT_SDE_LINE_WIDTH;

	cfg->vig_sspp_linewidth = props->exists[VIG_SSPP_LINEWIDTH] ?
			PROP_VALUE_ACCESS(props->values, VIG_SSPP_LINEWIDTH,
			0) : cfg->max_sspp_linewidth;

	cfg->scaling_linewidth = props->exists[SCALING_LINEWIDTH] ?
			PROP_VALUE_ACCESS(props->values, SCALING_LINEWIDTH,
			0) : cfg->vig_sspp_linewidth;

	cfg->max_wb_linewidth = props->exists[WB_LINEWIDTH] ?
			PROP_VALUE_ACCESS(props->values, WB_LINEWIDTH, 0) :
			DEFAULT_SDE_LINE_WIDTH;

	/* if wb linear width is not defined use the line width as default */
	cfg->max_wb_linewidth_linear = props->exists[WB_LINEWIDTH_LINEAR] ?
			PROP_VALUE_ACCESS(props->values, WB_LINEWIDTH_LINEAR, 0)
			:  cfg->max_wb_linewidth;

	cfg->max_mixer_width = props->exists[MIXER_LINEWIDTH] ?
			PROP_VALUE_ACCESS(props->values, MIXER_LINEWIDTH, 0) :
			DEFAULT_SDE_LINE_WIDTH;

	cfg->max_mixer_blendstages = props->exists[MIXER_BLEND] ?
			PROP_VALUE_ACCESS(props->values, MIXER_BLEND, 0) :
			DEFAULT_SDE_MIXER_BLENDSTAGES;

	cfg->ubwc_rev = props->exists[UBWC_VERSION] ?
			PROP_VALUE_ACCESS(props->values,
			UBWC_VERSION, 0) : DEFAULT_SDE_UBWC_NONE;

	cfg->mdp[0].highest_bank_bit = DEFAULT_SDE_HIGHEST_BANK_BIT;

	if (props->exists[BANK_BIT]) {
		for (i = 0; i < props->counts[BANK_BIT]; i++) {
			ddr_type = PROP_BITVALUE_ACCESS(props->values,
					BANK_BIT, i, 0);
			if (!ddr_type || (sde_get_ddr_type() == ddr_type))
				cfg->mdp[0].highest_bank_bit =
					PROP_BITVALUE_ACCESS(props->values,
					BANK_BIT, i, 1);
		}
	}

	cfg->macrotile_mode = props->exists[MACROTILE_MODE] ?
			PROP_VALUE_ACCESS(props->values, MACROTILE_MODE, 0) :
			DEFAULT_SDE_UBWC_MACROTILE_MODE;

	cfg->ubwc_bw_calc_rev = PROP_VALUE_ACCESS(props->values, UBWC_BW_CALC_VERSION, 0);

	cfg->mdp[0].ubwc_static = props->exists[UBWC_STATIC] ?
			PROP_VALUE_ACCESS(props->values, UBWC_STATIC, 0) :
			DEFAULT_SDE_UBWC_STATIC;

	cfg->mdp[0].ubwc_swizzle = props->exists[UBWC_SWIZZLE] ?
			PROP_VALUE_ACCESS(props->values, UBWC_SWIZZLE, 0) :
			DEFAULT_SDE_UBWC_SWIZZLE;

	cfg->mdp[0].has_dest_scaler =
		PROP_VALUE_ACCESS(props->values, DEST_SCALER, 0);

	cfg->mdp[0].smart_panel_align_mode =
		PROP_VALUE_ACCESS(props->values, SMART_PANEL_ALIGN_MODE, 0);

	cfg->ipcc_protocol_id = PROP_VALUE_ACCESS(props->values, IPCC_PROTOCOL_ID, 0);
	cfg->ipcc_client_phys_id = PROP_VALUE_ACCESS(props->values, IPCC_CLIENT_DPU_PHYS_ID, 0);
	if (!cfg->ipcc_protocol_id || !cfg->ipcc_client_phys_id)
		cfg->hw_fence_rev = 0; /* disable hw fences*/

	if (props->exists[SEC_SID_MASK]) {
		cfg->sec_sid_mask_count = props->counts[SEC_SID_MASK];
		for (i = 0; i < cfg->sec_sid_mask_count; i++)
			cfg->sec_sid_mask[i] = PROP_VALUE_ACCESS(props->values,
					SEC_SID_MASK, i);
	}

	if (PROP_VALUE_ACCESS(props->values, SRC_SPLIT, 0))
		set_bit(SDE_FEATURE_SRC_SPLIT, cfg->features);
	if (PROP_VALUE_ACCESS(props->values, DIM_LAYER, 0))
		set_bit(SDE_FEATURE_DIM_LAYER, cfg->features);
	if (PROP_VALUE_ACCESS(props->values, IDLE_PC, 0))
		set_bit(SDE_FEATURE_IDLE_PC, cfg->features);
	if (PROP_VALUE_ACCESS(props->values, WAKEUP_WITH_TOUCH, 0))
		set_bit(SDE_FEATURE_TOUCH_WAKEUP, cfg->features);
	cfg->pipe_order_type = PROP_VALUE_ACCESS(props->values,
			PIPE_ORDER_VERSION, 0);
	if (PROP_VALUE_ACCESS(props->values, BASE_LAYER, 0))
		set_bit(SDE_FEATURE_BASE_LAYER, cfg->features);
	cfg->qseed_hw_rev = PROP_VALUE_ACCESS(props->values, QSEED_HW_VERSION, 0);
	cfg->trusted_vm_env = PROP_VALUE_ACCESS(props->values, TRUSTED_VM_ENV, 0);
	cfg->max_trusted_vm_displays = PROP_VALUE_ACCESS(props->values,
			MAX_TRUSTED_VM_DISPLAYS, 0);
	if (props->exists[TVM_INCLUDE_REG]) {
		cfg->tvm_reg_count = props->counts[TVM_INCLUDE_REG] / 2;
		for (i = 0; i < cfg->tvm_reg_count; i++) {
			cfg->tvm_reg[i].start = PROP_VALUE_ACCESS(props->values,
				TVM_INCLUDE_REG, i * 2);
			cfg->tvm_reg[i].end = cfg->tvm_reg[i].start +
				PROP_VALUE_ACCESS(props->values, TVM_INCLUDE_REG,
						i * 2 + 1);
		}
	}

	if (PROP_VALUE_ACCESS(props->values, SDE_EMULATED_ENV, 0))
		set_bit(SDE_FEATURE_EMULATED_ENV, cfg->features);
}

static int sde_top_parse_dt(struct device_node *np, struct sde_mdss_cfg *cfg)
{
	int rc = 0, dma_rc, len;
	struct sde_dt_props *props;
	const char *type;
	u32 major_version;

	props = sde_get_dt_props(np, SDE_PROP_MAX, sde_prop,
			ARRAY_SIZE(sde_prop), &len);
	if (IS_ERR(props))
		return PTR_ERR(props);

	/* revalidate arrays not bound to off_count elements */
	rc = _validate_dt_entry(np, &sde_prop[SEC_SID_MASK], 1,
			&props->counts[SEC_SID_MASK], NULL);
	if (rc)
		goto end;

	/* update props with newly validated arrays */
	rc = _read_dt_entry(np, sde_prop, ARRAY_SIZE(sde_prop), props->counts,
			props->exists, props->values);
	if (rc)
		goto end;

	cfg->mdss_count = 1;
	cfg->mdss[0].base = MDSS_BASE_OFFSET;
	cfg->mdss[0].id = MDP_TOP;
	snprintf(cfg->mdss[0].name, SDE_HW_BLK_NAME_LEN, "mdss_%u",
			cfg->mdss[0].id - MDP_TOP);

	cfg->mdp_count = 1;
	cfg->mdp[0].id = MDP_TOP;
	snprintf(cfg->mdp[0].name, SDE_HW_BLK_NAME_LEN, "top_%u",
			cfg->mdp[0].id - MDP_TOP);
	cfg->mdp[0].base = PROP_VALUE_ACCESS(props->values, SDE_OFF, 0);
	cfg->mdp[0].len = props->exists[SDE_LEN] ? PROP_VALUE_ACCESS(
			props->values, SDE_LEN, 0) : DEFAULT_SDE_HW_BLOCK_LEN;

	_sde_top_parse_dt_helper(cfg, props);

	major_version = SDE_HW_MAJOR(cfg->hw_rev);
	if (major_version < SDE_HW_MAJOR(SDE_HW_VER_500))
		set_bit(SDE_MDP_VSYNC_SEL, &cfg->mdp[0].features);
	else if (major_version < SDE_HW_MAJOR(SDE_HW_VER_810))
		set_bit(SDE_MDP_WD_TIMER, &cfg->mdp[0].features);

	if (cfg->ppb_sz_program == SDE_PPB_SIZE_THRU_TOP)
		set_bit(SDE_MDP_TOP_PPB_SET_SIZE, &cfg->mdp[0].features);

	rc = _add_to_irq_offset_list(cfg, SDE_INTR_HWBLK_TOP,
			SDE_INTR_TOP_INTR, cfg->mdp[0].base);
	if (rc)
		goto end;

	rc = _add_to_irq_offset_list(cfg, SDE_INTR_HWBLK_TOP,
			SDE_INTR_TOP_INTR2, cfg->mdp[0].base);
	if (rc)
		goto end;

	rc = _add_to_irq_offset_list(cfg, SDE_INTR_HWBLK_TOP,
			SDE_INTR_TOP_HIST_INTR, cfg->mdp[0].base);
	if (rc)
		goto end;

	rc = of_property_read_string(np, sde_prop[QSEED_SW_LIB_REV].prop_name,
			&type);
	if (rc) {
		SDE_DEBUG("invalid %s node in device tree: %d\n",
				sde_prop[QSEED_SW_LIB_REV].prop_name, rc);
		rc = 0;
	} else if (!strcmp(type, "qseedv3")) {
		cfg->qseed_sw_lib_rev = SDE_SSPP_SCALER_QSEED3;
	} else if (!strcmp(type, "qseedv3lite")) {
		cfg->qseed_sw_lib_rev = SDE_SSPP_SCALER_QSEED3LITE;
	} else if (!strcmp(type, "qseedv2")) {
		cfg->qseed_sw_lib_rev = SDE_SSPP_SCALER_QSEED2;
	} else {
		SDE_DEBUG("Unknown type %s for property %s\n", type,
				sde_prop[QSEED_SW_LIB_REV].prop_name);
	}

	rc = of_property_read_string(np, sde_prop[CSC_TYPE].prop_name, &type);
	if (rc) {
		SDE_DEBUG("invalid %s node in device tree: %d\n",
				sde_prop[CSC_TYPE].prop_name, rc);
		rc = 0;
	} else if (!strcmp(type, "csc")) {
		cfg->csc_type = SDE_SSPP_CSC;
	} else if (!strcmp(type, "csc-10bit")) {
		cfg->csc_type = SDE_SSPP_CSC_10BIT;
	} else {
		SDE_DEBUG("Unknown type %s for property %s\n", type,
				sde_prop[CSC_TYPE].prop_name);
	}

	/*
	 * Current SDE support only Smart DMA 2.0-2.5.
	 * No support for Smart DMA 1.0 yet.
	 */
	cfg->smart_dma_rev = 0;
	dma_rc = of_property_read_string(np, sde_prop[SMART_DMA_REV].prop_name,
			&type);
	if (dma_rc) {
		SDE_DEBUG("invalid %s node in device tree: %d\n",
				sde_prop[SMART_DMA_REV].prop_name, dma_rc);
	} else if (!strcmp(type, "smart_dma_v2p5")) {
		cfg->smart_dma_rev = SDE_SSPP_SMART_DMA_V2p5;
	} else if (!strcmp(type, "smart_dma_v2")) {
		cfg->smart_dma_rev = SDE_SSPP_SMART_DMA_V2;
	} else if (!strcmp(type, "smart_dma_v1")) {
		SDE_ERROR("smart dma 1.0 is not supported in SDE\n");
	} else {
		SDE_DEBUG("unknown smart dma version %s\n", type);
	}

end:
	sde_put_dt_props(props);
	return rc;
}

static int sde_parse_reg_dma_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc = 0, i, prop_count[REG_DMA_PROP_MAX];
	struct sde_prop_value *prop_value = NULL;
	u32 off_count;
	bool prop_exists[REG_DMA_PROP_MAX];
	bool dma_type_exists[REG_DMA_TYPE_MAX];
	enum sde_reg_dma_type dma_type;

	prop_value = kcalloc(REG_DMA_PROP_MAX,
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, reg_dma_prop, ARRAY_SIZE(reg_dma_prop),
			prop_count, &off_count);
	if (rc || !off_count)
		goto end;

	rc = _read_dt_entry(np, reg_dma_prop, ARRAY_SIZE(reg_dma_prop),
			prop_count, prop_exists, prop_value);
	if (rc)
		goto end;

	sde_cfg->reg_dma_count = 0;
	memset(&dma_type_exists, 0, sizeof(dma_type_exists));
	for (i = 0; i < off_count; i++) {
		dma_type = PROP_VALUE_ACCESS(prop_value, REG_DMA_ID, i);
		if (dma_type >= REG_DMA_TYPE_MAX) {
			SDE_ERROR("Invalid DMA type %d\n", dma_type);
			goto end;
		} else if (dma_type_exists[dma_type]) {
			SDE_ERROR("DMA type ID %d exists more than once\n",
					dma_type);
			goto end;
		}

		dma_type_exists[dma_type] = true;
		sde_cfg->dma_cfg.reg_dma_blks[dma_type].base =
				PROP_VALUE_ACCESS(prop_value, REG_DMA_OFF, i);
		sde_cfg->dma_cfg.reg_dma_blks[dma_type].valid = true;
		sde_cfg->reg_dma_count++;
	}

	sde_cfg->dma_cfg.version = PROP_VALUE_ACCESS(prop_value,
						REG_DMA_VERSION, 0);
	sde_cfg->dma_cfg.trigger_sel_off = PROP_VALUE_ACCESS(prop_value,
						REG_DMA_TRIGGER_OFF, 0);
	sde_cfg->dma_cfg.broadcast_disabled = PROP_VALUE_ACCESS(prop_value,
						REG_DMA_BROADCAST_DISABLED, 0);
	sde_cfg->dma_cfg.xin_id = PROP_VALUE_ACCESS(prop_value,
						REG_DMA_XIN_ID, 0);
	sde_cfg->dma_cfg.clk_ctrl = SDE_CLK_CTRL_LUTDMA;
	sde_cfg->dma_cfg.vbif_idx = VBIF_RT;

	if (test_bit(SDE_FEATURE_VBIF_CLK_SPLIT, sde_cfg->features)) {
		sde_cfg->dma_cfg.split_vbif_supported = true;
	} else {
		for (i = 0; i < sde_cfg->mdp_count; i++) {
			sde_cfg->mdp[i].clk_ctrls[sde_cfg->dma_cfg.clk_ctrl].reg_off =
				PROP_BITVALUE_ACCESS(prop_value,
						REG_DMA_CLK_CTRL, 0, 0);
			sde_cfg->mdp[i].clk_ctrls[sde_cfg->dma_cfg.clk_ctrl].bit_off =
				PROP_BITVALUE_ACCESS(prop_value,
						REG_DMA_CLK_CTRL, 0, 1);
		}
	}
end:
	kfree(prop_value);
	/* reg dma is optional feature hence return 0 */
	return 0;
}

static int _sde_perf_parse_dt_validate(struct device_node *np, int *prop_count)
{
	int rc, len;

	rc = _validate_dt_entry(np, sde_perf_prop, ARRAY_SIZE(sde_perf_prop),
			prop_count, &len);
	if (rc)
		return rc;

	rc = _validate_dt_entry(np, &sde_perf_prop[PERF_CDP_SETTING], 1,
			&prop_count[PERF_CDP_SETTING], NULL);
	if (rc)
		return rc;

	return rc;
}

static int _sde_qos_parse_dt_cfg(struct sde_mdss_cfg *cfg, int *prop_count,
	struct sde_prop_value *prop_value, bool *prop_exists)
{
	int i, j;
	u32 qos_count = 1;

	if (prop_exists[QOS_REFRESH_RATES]) {
		qos_count = prop_count[QOS_REFRESH_RATES];
		cfg->perf.qos_refresh_rate = kcalloc(qos_count,
			sizeof(u32), GFP_KERNEL);
		if (!cfg->perf.qos_refresh_rate)
			goto end;

		for (j = 0; j < qos_count; j++) {
			cfg->perf.qos_refresh_rate[j] =
					PROP_VALUE_ACCESS(prop_value,
						QOS_REFRESH_RATES, j);
			SDE_DEBUG("qos usage:%d refresh rate:0x%x\n",
					j, cfg->perf.qos_refresh_rate[j]);
		}
	}
	cfg->perf.qos_refresh_count = qos_count;

	cfg->perf.danger_lut = kcalloc(qos_count,
		sizeof(u64) * SDE_QOS_LUT_USAGE_MAX * SDE_DANGER_SAFE_LUT_TYPE_MAX, GFP_KERNEL);
	cfg->perf.safe_lut = kcalloc(qos_count,
		sizeof(u64) * SDE_QOS_LUT_USAGE_MAX * SDE_DANGER_SAFE_LUT_TYPE_MAX, GFP_KERNEL);
	cfg->perf.creq_lut = kcalloc(qos_count,
		sizeof(u64) * SDE_QOS_LUT_USAGE_MAX * SDE_CREQ_LUT_TYPE_MAX, GFP_KERNEL);
	if (!cfg->perf.creq_lut || !cfg->perf.safe_lut || !cfg->perf.danger_lut)
		goto end;

	if (prop_exists[QOS_DANGER_LUT] &&
			(prop_count[QOS_DANGER_LUT] >=
			    (SDE_QOS_LUT_USAGE_MAX * qos_count * SDE_DANGER_SAFE_LUT_TYPE_MAX))) {
		for (i = 0; i < prop_count[QOS_DANGER_LUT]; i++) {
			cfg->perf.danger_lut[i] = PROP_VALUE_ACCESS(prop_value, QOS_DANGER_LUT, i);
			SDE_DEBUG("danger usage:%i lut:0x%llx\n", i, cfg->perf.danger_lut[i]);
		}
	}

	if (prop_exists[QOS_SAFE_LUT] &&
			(prop_count[QOS_SAFE_LUT] >=
			    (SDE_QOS_LUT_USAGE_MAX * qos_count * SDE_DANGER_SAFE_LUT_TYPE_MAX))) {
		for (i = 0; i < prop_count[QOS_SAFE_LUT]; i++) {
			cfg->perf.safe_lut[i] = PROP_VALUE_ACCESS(prop_value, QOS_SAFE_LUT, i);
			SDE_DEBUG("safe usage:%d lut:0x%llx\n", i, cfg->perf.safe_lut[i]);
		}
	}

	if (prop_exists[QOS_CREQ_LUT] &&
		(prop_count[QOS_CREQ_LUT] >=
		(SDE_QOS_LUT_USAGE_MAX * qos_count * SDE_CREQ_LUT_TYPE_MAX))) {
		u64 lut_hi, lut_lo;

		for (j = 0; j < (qos_count * SDE_QOS_LUT_USAGE_MAX * SDE_CREQ_LUT_TYPE_MAX); j++) {
			lut_hi = PROP_VALUE_ACCESS(prop_value, QOS_CREQ_LUT, (j * 2) + 0);
			lut_lo = PROP_VALUE_ACCESS(prop_value, QOS_CREQ_LUT, (j * 2) + 1);
			cfg->perf.creq_lut[j] = (lut_hi << 32) | lut_lo;
			SDE_DEBUG("creq usage:%d lut:0x%llx\n", j, cfg->perf.creq_lut[j]);
		}
	}

	return 0;

end:
	kfree(cfg->perf.qos_refresh_rate);
	kfree(cfg->perf.creq_lut);
	kfree(cfg->perf.danger_lut);
	kfree(cfg->perf.safe_lut);

	return -ENOMEM;
}

static void _sde_perf_parse_dt_cfg_populate(struct sde_mdss_cfg *cfg,
		int *prop_count,
		struct sde_prop_value *prop_value,
		bool *prop_exists)
{
	cfg->perf.max_bw_low =
			prop_exists[PERF_MAX_BW_LOW] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MAX_BW_LOW, 0) :
			DEFAULT_MAX_BW_LOW;
	cfg->perf.max_bw_high =
			prop_exists[PERF_MAX_BW_HIGH] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MAX_BW_HIGH, 0) :
			DEFAULT_MAX_BW_HIGH;
	cfg->perf.min_core_ib =
			prop_exists[PERF_MIN_CORE_IB] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MIN_CORE_IB, 0) :
			DEFAULT_MAX_BW_LOW;
	cfg->perf.min_llcc_ib =
			prop_exists[PERF_MIN_LLCC_IB] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MIN_LLCC_IB, 0) :
			DEFAULT_MAX_BW_LOW;
	cfg->perf.min_dram_ib =
			prop_exists[PERF_MIN_DRAM_IB] ?
			PROP_VALUE_ACCESS(prop_value, PERF_MIN_DRAM_IB, 0) :
			DEFAULT_MAX_BW_LOW;

	cfg->perf.undersized_prefill_lines =
			prop_exists[PERF_UNDERSIZED_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_UNDERSIZED_PREFILL_LINES, 0) :
			DEFAULT_UNDERSIZED_PREFILL_LINES;
	cfg->perf.xtra_prefill_lines =
			prop_exists[PERF_XTRA_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_XTRA_PREFILL_LINES, 0) :
			DEFAULT_XTRA_PREFILL_LINES;
	cfg->perf.dest_scale_prefill_lines =
			prop_exists[PERF_DEST_SCALE_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_DEST_SCALE_PREFILL_LINES, 0) :
			DEFAULT_DEST_SCALE_PREFILL_LINES;
	cfg->perf.macrotile_prefill_lines =
			prop_exists[PERF_MACROTILE_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_MACROTILE_PREFILL_LINES, 0) :
			DEFAULT_MACROTILE_PREFILL_LINES;
	cfg->perf.yuv_nv12_prefill_lines =
			prop_exists[PERF_YUV_NV12_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_YUV_NV12_PREFILL_LINES, 0) :
			DEFAULT_YUV_NV12_PREFILL_LINES;
	cfg->perf.linear_prefill_lines =
			prop_exists[PERF_LINEAR_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_LINEAR_PREFILL_LINES, 0) :
			DEFAULT_LINEAR_PREFILL_LINES;
	cfg->perf.downscaling_prefill_lines =
			prop_exists[PERF_DOWNSCALING_PREFILL_LINES] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_DOWNSCALING_PREFILL_LINES, 0) :
			DEFAULT_DOWNSCALING_PREFILL_LINES;
	cfg->perf.amortizable_threshold =
			prop_exists[PERF_AMORTIZABLE_THRESHOLD] ?
			PROP_VALUE_ACCESS(prop_value,
					PERF_AMORTIZABLE_THRESHOLD, 0) :
			DEFAULT_AMORTIZABLE_THRESHOLD;
	cfg->perf.num_ddr_channels = prop_exists[PERF_NUM_DDR_CHANNELS] ?
			PROP_VALUE_ACCESS(prop_value, PERF_NUM_DDR_CHANNELS, 0) :
			DEFAULT_NUM_DDR_CHANNELS;
	cfg->perf.dram_efficiency = prop_exists[PERF_DRAM_EFFICIENCY] ?
			PROP_VALUE_ACCESS(prop_value, PERF_DRAM_EFFICIENCY, 0) :
			DEFAULT_DRAM_EFFICIENCY;
	cfg->perf.num_mnoc_ports =
			prop_exists[PERF_NUM_MNOC_PORTS] ?
			PROP_VALUE_ACCESS(prop_value,
				PERF_NUM_MNOC_PORTS, 0) :
			DEFAULT_MNOC_PORTS;
	cfg->perf.axi_bus_width =
			prop_exists[PERF_AXI_BUS_WIDTH] ?
			PROP_VALUE_ACCESS(prop_value,
				PERF_AXI_BUS_WIDTH, 0) :
			DEFAULT_AXI_BUS_WIDTH;
}

/**
 * _sde_set_possible_cpu_mask - checks defective cores in qos mask and update the
 * mask to avoid defective cores and add next possible cores for pm qos vote.
 * @qos_mask:	qos_mask set from DT
 */
static int _sde_set_possible_cpu_mask(unsigned long qos_mask)
{
	int cpu = 0, defective_cores_count = 0;
	struct cpumask *cpu_qos_mask = to_cpumask(&qos_mask);
	unsigned long cpu_p_mask = cpu_possible_mask->bits[0];
	unsigned long cpu_defective_qos = qos_mask & (~cpu_p_mask);

	/* Count all the defective cores in cpu_defective_qos */
	defective_cores_count = cpumask_weight(to_cpumask(&cpu_defective_qos));

	for_each_cpu(cpu, cpu_all_mask) {
		if (cpu_possible(cpu) && !cpumask_test_cpu(cpu, cpu_qos_mask) &&
					defective_cores_count > 0) {
			/* Set next possible cpu */
			cpumask_set_cpu(cpu, cpu_qos_mask);
			defective_cores_count--;
		} else if (cpumask_test_cpu(cpu, cpu_qos_mask) && !cpu_possible(cpu)) {
			/* Unset the defective core from qos mask */
			cpumask_clear_cpu(cpu, cpu_qos_mask);
		}
	}

	qos_mask = cpu_qos_mask->bits[0];
	return qos_mask;
}

static int _sde_perf_parse_dt_cfg(struct device_node *np,
	struct sde_mdss_cfg *cfg, int *prop_count,
	struct sde_prop_value *prop_value, bool *prop_exists)
{
	int rc, j;
	const char *str = NULL;
	unsigned long qos_mask = 0;

	/*
	 * The following performance parameters (e.g. core_ib_ff) are
	 * mapped directly as device tree string constants.
	 */
	rc = of_property_read_string(np,
			sde_perf_prop[PERF_CORE_IB_FF].prop_name, &str);
	cfg->perf.core_ib_ff = rc ? DEFAULT_CORE_IB_FF : str;
	rc = of_property_read_string(np,
			sde_perf_prop[PERF_CORE_CLK_FF].prop_name, &str);
	cfg->perf.core_clk_ff = rc ? DEFAULT_CORE_CLK_FF : str;
	rc = of_property_read_string(np,
			sde_perf_prop[PERF_COMP_RATIO_RT].prop_name, &str);
	cfg->perf.comp_ratio_rt = rc ? DEFAULT_COMP_RATIO_RT : str;
	rc = of_property_read_string(np,
			sde_perf_prop[PERF_COMP_RATIO_NRT].prop_name, &str);
	cfg->perf.comp_ratio_nrt = rc ? DEFAULT_COMP_RATIO_NRT : str;
	rc = 0;

	_sde_perf_parse_dt_cfg_populate(cfg, prop_count, prop_value,
			prop_exists);

	if (prop_exists[PERF_CDP_SETTING]) {
		const u32 prop_size = 2;
		u32 count = prop_count[PERF_CDP_SETTING] / prop_size;

		count = min_t(u32, count, SDE_PERF_CDP_USAGE_MAX);

		for (j = 0; j < count; j++) {
			cfg->perf.cdp_cfg[j].rd_enable =
					PROP_VALUE_ACCESS(prop_value,
					PERF_CDP_SETTING, j * prop_size);
			cfg->perf.cdp_cfg[j].wr_enable =
					PROP_VALUE_ACCESS(prop_value,
					PERF_CDP_SETTING, j * prop_size + 1);
			SDE_DEBUG("cdp usage:%d rd:%d wr:%d\n",
				j, cfg->perf.cdp_cfg[j].rd_enable,
				cfg->perf.cdp_cfg[j].wr_enable);
		}

		set_bit(SDE_FEATURE_CDP, cfg->features);
	}

	qos_mask = prop_exists[PERF_CPU_MASK] ?
			PROP_VALUE_ACCESS(prop_value, PERF_CPU_MASK, 0) :
			DEFAULT_CPU_MASK;
	cfg->perf.cpu_mask = _sde_set_possible_cpu_mask(qos_mask);

	qos_mask = prop_exists[CPU_MASK_PERF] ?
			PROP_VALUE_ACCESS(prop_value, CPU_MASK_PERF, 0) :
			DEFAULT_CPU_MASK;
	cfg->perf.cpu_mask_perf = _sde_set_possible_cpu_mask(qos_mask);

	cfg->perf.cpu_dma_latency =
			prop_exists[PERF_CPU_DMA_LATENCY] ?
			PROP_VALUE_ACCESS(prop_value, PERF_CPU_DMA_LATENCY, 0) :
			DEFAULT_CPU_DMA_LATENCY;
	cfg->perf.cpu_irq_latency =
			prop_exists[PERF_CPU_IRQ_LATENCY] ?
			PROP_VALUE_ACCESS(prop_value, PERF_CPU_IRQ_LATENCY, 0) :
			PM_QOS_DEFAULT_VALUE;

	return 0;
}

static int sde_perf_parse_dt(struct device_node *np, struct sde_mdss_cfg *cfg)
{
	int rc, prop_count[PERF_PROP_MAX];
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[PERF_PROP_MAX];

	if (!cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(PERF_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _sde_perf_parse_dt_validate(np, prop_count);
	if (rc)
		goto freeprop;

	rc = _read_dt_entry(np, sde_perf_prop, ARRAY_SIZE(sde_perf_prop),
			prop_count, prop_exists, prop_value);
	if (rc)
		goto freeprop;

	rc = _sde_perf_parse_dt_cfg(np, cfg, prop_count, prop_value,
			prop_exists);

freeprop:
	kfree(prop_value);
end:
	return rc;
}

static int sde_qos_parse_dt(struct device_node *np, struct sde_mdss_cfg *cfg)
{
	int rc, prop_count[QOS_PROP_MAX];
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[QOS_PROP_MAX];

	if (!cfg) {
		SDE_ERROR("invalid argument\n");
		rc = -EINVAL;
		goto end;
	}

	prop_value = kzalloc(QOS_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value) {
		rc = -ENOMEM;
		goto end;
	}

	rc = _validate_dt_entry(np, sde_qos_prop, ARRAY_SIZE(sde_qos_prop),
			prop_count, NULL);
	if (rc)
		goto freeprop;

	rc = _read_dt_entry(np, sde_qos_prop, ARRAY_SIZE(sde_qos_prop),
			prop_count, prop_exists, prop_value);
	if (rc)
		goto freeprop;

	rc = _sde_qos_parse_dt_cfg(cfg, prop_count, prop_value, prop_exists);

freeprop:
	kfree(prop_value);
end:
	return rc;
}

static int sde_parse_merge_3d_dt(struct device_node *np,
		struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[HW_PROP_MAX], off_count, i;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[HW_PROP_MAX];
	struct sde_merge_3d_cfg *merge_3d;

	prop_value = kcalloc(HW_PROP_MAX, sizeof(struct sde_prop_value),
			GFP_KERNEL);
	if (!prop_value)
		return -ENOMEM;

	rc = _validate_dt_entry(np, merge_3d_prop, ARRAY_SIZE(merge_3d_prop),
		prop_count, &off_count);
	if (rc)
		goto end;

	sde_cfg->merge_3d_count = off_count;

	rc = _read_dt_entry(np, merge_3d_prop, ARRAY_SIZE(merge_3d_prop),
			prop_count,
			prop_exists, prop_value);
	if (rc) {
		sde_cfg->merge_3d_count = 0;
		goto end;
	}

	for (i = 0; i < off_count; i++) {
		merge_3d = sde_cfg->merge_3d + i;
		merge_3d->base = PROP_VALUE_ACCESS(prop_value, HW_OFF, i);
		merge_3d->id = MERGE_3D_0 + i;
		snprintf(merge_3d->name, SDE_HW_BLK_NAME_LEN, "merge_3d_%u",
				merge_3d->id -  MERGE_3D_0);
		merge_3d->len = PROP_VALUE_ACCESS(prop_value, HW_LEN, 0);
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_qdss_parse_dt(struct device_node *np,
			struct sde_mdss_cfg *sde_cfg)
{
	int rc, prop_count[HW_PROP_MAX], i;
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[HW_PROP_MAX];
	u32 off_count;
	struct sde_qdss_cfg *qdss;

	if (!sde_cfg) {
		SDE_ERROR("invalid argument\n");
		return -EINVAL;
	}

	prop_value = kzalloc(HW_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value)
		return -ENOMEM;

	rc = _validate_dt_entry(np, qdss_prop, ARRAY_SIZE(qdss_prop),
			prop_count, &off_count);
	if (rc) {
		sde_cfg->qdss_count = 0;
		goto end;
	}

	sde_cfg->qdss_count = off_count;

	rc = _read_dt_entry(np, qdss_prop, ARRAY_SIZE(qdss_prop), prop_count,
			prop_exists, prop_value);
	if (rc)
		goto end;

	for (i = 0; i < off_count; i++) {
		qdss = sde_cfg->qdss + i;
		qdss->base = PROP_VALUE_ACCESS(prop_value, HW_OFF, i);
		qdss->id = QDSS_0 + i;
		snprintf(qdss->name, SDE_HW_BLK_NAME_LEN, "qdss_%u",
				qdss->id - QDSS_0);
		qdss->len = PROP_VALUE_ACCESS(prop_value, HW_LEN, 0);
	}

end:
	kfree(prop_value);
	return rc;
}

static int sde_hardware_format_caps(struct sde_mdss_cfg *sde_cfg,
	uint32_t hw_rev)
{
	int rc = 0;
	uint32_t dma_list_size, vig_list_size, wb2_list_size, wb_rot_fmt_list_size;
	uint32_t virt_vig_list_size, in_rot_list_size = 0;
	uint32_t index = 0;
	uint32_t in_rot_restricted_list_size = 0;
	const struct sde_format_extended *inline_fmt_tbl = NULL;
	const struct sde_format_extended *inline_restricted_fmt_tbl = NULL;

	/* DMA pipe input formats */
	dma_list_size = ARRAY_SIZE(plane_formats);
	if (test_bit(SDE_FEATURE_FP16, sde_cfg->features))
		dma_list_size += ARRAY_SIZE(fp16_formats);

	sde_cfg->dma_formats = kcalloc(dma_list_size,
		sizeof(struct sde_format_extended), GFP_KERNEL);
	if (!sde_cfg->dma_formats) {
		rc = -ENOMEM;
		goto out;
	}

	index = sde_copy_formats(sde_cfg->dma_formats, dma_list_size,
			0, plane_formats, ARRAY_SIZE(plane_formats));
	if (test_bit(SDE_FEATURE_FP16, sde_cfg->features))
		index += sde_copy_formats(sde_cfg->dma_formats, dma_list_size,
			index, fp16_formats, ARRAY_SIZE(fp16_formats));

	/* ViG pipe input formats */
	vig_list_size = ARRAY_SIZE(plane_formats_vig);
	if (test_bit(SDE_FEATURE_VIG_P010, sde_cfg->features))
		vig_list_size += ARRAY_SIZE(p010_ubwc_formats);
	if (test_bit(SDE_FEATURE_FP16, sde_cfg->features))
		vig_list_size += ARRAY_SIZE(fp16_formats);

	sde_cfg->vig_formats = kcalloc(vig_list_size,
		sizeof(struct sde_format_extended), GFP_KERNEL);
	if (!sde_cfg->vig_formats) {
		rc = -ENOMEM;
		goto free_dma;
	}

	index = sde_copy_formats(sde_cfg->vig_formats, vig_list_size,
			0, plane_formats_vig, ARRAY_SIZE(plane_formats_vig));
	if (test_bit(SDE_FEATURE_VIG_P010, sde_cfg->features))
		index += sde_copy_formats(sde_cfg->vig_formats,
				vig_list_size, index, p010_ubwc_formats,
				ARRAY_SIZE(p010_ubwc_formats));
	if (test_bit(SDE_FEATURE_FP16, sde_cfg->features))
		index += sde_copy_formats(sde_cfg->vig_formats, vig_list_size,
			index, fp16_formats, ARRAY_SIZE(fp16_formats));

	/* Virtual ViG pipe input formats (all virt pipes use DMA formats) */
	virt_vig_list_size = ARRAY_SIZE(plane_formats);
	if (test_bit(SDE_FEATURE_FP16, sde_cfg->features))
		virt_vig_list_size += ARRAY_SIZE(fp16_formats);

	sde_cfg->virt_vig_formats = kcalloc(virt_vig_list_size,
		sizeof(struct sde_format_extended), GFP_KERNEL);
	if (!sde_cfg->virt_vig_formats) {
		rc = -ENOMEM;
		goto free_vig;
	}

	index = sde_copy_formats(sde_cfg->virt_vig_formats, virt_vig_list_size,
			0, plane_formats, ARRAY_SIZE(plane_formats));
	if (test_bit(SDE_FEATURE_FP16, sde_cfg->features))
		index += sde_copy_formats(sde_cfg->virt_vig_formats,
				virt_vig_list_size, index, fp16_formats,
				ARRAY_SIZE(fp16_formats));

	/* WB output formats */
	wb2_list_size = ARRAY_SIZE(wb2_formats);
	sde_cfg->wb_formats = kcalloc(wb2_list_size,
		sizeof(struct sde_format_extended), GFP_KERNEL);
	if (!sde_cfg->wb_formats) {
		SDE_ERROR("failed to allocate wb format list\n");
		rc = -ENOMEM;
		goto free_virt;
	}

	index = sde_copy_formats(sde_cfg->wb_formats, wb2_list_size,
			0, wb2_formats, ARRAY_SIZE(wb2_formats));

	/* WB rotation output formats */
	wb_rot_fmt_list_size = ARRAY_SIZE(wb_rot_formats);
	sde_cfg->wb_rot_formats = kcalloc(wb_rot_fmt_list_size,
			sizeof(struct sde_format_extended), GFP_KERNEL);
	if (!sde_cfg->wb_rot_formats) {
		rc = -ENOMEM;
		goto free_wb;
	}

	index = sde_copy_formats(sde_cfg->wb_rot_formats, wb_rot_fmt_list_size,
			0, wb_rot_formats, ARRAY_SIZE(wb_rot_formats));

	/* Rotation enabled input formats */
	if (IS_SDE_INLINE_ROT_REV_100(sde_cfg->true_inline_rot_rev)) {
		inline_fmt_tbl = true_inline_rot_v1_fmts;
		in_rot_list_size = ARRAY_SIZE(true_inline_rot_v1_fmts);
	} else if (IS_SDE_INLINE_ROT_REV_200(sde_cfg->true_inline_rot_rev)) {
		inline_fmt_tbl = true_inline_rot_v2_fmts;
		in_rot_list_size = ARRAY_SIZE(true_inline_rot_v2_fmts);
	} else if (IS_SDE_INLINE_ROT_REV_201(sde_cfg->true_inline_rot_rev)) {
		inline_fmt_tbl = true_inline_rot_v201_fmts;
		in_rot_list_size = ARRAY_SIZE(true_inline_rot_v201_fmts);
		inline_restricted_fmt_tbl = true_inline_rot_v201_restricted_fmts;
		in_rot_restricted_list_size = ARRAY_SIZE(true_inline_rot_v201_fmts);
	}

	if (in_rot_list_size) {
		sde_cfg->inline_rot_formats = kcalloc(in_rot_list_size,
			sizeof(struct sde_format_extended), GFP_KERNEL);
		if (!sde_cfg->inline_rot_formats) {
			SDE_ERROR("failed to alloc inline rot format list\n");
			rc = -ENOMEM;
			goto free_wb_rot;
		}

		index = sde_copy_formats(sde_cfg->inline_rot_formats,
			in_rot_list_size, 0, inline_fmt_tbl, in_rot_list_size);
	}

	if (in_rot_restricted_list_size) {
		sde_cfg->inline_rot_restricted_formats = kcalloc(in_rot_restricted_list_size,
			sizeof(struct sde_format_extended), GFP_KERNEL);
		if (!sde_cfg->inline_rot_restricted_formats) {
			SDE_ERROR("failed to alloc inline rot restricted format list\n");
			rc = -ENOMEM;
			goto free_in_rot;
		}

		index = sde_copy_formats(sde_cfg->inline_rot_restricted_formats,
			in_rot_restricted_list_size, 0, inline_restricted_fmt_tbl,
			in_rot_restricted_list_size);
	}

	return 0;
free_in_rot:
	kfree(sde_cfg->inline_rot_formats);
free_wb_rot:
	kfree(sde_cfg->wb_rot_formats);
free_wb:
	kfree(sde_cfg->wb_formats);
free_virt:
	kfree(sde_cfg->virt_vig_formats);
free_vig:
	kfree(sde_cfg->vig_formats);
free_dma:
	kfree(sde_cfg->dma_formats);
out:
	return rc;
}

static void _sde_hw_setup_uidle(struct sde_uidle_cfg *uidle_cfg)
{
	if (!uidle_cfg->uidle_rev)
		return;

	uidle_cfg->fal10_exit_cnt = SDE_UIDLE_FAL10_EXIT_CNT;
	uidle_cfg->fal10_exit_danger = SDE_UIDLE_FAL10_EXIT_DANGER;
	uidle_cfg->fal10_danger = SDE_UIDLE_FAL10_DANGER;
	uidle_cfg->fal10_target_idle_time = SDE_UIDLE_FAL10_TARGET_IDLE;
	uidle_cfg->fal1_target_idle_time = SDE_UIDLE_FAL1_TARGET_IDLE;
	uidle_cfg->max_dwnscale = SDE_UIDLE_MAX_DWNSCALE;
	uidle_cfg->debugfs_ctrl = true;
	uidle_cfg->fal1_max_threshold = SDE_UIDLE_FAL1_MAX_THRESHOLD;

	if (IS_SDE_UIDLE_REV_100(uidle_cfg->uidle_rev)) {
		uidle_cfg->fal10_threshold =
			SDE_UIDLE_FAL10_THRESHOLD_60;
		uidle_cfg->max_fps = SDE_UIDLE_MAX_FPS_60;
	} else if (IS_SDE_UIDLE_REV_101(uidle_cfg->uidle_rev)) {
		set_bit(SDE_UIDLE_QACTIVE_OVERRIDE,
				&uidle_cfg->features);
		uidle_cfg->fal10_threshold =
			SDE_UIDLE_FAL10_THRESHOLD_90;
		uidle_cfg->max_fps = SDE_UIDLE_MAX_FPS_90;
	} else if (IS_SDE_UIDLE_REV_102(uidle_cfg->uidle_rev)) {
		set_bit(SDE_UIDLE_QACTIVE_OVERRIDE,
				&uidle_cfg->features);
		uidle_cfg->fal10_threshold =
			SDE_UIDLE_FAL10_THRESHOLD_90;
		uidle_cfg->max_fps = SDE_UIDLE_MAX_FPS_90;
		uidle_cfg->max_fal1_fps = SDE_UIDLE_MAX_FPS_240;
		uidle_cfg->fal1_max_threshold =
				SDE_UIDLE_FAL1_MAX_THRESHOLD_EXT_REV_102;
	} else if (IS_SDE_UIDLE_REV_103(uidle_cfg->uidle_rev)) {
		set_bit(SDE_UIDLE_QACTIVE_OVERRIDE, &uidle_cfg->features);
		uidle_cfg->max_fps = SDE_UIDLE_MAX_FPS_240;
		uidle_cfg->max_fal1_fps = SDE_UIDLE_MAX_FPS_240;
		uidle_cfg->fal1_max_threshold = SDE_UIDLE_FAL1_MAX_THRESHOLD_EXT_REV_103;
		uidle_cfg->fal10_threshold = SDE_UIDLE_FAL10_THRESHOLD_60;
	} else if (IS_SDE_UIDLE_REV_104(uidle_cfg->uidle_rev)) {
		set_bit(SDE_UIDLE_QACTIVE_OVERRIDE, &uidle_cfg->features);
		uidle_cfg->max_fps = SDE_UIDLE_MAX_FPS_240;
		uidle_cfg->max_fal1_fps = SDE_UIDLE_MAX_FPS_240;
		uidle_cfg->fal1_max_threshold = SDE_UIDLE_FAL1_MAX_THRESHOLD_EXT_REV_104;
		uidle_cfg->fal10_threshold = SDE_UIDLE_FAL10_THRESHOLD_60;
		set_bit(SDE_UIDLE_WB_FAL_STATUS, &uidle_cfg->features);
	}
}

static int _sde_hardware_pre_caps(struct sde_mdss_cfg *sde_cfg, uint32_t hw_rev)
{
	int rc = 0, i;

	if (!sde_cfg)
		return -EINVAL;

	/* default settings for *MOST* targets */
	set_bit(SDE_FEATURE_COMBINED_ALPHA, sde_cfg->features);
	set_bit(SDE_FEATURE_DELAY_PRG_FETCH, sde_cfg->features);
	set_bit(SDE_FEATURE_SUI_MISR, sde_cfg->features);
	set_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features);
	set_bit(SDE_FEATURE_SUI_NS_ALLOWED, sde_cfg->features);
	set_bit(SDE_FEATURE_HDR, sde_cfg->features);
	sde_cfg->mdss_hw_block_size = DEFAULT_MDSS_HW_BLOCK_SIZE;

	/* Set target specific value based on sytems recommendation if not same as default value */
	sde_cfg->ppb_buf_max_lines = DEFAULT_PPB_BUF_MAX_LINES;

	for (i = 0; i < SSPP_MAX; i++) {
		sde_cfg->demura_supported[i][0] = ~0x0;
		sde_cfg->demura_supported[i][1] = ~0x0;
	}

	/* target specific settings */
	if (IS_MSM8996_TARGET(hw_rev)) {
		sde_cfg->perf.min_prefill_lines = 21;
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		clear_bit(SDE_FEATURE_COMBINED_ALPHA, sde_cfg->features);
		clear_bit(SDE_FEATURE_HDR, sde_cfg->features);
		clear_bit(SDE_FEATURE_DELAY_PRG_FETCH, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_MISR, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_NS_ALLOWED, sde_cfg->features);
	} else if (IS_MSM8998_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 25;
		sde_cfg->vbif_qos_nlvl = 4;
		sde_cfg->ts_prefill_rev = 1;
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		clear_bit(SDE_FEATURE_COMBINED_ALPHA, sde_cfg->features);
		clear_bit(SDE_FEATURE_DELAY_PRG_FETCH, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_MISR, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_NS_ALLOWED, sde_cfg->features);
	} else if (IS_SDM845_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->sui_block_xin_mask = 0x3F71;
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		clear_bit(SDE_FEATURE_DELAY_PRG_FETCH, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_NS_ALLOWED, sde_cfg->features);
	} else if (IS_SDM670_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		clear_bit(SDE_FEATURE_DELAY_PRG_FETCH, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_MISR, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_NS_ALLOWED, sde_cfg->features);
	} else if (IS_SM8150_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0x3F71;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
	} else if (IS_SDMSHRIKE_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_MISR, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features);
		clear_bit(SDE_FEATURE_SUI_NS_ALLOWED, sde_cfg->features);
	} else if (IS_SM6150_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		set_bit(SDE_FEATURE_DECIMATION, sde_cfg->features);
		sde_cfg->sui_block_xin_mask = 0x2EE1;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
	} else if (IS_SDMMAGPIE_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		clear_bit(SDE_FEATURE_HDR, sde_cfg->features);
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0xE71;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
	} else if (IS_KONA_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 35;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0x3F71;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_1_0_0;
		sde_cfg->uidle_cfg.uidle_rev = SDE_UIDLE_VERSION_1_0_0;
		set_bit(SDE_FEATURE_INLINE_DISABLE_CONST_CLR, sde_cfg->features);
	} else if (IS_SAIPAN_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0xE71;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_1_0_0;
		set_bit(SDE_FEATURE_INLINE_DISABLE_CONST_CLR, sde_cfg->features);
	} else if (IS_SDMTRINKET_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0xC61;
		clear_bit(SDE_FEATURE_HDR, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
	} else if (IS_BENGAL_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0xC01;
		clear_bit(SDE_FEATURE_HDR, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
	} else if (IS_LAGOON_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0x261;
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
	} else if (IS_SCUBA_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0x1;
		clear_bit(SDE_FEATURE_HDR, sde_cfg->features);
	} else if (IS_LAHAINA_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_DEMURA, sde_cfg->features);
		sde_cfg->demura_supported[SSPP_DMA1][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA1][1] = 1;
		sde_cfg->demura_supported[SSPP_DMA3][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA3][1] = 1;
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0x3F71;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_0;
		sde_cfg->uidle_cfg.uidle_rev = SDE_UIDLE_VERSION_1_0_1;
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
	} else if (IS_HOLI_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 24;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0xC01;
		clear_bit(SDE_FEATURE_HDR, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_FEATURE_RC_LM_FLUSH_OVERRIDE, sde_cfg->features);
	} else if (IS_SHIMA_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 35;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0xE71;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_1_0_0;
		set_bit(SDE_FEATURE_INLINE_DISABLE_CONST_CLR, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
	} else if (IS_WAIPIO_TARGET(hw_rev) || IS_CAPE_TARGET(hw_rev)) {
		sde_cfg->allowed_dsc_reservation_switch = SDE_DP_DSC_RESERVATION_SWITCH;
		set_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_FEATURE_INLINE_SKIP_THRESHOLD, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_1;
		sde_cfg->uidle_cfg.uidle_rev = SDE_UIDLE_VERSION_1_0_2;
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features);
		set_bit(SDE_FEATURE_FP16, sde_cfg->features);
		set_bit(SDE_MDP_PERIPH_TOP_0_REMOVED, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_DEMURA, sde_cfg->features);
		sde_cfg->demura_supported[SSPP_DMA1][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA1][1] = 1;
		sde_cfg->demura_supported[SSPP_DMA3][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA3][1] = 1;
		set_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features);
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
		set_bit(SDE_FEATURE_AVR_STEP, sde_cfg->features);
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
	} else if (IS_YUPIK_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->sui_block_xin_mask = 0x261;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_0;
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_FEATURE_RC_LM_FLUSH_OVERRIDE, sde_cfg->features);
		sde_cfg->cwb_virtual_mixers_mask = 0x2;
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
	} else if (IS_DIWALI_TARGET(hw_rev)) {
		sde_cfg->allowed_dsc_reservation_switch = SDE_DP_DSC_RESERVATION_SWITCH;
		set_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->has_reduced_ob_max = true;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_FEATURE_INLINE_SKIP_THRESHOLD, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_1;
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features);
		set_bit(SDE_FEATURE_FP16, sde_cfg->features);
		set_bit(SDE_MDP_PERIPH_TOP_0_REMOVED, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
		set_bit(SDE_FEATURE_AVR_STEP, sde_cfg->features);
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features);
		set_bit(SDE_FEATURE_DEMURA, sde_cfg->features);
		sde_cfg->demura_supported[SSPP_DMA1][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA1][1] = 1;
	} else if (IS_LEMANS_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_FEATURE_INLINE_SKIP_THRESHOLD, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_1;
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		sde_cfg->mdss_hw_block_size = 0x158;
		set_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features);
		set_bit(SDE_FEATURE_FP16, sde_cfg->features);
		set_bit(SDE_MDP_PERIPH_TOP_0_REMOVED, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features);
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
	} else if (IS_KALAMA_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_FEATURE_INLINE_SKIP_THRESHOLD, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		set_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features);
		set_bit(SDE_FEATURE_FP16, sde_cfg->features);
		set_bit(SDE_MDP_PERIPH_TOP_0_REMOVED, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_DEMURA, sde_cfg->features);
		set_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features);
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
		set_bit(SDE_FEATURE_AVR_STEP, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_CLK_SPLIT, sde_cfg->features);
		set_bit(SDE_FEATURE_CTL_DONE, sde_cfg->features);
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_SYS_CACHE_DISP_1, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_SYS_CACHE_DISP_WB, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_FEATURE_SYS_CACHE_NSE, sde_cfg->features);
		sde_cfg->allowed_dsc_reservation_switch = SDE_DP_DSC_RESERVATION_SWITCH;
		sde_cfg->autorefresh_disable_seq = AUTOREFRESH_DISABLE_SEQ2;
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->qos_target_time_ns = 11160;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_1;
		sde_cfg->uidle_cfg.uidle_rev = SDE_UIDLE_VERSION_1_0_3;
		sde_cfg->sid_rev = SDE_SID_VERSION_2_0_0;
		sde_cfg->mdss_hw_block_size = 0x158;
		sde_cfg->demura_supported[SSPP_DMA1][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA1][1] = 1;
		sde_cfg->demura_supported[SSPP_DMA3][0] = 0;
		sde_cfg->demura_supported[SSPP_DMA3][1] = 1;
		sde_cfg->has_line_insertion = true;
	} else if (IS_PINEAPPLE_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_DUAL_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_FEATURE_INLINE_SKIP_THRESHOLD, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		set_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features);
		set_bit(SDE_FEATURE_FP16, sde_cfg->features);
		set_bit(SDE_MDP_PERIPH_TOP_0_REMOVED, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_DEMURA, sde_cfg->features);
		set_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features);
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
		set_bit(SDE_FEATURE_AVR_STEP, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_CLK_SPLIT, sde_cfg->features);
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_FEATURE_CTL_DONE, sde_cfg->features);
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_SYS_CACHE_DISP_WB, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_FEATURE_SYS_CACHE_NSE, sde_cfg->features);
		set_bit(SDE_FEATURE_SYS_CACHE_STALING, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_ROTATION, sde_cfg->features);
		set_bit(SDE_FEATURE_EPT, sde_cfg->features);
		sde_cfg->allowed_dsc_reservation_switch = SDE_DP_DSC_RESERVATION_SWITCH;
		sde_cfg->autorefresh_disable_seq = AUTOREFRESH_DISABLE_SEQ2;
		/* if pingpong block supports it this should not be set on top block */
		sde_cfg->ppb_sz_program = SDE_PPB_SIZE_THRU_TOP;
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->qos_target_time_ns = 11160;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_1;
		sde_cfg->uidle_cfg.uidle_rev = SDE_UIDLE_VERSION_1_0_4;
		sde_cfg->sid_rev = SDE_SID_VERSION_2_0_0;
		sde_cfg->mdss_hw_block_size = 0x158;
		sde_cfg->demura_supported[SSPP_DMA1][0] = BIT(DEMURA_0) | BIT(DEMURA_2);
		sde_cfg->demura_supported[SSPP_DMA1][1] = BIT(DEMURA_1) | BIT(DEMURA_3);
		sde_cfg->demura_supported[SSPP_DMA3][0] = BIT(DEMURA_0) | BIT(DEMURA_2);
		sde_cfg->demura_supported[SSPP_DMA3][1] = BIT(DEMURA_1) | BIT(DEMURA_3);
		sde_cfg->has_line_insertion = true;
	} else if (IS_CLIFFS_TARGET(hw_rev)) {
		set_bit(SDE_FEATURE_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_DUAL_DEDICATED_CWB, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_DITHER, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_UBWC, sde_cfg->features);
		set_bit(SDE_FEATURE_CWB_CROP, sde_cfg->features);
		set_bit(SDE_FEATURE_QSYNC, sde_cfg->features);
		set_bit(SDE_FEATURE_3D_MERGE_RESET, sde_cfg->features);
		set_bit(SDE_FEATURE_HDR_PLUS, sde_cfg->features);
		set_bit(SDE_FEATURE_INLINE_SKIP_THRESHOLD, sde_cfg->features);
		set_bit(SDE_MDP_DHDR_MEMPOOL_4K, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_VIG_P010, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_DISABLE_SHAREABLE, sde_cfg->features);
		set_bit(SDE_FEATURE_DITHER_LUMA_MODE, sde_cfg->features);
		set_bit(SDE_FEATURE_MULTIRECT_ERROR, sde_cfg->features);
		set_bit(SDE_FEATURE_FP16, sde_cfg->features);
		set_bit(SDE_MDP_PERIPH_TOP_0_REMOVED, &sde_cfg->mdp[0].features);
		set_bit(SDE_FEATURE_DEMURA, sde_cfg->features);
		set_bit(SDE_FEATURE_UBWC_STATS, sde_cfg->features);
		set_bit(SDE_FEATURE_HW_VSYNC_TS, sde_cfg->features);
		set_bit(SDE_FEATURE_AVR_STEP, sde_cfg->features);
		set_bit(SDE_FEATURE_VBIF_CLK_SPLIT, sde_cfg->features);
		set_bit(SDE_FEATURE_TRUSTED_VM, sde_cfg->features);
		set_bit(SDE_FEATURE_CTL_DONE, sde_cfg->features);
		set_bit(SDE_SYS_CACHE_DISP, sde_cfg->sde_sys_cache_type_map);
		set_bit(SDE_FEATURE_SYS_CACHE_NSE, sde_cfg->features);
		set_bit(SDE_FEATURE_SYS_CACHE_STALING, sde_cfg->features);
		set_bit(SDE_FEATURE_WB_ROTATION, sde_cfg->features);
		set_bit(SDE_FEATURE_EPT, sde_cfg->features);
		sde_cfg->allowed_dsc_reservation_switch = SDE_DP_DSC_RESERVATION_SWITCH;
		sde_cfg->autorefresh_disable_seq = AUTOREFRESH_DISABLE_SEQ2;
		/* if pingpong block supports it this should not be set on top block */
		sde_cfg->ppb_sz_program = SDE_PPB_SIZE_THRU_TOP;
		sde_cfg->perf.min_prefill_lines = 40;
		sde_cfg->vbif_qos_nlvl = 8;
		sde_cfg->qos_target_time_ns = 11160;
		sde_cfg->ts_prefill_rev = 2;
		sde_cfg->ctl_rev = SDE_CTL_CFG_VERSION_1_0_0;
		sde_cfg->true_inline_rot_rev = SDE_INLINE_ROT_VERSION_2_0_1;
		sde_cfg->sid_rev = SDE_SID_VERSION_2_0_0;
		sde_cfg->mdss_hw_block_size = 0x158;
		sde_cfg->demura_supported[SSPP_DMA1][0] = BIT(DEMURA_0) | BIT(DEMURA_2);
		sde_cfg->demura_supported[SSPP_DMA1][1] = BIT(DEMURA_1);
		sde_cfg->demura_supported[SSPP_DMA3][0] = BIT(DEMURA_0) | BIT(DEMURA_2);
		sde_cfg->demura_supported[SSPP_DMA3][1] = BIT(DEMURA_1);
		sde_cfg->has_line_insertion = true;
	} else {
		SDE_ERROR("unsupported chipset id:%X\n", hw_rev);
		sde_cfg->perf.min_prefill_lines = 0xffff;
		rc = -ENODEV;
	}

	if (!rc)
		rc = sde_hardware_format_caps(sde_cfg, hw_rev);

	_sde_hw_setup_uidle(&sde_cfg->uidle_cfg);

	return rc;
}

static int _sde_hw_dnsc_blur_filter_caps(struct sde_mdss_cfg *sde_cfg)
{
	const struct sde_dnsc_blur_filter_info *filters = NULL;
	u32 size;
	int rc = 0;

	if (!sde_cfg->dnsc_blur_count || !sde_cfg->dnsc_blur_rev)
		return 0;

	if (IS_SDE_DNSC_BLUR_REV_100(sde_cfg->dnsc_blur_rev)) {
		filters = dnsc_blur_v100_filters;
		size = ARRAY_SIZE(dnsc_blur_v100_filters);
	}

	if (filters) {
		sde_cfg->dnsc_blur_filters = kcalloc(size,
				sizeof(struct sde_dnsc_blur_filter_info), GFP_KERNEL);
		if (!sde_cfg->dnsc_blur_filters) {
			SDE_ERROR("failed to alloc dnsc_blur filter list\n");
			rc = -ENOMEM;
			goto end;
		}
		memcpy(sde_cfg->dnsc_blur_filters, filters,
				sizeof(struct sde_dnsc_blur_filter_info) * size);
		sde_cfg->dnsc_blur_filter_count = size;

	}

end:
	return rc;
}

static void _sde_hw_fence_caps(struct sde_mdss_cfg *sde_cfg)
{
	struct sde_ctl_cfg *ctl;
	int i;

	if (!sde_cfg->hw_fence_rev)
		return;

	set_bit(SDE_FEATURE_HW_FENCE_IPCC, sde_cfg->features);

	if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_A00))
		set_bit(SDE_MDP_HW_FENCE_DIR_WRITE, &sde_cfg->mdp[0].features);

	for (i = 0; i < sde_cfg->ctl_count; i++) {
		ctl = sde_cfg->ctl + i;
		set_bit(SDE_CTL_HW_FENCE, &ctl->features);
		if (SDE_HW_MAJOR(sde_cfg->hw_rev) >= SDE_HW_MAJOR(SDE_HW_VER_A00)) {
			set_bit(SDE_CTL_HW_FENCE_TRIGGER_SEL, &ctl->features);
			set_bit(SDE_CTL_HW_FENCE_DIR_WRITE, &ctl->features);
		}
	}
}

static int _sde_hardware_post_caps(struct sde_mdss_cfg *sde_cfg,
	uint32_t hw_rev)
{
	int rc = 0, i;
	u32 max_horz_deci = 0, max_vert_deci = 0;

	if (!sde_cfg)
		return -EINVAL;

	if (test_bit(SDE_FEATURE_SUI_BLENDSTAGE, sde_cfg->features))
		sde_cfg->sui_supported_blendstage = sde_cfg->max_mixer_blendstages - SDE_STAGE_0;

	for (i = 0; i < sde_cfg->sspp_count; i++) {
		if (sde_cfg->sspp[i].sblk) {
			max_horz_deci = max(max_horz_deci,
				sde_cfg->sspp[i].sblk->maxhdeciexp);
			max_vert_deci = max(max_vert_deci,
				sde_cfg->sspp[i].sblk->maxvdeciexp);
		}

		/*
		 * set sec-ui blocked SSPP feature flag based on blocked
		 * xin-mask if sec-ui-misr feature is enabled;
		 */
		if (test_bit(SDE_FEATURE_SUI_MISR, sde_cfg->features) &&
		    (sde_cfg->sui_block_xin_mask & BIT(sde_cfg->sspp[i].xin_id)))
			set_bit(SDE_SSPP_BLOCK_SEC_UI, &sde_cfg->sspp[i].features);
	}

	if (max_horz_deci)
		sde_cfg->max_display_width = sde_cfg->max_sspp_linewidth *
			max_horz_deci;
	else
		sde_cfg->max_display_width = sde_cfg->max_sspp_linewidth *
			MAX_DOWNSCALE_RATIO;

	if (max_vert_deci)
		sde_cfg->max_display_height =
			MAX_DISPLAY_HEIGHT_WITH_DECIMATION * max_vert_deci;
	else
		sde_cfg->max_display_height = MAX_DISPLAY_HEIGHT_WITH_DECIMATION
			* MAX_DOWNSCALE_RATIO;

	sde_cfg->min_display_height = MIN_DISPLAY_HEIGHT;
	sde_cfg->min_display_width = MIN_DISPLAY_WIDTH;
	sde_cfg->max_cwb = min_t(u32, sde_cfg->wb_count, MAX_CWB_SESSIONS);
	if (test_bit(SDE_FEATURE_DUAL_DEDICATED_CWB, sde_cfg->features))
		sde_cfg->max_cwb = 2;
	else
		sde_cfg->max_cwb = 1;

	_sde_hw_fence_caps(sde_cfg);

	rc = _sde_hw_dnsc_blur_filter_caps(sde_cfg);

	return rc;
}

void sde_hw_catalog_deinit(struct sde_mdss_cfg *sde_cfg)
{
	int i, j;

	if (!sde_cfg)
		return;

	sde_hw_catalog_irq_offset_list_delete(&sde_cfg->irq_offset_list);

	for (i = 0; i < sde_cfg->sspp_count; i++)
		kfree(sde_cfg->sspp[i].sblk);

	for (i = 0; i < sde_cfg->mixer_count; i++)
		kfree(sde_cfg->mixer[i].sblk);

	for (i = 0; i < sde_cfg->wb_count; i++)
		kfree(sde_cfg->wb[i].sblk);

	for (i = 0; i < sde_cfg->dspp_count; i++)
		kfree(sde_cfg->dspp[i].sblk);

	if (sde_cfg->ds_count)
		kfree(sde_cfg->ds[0].top);

	for (i = 0; i < sde_cfg->pingpong_count; i++)
		kfree(sde_cfg->pingpong[i].sblk);

	for (i = 0; i < sde_cfg->vdc_count; i++)
		kfree(sde_cfg->vdc[i].sblk);

	for (i = 0; i < sde_cfg->dnsc_blur_count; i++)
		kfree(sde_cfg->dnsc_blur[i].sblk);

	for (i = 0; i < sde_cfg->vbif_count; i++) {
		kfree(sde_cfg->vbif[i].dynamic_ot_rd_tbl.cfg);
		kfree(sde_cfg->vbif[i].dynamic_ot_wr_tbl.cfg);

		for (j = VBIF_RT_CLIENT; j < VBIF_MAX_CLIENT; j++)
			kfree(sde_cfg->vbif[i].qos_tbl[j].priority_lvl);
	}

	for (i = 0; i < SDE_SYS_CACHE_MAX; i++)
		if (sde_cfg->sc_cfg[i].slice)
			llcc_slice_putd(sde_cfg->sc_cfg[i].slice);

	kfree(sde_cfg->perf.qos_refresh_rate);
	kfree(sde_cfg->perf.danger_lut);
	kfree(sde_cfg->perf.safe_lut);
	kfree(sde_cfg->perf.creq_lut);

	kfree(sde_cfg->dma_formats);
	kfree(sde_cfg->vig_formats);
	kfree(sde_cfg->wb_formats);
	kfree(sde_cfg->wb_rot_formats);
	kfree(sde_cfg->virt_vig_formats);
	kfree(sde_cfg->inline_rot_formats);

	kfree(sde_cfg->dnsc_blur_filters);

	kfree(sde_cfg);
}

static int sde_hw_ver_parse_dt(struct drm_device *dev, struct device_node *np,
			struct sde_mdss_cfg *cfg)
{
	int rc, len, prop_count[SDE_HW_PROP_MAX];
	struct sde_prop_value *prop_value = NULL;
	bool prop_exists[SDE_HW_PROP_MAX];

	if (!cfg) {
		SDE_ERROR("invalid argument\n");
		return -EINVAL;
	}

	prop_value = kzalloc(SDE_HW_PROP_MAX *
			sizeof(struct sde_prop_value), GFP_KERNEL);
	if (!prop_value)
		return -ENOMEM;

	rc = _validate_dt_entry(np, sde_hw_prop, ARRAY_SIZE(sde_hw_prop),
			prop_count, &len);
	if (rc)
		goto end;

	rc = _read_dt_entry(np, sde_hw_prop, ARRAY_SIZE(sde_hw_prop),
			prop_count, prop_exists, prop_value);
	if (rc)
		goto end;

	if (prop_exists[SDE_HW_VERSION])
		cfg->hw_rev = PROP_VALUE_ACCESS(prop_value, SDE_HW_VERSION, 0);
	else
		cfg->hw_rev = sde_kms_get_hw_version(dev);

	if (prop_exists[SDE_HW_FENCE_VERSION])
		cfg->hw_fence_rev = PROP_VALUE_ACCESS(prop_value, SDE_HW_FENCE_VERSION, 0);
	else
		cfg->hw_fence_rev = 0; /* disable hw-fences */

end:
	kfree(prop_value);
	return rc;
}

/*************************************************************
 * hardware catalog init
 *************************************************************/
struct sde_mdss_cfg *sde_hw_catalog_init(struct drm_device *dev)
{
	int rc;
	struct sde_mdss_cfg *sde_cfg;
	struct device_node *np = dev->dev->of_node;

	if (!np)
		return ERR_PTR(-EINVAL);

	sde_cfg = kzalloc(sizeof(*sde_cfg), GFP_KERNEL);
	if (!sde_cfg)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&sde_cfg->irq_offset_list);

	rc = sde_hw_ver_parse_dt(dev, np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_hardware_pre_caps(sde_cfg, sde_cfg->hw_rev);
	if (rc)
		goto end;

	rc = sde_top_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_perf_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_qos_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	/* uidle must be done before sspp and ctl,
	 * so if something goes wrong, we won't
	 * enable it in ctl and sspp.
	 */
	rc = sde_uidle_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_cache_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_ctl_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_sspp_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_dspp_top_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_dspp_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_ds_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_dsc_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_vdc_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_pp_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	/* mixer parsing should be done after dspp,
	 * ds and pp for mapping setup
	 */
	rc = sde_mixer_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_intf_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_wb_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	/* cdm parsing should be done after intf and wb for mapping setup */
	rc = sde_cdm_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	/* dnsc_blur parsing should be done after wb for mapping setup */
	rc = sde_dnsc_blur_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_vbif_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_parse_reg_dma_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_parse_merge_3d_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = sde_qdss_parse_dt(np, sde_cfg);
	if (rc)
		goto end;

	rc = _sde_hardware_post_caps(sde_cfg, sde_cfg->hw_rev);
	if (rc)
		goto end;

	return sde_cfg;

end:
	sde_hw_catalog_deinit(sde_cfg);
	return NULL;
}
