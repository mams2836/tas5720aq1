/*
 * tas5720.h - ALSA SoC Texas Instruments TAS5720 Mono Audio Amplifier
 *
 * Copyright (C)2015-2016 Texas Instruments Incorporated -  http://www.ti.com
 *
 * Author: Andreas Dannenberg <dannenberg@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __TAS5720_H__
#define __TAS5720_H__

/* Register Address Map */
#define TAS5720AQ1_DEVICE_ID_REG		0x00
#define TAS5720AQ1_POWER_CTRL_REG		0x01
#define TAS5720AQ1_DIGITAL_CTRL1_REG	0x02
#define TAS5720AQ1_VOLUME_CTRL_CFG_REG  0x03
#define TAS5720AQ1_LEFT_CHANNEL_VOLUME_CTRL     0x04
#define TAS5720AQ1_RIGHT_CHANNEL_VOLUME_CTRL    0x05
#define TAS5720AQ_ANALOG_CTRL_REG		    0x06
#define TAS5720AQ1_FAULT_REG		        0x08
#define TAS5720AQ1_DIGITAL_CLIP2_REG	    0x10
#define TAS5720AQ1_DIGITAL_CLIP1_REG	    0x11
#define TAS5720AQ1_MAX_REG			TAS5720_DIGITAL_CLIP1_REG

/* TAS5720AQ1_DEVICE_ID_REG */
#define TAS5720AQ1_DEVICE_ID		0x00

/* TAS5720AQ1_POWER_CTRL_REG */
#define TAS5720AQ1_DIG_CLIP_MASK		GENMASK(7, 2)
#define TAS5720AQ1_SLEEP			BIT(1)
#define TAS5720AQ1_SDZ			BIT(0)

/* TAS5720AQ1_DIGITAL_CTRL1_REG */
#define TAS5720AQ1_HPF_BYPASS		BIT(7)
#define TAS5720AQ1_DIGBOOST_MASK    GENMASK(5, 4)
#define TAS5720AQ1_DIGBOOST_0DB     (0x00)
#define TAS5720AQ1_DIGBOOST_6DB     (0x01)
#define TAS5720AQ1_DIGBOOST_12DB    (0x02)
#define TAS5720AQ1_DIGBOOST_18DB    (0x03)
#define TAS5720AQ1_SSZ_DS			BIT(3)
#define TAS5720AQ1_SAIF_RIGHTJ_24BIT	(0x0)
#define TAS5720AQ1_SAIF_RIGHTJ_20BIT	(0x1)
#define TAS5720AQ1_SAIF_RIGHTJ_18BIT	(0x2)
#define TAS5720AQ1_SAIF_RIGHTJ_16BIT	(0x3)
#define TAS5720AQ1_SAIF_I2S		(0x4)
#define TAS5720AQ1_SAIF_LEFTJ		(0x5)
#define TAS5720AQ1_SAIF_FORMAT_MASK	GENMASK(2, 0)

/* TAS5720AQ1_VOLUME_CTRL_CFG_REG */
#define TAS5720AQ1_FADE BIT(7)
#define TAS5720AQ1_MUTE_R BIT(1)
#define TAS5720AQ1_MUTE_L BIT(0)

/* TAS5720AQ1_DIGITAL_CTRL2_REG */
#define TAS5720AQ1_MUTE			BIT(4)
#define TAS5720AQ1_TDM_SLOT_SEL_MASK	GENMASK(2, 0)

/* TAS5720AQ1_ANALOG_CTRL_REG */
#define TAS5720AQ1_PWM_RATE_6_3_FSYNC	(0x0 << 4)
#define TAS5720AQ1_PWM_RATE_8_4_FSYNC	(0x1 << 4)
#define TAS5720AQ1_PWM_RATE_10_5_FSYNC	(0x2 << 4)
#define TAS5720AQ1_PWM_RATE_12_6_FSYNC	(0x3 << 4)
#define TAS5720AQ1_PWM_RATE_14_7_FSYNC	(0x4 << 4)
#define TAS5720AQ1_PWM_RATE_16_8_FSYNC	(0x5 << 4)
#define TAS5720AQ1_PWM_RATE_20_10_FSYNC	(0x6 << 4)
#define TAS5720AQ1_PWM_RATE_24_12_FSYNC	(0x7 << 4)
#define TAS5720AQ1_PWM_RATE_MASK		GENMASK(6, 4)
#define TAS5720AQ1_ANALOG_GAIN_19_2DBV	(0x0 << 2)
#define TAS5720AQ1_ANALOG_GAIN_20_7DBV	(0x1 << 2)
#define TAS5720AQ1_ANALOG_GAIN_23_5DBV	(0x2 << 2)
#define TAS5720AQ1_ANALOG_GAIN_26_3DBV	(0x3 << 2)
#define TAS5720AQ1_ANALOG_GAIN_MASK	GENMASK(3, 2)
#define TAS5720AQ1_ANALOG_GAIN_SHIFT	(0x2)
#define TAS5720AQ1_ANAALOG_CH_SEL BIT(1)


/* TAS5720AQ1_FAULT_REG */
#define TAS5720AQ1_OC_THRESH_100PCT	(0x0 << 4)
#define TAS5720AQ1_OC_THRESH_75PCT		(0x1 << 4)
#define TAS5720AQ1_OC_THRESH_50PCT		(0x2 << 4)
#define TAS5720AQ1_OC_THRESH_25PCT		(0x3 << 4)
#define TAS5720AQ1_OC_THRESH_MASK		GENMASK(5, 4)
#define TAS5720AQ1_CLKE			BIT(3)
#define TAS5720AQ1_OCE			BIT(2)
#define TAS5720AQ1_DCE			BIT(1)
#define TAS5720AQ1_OTE			BIT(0)
#define TAS5720AQ1_FAULT_MASK		GENMASK(3, 0)

/* TAS5720AQ1_DIGITAL_CLIP2_REG */
#define TAS5720AQ1_CLIP2_MASK		GENMASK(7, 0)

/* TAS5720AQ1_DIGITAL_CLIP1_REG */
#define TAS5720AQ1_CLIP1_MASK		GENMASK(7, 2)
#define TAS5720AQ1_CLIP1_SHIFT		(0x2)

#endif /* __TAS5720_H__ */