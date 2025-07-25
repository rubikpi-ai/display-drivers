/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022, 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2019, 2021 The Linux Foundation. All rights reserved.
 */

#include "sde_hw_mdss.h"

#define RGB_10BIT_FMTS	{DRM_FORMAT_BGRA1010102, 0}, \
	{DRM_FORMAT_BGRX1010102, 0}, \
	{DRM_FORMAT_RGBA1010102, 0}, \
	{DRM_FORMAT_RGBX1010102, 0}, \
	{DRM_FORMAT_ABGR2101010, 0}, \
	{DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED}, \
	{DRM_FORMAT_XBGR2101010, 0}, \
	{DRM_FORMAT_XBGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED}, \
	{DRM_FORMAT_ARGB2101010, 0}, \
	{DRM_FORMAT_XRGB2101010, 0}

#define RGB_FMTS	{DRM_FORMAT_ARGB8888, 0}, \
	{DRM_FORMAT_ABGR8888, 0}, \
	{DRM_FORMAT_RGBA8888, 0}, \
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED}, \
	{DRM_FORMAT_BGRA8888, 0}, \
	{DRM_FORMAT_XRGB8888, 0}, \
	{DRM_FORMAT_RGBX8888, 0}, \
	{DRM_FORMAT_BGRX8888, 0}, \
	{DRM_FORMAT_XBGR8888, 0}, \
	{DRM_FORMAT_XBGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED}, \
	{DRM_FORMAT_RGB888, 0}, \
	{DRM_FORMAT_BGR888, 0}, \
	{DRM_FORMAT_RGB565, 0}, \
	{DRM_FORMAT_BGR565, DRM_FORMAT_MOD_QCOM_COMPRESSED}, \
	{DRM_FORMAT_BGR565, 0}, \
	{DRM_FORMAT_ARGB1555, 0}, \
	{DRM_FORMAT_ABGR1555, 0}, \
	{DRM_FORMAT_RGBA5551, 0}, \
	{DRM_FORMAT_BGRA5551, 0}, \
	{DRM_FORMAT_XRGB1555, 0}, \
	{DRM_FORMAT_XBGR1555, 0}, \
	{DRM_FORMAT_RGBX5551, 0}, \
	{DRM_FORMAT_BGRX5551, 0}, \
	{DRM_FORMAT_ARGB4444, 0}, \
	{DRM_FORMAT_ABGR4444, 0}, \
	{DRM_FORMAT_RGBA4444, 0}, \
	{DRM_FORMAT_BGRA4444, 0}, \
	{DRM_FORMAT_XRGB4444, 0}, \
	{DRM_FORMAT_XBGR4444, 0}, \
	{DRM_FORMAT_RGBX4444, 0}, \
	{DRM_FORMAT_BGRX4444, 0}

#define TP10_UBWC_FMTS	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_COMPRESSED | \
		DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT}

#define P010_FMTS	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_DX}

#define P010_UBWC_FMTS	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_DX | \
		DRM_FORMAT_MOD_QCOM_COMPRESSED}

#define SDE_IS_IN_ROT_RESTRICTED_FMT(catalog, fmt) (catalog ? \
		(sde_format_validate_fmt(NULL, fmt, \
		catalog->inline_rot_restricted_formats) == 0) : false)

static const uint64_t supported_format_modifiers[] = {
	DRM_FORMAT_MOD_QCOM_COMPRESSED,
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static const struct sde_format_extended plane_formats[] = {
	RGB_FMTS,
	RGB_10BIT_FMTS,
	{0, 0},
};

static const struct sde_format_extended plane_formats_vig[] = {
	RGB_FMTS,

	{DRM_FORMAT_NV12, 0},
	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_NV21, 0},
	{DRM_FORMAT_VYUY, 0},
	{DRM_FORMAT_UYVY, 0},
	{DRM_FORMAT_YUYV, 0},
	{DRM_FORMAT_YVYU, 0},
	{DRM_FORMAT_YUV420, 0},
	{DRM_FORMAT_YVU420, 0},

	RGB_10BIT_FMTS,
	TP10_UBWC_FMTS,
	P010_FMTS,

	{0, 0},
};

static const struct sde_format_extended wb2_formats[] = {
	{DRM_FORMAT_RGB565, 0},
	{DRM_FORMAT_BGR565, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_RGB888, 0},
	{DRM_FORMAT_ARGB8888, 0},
	{DRM_FORMAT_RGBA8888, 0},
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_XBGR8888, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_XBGR2101010, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_XRGB8888, 0},
	{DRM_FORMAT_RGBX8888, 0},
	{DRM_FORMAT_XBGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_ARGB1555, 0},
	{DRM_FORMAT_RGBA5551, 0},
	{DRM_FORMAT_XRGB1555, 0},
	{DRM_FORMAT_RGBX5551, 0},
	{DRM_FORMAT_ARGB4444, 0},
	{DRM_FORMAT_RGBA4444, 0},
	{DRM_FORMAT_RGBX4444, 0},
	{DRM_FORMAT_XRGB4444, 0},

	{DRM_FORMAT_BGR565, 0},
	{DRM_FORMAT_BGR888, 0},
	{DRM_FORMAT_ABGR8888, 0},
	{DRM_FORMAT_BGRA8888, 0},
	{DRM_FORMAT_BGRX8888, 0},
	{DRM_FORMAT_XBGR8888, 0},
	{DRM_FORMAT_ABGR1555, 0},
	{DRM_FORMAT_BGRA5551, 0},
	{DRM_FORMAT_XBGR1555, 0},
	{DRM_FORMAT_BGRX5551, 0},
	{DRM_FORMAT_ABGR4444, 0},
	{DRM_FORMAT_BGRA4444, 0},
	{DRM_FORMAT_BGRX4444, 0},
	{DRM_FORMAT_XBGR4444, 0},

	{DRM_FORMAT_YUV420, 0},
	{DRM_FORMAT_NV12, 0},
	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_COMPRESSED},

	RGB_10BIT_FMTS,
	TP10_UBWC_FMTS,

	{0, 0},
};

static const struct sde_format_extended wb_rot_formats[] = {
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_XBGR8888, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_TILE},
	{DRM_FORMAT_XBGR2101010, DRM_FORMAT_MOD_QCOM_TILE},
	{0, 0},
};

static const struct sde_format_extended p010_ubwc_formats[] = {
	P010_UBWC_FMTS,
};

static const struct sde_format_extended true_inline_rot_v1_fmts[] = {
	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	TP10_UBWC_FMTS,
	{0, 0},
};

static const struct sde_format_extended true_inline_rot_v2_fmts[] = {
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	TP10_UBWC_FMTS,
	P010_UBWC_FMTS,
	{0, 0},
};

static const struct sde_format_extended fp16_formats[] = {
	{DRM_FORMAT_ARGB16161616F, 0},
	{DRM_FORMAT_ARGB16161616F, DRM_FORMAT_MOD_QCOM_ALPHA_SWAP},
	{DRM_FORMAT_ABGR16161616F, 0},
	{DRM_FORMAT_ABGR16161616F, DRM_FORMAT_MOD_QCOM_ALPHA_SWAP},
	{DRM_FORMAT_ABGR16161616F, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{0, 0}
};

static const struct sde_format_extended true_inline_rot_v201_fmts[] = {
	{DRM_FORMAT_NV12, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	TP10_UBWC_FMTS,
	P010_UBWC_FMTS,
	{DRM_FORMAT_ABGR16161616F, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{0, 0},
};

static const struct sde_format_extended true_inline_rot_v201_restricted_fmts[] = {
	{DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{DRM_FORMAT_ABGR2101010, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	P010_UBWC_FMTS,
	{DRM_FORMAT_ABGR16161616F, DRM_FORMAT_MOD_QCOM_COMPRESSED},
	{0, 0},
};

static const struct sde_dnsc_blur_filter_info dnsc_blur_v100_filters[] = {
	{DNSC_BLUR_GAUS_FILTER, 16, 8192, 1, 8192, 8, 64, 0, 7, {8, 12, 16, 24, 32, 48, 64}},
	{DNSC_BLUR_PCMN_FILTER, 16, 8192, 1, 8192, 1, 128, 1, 0, {0}},
};

