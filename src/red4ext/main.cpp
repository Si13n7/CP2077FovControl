#include <RED4ext/RED4ext.hpp>
#include <Windows.h>
#include <cstdint>
#include <cstring>

/// Native FOV utility exposed to Redscript.
/// Provides:
/// - FOV lock control via code patch
/// - Conversion between internal and display FOV values
struct FovControl : RED4ext::IScriptable
{
    /// Returns the RTTI type for FovControl.
    RED4ext::CClass *GetNativeType();
};

static RED4ext::TTypedClass<FovControl> s_type("FovControl");

RED4ext::CClass *FovControl::GetNativeType()
{
    return &s_type;
}

// Used to access RED4ext services (logger, etc.).
const RED4ext::Sdk *g_sdk = nullptr;
RED4ext::PluginHandle g_handle = nullptr;

namespace
{
    using namespace RED4ext;

    struct FovPair
    {
        float internalFov;
        float displayFov;
    };

    // Patched (locked) pattern.
    constexpr uint8_t kPatOn[] = {
        0x42, 0x08, 0x89, 0x41, 0x08, 0x0F, 0x10, 0x42,
        0x10, 0x0F, 0x11, 0x41, 0x10, 0x8B, 0x42, 0x20,
        0x90, 0x90, 0x90, 0x8B, 0x42, 0x24, 0x89, 0x41,
        0x24, 0x8B, 0x42, 0x28, 0x89, 0x41, 0x28, 0x8B};

    // Original (unlocked) pattern.
    constexpr uint8_t kPatOff[] = {
        0x42, 0x08, 0x89, 0x41, 0x08, 0x0F, 0x10, 0x42,
        0x10, 0x0F, 0x11, 0x41, 0x10, 0x8B, 0x42, 0x20,
        0x89, 0x41, 0x20, 0x8B, 0x42, 0x24, 0x89, 0x41,
        0x24, 0x8B, 0x42, 0x28, 0x89, 0x41, 0x28, 0x8B};

    constexpr size_t kLen = sizeof(kPatOff);
    constexpr size_t kDiff = 16;

    // Payloads for locked/unlocked state.
    constexpr uint8_t kOn[] = {0x90, 0x90, 0x90};
    constexpr uint8_t kOff[] = {0x89, 0x41, 0x20};

    // Resolved patch address (lazy init via EnsureTarget).
    uint8_t *g_patch = nullptr;

    // Global patch enable flag.
    // When false, no writes to the target location are performed.
    bool g_patchEnabled = true;

    // FOV mapping table (monotonic).
    constexpr FovPair fovTable[] = {
        {0.33750209f, 0.0f},
        {3.15171504f, 5.0f},
        {5.97414541f, 10.0f},
        {8.81220150f, 15.0f},
        {11.67340946f, 20.0f},
        {14.56548882f, 25.0f},
        {17.49640465f, 30.0f},
        {20.47442627f, 35.0f},
        {23.50821114f, 40.0f},
        {26.60686302f, 45.0f},
        {29.77999878f, 50.0f},
        {33.03782654f, 55.0f},
        {36.39122391f, 60.0f},
        {39.85181808f, 65.0f},
        {43.43202972f, 70.0f},
        {47.14517975f, 75.0f},
        {51.00551605f, 80.0f},
        {55.02826691f, 85.0f},
        {59.22966766f, 90.0f},
        {63.62687683f, 95.0f},
        {68.23798370f, 100.0f},
        {73.08179474f, 105.0f},
        {78.17757416f, 110.0f},
        {83.54468536f, 115.0f},
        {89.20195007f, 120.0f},
        {95.16699982f, 125.0f},
        {101.45520020f, 130.0f},
        {108.07840730f, 135.0f},
        {115.04350280f, 140.0f},
        {122.35076900f, 145.0f},
        {129.99212650f, 150.0f},
        {137.94955440f, 155.0f},
        {146.19392390f, 160.0f},
        {154.68443290f, 165.0f},
        {163.36904910f, 170.0f},
        {172.18605040f, 175.0f}};

    void LogError(const char* msg)
    {
        if (!g_sdk || !g_sdk->logger)
            return;
        g_sdk->logger->Error(g_handle, msg);
    }

    /// Locate the .text section of the main module.
    /// \param base Start address of .text on success.
    /// \param size Size of .text on success.
    /// \return true if found, false otherwise.
    bool GetTextSection(uint8_t *&base, size_t &size)
    {
        auto *module = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
        if (!module)
            return false;

        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(module);
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(module + dos->e_lfanew);
        auto *sec = IMAGE_FIRST_SECTION(nt);

        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        {
            if (std::memcmp(sec->Name, ".text", 5) == 0)
            {
                base = module + sec->VirtualAddress;
                size = sec->Misc.VirtualSize;
                return true;
            }
        }

        return false;
    }

    /// Scan the .text section for an exact byte pattern.
    /// \param pat Pattern bytes to search for.
    /// \return Address of first match, or nullptr if not found.
    uint8_t *FindPattern(const uint8_t *pat)
    {
        uint8_t *base = nullptr;
        size_t size = 0;

        if (!GetTextSection(base, size) || size < kLen)
            return nullptr;

        const auto *end = base + size - kLen;

        for (auto *cur = base; cur <= end; ++cur)
        {
            if (std::memcmp(cur, pat, kLen) == 0)
                return cur;
        }

        return nullptr;
    }

    /// Ensure that g_patch points to the patch location.
    /// Tries both original (off) and already-patched (on) patterns.
    /// \return true if resolved, false if pattern not found.
    bool EnsureTarget()
    {
        if (g_patch)
            return true;

        uint8_t *p = FindPattern(kPatOff);
        if (!p)
        {
            p = FindPattern(kPatOn);
            if (!p)
            {
                LogError("EnsureTarget: failed to locate patch pattern");
                return false;
            }
        }

        g_patch = p + kDiff;
        return true;
    }

    /// Write exactly 3 bytes at the patch location with proper protection.
    /// \param bytes Pointer to 3 bytes to write.
    /// \return true on success, false on failure/invalid target.
    bool Write3(const uint8_t *bytes)
    {
        if (!g_patchEnabled || !EnsureTarget())
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(g_patch, 3, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LogError("Write3: failed to change page protection for patching");
            return false;
        }

        std::memcpy(g_patch, bytes, 3);
        FlushInstructionCache(GetCurrentProcess(), g_patch, 3);

        DWORD tmp;
        VirtualProtect(g_patch, 3, oldProtect, &tmp);
        return true;
    }

    /// Apply lock state at the patch location.
    /// \param enable true to lock, false to unlock.
    /// \return true on success.
    bool ApplyPatch(bool enable)
    {
        return Write3(enable ? kOn : kOff);
    }

    /// Return whether patching operations are currently allowed.
    /// \return true if Lock/Unlock/Toggle may modify memory.
    bool IsPatchingAllowed()
    {
        return g_patchEnabled && EnsureTarget();
    }

    /// Prevent any future Lock/Unlock/Toggle from patching memory.
    /// \return true if patching is now prevented.
    bool PreventPatching()
    {
        g_patchEnabled = false;
        return true;
    }

    /// Allow Lock/Unlock/Toggle to patch memory again.
    /// \return true if patching is now allowed.
    bool ReleasePatching()
    {
        g_patchEnabled = true;
        return true;
    }

    /// Check whether the current patch bytes correspond to the locked state.
    /// \return true if locked, false if unlocked or unresolved.
    bool IsLocked()
    {
        if (!EnsureTarget())
            return false;

        return std::memcmp(g_patch, kOn, sizeof(kOn)) == 0;
    }

    /// Set lock state to the requested value (idempotent).
    /// \param enable true to lock, false to unlock.
    /// \return true if the resulting state matches the request and target is valid.
    bool SetLocked(bool enable)
    {
        if (!EnsureTarget())
            return false;

        const bool locked = IsLocked();
        if (enable == locked)
            return true;

        return ApplyPatch(enable);
    }

    /// Lock FOV.
    /// \return true if locked after call.
    bool Lock()
    {
        return SetLocked(true);
    }

    /// Unlock FOV.
    /// \return true if unlocked after call.
    bool Unlock()
    {
        return SetLocked(false);
    }

    /// Toggle FOV lock state.
    /// \return true if toggle succeeded.
    bool Toggle()
    {
        if (!EnsureTarget())
            return false;

        return ApplyPatch(!IsLocked());
    }

    /// Convert between internal and display FOV using piecewise linear interpolation.
    /// \param x Input FOV value.
    /// \param inverse false: internal -> display, true: display -> internal.
    /// \return Converted FOV value.
    float ConvertFormat(float x, bool inverse)
    {
        constexpr size_t n = sizeof(fovTable) / sizeof(fovTable[0]);
        if (n == 0)
            return x;

        // Select source axis (X) depending on direction
        const auto getX = [&](const FovPair &p) -> float
        {
            return inverse ? p.displayFov : p.internalFov;
        };

        // Select target axis (Y) depending on direction
        const auto getY = [&](const FovPair &p) -> float
        {
            return inverse ? p.internalFov : p.displayFov;
        };

        // Clamp: if outside table range, snap to edge
        const float xMin = getX(fovTable[0]);
        const float xMax = getX(fovTable[n - 1]);

        if (x <= xMin)
            return getY(fovTable[0]);
        if (x >= xMax)
            return getY(fovTable[n - 1]);

        // Find the segment [lo, hi] where x lies and interpolate
        for (size_t i = 1; i < n; ++i)
        {
            const auto &lo = fovTable[i - 1];
            const auto &hi = fovTable[i];

            const float xLo = getX(lo);
            const float xHi = getX(hi);

            if (x <= xHi)
            {
                const float t = (x - xLo) / (xHi - xLo); // normalized position in segment
                const float yLo = getY(lo);
                const float yHi = getY(hi);
                return yLo + t * (yHi - yLo); // linear interpolation on Y
            }
        }

        // Fallback (should not be reached due to clamping + monotonic table)
        return getY(fovTable[n - 1]);
    }

    /// Generic Redscript wrapper for static Bool() functions without parameters.
    /// \param ctx Script context (unused).
    /// \param frame Current call frame; advances code by one.
    /// \param out Destination for the bool result.
    /// \param a4 Unused ABI parameter.
    template <bool (*Fn)()>
    void RS_BoolNoArgs(IScriptable *, CStackFrame *frame, bool *out, int64_t)
    {
        frame->code++;
        if (out)
            *out = Fn();
    }

    /// Redscript wrapper for:
    ///   Float ConvertFormat(Float value, Bool inverse)
    /// \param ctx Script context (unused).
    /// \param frame Reads [value, inverse], advances code.
    /// \param out Destination for converted value.
    /// \param a4 Unused ABI parameter.
    void RS_ConvertFormat(IScriptable *, CStackFrame *frame, float *out, int64_t)
    {
        float value = 0.0f;
        bool inverse = false;

        GetParameter(frame, &value);
        GetParameter(frame, &inverse);
        frame->code++;

        if (out)
            *out = ConvertFormat(value, inverse);
    }

    /// Register FovControl RTTI type.
    void RegisterTypes()
    {
        CRTTISystem::Get()->RegisterType(&s_type);
    }

    /// Register all FovControl static natives.
    void PostRegisterTypes()
    {
        auto *rtti = CRTTISystem::Get();
        s_type.parent = rtti->GetClass("IScriptable");

        CBaseFunction::Flags flags = {.isNative = true, .isStatic = true};

        auto addBoolNoArgs = [&](const char *name, auto func)
        {
            auto *fn = CClassStaticFunction::Create(&s_type, name, name, func, flags);
            fn->SetReturnType("Bool");
            s_type.RegisterFunction(fn);
        };

        addBoolNoArgs("IsPatchingAllowed", &RS_BoolNoArgs<IsPatchingAllowed>);
        addBoolNoArgs("PreventPatching", &RS_BoolNoArgs<PreventPatching>);
        addBoolNoArgs("ReleasePatching", &RS_BoolNoArgs<ReleasePatching>);

        addBoolNoArgs("IsLocked", &RS_BoolNoArgs<IsLocked>);
        addBoolNoArgs("Lock", &RS_BoolNoArgs<Lock>);
        addBoolNoArgs("Unlock", &RS_BoolNoArgs<Unlock>);
        addBoolNoArgs("ToggleLock", &RS_BoolNoArgs<Toggle>);

        auto *cf = CClassStaticFunction::Create(&s_type, "ConvertFormat", "ConvertFormat", &RS_ConvertFormat, flags);
        cf->AddParam("Float", "value");
        cf->AddParam("Bool", "inverse");
        cf->SetReturnType("Float");
        s_type.RegisterFunction(cf);
    }
}

/// RED4ext plugin entry: register types and natives on load.
RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle handle, RED4ext::EMainReason reason, const RED4ext::Sdk *sdk)
{
    if (reason == RED4ext::EMainReason::Load)
    {
        g_sdk = sdk;
        g_handle = handle;
        RED4ext::RTTIRegistrator::Add(RegisterTypes, PostRegisterTypes);
    }
    return true;
}

/// Provide plugin metadata to RED4ext.
RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::PluginInfo *info)
{
    info->name = L"FovControl";
    info->author = L"Si13n7 Dev.\x99";
    info->version = RED4EXT_SEMVER(2, 31, 0);
    info->runtime = RED4EXT_RUNTIME_LATEST;
    info->sdk = RED4EXT_SDK_LATEST;
}

/// Report supported RED4ext API version.
RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_LATEST;
}
