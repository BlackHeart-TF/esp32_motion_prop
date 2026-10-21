#pragma once

void audio_init(void);

/* Play one random file from /storage/creaks once. Returns immediately. */
void audio_play_creak(void);

/* Fade out a creak that is still playing, then return. Does nothing if it already ended. */
void audio_fade(void);

/* Cut the clip that is playing. */
void audio_stop(void);

/* Play one random scream. Returns when it ends or audio_stop() cuts it. */
void audio_play_scream(void);
