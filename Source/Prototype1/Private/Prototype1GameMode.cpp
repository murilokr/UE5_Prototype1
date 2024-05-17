// Copyright Epic Games, Inc. All Rights Reserved.

#include "Prototype1GameMode.h"
#include "Prototype1Character.h"
#include "UObject/ConstructorHelpers.h"

APrototype1GameMode::APrototype1GameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}
