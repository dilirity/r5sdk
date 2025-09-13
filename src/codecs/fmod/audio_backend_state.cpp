#include "codecs/fmod/audio_backend.h"

static ICustomAudioBackend* g_activeCustomBackend = nullptr;

ICustomAudioBackend* GetActiveCustomAudioBackend() {
	return g_activeCustomBackend;
}

void SetActiveCustomAudioBackend(ICustomAudioBackend* backendInstance) {
	g_activeCustomBackend = backendInstance;
}