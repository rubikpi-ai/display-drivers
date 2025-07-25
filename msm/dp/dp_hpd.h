/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_HPD_H_
#define _DP_HPD_H_

#include <linux/types.h>
#include "dp_parser.h"
#include "dp_catalog.h"
#include "dp_aux_bridge.h"

struct device;

/**
 * enum dp_hpd_plug_orientation - plug orientation
 * @ORIENTATION_NONE:	Undefined or unspecified
 * @ORIENTATION_CC1:	CC1
 * @ORIENTATION_CC2:	CC2
 */
enum dp_hpd_plug_orientation {
	ORIENTATION_NONE,
	ORIENTATION_CC1,
	ORIENTATION_CC2,
};

/**
 * enum dp_hpd_type - dp hpd type
 * @DP_HPD_ALTMODE: AltMode over G-Link based HPD
 * @DP_HPD_USBPD:   USB type-c based HPD
 * @DP_HPD_GPIO:    GPIO based HPD
 * @DP_HPD_LPHW:    LPHW based HPD
 * @DP_HPD_BRIDGE:  External bridge HPD
 */

enum dp_hpd_type {
	DP_HPD_ALTMODE,
	DP_HPD_USBPD,
	DP_HPD_GPIO,
	DP_HPD_LPHW,
	DP_HPD_BRIDGE,
};

/**
 * struct dp_hpd_cb - callback functions provided by the client
 *
 * @configure: called when dp connection is ready.
 * @disconnect: notify the cable disconnect event.
 * @attention: notify any attention message event.
 */
struct dp_hpd_cb {
	int (*configure)(struct device *dev);
	int (*disconnect)(struct device *dev);
	int (*attention)(struct device *dev);
};

/**
 * struct dp_hpd - DisplayPort HPD status
 *
 * @type: type of HPD
 * @orientation: plug orientation configuration, USBPD type only.
 * @hpd_high: Hot Plug Detect signal is high.
 * @hpd_irq: Change in the status since last message
 * @alt_mode_cfg_done: bool to specify alt mode status
 * @multi_func: multi-function preferred, USBPD type only
 * @peer_usb_com: downstream supports usb data communication
 * @force_multi_func: force multi-function preferred
 * @flip_lanes: DP lanes selection in case of force_multi_func
 * @isr: event interrupt, BUILTIN and LPHW type only
 * @register_hpd: register hardware callback
 * @host_init: source or host side setup for hpd
 * @host_deinit: source or host side de-initializations
 * @simulate_connect: simulate disconnect or connect for debug mode
 * @simulate_attention: simulate attention messages for debug mode
 * @wakeup_phy: wakeup USBPD phy layer
 */
struct dp_hpd {
	enum dp_hpd_type type;
	u32 orientation;
	bool hpd_high;
	bool hpd_irq;
	bool alt_mode_cfg_done;
	bool multi_func;
	bool peer_usb_comm;
	bool force_multi_func;
	bool flip_lanes;

	void (*isr)(struct dp_hpd *dp_hpd);
	int (*register_hpd)(struct dp_hpd *dp_hpd);
	void (*host_init)(struct dp_hpd *hpd, struct dp_catalog_hpd *catalog);
	void (*host_deinit)(struct dp_hpd *hpd, struct dp_catalog_hpd *catalog);
	int (*simulate_connect)(struct dp_hpd *dp_hpd, bool hpd);
	int (*simulate_attention)(struct dp_hpd *dp_hpd, int vdo);
	void (*wakeup_phy)(struct dp_hpd *dp_hpd, bool wakeup);
};

/**
 * dp_hpd_get() - configure and get the DisplayPlot HPD module data
 *
 * @dev: device instance of the caller
 * @parser: pointer to DP parser module
 * @catalog: pointer to DP catalog module
 * @aux_bridge: handle for aux_bridge driver data
 * @cb: callback function for HPD response
 * return: pointer to allocated hpd module data
 *
 * This function sets up the hpd module
 */
struct dp_hpd *dp_hpd_get(struct device *dev, struct dp_parser *parser,
		struct dp_catalog_hpd *catalog,
		struct dp_aux_bridge *aux_bridge,
		struct dp_hpd_cb *cb);

/**
 * dp_hpd_put()
 *
 * Cleans up dp_hpd instance
 *
 * @dp_hpd: instance of dp_hpd
 */
void dp_hpd_put(struct dp_hpd *dp_hpd);

#endif /* _DP_HPD_H_ */
