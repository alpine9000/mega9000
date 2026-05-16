#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../../../e9k-lib/e9k-mega.h"

typedef enum psnd_audio_vis_source
{
  PSND_AUDIO_VIS_SOURCE_FM = 0,
  PSND_AUDIO_VIS_SOURCE_PSG = 1,
  PSND_AUDIO_VIS_SOURCE_DAC = 2,
  PSND_AUDIO_VIS_SOURCE_MIXED = 3
} psnd_audio_vis_source_t;

#define PSND_AUDIO_VIS_MUTE_FM  (1u << 0)
#define PSND_AUDIO_VIS_MUTE_PSG (1u << 1)
#define PSND_AUDIO_VIS_MUTE_DAC (1u << 2)

void megadrive_audio_vis_add_s16(psnd_audio_vis_source_t source, const int16_t *data, size_t frames, int stereo);
void megadrive_audio_vis_add_pair(psnd_audio_vis_source_t source, int32_t left, int32_t right);
void megadrive_audio_vis_add_s32(psnd_audio_vis_source_t source, const int32_t *data, size_t frames, int stereo);
int megadrive_audio_vis_is_enabled(void);
uint32_t megadrive_audio_vis_mute_mask(void);
