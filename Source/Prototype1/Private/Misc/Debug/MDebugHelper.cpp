#include "Misc/Debug/MDebugHelper.h"

#if WITH_EDITOR
#include "UnrealEd.h"
#endif

TAutoConsoleVariable<int32> MDebugHelper::CVarDrawHandTraceDebug(
    TEXT("r.Debug.DrawHandTraceDebug"),
    0,
    TEXT("Toggle debug hand trace drawing.\n0 = Off, 1 = On"),
    ECVF_Cheat
);

bool MDebugHelper::ShouldDrawTraceDebug()
{
#ifdef M_DEBUG_ENABLED
    return CVarDrawHandTraceDebug.GetValueOnGameThread() == 1;
#elif
    return false;
#endif
}

void MDebugHelper::PausePIE()
{
#if WITH_EDITOR
    GUnrealEd->PlayWorld->bDebugPauseExecution = true;
#endif
}
