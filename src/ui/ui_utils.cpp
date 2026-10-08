#include <universal/q_shared.h>
#include "ui_shared.h"
#include <qcommon/mem_track.h>
#include <qcommon/threads.h>
#include <universal/q_parse.h>
#include <universal/profile.h>
#ifdef __SWITCH__
#include <cstdio>
extern void Switch_LogWrite(const char *msg);
extern thread_local const char *g_switchFrameStage;
#endif

stringDef_s *g_strHandle[2048];

void __cdecl TRACK_ui_utils()
{
    track_static_alloc_internal(g_strHandle, 0x2000, "g_strHandle", 34);
}

void __cdecl Window_SetDynamicFlags(int localClientNum, windowDef_t *w, int flags)
{
    if (localClientNum)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            43,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    if (flags && (flags & 0xFFFFF) == 0)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 44, 0, "%s\n\t(flags) = %i", "(flags == 0 || flags & 0x000FFFFF)", flags);
    if ((flags & 0xFFF00000) != 0)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 45, 0, "%s", "!(flags & WINDOWSTATIC_CHECKMASK)");
    w->dynamicFlags[localClientNum] = flags;
}

void __cdecl Window_AddDynamicFlags(int localClientNum, windowDef_t *w, int newFlags)
{
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\ui\\ui_utils.h",
            23,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    Window_SetDynamicFlags(localClientNum, w, newFlags | w->dynamicFlags[localClientNum]);
}

void __cdecl Window_RemoveDynamicFlags(int localClientNum, windowDef_t *w, int newFlags)
{
    int modifiedFlags; // [esp+0h] [ebp-8h]

    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\ui\\ui_utils.h",
            23,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    modifiedFlags = newFlags;
    if ((newFlags & 4) != 0)
        modifiedFlags = newFlags | 2;
    Window_SetDynamicFlags(localClientNum, w, w->dynamicFlags[localClientNum] & ~modifiedFlags);
}

void __cdecl Window_SetStaticFlags(windowDef_t *w, int flags)
{
    if (flags && (flags & 0xFFF00000) == 0)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 82, 0, "%s\n\t(flags) = %i", "(flags == 0 || flags & 0xFFF00000)", flags);
    if ((flags & 0xFFFFF) != 0)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 83, 0, "%s", "!(flags & WINDOWDYNAMIC_CHECKMASK)");
    w->staticFlags = flags;
}

void __cdecl Menu_SetCursorItem(int localClientNum, menuDef_t *menu, int cursorItem)
{
    if (localClientNum)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            90,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    if (!menu)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 91, 0, "%s", "menu");
    menu->cursorItem[localClientNum] = cursorItem;
}

int __cdecl Item_IsVisible(int localClientNum, itemDef_s *item)
{
    PROF_SCOPED("Item_IsVisible");

#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/draw_field/loading_ui/item/visible/window";
#endif
    if (!Window_IsVisible(localClientNum, &item->window))
        return 0;
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/draw_field/loading_ui/item/visible/dvar";
#endif
    if ((item->dvarFlags & 0xC) != 0 && !Item_EnableShowViaDvar(item, 4))
        return 0;
#ifdef __SWITCH__
    g_switchFrameStage = "frame/scr/draw_field/loading_ui/item/visible/expr";
#endif
    if (!item->visibleExp.numEntries || IsExpressionTrue(localClientNum, &item->visibleExp))
        return 1;
    if (uiscript_debug->current.integer)
    {
        if (item->window.name)
            Com_Printf(CON_CHANNEL_UI, "Item %s is hidden because its 'visible when' expression is false\n", item->window.name);
        else
            Com_Printf(CON_CHANNEL_UI, "Item %s is hidden because its 'visible when' expression is false\n", "unnamed");
    }
    return 0;
}

bool __cdecl Item_EnableShowViaDvar(const itemDef_s *item, int flag)
{
#ifdef __SWITCH__
    static thread_local int switchTraceCount = 0;
    if (switchTraceCount < 8)
    {
        const uintptr_t enableValue = reinterpret_cast<uintptr_t>(item ? item->enableDvar : nullptr);
        const uintptr_t testValuePtr = reinterpret_cast<uintptr_t>(item ? item->dvarTest : nullptr);
        char trace[448];
        std::snprintf(
            trace,
            sizeof(trace),
            "[KisakCOD][FRAME] Item_EnableShowViaDvar entry item=%p flag=%d enableDvar=%p dvarTest=%p enableLow=%08x enableHigh=%08x testLow=%08x testHigh=%08x\n",
            static_cast<const void *>(item),
            flag,
            item ? static_cast<const void *>(item->enableDvar) : nullptr,
            item ? static_cast<const void *>(item->dvarTest) : nullptr,
            static_cast<unsigned>(enableValue & UINT64_C(0xffffffff)),
            static_cast<unsigned>(enableValue >> 32),
            static_cast<unsigned>(testValuePtr & UINT64_C(0xffffffff)),
            static_cast<unsigned>(testValuePtr >> 32));
        Switch_LogWrite(trace);
        ++switchTraceCount;
    }
#endif
    const char *testValue; // [esp+0h] [ebp-40Ch]
    char val[1024]; // [esp+4h] [ebp-408h] BYREF
    const char *p; // [esp+408h] [ebp-4h] BYREF

    if (!item)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 207, 0, "%s", "item");

#ifdef __SWITCH__
    auto badUiStringPointer = [](const char *ptr) -> bool
    {
        if (!ptr)
            return false;

        const uintptr_t value = reinterpret_cast<uintptr_t>(ptr);

        // Switch user pointers used by the engine are within the 39-bit user
        // address range. A 32-bit serialized pointer copied into a 64-bit field
        // can retain garbage in the upper half and otherwise look non-null.
        return value < UINT64_C(0x100000000) ||
               value >= (UINT64_C(1) << 39);
    };

    if (badUiStringPointer(item->enableDvar))
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH UI ABI] Item_EnableShowViaDvar invalid enableDvar=%p "
            "item=%p flag=%d low32=%08x high32=%08x\n",
            static_cast<const void *>(item->enableDvar),
            static_cast<const void *>(item),
            flag,
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(item->enableDvar) & UINT64_C(0xffffffff)),
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(item->enableDvar) >> 32));
        Switch_LogWrite(trace);
        return 1;
    }

    if (badUiStringPointer(item->dvarTest))
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH UI ABI] Item_EnableShowViaDvar invalid dvarTest=%p "
            "item=%p flag=%d low32=%08x high32=%08x\n",
            static_cast<const void *>(item->dvarTest),
            static_cast<const void *>(item),
            flag,
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(item->dvarTest) & UINT64_C(0xffffffff)),
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(item->dvarTest) >> 32));
        Switch_LogWrite(trace);
        return 1;
    }
#endif

    if (!item->enableDvar || !*item->enableDvar || !item->dvarTest || !*item->dvarTest)
        return 1;
    testValue = Dvar_GetVariantString(item->dvarTest);
#ifdef __SWITCH__
    {
        const uintptr_t value = reinterpret_cast<uintptr_t>(testValue);
        if (switchTraceCount < 8)
        {
            char trace[384];
            std::snprintf(
                trace,
                sizeof(trace),
                "[KisakCOD][FRAME] Item_EnableShowViaDvar result item=%p flag=%d testValue=%p low32=%08x high32=%08x\n",
                static_cast<const void *>(item),
                flag,
                static_cast<const void *>(testValue),
                static_cast<unsigned>(value & UINT64_C(0xffffffff)),
                static_cast<unsigned>(value >> 32));
            Switch_LogWrite(trace);
            ++switchTraceCount;
        }
    }
    if (badUiStringPointer(testValue))
    {
        char trace[320];
        std::snprintf(
            trace,
            sizeof(trace),
            "[SWITCH UI ABI] Item_EnableShowViaDvar invalid testValue=%p "
            "item=%p flag=%d dvarTest=%s low32=%08x high32=%08x\n",
            static_cast<const void *>(testValue),
            static_cast<const void *>(item),
            flag,
            item->dvarTest ? item->dvarTest : "<null>",
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(testValue) & UINT64_C(0xffffffff)),
            static_cast<unsigned>(
                reinterpret_cast<uintptr_t>(testValue) >> 32));
        Switch_LogWrite(trace);
        return 1;
    }
#endif
    p = item->enableDvar;
    do
    {
        if (!String_Parse(&p, val, 1024))
            return (flag & item->dvarFlags) == 0;
    } while (val[0] == 59 && !val[1] || I_stricmp(testValue, val));
    return (flag & item->dvarFlags) != 0;
}

void __cdecl Item_SetTextRect(int localClientNum, itemDef_s *item, const rectDef_s *textRect)
{
    rectDef_s *v3; // edx

    if (localClientNum)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            234,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    if (!item)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 235, 0, "%s", "item");
    if (!textRect)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 236, 0, "%s", "textRect");
    if (textRect->horzAlign >= 8u)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            237,
            0,
            "%s\n\t(textRect->horzAlign) = %i",
            "(textRect->horzAlign >= 0 && textRect->horzAlign <= 7)",
            textRect->horzAlign);
    if (textRect->vertAlign >= 8u)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            238,
            0,
            "%s\n\t(textRect->vertAlign) = %i",
            "(textRect->vertAlign >= 0 && textRect->vertAlign <= 7)",
            textRect->vertAlign);
    v3 = &item->textRect[localClientNum];
    v3->x = textRect->x;
    v3->y = textRect->y;
    v3->w = textRect->w;
    v3->h = textRect->h;
    v3->horzAlign = textRect->horzAlign;
    v3->vertAlign = textRect->vertAlign;
}

int __cdecl Item_GetCursorPosOffset(int localClientNum, const itemDef_s *item, const char *text, int delta)
{
    int pos; // [esp+0h] [ebp-4h]

    if (localClientNum)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            254,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    pos = item->cursorPos[localClientNum];
    if (delta > 0)
    {
        while (1)
        {
            while (&text[pos]
                && text[pos] == 94
                    && text[pos + 1]
                    && text[pos + 1] != 94
                    && text[pos + 1] >= 48
                    && text[pos + 1] <= 57)
                pos += 2;
            if (!text[pos] || !delta)
                break;
            ++pos;
            --delta;
        }
    }
    else
    {
        while (pos && delta)
        {
            if (pos < 2
                || &text[pos] == (const char *)2
                || text[pos - 2] != 94
                || !text[pos - 1]
                || text[pos - 1] == 94
                || text[pos - 1] < 48
                || text[pos - 1] > 57)
            {
                --pos;
                ++delta;
            }
            else
            {
                pos -= 2;
            }
        }
    }
    return pos;
}

bool __cdecl ListBox_HasValidCursorPos(int localClientNum, itemDef_s *item)
{
    const listBoxDef_s *listPtr; // [esp+4h] [ebp-8h]
    int cursorPos; // [esp+8h] [ebp-4h]

    if (localClientNum)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            295,
            0,
            "localClientNum doesn't index MAX_POSSIBLE_LOCAL_CLIENTS\n\t%i not in [0, %i)",
            localClientNum,
            1);
    listPtr = Item_GetListBoxDef(item);
    cursorPos = item->cursorPos[localClientNum];
    return cursorPos >= listPtr->startPos[localClientNum] && cursorPos < listPtr->endPos[localClientNum];
}

void __cdecl Menu_UpdatePosition(int localClientNum, menuDef_t *menu)
{
    int i; // [esp+10h] [ebp-10h]
    float x; // [esp+14h] [ebp-Ch]
    float y; // [esp+18h] [ebp-8h]

    if (menu)
    {
        x = menu->window.rect.x;
        y = menu->window.rect.y;
        if (menu->window.border)
        {
            x = x + menu->window.borderSize;
            y = y + menu->window.borderSize;
        }
        for (i = 0; i < menu->itemCount; ++i)
            Item_SetScreenCoords(
                localClientNum,
                menu->items[i],
                x,
                y,
                menu->window.rect.horzAlign,
                menu->window.rect.vertAlign);
    }
}

void __cdecl Item_SetScreenCoords(int localClientNum, itemDef_s *item, float x, float y, int horzAlign, int vertAlign)
{
    rectDef_s newRect; // [esp+0h] [ebp-1Ch] BYREF
    const rectDef_s *textRect; // [esp+18h] [ebp-4h]

    if (item)
    {
        if (item->window.border)
        {
            x = x + item->window.borderSize;
            y = y + item->window.borderSize;
        }
        item->window.rect = item->window.rectClient;
        item->window.rect.x = item->window.rect.x + x;
        item->window.rect.y = item->window.rect.y + y;
        if (!item->window.rect.horzAlign && !item->window.rect.vertAlign)
        {
            item->window.rect.horzAlign = horzAlign;
            item->window.rect.vertAlign = vertAlign;
        }
        textRect = Item_GetTextRect(localClientNum, item);
        newRect = *textRect;
        newRect.w = 0.0;
        newRect.h = 0.0;
        Item_SetTextRect(localClientNum, item, &newRect);
    }
    else
    {
        MyAssertHandler(".\\ui\\ui_utils.cpp", 310, 0, "%s", "item");
    }
}

int __cdecl Item_IsEditFieldDef(itemDef_s *item)
{
    int result; // eax

    switch (item->dataType)
    {
    case 0:
    case 4:
    case 9:
    case 0xA:
    case 0xB:
    case 0xE:
    case 0x10:
    case 0x11:
    case 0x12:
        result = 1;
        break;
    default:
        result = 0;
        break;
    }
    return result;
}

listBoxDef_s *__cdecl Item_GetListBoxDef(itemDef_s *item)
{
    if (item->dataType == 6)
        return item->typeData.listBox;
    Com_PrintError(CON_CHANNEL_UI, "Menu Error: Expecting type: ITEM_TYPE_LISTBOX\n");
    return 0;
}

editFieldDef_s *__cdecl Item_GetEditFieldDef(itemDef_s *item)
{
    if (Item_IsEditFieldDef(item))
        return item->typeData.editField;
    Com_PrintError(
        CON_CHANNEL_UI,
        "Menu Error: Expecting type: ITEM_TYPE_EDITFIELD, ITEM_TYPE_NUMERICFIELD, ITEM_TYPE_DECIMALFIELD, ITEM_TYPE_VALIDFILE"
        "FIELD, ITEM_TYPE_UPREDITFIELD, ITEM_TYPE_YESNO, ITEM_TYPE_BIND, ITEM_TYPE_SLIDER, or ITEM_TYPE_TEXT\n");
    return 0;
}

multiDef_s *__cdecl Item_GetMultiDef(itemDef_s *item)
{
    if (!item)
        MyAssertHandler(".\\ui\\ui_utils.cpp", 408, 0, "%s", "item");
    if (item->dataType != 12)
        MyAssertHandler(
            ".\\ui\\ui_utils.cpp",
            409,
            0,
            "%s\n\t(item->dataType) = %i",
            "(item->dataType == 12)",
            item->dataType);
    return item->typeData.multi;
}

uint8_t *__cdecl UI_Alloc(uint32_t size, int alignment)
{
    return Hunk_AllocAlign(size, alignment, "UI_Alloc", 34);
}

void __cdecl String_Init()
{
    memset((uint8_t *)g_strHandle, 0, sizeof(g_strHandle));
}

const char *staticNULL = "";
const char *__cdecl String_Alloc(const char *p)
{
    char v2; // [esp+3h] [ebp-45h]
    uint8_t *v3; // [esp+8h] [ebp-40h]
    const char *v4; // [esp+Ch] [ebp-3Ch]
    stringDef_s *str; // [esp+34h] [ebp-14h]
    stringDef_s *stra; // [esp+34h] [ebp-14h]
    stringDef_s *strb; // [esp+34h] [ebp-14h]
    int hash; // [esp+38h] [ebp-10h]
    uint8_t *s; // [esp+40h] [ebp-8h]
    stringDef_s *last; // [esp+44h] [ebp-4h]

    if (!Sys_IsMainThread())
        MyAssertHandler(".\\ui\\ui_utils.cpp", 458, 0, "%s", "Sys_IsMainThread()");
    if (!p)
        return 0;
    if (!*p)
        return staticNULL;
    hash = hashForString(p);
    for (str = g_strHandle[hash]; str; str = str->next)
    {
        if (!str->str)
            MyAssertHandler(".\\ui\\ui_utils.cpp", 475, 0, "%s", "str->str");
        if (!strcmp(p, str->str))
            return str->str;
    }
    s = UI_Alloc(strlen(p) + 1, 1);
    v4 = p;
    v3 = s;
    do
    {
        v2 = *v4;
        *v3++ = *v4++;
    } while (v2);
    stra = g_strHandle[hash];
    last = stra;
    while (stra && stra->next)
    {
        last = stra;
        stra = stra->next;
    }
    strb = reinterpret_cast<stringDef_s *>(
        UI_Alloc(sizeof(stringDef_s), static_cast<int>(alignof(stringDef_s))));
    strb->next = 0;
    strb->str = (const char *)s;
    if (last)
        last->next = strb;
    else
        g_strHandle[hash] = strb;
    return (const char *)s;
}

int __cdecl hashForString(const char *str)
{
    int hash; // [esp+0h] [ebp-Ch]
    int i; // [esp+8h] [ebp-4h]

    hash = 0;
    for (i = 0; str[i]; ++i)
        hash += tolower(str[i]) * (i + 119);
    return hash & 0x7FF;
}

int __cdecl Int_Parse(const char **p, int *i)
{
    parseInfo_t *token; // [esp+0h] [ebp-4h]

    token = Com_ParseOnLine(p);
    if (!token || !token->token[0])
        return 0;
    *i = atoi(token->token);
    return 1;
}

int __cdecl Float_Parse(const char **p, float *f)
{
    parseInfo_t *token; // [esp+0h] [ebp-4h]

    token = Com_ParseOnLine(p);
    if (!token || !token->token[0])
        return 0;
    *f = atof(token->token);
    return 1;
}

