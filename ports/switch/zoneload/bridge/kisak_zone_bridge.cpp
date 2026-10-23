#include "kisak_zone_hooks_internal.h"

#include "Game/IGame.h"
#include "Game/IW3/ContentLoaderIW3.h"
#include "Game/IW3/IW3.h"
#include "Loading/ILoadingStream.h"
#include "Zone/Stream/ZoneInputStream.h"
#include "Zone/XBlock.h"
#include "Zone/Zone.h"
#include "Loading/Exception/LoadingException.h"

#include <cstdio>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>

#if UINTPTR_MAX > 0xFFFFFFFFu
// Must match the engine cbrush_t stride (static_assert in src/xanim/xanim.h).
static_assert(sizeof(IW3::cbrush_t) == 96, "IW3::cbrush_t stride differs from the engine cbrush_t");
#endif
#include <exception>
#include <memory>
#include <optional>
#include <vector>

extern void Switch_LogWrite(const char *msg);
extern const char * volatile g_switchDbStage;

namespace
{
    thread_local const KisakZoneHooks* t_hooks = nullptr;

    class AssetLoadStallWatchdog final
    {
    public:
        explicit AssetLoadStallWatchdog(const char* zoneName)
            : m_zoneName(zoneName ? zoneName : "")
        {
            if (m_zoneName != "killhouse")
                return;

            try
            {
                m_thread = std::thread([this] { Run(); });
            }
            catch (...)
            {
                // The watchdog is best-effort; zone loading itself must still run.
            }
        }

        ~AssetLoadStallWatchdog()
        {
            m_stopping.store(true, std::memory_order_release);
            m_condition.notify_all();
            if (m_thread.joinable())
                m_thread.join();
        }

    private:
        void Run()
        {
            using Clock = std::chrono::steady_clock;
            auto lastProgressTime = Clock::now();
            uint64_t lastSerial = UINT64_MAX;
            std::unique_lock<std::mutex> lock(m_mutex);

            while (!m_stopping.load(std::memory_order_acquire))
            {
                m_condition.wait_for(lock, std::chrono::milliseconds(500), [this]
                {
                    return m_stopping.load(std::memory_order_acquire);
                });
                if (m_stopping.load(std::memory_order_acquire))
                    return;

                const auto progress = KisakZone_GetActiveAssetProgress();
                const auto now = Clock::now();
                if (progress.serial != lastSerial)
                {
                    lastSerial = progress.serial;
                    lastProgressTime = now;
                    continue;
                }

                if (progress.active && now - lastProgressTime >= std::chrono::seconds(10))
                {
                    const char *phaseName = "asset/start";
                    if (progress.assetType == 17)
                    {
                        switch (progress.phase)
                        {
                        case 1: phaseName = "lightdef/dispatch-entry"; break;
                        case 2: phaseName = "lightdef/loader-constructed"; break;
                        case 3: phaseName = "lightdef/load-return"; break;
                        case 4: phaseName = "lightdef/pointer-resolution"; break;
                        case 5: phaseName = "lightdef/pointer-return"; break;
                        case 6: phaseName = "lightdef/existing-name-lookup"; break;
                        case 7: phaseName = "lightdef/existing-lookup-return"; break;
                        case 8: phaseName = "lightdef/fields-read"; break;
                        case 9: phaseName = "lightdef/fields-decoded"; break;
                        case 10: phaseName = "lightdef/name-resolution"; break;
                        case 11: phaseName = "lightdef/name-resolved"; break;
                        case 12: phaseName = "lightdef/attenuation-resolution"; break;
                        case 13: phaseName = "lightdef/attenuation-return"; break;
                        case 14: phaseName = "lightdef/image-loader"; break;
                        case 15: phaseName = "lightdef/image-loader-return"; break;
                        case 16: phaseName = "lightdef/pointer-entry"; break;
                        case 17: phaseName = "lightdef/pointer-classify"; break;
                        case 18: phaseName = "lightdef/allocate"; break;
                        case 19: phaseName = "lightdef/insert-alias"; break;
                        case 20: phaseName = "lightdef/decode-entry"; break;
                        case 21: phaseName = "lightdef/decode-return"; break;
                        case 22: phaseName = "lightdef/asset-register"; break;
                        case 23: phaseName = "lightdef/register-return"; break;
                        case 24: phaseName = "lightdef/alias-lookup"; break;
                        case 25: phaseName = "lightdef/name-resolution"; break;
                        case 26: phaseName = "lightdef/collect-references"; break;
                        case 27: phaseName = "lightdef/engine-link"; break;
                        case 28: phaseName = "lightdef/engine-link-return"; break;
                        case 29: phaseName = "lightdef/oat-pool-insert"; break;
                        case 30: phaseName = "lightdef/oat-pool-return"; break;
                        case 31: phaseName = "lightdef/engine-register"; break;
                        case 32: phaseName = "lightdef/engine-register-return"; break;
                        default: phaseName = "lightdef/start"; break;
                        }
                    }

                    char trace[256];
                    const char *engineStage = g_switchDbStage;
                    std::snprintf(trace, sizeof(trace),
                        "[KisakCOD][ZONELOAD] asset-load no-progress zone=%s index=%zu type=%d phase=%s db=%s seconds=10\n",
                        m_zoneName.c_str(), progress.assetIndex, progress.assetType, phaseName,
                        engineStage ? engineStage : "(none)");
                    Switch_LogWrite(trace);
                    return;
                }
            }
        }

        const std::string m_zoneName;
        std::atomic<bool> m_stopping{false};
        std::mutex m_mutex;
        std::condition_variable m_condition;
        std::thread m_thread;
    };

    class ScopedHooks
    {
    public:
        explicit ScopedHooks(const KisakZoneHooks* hooks)
            : m_previous(t_hooks)
        {
            t_hooks = hooks;
        }

        ~ScopedHooks()
        {
            t_hooks = m_previous;
        }

    private:
        const KisakZoneHooks* m_previous;
    };

    class HookStream final : public ILoadingStream
    {
    public:
        explicit HookStream(const KisakZoneHooks& hooks)
            : m_hooks(hooks)
        {
        }

        size_t Load(void* buffer, const size_t length) override
        {
            const size_t read = m_hooks.read(m_hooks.context, buffer, length);
            m_position += static_cast<int64_t>(read);
            return read;
        }

        int64_t Pos() override
        {
            return m_position;
        }

    private:
        const KisakZoneHooks& m_hooks;
        int64_t m_position = 0;
    };

    struct BlockSetup
    {
        const char* name;
        XBlockType type;
    };

    // Same block kinds as OAT's ZoneLoaderFactoryIW3, in XFILE_BLOCK_* order.
    const BlockSetup kBlocks[9] = {
        {"XFILE_BLOCK_TEMP",             XBlockType::BLOCK_TYPE_TEMP   },
        {"XFILE_BLOCK_RUNTIME",          XBlockType::BLOCK_TYPE_RUNTIME},
        {"XFILE_BLOCK_LARGE_RUNTIME",    XBlockType::BLOCK_TYPE_RUNTIME},
        {"XFILE_BLOCK_PHYSICAL_RUNTIME", XBlockType::BLOCK_TYPE_RUNTIME},
        {"XFILE_BLOCK_VIRTUAL",          XBlockType::BLOCK_TYPE_NORMAL },
        {"XFILE_BLOCK_LARGE",            XBlockType::BLOCK_TYPE_NORMAL },
        {"XFILE_BLOCK_PHYSICAL",         XBlockType::BLOCK_TYPE_NORMAL },
        {"XFILE_BLOCK_VERTEX",           XBlockType::BLOCK_TYPE_NORMAL },
        {"XFILE_BLOCK_INDEX",            XBlockType::BLOCK_TYPE_NORMAL },
    };

    // IW3 PC zones: 32-bit pointers whose top 4 bits select the block.
    constexpr unsigned kZonePointerBits = 32u;
    constexpr unsigned kOffsetBlockBits = 4u;
} // namespace

const KisakZoneHooks* KisakZone_CurrentHooks()
{
    return t_hooks;
}

// Defined in ZoneCommon/Marking/BaseAssetMarker.cpp.
void KisakZone_ResetConvertedScriptStrings();

void* KisakZone_Load(const char* zoneName, const KisakZoneBlocks& blocks, const KisakZoneHooks& hooks, char* errorOut, const size_t errorSize)
{
    try
    {
        auto zone = std::make_unique<Zone>(zoneName ? zoneName : "", 0, GameId::IW3, GamePlatform::PC);

        std::vector<std::unique_ptr<XBlock>> ownedBlocks;
        std::vector<XBlock*> blockList;
        for (unsigned index = 0; index < 9; ++index)
        {
            auto block = std::make_unique<XBlock>(kBlocks[index].name, index, kBlocks[index].type);
            block->UseExternalBuffer(blocks.data[index], blocks.size[index]);
            blockList.push_back(block.get());
            ownedBlocks.push_back(std::move(block));
        }

        HookStream stream(hooks);
        ScopedHooks scope(&hooks);
        const auto input =
            ZoneInputStream::Create(kZonePointerBits, kOffsetBlockBits, blockList, IW3::XFILE_BLOCK_VIRTUAL, stream, zone->Memory(), std::nullopt);
        IW3::ContentLoader loader(*zone, *input);
        KisakZone_ClearActiveAsset();
        KisakZone_ResetConvertedScriptStrings();
        {
            AssetLoadStallWatchdog watchdog(zoneName);
            loader.Load();
        }
        KisakZone_ClearActiveAsset();
        KisakZone_ResetConvertedScriptStrings();
        return zone.release();
    }
    catch (LoadingException& e)
    {
        const auto progress = KisakZone_GetActiveAssetProgress();
        const std::string detail = e.DetailedMessage();
        if (errorOut && errorSize)
        {
            snprintf(errorOut, errorSize,
                "%s (zone=%s asset_index=%zu asset_type=%d phase=%d)",
                detail.c_str(),
                zoneName ? zoneName : "(unknown)",
                progress.assetIndex,
                progress.assetType,
                progress.phase);
        }

        char trace[768];
        snprintf(trace, sizeof(trace),
            "[KisakCOD][ZONELOAD FAIL] zone=%s asset_index=%zu asset_type=%d phase=%d error=%s\n",
            zoneName ? zoneName : "(unknown)",
            progress.assetIndex,
            progress.assetType,
            progress.phase,
            detail.c_str());
        Switch_LogWrite(trace);
        KisakZone_ClearActiveAsset();
    }
    catch (const std::exception& e)
    {
        const auto progress = KisakZone_GetActiveAssetProgress();
        if (errorOut && errorSize)
            snprintf(errorOut, errorSize,
                "%s (zone=%s asset_index=%zu asset_type=%d phase=%d)",
                e.what(),
                zoneName ? zoneName : "(unknown)",
                progress.assetIndex,
                progress.assetType,
                progress.phase);

        char trace[768];
        snprintf(trace, sizeof(trace),
            "[KisakCOD][ZONELOAD FAIL] zone=%s asset_index=%zu asset_type=%d phase=%d error=%s\n",
            zoneName ? zoneName : "(unknown)",
            progress.assetIndex,
            progress.assetType,
            progress.phase,
            e.what());
        Switch_LogWrite(trace);
        KisakZone_ClearActiveAsset();
    }
    catch (...)
    {
        const auto progress = KisakZone_GetActiveAssetProgress();
        if (errorOut && errorSize)
            snprintf(errorOut, errorSize,
                "unknown zone loading error (zone=%s asset_index=%zu asset_type=%d phase=%d)",
                zoneName ? zoneName : "(unknown)",
                progress.assetIndex,
                progress.assetType,
                progress.phase);
        KisakZone_ClearActiveAsset();
    }
    return nullptr;
}

void KisakZone_Free(void* zone)
{
    delete static_cast<Zone*>(zone);
}

/*
 * Extract the model pointers from an OAT WeaponDef without exposing OAT's
 * generated asset types to the engine translation unit. That unit also
 * includes the engine's legacy Fx/RawFile/StringTable declarations, so
 * including IW3_Assets.h there causes duplicate type declarations.
 */
bool KisakZone_ExtractWeaponModelPointers(const void* asset, void** modelPointers, const size_t modelPointerCount)
{
    constexpr size_t kWeaponModelPointerCount = 38;
    if (!asset || !modelPointers || modelPointerCount < kWeaponModelPointerCount)
        return false;

    const auto* const weapon = static_cast<const IW3::WeaponDef*>(asset);
    size_t outputIndex = 0;

    for (size_t index = 0; index < 16; ++index)
        modelPointers[outputIndex++] = weapon->gunXModel[index];
    for (size_t index = 0; index < 16; ++index)
        modelPointers[outputIndex++] = weapon->worldModel[index];

    modelPointers[outputIndex++] = weapon->handXModel;
    modelPointers[outputIndex++] = weapon->worldClipModel;
    modelPointers[outputIndex++] = weapon->rocketModel;
    modelPointers[outputIndex++] = weapon->knifeModel;
    modelPointers[outputIndex++] = weapon->worldKnifeModel;
    modelPointers[outputIndex++] = weapon->projectileModel;

    return outputIndex == kWeaponModelPointerCount;
}

bool KisakZone_ExtractClipMapStaticModels(
    const void *asset,
    const void **sourceList,
    size_t *recordCount,
    KisakZoneStaticModelRecord *records,
    const size_t recordCapacity)
{
    if (!asset || !sourceList || !recordCount)
        return false;

    const auto *clipMap = static_cast<const IW3::clipMap_t *>(asset);
    const auto *models = clipMap->staticModelList;
    const size_t count = clipMap->numStaticModels;
    *sourceList = models;
    *recordCount = count;

    if (!models || count == 0 || records == nullptr)
        return true;
    if (recordCapacity < count)
        return false;

    for (size_t index = 0; index < count; ++index)
    {
        const IW3::cStaticModel_s &source = models[index];
        KisakZoneStaticModelRecord &destination = records[index];
        destination.nextModelInWorldSector =
            source.writable.nextModelInWorldSector;
        destination.xmodel = source.xmodel;
        std::memcpy(destination.origin, source.origin, sizeof(destination.origin));
        std::memcpy(
            destination.invScaledAxis,
            source.invScaledAxis,
            sizeof(destination.invScaledAxis));
        std::memcpy(destination.absmin, source.absmin, sizeof(destination.absmin));
        std::memcpy(destination.absmax, source.absmax, sizeof(destination.absmax));
    }

    return true;
}
