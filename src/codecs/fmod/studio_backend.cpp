#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "audio_backend.h"

#include <unordered_map>
#include <string>

#include "thirdparty/fmod/inc/fmod_studio.h"
#include "thirdparty/fmod/inc/fmod_studio.hpp"
#include "thirdparty/fmod/inc/fmod.hpp"

static ConVar fmod_bank_root("fmod_bank_root", "audio/fmod", FCVAR_RELEASE, "Root directory for FMOD Studio bank files");

namespace
{
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

            // Autoload common banks if they exist under fmod_bank_root
            LoadBankFile("Master");
            LoadBankFile("Master.strings");
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
            if (m_studioSystem)
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

        // Bank management
        bool LoadBankFile(const char* bankName)
        {
            if (!m_studioSystem || !bankName) return false;
            if (m_loadedBanks.count(bankName)) return true;
            char path[MAX_PATH];
            V_snprintf(path, sizeof(path), "%s/%s.bank", fmod_bank_root.GetString(), bankName);
            Msg(eDLL_T::AUDIO, "Loading FMOD Studio bank file: %s\n", path);
            FMOD::Studio::Bank* bank = nullptr;
            if (m_studioSystem->loadBankFile(path, FMOD_STUDIO_LOAD_BANK_NORMAL, &bank) != FMOD_OK || !bank)
                return false;
            m_loadedBanks[bankName] = bank;
            return true;
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

    private:
        FMOD::Studio::System* m_studioSystem = nullptr;
        FMOD::System* m_core = nullptr;
        std::unordered_map<std::string, FMOD::Studio::Bank*> m_loadedBanks;
        uint64_t m_nextId = 1;
    };
}

ICustomAudioBackend* CreateFMODStudioBackend()
{
    return new FMODStudioBackend();
}


