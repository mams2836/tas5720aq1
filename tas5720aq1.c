/*
 * tas5720aq1.c - ALSA SoC Texas Instruments TAS5720 Closed Loop Automovie Audio Amplifier
 *
 * Author: 
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "tas5720.h"

/* Define how often to check (and clear) the fault status register (in ms) */
#define TAS5720_FAULT_CHECK_INTERVAL		200

struct tas5720_data {
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	struct i2c_client *tas5720_client;
	struct delayed_work fault_check_work;
	struct delayed_work dump_register_work;
	unsigned int last_fault;
};

static int tas5720aq1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int rate = params_rate(params);
	bool ssz_ds;
	int ret;

	switch (rate) {
	case 44100:
	case 48000:
		ssz_ds = false;
		break;
	case 88200:
	case 96000:
		ssz_ds = true;
		break;
	default:
		dev_err(codec->dev, "unsupported sample rate: %u\n", rate);
		return -EINVAL;
	}

	ret = snd_soc_update_bits(codec, TAS5720AQ1_DIGITAL_CTRL1_REG,
				  TAS5720AQ1_SSZ_DS, ssz_ds);
	if (ret < 0) {
		dev_err(codec->dev, "error setting sample rate: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5720aq1_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	u8 serial_format;
	int ret;

	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
		dev_vdbg(codec->dev, "DAI Format master is not found\n");
		return -EINVAL;
	}

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		       SND_SOC_DAIFMT_INV_MASK)) {
	case (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF):
		/* 1st data bit occur one BCLK cycle after the frame sync */
		serial_format = TAS5720AQ1_SAIF_I2S;
		break;
	case (SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF):
		/*
		 * Note that although the TAS5720 does not have a dedicated DSP
		 * mode it doesn't care about the LRCLK duty cycle during TDM
		 * operation. Therefore we can use the device's I2S mode with
		 * its delaying of the 1st data bit to receive DSP_A formatted
		 * data. See device datasheet for additional details.
		 */
		serial_format = TAS5720AQ1_SAIF_I2S;
		break;
	case (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF):
		/*
		 * Similar to DSP_A, we can use the fact that the TAS5720 does
		 * not care about the LRCLK duty cycle during TDM to receive
		 * DSP_B formatted data in LEFTJ mode (no delaying of the 1st
		 * data bit).
		 */
		serial_format = TAS5720AQ1_SAIF_LEFTJ;
		break;
	case (SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_NB_NF):
		/* No delay after the frame sync */
		serial_format = TAS5720AQ1_SAIF_LEFTJ;
		break;
	default:
		dev_vdbg(codec->dev, "DAI Format is not found\n");
		return -EINVAL;
	}

	ret = snd_soc_update_bits(codec, TAS5720AQ1_DIGITAL_CTRL1_REG,
				  TAS5720AQ1_SAIF_FORMAT_MASK,
				  serial_format);
	if (ret < 0) {
		dev_err(codec->dev, "error setting SAIF format: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tas5720aq1_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret;
	u16 regValue = TAS5720AQ1_MUTE_R | TAS5720AQ1_MUTE_L;
	ret = snd_soc_update_bits(codec, TAS5720AQ1_VOLUME_CTRL_CFG_REG,
				  regValue , mute ? regValue : 0);
	if (ret < 0) {
		dev_err(codec->dev, "error (un-)muting device: %d\n", ret);
		return ret;
	}
	return 0;
}

static void tas5720_fault_check_work(struct work_struct *work)
{
	struct tas5720_data *tas5720 = container_of(work, struct tas5720_data,
			fault_check_work.work);
	struct device *dev = tas5720->codec->dev;
	unsigned int curr_fault;
	int ret;

	ret = regmap_read(tas5720->regmap, TAS5720AQ1_FAULT_REG, &curr_fault);
	if (ret < 0) {
		dev_err(dev, "failed to read FAULT register: %d\n", ret);
		goto out;
	}

	/* Check/handle all errors except SAIF clock errors */
	curr_fault &= TAS5720AQ1_OCE | TAS5720AQ1_DCE | TAS5720AQ1_OTE;

	/*
	 * Only flag errors once for a given occurrence. This is needed as
	 * the TAS5720AQ1 will take time clearing the fault condition internally
	 * during which we don't want to bombard the system with the same
	 * error message over and over.
	 */
	if ((curr_fault & TAS5720AQ1_OCE) && !(tas5720->last_fault & TAS5720AQ1_OCE))
		dev_crit(dev, "experienced an over current hardware fault\n");

	if ((curr_fault & TAS5720AQ1_DCE) && !(tas5720->last_fault & TAS5720AQ1_DCE))
		dev_crit(dev, "experienced a DC detection fault\n");

	if ((curr_fault & TAS5720AQ1_OTE) && !(tas5720->last_fault & TAS5720AQ1_OTE))
		dev_crit(dev, "experienced an over temperature fault\n");

	/* Store current fault value so we can detect any changes next time */
	tas5720->last_fault = curr_fault;

	if (!curr_fault)
		goto out;

	/*
	 * Periodically toggle SDZ (shutdown bit) H->L->H to clear any latching
	 * faults as long as a fault condition persists. Always going through
	 * the full sequence no matter the first return value to minimizes
	 * chances for the device to end up in shutdown mode.
	 */
	ret = regmap_write_bits(tas5720->regmap, TAS5720AQ1_POWER_CTRL_REG,
				TAS5720AQ1_SDZ, 0);
	if (ret < 0)
		dev_err(dev, "failed to write POWER_CTRL register: %d\n", ret);

	ret = regmap_write_bits(tas5720->regmap, TAS5720AQ1_POWER_CTRL_REG,
				TAS5720AQ1_SDZ, TAS5720AQ1_SDZ);
	if (ret < 0)
		dev_err(dev, "failed to write POWER_CTRL register: %d\n", ret);

out:
	/* Schedule the next fault check at the specified interval */
	schedule_delayed_work(&tas5720->fault_check_work,
			      msecs_to_jiffies(TAS5720_FAULT_CHECK_INTERVAL));
}

static int tas5720aq1_codec_probe(struct snd_soc_codec *codec)
{
	struct tas5720_data *tas5720 = snd_soc_codec_get_drvdata(codec);
	unsigned int device_id;
	int ret;
	u16 regValue = TAS5720AQ1_MUTE_R | TAS5720AQ1_MUTE_L;

	tas5720->codec = codec;

	ret = regmap_read(tas5720->regmap, TAS5720AQ1_DEVICE_ID_REG, &device_id);
	if (ret < 0) {
		dev_err(codec->dev, "failed to read device ID register: %d\n",
			ret);
		goto probe_fail;
	}

	if (device_id != TAS5720AQ1_DEVICE_ID) {
		dev_err(codec->dev, "wrong device ID. expected: %u read: %u\n",
			TAS5720AQ1_DEVICE_ID, device_id);
		ret = -ENODEV;
		goto probe_fail;
	}

	dev_info(codec->dev, "device ID. expected: %u read: %u\n",
			TAS5720AQ1_DEVICE_ID, device_id);

	ret = snd_soc_update_bits(codec, TAS5720AQ1_ANALOG_CTRL_REG, TAS5720AQ1_RESERVED7_BIT,
				  TAS5720AQ1_RESERVED7_BIT);
	if (ret < 0)
		goto error_snd_soc_update_bits;

	//Initial mute and shutdown mode is removed for testing
	/*Set device to mute*/
	ret = snd_soc_update_bits(codec, TAS5720AQ1_VOLUME_CTRL_CFG_REG,
				  regValue , regValue);
	if (ret < 0)
		goto error_snd_soc_update_bits;

	/*
	 * Enter shutdown mode - our default when not playing audio - to
	 * minimize current consumption. On the TAS5720 there is no real down
	 * side doing so as all device registers are preserved and the wakeup
	 * of the codec is rather quick which we do using a dapm widget.
	 */
	ret = snd_soc_update_bits(codec, TAS5720AQ1_POWER_CTRL_REG,
				  TAS5720AQ1_SDZ, 0);
	if (ret < 0)
		goto error_snd_soc_update_bits;

	INIT_DELAYED_WORK(&tas5720->fault_check_work, tas5720_fault_check_work);

	return 0;

error_snd_soc_update_bits:
	dev_err(codec->dev, "error configuring device registers: %d\n", ret);

probe_fail:

	return ret;
}

static int tas5720aq1_codec_remove(struct snd_soc_codec *codec)
{
	struct tas5720_data *tas5720 = snd_soc_codec_get_drvdata(codec);
	int ret;

	cancel_delayed_work_sync(&tas5720->fault_check_work);

	return ret;
};

static int tas5720_dac_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct tas5720_data *tas5720 = snd_soc_codec_get_drvdata(codec);
	int ret;

	if (event & SND_SOC_DAPM_POST_PMU) {
		/* Take TAS5720AQ1 out of shutdown mode */
		ret = snd_soc_update_bits(codec, TAS5720AQ1_POWER_CTRL_REG,
					  TAS5720AQ1_SDZ, TAS5720AQ1_SDZ);
		if (ret < 0) {
			dev_err(codec->dev, "error waking codec: %d\n", ret);
			return ret;
		}

		/*
		 * Observe codec shutdown-to-active time. The datasheet only
		 * lists a nominal value however just use-it as-is without
		 * additional padding to minimize the delay introduced in
		 * starting to play audio (actually there is other setup done
		 * by the ASoC framework that will provide additional delays,
		 * so we should always be safe).
		 */
		msleep(25);

		/* Turn on TAS5720 periodic fault checking/handling */
		tas5720->last_fault = 0;
		schedule_delayed_work(&tas5720->fault_check_work,
				msecs_to_jiffies(200));
	} else if (event & SND_SOC_DAPM_PRE_PMD) {
		/* Disable TAS5720 periodic fault checking/handling */
		cancel_delayed_work_sync(&tas5720->fault_check_work);

		/* Place TAS5720 in shutdown mode to minimize current draw */
		ret = snd_soc_update_bits(codec, TAS5720AQ1_POWER_CTRL_REG,
					  TAS5720AQ1_SDZ, 0);
		if (ret < 0) {
			dev_err(codec->dev, "error shutting down codec: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static bool tas5720_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TAS5720AQ1_DEVICE_ID_REG:
	case TAS5720AQ1_FAULT_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tas5720aq1_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = TAS5720AQ1_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = tas5720_is_volatile_reg,
};

/*
 * DAC analog gain. There are four discrete values to select from, ranging
 * from 19.2 dB to 25 dB.
 */
static const DECLARE_TLV_DB_RANGE(dac_analog_tlv,
	0x0, 0x0, TLV_DB_SCALE_ITEM(1920, 0, 0),
	0x1, 0x1, TLV_DB_SCALE_ITEM(2260, 0, 0),
	0x2, 0x2, TLV_DB_SCALE_ITEM(2500, 0, 0),
);

/*
 * DAC digital volumes. From -103.5 to 24 dB in 0.5 dB steps. Note that
 * setting the gain below -100 dB (register value <0x7) is effectively a MUTE
 * as per device datasheet.
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, -10350, 50, 0);

static const struct snd_kcontrol_new tas5720_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Speaker Driver Playback Volume",
		TAS5720AQ1_LEFT_CHANNEL_VOLUME_CTRL, 
		TAS5720AQ1_RIGHT_CHANNEL_VOLUME_CTRL,
		0, 0xff, 0, dac_tlv),
	SOC_SINGLE_TLV("Speaker Driver Analog Gain", TAS5720AQ_ANALOG_CTRL_REG,
		       TAS5720AQ1_ANALOG_GAIN_SHIFT, 3, 0, dac_analog_tlv),
};

static const struct snd_soc_dapm_widget tas5720_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN("DAC IN", "Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, SND_SOC_NOPM, 0, 0, tas5720_dac_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas5720_audio_map[] = {
	{ "DAC", NULL, "DAC IN" },
	{ "OUT", NULL, "DAC" },
};

static struct snd_soc_codec_driver soc_codec_dev_tas5720aq1 = {
	.probe = tas5720aq1_codec_probe,
	.remove = tas5720aq1_codec_remove,
	.suspend = NULL,
	.resume = NULL,

	.component_driver = {
		.controls		= tas5720_snd_controls,
		.num_controls		= ARRAY_SIZE(tas5720_snd_controls),
		.dapm_widgets		= tas5720_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(tas5720_dapm_widgets),
		.dapm_routes		= tas5720_audio_map,
		.num_dapm_routes	= ARRAY_SIZE(tas5720_audio_map),
	},
};

/* PCM rates supported by the TAS5720 driver */
#define TAS5720_RATES	(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

/* Formats supported by TAS5720 driver */
#define TAS5720_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S18_3LE |\
			 SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops tas5720aq1_speaker_dai_ops = {
	.hw_params	= tas5720aq1_hw_params,
	.set_fmt	= tas5720aq1_set_dai_fmt,
	.digital_mute	= tas5720aq1_mute,
};

/*
 * TAS5720AQ1 DAI structure
 *
 * Note that were are advertising .playback.channels_max = 2 despite this being
 * a mono amplifier. The reason for that is that some serial ports such as TI's
 * McASP module have a minimum number of channels (2) that they can output.
 * Advertising more channels than we have will allow us to interface with such
 * a serial port without really any negative side effects as the TAS5720 will
 * simply ignore any extra channel(s) asides from the one channel that is
 * configured to be played back.
 */
static struct snd_soc_dai_driver tas5720aq1_dai[] = {
	{
		.name = "tas5720-amplifier",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = TAS5720_RATES,
			.formats = TAS5720_FORMATS,
		},
		.ops = &tas5720aq1_speaker_dai_ops,
	},
};

static int tas5720aq1_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tas5720_data *data;
	int ret;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->tas5720_client = client;
	data->regmap = devm_regmap_init_i2c(client, &tas5720aq1_regmap_config);
	if (IS_ERR(data->regmap)) {
		ret = PTR_ERR(data->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	dev_set_drvdata(dev, data);

	ret = snd_soc_register_codec(&client->dev,
				     &soc_codec_dev_tas5720aq1,
				     tas5720aq1_dai, ARRAY_SIZE(tas5720aq1_dai));
	if (ret < 0) {
		dev_err(dev, "failed to register codec: %d\n", ret);
		return ret;
	}

	dev_err(dev, "tas5720aq1_probe success \n", 0);
	return 0;
}

static int tas5720aq1_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	snd_soc_unregister_codec(dev);

	return 0;
}

static const struct i2c_device_id tas5720aq1_id[] = {
	{ "tas5720", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5720aq1_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tas5720aq1_of_match[] = {
	{ .compatible = "ti,tas5720", },
	{ },
};
MODULE_DEVICE_TABLE(of, tas5720aq1_of_match);
#endif

static struct i2c_driver tas5720aq1_i2c_driver = {
	.driver = {
		.name = "tas5720",
		.of_match_table = of_match_ptr(tas5720aq1_of_match),
	},
	.probe = tas5720aq1_probe,
	.remove = tas5720aq1_remove,
	.id_table = tas5720aq1_id,
};

module_i2c_driver(tas5720aq1_i2c_driver);

MODULE_AUTHOR("Vimal Babu <vimal2836@gmail.com");
MODULE_DESCRIPTION("TAS5720AQ1 Audio amplifier driver");
MODULE_LICENSE("GPL");
