// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip Utils API
 *
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 */

#include <dt-bindings/soc/rockchip-system-status.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

void rockchip_utils_get_performance(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);

	might_sleep();

	if (rate < 96000 && channels < 8)
		return;

	dev_dbg(dai->dev, "%s: stream[%d]: rate: %u, channels: %u\n",
		__func__, substream->stream, rate, channels);

	rockchip_set_system_status(SYS_STATUS_PERFORMANCE);
}
EXPORT_SYMBOL_GPL(rockchip_utils_get_performance);

void rockchip_utils_put_performance(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	unsigned int rate = substream->runtime->rate;
	unsigned int channels = substream->runtime->channels;

	might_sleep();

	if (rate < 96000 && channels < 8)
		return;

	dev_dbg(dai->dev, "%s: stream[%d]: rate: %u, channels: %u\n",
		__func__, substream->stream, rate, channels);

	rockchip_clear_system_status(SYS_STATUS_PERFORMANCE);
}
EXPORT_SYMBOL_GPL(rockchip_utils_put_performance);

MODULE_LICENSE("GPL");
