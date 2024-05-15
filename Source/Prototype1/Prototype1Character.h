// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "Prototype1Character.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);


USTRUCT(BlueprintType)
struct FHandsContextData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	bool IsGrabbing;

	// Maybe we'll remove this.
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	float GrabPositionT;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere)
	FHitResult HitResult;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHitLocation;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
	FVector LocalHitNormal;

	// Grab Target Position.
	FVector GetGrabPosition(const FVector TraceStart, const FVector TraceDir);

	// Hand Location
	FVector GetHitLocation();

	FVector GetHitNormal();

	FRotator GetHandRotation();
};


UCLASS(config=Game)
class APrototype1Character : public ACharacter
{
	GENERATED_BODY()

public:

	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Grab Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* GrabActionL;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputAction* GrabActionR;
	
	APrototype1Character();

	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/** Hand Utility Functions */
	// Hand Location
	UFUNCTION(BlueprintPure)
	const FVector GetHitLocation(int HandIndex);

	// Hand Normal
	UFUNCTION(BlueprintPure)
	const FVector GetHitNormal(int HandIndex);

	// Hand Rotation
	UFUNCTION(BlueprintPure)
	const FRotator GetHandRotation(int HandIndex);

	// IsGrabbing
	UFUNCTION(BlueprintPure)
	const bool IsGrabbing();
	/** End of Hand Utility Functions */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = PhysicalArms)
	float ArmsLengthUnits = 54.5f; //45

protected:
	virtual void BeginPlay();

	virtual void Tick(float DeltaSeconds) override;

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Called for grabbing input Right */
	void GrabR(const FInputActionValue& Value);

	/** Called for grabbing input Right */
	void StopGrabR(const FInputActionValue& Value);

	/** Called for grabbing input Left */
	void GrabL(const FInputActionValue& Value);

	/** Called for grabbing input Left */
	void StopGrabL(const FInputActionValue& Value);

	/** Called for grabbing input */
	void Grab(const int HandIndex);

	void TickGrab(FHandsContextData& HandData, float DeltaSeconds);

	/** Called for grabbing input */
	void StopGrabbing(const int HandIndex);

	// APawn interface
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FHandsContextData LeftHandData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FHandsContextData RightHandData;
};

