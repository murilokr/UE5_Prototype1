

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR || UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#define M_DEBUG_ENABLED
#endif

/**
 * TODO: Add a DEBUG define to further optimize this.
 */
class PROTOTYPE1_API MDebugHelper
{
public:
	
	static TAutoConsoleVariable<int32> CVarDrawHandTraceDebug;
	static bool ShouldDrawTraceDebug();

};
