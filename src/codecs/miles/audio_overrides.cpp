#include "core/stdafx.h"
#include "pluginsystem/modsystem.h"
#include "filesystem/filesystem.h"
#include "tier1/cvar.h"
#include <filesystem>
#include <fstream>
#include <random>
#include "audio_overrides.h"

using namespace std;
namespace fs = std::filesystem;

static CustomAudioManager g_CustomAudioManagerInst;
static char g_CurrentEventName[256] = {0};
static CThreadMutex g_CurrentEventMutex;

CustomAudioManager* GetAudioOverrideManager()
{
	return &g_CustomAudioManagerInst;
}

static void ReadFileAllBytes(const fs::path& path, vector<uint8_t>& out)
{
	ifstream f(path, ios::binary);
	if (!f.good()) return;
	f.seekg(0, ios::end);
	const size_t len = static_cast<size_t>(f.tellg());
	f.seekg(0, ios::beg);
	out.resize(len);
	f.read(reinterpret_cast<char*>(out.data()), len);
}

// Minimal WAV parser: verify RIFF/WAVE and extract first 'data' chunk as PCM
static bool ExtractPcmFromWave(const vector<uint8_t>& in, vector<uint8_t>& outPcm)
{
	if (in.size() < 44) return false;
	const uint8_t* p = in.data();
	if (!(p[0]=='R'&&p[1]=='I'&&p[2]=='F'&&p[3]=='F'&&p[8]=='W'&&p[9]=='A'&&p[10]=='V'&&p[11]=='E'))
		return false;
	size_t pos = 12;
	while (pos + 8 <= in.size())
	{
		const uint8_t* h = p + pos;
		uint32_t chunkSize = *(const uint32_t*)(h + 4);
		const char id0 = (char)h[0], id1 = (char)h[1], id2 = (char)h[2], id3 = (char)h[3];
		pos += 8;
		if (pos + chunkSize > in.size()) break;
		if (id0=='d' && id1=='a' && id2=='t' && id3=='a')
		{
			outPcm.assign(p + pos, p + pos + chunkSize);
			return true;
		}
		// chunks are word-aligned
		pos += ((chunkSize + 1) & ~1u);
	}
	return false;
}

void CustomAudioManager::LoadFromMods()
{
	if (!ModSystem()->IsEnabled())
		return;

	unordered_map<string, shared_ptr<EventOverrideData>> newOverrides;

	ModSystem()->LockModList();
	FOR_EACH_VEC(ModSystem()->GetModList(), i)
	{
		const CModSystem::ModInstance_t* mod = ModSystem()->GetModList()[i];
		if (!mod->IsEnabled())
			continue;

		const CUtlString base = mod->GetBasePath();
		// Support two layouts:
		// 1) audio_override/<EventName>/**.wav
		// 2) audio/<EventName>/**.wav  (Northstar-style without relying on JSON)
		auto loadEventFolder = [&newOverrides](const fs::path& eventFolder)
		{
			const string eventName = eventFolder.filename().string();
			if (eventName.empty()) return;

			for (auto& wav : fs::recursive_directory_iterator(eventFolder))
			{
				if (!wav.is_regular_file() || wav.path().extension() != ".wav") continue;
				vector<uint8_t> raw;
				ReadFileAllBytes(wav.path(), raw);
				vector<uint8_t> pcm;
				if (raw.empty() || !ExtractPcmFromWave(raw, pcm)) continue;

				auto& ptr = newOverrides[eventName];
				if (!ptr) ptr = make_shared<EventOverrideData>();

				AudioSample s;
				s.size = pcm.size();
				s.data.reset(new uint8_t[s.size]);
				memcpy(s.data.get(), pcm.data(), s.size);
				ptr->samples.emplace_back(std::move(s));
			}
		};

		fs::path audioOverrideDir = fs::path(base.String()) / "audio_override";
		if (fs::exists(audioOverrideDir) && fs::is_directory(audioOverrideDir))
		{
			for (auto& entry : fs::directory_iterator(audioOverrideDir))
			{
				if (entry.is_directory())
					loadEventFolder(entry.path());
			}
		}

		fs::path audioDir = fs::path(base.String()) / "audio";
		if (fs::exists(audioDir) && fs::is_directory(audioDir))
		{
			for (auto& entry : fs::directory_iterator(audioDir))
			{
				// Only treat subdirectories as event folders; ignore json files
				if (entry.is_directory())
					loadEventFolder(entry.path());
			}
		}
	}
	ModSystem()->UnlockModList();

	// Publish under lock
	{
		unique_lock lk(m_mutex);
		m_overrides.swap(newOverrides);
	}

	// Debug: list loaded override events and sample counts
	{
		shared_lock lk(m_mutex);
		for (const auto& [eventName, data] : m_overrides)
		{
			Msg(eDLL_T::AUDIO, "%s: override loaded for '%s' (%zu samples)\n", __FUNCTION__, eventName.c_str(), data ? data->samples.size() : 0);
		}
	}
}

void CustomAudioManager::Clear()
{
	unique_lock lk(m_mutex);
	m_overrides.clear();
}

bool CustomAudioManager::TryGetOverrideForEvent(const char* eventName, const void*& outPtr, unsigned& outLen)
{
	shared_lock lk(m_mutex);
	auto it = m_overrides.find(eventName);
	if (it == m_overrides.end())
		return false;

	auto& ov = *(it->second);
	if (ov.samples.empty())
		return false;

	// For now, sequential only
	if (ov.strategy == AudioSelectionStrategy::SEQUENTIAL)
	{
		const size_t idx = ov.currentIndex % ov.samples.size();
		outPtr = ov.samples[idx].data.get();
		outLen = static_cast<unsigned>(ov.samples[idx].size);
		ov.currentIndex = (idx + 1) % ov.samples.size();
		return true;
	}

	// Random selection
	static thread_local std::mt19937 rng{ std::random_device{}() };
	std::uniform_int_distribution<size_t> dist(0, ov.samples.size() - 1);
	const size_t idx = dist(rng);
	outPtr = ov.samples[idx].data.get();
	outLen = static_cast<unsigned>(ov.samples[idx].size);
	return true;
}

bool CustomAudioManager::HasOverride(const char* eventName)
{
	shared_lock lk(m_mutex);
	return m_overrides.find(eventName) != m_overrides.end();
}

int CustomAudioManager::GetOverrideCount()
{
	shared_lock lk(m_mutex);
	return static_cast<int>(m_overrides.size());
}

void AudioOverride_OnEventRun(const char* eventName)
{
	AUTO_LOCK(g_CurrentEventMutex);
	if (!eventName) { g_CurrentEventName[0] = '\0'; return; }
	V_strncpy(g_CurrentEventName, eventName, sizeof(g_CurrentEventName));
}

bool AudioOverride_TryGetBufferForCurrentEvent(const void*& outPtr, unsigned& outLen)
{
	AUTO_LOCK(g_CurrentEventMutex);
	if (g_CurrentEventName[0] == '\0') return false;
	return GetAudioOverrideManager()->TryGetOverrideForEvent(g_CurrentEventName, outPtr, outLen);
}

void AudioOverride_ClearCurrentEvent()
{
	AUTO_LOCK(g_CurrentEventMutex);
	g_CurrentEventName[0] = '\0';
}

const char* AudioOverride_GetCurrentEventName()
{
	AUTO_LOCK(g_CurrentEventMutex);
	return g_CurrentEventName;
}


