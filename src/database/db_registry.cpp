    if (g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u)
    {
        const uint64_t headerValue =
            *reinterpret_cast<const uint64_t *>(&newEntry->entry.asset.header);
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH ASSET1126] DB_Link entry=%p type=%u header64=%016llx low=%08x high=%08x zone=%u\n",
            static_cast<void *>(newEntry),
            static_cast<unsigned>(type),
            static_cast<unsigned long long>(headerValue),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue)),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue >> 32)),
            static_cast<unsigned>(newEntry->entry.zoneIndex));
        Switch_LogWrite(trace);
        g_switchDbStage = "asset/link_entry";
    }
    g_switchDbStage = "asset/type";
#endif
#ifdef __SWITCH__
    const bool switchTraceWeapon1506 =
        g_switchCurrentAssetIndex == 1506 &&
        g_switchCurrentAssetRawType == 23u &&
        type == ASSET_TYPE_WEAPON;
    if (switchTraceWeapon1506)
        Switch_LogWrite("[SWITCH WEAPON1506] DB_Link before DB_GetXAssetName\n");
    if (newEntry->entry.asset.type == ASSET_TYPE_TECHNIQUE_SET)
        Switch_LogWrite("[SWITCH TECHSET LINK] before DB_GetXAssetName\n");
    if (newEntry->entry.asset.type == ASSET_TYPE_IMAGE)
        Switch_LogWrite("[SWITCH IMAGE] DB_Link before DB_GetXAssetName\n");
#endif
#ifdef __SWITCH__
    g_switchDbStage = "asset/name";
    const bool switchTraceTechset4026 =
        type == ASSET_TYPE_TECHNIQUE_SET &&
        g_switchCurrentAssetIndex == 4026 &&
        g_switchCurrentAssetRawType == 5u;
    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_call";
    if (g_switchCurrentAssetIndex == 1126 &&
        g_switchCurrentAssetRawType == 31u)
    {
        const uint64_t headerValue =
            *reinterpret_cast<const uint64_t *>(&newEntry->entry.asset.header);
        char trace[384];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH RAWFILE] link pre-name asset=%d raw=%u type=%u "
            "entry=%p headerPtr=%p header64=%016llx low=%08x high=%08x\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(g_switchCurrentAssetRawType),
            static_cast<unsigned>(newEntry->entry.asset.type),
            static_cast<void *>(newEntry),
            static_cast<void *>(&newEntry->entry.asset.header),
            static_cast<unsigned long long>(headerValue),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue)),
            static_cast<unsigned>(static_cast<uint32_t>(headerValue >> 32)));
        Switch_LogWrite(trace);
        g_switchDbStage = "asset/name_rawfile";
    }
#endif
    if (newEntry->entry.asset.type == ASSET_TYPE_IMAGE)
        name = newEntry->entry.asset.header.image->name;
    else
        name = DB_GetXAssetName(&newEntry->entry.asset);
#ifdef __SWITCH__
    if (switchTraceTechset4026)
        g_switchDbStage = "asset/name_return";
#endif
#ifdef __SWITCH__
    if (type == ASSET_TYPE_FX &&
        g_switchCurrentAssetIndex >= 4505 &&
        g_switchCurrentAssetIndex <= 4510 &&
        g_switchCurrentAssetRawType == 25u)
    {
        char trace[256];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH FX TRACE] registry asset=%d type=%u header=%p name=%p\n",
            g_switchCurrentAssetIndex,
            static_cast<unsigned>(type),
            newEntry->entry.asset.header.data,
            static_cast<const void *>(name));
        Switch_LogWrite(trace);
    }
#endif
#ifdef __SWITCH__
    if (switchTraceWeapon1506)
    {
        char trace[224];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH WEAPON1506] DB_Link name=%p header=%p type=%u\n",
            static_cast<const void *>(name),
            static_cast<void *>(newEntry->entry.asset.header.data),
            static_cast<unsigned>(type));
        Switch_LogWrite(trace);
    }
#endif

#ifdef __SWITCH__
    if (newEntry->entry.asset.type == ASSET_TYPE_TECHNIQUE_SET)
    {