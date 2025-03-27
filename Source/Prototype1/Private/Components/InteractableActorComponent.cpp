#include "Components/InteractableActorComponent.h"


// Sets default values for this component's properties
UInteractableActorComponent::UInteractableActorComponent()
{
	// TODO: We might want to enable this in the future for some framerate dependent behavior.
	PrimaryComponentTick.bCanEverTick = false;
}

UInteractableActorComponent* UInteractableActorComponent::GetComponentFromActor(AActor* InActor)
{
	if (UInteractableActorComponent* InteractableActorComponent = InActor->GetComponentByClass<UInteractableActorComponent>())
	{
		return InteractableActorComponent;
	}
	return nullptr;
}

bool UInteractableActorComponent::IsActorInteractable(AActor* InActor, bool IsDefaultInteractable)
{
	if (UInteractableActorComponent* InteractableActorComponent = UInteractableActorComponent::GetComponentFromActor(InActor))
	{
		return InteractableActorComponent->IsClimbable() || InteractableActorComponent->IsGrabbable() || InteractableActorComponent->IsUsable();
	}

	return IsDefaultInteractable;
}


// Called when the game starts
void UInteractableActorComponent::BeginPlay()
{
	Super::BeginPlay();
}

#if WITH_EDITOR
void UInteractableActorComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Look for changed properties, if null assume all properties might have changed.
	const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;


	if (PropertyName == GET_MEMBER_NAME_CHECKED(UInteractableActorComponent, InteractType) || PropertyName == NAME_None)
	{
		USceneComponent* SceneComponent = GetOwner()->GetRootComponent();
		if (InteractType == EInteractType::INT_Grabbable)
		{
			if (SceneComponent->Mobility != EComponentMobility::Movable || !SceneComponent->IsSimulatingPhysics())
			{
				UE_LOG(LogTemp, Warning, TEXT("InteractableActor Interact type set as Grabbable, but RootComponent is static or is not simulating physics. Defining as InteractType None"));
				InteractType = EInteractType::INT_None;
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

