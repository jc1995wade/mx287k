/*
 * zlg_apollo.c  --  ALSA Soc Audio Layer
 *
 * Copyright (c) 2007 Roman Moravcik <roman.moravcik@xxxxxxxxx>
 *
 * Authors: Graeme Gregory graeme.gregory@xxxxxxxxxxxxxxxx
 *          Philipp Zabel <philipp.zabel@xxxxxxxxx>
 *          Denis Grigoriev <dgreenday@xxxxxxxxx>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/uda1380.h>

#include <mach/dma.h>
#include <mach/device.h>

#include "mxs-dai.h"
#include "mxs-pcm.h"
#include "../codecs/uda1380.h"

#define ZLG_APOLLO_HP_OFF    0
#define ZLG_APOLLO_HP_ON     1

#define ZLG_APOLLO_SPK_ON    0
#define ZLG_APOLLO_SPK_OFF   1
#define ZLG_APOLLO_MIC       2

static int zlg_apollo_jack_func = ZLG_APOLLO_HP_ON;
static int zlg_apollo_spk_func = ZLG_APOLLO_SPK_OFF;

struct mxs_evk_priv {
	int sysclk;
	int hw;
	struct platform_device *pdev;
};

static struct mxs_evk_priv card_priv;

static void zlg_apollo_ext_control(struct snd_soc_codec *codec)
{
	if (zlg_apollo_spk_func == ZLG_APOLLO_SPK_ON)
		snd_soc_dapm_enable_pin(codec, "Speaker");
	else
		snd_soc_dapm_disable_pin(codec, "Speaker");
	    
	switch (zlg_apollo_jack_func) {
		case ZLG_APOLLO_HP_OFF:
	    		snd_soc_dapm_disable_pin(codec, "Headphone Jack");
			snd_soc_dapm_disable_pin(codec, "Mic Jack");
			break;
		case ZLG_APOLLO_HP_ON:
			snd_soc_dapm_enable_pin(codec, "Headphone Jack");
			snd_soc_dapm_disable_pin(codec, "Mic Jack");
			break;
		case ZLG_APOLLO_MIC:
			snd_soc_dapm_disable_pin(codec, "Headphone Jack");
			snd_soc_dapm_enable_pin(codec, "Mic Jack");
			break;
	}
	snd_soc_dapm_sync(codec);
}

static int zlg_apollo_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->card->codec;

	/* check the jack status at stream startup */
	zlg_apollo_ext_control(codec);

	return 0;
}

static void zlg_apollo_shutdown(struct snd_pcm_substream *substream)
{
	struct mxs_evk_priv *priv = &card_priv;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_dai *cpu_dai = machine->cpu_dai;

	if (cpu_dai->playback.active || cpu_dai->capture.active)
		priv->hw = 1;
	else
		priv->hw = 0;
}

static int zlg_apollo_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai_link *machine = rtd->dai;
	struct snd_soc_dai *cpu_dai = machine->cpu_dai;
	struct snd_soc_dai *codec_dai = machine->codec_dai;
	struct mxs_evk_priv *priv = &card_priv;
	unsigned int rate = params_rate(params);
	int ret = 0;

	u32 dai_format;

	/* only need to do this once as capture and playback are sync */
	if (priv->hw)
		return 0;
	priv->hw = 1;
	priv->sysclk = 512 * rate;

	snd_soc_dai_set_clkdiv(cpu_dai, IMX_SSP_SYS_MCLK, 128);

	/* set codec to slave mode */
	dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
	    SND_SOC_DAIFMT_CBS_CFS;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if (ret < 0)
		return ret;
	/* set cpu_dai to master mode for playback, slave mode for record */
	dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_format);
	if (ret < 0)
		return ret;

	/* set the SAIF system clock as output */
	snd_soc_dai_set_sysclk(cpu_dai, IMX_SSP_SYS_CLK, priv->sysclk, \
		SND_SOC_CLOCK_OUT);

	return 0;
}

static struct snd_soc_ops zlg_apollo_ops = {
	.startup 	= zlg_apollo_startup,
	.shutdown 	= zlg_apollo_shutdown,
	.hw_params 	= zlg_apollo_hw_params,
};

static int zlg_apollo_get_jack(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = zlg_apollo_jack_func;
	return 0;
}

static int zlg_apollo_set_jack(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (zlg_apollo_jack_func == ucontrol->value.integer.value[0])
		return 0;

	zlg_apollo_jack_func = ucontrol->value.integer.value[0];
	zlg_apollo_ext_control(codec);
	return 1;
}

static int zlg_apollo_get_spk(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = zlg_apollo_spk_func;
	return 0;
}

static int zlg_apollo_set_spk(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	if (zlg_apollo_spk_func == ucontrol->value.integer.value[0])
		return 0;

	zlg_apollo_spk_func = ucontrol->value.integer.value[0];
	zlg_apollo_ext_control(codec);
	return 1;
}

static int zlg_apollo_spk_power(struct snd_soc_dapm_widget *w,
	       	struct snd_kcontrol *kcontrol, int event)
{
	return 0;
}

static const struct snd_soc_dapm_widget uda1380_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", zlg_apollo_spk_power),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* headphone connected to VOUTLHP, VOUTRHP */
	{"Headphone Jack", NULL, "VOUTLHP"},
	{"Headphone Jack", NULL, "VOUTRHP"},

	/* ext speaker connected to VOUTL, VOUTR  */
	{"Speaker", NULL, "VOUTL"},
	{"Speaker", NULL, "VOUTR"},

	/* mic is connected to VINM */
	{"VINM", NULL, "Mic Jack"},
};

static const char *jack_function[] = {"Off", "Headphone", "Mic"};
static const char *spk_function[] = {"On", "Off"};

static const struct soc_enum zlg_apollo_enum[] = {
	SOC_ENUM_SINGLE_EXT(3, jack_function),
	SOC_ENUM_SINGLE_EXT(2, spk_function),
};

static const struct snd_kcontrol_new uda1380_zlg_apollo_controls[] = {
	SOC_ENUM_EXT("Jack Function", zlg_apollo_enum[0], zlg_apollo_get_jack,
			zlg_apollo_set_jack),
	SOC_ENUM_EXT("Speaker Function", zlg_apollo_enum[1], zlg_apollo_get_spk,
			zlg_apollo_set_spk),
};

static int zlg_apollo_uda1380_init(struct snd_soc_codec *codec)
{
	int i, err;

	snd_soc_dapm_nc_pin(codec, "VOUTLHP");
	snd_soc_dapm_nc_pin(codec, "VOUTRHP");
	
	/* Add specific widgets */
	snd_soc_dapm_new_controls(codec, uda1380_dapm_widgets,
				  ARRAY_SIZE(uda1380_dapm_widgets));

	zlg_apollo_ext_control(codec);

	for (i = 0; i < ARRAY_SIZE(uda1380_zlg_apollo_controls); i++) {
		if ((err = snd_ctl_add(codec->card,
				snd_soc_cnew(&uda1380_zlg_apollo_controls[i],
				codec, NULL))) < 0)
			return err;
	}
	err = snd_soc_dapm_add_routes(codec, audio_map,
				      ARRAY_SIZE(audio_map));

	snd_soc_dapm_sync(codec);

	return 0;
}

static struct snd_soc_dai_link mxs_28x = {
	.name 		= "UDA1380",
	.stream_name 	= "UDA1380",
	.codec_dai 	= &uda1380_dai[UDA1380_DAI_DUPLEX],
	.init 		= zlg_apollo_uda1380_init,
	.ops 		= &zlg_apollo_ops,
};

static struct snd_soc_card snd_soc_machine_zlg_apollo = {
	.name 		= "zlg@uda1380",
	.dai_link 	= &mxs_28x,
	.platform 	= &mxs_soc_platform,
	.num_links 	= 1,
};

static struct snd_soc_device mxs_28x_snd_devdata = {
	.card 	= &snd_soc_machine_zlg_apollo,
	.codec_dev 	= &soc_codec_dev_uda1380,
};

static struct platform_device *mxs_28x_snd_device;

static int __devinit mxs_apollo_uda1380_probe(struct platform_device *pdev)
{
	struct mxs_audio_platform_data *plat = pdev->dev.platform_data;
	struct mxs_saif *saif_select;
	int ret = -EINVAL;
	if (plat->init && plat->init())
		goto err_plat_init;

	mxs_28x.cpu_dai = &mxs_saif_dai[0];
	saif_select = (struct mxs_saif *)mxs_28x.cpu_dai->private_data;
	saif_select->stream_mapping = PLAYBACK_SAIF0_CAPTURE_SAIF1;
	saif_select->saif_mclk = plat->saif_mclock;
	saif_select->saif_clk = SAIF0;
	return 0;
err_plat_init:
	if (plat->finit)
		plat->finit();
	return ret;
}

static int mxs_apollo_uda1380_remove(struct platform_device *pdev)
{
	struct mxs_audio_platform_data *plat = pdev->dev.platform_data;

	if (plat->finit)
		plat->finit();
	return 0;
}

static struct platform_driver mxs_apollo_uda1380_audio_driver = {
	.probe = mxs_apollo_uda1380_probe,
	.remove = mxs_apollo_uda1380_remove,
	.driver = {
		   .name = "mxs-uda1380",
		   },
};

static int __init zlg_apollo_init(void)
{
	int ret;
	
	ret = platform_driver_register(&mxs_apollo_uda1380_audio_driver);
	if (ret)
		return -ENOMEM;
	
	mxs_28x_snd_device = platform_device_alloc("soc-audio", -1);
	if (!mxs_28x_snd_device) {
		printk(KERN_ERR "platform_dev_alloc failed\n");
		return -ENOMEM;
	}
	memset(&card_priv.sysclk, 0,sizeof(struct mxs_evk_priv));
	platform_set_drvdata(mxs_28x_snd_device, &mxs_28x_snd_devdata);
	mxs_28x_snd_devdata.dev = &mxs_28x_snd_device->dev;
	ret = platform_device_add(mxs_28x_snd_device);

	if (ret) {
		printk(KERN_ERR "platform_dev_add failed\n");
		platform_device_put(mxs_28x_snd_device);
	}

	return ret;
}

static void __exit zlg_apollo_exit(void)
{
	platform_driver_unregister(&mxs_apollo_uda1380_audio_driver);
	platform_device_unregister(mxs_28x_snd_device);
}

module_init(zlg_apollo_init);
module_exit(zlg_apollo_exit);

MODULE_AUTHOR("www.zlgmcu.com cxf");
MODULE_DESCRIPTION("ALSA SoC For ZLG EasyARM-i.MX28xA");
MODULE_LICENSE("GPL");
