#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR || UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
#define M_DEBUG_ENABLED
#endif

/**
 * Debug draw categories. Each one registers a "p1.Debug.<CVar>" console variable (off by default).
 * X(Name, CVar, Help)
 */
#define M_DEBUG_CATEGORIES(X) \
	X(HandTrace,     "Char.HandTrace",     "Hand reach trace, sweep capsules and input buffer grabs.") \
	X(ArmConstraint, "Char.ArmConstraint", "Arm length, spring and stretch vectors and root fix.") \
	X(HandSlip,      "Char.HandSlip",      "Hand slip direction and slip target validation.") \
	X(HandMove,      "Char.HandMove",      "Mouse hand input and grab location moves.") \
	X(HandSpace,     "Char.HandSpace",     "Hand to world transform gizmos.") \
	X(Camera,        "Char.Camera",        "Body vs camera yaw lock and camera forward.") \
	X(FallDeath,     "Char.FallDeath",     "Fall to death timer and falling velocity.") \
	X(HandForces,    "CMC.HandForces",     "Per hand climbing accelerations, arm spring/stretch and slip acceleration.") \
	X(Velocity,      "CMC.Velocity",       "Climbing move direction and velocity delta.") \
	X(WallPull,      "CMC.WallPull",       "Push/pull towards wall decision.") \
	X(HelperSpring,  "CMC.HelperSpring",   "Helper spring direction and intensity.") \
	X(GrabbedObject, "CMC.GrabbedObject",  "Grabbed physics object forces and let go.") \
	X(Ragdoll,       "Camera.Ragdoll",     "Death cam look direction towards the ragdoll.")

enum class EMDebug : uint8
{
#define M_DEBUG_ENUM_ENTRY(Name, CVar, Help) Name,
	M_DEBUG_CATEGORIES(M_DEBUG_ENUM_ENTRY)
#undef M_DEBUG_ENUM_ENTRY
	Count
};

class PROTOTYPE1_API MDebugHelper
{
public:

	// True if the category CVar (or p1.Debug.All) is on.
	static bool IsEnabled(EMDebug Category);

	// Same as above, also filtered by p1.Debug.Hand.
	static bool IsEnabled(EMDebug Category, int32 HandIndex);

	static void PausePIE();
};

/**
 * Gates a debug block behind its category, e.g:
 *	M_DEBUG_HAND(HandTrace, HandData.HandIndex)
 *	{
 *		DrawDebugLine(...);
 *	}
 * Arguments are not evaluated when the category is off, and the block is compiled out when M_DEBUG_ENABLED is not defined.
 */
#ifdef M_DEBUG_ENABLED
#define M_DEBUG(Category) if (MDebugHelper::IsEnabled(EMDebug::Category))
#define M_DEBUG_HAND(Category, HandIndex) if (MDebugHelper::IsEnabled(EMDebug::Category, HandIndex))
#else
#define M_DEBUG(Category) if constexpr (false)
#define M_DEBUG_HAND(Category, HandIndex) if constexpr (false)
#endif
