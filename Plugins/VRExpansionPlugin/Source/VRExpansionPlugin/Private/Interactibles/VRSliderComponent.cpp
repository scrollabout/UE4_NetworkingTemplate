// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

// Parent Header
#include "Interactibles/VRSliderComponent.h"

// Unreal
#include "Net/UnrealNetwork.h"

// VREP



// UVRSliderComponent

// Public

// Constructor & Destructor

//=============================================================================
UVRSliderComponent::UVRSliderComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->SetGenerateOverlapEvents(true);
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;

	// Defaulting these true so that they work by default in networked environments
	bReplicateMovement = true;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;

	InitialRelativeTransform = FTransform::Identity;
	bDenyGripping = false;

	MinSlideDistance = FVector::ZeroVector;
	MaxSlideDistance = FVector(10.0f, 0.f, 0.f);
	SliderRestitution = 0.0f;
	CurrentSliderProgress = 0.0f;
	LastSliderProgress = 0.0f;
	
	MomentumAtDrop = 0.0f;
	SliderMomentumFriction = 3.0f;
	MaxSliderMomentum = 1.0f;
	FramesToAverage = 3;

	InitialInteractorLocation = FVector::ZeroVector;
	InitialGripLoc = FVector::ZeroVector;

	bSlideDistanceIsInParentSpace = true;

	SplineComponentToFollow = nullptr;

	bFollowSplineRotationAndScale = false;
	SplineLerpType = EVRInteractibleSliderLerpType::Lerp_None;
	SplineLerpValue = 8.f;

	GripPriority = 1;
	LastSliderProgressState = -1.0f;
	LastInputKey = 0.0f;

	bSliderUsesSnapPoints = false;
	SnapIncrement = 0.1f;
	SnapThreshold = 0.1f;
	EventThrowThreshold = 1.0f;
	bHitEventThreshold = false;

	// Set to only overlap with things so that its not ruined by touching over actors
	this->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
}

//=============================================================================
UVRSliderComponent::~UVRSliderComponent()
{
}

// Functions

void UVRSliderComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	CalculateSliderProgress();

	bOriginalReplicatesMovement = bReplicateMovement;
}

void UVRSliderComponent::CheckSliderProgress()
{
	// Skip first check, this will skip an event throw on rounded
	if (LastSliderProgressState < 0.0f)
	{
		// Skip first tick, this is our resting position
		LastSliderProgressState = CurrentSliderProgress;
	}
	else if ((LastSliderProgressState != CurrentSliderProgress) || bHitEventThreshold)
	{
		if ((!bSliderUsesSnapPoints && (CurrentSliderProgress == 1.0f || CurrentSliderProgress == 0.0f)) ||
			(bSliderUsesSnapPoints && FMath::IsNearlyEqual(FMath::Fmod(CurrentSliderProgress, SnapIncrement), 0.0f))
			)
		{
			// I am working with exacts here because of the clamping, it should actually work with no precision issues
			// I wanted to ABS(Last-Cur) == 1.0 but it would cause an initial miss on whatever one last was inited to. 

			if (!bSliderUsesSnapPoints)
				LastSliderProgressState = FMath::RoundToFloat(CurrentSliderProgress); // Ensure it is rounded to 0 or 1
			else
				LastSliderProgressState = CurrentSliderProgress;

			ReceiveSliderHitPoint(LastSliderProgressState);
			OnSliderHitPoint.Broadcast(LastSliderProgressState);
			bHitEventThreshold = false;
		}
	}

	if (FMath::Abs(LastSliderProgressState - CurrentSliderProgress) >= EventThrowThreshold)
	{
		bHitEventThreshold = true;
	}
}

void UVRSliderComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVRSliderComponent, InitialRelativeTransform);
	DOREPLIFETIME(UVRSliderComponent, SplineComponentToFollow);
	//DOREPLIFETIME_CONDITION(UVRSliderComponent, bIsLerping, COND_InitialOnly);

	DOREPLIFETIME(UVRSliderComponent, bRepGameplayTags);
	DOREPLIFETIME(UVRSliderComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRSliderComponent, GameplayTags, COND_Custom);
}

void UVRSliderComponent::OnRegister()
{
	Super::OnRegister();

	// Init the slider settings
	if (USplineComponent * ParentSpline = Cast<USplineComponent>(GetAttachParent()))
	{
		SetSplineComponentToFollow(ParentSpline);
	}
	else
	{
		ResetInitialSliderLocation();
	}
}

void UVRSliderComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Replicate the levers initial transform if we are replicating movement
	//DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, InitialRelativeTransform, bReplicateMovement);
	//DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, SplineComponentToFollow, bReplicateMovement);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UVRSliderComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Call supers tick (though I don't think any of the base classes to this actually implement it)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsLerping)
	{
		if (FMath::IsNearlyZero(MomentumAtDrop * DeltaTime, 0.00001f))
		{
			bIsLerping = false;
		}
		else
		{
			MomentumAtDrop = FMath::FInterpTo(MomentumAtDrop, 0.0f, DeltaTime, SliderMomentumFriction);

			float newProgress = CurrentSliderProgress + (MomentumAtDrop * DeltaTime);

			if (newProgress < 0.0f || FMath::IsNearlyEqual(newProgress, 0.0f, 0.00001f))
			{
				if (SliderRestitution > 0.0f)
				{
					// Reverse the momentum
					MomentumAtDrop = -(MomentumAtDrop * SliderRestitution);
					this->SetSliderProgress(0.0f);
				}
				else
				{
					bIsLerping = false;
					this->SetSliderProgress(0.0f);
				}
			}
			else if (newProgress > 1.0f || FMath::IsNearlyEqual(newProgress, 1.0f, 0.00001f))
			{
				if (SliderRestitution > 0.0f)
				{
					// Reverse the momentum
					MomentumAtDrop = -(MomentumAtDrop * SliderRestitution);
					this->SetSliderProgress(1.0f);
				}
				else
				{
					bIsLerping = false;
					this->SetSliderProgress(1.0f);
				}
			}
			else
			{
				this->SetSliderProgress(newProgress);
			}
		}

		if (!bIsLerping)
		{
			// Notify the end user
			OnSliderFinishedLerping.Broadcast(CurrentSliderProgress);
			ReceiveSliderFinishedLerping(CurrentSliderProgress);

			this->SetComponentTickEnabled(false);
			bReplicateMovement = bOriginalReplicatesMovement;
		}
		
		// Check for the hit point always
		CheckSliderProgress();
	}
}

// IVRGripInterface Implementation.

FBPAdvGripSettings UVRSliderComponent::AdvancedGripSettings_Implementation()
{
	return FBPAdvGripSettings(GripPriority);
}

bool UVRSliderComponent::AllowsMultipleGrips_Implementation()
{
	return false;
}

void UVRSliderComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

bool UVRSliderComponent::DenyGripping_Implementation()
{
	return bDenyGripping;
}

bool UVRSliderComponent::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	return false;
}

void UVRSliderComponent::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = 0.0f;
	GripDampingOut = 0.0f;
}

EGripCollisionType UVRSliderComponent::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return EGripCollisionType::CustomGrip;
}

float UVRSliderComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

EGripLateUpdateSettings UVRSliderComponent::GripLateUpdateSetting_Implementation()
{
	return EGripLateUpdateSettings::LateUpdatesAlwaysOff;
}

EGripMovementReplicationSettings UVRSliderComponent::GripMovementReplicationType_Implementation()
{
	return MovementReplicationSetting;
}

void UVRSliderComponent::IsHeld_Implementation(TArray<FBPGripPair> & CurHoldingControllers, bool & bCurIsHeld)
{
	CurHoldingControllers.Empty();
	if (HoldingGrip.IsValid())
	{
		CurHoldingControllers.Add(HoldingGrip);
		bCurIsHeld = bIsHeld;
	}
	else
	{
		bCurIsHeld = false;
	}
}

void UVRSliderComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation)
{
	FTransform CurrentRelativeTransform = InitialRelativeTransform * UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);

	// This lets me use the correct original location over the network without changes
	FTransform ReversedRelativeTransform = FTransform(GripInformation.RelativeTransform.ToInverseMatrixWithScale());
	FTransform RelativeToGripTransform = ReversedRelativeTransform * this->GetComponentTransform();

	InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(RelativeToGripTransform.GetTranslation());
	InitialGripLoc = InitialRelativeTransform.InverseTransformPosition(this->RelativeLocation);
	InitialDropLocation = ReversedRelativeTransform.GetTranslation();
	LastInputKey = -1.0f;
	LerpedKey = 0.0f;
	bHitEventThreshold = false;
	LastSliderProgressState = -1.0f;
	LastSliderProgress = CurrentSliderProgress;

	bIsLerping = false;
	MomentumAtDrop = 0.0f;

	if (GripInformation.GripMovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
	{
		bReplicateMovement = false;
	}
}

void UVRSliderComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed)
{
	//this->SetComponentTickEnabled(false);
	// #TODO: Handle letting go and how lerping works, specifically with the snap points it may be an issue
	if (SliderBehaviorWhenReleased != EVRInteractibleSliderDropBehavior::Stay)
	{
		bIsLerping = true;
		this->SetComponentTickEnabled(true);

		if (MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
			bReplicateMovement = false;
	}
	else
	{
		this->SetComponentTickEnabled(false);
		bReplicateMovement = bOriginalReplicatesMovement;
	}
}

void UVRSliderComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UVRSliderComponent::OnEndUsed_Implementation() {}
void UVRSliderComponent::OnEndSecondaryUsed_Implementation() {}
void UVRSliderComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
void UVRSliderComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnSecondaryUsed_Implementation() {}
void UVRSliderComponent::OnUsed_Implementation() {}

bool UVRSliderComponent::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }

ESecondaryGripType UVRSliderComponent::SecondaryGripType_Implementation()
{
	return ESecondaryGripType::SG_None;
}

void UVRSliderComponent::SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, uint8 GripID, bool bNewIsHeld)
{
	if (bNewIsHeld)
	{
		HoldingGrip = FBPGripPair(NewHoldingController, GripID);
		if (MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			if (!bIsHeld && !bIsLerping)
				bOriginalReplicatesMovement = bReplicateMovement;
			bReplicateMovement = false;
		}
	}
	else
	{
		HoldingGrip.Clear();
		if (MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			bReplicateMovement = bOriginalReplicatesMovement;
		}
	}

	bIsHeld = bNewIsHeld;
}

bool UVRSliderComponent::SimulateOnDrop_Implementation()
{
	return false;
}


EGripInterfaceTeleportBehavior UVRSliderComponent::TeleportBehavior_Implementation()
{
	return EGripInterfaceTeleportBehavior::DropOnTeleport;
}

void UVRSliderComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime)
{
	// Handle manual tracking here
	FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
	FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetPivotLocation());

	FVector CalculatedLocation = InitialGripLoc + (CurInteractorLocation - InitialInteractorLocation);

	float SplineProgress = CurrentSliderProgress;
	if (SplineComponentToFollow != nullptr)
	{
		FVector WorldCalculatedLocation = CurrentRelativeTransform.TransformPosition(CalculatedLocation);
		float ClosestKey = SplineComponentToFollow->FindInputKeyClosestToWorldLocation(WorldCalculatedLocation);

		if (bSliderUsesSnapPoints)
		{
			float SplineLength = SplineComponentToFollow->GetSplineLength();
			SplineProgress = GetCurrentSliderProgress(WorldCalculatedLocation, true, ClosestKey);

			SplineProgress = UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(SplineProgress, SnapIncrement, SnapThreshold);

			const int32 NumPoints = SplineComponentToFollow->SplineCurves.Position.Points.Num();

			if (SplineComponentToFollow->SplineCurves.Position.Points.Num() > 1)
			{
				ClosestKey = SplineComponentToFollow->SplineCurves.ReparamTable.Eval(SplineProgress * SplineLength, 0.0f);
			}

			WorldCalculatedLocation = SplineComponentToFollow->GetLocationAtSplineInputKey(ClosestKey, ESplineCoordinateSpace::World);
		}

		bool bLerpToNewKey = true;
		bool bChangedLocation = false;

		if (bEnforceSplineLinearity && LastInputKey >= 0.0f &&
			FMath::Abs((FMath::TruncToFloat(ClosestKey) - FMath::TruncToFloat(LastInputKey))) > 1.0f &&
			(!bSliderUsesSnapPoints || (SplineProgress - CurrentSliderProgress > SnapIncrement))
			)
		{
			bLerpToNewKey = false;
		}
		else
		{
			LerpedKey = ClosestKey;
		}

		if (bFollowSplineRotationAndScale)
		{
			FTransform trans;
			if (SplineLerpType != EVRInteractibleSliderLerpType::Lerp_None && LastInputKey >= 0.0f && !FMath::IsNearlyEqual(LerpedKey, LastInputKey))
			{
				GetLerpedKey(LerpedKey, DeltaTime);
				trans = SplineComponentToFollow->GetTransformAtSplineInputKey(LerpedKey, ESplineCoordinateSpace::World, true);
				bChangedLocation = true;
			}
			else if (bLerpToNewKey)
			{
				trans = SplineComponentToFollow->FindTransformClosestToWorldLocation(WorldCalculatedLocation, ESplineCoordinateSpace::World, true);
				bChangedLocation = true;
			}

			if (bChangedLocation)
			{
				trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
				trans = trans * ParentTransform.Inverse();
				this->SetRelativeTransform(trans);
			}
		}
		else
		{
			FVector WorldLocation;
			if (SplineLerpType != EVRInteractibleSliderLerpType::Lerp_None && LastInputKey >= 0.0f && !FMath::IsNearlyEqual(LerpedKey, LastInputKey))
			{
				GetLerpedKey(LerpedKey, DeltaTime);
				WorldLocation = SplineComponentToFollow->GetLocationAtSplineInputKey(LerpedKey, ESplineCoordinateSpace::World);
				bChangedLocation = true;
			}
			else if (bLerpToNewKey)
			{
				WorldLocation = SplineComponentToFollow->FindLocationClosestToWorldLocation(WorldCalculatedLocation, ESplineCoordinateSpace::World);
				bChangedLocation = true;
			}

			if (bChangedLocation)
				this->SetRelativeLocation(ParentTransform.InverseTransformPosition(WorldLocation));
		}

		CurrentSliderProgress = GetCurrentSliderProgress(WorldCalculatedLocation, true, bLerpToNewKey ? LerpedKey : ClosestKey);
		if (bLerpToNewKey)
		{
			LastInputKey = LerpedKey;
		}
	}
	else
	{
		FVector ClampedLocation = ClampSlideVector(CalculatedLocation);
		this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
		CurrentSliderProgress = GetCurrentSliderProgress(bSlideDistanceIsInParentSpace ? ClampedLocation * InitialRelativeTransform.GetScale3D() : ClampedLocation);
	}

	if (SliderBehaviorWhenReleased == EVRInteractibleSliderDropBehavior::RetainMomentum)
	{
		// Rolling average across num samples
		MomentumAtDrop -= MomentumAtDrop / FramesToAverage;
		MomentumAtDrop += ((CurrentSliderProgress - LastSliderProgress) / DeltaTime) / FramesToAverage;

		MomentumAtDrop = FMath::Min(MaxSliderMomentum, MomentumAtDrop);

		LastSliderProgress = CurrentSliderProgress;
	}

	CheckSliderProgress();

	// Converted to a relative value now so it should be correct
	if (BreakDistance > 0.f && GrippingController->HasGripAuthority(GripInformation) && FVector::DistSquared(InitialDropLocation, this->GetComponentTransform().InverseTransformPosition(GrippingController->GetPivotLocation())) >= FMath::Square(BreakDistance))
	{
		GrippingController->DropObjectByInterface(this, HoldingGrip.GripID);
		return;
	}
}

/*EGripCollisionType UVRSliderComponent::SlotGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

EGripCollisionType UVRSliderComponent::FreeGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}*/

/*float UVRSliderComponent::GripStiffness_Implementation()
{
	return 0.0f;
}

float UVRSliderComponent::GripDamping_Implementation()
{
	return 0.0f;
}*/

/*void UVRSliderComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRSliderComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}*/

/*FBPInteractionSettings UVRSliderComponent::GetInteractionSettings_Implementation()
{
	return FBPInteractionSettings();
}*/

float UVRSliderComponent::CalculateSliderProgress()
{
	if (this->SplineComponentToFollow != nullptr)
	{
		CurrentSliderProgress = GetCurrentSliderProgress(this->GetComponentLocation());
	}
	else
	{
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
		FVector CalculatedLocation = CurrentRelativeTransform.InverseTransformPosition(this->GetComponentLocation());

		//if (bSlideDistanceIsInParentSpace)
			//CalculatedLocation *= FVector(1.0f) / InitialRelativeTransform.GetScale3D();

		CurrentSliderProgress = GetCurrentSliderProgress(CalculatedLocation);
	}

	return CurrentSliderProgress;
}

FVector UVRSliderComponent::ClampSlideVector(FVector ValueToClamp)
{
	FVector fScaleFactor = FVector(1.0f);

	if (bSlideDistanceIsInParentSpace)
		fScaleFactor = fScaleFactor / InitialRelativeTransform.GetScale3D();

	FVector MinScale = MinSlideDistance * fScaleFactor;

	FVector Dist = (MinSlideDistance + MaxSlideDistance) * fScaleFactor;
	FVector Progress = (ValueToClamp - (-MinScale)) / Dist;

	if (bSliderUsesSnapPoints)
	{
		Progress.X = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.X, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
		Progress.Y = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.Y, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
		Progress.Z = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.Z, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
	}
	else
	{
		Progress.X = FMath::Clamp(Progress.X, 0.f, 1.f);
		Progress.Y = FMath::Clamp(Progress.Y, 0.f, 1.f);
		Progress.Z = FMath::Clamp(Progress.Z, 0.f, 1.f);
	}

	return (Progress * Dist) - (MinScale);
}

float UVRSliderComponent::GetCurrentSliderProgress(FVector CurLocation, bool bUseKeyInstead, float CurKey)
{
	if (SplineComponentToFollow != nullptr)
	{
		// In this case it is a world location
		float ClosestKey = CurKey;

		if (!bUseKeyInstead)
			ClosestKey = SplineComponentToFollow->FindInputKeyClosestToWorldLocation(CurLocation);

		int32 primaryKey = FMath::TruncToInt(ClosestKey);

		float distance1 = SplineComponentToFollow->GetDistanceAlongSplineAtSplinePoint(primaryKey);
		float distance2 = SplineComponentToFollow->GetDistanceAlongSplineAtSplinePoint(primaryKey + 1);

		float FinalDistance = ((distance2 - distance1) * (ClosestKey - (float)primaryKey)) + distance1;
		return FMath::Clamp(FinalDistance / SplineComponentToFollow->GetSplineLength(), 0.0f, 1.0f);
	}

	// Should need the clamp normally, but if someone is manually setting locations it could go out of bounds
	return FMath::Clamp(FVector::Dist(-MinSlideDistance, CurLocation) / FVector::Dist(-MinSlideDistance, MaxSlideDistance), 0.0f, 1.0f);
}

void UVRSliderComponent::GetLerpedKey(float &ClosestKey, float DeltaTime)
{
	switch (SplineLerpType)
	{
	case EVRInteractibleSliderLerpType::Lerp_Interp:
	{
		ClosestKey = FMath::FInterpTo(LastInputKey, ClosestKey, DeltaTime, SplineLerpValue);
	}break;
	case EVRInteractibleSliderLerpType::Lerp_InterpConstantTo:
	{
		ClosestKey = FMath::FInterpConstantTo(LastInputKey, ClosestKey, DeltaTime, SplineLerpValue);
	}break;

	default: break;
	}
}

void UVRSliderComponent::ResetInitialSliderLocation()
{
	// Get our initial relative transform to our parent (or not if un-parented).
	InitialRelativeTransform = this->GetRelativeTransform();
	ResetToParentSplineLocation();

	if (SplineComponentToFollow == nullptr)
		CurrentSliderProgress = GetCurrentSliderProgress(FVector(0, 0, 0));
}

void UVRSliderComponent::ResetToParentSplineLocation()
{
	if (SplineComponentToFollow != nullptr)
	{
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		FTransform WorldTransform = SplineComponentToFollow->FindTransformClosestToWorldLocation(this->GetComponentLocation(), ESplineCoordinateSpace::World, true);
		if (bFollowSplineRotationAndScale)
		{
			WorldTransform.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
			WorldTransform = WorldTransform * ParentTransform.Inverse();
			this->SetRelativeTransform(WorldTransform);
		}
		else
		{
			this->SetWorldLocation(WorldTransform.GetLocation());
		}

		CurrentSliderProgress = GetCurrentSliderProgress(WorldTransform.GetLocation());
	}
}

void UVRSliderComponent::SetSliderProgress(float NewSliderProgress)
{
	NewSliderProgress = FMath::Clamp(NewSliderProgress, 0.0f, 1.0f);

	if (SplineComponentToFollow != nullptr)
	{
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		float splineProgress = SplineComponentToFollow->GetSplineLength() * NewSliderProgress;

		if (bFollowSplineRotationAndScale)
		{
			FTransform trans = SplineComponentToFollow->GetTransformAtDistanceAlongSpline(splineProgress, ESplineCoordinateSpace::World, true);
			trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
			trans = trans * ParentTransform.Inverse();
			this->SetRelativeTransform(trans);
		}
		else
		{
			this->SetRelativeLocation(ParentTransform.InverseTransformPosition(SplineComponentToFollow->GetLocationAtDistanceAlongSpline(splineProgress, ESplineCoordinateSpace::World)));
		}
	}
	else // Not a spline follow
	{
		// Doing it min+max because the clamp value subtracts the min value
		FVector CalculatedLocation = FMath::Lerp(-MinSlideDistance, MaxSlideDistance, NewSliderProgress);

		if (bSlideDistanceIsInParentSpace)
			CalculatedLocation *= FVector(1.0f) / InitialRelativeTransform.GetScale3D();

		FVector ClampedLocation = ClampSlideVector(CalculatedLocation);

		//if (bSlideDistanceIsInParentSpace)
		//	this->SetRelativeLocation(InitialRelativeTransform.TransformPositionNoScale(ClampedLocation));
		//else
		this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
	}

	CurrentSliderProgress = NewSliderProgress;
}

void UVRSliderComponent::SetSplineComponentToFollow(USplineComponent * SplineToFollow)
{
	SplineComponentToFollow = SplineToFollow;

	if (SplineToFollow != nullptr)
		ResetToParentSplineLocation();
	else
		CalculateSliderProgress();
}


