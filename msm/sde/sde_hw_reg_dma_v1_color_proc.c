// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include <drm/msm_drm_pp.h>
#include "sde_reg_dma.h"
#include "sde_hw_reg_dma_v1_color_proc.h"
#include "sde_hw_color_proc_common_v4.h"
#include "sde_hw_ctl.h"
#include "sde_hw_sspp.h"
#include "sde_hwio.h"
#include "sde_hw_lm.h"
#include "sde_dbg.h"
#include "sde_hw_util.h"
#include "sde_kms.h"

/* Reserve space of 128 words for LUT dma payload set-up */
#define REG_DMA_HEADERS_BUFFER_SZ (sizeof(u32) * 128)

#define VLUT_MEM_SIZE ((128 * sizeof(u32)) + REG_DMA_HEADERS_BUFFER_SZ)
#define VLUT_LEN (128 * sizeof(u32))
#define PA_OP_MODE_OFF 0x800
#define PA_LUTV_OPMODE_OFF 0x84c
#define REG_DMA_PA_MODE_HSIC_MASK 0xE1EFFFFF
#define REG_DMA_PA_MODE_SZONE_MASK 0x1FEFFFFF
#define REG_DMA_PA_PWL_HOLD_SZONE_MASK 0x0FFF
#define PA_LUTV_DSPP_CTRL_OFF 0x4c
#define PA_LUTV_DSPP_SWAP_OFF 0x18
/**
 * the diff between LTM_INIT_ENABLE/DISABLE masks is portrait_en bits.
 * When disabling INIT property, we don't want to reset those bits since
 * they are needed for both LTM histogram and VLUT.
 */
#define REG_DMA_LTM_INIT_ENABLE_OP_MASK 0xFFFF8CAB
#define REG_DMA_LTM_INIT_DISABLE_OP_MASK 0xFFFF8CAF
#define REG_DMA_LTM_ROI_OP_MASK 0xFEFFFFFF
/**
 * the diff between LTM_VLUT_ENABLE/DISABLE masks are merge_mode, dither
 * strength and unsharp_gain bits. When disabling VLUT property, we want
 * to reset these bits since those are only valid if VLUT is enabled.
 */
#define REG_DMA_LTM_VLUT_ENABLE_OP_MASK 0xFEFCFFAD
#define REG_DMA_LTM_VLUT_DISABLE_OP_MASK 0xFEFF8CAD
#define REG_DMA_LTM_UPDATE_REQ_MASK 0xFFFFFFFE

#define REG_DMA_SPR_CONFIG_MASK ~0xFDFFFFFF

#define GAMUT_LUT_MEM_SIZE ((sizeof(struct drm_msm_3d_gamut)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define GAMUT_SCALE_OFF_LEN (GAMUT_3D_SCALE_OFF_SZ * sizeof(u32))
#define GAMUT_SCALE_OFF_LEN_12 (GAMUT_3D_SCALEB_OFF_SZ * sizeof(u32))

#define GC_LUT_MEM_SIZE ((sizeof(struct drm_msm_pgc_lut)) + \
		REG_DMA_HEADERS_BUFFER_SZ)

#define IGC_LUT_MEM_SIZE ((sizeof(struct drm_msm_igc_lut)) + \
		REG_DMA_HEADERS_BUFFER_SZ)

#define PCC_LUT_ENTRIES (PCC_NUM_PLANES * PCC_NUM_COEFF)
#define PCC_LEN (PCC_LUT_ENTRIES * sizeof(u32))
#define PCC_MEM_SIZE (PCC_LEN + \
		REG_DMA_HEADERS_BUFFER_SZ)

#define HSIC_MEM_SIZE ((sizeof(struct drm_msm_pa_hsic)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define SIXZONE_MEM_SIZE ((sizeof(struct drm_msm_sixzone)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define MEMCOLOR_MEM_SIZE ((sizeof(struct drm_msm_memcol)) + \
		REG_DMA_HEADERS_BUFFER_SZ)

#define RC_MASK_CFG_SIZE (sizeof(struct drm_msm_rc_mask_cfg) + \
		REG_DMA_HEADERS_BUFFER_SZ)
//TBD: exact size calculations
#define RC_PU_CFG_SIZE ((64 * sizeof(u32)) + \
		REG_DMA_HEADERS_BUFFER_SZ)

#define QSEED3_MEM_SIZE (sizeof(struct sde_hw_scaler3_cfg) + \
			(450 * sizeof(u32)) + \
			REG_DMA_HEADERS_BUFFER_SZ)
#define LTM_INIT_MEM_SIZE ((sizeof(struct drm_msm_ltm_init_param)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define LTM_ROI_MEM_SIZE ((sizeof(struct drm_msm_ltm_cfg_param)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define LTM_VLUT_MEM_SIZE ((sizeof(struct drm_msm_ltm_data)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define SPR_INIT_MEM_SIZE ((sizeof(struct drm_msm_spr_init_cfg)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define SPR_UDC_MEM_SIZE ((sizeof(struct drm_msm_spr_udc_cfg)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define DEMURA_MEM_SIZE ((sizeof(struct drm_msm_dem_cfg)) + \
		REG_DMA_HEADERS_BUFFER_SZ)
#define DEMURA_CFG0_PARAM2_MEM_SIZE ((sizeof(struct drm_msm_dem_cfg0_param2)) + \
		REG_DMA_HEADERS_BUFFER_SZ)

#define APPLY_MASK_AND_SHIFT(x, n, shift) ((x & (REG_MASK(n))) << (shift))
#define REG_DMA_VIG_GAMUT_OP_MASK 0x300
#define REG_DMA_VIG_IGC_OP_MASK 0x1001F
#define DMA_DGM_0_OP_MODE_OFF 0x604
#define DMA_DGM_1_OP_MODE_OFF 0x1604
#define REG_DMA_DMA_IGC_OP_MASK 0x10005
#define REG_DMA_DMA_GC_OP_MASK 0x10003
#define DMA_1D_LUT_IGC_LEN 128
#define DMA_1D_LUT_GC_LEN 128
#define DMA_1D_LUT_IGC_DITHER_OFF 0x408
#define VIG_1D_LUT_IGC_LEN 128
#define VIG_IGC_DATA_MASK (BIT(10) - 1)
#define VIG_IGC_DATA_MASK_V6 (BIT(12) - 1)

/* SDE_SCALER_QSEED3 */
#define QSEED3_DE_OFFSET                       0x24
#define QSEED3_COEF_LUT_SWAP_BIT           0
#define QSEED3_COEF_LUT_CTRL_OFF               0x4C

#define QSEED5_DE_LPF_OFFSET                   0x64
#define QSEED5_DEFAULT_DE_LPF_BLEND            0x3FF00000

/* SDE_SCALER_QSEED3LITE */
#define QSEED3L_COEF_LUT_SWAP_BIT          0
#define QSEED3L_COEF_LUT_Y_SEP_BIT         4
#define QSEED3L_COEF_LUT_UV_SEP_BIT        5
#define QSEED3L_COEF_LUT_CTRL_OFF              0x4c
#define Y_INDEX                            0
#define UV_INDEX                           1

#define REG_DMA_DSPP_GAMUT_OP_MASK 0xFFFFFFE0

#define LOG_FEATURE_OFF SDE_EVT32(ctx->idx, 0)
#define LOG_FEATURE_ON SDE_EVT32(ctx->idx, 1)

enum ltm_vlut_ops_bitmask {
	ltm_unsharp = BIT(0),
	ltm_dither = BIT(1),
	ltm_roi = BIT(2),
	ltm_vlut = BIT(3),
	ltm_ops_max = BIT(31),
};

static u32 ltm_vlut_ops_mask[LTM_MAX];

static struct sde_reg_dma_buffer *dspp_buf[REG_DMA_FEATURES_MAX][DSPP_MAX];
static struct sde_reg_dma_buffer
	*sspp_buf[SDE_SSPP_RECT_MAX][REG_DMA_FEATURES_MAX][SSPP_MAX];
static struct sde_reg_dma_buffer *ltm_buf[REG_DMA_FEATURES_MAX][LTM_MAX];

static u32 feature_map[SDE_DSPP_MAX] = {
	[SDE_DSPP_VLUT] = VLUT,
	[SDE_DSPP_GAMUT] = GAMUT,
	[SDE_DSPP_IGC] = IGC,
	[SDE_DSPP_PCC] = PCC,
	[SDE_DSPP_GC] = GC,
	[SDE_DSPP_HSIC] = HSIC,
	/* MEMCOLOR can be mapped to any MEMC_SKIN/SKY/FOLIAGE/PROT*/
	[SDE_DSPP_MEMCOLOR] = MEMC_SKIN,
	[SDE_DSPP_SIXZONE] = SIX_ZONE,
	/* SPR can be mapped to SPR_INIT & SPR_PU_CFG */
	[SDE_DSPP_SPR] = SPR_INIT,
	[SDE_DSPP_DITHER] = REG_DMA_FEATURES_MAX,
	[SDE_DSPP_HIST] = REG_DMA_FEATURES_MAX,
	[SDE_DSPP_AD] = REG_DMA_FEATURES_MAX,
	/* RC can be mapped to RC_MASK_CFG & RC_PU_CFG */
	[SDE_DSPP_RC] = RC_MASK_CFG,
	[SDE_DSPP_DEMURA] = DEMURA_CFG,
	[SDE_DSPP_DEMURA_CFG0_PARAM2] = DEMURA_CFG0_PARAM2,
};

static u32 sspp_feature_map[SDE_SSPP_MAX] = {
	[SDE_SSPP_VIG_IGC] = IGC,
	[SDE_SSPP_VIG_GAMUT] = GAMUT,
	[SDE_SSPP_DMA_IGC] = IGC,
	[SDE_SSPP_DMA_GC] = GC,
	[SDE_SSPP_SCALER_QSEED3] = QSEED,
	[SDE_SSPP_SCALER_QSEED3LITE] = QSEED,
};

static u32 ltm_feature_map[SDE_LTM_MAX] = {
	[SDE_LTM_INIT] = LTM_INIT,
	[SDE_LTM_ROI] = LTM_ROI,
	[SDE_LTM_VLUT] = LTM_VLUT,
};

static u32 feature_reg_dma_sz[SDE_DSPP_MAX] = {
	[SDE_DSPP_VLUT] = VLUT_MEM_SIZE,
	[SDE_DSPP_GAMUT] = GAMUT_LUT_MEM_SIZE,
	[SDE_DSPP_GC] = GC_LUT_MEM_SIZE,
	[SDE_DSPP_IGC] = IGC_LUT_MEM_SIZE,
	[SDE_DSPP_PCC] = PCC_MEM_SIZE,
	[SDE_DSPP_HSIC] = HSIC_MEM_SIZE,
	[SDE_DSPP_SIXZONE] = SIXZONE_MEM_SIZE,
	[SDE_DSPP_MEMCOLOR] = MEMCOLOR_MEM_SIZE,
	[SDE_DSPP_RC] = RC_MASK_CFG_SIZE,
	[SDE_DSPP_RC_PU] = RC_PU_CFG_SIZE,
	[SDE_DSPP_SPR] = SPR_INIT_MEM_SIZE,
	[SDE_DSPP_DEMURA] = DEMURA_MEM_SIZE,
	[SDE_DSPP_DEMURA_CFG0_PARAM2] = DEMURA_CFG0_PARAM2_MEM_SIZE,
};

static u32 sspp_feature_reg_dma_sz[SDE_SSPP_MAX] = {
	[SDE_SSPP_VIG_IGC] = IGC_LUT_MEM_SIZE,
	[SDE_SSPP_VIG_GAMUT] = GAMUT_LUT_MEM_SIZE,
	[SDE_SSPP_DMA_IGC] = IGC_LUT_MEM_SIZE,
	[SDE_SSPP_DMA_GC] = GC_LUT_MEM_SIZE,
	[SDE_SSPP_SCALER_QSEED3] = QSEED3_MEM_SIZE,
	[SDE_SSPP_SCALER_QSEED3LITE] = QSEED3_MEM_SIZE,
};

static u32 ltm_feature_reg_dma_sz[SDE_LTM_MAX] = {
	[SDE_LTM_INIT] = LTM_INIT_MEM_SIZE,
	[SDE_LTM_ROI] = LTM_ROI_MEM_SIZE,
	[SDE_LTM_VLUT] = LTM_VLUT_MEM_SIZE,
};

static u32 dspp_mapping[DSPP_MAX] = {
	[DSPP_0] = DSPP0,
	[DSPP_1] = DSPP1,
	[DSPP_2] = DSPP2,
	[DSPP_3] = DSPP3,
};

static u32 sspp_mapping[SSPP_MAX] = {
	[SSPP_VIG0] = VIG0,
	[SSPP_VIG1] = VIG1,
	[SSPP_VIG2] = VIG2,
	[SSPP_VIG3] = VIG3,
	[SSPP_DMA0] = DMA0,
	[SSPP_DMA1] = DMA1,
	[SSPP_DMA2] = DMA2,
	[SSPP_DMA3] = DMA3,
	[SSPP_DMA4] = DMA4,
	[SSPP_DMA5] = DMA5,
};

static u32 ltm_mapping[LTM_MAX] = {
	[LTM_0] = LTM0,
	[LTM_1] = LTM1,
	[LTM_2] = LTM2,
	[LTM_3] = LTM3,
};

#define REG_DMA_INIT_OPS(cfg, block, reg_dma_feature, feature_dma_buf) \
	do { \
		memset(&cfg, 0, sizeof(cfg)); \
		(cfg).blk = block; \
		(cfg).feature = reg_dma_feature; \
		(cfg).dma_buf = feature_dma_buf; \
	} while (0)

#define REG_DMA_SETUP_OPS(cfg, block_off, data_ptr, data_len, op, \
		wrap_sz, wrap_inc, reg_mask) \
	do { \
		(cfg).ops = op; \
		(cfg).blk_offset = block_off; \
		(cfg).data_size = data_len; \
		(cfg).data = data_ptr; \
		(cfg).inc = wrap_inc; \
		(cfg).wrap_size = wrap_sz; \
		(cfg).mask = reg_mask; \
	} while (0)

#define REG_DMA_SETUP_KICKOFF(cfg, hw_ctl, feature_dma_buf, ops, ctl_q, \
		mode, reg_dma_feature) \
	do { \
		memset(&cfg, 0, sizeof(cfg)); \
		(cfg).ctl = hw_ctl; \
		(cfg).dma_buf = feature_dma_buf; \
		(cfg).op = ops; \
		(cfg).dma_type = REG_DMA_TYPE_DB; \
		(cfg).queue_select = ctl_q; \
		(cfg).trigger_mode = mode; \
		(cfg).feature = reg_dma_feature; \
	} while (0)

static int reg_dma_buf_init(struct sde_reg_dma_buffer **buf, u32 sz);
static int reg_dma_dspp_check(struct sde_hw_dspp *ctx, void *cfg,
		enum sde_reg_dma_features feature);
static int reg_dma_sspp_check(struct sde_hw_pipe *ctx, void *cfg,
		enum sde_reg_dma_features feature,
		enum sde_sspp_multirect_index idx);
static int reg_dma_ltm_check(struct sde_hw_dspp *ctx, void *cfg,
		enum sde_reg_dma_features feature);
static void _perform_sbdma_kickoff(struct sde_hw_dspp *ctx,
		struct sde_hw_cp_cfg *hw_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		u32 blk, enum sde_reg_dma_features feature);

static inline int _reg_dmav1_rc_write(struct sde_hw_dspp *ctx,
		u32 reg_offset, u32 val,
		struct sde_hw_reg_dma_ops *dma_ops,
		enum sde_reg_dma_features feature)
{
	int rc = 0;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	uint32_t abs_offset;

	abs_offset = ctx->hw.blk_off + ctx->cap->sblk->rc.base + reg_offset;
	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, feature,
		dspp_buf[feature][ctx->idx]);
	REG_DMA_SETUP_OPS(dma_write_cfg, abs_offset, &val,
		sizeof(__u32), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc)
		SDE_ERROR("rc dma write failed ret %d\n", rc);
	return rc;
}

static int _reg_dmav1_rc_program_enable_bits(
		struct sde_hw_dspp *hw_dspp,
		struct drm_msm_rc_mask_cfg *rc_mask_cfg,
		enum rc_param_a param_a,
		enum rc_param_b param_b,
		enum rc_param_r param_r,
		int merge_mode,
		struct sde_rect *rc_roi,
		struct sde_hw_reg_dma_ops *dma_ops,
		enum sde_reg_dma_features feature)
{
	int rc = 0;
	u32 val = 0, param_c = 0, rc_merge_mode = 0, ystart = 0;
	u64 flags = 0, mask_w = 0, mask_h = 0;
	bool r1_valid = false, r2_valid = false;
	bool pu_in_r1 = false, pu_in_r2 = false;
	bool r1_enable = false, r2_enable = false;

	if (!hw_dspp || !rc_mask_cfg || !rc_roi) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	rc = _sde_hw_rc_get_enable_bits(param_a, param_b, &param_c,
			merge_mode, &rc_merge_mode);
	if (rc) {
		SDE_ERROR("invalid enable bits, rc:%d\n", rc);
		return rc;
	}

	flags = rc_mask_cfg->flags;
	mask_w = rc_mask_cfg->width;
	mask_h = rc_mask_cfg->height;
	r1_valid = ((flags & SDE_HW_RC_DISABLE_R1) != SDE_HW_RC_DISABLE_R1);
	r2_valid = ((flags & SDE_HW_RC_DISABLE_R2) != SDE_HW_RC_DISABLE_R2);
	pu_in_r1 = (param_r == RC_PARAM_R1 || param_r == RC_PARAM_R1R2);
	pu_in_r2 = (param_r == RC_PARAM_R2 || param_r == RC_PARAM_R1R2);
	r1_enable = (r1_valid && pu_in_r1);
	r2_enable = (r2_valid && pu_in_r2);

	if (r1_enable)
		val |= BIT(0);

	if (r2_enable)
		val |= BIT(4);

	/*corner case for partial update in R2 region*/
	if (!r1_enable && r2_enable)
		ystart = rc_roi->y;

	SDE_DEBUG("idx:%d w:%d h:%d flags:%x, R1:%d, R2:%d, PU R1:%d, PU R2:%d, Y_START:%d\n",
		RC_IDX(hw_dspp), mask_w, mask_h, flags, r1_valid, r2_valid, pu_in_r1,
		pu_in_r2, ystart);
	SDE_EVT32(RC_IDX(hw_dspp), mask_w, mask_h, flags, r1_valid, r2_valid, pu_in_r1, pu_in_r2,
		ystart);

	val |= param_c;
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG1, val, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG13, ystart, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG9, rc_merge_mode, dma_ops, feature);

	return rc;
}

static int _reg_dmav1_rc_program_roi(struct sde_hw_dspp *hw_dspp,
		struct drm_msm_rc_mask_cfg *rc_mask_cfg,
		int merge_mode,
		struct sde_rect *rc_roi,
		struct sde_hw_reg_dma_ops *dma_ops,
		enum sde_reg_dma_features feature)
{
	int rc = 0;
	u32 val2 = 0, val3 = 0, val4 = 0;
	enum rc_param_r param_r = RC_PARAM_R0;
	enum rc_param_a param_a = RC_PARAM_A0;
	enum rc_param_b param_b = RC_PARAM_B0;

	if (!hw_dspp || !rc_mask_cfg || !rc_roi) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	rc = _sde_hw_rc_get_param_rb(rc_mask_cfg, rc_roi, &param_r,
			&param_b);
	if (rc) {
		SDE_ERROR("invalid rc roi, rc:%d\n", rc);
		return rc;
	}

	param_a = rc_mask_cfg->cfg_param_03;
	rc = _reg_dmav1_rc_program_enable_bits(hw_dspp, rc_mask_cfg,
		param_a, param_b, param_r, merge_mode, rc_roi,
		dma_ops, feature);
	if (rc) {
		SDE_ERROR("failed to program enable bits, rc:%d\n", rc);
		return rc;
	}

	val2 = ((rc_mask_cfg->cfg_param_01 & 0x0000FFFF) |
			((rc_mask_cfg->cfg_param_02 << 16) & 0xFFFF0000));
	if (param_a == RC_PARAM_A1) {
		val3 = (rc_mask_cfg->cfg_param_04[0] |
				(rc_mask_cfg->cfg_param_04[1] << 16));
		val4 = (rc_mask_cfg->cfg_param_04[2] |
				(rc_mask_cfg->cfg_param_04[3] << 16));
	} else if (param_a == RC_PARAM_A0) {
		val3 = (rc_mask_cfg->cfg_param_04[0]);
		val4 = (rc_mask_cfg->cfg_param_04[1]);
	}

	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG2, val2, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG3, val3, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG4, val4, dma_ops, feature);

	return rc;
}

static int _reg_dmav1_rc_program_data_offset(
		struct sde_hw_dspp *hw_dspp,
		struct drm_msm_rc_mask_cfg *rc_mask_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		enum sde_reg_dma_features feature)
{
	int rc = 0;
	u32 val5 = 0, val6 = 0, val7 = 0, val8 = 0;
	u32 cfg_param_07;

	if (!hw_dspp || !rc_mask_cfg) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	cfg_param_07 = rc_mask_cfg->cfg_param_07;
	if (rc_mask_cfg->cfg_param_03 == RC_PARAM_A1) {
		val5 = ((rc_mask_cfg->cfg_param_05[0] + cfg_param_07) |
				((rc_mask_cfg->cfg_param_05[1] + cfg_param_07)
				<< 16));
		val6 = ((rc_mask_cfg->cfg_param_05[2] + cfg_param_07)|
				((rc_mask_cfg->cfg_param_05[3] + cfg_param_07)
				<< 16));
		val7 = ((rc_mask_cfg->cfg_param_06[0] + cfg_param_07) |
				((rc_mask_cfg->cfg_param_06[1] + cfg_param_07)
				<< 16));
		val8 = ((rc_mask_cfg->cfg_param_06[2] + cfg_param_07) |
				((rc_mask_cfg->cfg_param_06[3] + cfg_param_07)
				<< 16));
	} else if (rc_mask_cfg->cfg_param_03 == RC_PARAM_A0) {
		val5 = (rc_mask_cfg->cfg_param_05[0] + cfg_param_07);
		val6 = (rc_mask_cfg->cfg_param_05[1] + cfg_param_07);
		val7 = (rc_mask_cfg->cfg_param_06[0] + cfg_param_07);
		val8 = (rc_mask_cfg->cfg_param_06[1] + cfg_param_07);
	}

	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG5, val5, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG6, val6, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG7, val7, dma_ops, feature);
	rc = _reg_dmav1_rc_write(hw_dspp, SDE_HW_RC_REG8, val8, dma_ops, feature);

	return rc;
}

static int reg_dma_buf_init(struct sde_reg_dma_buffer **buf, u32 size)
{
	struct sde_hw_reg_dma_ops *dma_ops;

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	if (!buf) {
		DRM_ERROR("invalid buf\n");
		return -EINVAL;
	}

	/* buffer already initialized */
	if (*buf)
		return 0;

	*buf = dma_ops->alloc_reg_dma_buf(size);
	if (IS_ERR_OR_NULL(*buf))
		return -EINVAL;

	return 0;
}

static int reg_dma_dspp_check(struct sde_hw_dspp *ctx, void *cfg,
		enum sde_reg_dma_features feature)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_hw_cp_cfg *hw_cfg = cfg;

	if (!cfg || !ctx) {
		DRM_ERROR("invalid cfg %pK ctx %pK\n", cfg, ctx);
		return -EINVAL;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -EINVAL;

	if (!hw_cfg->ctl || ctx->idx >= DSPP_MAX ||
		feature >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid ctl %pK dspp idx %d feature %d\n",
			hw_cfg->ctl, ctx->idx, feature);
		return -EINVAL;
	}

	if (!dspp_buf[feature][ctx->idx]) {
		DRM_ERROR("invalid dma_buf\n");
		return -EINVAL;
	}

	return 0;
}

static int _reg_dma_init_dspp_feature_buf(int feature, enum sde_dspp idx)
{
	int rc = 0;

	if (feature == SDE_DSPP_MEMCOLOR) {
		rc = reg_dma_buf_init(
			&dspp_buf[MEMC_SKIN][idx],
			feature_reg_dma_sz[feature]);
		if (rc)
			return rc;

		rc = reg_dma_buf_init(
			&dspp_buf[MEMC_SKY][idx],
			feature_reg_dma_sz[feature]);
		if (rc)
			return rc;

		rc = reg_dma_buf_init(
			&dspp_buf[MEMC_FOLIAGE][idx],
			feature_reg_dma_sz[feature]);
		if (rc)
			return rc;

		rc = reg_dma_buf_init(
			&dspp_buf[MEMC_PROT][idx],
			feature_reg_dma_sz[feature]);
	} else if (feature == SDE_DSPP_RC) {
		rc = reg_dma_buf_init(
			&dspp_buf[RC_MASK_CFG][idx],
			feature_reg_dma_sz[feature]);
		if (rc)
			return rc;

		rc = reg_dma_buf_init(
			&dspp_buf[RC_PU_CFG][idx],
			feature_reg_dma_sz[SDE_DSPP_RC_PU]);
	} else {
		rc = reg_dma_buf_init(
			&dspp_buf[feature_map[feature]][idx],
			feature_reg_dma_sz[feature]);
	}

	return rc;
}

int reg_dmav1_init_dspp_op_v4(int feature, enum sde_dspp idx)
{
	int rc = -ENOTSUPP;
	struct sde_hw_reg_dma_ops *dma_ops;
	bool is_supported = false;
	u32 blk;

	if (feature >= SDE_DSPP_MAX || idx >= DSPP_MAX) {
		DRM_ERROR("invalid feature %x max %x dspp idx %x max %xd\n",
			feature, SDE_DSPP_MAX, idx, DSPP_MAX);
		return rc;
	}

	if (feature_map[feature] >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid feature map %d for feature %d\n",
			feature_map[feature], feature);
		return -ENOTSUPP;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	blk = (feature_map[feature] == IGC) ? DSPP_IGC : dspp_mapping[idx];
	rc = dma_ops->check_support(feature_map[feature], blk, &is_supported);
	if (!rc)
		rc = (is_supported) ? 0 : -ENOTSUPP;

	if (is_supported)
		rc = _reg_dma_init_dspp_feature_buf(feature, idx);

	return rc;
}

static int reg_dmav1_get_dspp_blk(struct sde_hw_cp_cfg *hw_cfg,
		enum sde_dspp curr_dspp, u32 *blk, u32 *num_of_mixers)
{
	struct sde_hw_dspp *dspp;
	int rc = 0;

	*num_of_mixers = 0;

	if (hw_cfg == NULL) {
		DRM_ERROR("Invalid sde_hw_cp_cfg structure provided\n");
		return -EINVAL;
	}

	if (blk == NULL) {
		DRM_ERROR("Invalid payload provided\n");
		return -EINVAL;
	}

	if (curr_dspp >= DSPP_MAX) {
		DRM_ERROR("Invalid current dspp idx %d", curr_dspp);
		return -EINVAL;
	}

	/* Treat first dspp as master to simplify setup */
	dspp = hw_cfg->dspp[0];
	if(!dspp) {
		DRM_ERROR("Invalid dspp NULL");
		return -EINVAL;
	}

	if (hw_cfg->broadcast_disabled) {
		*blk = dspp_mapping[curr_dspp];
		(*num_of_mixers)++;
	} else if (curr_dspp != dspp->idx) {
		DRM_DEBUG_DRIVER("Slave DSPP instance %d\n", dspp->idx);
		rc = -EALREADY;
	} else {
		u32 i;

		for (i = 0 ; i < hw_cfg->num_of_mixers; i++) {
			dspp = hw_cfg->dspp[i];
			if (!dspp) {
				DRM_ERROR("Invalid dspp NULL");
				rc = -EINVAL;
				break;
			}
			if (dspp->idx >= DSPP_MAX) {
				DRM_ERROR("Invalid dspp idx %d", dspp->idx);
				rc = -EINVAL;
				break;
			}
			*blk |= dspp_mapping[dspp->idx];
			(*num_of_mixers)++;
		}
	}

	if (!rc && !blk) {
		rc = -EINVAL;
		*num_of_mixers = 0;
	}

	return rc;
}

void reg_dmav1_setup_dspp_vlutv18(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_pa_vlut *payload = NULL;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_hw_ctl *ctl = NULL;
	struct sde_hw_dspp *dspp_list[DSPP_MAX];
	u32 *data = NULL;
	int i, j, rc = 0;
	u32 index, num_of_mixers, blk = 0;

	rc = reg_dma_dspp_check(ctx, cfg, VLUT);
	if (rc)
		return;

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	} else if (num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
			DSPP_MAX);
		return;
	} else if (num_of_mixers > 1) {
		memcpy(dspp_list, hw_cfg->dspp,
			sizeof(struct sde_hw_dspp *) * num_of_mixers);
	} else {
		dspp_list[0] = ctx;
	}

	ctl = hw_cfg->ctl;
	if (!hw_cfg->payload) {
		struct sde_hw_dspp *dspp;

		DRM_DEBUG_DRIVER("Disable vlut feature\n");
		LOG_FEATURE_OFF;
		for (index = 0; index < num_of_mixers; index++) {
			dspp = hw_cfg->dspp[index];
			SDE_REG_WRITE(&dspp->hw, dspp->cap->sblk->hist.base +
					PA_LUTV_DSPP_CTRL_OFF, 0);
		}
		goto exit;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_pa_vlut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_pa_vlut));
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[VLUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, VLUT, dspp_buf[VLUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	data = kvzalloc(VLUT_LEN, GFP_KERNEL);
	if (!data)
		return;

	payload = hw_cfg->payload;
	DRM_DEBUG_DRIVER("Enable vlut feature flags %llx\n", payload->flags);
	for (i = 0, j = 0; i < ARRAY_SIZE(payload->val); i += 2, j++)
		data[j] = (payload->val[i] & REG_MASK(10)) |
		((payload->val[i + 1] & REG_MASK(10)) << 16);


	REG_DMA_SETUP_OPS(dma_write_cfg, ctx->cap->sblk->vlut.base, data,
			VLUT_LEN, REG_BLK_WRITE_SINGLE, 0, 0, 0);

	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write pa vlut failed ret %d\n", rc);
		goto exit;
	}

	i = 1;
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->hist.base + PA_LUTV_DSPP_CTRL_OFF, &i,
		sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		goto exit;
	}
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->hist.base + PA_LUTV_DSPP_SWAP_OFF, &i,
		sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[VLUT][ctx->idx],
	    REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, VLUT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		goto exit;
	}

exit:
	kvfree(data);
	/* update flush bit */
	if (!rc && ctl && ctl->ops.update_bitmask_dspp_pavlut) {
		int dspp_idx;

		for (index = 0; index < num_of_mixers; index++) {
			dspp_idx = dspp_list[index]->idx;
			ctl->ops.update_bitmask_dspp_pavlut(ctl, dspp_idx,
				true);
		}
	}
}

static int sde_gamut_get_mode_info(u32 pipe, struct drm_msm_3d_gamut *payload,
		u32 *tbl_len, u32 *tbl_off, u32 *opcode, u32 *scale_off)
{
	int rc = 0;

	if (payload->mode > GAMUT_3D_MODE_13) {
		DRM_ERROR("invalid mode %d", payload->mode);
		return -EINVAL;
	}

	switch (payload->mode) {
	case GAMUT_3D_MODE_17:
		*tbl_len = GAMUT_3D_MODE17_TBL_SZ * sizeof(u32) * 2;
		*tbl_off = 0;
		if (pipe == DSPP) {
			*scale_off = GAMUT_SCALEA_OFFSET_OFF;
			*opcode = gamut_mode_17;
		} else {
			*opcode = (*opcode & (BIT(5) - 1)) >> 2;
			if (*opcode == gamut_mode_17b)
				*opcode = gamut_mode_17;
			else
				*opcode = gamut_mode_17b;
			*scale_off = (*opcode == gamut_mode_17) ?
				GAMUT_SCALEA_OFFSET_OFF :
				GAMUT_SCALEB_OFFSET_OFF;
		}
		*opcode <<= 2;
		break;
	case GAMUT_3D_MODE_5:
		*tbl_len = GAMUT_3D_MODE5_TBL_SZ * sizeof(u32) * 2;
		*tbl_off = GAMUT_MODE_5_OFF;
		*scale_off = GAMUT_SCALEB_OFFSET_OFF;
		*opcode = gamut_mode_5 << 2;
		break;
	case GAMUT_3D_MODE_13:
		*tbl_len = GAMUT_3D_MODE13_TBL_SZ * sizeof(u32) * 2;
		*opcode = (*opcode & (BIT(4) - 1)) >> 2;
		if (*opcode == gamut_mode_13a)
			*opcode = gamut_mode_13b;
		else
			*opcode = gamut_mode_13a;
		*tbl_off = (*opcode == gamut_mode_13a) ? 0 :
			GAMUT_MODE_13B_OFF;
		*scale_off = (*opcode == gamut_mode_13a) ?
			GAMUT_SCALEA_OFFSET_OFF : GAMUT_SCALEB_OFFSET_OFF;
		*opcode <<= 2;
		break;
	default:
		rc = -EINVAL;
		break;
	}
	if (payload->flags & GAMUT_3D_MAP_EN)
		*opcode |= GAMUT_MAP_EN;
	*opcode |= GAMUT_EN;

	return rc;
}

static void dspp_3d_gamutv4_off(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 op_mode = 0;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc;
	u32 num_of_mixers, blk = 0;

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, GAMUT, dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->gamut.base,
		&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[GAMUT][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GAMUT);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

static void reg_dmav1_setup_dspp_3d_gamutv4_common(struct sde_hw_dspp *ctx,
		void *cfg, u32 scale_tbl_a_len, u32 scale_tbl_b_len)
{
	struct drm_msm_3d_gamut *payload;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 op_mode, reg, tbl_len, tbl_off, scale_off, i;
	u32 scale_tbl_len, scale_tbl_off;
	u32 *scale_data;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	int rc;
	u32 num_of_mixers, blk = 0;

	rc = reg_dma_dspp_check(ctx, cfg, GAMUT);
	if (rc)
		return;

	op_mode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->gamut.base);
	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable gamut feature\n");
		LOG_FEATURE_OFF;
		dspp_3d_gamutv4_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_3d_gamut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_3d_gamut));
		return;
	}
	payload = hw_cfg->payload;
	rc = sde_gamut_get_mode_info(DSPP, payload, &tbl_len, &tbl_off,
			&op_mode, &scale_off);
	if (rc) {
		DRM_ERROR("invalid mode info rc %d\n", rc);
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, GAMUT, dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}
	for (i = 0; i < GAMUT_3D_TBL_NUM; i++) {
		reg = GAMUT_TABLE0_SEL << i;
		reg |= ((tbl_off) & (BIT(11) - 1));
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->gamut.base + GAMUT_TABLE_SEL_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write tbl sel reg failed ret %d\n", rc);
			return;
		}
		REG_DMA_SETUP_OPS(dma_write_cfg,
		    ctx->cap->sblk->gamut.base + GAMUT_LOWER_COLOR_OFF,
		    &payload->col[i][0].c2_c1, tbl_len,
		    REG_BLK_WRITE_MULTIPLE, 2, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write color reg failed ret %d\n", rc);
			return;
		}
	}

	if (op_mode & GAMUT_MAP_EN) {
		if (scale_off == GAMUT_SCALEA_OFFSET_OFF)
			scale_tbl_len = scale_tbl_a_len;
		else
			scale_tbl_len = scale_tbl_b_len;

		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			scale_tbl_off = ctx->cap->sblk->gamut.base + scale_off +
					(i * scale_tbl_len);
			scale_data = &payload->scale_off[i][0];
			REG_DMA_SETUP_OPS(dma_write_cfg, scale_tbl_off,
					scale_data, scale_tbl_len,
					REG_BLK_WRITE_SINGLE, 0, 0, 0);
			rc = dma_ops->setup_payload(&dma_write_cfg);
			if (rc) {
				DRM_ERROR("write scale/off reg failed ret %d\n",
						rc);
				return;
			}
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->gamut.base,
		&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[GAMUT][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GAMUT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_dspp_3d_gamutv4(struct sde_hw_dspp *ctx, void *cfg)
{
	reg_dmav1_setup_dspp_3d_gamutv4_common(ctx, cfg, GAMUT_SCALE_OFF_LEN,
		GAMUT_SCALE_OFF_LEN_12);
}

void reg_dmav1_setup_dspp_3d_gamutv41(struct sde_hw_dspp *ctx, void *cfg)
{
	reg_dmav1_setup_dspp_3d_gamutv4_common(ctx, cfg, GAMUT_SCALE_OFF_LEN,
		GAMUT_SCALE_OFF_LEN);
}

void reg_dmav1_setup_dspp_3d_gamutv42(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_3d_gamut *payload = NULL;
	uint32_t i, j, tmp;
	uint32_t scale_off[GAMUT_3D_SCALE_OFF_TBL_NUM][GAMUT_3D_SCALE_OFF_SZ];
	int rc;

	rc = reg_dma_dspp_check(ctx, cfg, GAMUT);
	if (rc)
		return;
	if (hw_cfg->payload && hw_cfg->len != sizeof(struct drm_msm_3d_gamut)) {
		DRM_ERROR("invalid payload len actual %d expected %zd",
				hw_cfg->len, sizeof(struct drm_msm_3d_gamut));
		return;
	}

	payload = hw_cfg->payload;
	if (payload && (payload->flags & GAMUT_3D_MAP_EN)) {
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			for (j = 0; j < GAMUT_3D_SCALE_OFF_SZ; j++) {
				scale_off[i][j] = payload->scale_off[i][j];
				tmp = payload->scale_off[i][j] & 0x1ffff000;
				payload->scale_off[i][j] &= 0xfff;
				tmp = tmp << 3;
				payload->scale_off[i][j] =
					tmp | payload->scale_off[i][j];
			}
		}
	}
	reg_dmav1_setup_dspp_3d_gamutv4_common(ctx, cfg, GAMUT_SCALE_OFF_LEN,
		GAMUT_SCALE_OFF_LEN);
	if (payload && (payload->flags & GAMUT_3D_MAP_EN)) {
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			for (j = 0; j < GAMUT_3D_SCALE_OFF_SZ; j++) {
				payload->scale_off[i][j] = scale_off[i][j];
			}
		}
	}
}

void reg_dmav1_setup_dspp_gcv18(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_pgc_lut *lut_cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc, i = 0;
	u32 reg;
	u32 *addr[GC_TBL_NUM];
	u32 num_of_mixers, blk = 0;

	rc = reg_dma_dspp_check(ctx, cfg, GC);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable pgc feature\n");
		LOG_FEATURE_OFF;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->gc.base, 0);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_pgc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_pgc_lut));
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	lut_cfg = hw_cfg->payload;
	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[GC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, GC, dspp_buf[GC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	addr[0] = lut_cfg->c0;
	addr[1] = lut_cfg->c1;
	addr[2] = lut_cfg->c2;
	for (i = 0; i < GC_TBL_NUM; i++) {
		reg = 0;
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->gc.base + GC_C0_INDEX_OFF +
			(i * sizeof(u32) * 2),
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("index init failed ret %d\n", rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->gc.base + GC_C0_OFF +
			(i * sizeof(u32) * 2),
			addr[i],
			PGC_TBL_LEN * sizeof(u32),
			REG_BLK_WRITE_INC, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			return;
		}
	}

	reg = BIT(0);
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->gc.base + GC_LUT_SWAP_OFF,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting swap offset failed ret %d\n", rc);
		return;
	}

	reg = GC_EN | ((lut_cfg->flags & PGC_8B_ROUND) ? GC_8B_ROUND_EN : 0);
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->gc.base,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("enabling gamma correction failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[GC][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}
}

static void _dspp_igcv31_off(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc;
	u32 reg;
	u32 num_of_mixers, blk = 0;

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, IGC, dspp_buf[IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	reg = IGC_DIS;
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->igc.base + IGC_OPMODE_OFF,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[IGC][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_dspp_igcv31(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_igc_lut *lut_cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_dspp *dspp_list[DSPP_MAX];
	int rc, i = 0, j = 0;
	u32 *addr[IGC_TBL_NUM];
	u32 offset = 0;
	u32 reg;
	u32 index, num_of_mixers, dspp_sel, blk = 0;

	rc = reg_dma_dspp_check(ctx, cfg, IGC);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable igc feature\n");
		LOG_FEATURE_OFF;
		_dspp_igcv31_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_igc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_igc_lut));
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	} else if (num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
			DSPP_MAX);
		return;
	} else if (num_of_mixers > 1) {
		memcpy(dspp_list, hw_cfg->dspp,
			sizeof(struct sde_hw_dspp *) * num_of_mixers);
	} else {
		dspp_list[0] = ctx;
	}

	lut_cfg = hw_cfg->payload;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, DSPP_IGC, IGC, dspp_buf[IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	dspp_sel = -1;
	for (index = 0; index < num_of_mixers; index++)
		dspp_sel &= IGC_DSPP_SEL_MASK(dspp_list[index]->idx - 1);

	addr[0] = lut_cfg->c0;
	addr[1] = lut_cfg->c1;
	addr[2] = lut_cfg->c2;
	for (i = 0; i < IGC_TBL_NUM; i++) {
		offset = IGC_C0_OFF + (i * sizeof(u32));

		for (j = 0; j < IGC_TBL_LEN; j++) {
			addr[i][j] &= IGC_DATA_MASK;
			addr[i][j] |= dspp_sel;
			if (j == 0)
				addr[i][j] |= IGC_INDEX_UPDATE;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg, offset, addr[i],
			IGC_TBL_LEN * sizeof(u32),
			REG_BLK_WRITE_INC, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_INIT_OPS(dma_write_cfg, blk, IGC, dspp_buf[IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	if (lut_cfg->flags & IGC_DITHER_ENABLE) {
		reg = lut_cfg->strength & IGC_DITHER_DATA_MASK;
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->igc.base + IGC_DITHER_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("dither strength failed ret %d\n", rc);
			return;
		}
	}

	reg = IGC_EN;
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->igc.base + IGC_OPMODE_OFF,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[IGC][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

int reg_dmav1_setup_rc_pu_configv1(struct sde_hw_dspp *ctx, void *cfg)
{
	int rc = 0;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_rc_mask_cfg *rc_mask_cfg;
	struct msm_roi_list *roi_list;
	struct msm_roi_list empty_roi_list;
	struct sde_rect rc_roi, merged_roi;
	enum rc_param_r param_r = RC_PARAM_R0;
	enum rc_param_a param_a = RC_PARAM_A0;
	enum rc_param_b param_b = RC_PARAM_B0;
	u32 merge_mode = 0;

	if (!ctx || !cfg) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	if (hw_cfg->len != sizeof(struct sde_drm_roi_v1)) {
		SDE_ERROR("invalid payload size\n");
		return -EINVAL;
	}

	rc = reg_dma_dspp_check(ctx, cfg, RC_PU_CFG);
	if (rc) {
		SDE_ERROR("invalid dma dspp check rc = %d\n", rc);
		return -EINVAL;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[RC_PU_CFG][ctx->idx]);
	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, RC_PU_CFG,
		dspp_buf[RC_PU_CFG][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		SDE_ERROR("write decode select failed ret %d\n", rc);
		return -ENOMEM;
	}

	roi_list = hw_cfg->payload;
	if (!roi_list) {
		SDE_DEBUG("full frame update\n");
		memset(&empty_roi_list, 0, sizeof(struct msm_roi_list));
		roi_list = &empty_roi_list;
	}

	rc_mask_cfg = ctx->rc_state.last_rc_mask_cfg;
	SDE_EVT32(RC_IDX(ctx), roi_list, rc_mask_cfg, rc_mask_cfg->cfg_param_03);

	/* early return when there is no mask in memory */
	if (!rc_mask_cfg || !rc_mask_cfg->cfg_param_03) {
		SDE_DEBUG("no previous rc mask programmed\n");
		SDE_EVT32(RC_IDX(ctx));
		return SDE_HW_RC_PU_SKIP_OP;
	}

	sde_kms_rect_merge_rectangles(roi_list, &merged_roi);
	rc = _sde_hw_rc_get_ajusted_roi(hw_cfg, &merged_roi, &rc_roi);
	if (rc) {
		SDE_ERROR("failed to get adjusted roi, rc:%d\n", rc);
		return rc;
	}

	rc = _sde_hw_rc_get_merge_mode(hw_cfg, &merge_mode);
	if (rc) {
		SDE_ERROR("invalid merge_mode, rc:%d\n", rc);
		return rc;
	}

	rc = _sde_hw_rc_get_param_rb(rc_mask_cfg, &rc_roi, &param_r,
			&param_b);
	if (rc) {
		SDE_ERROR("invalid roi, rc:%d\n", rc);
		return rc;
	}

	param_a = rc_mask_cfg->cfg_param_03;
	rc = _reg_dmav1_rc_program_enable_bits(ctx, rc_mask_cfg,
		param_a, param_b, param_r, merge_mode, &rc_roi,
		dma_ops, RC_PU_CFG);
	if (rc) {
		SDE_ERROR("failed to program enable bits, rc:%d\n", rc);
		return rc;
	}

	/* defer trigger to kickoff phase */
	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
		dspp_buf[RC_PU_CFG][ctx->idx], REG_DMA_WRITE,
		DMA_CTL_QUEUE0, WRITE_TRIGGER, RC_PU_CFG);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		SDE_ERROR("failed to kick off ret %d\n", rc);
		return rc;
	}
	LOG_FEATURE_ON;

	memcpy(ctx->rc_state.last_roi_list,
			roi_list, sizeof(struct msm_roi_list));

	return 0;
}

int reg_dmav1_setup_rc_mask_configv1(struct sde_hw_dspp *ctx, void *cfg)
{
	int rc = 0;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_rc_mask_cfg *rc_mask_cfg;
	struct sde_rect rc_roi, merged_roi;
	struct msm_roi_list *last_roi_list;
	u32 merge_mode = 0;
	u64 mask_w = 0, mask_h = 0, panel_w = 0, panel_h = 0;
	u32 *data = NULL;
	u32 cfg_param_07; u64 cfg_param_09;
	u32 buf_sz = 0, abs_offset = 0;
	int i = 0;

	if (!ctx || !cfg) {
		SDE_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	rc = reg_dma_dspp_check(ctx, cfg, RC_MASK_CFG);
	if (rc) {
		SDE_ERROR("invalid dma dspp check rc = %d\n", rc);
		return -EINVAL;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[RC_MASK_CFG][ctx->idx]);
	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, RC_MASK_CFG,
		dspp_buf[RC_MASK_CFG][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		SDE_ERROR("write decode select failed ret %d\n", rc);
		return -ENOMEM;
	}

	if ((hw_cfg->len == 0 && hw_cfg->payload == NULL)) {
		SDE_DEBUG("RC feature disabled\n");
		rc = _reg_dmav1_rc_write(ctx, SDE_HW_RC_REG1, 0, dma_ops, RC_MASK_CFG);
		memset(ctx->rc_state.last_rc_mask_cfg, 0,
				sizeof(struct drm_msm_rc_mask_cfg));
		memset(ctx->rc_state.last_roi_list, 0,
				sizeof(struct msm_roi_list));
		SDE_EVT32(RC_IDX(ctx), ctx->rc_state.last_rc_mask_cfg,
				ctx->rc_state.last_rc_mask_cfg->cfg_param_03,
				ctx->rc_state.last_roi_list->num_rects);

		REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[RC_MASK_CFG][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_TRIGGER, RC_MASK_CFG);
		rc = dma_ops->kick_off(&kick_off);
		if (rc) {
			SDE_ERROR("failed to kick off ret %d\n", rc);
			return rc;
		}
		LOG_FEATURE_OFF;
		return 0;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_rc_mask_cfg) ||
			!hw_cfg->payload) {
		SDE_ERROR("invalid payload\n");
		return -EINVAL;
	}

	rc_mask_cfg = hw_cfg->payload;
	last_roi_list = ctx->rc_state.last_roi_list;

	mask_w = rc_mask_cfg->width;
	mask_h = rc_mask_cfg->height;
	panel_w =  hw_cfg->panel_width;
	panel_h = hw_cfg->panel_height;

	if ((panel_w != mask_w || panel_h != mask_h)) {
		SDE_ERROR("RC-%d mask: w %d h %d panel: w %d h %d mismatch\n",
				RC_IDX(ctx), mask_w, mask_h, panel_w, panel_h);
		SDE_EVT32(1);
		rc = _reg_dmav1_rc_write(ctx, SDE_HW_RC_REG1, 0, dma_ops, RC_MASK_CFG);

		REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[RC_MASK_CFG][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_TRIGGER, RC_MASK_CFG);
		rc = dma_ops->kick_off(&kick_off);
		if (rc) {
			SDE_ERROR("failed to kick off ret %d\n", rc);
			return -EINVAL;
		}
		LOG_FEATURE_OFF;
		return -EINVAL;
	}

	if (!last_roi_list || !last_roi_list->num_rects) {
		SDE_DEBUG("full frame update\n");
		memset(&merged_roi, 0, sizeof(struct sde_rect));
	} else {
		SDE_DEBUG("partial frame update\n");
		sde_kms_rect_merge_rectangles(last_roi_list, &merged_roi);
	}
	SDE_EVT32(RC_IDX(ctx), last_roi_list->num_rects);

	rc = _sde_hw_rc_get_ajusted_roi(hw_cfg, &merged_roi, &rc_roi);
	if (rc) {
		SDE_ERROR("failed to get adjusted roi, rc:%d\n", rc);
		return rc;
	}

	rc = _sde_hw_rc_get_merge_mode(hw_cfg, &merge_mode);
	if (rc) {
		SDE_ERROR("invalid merge_mode, rc:%d\n", rc);
		return rc;
	}

	rc = _reg_dmav1_rc_program_roi(ctx, rc_mask_cfg,
		merge_mode, &rc_roi, dma_ops, RC_MASK_CFG);
	if (rc) {
		SDE_ERROR("unable to program rc roi, rc:%d\n", rc);
		return rc;
	}

	rc = _reg_dmav1_rc_program_data_offset(ctx, rc_mask_cfg, dma_ops, RC_MASK_CFG);
	if (rc) {
		SDE_ERROR("unable to program data offsets, rc:%d\n", rc);
		return rc;
	}

	/* rc data should be programmed once if dspp are in multi-pipe mode */
	if (!(rc_mask_cfg->flags & SDE_HW_RC_SKIP_DATA_PROG) &&
		(ctx->cap->sblk->rc.idx % hw_cfg->num_of_mixers == 0)) {
		buf_sz = rc_mask_cfg->cfg_param_08 * 2 * sizeof(u32);
		abs_offset = ctx->hw.blk_off + ctx->cap->sblk->rc.base + 0x28;

		SDE_DEBUG("allocating %u bytes of memory for dma\n", buf_sz);
		data = kvzalloc(buf_sz, GFP_KERNEL);
		if (!data) {
			SDE_ERROR("memory allocation failed ret %d\n", rc);
			return -ENOMEM;
		}

		cfg_param_07 = rc_mask_cfg->cfg_param_07;
		SDE_DEBUG("cfg_param_07:%u\n", cfg_param_07);

		for (i = 0; i < rc_mask_cfg->cfg_param_08; i++) {
			cfg_param_09 =  rc_mask_cfg->cfg_param_09[i];
			SDE_DEBUG("cfg_param_09[%d] = 0x%016llX at %u\n", i,
					cfg_param_09,
					i + cfg_param_07);
			data[i * 2] = (i == 0) ? (BIT(30) | (cfg_param_07 << 18)) : 0;
			data[i * 2] |= (cfg_param_09 & 0x3FFFF);
			data[i * 2 + 1] = ((cfg_param_09 >> 18) & 0x3FFFF);
		}

		REG_DMA_SETUP_OPS(dma_write_cfg, abs_offset, data, buf_sz,
				REG_BLK_WRITE_INC, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			SDE_ERROR("rc dma write failed ret %d\n", rc);
			goto exit;
		}
	} else {
		SDE_DEBUG("skip data programming\n");
		SDE_EVT32(RC_IDX(ctx));
	}

	/* defer trigger to kickoff phase */
	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
		dspp_buf[RC_MASK_CFG][ctx->idx], REG_DMA_WRITE,
		DMA_CTL_QUEUE0, WRITE_TRIGGER, RC_MASK_CFG);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		SDE_ERROR("failed to kick off ret %d\n", rc);
		goto exit;
	}

	LOG_FEATURE_ON;
	memcpy(ctx->rc_state.last_rc_mask_cfg, rc_mask_cfg,
			sizeof(struct drm_msm_rc_mask_cfg));

exit:
	if (data != NULL)
		kvfree(data);
	return rc;
}

static void _dspp_pcc_common_off(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc;
	u32 reg;
	u32 num_of_mixers, blk = 0;

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[PCC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, PCC, dspp_buf[PCC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	reg = PCC_DIS;
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->pcc.base,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[PCC][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, PCC);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

static void reg_dmav1_setup_dspp_pcc_common(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct drm_msm_pcc *pcc_cfg;
	struct drm_msm_pcc_coeff *coeffs = NULL;
	u32 *data = NULL;
	int rc, i = 0;
	u32 reg = 0;
	u32 num_of_mixers, blk = 0;

	rc = reg_dma_dspp_check(ctx, cfg, PCC);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable pcc feature\n");
		LOG_FEATURE_OFF;
		_dspp_pcc_common_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_pcc)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_pcc));
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	pcc_cfg = hw_cfg->payload;
	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[PCC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, PCC, dspp_buf[PCC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	data = kvzalloc(PCC_LEN, GFP_KERNEL);
	if (!data)
		return;

	for (i = 0; i < PCC_NUM_PLANES; i++) {
		switch (i) {
		case 0:
			coeffs = &pcc_cfg->r;
			data[i + 24] = pcc_cfg->r_rr;
			data[i + 27] = pcc_cfg->r_gg;
			data[i + 30] = pcc_cfg->r_bb;
			break;
		case 1:
			coeffs = &pcc_cfg->g;
			data[i + 24] = pcc_cfg->g_rr;
			data[i + 27] = pcc_cfg->g_gg;
			data[i + 30] = pcc_cfg->g_bb;
			break;
		case 2:
			coeffs = &pcc_cfg->b;
			data[i + 24] = pcc_cfg->b_rr;
			data[i + 27] = pcc_cfg->b_gg;
			data[i + 30] = pcc_cfg->b_bb;
			break;
		default:
			DRM_ERROR("invalid pcc plane: %d\n", i);
			goto exit;
		}

		data[i] = coeffs->c;
		data[i + 3] = coeffs->r;
		data[i + 6] = coeffs->g;
		data[i + 9] = coeffs->b;
		data[i + 12] = coeffs->rg;
		data[i + 15] = coeffs->rb;
		data[i + 18] = coeffs->gb;
		data[i + 21] = coeffs->rgb;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->pcc.base + PCC_C_OFF,
		data, PCC_LEN,
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write pcc lut failed ret %d\n", rc);
		goto exit;
	}


	reg = PCC_EN;
	if (pcc_cfg->flags & PCC_BEFORE)
		reg |= BIT(16);

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->pcc.base,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[PCC][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, PCC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);

exit:
	kvfree(data);

}

void reg_dmav1_setup_dspp_pccv4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_pcc *pcc_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;

	if (hw_cfg->payload) {
		if (hw_cfg->len != sizeof(struct drm_msm_pcc)) {
			DRM_ERROR("invalid size of payload len %d exp %zd\n",
					hw_cfg->len,
					sizeof(struct drm_msm_pcc));
			return;
		}
		//Flags unsupported for PCCv4
		pcc_cfg = hw_cfg->payload;
		pcc_cfg->flags = 0;
	}
	reg_dmav1_setup_dspp_pcc_common(ctx, cfg);
}

void reg_dmav1_setup_dspp_pccv5(struct sde_hw_dspp *ctx, void *cfg)
{
	reg_dmav1_setup_dspp_pcc_common(ctx, cfg);
}

void reg_dmav1_setup_dspp_pa_hsicv17(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct drm_msm_pa_hsic *hsic_cfg;
	struct sde_hw_dspp *dspp_list[DSPP_MAX];
	u32 reg = 0, opcode = 0, local_opcode = 0;
	int rc, i;
	u32 num_of_mixers, blk = 0;


	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

	rc = reg_dma_dspp_check(ctx, cfg, HSIC);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable pa hsic feature\n");
		opcode &= ~(PA_HUE_EN | PA_SAT_EN | PA_VAL_EN | PA_CONT_EN);
		if (PA_DISABLE_REQUIRED(opcode))
			opcode &= ~PA_EN;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
		LOG_FEATURE_OFF;
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_pa_hsic)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_pa_hsic));
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	} else if (num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
			DSPP_MAX);
		return;
	} else if (num_of_mixers > 1) {
		memcpy(dspp_list, hw_cfg->dspp,
			sizeof(struct sde_hw_dspp *) * num_of_mixers);
	} else {
		dspp_list[0] = ctx;
	}

	hsic_cfg = hw_cfg->payload;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[HSIC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, HSIC, dspp_buf[HSIC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	if (hsic_cfg->flags & PA_HSIC_HUE_ENABLE) {
		reg = hsic_cfg->hue & PA_HUE_MASK;
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base + PA_HUE_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("hsic hue write failed ret %d\n", rc);
			return;
		}
		local_opcode |= PA_HUE_EN;
	}

	if (hsic_cfg->flags & PA_HSIC_SAT_ENABLE) {
		reg = hsic_cfg->saturation & PA_SAT_MASK;
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base + PA_SAT_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("hsic saturation write failed ret %d\n", rc);
			return;
		}
		local_opcode |= PA_SAT_EN;
	}

	if (hsic_cfg->flags & PA_HSIC_VAL_ENABLE) {
		reg = hsic_cfg->value & PA_VAL_MASK;
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base + PA_VAL_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("hsic value write failed ret %d\n", rc);
			return;
		}
		local_opcode |= PA_VAL_EN;
	}

	if (hsic_cfg->flags & PA_HSIC_CONT_ENABLE) {
		reg = hsic_cfg->contrast & PA_CONT_MASK;
		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base + PA_CONT_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("hsic contrast write failed ret %d\n", rc);
			return;
		}
		local_opcode |= PA_CONT_EN;
	}

	if (local_opcode) {
		local_opcode |= PA_EN;
	} else {
		DRM_ERROR("Invalid hsic config 0x%x\n", local_opcode);
		return;
	}

	for (i = 0; i < num_of_mixers; i++) {
		blk = dspp_mapping[dspp_list[i]->idx];
		REG_DMA_INIT_OPS(dma_write_cfg, blk, HSIC,
			dspp_buf[HSIC][ctx->idx]);

		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT,
			0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base, &local_opcode,
			sizeof(local_opcode), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_PA_MODE_HSIC_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting opcode failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, dspp_buf[HSIC][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, HSIC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

static int reg_dma_validate_sixzone_config(struct sde_hw_dspp *ctx, void *cfg,
		u32 *num_of_mixers, u32 *blk, struct sde_hw_dspp *dspp_list[])
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 opcode = 0;
	int rc;

	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

	rc = reg_dma_dspp_check(ctx, cfg, SIX_ZONE);
	if (rc)
		return rc;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable sixzone feature\n");
		opcode &= ~(PA_SIXZONE_HUE_EN | PA_SIXZONE_SAT_EN |
			PA_SIXZONE_VAL_EN);
		if (PA_DISABLE_REQUIRED(opcode))
			opcode &= ~PA_EN;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
		LOG_FEATURE_OFF;
		return -EALREADY;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_sixzone)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_sixzone));
		return -EINVAL;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, blk,
		num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return rc;
	} else if (rc == -EALREADY) {
		return rc;
	} else if (*num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
			DSPP_MAX);
		return -EINVAL;
	} else if (*num_of_mixers > 1) {
		memcpy(dspp_list, hw_cfg->dspp,
			sizeof(struct sde_hw_dspp *) * (*num_of_mixers));
	} else {
		dspp_list[0] = ctx;
	}
	return 0;
}

void reg_dmav1_setup_dspp_sixzonev17(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct drm_msm_sixzone *sixzone;
	struct sde_hw_dspp *dspp_list[DSPP_MAX];
	u32 reg = 0;
	u32 local_opcode = 0, local_hold = 0;
	u32 num_of_mixers, blk = 0;
	int i, rc;

	rc = reg_dma_validate_sixzone_config(ctx, cfg, &num_of_mixers, &blk, dspp_list);
	if (rc) {
		return;
	}

	sixzone = hw_cfg->payload;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[SIX_ZONE][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, SIX_ZONE,
		dspp_buf[SIX_ZONE][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	reg = BIT(26);
	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base,
		&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting lut index failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
	    (ctx->cap->sblk->sixzone.base + SIXZONE_ADJ_CURVE_P1_OFF),
		&sixzone->curve[0].p1, (SIXZONE_LUT_SIZE * sizeof(u32) * 2),
		REG_BLK_WRITE_MULTIPLE, 2, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone lut failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_THRESHOLDS_OFF,
		&sixzone->threshold, 3 * sizeof(u32),
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone threshold failed ret %d\n", rc);
		return;
	}

	local_hold = ((sixzone->sat_hold & REG_MASK(2)) << 12);
	local_hold |= ((sixzone->val_hold & REG_MASK(2)) << 14);
	if (sixzone->flags & SIXZONE_HUE_ENABLE)
		local_opcode |= PA_SIXZONE_HUE_EN;
	if (sixzone->flags & SIXZONE_SAT_ENABLE)
		local_opcode |= PA_SIXZONE_SAT_EN;
	if (sixzone->flags & SIXZONE_VAL_ENABLE)
		local_opcode |= PA_SIXZONE_VAL_EN;

	if (local_opcode) {
		local_opcode |= PA_EN;
	} else {
		DRM_ERROR("Invalid six zone config 0x%x\n", local_opcode);
		return;
	}

	for (i = 0; i < num_of_mixers; i++) {
		blk = dspp_mapping[dspp_list[i]->idx];
		REG_DMA_INIT_OPS(dma_write_cfg, blk, SIX_ZONE,
			dspp_buf[SIX_ZONE][ctx->idx]);

		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT,
			0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base + PA_PWL_HOLD_OFF, &local_hold,
			sizeof(local_hold), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_PA_PWL_HOLD_SZONE_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting local_hold failed ret %d\n", rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base, &local_opcode,
			sizeof(local_opcode), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_PA_MODE_SZONE_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting local_opcode failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
		dspp_buf[SIX_ZONE][ctx->idx],
		REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, SIX_ZONE);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav2_setup_dspp_sixzonev2(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct drm_msm_sixzone *sixzone;
	struct sde_hw_dspp *dspp_list[DSPP_MAX];
	u32 local_opcode = 0, local_hold = 0, sv_ctl = 0;
	u32 num_of_mixers, blk = 0, len, transfer_size_bytes;
	u16 *data = NULL;
	int i, rc, j, k;

	rc = reg_dma_validate_sixzone_config(ctx, cfg, &num_of_mixers, &blk, dspp_list);
	if (rc) {
		return;
	}

	sixzone = hw_cfg->payload;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[SIX_ZONE][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, SIX_ZONE,
		dspp_buf[SIX_ZONE][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select for sixzone failed ret %d\n", rc);
		return;
	}

	/* 384 LUT entries * 5 components (hue, sat_low, sat_med, sat_high, value)
	* 16 bit per LUT entry */
	len = SIXZONE_LUT_SIZE * 5 * sizeof(u16);
	/* Data size must be aligned with word size AND LUT transfer size */
	transfer_size_bytes = LUTBUS_SIXZONE_TRANS_SIZE * sizeof(u32);
	if (len % transfer_size_bytes)
		len = len + (transfer_size_bytes - len % transfer_size_bytes);

	data = kvzalloc(len, GFP_KERNEL);
	if (!data) {
		DRM_ERROR("Allocating memory for sixzone data failed!");
		return;
	}

	for (j = 0, k = 0; j < SIXZONE_LUT_SIZE; j++) {
		/* p0 --> hue, p1 --> sat_low/value, p2 --> sat_mid/sat_high */
		/* 16 bit per LUT entry and MSB aligned to allow expansion,
		* hence, sw need to left shift 4 bits before sending to HW.
		*/
		data[k++] = (u16) (sixzone->curve[j].p0 << 4);
		data[k++] = (u16) ((sixzone->curve[j].p1 >> 16) << 4);
		data[k++] = (u16) (sixzone->curve_p2[j] << 4);
		data[k++] = (u16) ((sixzone->curve_p2[j] >> 16) << 4);
		data[k++] = (u16) (sixzone->curve[j].p1 << 4);
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, (u32 *)data, len,
			REG_BLK_LUT_WRITE, 0, 0, 0);
	/* table select is only relevant to SSPP Gamut */
	dma_write_cfg.table_sel = 0;
	dma_write_cfg.block_sel = LUTBUS_BLOCK_SIXZONE;
	dma_write_cfg.trans_size = LUTBUS_SIXZONE_TRANS_SIZE;
	dma_write_cfg.lut_size = len / transfer_size_bytes;

	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("lut write for sixzone failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_THRESHOLDS_OFF,
		&sixzone->threshold, sizeof(u32),
		REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone threshold failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_ADJ_PWL0_OFF,
		&sixzone->adjust_p0, sizeof(u32),
		REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone adjust p0 failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_ADJ_PWL1_OFF,
		&sixzone->adjust_p1, sizeof(u32),
		REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone adjust p1 failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_SAT_PWL0_OFF,
		&sixzone->sat_adjust_p0, sizeof(u32),
		REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone saturation adjust p0 failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_SAT_PWL1_OFF,
		&sixzone->sat_adjust_p1, sizeof(u32),
		REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write sixzone saturation adjust p1 failed ret %d\n", rc);
		goto exit;
	}

	if (sixzone->flags & SIXZONE_SV_ENABLE) {
		sv_ctl |= PA_SIXZONE_SV_EN;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->sixzone.base + SIXZONE_SV_CTL_OFF,
		&sv_ctl, sizeof(sv_ctl), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("sv enable write failed for sixzone ret %d\n", rc);
		goto exit;
	}

	local_hold = ((sixzone->sat_hold & REG_MASK(2)) << 12);
	local_hold |= ((sixzone->val_hold & REG_MASK(2)) << 14);
	if (sixzone->flags & SIXZONE_HUE_ENABLE)
		local_opcode |= PA_SIXZONE_HUE_EN;
	if (sixzone->flags & SIXZONE_SAT_ENABLE)
		local_opcode |= PA_SIXZONE_SAT_EN;
	if (sixzone->flags & SIXZONE_VAL_ENABLE)
		local_opcode |= PA_SIXZONE_VAL_EN;

	if (local_opcode) {
		local_opcode |= PA_EN;
	} else {
		DRM_ERROR("Invalid six zone config 0x%x\n", local_opcode);
		goto exit;
	}

	for (i = 0; i < num_of_mixers; i++) {
		u32 modify_blk = dspp_mapping[dspp_list[i]->idx];

		REG_DMA_INIT_OPS(dma_write_cfg, modify_blk, SIX_ZONE,
			dspp_buf[SIX_ZONE][ctx->idx]);

		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT,
			0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed for sixzone ret %d\n", rc);
			goto exit;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base + PA_PWL_HOLD_OFF, &local_hold,
			sizeof(local_hold), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_PA_PWL_HOLD_SZONE_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting local_hold failed for sixzone ret %d\n", rc);
			goto exit;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base, &local_opcode,
			sizeof(local_opcode), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_PA_MODE_SZONE_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting local_opcode failed for sixzone ret %d\n", rc);
			goto exit;
		}
	}

	LOG_FEATURE_ON;
	_perform_sbdma_kickoff(ctx, hw_cfg, dma_ops, blk, SIX_ZONE);

exit:
	kvfree(data);
}

int reg_dmav1_deinit_dspp_ops(enum sde_dspp idx)
{
	int i;
	struct sde_hw_reg_dma_ops *dma_ops;

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	if (idx >= DSPP_MAX) {
		DRM_ERROR("invalid dspp idx %x max %xd\n", idx, DSPP_MAX);
		return -EINVAL;
	}

	for (i = 0; i < REG_DMA_FEATURES_MAX; i++) {
		if (!dspp_buf[i][idx])
			continue;
		dma_ops->dealloc_reg_dma(dspp_buf[i][idx]);
		dspp_buf[i][idx] = NULL;
	}
	return 0;
}

static void __setup_dspp_memcol(struct sde_hw_dspp *ctx,
		enum sde_reg_dma_features type,
		struct sde_hw_cp_cfg *hw_cfg)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct drm_msm_memcol *memcolor;
	struct sde_hw_dspp *dspp_list[DSPP_MAX];
	int rc, i;
	u32 addr = 0, idx = 0;
	u32 hold = 0, hold_shift = 0, mask = 0xFFFF;
	u32 opcode = 0, opcode_mask = 0xFFFFFFFF;
	u32 num_of_mixers, blk = 0;

	switch (type) {
	case MEMC_SKIN:
		idx = 0;
		opcode |= PA_SKIN_EN;
		break;
	case MEMC_SKY:
		idx = 1;
		opcode |= PA_SKY_EN;
		break;
	case MEMC_FOLIAGE:
		idx = 2;
		opcode |= PA_FOL_EN;
		break;
	default:
		DRM_ERROR("Invalid memory color type %d\n", type);
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
		&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	} else if (num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
			DSPP_MAX);
		return;
	} else if (num_of_mixers > 1) {
		memcpy(dspp_list, hw_cfg->dspp,
			sizeof(struct sde_hw_dspp *) * num_of_mixers);
	} else {
		dspp_list[0] = ctx;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[type][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, type, dspp_buf[type][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	memcolor = hw_cfg->payload;
	addr = ctx->cap->sblk->memcolor.base + MEMCOL_PWL0_OFF +
		(idx * MEMCOL_SIZE0);
	/* write color_adjust_p0 and color_adjust_p1 */
	REG_DMA_SETUP_OPS(dma_write_cfg, addr, &memcolor->color_adjust_p0,
		sizeof(u32) * 2, REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting color_adjust_p0 failed ret %d\n", rc);
		return;
	}

	/* write hue/sat/val region */
	addr += 8;
	REG_DMA_SETUP_OPS(dma_write_cfg, addr, &memcolor->hue_region,
		sizeof(u32) * 3, REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting color_adjust_p0 failed ret %d\n", rc);
		return;
	}

	addr = ctx->cap->sblk->memcolor.base + MEMCOL_PWL2_OFF +
		(idx * MEMCOL_SIZE1);
	/* write color_adjust_p2 and blend_gain */
	REG_DMA_SETUP_OPS(dma_write_cfg, addr, &memcolor->color_adjust_p2,
		sizeof(u32) * 2, REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting color_adjust_p0 failed ret %d\n", rc);
		return;
	}

	addr = ctx->cap->sblk->hsic.base + PA_PWL_HOLD_OFF;
	hold_shift = idx * MEMCOL_HOLD_SIZE;
	hold = ((memcolor->sat_hold & REG_MASK(2)) << hold_shift);
	hold |= ((memcolor->val_hold & REG_MASK(2)) << (hold_shift + 2));
	mask &= ~REG_MASK_SHIFT(4, hold_shift);
	opcode |= PA_EN;
	opcode_mask &= ~(opcode);

	/* write sat_hold and val_hold in PA_PWL_HOLD */
	for (i = 0; i < num_of_mixers; i++) {
		blk = dspp_mapping[dspp_list[i]->idx];
		REG_DMA_INIT_OPS(dma_write_cfg, blk, type,
			dspp_buf[type][ctx->idx]);

		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT,
			0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg, addr, &hold, sizeof(hold),
			REG_SINGLE_MODIFY, 0, 0, mask);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting color_adjust_p0 failed ret %d\n",
				rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->hsic.base, &opcode, sizeof(opcode),
			REG_SINGLE_MODIFY, 0, 0, opcode_mask);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("setting opcode failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
		dspp_buf[type][ctx->idx],
		REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, type);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_dspp_memcol_skinv17(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 opcode = 0;
	int rc;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	rc = reg_dma_dspp_check(ctx, cfg, MEMC_SKIN);
	if (rc)
		return;

	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable memcolor skin feature\n");
		opcode &= ~(PA_SKIN_EN);
		if (PA_DISABLE_REQUIRED(opcode))
			opcode &= ~PA_EN;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
		LOG_FEATURE_OFF;
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_memcol)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_memcol));
		return;
	}

	LOG_FEATURE_ON;
	__setup_dspp_memcol(ctx, MEMC_SKIN, hw_cfg);
}

void reg_dmav1_setup_dspp_memcol_skyv17(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 opcode = 0;
	int rc;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	rc = reg_dma_dspp_check(ctx, cfg, MEMC_SKY);
	if (rc)
		return;

	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable memcolor sky feature\n");
		opcode &= ~(PA_SKY_EN);
		if (PA_DISABLE_REQUIRED(opcode))
			opcode &= ~PA_EN;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
		LOG_FEATURE_OFF;
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_memcol)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_memcol));
		return;
	}

	LOG_FEATURE_ON;
	__setup_dspp_memcol(ctx, MEMC_SKY, hw_cfg);
}

void reg_dmav1_setup_dspp_memcol_folv17(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 opcode = 0;
	int rc;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	rc = reg_dma_dspp_check(ctx, cfg, MEMC_FOLIAGE);
	if (rc)
		return;

	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable memcolor foliage feature\n");
		opcode &= ~(PA_FOL_EN);
		if (PA_DISABLE_REQUIRED(opcode))
			opcode &= ~PA_EN;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
		LOG_FEATURE_OFF;
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_memcol)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_memcol));
		return;
	}

	LOG_FEATURE_ON;
	__setup_dspp_memcol(ctx, MEMC_FOLIAGE, hw_cfg);
}

void reg_dmav1_setup_dspp_memcol_protv17(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct drm_msm_memcol *memcolor;
	int rc;
	u32 opcode = 0, opcode_mask = 0xFFFFFFFF;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid param ctx %pK cfg %pK\n", ctx, cfg);
		return;
	}

	rc = reg_dma_dspp_check(ctx, cfg, MEMC_PROT);
	if (rc)
		return;

	opcode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->hsic.base);

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable memcolor prot feature\n");
		opcode &= ~(MEMCOL_PROT_MASK);
		if (PA_DISABLE_REQUIRED(opcode))
			opcode &= ~PA_EN;
		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->hsic.base, opcode);
		LOG_FEATURE_OFF;
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_memcol)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_memcol));
		return;
	}

	memcolor = hw_cfg->payload;
	opcode = 0;
	if (memcolor->prot_flags) {
		if (memcolor->prot_flags & MEMCOL_PROT_HUE)
			opcode |= MEMCOL_PROT_HUE_EN;
		if (memcolor->prot_flags & MEMCOL_PROT_SAT)
			opcode |= MEMCOL_PROT_SAT_EN;
		if (memcolor->prot_flags & MEMCOL_PROT_VAL)
			opcode |= MEMCOL_PROT_VAL_EN;
		if (memcolor->prot_flags & MEMCOL_PROT_CONT)
			opcode |= MEMCOL_PROT_CONT_EN;
		if (memcolor->prot_flags & MEMCOL_PROT_SIXZONE)
			opcode |= MEMCOL_PROT_SIXZONE_EN;
		if (memcolor->prot_flags & MEMCOL_PROT_BLEND)
			opcode |= MEMCOL_PROT_BLEND_EN;
	}

	if (!opcode) {
		DRM_ERROR("Invalid memcolor prot config 0x%x\n", opcode);
		return;
	}
	opcode |= PA_EN;
	opcode_mask &= ~(MEMCOL_PROT_MASK);

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[MEMC_PROT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, dspp_mapping[ctx->idx],
		MEMC_PROT, dspp_buf[MEMC_PROT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		ctx->cap->sblk->hsic.base, &opcode, sizeof(opcode),
		REG_SINGLE_MODIFY, 0, 0, opcode_mask);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[MEMC_PROT][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, MEMC_PROT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

int reg_dmav1_init_sspp_op_v4(int feature, enum sde_sspp idx)
{
	int rc = -ENOTSUPP;
	struct sde_hw_reg_dma_ops *dma_ops;
	bool is_supported = false;
	u32 blk, i = 0;

	if (feature >= SDE_SSPP_MAX || idx >= SSPP_MAX) {
		DRM_ERROR("invalid feature %x max %x sspp idx %x max %xd\n",
			feature, SDE_SSPP_MAX, idx, SSPP_MAX);
		return rc;
	}

	if (sspp_feature_map[feature] >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid feature map %d for feature %d\n",
			sspp_feature_map[feature], feature);
		return -ENOTSUPP;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	blk = sspp_mapping[idx];
	rc = dma_ops->check_support(sspp_feature_map[feature], blk,
				    &is_supported);
	if (!rc)
		rc = (is_supported) ? 0 : -ENOTSUPP;

	if (!rc) {
		for (i = SDE_SSPP_RECT_SOLO; i < SDE_SSPP_RECT_MAX; i++) {
			rc = reg_dma_buf_init(
				&sspp_buf[i][sspp_feature_map[feature]][idx],
				sspp_feature_reg_dma_sz[feature]);
			if (rc) {
				DRM_ERROR("rect %d buf init failed\n", i);
				break;
			}
		}

	}

	return rc;
}

static int reg_dma_sspp_check(struct sde_hw_pipe *ctx, void *cfg,
		enum sde_reg_dma_features feature,
		enum sde_sspp_multirect_index idx)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_hw_cp_cfg *hw_cfg = cfg;

	if (!cfg || !ctx) {
		DRM_ERROR("invalid cfg %pK ctx %pK\n", cfg, ctx);
		return -EINVAL;
	}

	if (idx >= SDE_SSPP_RECT_MAX) {
		DRM_ERROR("invalid multirect idx %d\n", idx);
		return -EINVAL;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -EINVAL;

	if (!hw_cfg->ctl || !SDE_SSPP_VALID(ctx->idx) || feature >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid ctl %pK sspp idx %d feature %d\n",
			hw_cfg->ctl, ctx->idx, feature);
		return -EINVAL;
	}

	if (!sspp_buf[idx][feature][ctx->idx]) {
		DRM_ERROR("invalid dma_buf for rect idx %d sspp idx %d\n", idx,
			ctx->idx);
		return -EINVAL;
	}

	return 0;
}

static void vig_gamutv5_off(struct sde_hw_pipe *ctx, void *cfg)
{
	int rc;
	u32 op_mode = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 gamut_base = ctx->cap->sblk->gamut_blk.regdma_base;
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], GAMUT,
			sspp_buf[idx][GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, gamut_base,
		&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode modify single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][GAMUT][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GAMUT);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_vig_gamutv5(struct sde_hw_pipe *ctx, void *cfg)
{
	int rc;
	u32 i, op_mode, reg, tbl_len, tbl_off, scale_off, scale_tbl_off;
	u32 *scale_data;
	struct drm_msm_3d_gamut *payload;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 gamut_base = ctx->cap->sblk->gamut_blk.regdma_base;
	bool use_2nd_memory = false;
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;

	rc = reg_dma_sspp_check(ctx, cfg, GAMUT, idx);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable gamut feature\n");
		/* v5 and v6 call the same off version */
		LOG_FEATURE_OFF;
		vig_gamutv5_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_3d_gamut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_3d_gamut));
		return;
	}
	op_mode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->gamut_blk.base);
	payload = hw_cfg->payload;
	rc = sde_gamut_get_mode_info(SSPP, payload, &tbl_len, &tbl_off,
			&op_mode, &scale_off);
	if (rc) {
		DRM_ERROR("invalid mode info rc %d\n", rc);
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], GAMUT,
			sspp_buf[idx][GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}
	if ((op_mode & (BIT(5) - 1)) >> 2 == gamut_mode_17b)
		use_2nd_memory = true;
	for (i = 0; i < GAMUT_3D_TBL_NUM; i++) {
		reg = GAMUT_TABLE0_SEL << i;
		reg |= ((tbl_off) & (BIT(11) - 1));
		/* when bit 11 equals to 1, 2nd memory will be in use */
		if (use_2nd_memory)
			reg |= BIT(11);
		REG_DMA_SETUP_OPS(dma_write_cfg,
			gamut_base + GAMUT_TABLE_SEL_OFF,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write tbl sel reg failed ret %d\n", rc);
			return;
		}
		REG_DMA_SETUP_OPS(dma_write_cfg,
		    gamut_base + GAMUT_LOWER_COLOR_OFF,
		    &payload->col[i][0].c2_c1, tbl_len,
		    REG_BLK_WRITE_MULTIPLE, 2, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write color reg failed ret %d\n", rc);
			return;
		}
	}

	if (op_mode & GAMUT_MAP_EN) {
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			scale_tbl_off = gamut_base + scale_off +
					(i * GAMUT_SCALE_OFF_LEN);
			scale_data = &payload->scale_off[i][0];
			REG_DMA_SETUP_OPS(dma_write_cfg, scale_tbl_off,
					scale_data, GAMUT_SCALE_OFF_LEN,
					REG_BLK_WRITE_SINGLE, 0, 0, 0);
			rc = dma_ops->setup_payload(&dma_write_cfg);
			if (rc) {
				DRM_ERROR("write scale/off reg failed ret %d\n",
						rc);
				return;
			}
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, gamut_base,
		&op_mode, sizeof(op_mode), REG_SINGLE_MODIFY, 0, 0,
		REG_DMA_VIG_GAMUT_OP_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][GAMUT][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GAMUT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_vig_gamutv6(struct sde_hw_pipe *ctx, void *cfg)
{
	reg_dmav1_setup_vig_gamutv5(ctx, cfg);
}

static void vig_igcv5_off(struct sde_hw_pipe *ctx, void *cfg)
{
	int rc;
	u32 op_mode = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 igc_base = ctx->cap->sblk->igc_blk[0].regdma_base;
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], IGC,
			sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, igc_base, &op_mode, sizeof(op_mode),
		REG_SINGLE_MODIFY, 0, 0, REG_DMA_VIG_IGC_OP_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode modify single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][IGC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

static int reg_dmav1_setup_vig_igc_common(struct sde_hw_reg_dma_ops *dma_ops,
				struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
				struct sde_hw_pipe *ctx,
				struct sde_hw_cp_cfg *hw_cfg, u32 mask,
				struct drm_msm_igc_lut *igc_lut)
{
	int rc = 0;
	u32 i = 0, j = 0, reg = 0, index = 0;
	u32 offset = 0;
	u32 lut_sel = 0, lut_enable = 0;
	u32 *data = NULL, *data_ptr = NULL;
	u32 igc_base = ctx->cap->sblk->igc_blk[0].regdma_base;
	u32 *addr[IGC_TBL_NUM];

	if (hw_cfg->len != sizeof(struct drm_msm_igc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_igc_lut));
	}

	data = kvzalloc(VIG_1D_LUT_IGC_LEN * sizeof(u32), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	reg = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->igc_blk[0].base);
	lut_enable = (reg >> 8) & BIT(0);
	lut_sel = (reg >> 9) & BIT(0);
	/* select LUT table (0 or 1) when 1D LUT is in active mode */
	if (lut_enable)
		lut_sel = (~lut_sel) && BIT(0);

	addr[0] = igc_lut->c0;
	addr[1] = igc_lut->c1;
	addr[2] = igc_lut->c2;
	for (i = 0; i < IGC_TBL_NUM; i++) {
		/* write 0 to the index register */
		index = 0;
		REG_DMA_SETUP_OPS(*dma_write_cfg, igc_base + 0x1B0,
			&index, sizeof(index), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(dma_write_cfg);
		if (rc) {
			DRM_ERROR("VIG IGC index write failed ret %d\n", rc);
			goto exit;
		}

		offset = igc_base + 0x1B4 + i * sizeof(u32);
		data_ptr = addr[i];
		for (j = 0; j < VIG_1D_LUT_IGC_LEN; j++)
			data[j] = (data_ptr[2 * j] & mask) |
				(data_ptr[2 * j + 1] & mask) << 16;

		REG_DMA_SETUP_OPS(*dma_write_cfg, offset, data,
				VIG_1D_LUT_IGC_LEN * sizeof(u32),
				REG_BLK_WRITE_INC, 0, 0, 0);
		rc = dma_ops->setup_payload(dma_write_cfg);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			goto exit;
		}
	}

	if (igc_lut->flags & IGC_DITHER_ENABLE) {
		reg = igc_lut->strength & IGC_DITHER_DATA_MASK;
		reg |= BIT(4);
	} else {
		reg = 0;
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, igc_base + 0x1C0,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("dither strength failed ret %d\n", rc);
		goto exit;
	}

	reg = BIT(8) | (lut_sel << 9);
	REG_DMA_SETUP_OPS(*dma_write_cfg, igc_base, &reg, sizeof(reg),
		REG_SINGLE_MODIFY, 0, 0, REG_DMA_VIG_IGC_OP_MASK);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc)
		DRM_ERROR("setting opcode failed ret %d\n", rc);
exit:
	kvfree(data);
	return rc;
}

void reg_dmav1_setup_vig_igcv5(struct sde_hw_pipe *ctx, void *cfg)
{
	int rc;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct drm_msm_igc_lut *igc_lut;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;

	rc = reg_dma_sspp_check(ctx, hw_cfg, IGC, idx);
	if (rc)
		return;

	igc_lut = hw_cfg->payload;
	if (!igc_lut) {
		DRM_DEBUG_DRIVER("disable igc feature\n");
		LOG_FEATURE_OFF;
		vig_igcv5_off(ctx, hw_cfg);
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], IGC,
			sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	rc = reg_dmav1_setup_vig_igc_common(dma_ops, &dma_write_cfg,
			ctx, cfg, VIG_IGC_DATA_MASK, igc_lut);
	if (rc) {
		DRM_ERROR("setup_vig_igc_common failed\n");
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][IGC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_vig_igcv6(struct sde_hw_pipe *ctx, void *cfg)
{
	int rc;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	u32 igc_base = ctx->cap->sblk->igc_blk[0].regdma_base;
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;
	struct drm_msm_igc_lut *igc_lut;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;

	rc = reg_dma_sspp_check(ctx, hw_cfg, IGC, idx);
	if (rc)
		return;

	igc_lut = hw_cfg->payload;
	if (!igc_lut) {
		DRM_DEBUG_DRIVER("disable igc feature\n");
		LOG_FEATURE_OFF;
		/* Both v5 and v6 call same igcv5_off */
		vig_igcv5_off(ctx, hw_cfg);
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], IGC,
			sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	rc = reg_dmav1_setup_vig_igc_common(dma_ops, &dma_write_cfg,
			ctx, cfg, VIG_IGC_DATA_MASK_V6, igc_lut);
	if (rc) {
		DRM_ERROR("setup_vig_igcv6 failed\n");
		return;
	}

	/* Perform LAST_LUT required for v6*/
	REG_DMA_SETUP_OPS(dma_write_cfg, igc_base + 0x1C4, &igc_lut->c0_last,
		sizeof(u32) * 3, REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("c_last failed ret %d", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][IGC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

static void dma_igcv5_off(struct sde_hw_pipe *ctx, void *cfg,
			enum sde_sspp_multirect_index idx)
{
	int rc;
	u32 op_mode = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 igc_opmode_off;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], IGC,
			sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	if (idx == SDE_SSPP_RECT_SOLO || idx == SDE_SSPP_RECT_0)
		igc_opmode_off = DMA_DGM_0_OP_MODE_OFF;
	else
		igc_opmode_off = DMA_DGM_1_OP_MODE_OFF;

	REG_DMA_SETUP_OPS(dma_write_cfg, igc_opmode_off, &op_mode,
			sizeof(op_mode), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_DMA_IGC_OP_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode modify single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][IGC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_dma_igcv5(struct sde_hw_pipe *ctx, void *cfg,
			enum sde_sspp_multirect_index idx)
{
	int rc;
	u32 i = 0, reg = 0;
	u32 *data = NULL;
	struct drm_msm_igc_lut *igc_lut;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 igc_base, igc_dither_off, igc_opmode_off;

	rc = reg_dma_sspp_check(ctx, cfg, IGC, idx);
	if (rc)
		return;

	igc_lut = hw_cfg->payload;
	if (!igc_lut) {
		DRM_DEBUG_DRIVER("disable igc feature\n");
		LOG_FEATURE_OFF;
		dma_igcv5_off(ctx, cfg, idx);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_igc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_igc_lut));
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], IGC,
			sspp_buf[idx][IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	data = kvzalloc(DMA_1D_LUT_IGC_LEN * sizeof(u32), GFP_KERNEL);
	if (!data) {
		DRM_ERROR("failed to allocate memory for igc\n");
		return;
	}

	/* client packs the 1D LUT data in c2 instead of c0 */
	for (i = 0; i < DMA_1D_LUT_IGC_LEN; i++)
		data[i] = (igc_lut->c2[2 * i] & IGC_DATA_MASK) |
			((igc_lut->c2[2 * i + 1] & IGC_DATA_MASK) << 16);

	if (idx == SDE_SSPP_RECT_SOLO || idx == SDE_SSPP_RECT_0) {
		igc_base = ctx->cap->sblk->igc_blk[0].regdma_base;
		igc_dither_off = igc_base + DMA_1D_LUT_IGC_DITHER_OFF;
		igc_opmode_off = DMA_DGM_0_OP_MODE_OFF;
	} else {
		igc_base = ctx->cap->sblk->igc_blk[1].regdma_base;
		igc_dither_off = igc_base + DMA_1D_LUT_IGC_DITHER_OFF;
		igc_opmode_off = DMA_DGM_1_OP_MODE_OFF;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, igc_base, data,
			DMA_1D_LUT_IGC_LEN * sizeof(u32),
			REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("lut write failed ret %d\n", rc);
		goto igc_exit;
	}
	if (igc_lut->flags & IGC_DITHER_ENABLE) {
		reg = igc_lut->strength & IGC_DITHER_DATA_MASK;
		reg |= BIT(4);
	} else {
		reg = 0;
	}
	REG_DMA_SETUP_OPS(dma_write_cfg, igc_dither_off, &reg,
			sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("failed to set dither strength %d\n", rc);
		goto igc_exit;
	}

	reg = BIT(1);
	REG_DMA_SETUP_OPS(dma_write_cfg, igc_opmode_off, &reg, sizeof(reg),
			REG_SINGLE_MODIFY, 0, 0, REG_DMA_DMA_IGC_OP_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		goto igc_exit;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][IGC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, IGC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
igc_exit:
	kvfree(data);
}

static void dma_gcv5_off(struct sde_hw_pipe *ctx, void *cfg,
			enum sde_sspp_multirect_index idx)
{
	int rc;
	u32 op_mode = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 gc_opmode_off;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][GC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], GC,
			sspp_buf[idx][GC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	if (idx == SDE_SSPP_RECT_SOLO || idx == SDE_SSPP_RECT_0)
		gc_opmode_off = DMA_DGM_0_OP_MODE_OFF;
	else
		gc_opmode_off = DMA_DGM_1_OP_MODE_OFF;

	REG_DMA_SETUP_OPS(dma_write_cfg, gc_opmode_off, &op_mode,
			sizeof(op_mode), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_DMA_GC_OP_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode modify single reg failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][GC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GC);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

void reg_dmav1_setup_dma_gcv5(struct sde_hw_pipe *ctx, void *cfg,
			enum sde_sspp_multirect_index idx)
{
	int rc;
	u32 reg = 0;
	struct drm_msm_pgc_lut *gc_lut;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 gc_base, gc_opmode_off;

	rc = reg_dma_sspp_check(ctx, cfg, GC, idx);
	if (rc)
		return;

	gc_lut = hw_cfg->payload;
	if (!gc_lut) {
		DRM_DEBUG_DRIVER("disable gc feature\n");
		LOG_FEATURE_OFF;
		dma_gcv5_off(ctx, cfg, idx);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_pgc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_pgc_lut));
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][GC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], GC,
			sspp_buf[idx][GC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	if (idx == SDE_SSPP_RECT_SOLO || idx == SDE_SSPP_RECT_0) {
		gc_base = ctx->cap->sblk->gc_blk[0].regdma_base;
		gc_opmode_off = DMA_DGM_0_OP_MODE_OFF;
	} else {
		gc_base = ctx->cap->sblk->gc_blk[1].regdma_base;
		gc_opmode_off = DMA_DGM_1_OP_MODE_OFF;
	}

	/* client packs the 1D LUT data in c2 instead of c0,
	 * and even & odd values are already stacked in register foramt
	 */
	REG_DMA_SETUP_OPS(dma_write_cfg, gc_base, gc_lut->c2,
			DMA_1D_LUT_GC_LEN * sizeof(u32),
			REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("lut write failed ret %d\n", rc);
		return;
	}
	reg = BIT(2);
	REG_DMA_SETUP_OPS(dma_write_cfg, gc_opmode_off, &reg,
			sizeof(reg), REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_DMA_GC_OP_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][GC][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GC);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

int reg_dmav1_deinit_sspp_ops(enum sde_sspp idx)
{
	u32 i, j;
	struct sde_hw_reg_dma_ops *dma_ops;

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	if (idx >= SSPP_MAX) {
		DRM_ERROR("invalid sspp idx %x max %x\n", idx, SSPP_MAX);
		return -EINVAL;
	}

	for (i = SDE_SSPP_RECT_SOLO; i < SDE_SSPP_RECT_MAX; i++) {
		for (j = 0; j < REG_DMA_FEATURES_MAX; j++) {
			if (!sspp_buf[i][j][idx])
				continue;
			dma_ops->dealloc_reg_dma(sspp_buf[i][j][idx]);
			sspp_buf[i][j][idx] = NULL;
		}
	}
	return 0;
}

void reg_dmav1_setup_scaler3_lut(struct sde_reg_dma_setup_ops_cfg *buf,
		struct sde_hw_scaler3_cfg *scaler3_cfg, u32 offset)
{
	int i, filter, rc;
	int config_lut = 0x0;
	unsigned long lut_flags;
	u32 lut_addr, lut_offset, lut_len;
	struct sde_hw_reg_dma_ops *dma_ops;
	u32 *lut[QSEED3_FILTERS] = {NULL, NULL, NULL, NULL, NULL};
	static const uint32_t off_tbl[QSEED3_FILTERS][QSEED3_LUT_REGIONS][2] = {
		{{18, 0x000}, {12, 0x120}, {12, 0x1E0}, {8, 0x2A0} },
		{{6, 0x320}, {3, 0x3E0}, {3, 0x440}, {3, 0x4A0} },
		{{6, 0x500}, {3, 0x5c0}, {3, 0x620}, {3, 0x680} },
		{{6, 0x380}, {3, 0x410}, {3, 0x470}, {3, 0x4d0} },
		{{6, 0x560}, {3, 0x5f0}, {3, 0x650}, {3, 0x6b0} },
	};

	dma_ops = sde_reg_dma_get_ops();
	lut_flags = (unsigned long) scaler3_cfg->lut_flag;
	if (test_bit(QSEED3_COEF_LUT_DIR_BIT, &lut_flags) &&
		(scaler3_cfg->dir_len == QSEED3_DIR_LUT_SIZE)) {
		lut[0] = scaler3_cfg->dir_lut;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_Y_CIR_BIT, &lut_flags) &&
		(scaler3_cfg->y_rgb_cir_lut_idx < QSEED3_CIRCULAR_LUTS) &&
		(scaler3_cfg->cir_len == QSEED3_CIR_LUT_SIZE)) {
		lut[1] = scaler3_cfg->cir_lut +
			scaler3_cfg->y_rgb_cir_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_UV_CIR_BIT, &lut_flags) &&
		(scaler3_cfg->uv_cir_lut_idx < QSEED3_CIRCULAR_LUTS) &&
		(scaler3_cfg->cir_len == QSEED3_CIR_LUT_SIZE)) {
		lut[2] = scaler3_cfg->cir_lut +
			scaler3_cfg->uv_cir_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_Y_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->y_rgb_sep_lut_idx < QSEED3_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3_SEP_LUT_SIZE)) {
		lut[3] = scaler3_cfg->sep_lut +
			scaler3_cfg->y_rgb_sep_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_UV_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->uv_sep_lut_idx < QSEED3_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3_SEP_LUT_SIZE)) {
		lut[4] = scaler3_cfg->sep_lut +
			scaler3_cfg->uv_sep_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}

	for (filter = 0; filter < QSEED3_FILTERS && config_lut; filter++) {
		if (!lut[filter])
			continue;
		lut_offset = 0;
		for (i = 0; i < QSEED3_LUT_REGIONS; i++) {
			lut_addr = QSEED3_COEF_LUT_OFF + offset
				+ off_tbl[filter][i][1];
			lut_len = off_tbl[filter][i][0] << 2;
			REG_DMA_SETUP_OPS(*buf, lut_addr,
				&lut[filter][lut_offset], lut_len * sizeof(u32),
				REG_BLK_WRITE_SINGLE, 0, 0, 0);
			rc = dma_ops->setup_payload(buf);
			if (rc) {
				DRM_ERROR("lut write failed ret %d\n", rc);
				return;
			}
			lut_offset += lut_len;
		}
	}

	if (test_bit(QSEED3_COEF_LUT_SWAP_BIT, &lut_flags)) {
		i = BIT(0);
		REG_DMA_SETUP_OPS(*buf, QSEED3_COEF_LUT_CTRL_OFF + offset, &i,
				sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(buf);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			return;
		}
	}

}

void reg_dmav1_setup_scaler3lite_lut(
		struct sde_reg_dma_setup_ops_cfg *buf,
			struct sde_hw_scaler3_cfg *scaler3_cfg, u32 offset)
{
	int i, filter, rc;
	int config_lut = 0x0;
	unsigned long lut_flags;
	u32 lut_addr, lut_offset;
	struct sde_hw_reg_dma_ops *dma_ops;
	u32 *lut[QSEED3LITE_FILTERS] = {NULL, NULL};
	static const uint32_t off_tbl[QSEED3LITE_FILTERS] = {0x000, 0x200};

	/* destination scaler case */
	if (!scaler3_cfg->sep_lut)
		return;

	dma_ops = sde_reg_dma_get_ops();
	lut_flags = (unsigned long) scaler3_cfg->lut_flag;
	if (test_bit(QSEED3L_COEF_LUT_Y_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->y_rgb_sep_lut_idx < QSEED3L_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3L_SEP_LUT_SIZE)) {
		lut[Y_INDEX] = scaler3_cfg->sep_lut +
			scaler3_cfg->y_rgb_sep_lut_idx * QSEED3L_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3L_COEF_LUT_UV_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->uv_sep_lut_idx < QSEED3L_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3L_SEP_LUT_SIZE)) {
		lut[UV_INDEX] = scaler3_cfg->sep_lut +
			scaler3_cfg->uv_sep_lut_idx * QSEED3L_LUT_SIZE;
		config_lut = 1;
	}

	for (filter = 0; filter < QSEED3LITE_FILTERS && config_lut; filter++) {
		if (!lut[filter])
			continue;
		lut_offset = 0;
		lut_addr = QSEED3L_COEF_LUT_OFF + offset
			+ off_tbl[filter];
		REG_DMA_SETUP_OPS(*buf, lut_addr,
				&lut[filter][0], QSEED3L_LUT_SIZE * sizeof(u32),
				REG_BLK_WRITE_SINGLE, 0, 0, 0);
		rc = dma_ops->setup_payload(buf);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			return;
		}
	}

	if (test_bit(QSEED3L_COEF_LUT_SWAP_BIT, &lut_flags)) {
		i = BIT(0);
		REG_DMA_SETUP_OPS(*buf, QSEED3L_COEF_LUT_CTRL_OFF + offset, &i,
				sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(buf);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			return;
		}
	}
}

static int reg_dmav1_setup_scaler3_de(struct sde_reg_dma_setup_ops_cfg *buf,
	struct sde_hw_scaler3_cfg *scaler3_cfg, u32 offset, bool de_lpf)
{
	u32 de_config[7];
	struct sde_hw_reg_dma_ops *dma_ops;
	int rc;
	struct sde_hw_scaler3_de_cfg *de_cfg = &scaler3_cfg->de;
	u32 de_lpf_config;

	dma_ops = sde_reg_dma_get_ops();
	de_config[0] = (de_cfg->sharpen_level1 & 0x1FF) |
		((de_cfg->sharpen_level2 & 0x1FF) << 16);

	de_config[1] = ((de_cfg->limit & 0xF) << 9) |
		((de_cfg->prec_shift & 0x7) << 13) |
		((de_cfg->clip & 0x7) << 16) |
		((de_cfg->blend & 0xF) << 20);

	de_config[2] = (de_cfg->thr_quiet & 0xFF) |
		((de_cfg->thr_dieout & 0x3FF) << 16);

	de_config[3] = (de_cfg->thr_low & 0x3FF) |
		((de_cfg->thr_high & 0x3FF) << 16);

	de_config[4] = (de_cfg->adjust_a[0] & 0x3FF) |
		((de_cfg->adjust_a[1] & 0x3FF) << 10) |
		((de_cfg->adjust_a[2] & 0x3FF) << 20);

	de_config[5] = (de_cfg->adjust_b[0] & 0x3FF) |
		((de_cfg->adjust_b[1] & 0x3FF) << 10) |
		((de_cfg->adjust_b[2] & 0x3FF) << 20);

	de_config[6] = (de_cfg->adjust_c[0] & 0x3FF) |
		((de_cfg->adjust_c[1] & 0x3FF) << 10) |
		((de_cfg->adjust_c[2] & 0x3FF) << 20);

	REG_DMA_SETUP_OPS(*buf, offset + QSEED3_DE_OFFSET,
		de_config, sizeof(de_config), REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(buf);
	if (rc) {
		DRM_ERROR("de write failed ret %d\n", rc);
		return rc;
	}

	if (de_lpf) {
		if (scaler3_cfg->de_lpf_flags & SDE_DE_LPF_BLEND_FLAG_EN)
			de_lpf_config = (scaler3_cfg->de_lpf_l & 0x3FF) |
				((scaler3_cfg->de_lpf_m & 0x3FF) << 10) |
				((scaler3_cfg->de_lpf_h & 0x3FF) << 20);
		else
			de_lpf_config = QSEED5_DEFAULT_DE_LPF_BLEND;

		REG_DMA_SETUP_OPS(*buf, offset + QSEED5_DE_LPF_OFFSET,
			&de_lpf_config, sizeof(u32), REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(buf);
		if (rc) {
			DRM_ERROR("de lpf write failed ret %d\n", rc);
			return rc;
		}
	}

	return 0;
}

void reg_dmav1_setup_vig_qseed3(struct sde_hw_pipe *ctx,
	struct sde_hw_pipe_cfg *sspp, struct sde_hw_pixel_ext *pe,
	void *scaler_cfg)
{
	struct sde_hw_scaler3_cfg *scaler3_cfg = scaler_cfg;
	int rc;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_cp_cfg hw_cfg = {};
	u32 op_mode = 0, offset;
	u32 preload, src_y_rgb, src_uv, dst, dir_weight;
	u32 cache[4];
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;
	bool de_lpf_cap = false;

	if (!ctx || !pe || !scaler_cfg) {
		DRM_ERROR("invalid params ctx %pK pe %pK scaler_cfg %pK",
			ctx, pe, scaler_cfg);
		return;
	}

	hw_cfg.ctl = ctx->ctl;
	hw_cfg.payload = scaler_cfg;
	hw_cfg.len = sizeof(*scaler3_cfg);
	rc = reg_dma_sspp_check(ctx, &hw_cfg, QSEED, idx);
	if (rc || !sspp) {
		DRM_ERROR("invalid params rc %d sspp %pK\n", rc, sspp);
		return;
	}

	offset = ctx->cap->sblk->scaler_blk.regdma_base;
	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][QSEED][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], QSEED,
	    sspp_buf[idx][QSEED][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	if (!scaler3_cfg->enable) {
		LOG_FEATURE_OFF;
		goto end;
	} else {
		LOG_FEATURE_ON;
	}

	op_mode = BIT(0);
	op_mode |= (scaler3_cfg->y_rgb_filter_cfg & 0x3) << 16;

	if (sspp->layout.format && SDE_FORMAT_IS_YUV(sspp->layout.format)) {
		op_mode |= BIT(12);
		op_mode |= (scaler3_cfg->uv_filter_cfg & 0x3) << 24;
	}

	op_mode |= (scaler3_cfg->blend_cfg & 1) << 31;
	op_mode |= (scaler3_cfg->dir_en) ? BIT(4) : 0;
	op_mode |= (scaler3_cfg->dir_en && scaler3_cfg->cor_en) ? BIT(5) : 0;
	op_mode |= (scaler3_cfg->dir_en && scaler3_cfg->dir45_en) ? BIT(6) : 0;
	op_mode |= (scaler3_cfg->dyn_exp_disabled) ? BIT(13) : 0;

	preload =
		((scaler3_cfg->preload_x[0] & 0x7F) << 0) |
		((scaler3_cfg->preload_y[0] & 0x7F) << 8) |
		((scaler3_cfg->preload_x[1] & 0x7F) << 16) |
		((scaler3_cfg->preload_y[1] & 0x7F) << 24);

	src_y_rgb = (scaler3_cfg->src_width[0] & 0xFFFF) |
		((scaler3_cfg->src_height[0] & 0xFFFF) << 16);

	src_uv = (scaler3_cfg->src_width[1] & 0xFFFF) |
		((scaler3_cfg->src_height[1] & 0xFFFF) << 16);

	dst = (scaler3_cfg->dst_width & 0xFFFF) |
		((scaler3_cfg->dst_height & 0xFFFF) << 16);

	if (scaler3_cfg->de.enable) {
		if (test_bit(SDE_SSPP_SCALER_DE_LPF_BLEND, &ctx->cap->features))
			de_lpf_cap = true;
		rc = reg_dmav1_setup_scaler3_de(&dma_write_cfg,
			scaler3_cfg, offset, de_lpf_cap);
		if (!rc)
			op_mode |= BIT(8);
	}

	ctx->ops.setup_scaler_lut(&dma_write_cfg, scaler3_cfg, offset);

	cache[0] = scaler3_cfg->init_phase_x[0] & 0x1FFFFF;
	cache[1] = scaler3_cfg->init_phase_y[0] & 0x1FFFFF;
	cache[2] = scaler3_cfg->init_phase_x[1] & 0x1FFFFF;
	cache[3] = scaler3_cfg->init_phase_y[1] & 0x1FFFFF;
	REG_DMA_SETUP_OPS(dma_write_cfg,
		offset + 0x90, cache, sizeof(cache),
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting phase failed ret %d\n", rc);
		return;
	}

	cache[0] = scaler3_cfg->phase_step_x[0] & 0xFFFFFF;
	cache[1] = scaler3_cfg->phase_step_y[0] & 0xFFFFFF;
	cache[2] = scaler3_cfg->phase_step_x[1] & 0xFFFFFF;
	cache[3] = scaler3_cfg->phase_step_y[1] & 0xFFFFFF;
	REG_DMA_SETUP_OPS(dma_write_cfg,
		offset + 0x10, cache, sizeof(cache),
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting phase failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		offset + 0x20, &preload, sizeof(u32),
		REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting preload failed ret %d\n", rc);
		return;
	}

	cache[0] = src_y_rgb;
	cache[1] = src_uv;
	cache[2] = dst;

	REG_DMA_SETUP_OPS(dma_write_cfg,
		offset + 0x40, cache, 3 * sizeof(u32),
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting sizes failed ret %d\n", rc);
		return;
	}

	if (is_qseed3_rev_qseed3lite(ctx->catalog)) {
		dir_weight = (scaler3_cfg->dir_weight & 0xFF);

		REG_DMA_SETUP_OPS(dma_write_cfg,
				offset + 0x60, &dir_weight, sizeof(u32),
				REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			return;
		}
	}

end:
	if (sspp->layout.format) {
		if (!SDE_FORMAT_IS_DX(sspp->layout.format))
			op_mode |= BIT(14);
		if (sspp->layout.format->alpha_enable) {
			op_mode |= BIT(10);
			op_mode |= (scaler3_cfg->alpha_filter_cfg & 0x3) << 29;
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
		offset + 0x4,
		&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opmode failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg.ctl,
			sspp_buf[idx][QSEED][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, QSEED);
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);

}

int reg_dmav1_init_ltm_op_v6(int feature, enum sde_dspp dspp_idx)
{
	int rc = -ENOTSUPP;
	struct sde_hw_reg_dma_ops *dma_ops;
	bool is_supported = false;
	u32 blk;
	/* LTM blocks are hardwired to DSPP blocks */
	enum sde_ltm idx = (enum sde_ltm)dspp_idx;

	if (feature >= SDE_LTM_MAX || idx >= LTM_MAX) {
		DRM_ERROR("invalid feature %x max %x ltm idx %x max %xd\n",
			feature, SDE_LTM_MAX, idx, LTM_MAX);
		return rc;
	}

	if (ltm_feature_map[feature] >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid feature map %d for feature %d\n",
			ltm_feature_map[feature], feature);
		return -ENOTSUPP;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	blk = ltm_mapping[idx];
	rc = dma_ops->check_support(ltm_feature_map[feature], blk,
			&is_supported);
	if (!rc)
		rc = (is_supported) ? 0 : -ENOTSUPP;

	if (!rc)
		rc = reg_dma_buf_init(&ltm_buf[ltm_feature_map[feature]][idx],
				ltm_feature_reg_dma_sz[feature]);
	return rc;
}


int reg_dmav1_deinit_ltm_ops(enum sde_dspp dspp_idx)
{
	int i;
	struct sde_hw_reg_dma_ops *dma_ops;
	/* LTM blocks are hardwired to DSPP blocks */
	enum sde_ltm idx = (enum sde_ltm)dspp_idx;

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	if (idx >= LTM_MAX) {
		DRM_DEBUG("invalid ltm idx %x max %xd\n", idx, LTM_MAX);
		return -EINVAL;
	}

	for (i = 0; i < REG_DMA_FEATURES_MAX; i++) {
		if (!ltm_buf[i][idx])
			continue;
		dma_ops->dealloc_reg_dma(ltm_buf[i][idx]);
		ltm_buf[i][idx] = NULL;
	}
	return 0;
}

static int reg_dma_ltm_check(struct sde_hw_dspp *ctx, void *cfg,
		enum sde_reg_dma_features feature)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_hw_cp_cfg *hw_cfg = cfg;

	if (!ctx || !cfg) {
		DRM_ERROR("invalid ctx %pK cfg %pK\n", ctx, cfg);
		return -EINVAL;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -EINVAL;

	if (!hw_cfg->ctl || ctx->idx >= DSPP_MAX ||
		feature >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid ctl %pK dspp idx %d feature %d\n",
			hw_cfg->ctl, ctx->idx, feature);
		return -EINVAL;
	}

	if (!ltm_buf[feature][ctx->idx]) {
		DRM_ERROR("invalid dma_buf\n");
		return -EINVAL;
	}
	return 0;
}

static int reg_dmav1_get_ltm_blk(struct sde_hw_cp_cfg *hw_cfg,
	enum sde_ltm idx, enum sde_ltm *dspp_idx, u32 *blk)
{
	struct sde_hw_mixer *hw_lm = NULL;
	u32 i = 0, num_mixers = 0;

	if (idx >= LTM_MAX) {
		DRM_ERROR("invalid ltm idx %d\n", idx);
		return -EINVAL;
	}

	num_mixers = hw_cfg->num_of_mixers;
	hw_lm = hw_cfg->mixer_info;
	if (num_mixers == 1) {
		*blk = ltm_mapping[idx];
		dspp_idx[0] = (enum sde_ltm)(hw_cfg->dspp[0]->idx);
	} else if (num_mixers == 2) {
		if (hw_lm->cfg.right_mixer) {
			DRM_DEBUG_DRIVER("slave LTM instance\n");
			return -EALREADY;
		}
		*blk = 0;
		for (i = 0; i < num_mixers; i++) {
			if (hw_cfg->dspp[i] && (i < LTM_MAX)) {
				dspp_idx[i] =
					(enum sde_ltm)(hw_cfg->dspp[i]->idx);
				*blk |= ltm_mapping[dspp_idx[i]];
			} else {
				DRM_ERROR("invalid dspp = %pK, i = %d\n",
					hw_cfg->dspp[i], i);
				return -EINVAL;
			}
		}
	} else {
		DRM_ERROR("invalid num_of_mixers %d for LTM\n",
				hw_cfg->num_of_mixers);
		return -EINVAL;
	}
	return 0;
}

static void ltm_initv1_disable(struct sde_hw_dspp *ctx, void *cfg,
		u32 num_mixers, enum sde_ltm *dspp_idx)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	int rc, i = 0;
	enum sde_ltm idx = 0;
	u32 opmode = 0;

	idx = (enum sde_ltm)ctx->idx;
	if (idx >= LTM_MAX) {
		DRM_ERROR("invalid ltm idx %d\n", ctx->idx);
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(ltm_buf[LTM_INIT][idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, ltm_mapping[idx], LTM_INIT,
			ltm_buf[LTM_INIT][idx]);

	for (i = 0; i < num_mixers; i++) {
		dma_write_cfg.blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		ltm_vlut_ops_mask[dspp_idx[i]] &= ~ltm_dither;
		ltm_vlut_ops_mask[dspp_idx[i]] &= ~ltm_unsharp;
		REG_DMA_SETUP_OPS(dma_write_cfg, 0x04, &opmode, sizeof(opmode),
			REG_SINGLE_MODIFY, 0, 0,
			REG_DMA_LTM_INIT_DISABLE_OP_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("opmode write failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, ltm_buf[LTM_INIT][idx],
				REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
				LTM_INIT);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}
}

void reg_dmav1_setup_ltm_initv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct drm_msm_ltm_init_param *init_param = NULL;
	struct sde_ltm_phase_info phase;
	enum sde_ltm dspp_idx[LTM_MAX] = {0};
	enum sde_ltm idx = 0;
	u32 blk = 0, opmode = 0, i = 0, num_mixers = 0;
	u32 phase_data[3];
	int rc = 0;

	rc = reg_dma_ltm_check(ctx, cfg, LTM_INIT);
	if (rc)
		return;

	idx = (enum sde_ltm)ctx->idx;
	rc = reg_dmav1_get_ltm_blk(hw_cfg, idx, &dspp_idx[0], &blk);
	if (rc) {
		if (rc != -EALREADY)
			DRM_ERROR("failed to get the blk info\n");
		return;
	}

	num_mixers = hw_cfg->num_of_mixers;
	/* disable case */
	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("Disable LTM init feature\n");
		LOG_FEATURE_OFF;
		ltm_initv1_disable(ctx, cfg, num_mixers, dspp_idx);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_ltm_init_param)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_ltm_init_param));
		return;
	}

	init_param = hw_cfg->payload;


	memset(&phase, 0, sizeof(phase));
	sde_ltm_get_phase_info(hw_cfg, &phase);

	if (phase.portrait_en)
		opmode |= BIT(2);
	else
		opmode &= ~BIT(2);

	phase_data[0] = phase.init_v;
	phase_data[1] = phase.inc_h;
	phase_data[2] = phase.inc_v;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(ltm_buf[LTM_INIT][idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, LTM_INIT, ltm_buf[LTM_INIT][idx]);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, 0x0c, phase_data, sizeof(u32) * 3,
			REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write phase data failed ret %d\n",
				rc);
		return;
	}

	for (i = 0; i < num_mixers; i++) {
		/* reset decode select to unicast for phase init_h value*/
		dma_write_cfg.blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg, 0x08,
				&phase.init_h[dspp_idx[i]], sizeof(u32),
				REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("opmode write failed ret %d\n", rc);
			return;
		}

		if (init_param->init_param_01) {
			if (ltm_vlut_ops_mask[dspp_idx[i]] & ltm_vlut)
				opmode |= BIT(6);
			ltm_vlut_ops_mask[dspp_idx[i]] |= ltm_dither;
			opmode |= ((init_param->init_param_02 & 0x7) << 12);
		} else {
			opmode &= ~BIT(6);
			ltm_vlut_ops_mask[dspp_idx[i]] &= ~ltm_dither;
		}

		if (init_param->init_param_03) {
			if (ltm_vlut_ops_mask[dspp_idx[i]] & ltm_vlut)
				opmode |= BIT(4);
			ltm_vlut_ops_mask[dspp_idx[i]] |= ltm_unsharp;
			opmode |= ((init_param->init_param_04 & 0x3) << 8);
		} else {
			opmode &= ~BIT(4);
			ltm_vlut_ops_mask[dspp_idx[i]] &= ~ltm_unsharp;
		}

		/* broadcast feature is not supported with REG_SINGLE_MODIFY */
		REG_DMA_SETUP_OPS(dma_write_cfg, 0x04, &opmode, sizeof(opmode),
				REG_SINGLE_MODIFY, 0, 0,
				REG_DMA_LTM_INIT_ENABLE_OP_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("opmode write failed ret %d\n", rc);
			return;
		}
	}
	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, ltm_buf[LTM_INIT][idx],
				REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
				LTM_INIT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}
}

static void ltm_roiv1_disable(struct sde_hw_dspp *ctx, void *cfg,
		u32 num_mixers, enum sde_ltm *dspp_idx)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	int rc, i = 0;
	enum sde_ltm idx = 0;
	u32 opmode = 0;

	idx = (enum sde_ltm)ctx->idx;
	if (idx >= LTM_MAX) {
		DRM_ERROR("invalid ltm idx %d\n", ctx->idx);
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(ltm_buf[LTM_ROI][idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, ltm_mapping[idx], LTM_ROI,
			ltm_buf[LTM_ROI][idx]);

	for (i = 0; i < num_mixers; i++) {
		dma_write_cfg.blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		ltm_vlut_ops_mask[dspp_idx[i]] &= ~ltm_roi;

		REG_DMA_SETUP_OPS(dma_write_cfg, 0x04, &opmode, sizeof(opmode),
			REG_SINGLE_MODIFY, 0, 0, REG_DMA_LTM_ROI_OP_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("opmode write failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, ltm_buf[LTM_ROI][idx],
				REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
				LTM_ROI);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}
}

void reg_dmav1_setup_ltm_roiv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct drm_msm_ltm_cfg_param *cfg_param = NULL;
	enum sde_ltm dspp_idx[LTM_MAX] = {0};
	enum sde_ltm idx = 0;
	u32 blk = 0, opmode = 0, i = 0, num_mixers = 0;
	u32 roi_data[3];
	int rc = 0;

	rc = reg_dma_ltm_check(ctx, cfg, LTM_ROI);
	if (rc)
		return;

	idx = (enum sde_ltm)ctx->idx;
	rc = reg_dmav1_get_ltm_blk(hw_cfg, idx, &dspp_idx[0], &blk);
	if (rc) {
		if (rc != -EALREADY)
			DRM_ERROR("failed to get the blk info\n");
		return;
	}

	num_mixers = hw_cfg->num_of_mixers;
	/* disable case */
	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("Disable LTM roi feature\n");
		LOG_FEATURE_OFF;
		ltm_roiv1_disable(ctx, cfg, num_mixers, dspp_idx);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_ltm_cfg_param)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
			hw_cfg->len, sizeof(struct drm_msm_ltm_cfg_param));
		return;
	}

	cfg_param = hw_cfg->payload;
	/* input param exceeds the display width */
	if (cfg_param->cfg_param_01 + cfg_param->cfg_param_03 >
			hw_cfg->displayh) {
		DRM_DEBUG_DRIVER("invalid input = [%u,%u], displayh = %u\n",
			cfg_param->cfg_param_01, cfg_param->cfg_param_03,
			hw_cfg->displayh);
		/* set the roi width to max register value */
		cfg_param->cfg_param_03 = 0xFFFF;
	}

	/* input param exceeds the display height */
	if (cfg_param->cfg_param_02 + cfg_param->cfg_param_04 >
			hw_cfg->displayv) {
		DRM_DEBUG_DRIVER("invalid input = [%u,%u], displayv = %u\n",
			cfg_param->cfg_param_02, cfg_param->cfg_param_04,
			hw_cfg->displayv);
		/* set the roi height to max register value */
		cfg_param->cfg_param_04 = 0xFFFF;
	}

	roi_data[0] = ((cfg_param->cfg_param_02 & 0xFFFF) << 16) |
			(cfg_param->cfg_param_01 & 0xFFFF);
	roi_data[1] = ((cfg_param->cfg_param_04 & 0xFFFF) << 16) |
			(cfg_param->cfg_param_03 & 0xFFFF);
	roi_data[2] = ((cfg_param->cfg_param_05 & 0x1FF) << 16) |
			(cfg_param->cfg_param_06 & 0x1FF);

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(ltm_buf[LTM_ROI][idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, LTM_ROI, ltm_buf[LTM_ROI][idx]);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, 0xb0, roi_data, sizeof(u32) * 3,
			REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write roi data failed ret %d\n",
				rc);
		return;
	}

	for (i = 0; i < num_mixers; i++) {
		/* broadcast feature is not supported with REG_SINGLE_MODIFY */
		/* reset decode select to unicast */
		dma_write_cfg.blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return;
		}

		if (ltm_vlut_ops_mask[dspp_idx[i]] & ltm_vlut)
			opmode |= BIT(24);
		ltm_vlut_ops_mask[dspp_idx[i]] |= ltm_roi;

		REG_DMA_SETUP_OPS(dma_write_cfg, 0x04, &opmode, sizeof(opmode),
			REG_SINGLE_MODIFY, 0, 0, REG_DMA_LTM_ROI_OP_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("opmode write failed ret %d\n", rc);
			return;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, ltm_buf[LTM_ROI][idx],
				REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
				LTM_ROI);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}
}

static void ltm_vlutv1_disable(struct sde_hw_dspp *ctx, u32 clear)
{
	enum sde_ltm idx = 0;
	u32 opmode = 0, offset = 0;

	idx = (enum sde_ltm)ctx->idx;
	if (idx >= LTM_MAX) {
		DRM_ERROR("invalid ltm idx %d\n", ctx->idx);
		return;
	}

	offset = ctx->cap->sblk->ltm.base + 0x4;
	ltm_vlut_ops_mask[ctx->idx] &= ~ltm_vlut;
	opmode = SDE_REG_READ(&ctx->hw, offset);
	if (opmode & BIT(0))
		/* disable VLUT/INIT/ROI */
		opmode &= REG_DMA_LTM_VLUT_DISABLE_OP_MASK;
	else
		opmode &= clear;
	SDE_REG_WRITE(&ctx->hw, offset, opmode);
}

static int reg_dmav1_setup_ltm_vlutv1_common(struct sde_hw_dspp *ctx, void *cfg,
				struct sde_hw_reg_dma_ops *dma_ops,
				struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
				u32 *opmode, enum sde_ltm *dspp_idx)
{
	struct drm_msm_ltm_data *payload = NULL;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	enum sde_ltm idx = 0;
	u32 offset, crs = 0, index = 0, len = 0, blk = 0;
	u32 i = 0, num_mixers = 0;
	int rc = 0;

	idx = (enum sde_ltm)ctx->idx;
	num_mixers = hw_cfg->num_of_mixers;
	rc = reg_dmav1_get_ltm_blk(hw_cfg, idx, &dspp_idx[0], &blk);
	if (rc) {
		if (rc != -EALREADY)
			DRM_ERROR("failed to get the blk info\n");
		return -EINVAL;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_ltm_data)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_ltm_data));
		return -EINVAL;
	}

	offset = ctx->cap->sblk->ltm.base + 0x5c;
	crs = SDE_REG_READ(&ctx->hw, offset);
	if (!(crs & BIT(3))) {
		DRM_ERROR("LTM VLUT buffer is not ready: crs = %d\n", crs);
		return -EINVAL;
	}

	dma_ops->reset_reg_dma_buf(ltm_buf[LTM_VLUT][idx]);

	REG_DMA_INIT_OPS(*dma_write_cfg, blk, LTM_VLUT, ltm_buf[LTM_VLUT][idx]);
	REG_DMA_SETUP_OPS(*dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return -EINVAL;
	}

	/* write VLUT index */
	REG_DMA_SETUP_OPS(*dma_write_cfg, 0x38, &index, sizeof(u32),
				REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write VLUT index reg failed ret %d\n", rc);
		return -EINVAL;
	}

	payload = hw_cfg->payload;
	len = sizeof(u32) * LTM_DATA_SIZE_0 * LTM_DATA_SIZE_3;
	REG_DMA_SETUP_OPS(*dma_write_cfg, 0x3c, &payload->data[0][0],
			len, REG_BLK_WRITE_INC, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write VLUT data failed rc %d\n", rc);
		return -EINVAL;
	}

	for (i = 0; i < num_mixers; i++) {
		/* broadcast feature is not supported with REG_SINGLE_MODIFY */
		/* reset decode select to unicast */
		dma_write_cfg->blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(*dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			return -EINVAL;
		}

		/* set the UPDATE_REQ bit */
		crs = BIT(0);
		REG_DMA_SETUP_OPS(*dma_write_cfg, 0x5c, &crs, sizeof(u32),
				REG_SINGLE_MODIFY, 0, 0,
				REG_DMA_LTM_UPDATE_REQ_MASK);
		rc = dma_ops->setup_payload(dma_write_cfg);
		if (rc) {
			DRM_ERROR("write UPDATE_REQ failed ret %d\n", rc);
			return -EINVAL;
		}
		opmode[i] = BIT(1);
		if (ltm_vlut_ops_mask[dspp_idx[i]] & ltm_unsharp)
			opmode[i] |= BIT(4);
		if (ltm_vlut_ops_mask[dspp_idx[i]] & ltm_dither)
			opmode[i] |= BIT(6);
		if (ltm_vlut_ops_mask[dspp_idx[i]] & ltm_roi)
			opmode[i] |= BIT(24);
		ltm_vlut_ops_mask[dspp_idx[i]] |= ltm_vlut;
	}
	return 0;
}

void reg_dmav1_setup_ltm_vlutv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_ltm_phase_info phase;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 *opmode;
	u32 i = 0, num_mixers = 0;
	int rc = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	enum sde_ltm dspp_idx[LTM_MAX] = {0};
	enum sde_ltm idx = 0;

	rc = reg_dma_ltm_check(ctx, cfg, LTM_VLUT);
	if (rc)
		return;

	/* disable case */
	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("Disable LTM vlut feature\n");
		LOG_FEATURE_OFF;
		ltm_vlutv1_disable(ctx, LTM_CONFIG_MERGE_MODE_ONLY);
		return;
	}

	idx = (enum sde_ltm)ctx->idx;
	num_mixers = hw_cfg->num_of_mixers;
	opmode = kvzalloc((num_mixers * sizeof(u32)), GFP_KERNEL);
	if (!opmode)
		return;

	dma_ops = sde_reg_dma_get_ops();

	rc = reg_dmav1_setup_ltm_vlutv1_common(ctx, cfg, dma_ops,
					&dma_write_cfg, opmode, dspp_idx);
	if (rc)
		goto vlut_exit;

	sde_ltm_get_phase_info(hw_cfg, &phase);
	for (i = 0; i < num_mixers; i++) {
		dma_write_cfg.blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			goto vlut_exit;
		}

		if (phase.merge_en)
			opmode[i] |= BIT(16);
		else
			opmode[i] &= ~LTM_CONFIG_MERGE_MODE_ONLY;

		REG_DMA_SETUP_OPS(dma_write_cfg, 0x4, &opmode[i], sizeof(u32),
				REG_SINGLE_MODIFY, 0, 0,
				REG_DMA_LTM_VLUT_ENABLE_OP_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write opmode failed ret %d\n", rc);
			goto vlut_exit;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, ltm_buf[LTM_VLUT][idx],
				REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
				LTM_VLUT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
vlut_exit:
	kvfree(opmode);
}

void reg_dmav1_setup_ltm_vlutv1_2(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_ltm_phase_info phase;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 merge_mode = 0;
	u32 *opmode;
	u32 i = 0, num_mixers = 0;
	int rc = 0;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	enum sde_ltm dspp_idx[LTM_MAX] = {0};
	enum sde_ltm idx = 0;

	rc = reg_dma_ltm_check(ctx, cfg, LTM_VLUT);
	if (rc)
		return;

	/* disable case */
	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("Disable LTM vlut feature\n");
		LOG_FEATURE_OFF;
		ltm_vlutv1_disable(ctx, 0x0);
		return;
	}

	idx = (enum sde_ltm)ctx->idx;
	num_mixers = hw_cfg->num_of_mixers;
	opmode = kvzalloc((num_mixers * sizeof(u32)), GFP_KERNEL);
	if (!opmode)
		return;

	dma_ops = sde_reg_dma_get_ops();

	rc = reg_dmav1_setup_ltm_vlutv1_common(ctx, cfg, dma_ops,
					&dma_write_cfg, opmode, dspp_idx);
	if (rc)
		goto vlut_exit;

	sde_ltm_get_phase_info(hw_cfg, &phase);
	for (i = 0; i < num_mixers; i++) {
		dma_write_cfg.blk = ltm_mapping[dspp_idx[i]];
		REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0,
				0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write decode select failed ret %d\n", rc);
			goto vlut_exit;
		}

		if (phase.merge_en)
			merge_mode = BIT(0);
		else
			merge_mode = 0x0;
		REG_DMA_SETUP_OPS(dma_write_cfg, 0x18, &merge_mode, sizeof(u32),
				REG_SINGLE_MODIFY, 0, 0,
				0xFFFFFFFC);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write merge_ctrl failed ret %d\n", rc);
			goto vlut_exit;
		}

		REG_DMA_SETUP_OPS(dma_write_cfg, 0x4, &opmode[i], sizeof(u32),
				REG_SINGLE_MODIFY, 0, 0,
				REG_DMA_LTM_VLUT_ENABLE_OP_MASK);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write opmode failed ret %d\n", rc);
			goto vlut_exit;
		}
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl, ltm_buf[LTM_VLUT][idx],
				REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
				LTM_VLUT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
vlut_exit:
	kvfree(opmode);
}

int reg_dmav2_init_dspp_op_v4(int feature, enum sde_dspp idx)
{
	int rc = 0;
	struct sde_hw_reg_dma_ops *dma_ops;
	bool is_supported = false;
	u32 blk;

	if (feature >= SDE_DSPP_MAX || idx >= DSPP_MAX) {
		DRM_ERROR("invalid feature %d max %d dspp idx %d max %d\n",
				feature, SDE_DSPP_MAX, idx, DSPP_MAX);
		return -ENOTSUPP;
	}

	if (feature_map[feature] >= REG_DMA_FEATURES_MAX) {
		DRM_ERROR("invalid feature map %d for feature %d\n",
				feature_map[feature], feature);
		return -ENOTSUPP;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -ENOTSUPP;

	blk = dspp_mapping[idx];
	rc = dma_ops->check_support(feature_map[feature], blk, &is_supported);
	if (!rc)
		rc = (is_supported) ? 0 : -ENOTSUPP;

	if (is_supported)
		_reg_dma_init_dspp_feature_buf(feature, idx);

	return rc;
}

/* Attempt to submit a feature buffer to SB DMA.
 * Note that if SB DMA is not supported, this function
 * will quitely attempt to fallback to DB DMA
 */
static void _perform_sbdma_kickoff(struct sde_hw_dspp *ctx,
		struct sde_hw_cp_cfg *hw_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		u32 blk, enum sde_reg_dma_features feature)
{
	int rc, i;
	struct sde_reg_dma_kickoff_cfg kick_off;

	if ((feature != GAMUT && feature != IGC && feature != SIX_ZONE) ||
			!(blk & (DSPP0 | DSPP1 | DSPP2 | DSPP3))) {
		DRM_ERROR("SB DMA invalid for feature / block - %d/%d\n",
				feature, blk);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[feature][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE1, WRITE_IMMEDIATE,
			feature);
	kick_off.dma_type = REG_DMA_TYPE_SB;
	rc = dma_ops->kick_off(&kick_off);
	if (!rc) {
		for (i = 0; i < hw_cfg->num_of_mixers; i++) {
			if (blk & dspp_mapping[hw_cfg->dspp[i]->idx])
				hw_cfg->dspp[i]->sb_dma_in_use = true;
		}
	} else if (rc == -EOPNOTSUPP) {
		DRM_DEBUG("Falling back to dbdma, rc = %d\n", rc);

		REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
				dspp_buf[feature][ctx->idx], REG_DMA_WRITE,
				DMA_CTL_QUEUE0, WRITE_IMMEDIATE, feature);
		rc = dma_ops->kick_off(&kick_off);
		if (rc)
			DRM_ERROR("failed dbdma kick off ret %d\n", rc);
	} else {
		DRM_ERROR("failed sbdma kick off ret %d\n", rc);
	}
}

static void _dspp_igcv4_off(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc = 0;
	u32 reg = 0, num_of_mixers = 0, blk = 0;

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
			&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, IGC, dspp_buf[IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	reg = 0;
	REG_DMA_SETUP_OPS(dma_write_cfg, ctx->cap->sblk->igc.base + 0x4,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		return;
	}

	_perform_sbdma_kickoff(ctx, hw_cfg, dma_ops, blk, IGC);
}

void reg_dmav2_setup_dspp_igcv4(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_igc_lut *lut_cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc = 0, i = 0, j = 0;
	u16 *data = NULL;
	u32 len = 0, reg = 0, num_of_mixers = 0, blk = 0, transfer_size_bytes;

	rc = reg_dma_dspp_check(ctx, cfg, IGC);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable igc feature\n");
		LOG_FEATURE_OFF;
		_dspp_igcv4_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_igc_lut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_igc_lut));
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
			&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	} else if (num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
				DSPP_MAX);
		return;
	}
	lut_cfg = hw_cfg->payload;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[IGC][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, IGC, dspp_buf[IGC][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	/* 257 entries per color * 3 colors * 16 bit per LUT entry */
	len = (IGC_TBL_LEN + 1) * 3 * sizeof(u16);
	/* Data size must be aligned with word size AND LUT transfer size */
	transfer_size_bytes = LUTBUS_IGC_TRANS_SIZE * sizeof(u32);
	if (len % transfer_size_bytes)
		len = len + (transfer_size_bytes - len % transfer_size_bytes);

	data = kvzalloc(len, GFP_KERNEL);
	if (!data)
		return;

	for (i = 0, j = 0; i < IGC_TBL_LEN; i++) {
		/* c0 --> G; c1 --> B; c2 --> R */
		/* 16 bit per LUT entry and MSB aligned to allow expansion,
		 * hence, sw need to left shift 4 bits before sending to HW.
		 */
		data[j++] = (u16)(lut_cfg->c2[i] << 4);
		data[j++] = (u16)(lut_cfg->c0[i] << 4);
		data[j++] = (u16)(lut_cfg->c1[i] << 4);
	}
	data[j++] = (4095 << 4);
	data[j++] = (4095 << 4);
	data[j++] = (4095 << 4);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, (u32 *)data, len,
			REG_BLK_LUT_WRITE, 0, 0, 0);
	/* table select is only relevant to SSPP Gamut */
	dma_write_cfg.table_sel = 0;
	dma_write_cfg.block_sel = LUTBUS_BLOCK_IGC;
	dma_write_cfg.trans_size = LUTBUS_IGC_TRANS_SIZE;
	dma_write_cfg.lut_size = len / transfer_size_bytes;

	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("lut write failed ret %d\n", rc);
		goto exit;
	}

	reg = BIT(8);
	if (lut_cfg->flags & IGC_DITHER_ENABLE) {
		reg |= BIT(4);
		reg |= (lut_cfg->strength & IGC_DITHER_DATA_MASK);
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, ctx->cap->sblk->igc.base + 0x4,
			&reg, sizeof(reg), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("setting opcode failed ret %d\n", rc);
		goto exit;
	}

	LOG_FEATURE_ON;
	_perform_sbdma_kickoff(ctx, hw_cfg, dma_ops, blk, IGC);

exit:
	kvfree(data);
}

static void dspp_3d_gamutv43_off(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	int rc;
	u32 op_mode = 0, num_of_mixers, blk = 0;

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
			&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	}

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, GAMUT, dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->gamut.base,
			&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode modify single reg failed ret %d\n", rc);
		return;
	}

	_perform_sbdma_kickoff(ctx, hw_cfg, dma_ops, blk, GAMUT);
}


void reg_dmav2_setup_dspp_3d_gamutv43(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct drm_msm_3d_gamut *payload;
	int rc;
	u32 num_of_mixers, blk = 0, i, j, k = 0, len, tmp;
	u32 op_mode, scale_offset, scale_tbl_offset, transfer_size_bytes;
	u16 *data;
	u32 scale_off[GAMUT_3D_SCALE_OFF_TBL_NUM][GAMUT_3D_SCALE_OFF_SZ];

	rc = reg_dma_dspp_check(ctx, cfg, GAMUT);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable gamut feature\n");
		LOG_FEATURE_OFF;
		dspp_3d_gamutv43_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_3d_gamut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_3d_gamut));
		return;
	}

	rc = reg_dmav1_get_dspp_blk(hw_cfg, ctx->idx, &blk,
			&num_of_mixers);
	if (rc == -EINVAL) {
		DRM_ERROR("unable to determine LUTDMA DSPP blocks\n");
		return;
	} else if (rc == -EALREADY) {
		return;
	} else if (num_of_mixers > DSPP_MAX) {
		DRM_ERROR("unable to process more than %d DSPP blocks\n",
				DSPP_MAX);
		return;
	}

	/* will support 17x17x17 modes only */
	payload = hw_cfg->payload;
	if (payload->mode != GAMUT_3D_MODE_17) {
		DRM_ERROR("invalid mode %d", payload->mode);
		return;
	}

	/* Determine ctrl register configuration */
	scale_offset = GAMUT_SCALEA_OFFSET_OFF;
	op_mode = gamut_mode_17 << 2;
	if (payload->flags & GAMUT_3D_MAP_EN)
		op_mode |= GAMUT_MAP_EN;
	op_mode |= GAMUT_EN;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, blk, GAMUT, dspp_buf[GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	/* 4 tbls * 1229 entries per tbl * 3 colors * sizeof(u16) */
	len = GAMUT_3D_TBL_NUM * GAMUT_3D_MODE17_TBL_SZ * 3 * sizeof(u16);
	/* Data size must be aligned with word size AND LUT transfer size */
	transfer_size_bytes = LUTBUS_GAMUT_TRANS_SIZE * sizeof(u32);
	if (len % transfer_size_bytes)
		len = len + (transfer_size_bytes - len % transfer_size_bytes);

	data = vzalloc(len);
	if (!data)
		return;

	k = 0;
	for (j = 0; j < GAMUT_3D_MODE17_TBL_SZ; j++) {
		for (i = 0; i < GAMUT_3D_TBL_NUM; i++) {
			/* 12 bit entries, 16 bit per LUTBUS entry and MSB
			 * aligned to allow expansion, hence, sw needs to
			 * left shift 6 bits before sending to HW.
			 */
			data[k++] = (u16)(payload->col[i][j].c0 << 4);
			data[k++] = (u16)
					((payload->col[i][j].c2_c1 >> 16) << 4);
			data[k++] = (u16)((payload->col[i][j].c2_c1) << 4);
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, (u32 *)data, len,
			REG_BLK_LUT_WRITE, 0, 0, 0);
	/* table select is only relevant to SSPP Gamut */
	dma_write_cfg.table_sel = 0;
	dma_write_cfg.block_sel = LUTBUS_BLOCK_GAMUT;
	dma_write_cfg.trans_size = LUTBUS_GAMUT_TRANS_SIZE;
	dma_write_cfg.lut_size = len / transfer_size_bytes;

	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("lut write failed ret %d\n", rc);
		goto exit;
	}

	if (payload && (payload->flags & GAMUT_3D_MAP_EN)) {
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			for (j = 0; j < GAMUT_3D_SCALE_OFF_SZ; j++) {
				scale_off[i][j] = payload->scale_off[i][j];
				tmp = scale_off[i][j] & 0x1ffff000;
				scale_off[i][j] &= 0xfff;
				tmp = tmp << 3;
				scale_off[i][j] =
					tmp | scale_off[i][j];
			}
		}
	}

	if (op_mode & GAMUT_MAP_EN) {
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			scale_tbl_offset = ctx->cap->sblk->gamut.base +
					scale_offset +
					(i * GAMUT_SCALE_OFF_LEN);
			REG_DMA_SETUP_OPS(dma_write_cfg, scale_tbl_offset,
					&scale_off[i][0],
					GAMUT_SCALE_OFF_LEN,
					REG_BLK_WRITE_SINGLE, 0, 0, 0);
			rc = dma_ops->setup_payload(&dma_write_cfg);
			if (rc) {
				DRM_ERROR("write scale/off reg failed ret %d\n",
						rc);
				goto exit;
			}
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg,
			ctx->cap->sblk->gamut.base,
			&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		goto exit;
	}

	LOG_FEATURE_ON;
	_perform_sbdma_kickoff(ctx, hw_cfg, dma_ops, blk, GAMUT);

exit:
	vfree(data);
}

void reg_dmav2_setup_vig_gamutv61(struct sde_hw_pipe *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct drm_msm_3d_gamut *payload;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	int rc;
	enum sde_sspp_multirect_index idx = SDE_SSPP_RECT_0;

	u32 gamut_base = ctx->cap->sblk->gamut_blk.regdma_base;
	u32 i, j, k = 0, len, table_select = 0;
	u32 op_mode, scale_offset, scale_tbl_offset, transfer_size_bytes;
	u16 *data;

	rc = reg_dma_sspp_check(ctx, cfg, GAMUT, idx);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		DRM_DEBUG_DRIVER("disable gamut feature\n");
		/* v5 and v6 call the same off version */
		LOG_FEATURE_OFF;
		vig_gamutv5_off(ctx, cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_3d_gamut)) {
		DRM_ERROR("invalid size of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_3d_gamut));
		return;
	}

	payload = hw_cfg->payload;
	if (payload->mode != GAMUT_3D_MODE_17) {
		DRM_ERROR("invalid mode %d", payload->mode);
		return;
	}

	op_mode = SDE_REG_READ(&ctx->hw, ctx->cap->sblk->gamut_blk.base);
	op_mode = (op_mode & (BIT(5) - 1)) >> 2;
	if (op_mode == gamut_mode_17b) {
		op_mode = gamut_mode_17;
		table_select = 0;
		scale_offset = GAMUT_SCALEA_OFFSET_OFF;
	} else {
		op_mode = gamut_mode_17b;
		table_select = 1;
		scale_offset = GAMUT_SCALEB_OFFSET_OFF;
	}

	op_mode <<= 2;
	if (payload->flags & GAMUT_3D_MAP_EN)
		op_mode |= GAMUT_MAP_EN;
	op_mode |= GAMUT_EN;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(sspp_buf[idx][GAMUT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, sspp_mapping[ctx->idx], GAMUT,
			sspp_buf[idx][GAMUT][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	len = GAMUT_3D_TBL_NUM * GAMUT_3D_MODE17_TBL_SZ * 3 * sizeof(u16);
	/* Data size must be aligned with word size AND LUT transfer size */
	transfer_size_bytes = LUTBUS_GAMUT_TRANS_SIZE * sizeof(u32);
	if (len % transfer_size_bytes)
		len = len + (transfer_size_bytes - len % transfer_size_bytes);

	data = kvzalloc(len, GFP_KERNEL);
	if (!data)
		return;

	k = 0;
	for (j = 0; j < GAMUT_3D_MODE17_TBL_SZ; j++) {
		for (i = 0; i < GAMUT_3D_TBL_NUM; i++) {
			/* 10 bit entries, 16 bit per LUTBUS entry and MSB
			 * aligned to allow expansion, hence, sw needs to
			 * left shift 6 bits before sending to HW.
			 */
			data[k++] = (u16)(payload->col[i][j].c0 << 6);
			data[k++] = (u16)
					((payload->col[i][j].c2_c1 >> 16) << 6);
			data[k++] = (u16)((payload->col[i][j].c2_c1) << 6);
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, (u32 *)data, len,
			REG_BLK_LUT_WRITE, 0, 0, 0);
	dma_write_cfg.table_sel = table_select;
	dma_write_cfg.block_sel = LUTBUS_BLOCK_GAMUT;
	dma_write_cfg.trans_size = LUTBUS_GAMUT_TRANS_SIZE;
	dma_write_cfg.lut_size = len / transfer_size_bytes;

	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("lut write failed ret %d\n", rc);
		goto exit;
	}

	if (op_mode & GAMUT_MAP_EN) {
		for (i = 0; i < GAMUT_3D_SCALE_OFF_TBL_NUM; i++) {
			scale_tbl_offset = gamut_base + scale_offset +
					(i * GAMUT_SCALE_OFF_LEN);
			REG_DMA_SETUP_OPS(dma_write_cfg, scale_tbl_offset,
					&payload->scale_off[i][0],
					GAMUT_SCALE_OFF_LEN,
					REG_BLK_WRITE_SINGLE, 0, 0, 0);
			rc = dma_ops->setup_payload(&dma_write_cfg);
			if (rc) {
				DRM_ERROR("write scale/off reg failed ret %d\n",
						rc);
				goto exit;
			}
		}
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, gamut_base,
			&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("opmode write single reg failed ret %d\n", rc);
		goto exit;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			sspp_buf[idx][GAMUT][ctx->idx], REG_DMA_WRITE,
			DMA_CTL_QUEUE0, WRITE_IMMEDIATE, GAMUT);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);

exit:
	kvfree(data);
}


int reg_dmav2_init_spr_op_v1(int feature, enum sde_dspp dspp_idx)
{
	int rc = -EOPNOTSUPP;
	struct sde_hw_reg_dma_ops *dma_ops;
	bool is_supported = false;
	enum sde_reg_dma_features dma_features[2];
	u32 i, blk, buffer_size, dma_feature_cnt = 0;

	/* SPR blocks are hardwired to DSPP blocks */
	if (feature >= SDE_SPR_MAX || dspp_idx >= DSPP_MAX) {
		DRM_ERROR("invalid feature %x max %x dspp idx %x max %xd\n",
			feature, SDE_SPR_MAX, dspp_idx, DSPP_MAX);
		return rc;
	}

	dma_ops = sde_reg_dma_get_ops();
	if (IS_ERR_OR_NULL(dma_ops))
		return -EOPNOTSUPP;

	if (feature == SDE_SPR_INIT) {
		dma_features[dma_feature_cnt++] = SPR_INIT;
		dma_features[dma_feature_cnt++] = SPR_PU_CFG;
		buffer_size = SPR_INIT_MEM_SIZE;
	} else if (feature == SDE_SPR_UDC) {
		dma_features[dma_feature_cnt++] = SPR_UDC;
		buffer_size = SPR_UDC_MEM_SIZE;
	}

	rc = 0;
	blk = dspp_mapping[dspp_idx];
	for (i = 0; (i < dma_feature_cnt) && !rc; i++) {
		rc = dma_ops->check_support(dma_features[i], blk, &is_supported);
		if (!rc) {
			if (is_supported)
				rc = reg_dma_buf_init(&dspp_buf[dma_features[i]][dspp_idx],
						buffer_size);
			else
				rc = -EOPNOTSUPP;
		}
	}

	return rc;
}

static int reg_dmav1_setup_spr_cfg3_params(struct sde_hw_dspp *ctx,
		struct drm_msm_spr_init_cfg *payload,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	uint32_t reg_off, reg_cnt, base_off;
	uint32_t reg[16];
	int i, index, rc = 0;

	if (!payload->cfg3)
		return rc;

	base_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base;
	reg_cnt = 2;
	reg_off = base_off + 0x70;
	reg[0] = APPLY_MASK_AND_SHIFT(payload->cfg13[0], 10, 0) |
		APPLY_MASK_AND_SHIFT(payload->cfg13[1], 10, 10) |
		APPLY_MASK_AND_SHIFT(payload->cfg13[2], 10, 20);
	reg[1] = payload->cfg10 & REG_MASK(30);

	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, reg,
			reg_cnt * sizeof(u32), REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr cfg13 failed ret %d\n", rc);
		return rc;
	}

	reg_cnt = ARRAY_SIZE(payload->cfg15) / 2;
	reg_off = base_off + 0xA0;
	for (i = 0; i < reg_cnt; i++) {
		index = 2 * i;
		reg[i] = APPLY_MASK_AND_SHIFT(payload->cfg15[index], 12, 0) |
			APPLY_MASK_AND_SHIFT(payload->cfg15[index + 1], 12, 16);
	}

	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, reg,
			reg_cnt * sizeof(u32), REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr cfg15 failed ret %d\n", rc);
		return rc;
	}

	return rc;
}

static int reg_dmav1_setup_spr_cfg4_params(struct sde_hw_dspp *ctx,
		struct drm_msm_spr_init_cfg *payload,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	uint32_t reg_off, reg_cnt, base_off;
	uint32_t reg[16];
	int rc = 0;

	if (!payload->cfg4)
		return rc;

	reg_cnt = 3;
	base_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base;
	reg_off = base_off + 0x60;
	reg[0] = payload->cfg9 & 0x0F;
	reg[1] = APPLY_MASK_AND_SHIFT(payload->cfg12[3], 10, 0) |
		APPLY_MASK_AND_SHIFT(payload->cfg12[0], 11, 16);
	reg[2] = APPLY_MASK_AND_SHIFT(payload->cfg12[1], 11, 0) |
		APPLY_MASK_AND_SHIFT(payload->cfg12[2], 11, 16);

	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, reg,
			reg_cnt * sizeof(u32), REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr cfg12 failed ret %d\n", rc);
		return rc;
	}

	return rc;
}

static int reg_dmav1_setup_spr_cfg5_params(struct sde_hw_dspp *ctx,
		struct drm_msm_spr_init_cfg *payload,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	uint32_t i, reg[1];
	int rc = 0;

	if (!payload->cfg18_en) {
		ctx->spr_cfg_18_default = 0;
		return rc;
	}

	reg[0] = APPLY_MASK_AND_SHIFT(payload->cfg18[0], 3, 16) |
		 APPLY_MASK_AND_SHIFT(payload->cfg18[1], 3, 20) |
		 APPLY_MASK_AND_SHIFT(payload->cfg18[2], 3, 24);

	for (i = 0; i < 4; i++) {
		uint32_t val = 0;

		switch (payload->cfg18[3 + i]) {
		case 0:
			val = 0;
			break;
		case 2:
			val = 1;
			break;
		case 4:
			val = 2;
			break;
		default:
			DRM_ERROR("Invalid payload for cfg18. Val %u\n", payload->cfg18[3 + i]);
			break;
		}

		reg[0] |= APPLY_MASK_AND_SHIFT(val, 2, 4 * i);
	}

	SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->spr.base + 0x7C, reg[0]);
	ctx->spr_cfg_18_default = reg[0];

	return rc;
}

static void reg_dmav1_disable_spr(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_reg_dma_ops *dma_ops;
	uint32_t reg_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base + 0x4;
	uint32_t reg = 0;
	int rc = 0;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[SPR_INIT][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, SPR_INIT,
			dspp_buf[SPR_INIT][ctx->idx]);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("spr write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, reg_off, &reg, sizeof(u32),
			REG_SINGLE_MODIFY, 0, 0, REG_DMA_SPR_CONFIG_MASK);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr disable failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[SPR_INIT][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
			SPR_INIT);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}

	ctx->spr_cfg_18_default = 0;
}

static int reg_dmav1_setup_spr_init_common(struct sde_hw_dspp *ctx,
		struct drm_msm_spr_init_cfg *payload,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	uint32_t reg_off, reg_cnt;
	uint32_t reg[16];
	uint32_t base_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base;
	int i, index, rc = 0;

	reg_cnt = ARRAY_SIZE(payload->cfg17) / 6;
	reg_off = base_off + 0x24;
	for (i = 0; i < reg_cnt; i++) {
		index = 6 * i;
		reg[i] = APPLY_MASK_AND_SHIFT(payload->cfg17[index], 5, 0) |
			 APPLY_MASK_AND_SHIFT(payload->cfg17[index + 1], 5, 5) |
			 APPLY_MASK_AND_SHIFT(payload->cfg17[index + 2], 5, 10) |
			 APPLY_MASK_AND_SHIFT(payload->cfg17[index + 3], 5, 15) |
			 APPLY_MASK_AND_SHIFT(payload->cfg17[index + 4], 5, 20) |
			 APPLY_MASK_AND_SHIFT(payload->cfg17[index + 5], 5, 25);
	}
	reg[reg_cnt - 1] &= 0x3FF;

	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, reg,
			reg_cnt * sizeof(u32), REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr cfg17 failed ret %d\n", rc);
		return rc;
	}

	reg_cnt = ARRAY_SIZE(payload->cfg16) / 2;
	reg_off = base_off + 0x34;
	for (i = 0; i < reg_cnt; i++) {
		index = 2 * i;
		reg[i] = APPLY_MASK_AND_SHIFT(payload->cfg16[index], 11, 0) |
			 APPLY_MASK_AND_SHIFT(payload->cfg16[index + 1], 11, 16);
	}

	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, reg, reg_cnt * sizeof(u32),
			REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr cfg16 failed ret %d\n", rc);
		return rc;
	}

	rc = reg_dmav1_setup_spr_cfg3_params(ctx, payload,
			dma_write_cfg, dma_ops);
	if (rc)
		return rc;

	rc = reg_dmav1_setup_spr_cfg4_params(ctx, payload,
			dma_write_cfg, dma_ops);

	return rc;
}

static int reg_dmav1_get_spr_target(struct sde_hw_dspp *ctx, void *cfg,
		struct sde_hw_reg_dma_ops **dma_ops, uint32_t *base_off,
		struct sde_reg_dma_buffer **buffer, bool *disable)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int rc = reg_dma_dspp_check(ctx, cfg, SPR_INIT);
	if (rc) {
		return rc;
	}

	*base_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base;
	*buffer = dspp_buf[SPR_INIT][ctx->idx];
	*dma_ops = sde_reg_dma_get_ops();

	(*dma_ops)->reset_reg_dma_buf(*buffer);

	if (!hw_cfg->payload) {
		*disable = true;
	} else {
		*disable = false;
		if (hw_cfg->len != sizeof(struct drm_msm_spr_init_cfg)) {
			DRM_ERROR("invalid payload size len %d exp %zd\n", hw_cfg->len,
				  sizeof(struct drm_msm_spr_init_cfg));
			rc = -EINVAL;
		}
	}

	return rc;
}

static int reg_dmav1_setup_spr_init_kickoff(uint32_t version, uint32_t base_off,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg,
		struct drm_msm_spr_init_cfg *payload,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg)
{
	struct sde_reg_dma_kickoff_cfg kick_off;
	uint32_t reg[2];
	uint32_t reg_off;
	int rc = 0;

	if ((payload->flags & SPR_FLAG_BYPASS)) {
		reg[0] = APPLY_MASK_AND_SHIFT(payload->cfg1, 1, 1) |
			 APPLY_MASK_AND_SHIFT(payload->cfg2, 1, 2) |
			 APPLY_MASK_AND_SHIFT(payload->cfg3, 1, 24);
	} else {
		reg[0] = APPLY_MASK_AND_SHIFT(payload->cfg0, 1, 0) |
			 APPLY_MASK_AND_SHIFT(payload->cfg1, 1, 1) |
			 APPLY_MASK_AND_SHIFT(payload->cfg2, 1, 2) |
			 APPLY_MASK_AND_SHIFT(payload->cfg4, 1, 3) |
			 APPLY_MASK_AND_SHIFT(payload->cfg3, 1, 24);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg5, 4, 16);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg6, 3, 20);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg7, 2, 4);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg8, 2, 6);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg11[0], 2, 8);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg11[1], 2, 10);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg11[2], 2, 12);
		reg[0] |= APPLY_MASK_AND_SHIFT(payload->cfg11[3], 1, 14);

		if (version == 2) {
			reg[0] |= payload->cfg18_en ? (1 << 26) : 0;
			reg[0] |= payload->cfg7 & 0x4 ? (1 << 15) : 0;
		}
	}

	reg[1] = 0;
	if (hw_cfg->num_of_mixers == 2)
		reg[1] = 1;
	else if (hw_cfg->num_of_mixers == 4)
		reg[1] = 3;

	reg_off = base_off + 0x04;
	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, &reg[0], sizeof(u32),
			REG_SINGLE_MODIFY, 0, 0, REG_DMA_SPR_CONFIG_MASK);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr config pt1 failed ret %d\n", rc);
		return rc;
	}

	reg_off = base_off + 0x08;
	REG_DMA_SETUP_OPS(*dma_write_cfg, reg_off, &reg[1], sizeof(u32),
			REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr config failed ret %d\n", rc);
		return rc;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dma_write_cfg->dma_buf,
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
			SPR_INIT);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return rc;
	}

	return rc;
}

void reg_dmav1_setup_spr_init_cfgv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_spr_init_cfg *payload = NULL;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_buffer *buffer;
	uint32_t base_off;
	int rc = 0;
	bool disable = false;

	if (reg_dmav1_get_spr_target(ctx, cfg, &dma_ops, &base_off,
			&buffer, &disable))
		return;

	if (disable) {
		LOG_FEATURE_OFF;
		return reg_dmav1_disable_spr(ctx, cfg);
	}

	payload = hw_cfg->payload;
	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, SPR_INIT, buffer);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("spr write decode select failed ret %d\n", rc);
		return;
	}

	if (!(payload->flags & SPR_FLAG_BYPASS)) {
		rc = reg_dmav1_setup_spr_init_common(ctx, payload, &dma_write_cfg, dma_ops);
		if (rc)
			return;
	}

	if (!reg_dmav1_setup_spr_init_kickoff(1, base_off, dma_ops, hw_cfg,
			payload, &dma_write_cfg))
		LOG_FEATURE_ON;
}


void reg_dmav1_setup_spr_init_cfgv2(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_spr_init_cfg *payload = NULL;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_buffer *buffer;
	uint32_t base_off;
	int rc = 0;
	bool disable = false;

	if (reg_dmav1_get_spr_target(ctx, cfg, &dma_ops, &base_off, &buffer, &disable))
		return;

	if (disable) {
		LOG_FEATURE_OFF;
		return reg_dmav1_disable_spr(ctx, cfg);
	}

	payload = hw_cfg->payload;
	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, SPR_INIT, buffer);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("spr write decode select failed ret %d\n", rc);
		return;
	}

	if (!(payload->flags & SPR_FLAG_BYPASS)) {
		rc = reg_dmav1_setup_spr_init_common(ctx, payload, &dma_write_cfg, dma_ops);
		if (rc)
			return;

		rc = reg_dmav1_setup_spr_cfg5_params(ctx, payload, &dma_write_cfg, dma_ops);
		if (rc)
			return;
	} else {
		ctx->spr_cfg_18_default = 0;
	}

	if (!reg_dmav1_setup_spr_init_kickoff(2, base_off, dma_ops, hw_cfg,
			payload, &dma_write_cfg))
		LOG_FEATURE_ON;
}


#define SPR_UDC_MAX_REG_CNT 128
#define SPR_UDC_TARGET (1 << 25)
void reg_dmav1_setup_spr_udc_cfgv2(struct sde_hw_dspp *ctx, void *cfg)
{
	size_t UDC_LENGTH = sizeof(uint32_t) * SPR_UDC_PARAM_SIZE_2 / 4;
	struct drm_msm_spr_udc_cfg *payload = NULL;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_buffer *buffer;
	uint32_t base_off, reg_off, reg_cnt;
	uint32_t *reg = kvzalloc(UDC_LENGTH, GFP_KERNEL);
	int rc = 0;
	bool disable = false;

	if (reg == NULL) {
		DRM_ERROR("Unable to allocate memory for UDC mask programming\n");
		return;
	}

	if (!hw_cfg->payload) {
		disable = true;
	} else {
		disable = false;
		if (hw_cfg->len != sizeof(struct drm_msm_spr_udc_cfg)) {
			DRM_ERROR("invalid payload size len %d exp %zd\n", hw_cfg->len,
				  sizeof(struct drm_msm_spr_udc_cfg));
			goto cleanup;
		}
	}

	if (reg_dma_dspp_check(ctx, cfg, SPR_UDC))
		goto cleanup;

	dma_ops = sde_reg_dma_get_ops();
	buffer = dspp_buf[SPR_UDC][ctx->idx];
	payload = hw_cfg->payload;
	base_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base;
	dma_ops->reset_reg_dma_buf(buffer);

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, SPR_UDC, buffer);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("spr write decode select failed ret %d\n", rc);
		goto cleanup;
	}

	if (disable) {
		reg[0] = 0;
	} else {
		uint32_t lines[3];
		uint32_t index = 0;
		uint32_t i = 0, j = 0;

		for (i = 0; i < 3; i++) {
			index = i * 4;

			reg[index++] = APPLY_MASK_AND_SHIFT(payload->cfg1[j], 16, 0) |
					APPLY_MASK_AND_SHIFT(payload->cfg1[j + 1], 16, 16);
			j += 2;

			lines[i] = APPLY_MASK_AND_SHIFT(payload->cfg1[j + 1], 9, 0);
			reg[index++] =
				APPLY_MASK_AND_SHIFT(payload->cfg1[j], 10, 0) | lines[i] << 16;
			j += 2;

			reg[index++] = APPLY_MASK_AND_SHIFT(payload->cfg1[j], 4, 0) |
					APPLY_MASK_AND_SHIFT(payload->cfg1[j + 1], 4, 8) |
					APPLY_MASK_AND_SHIFT(payload->cfg1[j + 2], 4, 16) |
					APPLY_MASK_AND_SHIFT(payload->cfg1[j + 3], 4, 24);
			j += 4;
			reg[index++] = APPLY_MASK_AND_SHIFT(payload->cfg1[j++], 11, 0);
		}

		reg_off = base_off + 0x120;
		reg_cnt = index;

		REG_DMA_SETUP_OPS(dma_write_cfg, reg_off, reg,
				reg_cnt * sizeof(u32), REG_BLK_WRITE_SINGLE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write spr udc cfg p1 failed ret %d\n", rc);
			goto cleanup;
		}

		reg_off = base_off + 0x150;
		reg[0] =  0;
		REG_DMA_SETUP_OPS(dma_write_cfg, reg_off, reg, sizeof(u32),
					REG_SINGLE_WRITE, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write spr udc cfg p2 failed - ret %d\n", rc);
			goto cleanup;
		}

		memset(reg, 0, UDC_LENGTH);
		for (i = 0; i < 3; i++) {
			uint32_t line = 0;
			uint32_t index  = i * (SPR_UDC_PARAM_SIZE_2 / 3);
			uint32_t line_cnt = (lines[i] + 1) >> 1;

			if (i == 0)
				j = (SPR_UDC_PARAM_SIZE_2 / 12) * 2;
			else if (i == 1)
				j = 0;
			else
				j = (SPR_UDC_PARAM_SIZE_2 / 12);

			for (line = 0; line < line_cnt; line++) {
				reg[j++] = (payload->cfg2[index] & 0xff) |
					((payload->cfg2[index + 1]  & 0xff) << 8) |
					((payload->cfg2[index + 2]  & 0xff) << 16) |
					((payload->cfg2[index + 3]  & 0xff) << 24);
				index += 4;
			}
		}

		reg_off = base_off + 0x154;
		REG_DMA_SETUP_OPS(dma_write_cfg, reg_off, reg, UDC_LENGTH,
					REG_BLK_WRITE_INC, 0, 0, 0);
		rc = dma_ops->setup_payload(&dma_write_cfg);
		if (rc) {
			DRM_ERROR("write spr udc cfg p3 failed - L%d\t ret %d\n", i, rc);
			goto cleanup;
		}

		reg[0] = SPR_UDC_TARGET;
	}

	reg_off = base_off + 0x4;
	REG_DMA_SETUP_OPS(dma_write_cfg, reg_off, reg, sizeof(u32),
			REG_SINGLE_MODIFY, 0, 0, ~SPR_UDC_TARGET);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write spr udc config failed ret %d\n", rc);
		goto cleanup;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dma_write_cfg.dma_buf,
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, SPR_UDC);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		goto cleanup;
	}

	if (disable)
		LOG_FEATURE_OFF;
	else
		LOG_FEATURE_ON;

cleanup:
	kvfree(reg);
}

static int reg_dmav1_setup_spr_pu_common(struct sde_hw_dspp *ctx, struct sde_hw_cp_cfg *hw_cfg,
		struct msm_roi_list *roi_list,
		struct sde_hw_reg_dma_ops *dma_ops, struct sde_reg_dma_buffer *buffer)
{
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	uint32_t reg, reg_off, base_off;
	int rc = 0;

	if (!roi_list) {
		DRM_DEBUG("invalid payload of pu rects\n");
		reg = 0;
	} else {
		roi_list = hw_cfg->payload;
		if (roi_list->num_rects > 1) {
			DRM_ERROR("multiple pu regions not supported with spr\n");
			return -EINVAL;
		}

		if ((roi_list->spr_roi[0].x2 - roi_list->spr_roi[0].x1) != hw_cfg->panel_width) {
			DRM_ERROR("pu region not full width %d\n",
					(roi_list->spr_roi[0].x2 - roi_list->spr_roi[0].x1));
			return -EINVAL;
		}

		reg = APPLY_MASK_AND_SHIFT(roi_list->spr_roi[0].x1, 16, 0) |
			APPLY_MASK_AND_SHIFT(roi_list->spr_roi[0].y1, 16, 16);
	}

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, SPR_PU_CFG, buffer);
	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("spr write decode select failed ret %d\n", rc);
		return rc;
	}

	base_off = ctx->hw.blk_off + ctx->cap->sblk->spr.base;
	reg_off = base_off + 0x20;
	REG_DMA_SETUP_OPS(dma_write_cfg, reg_off, &reg,
			sizeof(__u32), REG_SINGLE_WRITE, 0, 0, 0);

	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write pu config failed ret %d\n", rc);
	}

	return rc;
}

void reg_dmav1_setup_spr_pu_cfgv1(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_buffer *buffer;
	struct msm_roi_list *roi_list = NULL;
	int rc = 0;

	rc = reg_dma_dspp_check(ctx, cfg, SPR_PU_CFG);
	if (rc)
		return;

	buffer = dspp_buf[SPR_PU_CFG][ctx->idx];
	dma_ops = sde_reg_dma_get_ops();
	if (dma_ops == NULL)
		return;

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);
	dma_ops->reset_reg_dma_buf(buffer);

	if (hw_cfg->payload && hw_cfg->len == sizeof(struct sde_drm_roi_v1))
		roi_list = hw_cfg->payload;

	rc = reg_dmav1_setup_spr_pu_common(ctx, cfg, roi_list, dma_ops, buffer);
	if (rc)
		return;

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			buffer,	REG_DMA_WRITE, DMA_CTL_QUEUE0,
			WRITE_IMMEDIATE, SPR_PU_CFG);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}
	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);
}

void reg_dmav1_setup_spr_pu_cfgv2(struct sde_hw_dspp *ctx, void *cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_buffer *buffer;
	struct msm_roi_list *roi_list = NULL;
	int rc;

	rc = reg_dma_dspp_check(ctx, cfg, SPR_PU_CFG);
	if (rc)
		return;

	buffer = dspp_buf[SPR_PU_CFG][ctx->idx];
	dma_ops = sde_reg_dma_get_ops();
	if (dma_ops == NULL)
		return;

	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);
	dma_ops->reset_reg_dma_buf(buffer);

	if (hw_cfg->payload && hw_cfg->len == sizeof(struct sde_drm_roi_v1))
		roi_list = hw_cfg->payload;


	rc = reg_dmav1_setup_spr_pu_common(ctx, cfg, roi_list, dma_ops, buffer);
	if (rc)
		return;

	if (ctx->spr_cfg_18_default != 0) {
		uint32_t reg = ctx->spr_cfg_18_default;

		//No ROI list means full screen update so apply without modification
		if (roi_list && roi_list->spr_roi[0].y1 != 0)
			reg &= 0xFFFFFFFC;

		if (roi_list && roi_list->spr_roi[0].y2 != hw_cfg->panel_height)
			reg &= 0xFFFFFFCF;

		SDE_REG_WRITE(&ctx->hw, ctx->cap->sblk->spr.base + 0x7C, reg);
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			buffer,	REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE, SPR_PU_CFG);
	rc = dma_ops->kick_off(&kick_off);
	if (rc) {
		DRM_ERROR("failed to kick off ret %d\n", rc);
		return;
	}

	SDE_EVT32(SDE_EVTLOG_FUNC_EXIT);
}

static void reg_dma_demura_off(struct sde_hw_dspp *ctx,
		struct sde_hw_cp_cfg *hw_cfg)
{
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 demura_base = ctx->cap->sblk->demura.base;
	u32 op_mode = 0;
	int rc;

	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[DEMURA_CFG][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, DEMURA_CFG,
			dspp_buf[DEMURA_CFG][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	REG_DMA_SETUP_OPS(dma_write_cfg, ctx->hw.blk_off + demura_base + 0x4,
		&op_mode, sizeof(op_mode), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("off(0x4): REG_SINGLE_WRITE failed ret %d\n", rc);
		return;
	}
	SDE_EVT32(SDE_EVTLOG_FUNC_ENTRY);
	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[DEMURA_CFG][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
			DEMURA_CFG);

	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);
}

static int __reg_dmav1_setup_demurav1_cfg0_c_params_cmn(
						struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
						struct sde_hw_reg_dma_ops *dma_ops,
						u64 *p, u32 len, u32 *temp, u32 comp_index,
						u32 demura_base)
{
	u32 i;
	int rc;

	i = ((comp_index & 0x3) << 28) | BIT(31);
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x68,
		&i, sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x68: REG_SINGLE_WRITE failed ret %d\n", rc);
		return rc;
	}

	for (i = 0; i < len; i++) {
		temp[i * 2] = p[i] & REG_MASK_ULL(32);
		temp[i * 2 + 1] = (p[i] & REG_MASK_SHIFT_ULL(10, 32)) >> 32;
		DRM_DEBUG_DRIVER("0x6c: index %d value %x\n",
				i * 2, temp[i * 2]);
		DRM_DEBUG_DRIVER("0x6c: index %d value %x\n",
				i * 2 + 1, temp[i * 2 + 1]);
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x6c,
		temp, sizeof(u64) * len, REG_BLK_WRITE_INC, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x6c: REG_BLK_WRITE_INC failed ret %d\n", rc);
		return rc;
	}
	return rc;
}

static int __reg_dmav1_setup_demurav1_cfg0_c_params(
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		u32 *temp, u32 temp_sz, u32 comp_index,
		u32 demura_base)
{
	u32 len;
	u64 *p;
	int rc;

	if (temp_sz < ARRAY_SIZE(dcfg->cfg0_param2_c0) * 8 || comp_index > 2) {
		DRM_ERROR("exp sz %zd act sz %d comp index %d\n",
			ARRAY_SIZE(dcfg->cfg0_param2_c0) * 8,
			temp_sz, comp_index);
		return -EINVAL;
	}

	memset(temp, 0x0, ARRAY_SIZE(dcfg->cfg0_param2_c0) * 8);
	if (comp_index == 0) {
		len = 1 << dcfg->c0_depth;
		p = dcfg->cfg0_param2_c0;
	} else if (comp_index == 1) {
		len = 1 << dcfg->c1_depth;
		p = dcfg->cfg0_param2_c1;
	} else {
		len = 1 << dcfg->c2_depth;
		p = dcfg->cfg0_param2_c2;
	}

	if (!len || len > 256) {
		DRM_ERROR("invalid len %d Max 256\n", len);
		return -EINVAL;
	}

	rc = __reg_dmav1_setup_demurav1_cfg0_c_params_cmn(dma_write_cfg, dma_ops, p, len,
					temp, comp_index, demura_base);
	return rc;
}

static u32 __get_offset_idx(u32 idx, u32 depth)
{
	u32 offset;

	if (depth > 8 || idx > 3 || (depth == 8 && idx > 0) || (depth == 7 && idx > 1)) {
		DRM_ERROR("invalid depth %d index %d\n", depth, idx);
		return 0;
	}

	offset = (1 << depth) * idx;
	if ((offset + (1 << depth)) > 256) {
		DRM_ERROR("invalid offset %d end %d > 256\n", offset, (offset + (1 << depth)));
		return 0;
	}

	offset = (offset << 16) | (offset << 8) | offset;
	return offset;
}

static int __reg_dmav1_setup_demurav1_cfg0(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg)
{
	u32 *temp = NULL, i, *p = NULL, shift, width, codebook_offset;
	int rc;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	if (!dcfg->cfg0_en) {
		DRM_DEBUG_DRIVER("dcfg->cfg0_en is disabled\n");
		return 0;
	}

	temp = kvzalloc(sizeof(struct drm_msm_dem_cfg), GFP_KERNEL);
	if (!temp)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(dcfg->cfg01_param0); i += 2) {
		temp[i >> 1] = (dcfg->cfg01_param0[i] & REG_MASK(12)) |
			((dcfg->cfg01_param0[i + 1] & REG_MASK(12)) << 16);
		DRM_DEBUG_DRIVER("0x1c: index %d value %x\n", i >> 1,
				temp[i >> 1]);
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x1c,
		temp, sizeof(u32) * 4, REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x1c: write err %d len %zd buffer index %d\n",
			rc, sizeof(u32) * 4, dma_write_cfg->dma_buf->index);
		goto quit;
	}

	memset(temp, 0, ARRAY_SIZE(dcfg->cfg0_param1));
	for (i = 0; i < ARRAY_SIZE(dcfg->cfg0_param1); i++) {
		p = (i < 4) ? &temp[0] : &temp[1];
		shift = (8 * i) % 32;
		*p |= (((dcfg->cfg0_param1[i] & 0x3f) & REG_MASK(6)) << shift);
		DRM_DEBUG_DRIVER("0xc: index %d value %x val %x shift %d\n",
			i, *p, (dcfg->cfg0_param1[i] & 0x3f), shift);
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0xc,
		temp, sizeof(u32) * 2, REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0xc: write err %d len %zd buffer index %d\n",
		    rc, sizeof(u32) * 2, dma_write_cfg->dma_buf->index);
		goto quit;
	}

	for (i = 0; i < ARRAY_SIZE(dcfg->cfg0_param0); i++)
		DRM_DEBUG_DRIVER("0x2c: index %d value %x\n",
			i, dcfg->cfg0_param0[i]);
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x2c,
		dcfg->cfg0_param0, ARRAY_SIZE(dcfg->cfg0_param0) * sizeof(u32),
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x2c: write err %d len %zd buf idx %d\n",
			rc, ARRAY_SIZE(dcfg->cfg0_param0) * sizeof(u32),
			dma_write_cfg->dma_buf->index);
		goto quit;
	}

	shift = 0;
	memset(temp, 0, ARRAY_SIZE(dcfg->cfg0_param3_c0) * 3 * sizeof(u32));
	for (i = 0; i < ARRAY_SIZE(dcfg->cfg0_param3_c0); i += 4) {
		temp[shift] = (dcfg->cfg0_param3_c0[i] & REG_MASK(8)) |
			(dcfg->cfg0_param3_c0[i + 1] & REG_MASK(8) << 8) |
			(dcfg->cfg0_param3_c0[i + 2] & REG_MASK(8) << 16) |
			(dcfg->cfg0_param3_c0[i + 3] & REG_MASK(8) << 23);
		temp[shift + 2] = (dcfg->cfg0_param3_c1[i] & REG_MASK(8)) |
			(dcfg->cfg0_param3_c1[i + 1] & REG_MASK(8) << 8) |
			(dcfg->cfg0_param3_c1[i + 2] & REG_MASK(8) << 16) |
			(dcfg->cfg0_param3_c1[i + 3] & REG_MASK(8) << 23);
		temp[shift + 4] = (dcfg->cfg0_param3_c2[i] & REG_MASK(8)) |
			(dcfg->cfg0_param3_c2[i + 1] & REG_MASK(8) << 8) |
			(dcfg->cfg0_param3_c2[i + 2] & REG_MASK(8) << 16) |
			(dcfg->cfg0_param3_c2[i + 3] & REG_MASK(8) << 23);
		DRM_DEBUG_DRIVER("0xb0: index %d value %x\n",
				shift,  temp[shift]);
		DRM_DEBUG_DRIVER("0xb0: index %d value %x\n",
				shift + 2,  temp[shift + 2]);
		DRM_DEBUG_DRIVER("0xb0: index %d value %x\n",
				shift + 4,  temp[shift + 4]);
		shift++;
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0xb0,
		temp, ARRAY_SIZE(dcfg->cfg0_param3_c0) * 3 * sizeof(u32),
		REG_BLK_WRITE_SINGLE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0xb0: write err %d len %zd buffer index %d\n", rc,
			ARRAY_SIZE(dcfg->cfg0_param3_c0) * 3 * sizeof(u32),
			dma_write_cfg->dma_buf->index);
		goto quit;
	}

	for (i = 0; i < 3 && !(dcfg->flags & DEMURA_SKIP_CFG0_PARAM2); i++) {
		rc = __reg_dmav1_setup_demurav1_cfg0_c_params(dma_write_cfg,
				dcfg, dma_ops, temp,
				sizeof(struct drm_msm_dem_cfg), i,
				demura_base);
		if (rc)
			goto quit;
	}

	if (!(dcfg->flags & DEMURA_SKIP_CFG0_PARAM2))
		dcfg->cfg0_param2_idx = 0;

	codebook_offset = __get_offset_idx(dcfg->cfg0_param2_idx, dcfg->c0_depth);

	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x70,
		&codebook_offset, sizeof(codebook_offset), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x70: REG_SINGLE_WRITE err %d len %zd buf idx %d\n",
			rc, sizeof(codebook_offset), dma_write_cfg->dma_buf->index);
		goto quit;
	}

	width = hw_cfg->panel_width >> ((dcfg->flags & DEMURA_FLAG_1) ? 2 : 1);
	DRM_DEBUG_DRIVER("0x80: value %x\n", width);
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x80,
		&width, sizeof(width), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x80: REG_SINGLE_WRITE err %d len %zd buf idx %d\n",
			rc, sizeof(width), dma_write_cfg->dma_buf->index);
		goto quit;
	}

	i = 0x400;
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0xe0,
		&i, sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0xe0: REG_SINGLE_WRITE err %d len %zd buf idx %d\n",
			rc, sizeof(i), dma_write_cfg->dma_buf->index);
		goto quit;
	}

quit:
	kvfree(temp);
	return rc;
}

static int __reg_dmav1_setup_demurav1_cfg1(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg)
{
	u32 temp[2], i, shift, *cfg1_data = NULL, len = 0;
	int rc;
	u32 width = 0;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	len = ARRAY_SIZE(dcfg->cfg1_param0_c0);
	cfg1_data = kvzalloc((len * sizeof(u32)), GFP_KERNEL);
	if (!cfg1_data)
		return -ENOMEM;

	DRM_DEBUG_DRIVER("dcfg->cfg1_high_idx %d dcfg->cfg1_low_idx %d\n",
				dcfg->cfg1_high_idx, dcfg->cfg1_low_idx);
	if (dcfg->cfg1_high_idx >= ARRAY_SIZE(dcfg->cfg01_param0))
		dcfg->cfg1_high_idx = ARRAY_SIZE(dcfg->cfg01_param0) - 1;

	if (dcfg->cfg1_low_idx >= ARRAY_SIZE(dcfg->cfg01_param0))
		dcfg->cfg1_low_idx = ARRAY_SIZE(dcfg->cfg01_param0) - 1;

	temp[0] = dcfg->cfg01_param0[dcfg->cfg1_high_idx];
	temp[1] = dcfg->cfg01_param0[dcfg->cfg1_low_idx];
	if (temp[0] > temp[1])
		shift = temp[0] - temp[1];
	else
		shift = 1;
	i = (1 << 22) / shift;
	DRM_DEBUG_DRIVER("0x14: value %x\n", i);

	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x14,
		&i, sizeof(i), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x14: REG_SINGLE_WRITE failed ret %d\n", rc);
		goto quit;
	}

	width = hw_cfg->panel_width;
	DRM_DEBUG_DRIVER("width for LFC calculation is %d\n", width);
	if (hw_cfg->panel_width < hw_cfg->panel_height) {
		temp[0] = (8 * (1 << 21)) / width;
		temp[1] = (16 * (1 << 21)) / hw_cfg->panel_height;
	} else {
		temp[0] = (16 * (1 << 21)) / width;
		temp[1] = (8 * (1 << 21)) / hw_cfg->panel_height;
	}
	temp[0] = (dcfg->pentile) ? ((temp[0]) | BIT(31)) : temp[0];

	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x54,
		&temp[0], sizeof(temp[0]), REG_SINGLE_WRITE, 0, 0, 0);
	DRM_DEBUG_DRIVER("0x54 value %x\n", temp[0]);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x54: REG_SINGLE_WRITE ret %d\n", rc);
		goto quit;
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x5c,
		&temp[1], sizeof(temp[1]), REG_SINGLE_WRITE, 0, 0, 0);
	DRM_DEBUG_DRIVER("0x5c value %x\n", temp[1]);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc) {
		DRM_ERROR("0x5c: REG_SINGLE_WRITE ret %d\n", rc);
		goto quit;
	}

	if (dcfg->cfg1_en) {
		cfg1_data[0] = (dcfg->cfg1_param0_c0[0] & REG_MASK(10)) |
			((dcfg->cfg1_param0_c1[0] & REG_MASK(10)) << 10) |
			((dcfg->cfg1_param0_c2[0] & REG_MASK(10)) << 20) | BIT(31);
		DRM_DEBUG_DRIVER("0x64: value %x\n", cfg1_data[0]);
		for (i = 1; i < len; i++) {
			cfg1_data[i] = (dcfg->cfg1_param0_c0[i] & REG_MASK(10)) |
				((dcfg->cfg1_param0_c1[i] & REG_MASK(10)) << 10) |
				((dcfg->cfg1_param0_c2[i] & REG_MASK(10)) << 20);
				DRM_DEBUG_DRIVER("0x64 index %d value %x\n", i,
						cfg1_data[i]);
		}
		REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x64,
			cfg1_data, len * sizeof(u32), REG_BLK_WRITE_INC, 0,
			0, 0);
		rc = dma_ops->setup_payload(dma_write_cfg);
		if (rc) {
			DRM_ERROR("lut write failed ret %d\n", rc);
			goto quit;
		}
	}

quit:
	kvfree(cfg1_data);
	return rc;
}

static int __reg_dmav1_setup_demurav1_cfg3(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	u32 temp[CFG3_PARAM01_LEN + 1];
	int rc, i;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	if (!dcfg->cfg3_en) {
		DRM_DEBUG_DRIVER("dcfg->cfg3_en is disabled\n");
		return 0;
	}

	temp[0] = dcfg->cfg3_param0_a[0] & REG_MASK(10);
	temp[0] = (dcfg->cfg3_param0_a[1] & REG_MASK(10)) << 16;
	temp[1] = dcfg->cfg3_param0_a[2] & REG_MASK(10);
	temp[1] = (dcfg->cfg3_param0_a[3] & REG_MASK(10)) << 16;
	temp[2] = dcfg->cfg3_param0_b[0] & REG_MASK(11);
	temp[2] = (dcfg->cfg3_param0_b[1] & REG_MASK(11)) << 16;
	temp[3] = dcfg->cfg3_param0_b[2] & REG_MASK(11);
	temp[3] = (dcfg->cfg3_param0_b[3] & REG_MASK(11)) << 16;
	temp[4] = (dcfg->cfg3_ab_adj) & REG_MASK(11);
	for (i = 0; i < ARRAY_SIZE(temp); i++)
		DRM_DEBUG_DRIVER("0xd0: index %i value %x\n", i, temp[i]);

	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0xd0,
		temp, ARRAY_SIZE(temp) * sizeof(u32), REG_BLK_WRITE_SINGLE, 0,
		0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc)
		DRM_ERROR("0xd0: REG_BLK_WRITE_SINGLE failed ret %d\n", rc);

	return rc;
}

static int __reg_dmav1_setup_demurav1_cfg5(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	u32 temp[CFG5_PARAM01_LEN];
	int rc, i;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	if (!dcfg->cfg5_en) {
		DRM_DEBUG_DRIVER("dcfg->cfg5_en is disabled\n");
		return 0;
	}

	temp[0] = dcfg->cfg5_param0[0] & REG_MASK(10);
	temp[0] |= (dcfg->cfg5_param0[1] & REG_MASK(10)) << 16;
	temp[1] = dcfg->cfg5_param0[2] & REG_MASK(10);
	temp[1] |= (dcfg->cfg5_param0[3] & REG_MASK(10)) << 16;
	temp[2] = dcfg->cfg5_param1[0] & REG_MASK(11);
	temp[2] |= (dcfg->cfg5_param1[1] & REG_MASK(11)) << 16;
	temp[3] = dcfg->cfg5_param1[2] & REG_MASK(11);
	temp[3] |= (dcfg->cfg5_param1[3] & REG_MASK(11)) << 16;
	for (i = 0; i < ARRAY_SIZE(temp); i++)
		DRM_DEBUG_DRIVER("0xa0: index %i value %x\n", i, temp[i]);

	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0xa0,
		temp, ARRAY_SIZE(temp) * sizeof(u32), REG_BLK_WRITE_SINGLE, 0,
		0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc)
		DRM_ERROR("0xa0: REG_BLK_WRITE_SINGLE failed ret %d\n", rc);

	return rc;
}

static bool __reg_dmav1_valid_hfc_en_cfg(struct drm_msm_dem_cfg *dcfg,
			struct sde_hw_cp_cfg *hw_cfg)
{
	u32 h, w, temp;
	if (!hw_cfg->valid_skip_blend_plane) {
		DRM_ERROR("HFC plane not set\n");
		return false;
	}

	h = hw_cfg->num_ds_enabled ? hw_cfg->panel_height : hw_cfg->displayv;
	w = hw_cfg->panel_width;
	temp = hw_cfg->panel_width / (2 * ((dcfg->flags & DEMURA_FLAG_1) ? 2 : 1));
	if (dcfg->pentile) {
		w = dcfg->c0_depth * (temp / 2) + dcfg->c1_depth * temp +
			dcfg->c2_depth * (temp / 2);
	} else {
		w = dcfg->c0_depth * temp + dcfg->c1_depth * temp + dcfg->c2_depth * temp;
	}
	if (w % 32)
		w = 32 - (w % 32) + w;
	w = 2 * (w / 32);
	w = w / (hw_cfg->num_of_mixers ? hw_cfg->num_of_mixers : 1);

	if (h != hw_cfg->skip_blend_plane_h || w != hw_cfg->skip_blend_plane_w) {
		DRM_ERROR("invalid hfc cfg exp h %d exp w %d act h %d act w %d\n",
			h, w, hw_cfg->skip_blend_plane_h, hw_cfg->skip_blend_plane_w);
		DRM_ERROR("c0_depth %d c1_depth %d c2 depth %d hw_cfg->panel_width %d\n",
			dcfg->c0_depth, dcfg->c1_depth, dcfg->c2_depth, hw_cfg->panel_width);
		return false;
	}

	if (dcfg->src_id == BIT(3) && hw_cfg->skip_blend_plane == SSPP_DMA3)
		return true;

	if (dcfg->src_id == BIT(1) && hw_cfg->skip_blend_plane == SSPP_DMA1)
		return true;

	DRM_ERROR("invalid HFC plane dcfg->src_id %d hw_cfg->skip_blend_plane %d\n",
		dcfg->src_id, hw_cfg->skip_blend_plane);
	return false;
}

static void __reg_dmav1_setup_demura_common_en(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg,
		u32 *en)
{
	bool valid_hfc_cfg = false;

	*en = (dcfg->src_id == BIT(3)) ? 0 : BIT(31);
	*en |= (dcfg->cfg1_high_idx & REG_MASK(3)) << 24;
	*en |= (dcfg->cfg1_low_idx & REG_MASK(3)) << 20;
	*en |= (dcfg->c2_depth & REG_MASK(4)) << 16;
	*en |= (dcfg->c1_depth & REG_MASK(4)) << 12;
	*en |= (dcfg->c0_depth & REG_MASK(4)) << 8;
	*en |= (dcfg->cfg3_en) ? BIT(5) : 0;
	*en |= (dcfg->cfg4_en) ? BIT(4) : 0;
	*en |= (dcfg->cfg2_en) ? BIT(3) : 0;
	if (dcfg->cfg0_en)
		valid_hfc_cfg = __reg_dmav1_valid_hfc_en_cfg(dcfg, hw_cfg);
	if (valid_hfc_cfg)
		*en |= (dcfg->cfg0_en) ? BIT(2) : 0;
	*en |= (dcfg->cfg1_en) ? BIT(1) : 0;
	DRM_DEBUG_DRIVER("demura common en %x\n", *en);
}

static int __reg_dmav1_setup_demurav1_en(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg)
{
	u32 en = 0;
	int rc;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	__reg_dmav1_setup_demura_common_en(ctx, dcfg, dma_write_cfg, dma_ops, hw_cfg, &en);

	DRM_DEBUG_DRIVER("demura v1 en 0x%x\n", en);
	SDE_EVT32(en);
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x4,
		&en, sizeof(en), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc)
		DRM_ERROR("0x4: REG_SINGLE_WRITE failed ret %d\n", rc);

	return rc;
}

static int __reg_dmav1_setup_demurav2_en(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg)
{
	u32 en = 0;
	int rc, val;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	__reg_dmav1_setup_demura_common_en(ctx, dcfg, dma_write_cfg, dma_ops, hw_cfg, &en);

	/* These are Demura V2 config flags */
	val = (dcfg->flags & DEMURA_FLAG_2) >> 2;
	if (val && val < 3)
		en |= (val & REG_MASK(2)) << 28;

	if (dcfg->flags & DEMURA_FLAG_1)
		en |= BIT(7);

	if (dcfg->flags & DEMURA_FLAG_0)
		en |= BIT(6);

	DRM_DEBUG_DRIVER("demura v2 en 0x%x\n", en);
	SDE_EVT32(en);
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x4,
		&en, sizeof(en), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc)
		DRM_ERROR("0x4: REG_SINGLE_WRITE failed ret %d\n", rc);

	return rc;
}

static int __reg_dmav1_setup_demurav1_dual_pipe(struct sde_hw_dspp *ctx,
		struct sde_hw_cp_cfg *hw_cfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops)
{
	struct sde_hw_dspp *dspp;
	u32 temp;
	int rc;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	dspp = hw_cfg->dspp[0];

	if (dspp->idx == ctx->idx) {
		temp = 0;
	} else {
		if (hw_cfg->panel_width < hw_cfg->panel_height)
			temp = (8 * (1 << 21)) / hw_cfg->panel_width;
		else
			temp = (16 * (1 << 21)) / hw_cfg->panel_width;

		temp = temp * (hw_cfg->panel_width >> 1);
	}
	REG_DMA_SETUP_OPS(*dma_write_cfg, demura_base + 0x58,
		&temp, sizeof(temp), REG_SINGLE_WRITE, 0, 0, 0);
	rc = dma_ops->setup_payload(dma_write_cfg);
	if (rc)
		DRM_ERROR("0x58: REG_SINGLE_WRITE failed ret %d\n", rc);
	SDE_EVT32(0x58, temp, ctx->idx);

	return rc;
}

static int __reg_dmav1_setup_demura_cfg_common(struct sde_hw_dspp *ctx,
		struct drm_msm_dem_cfg *dcfg,
		struct sde_reg_dma_setup_ops_cfg *dma_write_cfg,
		struct sde_hw_reg_dma_ops *dma_ops,
		struct sde_hw_cp_cfg *hw_cfg)
{
	int rc = 0;

	rc = __reg_dmav1_setup_demurav1_cfg0(ctx, dcfg, dma_write_cfg,
			dma_ops, hw_cfg);
	if (rc) {
		DRM_ERROR("failed setup_demurav1_cfg0 rc %d", rc);
		return rc;
	}
	rc = __reg_dmav1_setup_demurav1_cfg1(ctx, dcfg, dma_write_cfg,
			dma_ops, hw_cfg);
	if (rc) {
		DRM_ERROR("failed setup_demurav1_cfg1 rc %d", rc);
		return rc;
	}

	rc = __reg_dmav1_setup_demurav1_cfg3(ctx, dcfg, dma_write_cfg,
		dma_ops);
	if (rc) {
		DRM_ERROR("failed setup_demurav1_cfg3 rc %d", rc);
		return rc;
	}

	rc = __reg_dmav1_setup_demurav1_cfg5(ctx, dcfg, dma_write_cfg,
		dma_ops);
	if (rc) {
		DRM_ERROR("failed setup_demurav1_cfg5 rc %d", rc);
		return rc;
	}
	rc = __reg_dmav1_setup_demurav1_dual_pipe(ctx, hw_cfg, dma_write_cfg,
		dma_ops);
	if (rc) {
		DRM_ERROR("failed setup_demurav1_dual_pipe rc %d", rc);
		return rc;
	}

	return rc;
}

void reg_dmav1_setup_demurav1(struct sde_hw_dspp *ctx, void *cfx)
{
	struct drm_msm_dem_cfg *dcfg;
	struct sde_hw_cp_cfg *hw_cfg = cfx;
	int rc = 0;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;

	rc = reg_dma_dspp_check(ctx, cfx, DEMURA_CFG);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		LOG_FEATURE_OFF;
		reg_dma_demura_off(ctx, hw_cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_dem_cfg)) {
		DRM_ERROR("invalid sz of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_dem_cfg));
	}
	dcfg = hw_cfg->payload;
	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[DEMURA_CFG][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, DEMURA_CFG,
			dspp_buf[DEMURA_CFG][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	rc = __reg_dmav1_setup_demura_cfg_common(ctx, dcfg, &dma_write_cfg, dma_ops, hw_cfg);
	if (rc) {
		DRM_ERROR("failed to setup_demurav1_cfg rc %d", rc);
		return;
	}

	rc = __reg_dmav1_setup_demurav1_en(ctx, dcfg, &dma_write_cfg, dma_ops, hw_cfg);
	if (rc) {
		DRM_ERROR("failed setup_demurav1_en rc %d", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[DEMURA_CFG][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
			DEMURA_CFG);

	DRM_DEBUG_DRIVER("enable demura v1 buffer size %d\n",
				dspp_buf[DEMURA_CFG][ctx->idx]->index);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off demurav1 ret %d\n", rc);
}

void reg_dmav1_setup_demurav2(struct sde_hw_dspp *ctx, void *cfx)
{
	int rc = 0;
	struct drm_msm_dem_cfg *dcfg;
	struct sde_hw_cp_cfg *hw_cfg = cfx;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;

	rc = reg_dma_dspp_check(ctx, cfx, DEMURA_CFG);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		LOG_FEATURE_OFF;
		reg_dma_demura_off(ctx, hw_cfg);
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_dem_cfg)) {
		DRM_ERROR("invalid sz of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_dem_cfg));
	}
	dcfg = hw_cfg->payload;
	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[DEMURA_CFG][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, DEMURA_CFG,
			dspp_buf[DEMURA_CFG][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}

	rc = __reg_dmav1_setup_demura_cfg_common(ctx, dcfg, &dma_write_cfg, dma_ops, hw_cfg);
	if (rc) {
		DRM_ERROR("failed to setup_demurav2_cfg rc %d", rc);
		return;
	}

	rc = __reg_dmav1_setup_demurav2_en(ctx, dcfg, &dma_write_cfg, dma_ops, hw_cfg);
	if (rc) {
		DRM_ERROR("failed setup_demurav2_en rc %d", rc);
		return;
	}

	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[DEMURA_CFG][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
			DEMURA_CFG);

	DRM_DEBUG_DRIVER("enable demura v2 buffer size %d\n",
				dspp_buf[DEMURA_CFG][ctx->idx]->index);
	LOG_FEATURE_ON;
	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off demurav2 ret %d\n", rc);
}

void reg_dmav1_setup_demura_cfg0_param2(struct sde_hw_dspp *ctx, void *cfg)
{
	struct drm_msm_dem_cfg0_param2 *dcfg;
	struct sde_hw_cp_cfg *hw_cfg = cfg;
	int rc = 0;
	struct sde_hw_reg_dma_ops *dma_ops;
	struct sde_reg_dma_setup_ops_cfg dma_write_cfg;
	struct sde_reg_dma_kickoff_cfg kick_off;
	u32 *temp, i, len;
	u64 *p;
	u32 demura_base = ctx->cap->sblk->demura.base + ctx->hw.blk_off;

	rc = reg_dma_dspp_check(ctx, cfg, DEMURA_CFG0_PARAM2);
	if (rc)
		return;

	if (!hw_cfg->payload) {
		LOG_FEATURE_OFF;
		return;
	}

	if (hw_cfg->len != sizeof(struct drm_msm_dem_cfg0_param2)) {
		DRM_ERROR("invalid sz of payload len %d exp %zd\n",
				hw_cfg->len, sizeof(struct drm_msm_dem_cfg0_param2));
	}
	dcfg = hw_cfg->payload;
	dma_ops = sde_reg_dma_get_ops();
	dma_ops->reset_reg_dma_buf(dspp_buf[DEMURA_CFG0_PARAM2][ctx->idx]);

	REG_DMA_INIT_OPS(dma_write_cfg, MDSS, DEMURA_CFG0_PARAM2,
			dspp_buf[DEMURA_CFG0_PARAM2][ctx->idx]);

	REG_DMA_SETUP_OPS(dma_write_cfg, 0, NULL, 0, HW_BLK_SELECT, 0, 0, 0);
	rc = dma_ops->setup_payload(&dma_write_cfg);
	if (rc) {
		DRM_ERROR("write decode select failed ret %d\n", rc);
		return;
	}
	temp = kvzalloc(sizeof(struct drm_msm_dem_cfg0_param2), GFP_KERNEL);
	if (!temp)
		return;
	len = dcfg->cfg0_param2_len;
	for (i = 0; i < 3; i++) {
		if (!i)
			p = dcfg->cfg0_param2_c0;
		else if (i == 1)
			p = dcfg->cfg0_param2_c1;
		else if (i == 2)
			p = dcfg->cfg0_param2_c2;
		__reg_dmav1_setup_demurav1_cfg0_c_params_cmn(&dma_write_cfg, dma_ops,
						p, len, temp, i, demura_base);
	}
	REG_DMA_SETUP_KICKOFF(kick_off, hw_cfg->ctl,
			dspp_buf[DEMURA_CFG0_PARAM2][ctx->idx],
			REG_DMA_WRITE, DMA_CTL_QUEUE0, WRITE_IMMEDIATE,
			DEMURA_CFG0_PARAM2);

	rc = dma_ops->kick_off(&kick_off);
	if (rc)
		DRM_ERROR("failed to kick off ret %d\n", rc);

	LOG_FEATURE_ON;
	kvfree(temp);
}
