#include "Misc/Debug/MDebugHelper.h"

#if WITH_EDITOR
#include "UnrealEd.h"
#endif

#ifdef M_DEBUG_ENABLED
namespace
{
    bool GDebugAll = false;
    int32 GDebugHand = -1;
    bool GDebugCategories[static_cast<uint8>(EMDebug::Count)] = {};

    FAutoConsoleVariableRef CVarDebugAll(
        TEXT("p1.Debug.All"),
        GDebugAll,
        TEXT("Enable every p1.Debug category.\n0 = Off, 1 = On"),
        ECVF_Cheat
    );

    FAutoConsoleVariableRef CVarDebugHand(
        TEXT("p1.Debug.Hand"),
        GDebugHand,
        TEXT("Filter hand debug draws.\n-1 = Both, 0 = Right, 1 = Left"),
        ECVF_Cheat
    );

#define M_DEBUG_CVAR(Name, CVar, Help) \
    FAutoConsoleVariableRef CVarDebug##Name( \
        TEXT("p1.Debug." CVar), \
        GDebugCategories[static_cast<uint8>(EMDebug::Name)], \
        TEXT(Help "\n0 = Off, 1 = On"), \
        ECVF_Cheat \
    );
    M_DEBUG_CATEGORIES(M_DEBUG_CVAR)
#undef M_DEBUG_CVAR
}
#endif

bool MDebugHelper::IsEnabled(EMDebug Category)
{
#ifdef M_DEBUG_ENABLED
    return GDebugAll || GDebugCategories[static_cast<uint8>(Category)];
#else
    return false;
#endif
}

bool MDebugHelper::IsEnabled(EMDebug Category, int32 HandIndex)
{
#ifdef M_DEBUG_ENABLED
    return IsEnabled(Category) && (GDebugHand < 0 || GDebugHand == HandIndex);
#else
    return false;
#endif
}

void MDebugHelper::PausePIE()
{
#if WITH_EDITOR
    GUnrealEd->PlayWorld->bDebugPauseExecution = true;
#endif
}
