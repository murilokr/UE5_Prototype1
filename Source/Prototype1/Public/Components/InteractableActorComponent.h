#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractableActorComponent.generated.h"

UENUM(BlueprintType)
enum EInteractType
{
	INT_None = 0		UMETA(DisplayName = "Interact Type - None"),
	INT_Climbable = 1	UMETA(DisplayName = "Interact Type - Climbable"),
	INT_Grabbable = 2	UMETA(DisplayName = "Interact Type - Grabbable"),
	INT_Use = 3			UMETA(DisplayName = "Interact Type - Usable"),
	INT_MAXCOUNT = 4	UMETA(Hidden),
};

// TODO: Add a DataAsset for Surface Properties
// Friction
// Slippery
// OnGrabEffect
// OnReleaseEffect
// UV_RT
//

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROTOTYPE1_API UInteractableActorComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UInteractableActorComponent();

	static UInteractableActorComponent* GetComponentFromActor(AActor* InActor);
	static bool IsActorInteractable(AActor* InActor, bool IsDefaultInteractable = false);

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	bool IsClimbable() const { return InteractType == EInteractType::INT_Climbable; }
	bool IsGrabbable() const { return InteractType == EInteractType::INT_Grabbable; }
	bool IsUsable() const { return InteractType == EInteractType::INT_Use; }

	// The type of interaction that this actor is defined by.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "General Properties")
	TEnumAsByte<EInteractType> InteractType = EInteractType::INT_Climbable;

	// If this is greater than 0, then anytime the player tries to grab this surface, their hands will slip according to this value.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Surface Properties")
	float SlipperyValue = 0.0f;

	// The Friction value determines the friction of the surface, this affects the slip mechanic.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Surface Properties")
	float FrictionValue = 1.0f;
};
