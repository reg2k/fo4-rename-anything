#include <shlobj.h>
#include "f4se_common/SafeWrite.h"

#include "f4se/PluginAPI.h"
#include "f4se/GameReferences.h"
#include "f4se/GameExtraData.h"
#include "f4se/PapyrusNativeFunctions.h"

#include "f4se/ScaleformValue.h"
#include "f4se/ScaleformCallbacks.h"

#include "f4se/ObScript.h"

#include "Config.h"
#include "rva/RVA.h"

IDebugLog gLog;
PluginHandle g_pluginHandle = kPluginHandle_Invalid;

F4SEScaleformInterface *g_scaleform = NULL;
F4SEPapyrusInterface   *g_papyrus   = NULL;

//--------------------
// Addresses [5]
//--------------------

RVA <uintptr_t> InspectMode_Check({
    { RUNTIME_VERSION_1_10_75, 0x00B18861 },
    { RUNTIME_VERSION_1_10_64, 0x00B18861 },
    { RUNTIME_VERSION_1_10_40, 0x00B18861 },
    { RUNTIME_VERSION_1_10_26, 0x00B18851 },
    { RUNTIME_VERSION_1_10_20, 0x00B18831 },
}, "0F 85 ? ? ? ? 41 80 BE ? ? ? ? ? 0F 85 ? ? ? ? 41 80 BE ? ? ? ? ?");

RVA <uintptr_t> HTMLEntity_Check({
    { RUNTIME_VERSION_1_10_75, 0x00B189B7 },
    { RUNTIME_VERSION_1_10_64, 0x00B189B7 },
    { RUNTIME_VERSION_1_10_40, 0x00B189B7 },
    { RUNTIME_VERSION_1_10_26, 0x00B189A7 },
    { RUNTIME_VERSION_1_10_20, 0x00B18987 },
}, "75 04 B1 01 EB 08");

typedef bool(*_Console_GetArgument)(void * paramInfo, void * scriptData, void * opcodeOffsetPtr, TESObjectREFR * thisObj, void * containingObj, void * scriptObj, void * locals, ...);
RVA <_Console_GetArgument> Console_GetArgument({
    { RUNTIME_VERSION_1_10_75, 0x004E3650 },
    { RUNTIME_VERSION_1_10_64, 0x004E3650 },
    { RUNTIME_VERSION_1_10_40, 0x004E3650 },
    { RUNTIME_VERSION_1_10_26, 0x004E3630 },
    { RUNTIME_VERSION_1_10_20, 0x004E3640 },
}, "4C 89 4C 24 ? 48 89 4C 24 ? 55 41 56");

typedef void(*_ExtraTextDisplayData_SetName)(ExtraTextDisplayData* extraTextDisplayData, const char* newName);
RVA <_ExtraTextDisplayData_SetName> ExtraTextDisplayData_SetName({
    { RUNTIME_VERSION_1_10_75, 0x000C0B30 },
    { RUNTIME_VERSION_1_10_64, 0x000C0B30 },
    { RUNTIME_VERSION_1_10_40, 0x000C0B30 },
    { RUNTIME_VERSION_1_10_26, 0x000C0B30 },
    { RUNTIME_VERSION_1_10_20, 0x000C0B30 },
}, "48 89 5C 24 ? 57 48 83 EC 20 48 83 79 ? ? 48 8B FA 48 8B D9 75 2D");

typedef void * (*_ExtraTextDisplayData_Create)(ExtraTextDisplayData* memory);
RVA <_ExtraTextDisplayData_Create> ExtraTextDisplayData_Create({
    { RUNTIME_VERSION_1_10_75, 0x000AA210 },
    { RUNTIME_VERSION_1_10_64, 0x000AA210 },
    { RUNTIME_VERSION_1_10_40, 0x000AA210 },
    { RUNTIME_VERSION_1_10_26, 0x000AA210 },
    { RUNTIME_VERSION_1_10_20, 0x000AA210 },
}, "48 89 5C 24 ? 57 48 83 EC 20 33 FF 48 8D 05 ? ? ? ? 48 8B D9 C6 41 12 99");

//--------------------
// Rename Anything
//--------------------

bool RenameReference(TESObjectREFR* akRef, const char* asName) {
    if (!akRef) return false;
    ExtraTextDisplayData* extraData = (ExtraTextDisplayData*)akRef->extraDataList->GetByType(ExtraDataType::kExtraData_TextDisplayData);
    if (strcmp(asName, "") != 0) {
        if (!extraData) {
            extraData = (ExtraTextDisplayData*)Heap_Allocate(sizeof(ExtraTextDisplayData));
            ExtraTextDisplayData_Create(extraData);
            akRef->extraDataList->Add(ExtraDataType::kExtraData_TextDisplayData, extraData);
        }
        ExtraTextDisplayData_SetName(extraData, asName);
    } else {
        if (extraData) {
            // Empty string - remove name override.
            akRef->extraDataList->Remove(ExtraDataType::kExtraData_TextDisplayData, extraData);
        }
    }
    return true;
}

bool SetName_Execute(void * paramInfo, void * scriptData, TESObjectREFR * thisObj, void * containingObj, void * scriptObj, void * locals, double * result, void * opcodeOffsetPtr)
{
    if (thisObj) {
        char newName[MAX_PATH] = {};
        bool result = Console_GetArgument(paramInfo, scriptData, opcodeOffsetPtr, thisObj, containingObj, scriptObj, locals, &newName);

        if (result) {
            _MESSAGE("New name: %s", newName);

            BSFixedString originalName = CALL_MEMBER_FN(thisObj, GetReferenceName)();
            RenameReference(thisObj, &newName[0]);
            BSFixedString renamedName = CALL_MEMBER_FN(thisObj, GetReferenceName)();

            const char* originalNameStr = originalName.c_str();
            const char* renamedNameStr  = renamedName.c_str();

            if (!newName) {
                Console_Print("RenameAnything >> Name reset to default.");
            } else if (originalName == renamedName) {
                Console_Print("RenameAnything >> %s was not renamed.", renamedNameStr);
            } else {
                Console_Print("RenameAnything >> %s renamed to %s.", originalNameStr, renamedNameStr);
            }
        } else {
            _MESSAGE("Could not extract argument from command.");
        }
    } else {
        Console_Print("No reference selected.");
    }

    return true;
}

//-----------------------
// Scaleform Functions
//-----------------------

class F4SEScaleform_OnEnableRename : public GFxFunctionHandler
{
public:
    virtual void F4SEScaleform_OnEnableRename::Invoke(Args* args)
    {
        bool result = args->movie->movieRoot->SetVariable("root.BaseInstance.allowRename", &GFxValue(true));
        _MESSAGE("Rename enabled: %d", result);
    }
};

bool RegisterScaleform(GFxMovieView * view, GFxValue * f4se_root)
{
    GFxMovieRoot *root = view->movieRoot;

    GFxValue currentSWFPath;
    const char* currentSWFPathString = nullptr;
    if (root->GetVariable(&currentSWFPath, "root.loaderInfo.url")) {
        currentSWFPathString = currentSWFPath.GetString();
    } else {
        _MESSAGE("WARNING: Scaleform registration failed.");
        return true;
    }

    if (strcmp(currentSWFPathString, "Interface/ExamineMenu.swf") == 0) {
        _MESSAGE("ExamineMenu opened");

        // Create BGSCodeObj.EnableRename();
        GFxValue codeObj;
        root->GetVariable(&codeObj, "root.BaseInstance.BGSCodeObj");

        if (!codeObj.IsUndefined()) {
            RegisterFunction<F4SEScaleform_OnEnableRename>(&codeObj, root, "EnableRename");
            _MESSAGE("Successfully registered scaleform native.");
        } else {
            _MESSAGE("Scaleform native registration failed.");
        }
    }

    return true;
}

//-----------------------
// Papyrus Functions
//-----------------------

namespace PapyrusR2K {
    bool SetRefName(StaticFunctionTag* base, TESObjectREFR* akRef, BSFixedString asName, bool abForce) {
        if (!akRef) return false;
        if (abForce) RenameReference(akRef, "");
        return RenameReference(akRef, asName.c_str());
    }
    BSFixedString GetRefName(StaticFunctionTag* base, TESObjectREFR* akRef) {
        if (!akRef) return BSFixedString();
        return CALL_MEMBER_FN(akRef, GetReferenceName)();
    }
}

bool RegisterPapyrus(VirtualMachine *vm) {
    vm->RegisterFunction(new NativeFunction3<StaticFunctionTag, bool, TESObjectREFR*, BSFixedString, bool>("SetRefName", "RenameAnything", PapyrusR2K::SetRefName, vm));
    vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, BSFixedString, TESObjectREFR*>("GetRefName", "RenameAnything", PapyrusR2K::GetRefName, vm));

    _MESSAGE("Registered Papyrus native functions.");

    return true;
}

extern "C"
{

bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)
{
    char logPath[MAX_PATH];
    sprintf_s(logPath, sizeof(logPath), "\\My Games\\Fallout4\\F4SE\\%s.log", PLUGIN_NAME_SHORT);
    gLog.OpenRelative(CSIDL_MYDOCUMENTS, logPath);

    _MESSAGE("%s v%s", PLUGIN_NAME_SHORT, PLUGIN_VERSION_STRING);
    _MESSAGE("%s query", PLUGIN_NAME_SHORT);

    // populate info structure
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name    = PLUGIN_NAME_SHORT;
    info->version = PLUGIN_VERSION;

    // store plugin handle so we can identify ourselves later
    g_pluginHandle = f4se->GetPluginHandle();

    // Check game version
    if (!COMPATIBLE(f4se->runtimeVersion)) {
        char str[512];
        sprintf_s(str, sizeof(str), "Your game version: v%d.%d.%d.%d\nExpected version: v%d.%d.%d.%d\n%s will be disabled.",
            GET_EXE_VERSION_MAJOR(f4se->runtimeVersion),
            GET_EXE_VERSION_MINOR(f4se->runtimeVersion),
            GET_EXE_VERSION_BUILD(f4se->runtimeVersion),
            GET_EXE_VERSION_SUB(f4se->runtimeVersion),
            GET_EXE_VERSION_MAJOR(SUPPORTED_RUNTIME_VERSION),
            GET_EXE_VERSION_MINOR(SUPPORTED_RUNTIME_VERSION),
            GET_EXE_VERSION_BUILD(SUPPORTED_RUNTIME_VERSION),
            GET_EXE_VERSION_SUB(SUPPORTED_RUNTIME_VERSION),
            PLUGIN_NAME_LONG
        );

        MessageBox(NULL, str, PLUGIN_NAME_LONG, MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    if (f4se->runtimeVersion > SUPPORTED_RUNTIME_VERSION) {
        _MESSAGE("INFO: Newer game version (%08X) than target (%08X).", f4se->runtimeVersion, SUPPORTED_RUNTIME_VERSION);
    }

    // get the scaleform interface and query its version
    g_scaleform = (F4SEScaleformInterface *)f4se->QueryInterface(kInterface_Scaleform);
    if(!g_scaleform)
    {
        _MESSAGE("couldn't get scaleform interface");
        return false;
    }

    // get the papyrus interface and query its version
    g_papyrus = (F4SEPapyrusInterface *)f4se->QueryInterface(kInterface_Papyrus);
    if (!g_papyrus) {
        _MESSAGE("couldn't get papyrus interface");
        return false;
    }

    return true;
}

bool F4SEPlugin_Load(const F4SEInterface *f4se)
{
    _MESSAGE("rename_anything load");
    RVAManager::UpdateAddresses(f4se->runtimeVersion);
    g_scaleform->Register("rename_anything", RegisterScaleform);
    g_papyrus->Register(RegisterPapyrus);

    // patch memory
    unsigned char data[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    SafeWriteBuf(InspectMode_Check.GetUIntPtr(), &data, sizeof(data));

    // Read plugin settings from INI
    int bAllowHTML = GetPrivateProfileInt("RenameAnything", "bAllowHTML", 1, "./Data/F4SE/Plugins/rename_anything.ini");
    if (bAllowHTML == 1) {
        // patch html entity check
        // patch JNE with JMP unconditional
        UInt8 data2 = 0xEB;
        SafeWrite8(HTMLEntity_Check.GetUIntPtr(), data2);
        _MESSAGE("bAllowHTML: %d", bAllowHTML);
    }

    ObScriptParam* CommandParams = nullptr;
    // Find params
    for (ObScriptCommand* iter = g_firstConsoleCommand; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); iter++) {
        if (strcmp(iter->longName, "ToggleLODLand") == 0) {
            CommandParams = iter->params;
            break;
        }
    }
    // Install console command
    for (ObScriptCommand* iter = g_firstConsoleCommand; iter->opcode < (kObScript_NumConsoleCommands + kObScript_ConsoleOpBase); iter++) {
        if (strcmp(iter->longName, "RemoveWatchAddress") == 0) {
            iter->longName      = "SetName";
            iter->shortName     = "";
            iter->helpText      = "";
            iter->needsParent   = 0;
            iter->numParams     = 1;
            iter->execute       = SetName_Execute;
            iter->flags         = 0;
            iter->params        = CommandParams;
            break;
        }
    }

    _MESSAGE("patch complete");

    return true;
}

};
