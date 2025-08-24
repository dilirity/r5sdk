#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "audio_backend.h"

#include <unordered_map>
#include <string>
#include <vector>

#include "thirdparty/fmod/inc/fmod_studio.h"
#include "thirdparty/fmod/inc/fmod_studio.hpp"
#include "thirdparty/fmod/inc/fmod.hpp"

#include "../miles/miles_impl.h"

class FMODStudioBackend final : public ICustomAudioBackend
    {
    public:
        bool Initialize() override
        {
            if (m_studioSystem)
                return true;

            FMOD::Studio::System::create(&m_studioSystem);
            if (!m_studioSystem) return false;

            m_studioSystem->initialize(64, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr);
            m_studioSystem->getCoreSystem(&m_core);
            if (!m_core) return false;

            LoadBaseBanks();
            LoadModBanks();
            return true;
        }

        void Shutdown() override
        {
            for (auto& it : m_loadedBanks)
            {
                if (it.second)
                    it.second->unload();
            }
            m_loadedBanks.clear();
            if (m_studioSystem)
            {
                m_studioSystem->unloadAll();
                m_studioSystem->release();
                m_studioSystem = nullptr;
            }
            m_core = nullptr;
        }

        void Update(float /*dt*/) override
        {
            if (!m_studioSystem)
                return;

            // Sync global volume from engine cvar (Miles global state)
            SyncGlobalVolume();

            m_studioSystem->update();
        }

        void SetListenerPosition(const Vector3D& position, const QAngle& rotation) override
        {
            if (!m_studioSystem) return;
            FMOD_3D_ATTRIBUTES attrs{};

            Vector3D forward, up;
            AngleVectors(rotation, &forward, nullptr, &up);

            attrs.position = { position.x, position.y, position.z };
            attrs.forward = { -forward.x, -forward.y, -forward.z };
            attrs.up = { up.x, up.y, up.z };
            m_studioSystem->setListenerAttributes(0, &attrs);
        }

        uint64_t PlayRawPCM3D(const void*, unsigned int, int, unsigned short, unsigned short, const Vector3D&, float, const char*) override
        {
            // Not supported in Studio backend
            return 0;
        }

        uint64_t PlayEvent3D(const char* eventPathOrName, const Vector3D& position, float initialVolume) override
        {
            if (!m_studioSystem || !eventPathOrName) return 0;

            // Ensure path has event:/ prefix
            char pathBuf[512];
            const char* query = eventPathOrName;
            if (V_strnicmp(query, "event:/", 7) != 0)
            {
                V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventPathOrName);
                query = pathBuf;
            }

            FMOD::Studio::EventDescription* desc = nullptr;
            if (m_studioSystem->getEvent(query, &desc) != FMOD_OK || !desc)
                return 0;

            FMOD::Studio::EventInstance* inst = nullptr;
            if (desc->createInstance(&inst) != FMOD_OK || !inst)
                return 0;

            FMOD_3D_ATTRIBUTES attrs{};
            attrs.position = { position.x, position.y, position.z };
            attrs.forward = { 0.0f, 0.0f, 1.0f };
            attrs.up = { 0.0f, 1.0f, 0.0f };
            inst->set3DAttributes(&attrs);
            inst->setVolume(initialVolume);
            inst->start();
            inst->release(); // auto-release once stopped
            return ++m_nextId;
        }

        int StopSamplesForEvent(const char* /*eventName*/) override
        {
            // Could be implemented by querying instances by path; keep simple for now
            return 0;
        }

        void StopAll() override
        {
            if (!m_studioSystem) return;
            m_studioSystem->flushCommands();
            m_studioSystem->unloadAll();
            m_loadedBanks.clear();
        }

        // Utility: print currently loaded banks
        void ListLoadedBanks() const
        {
            int count = (int)m_loadedBanks.size();
            Msg(eDLL_T::AUDIO, "FMOD: %d bank(s) loaded\n", count);
            for (const auto& it : m_loadedBanks)
            {
                Msg(eDLL_T::AUDIO, " - %s\n", it.first.c_str());
            }
        }

        bool EventExists(const char* eventPathOrName) override
        {
            if (!m_studioSystem || !eventPathOrName) return false;
            // Try with provided string, then with event:/ prefix
            FMOD::Studio::EventDescription* desc = nullptr;
            if (m_studioSystem->getEvent(eventPathOrName, &desc) == FMOD_OK && desc != nullptr)
                return true;
            char pathBuf[512];
            if (V_strnicmp(eventPathOrName, "event:/", 7) != 0)
            {
                V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventPathOrName);
                if (m_studioSystem->getEvent(pathBuf, &desc) == FMOD_OK && desc != nullptr)
                    return true;
            }
            return false;
        }

        bool isAnimEvent(const char* eventPathOrName) override
        {
            if (!m_studioSystem || !eventPathOrName)
                return false;

            // Ensure path has event:/ prefix
            char pathBuf[512];
            const char* query = eventPathOrName;
            if (V_strnicmp(query, "event:/", 7) != 0)
            {
                V_snprintf(pathBuf, sizeof(pathBuf), "event:/%s", eventPathOrName);
                query = pathBuf;
            }

            FMOD::Studio::EventDescription* desc = nullptr;
            if (m_studioSystem->getEvent(query, &desc) != FMOD_OK || !desc)
                return false;

            FMOD_STUDIO_USER_PROPERTY prop{};
            if (desc->getUserProperty("isAnimEvent", &prop) != FMOD_OK)
                return false;

            if (prop.type == FMOD_STUDIO_USER_PROPERTY_TYPE_FLOAT)
            {
                return prop.floatvalue == 1;
            }

            return false;
        }

    private:
        void SyncGlobalVolume()
        {
            if (!g_milesGlobals)
                return;

            // Acquire master bus once
            if (!m_masterBus)
            {
                if (m_studioSystem->getBus("bus:/", &m_masterBus) != FMOD_OK || !m_masterBus)
                {
                    // Fallback in case project uses explicit Master path
                    m_studioSystem->getBus("bus:/Master", &m_masterBus);
                }
            }

            const float desired = (g_milesGlobals->soundMasterVolume < 0.0f) ? 0.0f : (g_milesGlobals->soundMasterVolume > 1.0f ? 1.0f : g_milesGlobals->soundMasterVolume);

            if (m_masterBus && fabsf(desired - m_lastGlobalVolume) > 0.001f)
            {
                m_masterBus->setVolume(desired);
                m_lastGlobalVolume = desired;
            }
        }

        void LoadBaseBanks()
        {
            char searchPattern[MAX_PATH];
            V_snprintf(searchPattern, sizeof(searchPattern), "%s/*.bank", "audio/fmod");

            WIN32_FIND_DATAA findData{};
            HANDLE hFind = FindFirstFileA(searchPattern, &findData);
            if (hFind == INVALID_HANDLE_VALUE)
                return;

            std::vector<std::string> bankFiles;
            do
            {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                bankFiles.emplace_back(findData.cFileName);
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);

            auto loadOne = [&](const char* filename)
            {
                char fullPath[MAX_PATH];
                V_snprintf(fullPath, sizeof(fullPath), "%s/%s", "audio/fmod", filename);

                // derive base name without extension for map key
                const char* dot = strrchr(filename, '.');
                size_t baseLen = dot ? static_cast<size_t>(dot - filename) : strlen(filename);
                char base[260];
                V_strncpy(base, filename, min(baseLen + 1, sizeof(base)));

                if (m_loadedBanks.count(base))
                    return;

                Msg(eDLL_T::AUDIO, "Loading FMOD Studio bank file: %s\n", fullPath);
                FMOD::Studio::Bank* bank = nullptr;
                if (m_studioSystem->loadBankFile(fullPath, FMOD_STUDIO_LOAD_BANK_NORMAL, &bank) == FMOD_OK && bank)
                {
                    m_loadedBanks[base] = bank;
                }
                else
                {
                    Msg(eDLL_T::AUDIO, "Failed to load FMOD Studio bank: %s\n", fullPath);
                }
            };

            for (const std::string& f : bankFiles)
            {
                loadOne(f.c_str());
            }
        }

        void LoadModBanks()
        {
            // Enumerate mods/*/audio/fmod/*.bank
            WIN32_FIND_DATAA modFindData{};
            HANDLE modFind = FindFirstFileA("mods/*", &modFindData);
            if (modFind == INVALID_HANDLE_VALUE)
                return;

            do
            {
                if ((modFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    continue;
                if (modFindData.cFileName[0] == '.')
                    continue; // skip . and ..

                char bankSearch[MAX_PATH];
                V_snprintf(bankSearch, sizeof(bankSearch), "mods/%s/audio/fmod/*.bank", modFindData.cFileName);

                WIN32_FIND_DATAA bankFindData{};
                HANDLE bankFind = FindFirstFileA(bankSearch, &bankFindData);
                if (bankFind == INVALID_HANDLE_VALUE)
                    continue;

                do
                {
                    if (bankFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        continue;

                    char fullPath[MAX_PATH];
                    V_snprintf(fullPath, sizeof(fullPath), "mods/%s/audio/fmod/%s", modFindData.cFileName, bankFindData.cFileName);

                    // Use a unique key per mod + filename to avoid clobbering base banks in the map
                    char bankKey[260];
                    V_snprintf(bankKey, sizeof(bankKey), "%s/%s", modFindData.cFileName, bankFindData.cFileName);
                    if (m_loadedBanks.count(bankKey))
                        continue;

                    Msg(eDLL_T::AUDIO, "Loading FMOD Studio bank file (mod): %s\n", fullPath);
                    FMOD::Studio::Bank* bank = nullptr;
                    if (m_studioSystem->loadBankFile(fullPath, FMOD_STUDIO_LOAD_BANK_NORMAL, &bank) == FMOD_OK && bank)
                    {
                        m_loadedBanks[bankKey] = bank;
                    }
                    else
                    {
                        Msg(eDLL_T::AUDIO, "Failed to load FMOD Studio bank file (mod): %s\n", fullPath);
                    }
                } while (FindNextFileA(bankFind, &bankFindData));

                FindClose(bankFind);

            } while (FindNextFileA(modFind, &modFindData));

            FindClose(modFind);
        }

        FMOD::Studio::System* m_studioSystem = nullptr;
        FMOD::System* m_core = nullptr;
        std::unordered_map<std::string, FMOD::Studio::Bank*> m_loadedBanks;
        uint64_t m_nextId = 1;
        FMOD::Studio::Bus* m_masterBus = nullptr;
        float m_lastGlobalVolume = -1.0f;
    };

ICustomAudioBackend* CreateFMODStudioBackend()
{
    return new FMODStudioBackend();
}

static void CC_fmod_event_exists(const CCommand& args)
{
    if (args.ArgC() < 2)
    {
        Msg(eDLL_T::AUDIO, "Usage: fmod_event_exists <event_path_or_name>\n");
        return;
    }


    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be)
    {
        Msg(eDLL_T::AUDIO, "No active custom audio backend\n");
        return;
    }
    const char* path = args[1];
    Msg(eDLL_T::AUDIO, "FMOD event '%s': %s\n", path, be->EventExists(path) ? "FOUND" : "NOT FOUND");
}
static ConCommand fmod_event_exists("fmod_event_exists", CC_fmod_event_exists, "Check if an FMOD Studio event exists in loaded banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);

static void CC_fmod_list_banks(const CCommand& args)
{
    ICustomAudioBackend* be = GetActiveCustomAudioBackend();
    if (!be)
    {
        Msg(eDLL_T::AUDIO, "No active custom audio backend\n");
        return;
    }

    // Only supported by the Studio backend
    FMODStudioBackend* studio = dynamic_cast<FMODStudioBackend*>(be);
    if (!studio)
    {
        Msg(eDLL_T::AUDIO, "The active backend does not support listing FMOD banks\n");
        return;
    }
    studio->ListLoadedBanks();
}
static ConCommand fmod_list_banks("fmod_list_banks", CC_fmod_list_banks, "List currently loaded FMOD Studio banks", FCVAR_CLIENTDLL | FCVAR_RELEASE);


