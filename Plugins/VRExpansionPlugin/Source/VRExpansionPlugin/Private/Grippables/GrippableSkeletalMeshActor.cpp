// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

// Parent Header
#include "Grippables/GrippableSkeletalMeshActor.h"

// Unreal
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

// VREP



// UOptionalRepSkeletalMeshComponent

// Public

// Constructor & Destructor

//=============================================================================
UOptionalRepSkeletalMeshComponent::UOptionalRepSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer) : 
	Super             (ObjectInitializer),
	bReplicateMovement(true             )
{
	//bReplicateMovement = true;
}

// Functions

void UOptionalRepSkeletalMeshComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}

// CPP only:

void UOptionalRepSkeletalMeshComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOptionalRepSkeletalMeshComponent, bReplicateMovement);
}


// AGrippableSkeletalMeshActor

// Constructor & Destructor

//=============================================================================
AGrippableSkeletalMeshActor::AGrippableSkeletalMeshActor(const FObjectInitializer& ObjectInitializer) : 
	Super(ObjectInitializer.SetDefaultSubobjectClass<UOptionalRepSkeletalMeshComponent>(TEXT("SkeletalMeshComponent0")))
{
	VRGripInterfaceSettings.bDenyGripping           = false                                                    ;
	VRGripInterfaceSettings.OnTeleportBehavior      = EGripInterfaceTeleportBehavior::TeleportAllComponents    ;
	VRGripInterfaceSettings.bSimulateOnDrop         = true                                                     ;
	VRGripInterfaceSettings.SlotDefaultGripType     = EGripCollisionType::InteractiveCollisionWithPhysics      ;
	VRGripInterfaceSettings.FreeDefaultGripType     = EGripCollisionType::InteractiveCollisionWithPhysics      ;
	VRGripInterfaceSettings.SecondaryGripType       = ESecondaryGripType::SG_None                              ;
	VRGripInterfaceSettings.MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	VRGripInterfaceSettings.LateUpdateSetting       = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping;
	VRGripInterfaceSettings.ConstraintStiffness     = 1500.0f                                                  ;
	VRGripInterfaceSettings.ConstraintDamping       = 200.0f                                                   ;
	VRGripInterfaceSettings.ConstraintBreakDistance = 100.0f                                                   ;
	VRGripInterfaceSettings.SecondarySlotRange      = 20.0f                                                    ;
	VRGripInterfaceSettings.PrimarySlotRange        = 20.0f                                                    ;

	VRGripInterfaceSettings.bIsHeld = false;

	// Default replication on for multiplayer
	//this->bNetLoadOnClient = false;
	this->bReplicateMovement = true;
	this->bReplicates = true;

	bRepGripSettingsAndGameplayTags = true;
	bAllowIgnoringAttachOnOwner = true;

	// Setting a minimum of every 3rd frame (VR 90fps) for replication consideration
	// Otherwise we will get some massive slow downs if the replication is allowed to hit the 2 per second minimum default
	MinNetUpdateFrequency = 30.0f;
}

AGrippableSkeletalMeshActor::~AGrippableSkeletalMeshActor()
{}


// Functions

void AGrippableSkeletalMeshActor::SetDenyGripping(bool bDenyGripping)
{
	VRGripInterfaceSettings.bDenyGripping = bDenyGripping;
}

// ------------------------------------------------
// Client Auth Throwing Data and functions 
// ------------------------------------------------

void AGrippableSkeletalMeshActor::CeaseReplicationBlocking()
{
	ClientAuthReplicationData.bIsCurrentlyClientAuth = false;
	if (ClientAuthReplicationData.ResetReplicationHandle.IsValid())
	{
		if (UWorld * OurWorld = GetWorld())
		{
			OurWorld->GetTimerManager().ClearTimer(ClientAuthReplicationData.ResetReplicationHandle);
		}
	}
}

bool AGrippableSkeletalMeshActor::PollReplicationEvent()
{
	if (!ClientAuthReplicationData.bIsCurrentlyClientAuth)
		return false;

	UWorld *OurWorld = GetWorld();
	if (!OurWorld)
		return false;

	if ((OurWorld->GetTimeSeconds() - ClientAuthReplicationData.TimeAtInitialThrow) > 10.0f)
	{
		// Lets time out sending, its been 10 seconds since we threw the object and its likely that it is conflicting with some server
		// Authed movement that is forcing it to keep momentum.
		return false;
	}

	// Store current transform for resting check
	FTransform CurTransform = this->GetActorTransform();

	if (!CurTransform.GetRotation().Equals(ClientAuthReplicationData.LastActorTransform.GetRotation()) || !CurTransform.GetLocation().Equals(ClientAuthReplicationData.LastActorTransform.GetLocation()))
	{
		ClientAuthReplicationData.LastActorTransform = CurTransform;

		if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(RootComponent))
		{
			// Need to clamp to a max time since start, to handle cases with conflicting collisions
			if (PrimComp->IsSimulatingPhysics() && ShouldWeSkipAttachmentReplication(false))
			{
				FRepMovementVR ClientAuthMovementRep;

				if (ClientAuthMovementRep.GatherActorsMovement(this))
				{
					Server_GetClientAuthReplication(ClientAuthMovementRep);

					if (PrimComp->RigidBodyIsAwake())
					{
						return true;
					}
				}
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		// Difference is too small, lets end sending location.
		ClientAuthReplicationData.LastActorTransform = FTransform::Identity;
	}

	AActor* TopOwner = GetOwner();

	if (TopOwner != nullptr)
	{
		AActor * tempOwner = TopOwner->GetOwner();

		// I have an owner so search that for the top owner
		while (tempOwner)
		{
			TopOwner = tempOwner;
			tempOwner = TopOwner->GetOwner();
		}

		if (APlayerController* PlayerController = Cast<APlayerController>(TopOwner))
		{
			if (APlayerState* PlayerState = PlayerController->PlayerState)
			{
				if (ClientAuthReplicationData.ResetReplicationHandle.IsValid())
				{
					OurWorld->GetTimerManager().ClearTimer(ClientAuthReplicationData.ResetReplicationHandle);
				}

				// Lets clamp the ping to a min / max value just in case
				float clampedPing = FMath::Clamp(PlayerState->ExactPing * 0.001f, 0.0f, 1000.0f);
				OurWorld->GetTimerManager().SetTimer(ClientAuthReplicationData.ResetReplicationHandle, this, &AGrippableSkeletalMeshActor::CeaseReplicationBlocking, clampedPing, false);
			}
		}
	}

	return false;
}

bool AGrippableSkeletalMeshActor::Server_GetClientAuthReplication_Validate(const FRepMovementVR& newMovement)
{
	return true;
}

void AGrippableSkeletalMeshActor::Server_GetClientAuthReplication_Implementation(const FRepMovementVR& newMovement)
{
	newMovement.CopyTo(ReplicatedMovement);
	OnRep_ReplicatedMovement();
}

// End client auth throwing data and functions. //

// Actor Overloads

// On Destroy clean up our objects.
void AGrippableSkeletalMeshActor::BeginDestroy()
{
	Super::BeginDestroy();

	for (int32 i = 0; i < GripLogicScripts.Num(); i++)
	{
		if (UObject *SubObject = GripLogicScripts[i])
		{
			SubObject->MarkPendingKill();
		}
	}

	GripLogicScripts.Empty();
}

void AGrippableSkeletalMeshActor::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	// Call all grip scripts begin play events so they can perform any needed logic
	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script)
		{
			Script->BeginPlay(this);
		}
	}
}

void AGrippableSkeletalMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
	{
		GEngine->GetEngineSubsystem<UBucketUpdateSubsystem>()->RemoveObjectFromBucketByFunctionName(this, FName(TEXT("PollReplicationEvent")));
	}

	RemoveFromClientReplicationBucket();

	// Call all grip scripts begin play events so they can perform any needed logic
	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script)
		{
			Script->EndPlay(EndPlayReason);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AGrippableSkeletalMeshActor::MarkComponentsAsPendingKill()
{
	Super::MarkComponentsAsPendingKill();

	for (int32 i = 0; i < GripLogicScripts.Num(); ++i)
	{
		if (UObject *SubObject = GripLogicScripts[i])
		{
			SubObject->MarkPendingKill();
		}
	}

	GripLogicScripts.Empty();
}

void AGrippableSkeletalMeshActor::OnRep_AttachmentReplication()
{
	if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	// None of our overrides are required, lets just pass it on now
	Super::OnRep_AttachmentReplication();
}

void AGrippableSkeletalMeshActor::OnRep_ReplicateMovement()
{
	if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	if (RootComponent)
	{
		const FRepAttachment ReplicationAttachment = GetAttachmentReplication();
		if (!ReplicationAttachment.AttachParent)
		{
			// This "fix" corrects the simulation state not replicating over correctly
			// If you turn off movement replication, simulate an object, turn movement replication back on and un-simulate, it never knows the difference
			// This change ensures that it is checking against the current state
			if (RootComponent->IsSimulatingPhysics() != ReplicatedMovement.bRepPhysics)//SavedbRepPhysics != ReplicatedMovement.bRepPhysics)
			{
				// Turn on/off physics sim to match server.
				SyncReplicatedPhysicsSimulation();

				// It doesn't really hurt to run it here, the super can call it again but it will fail out as they already match
			}
		}
	}

	Super::OnRep_ReplicateMovement();
}

void AGrippableSkeletalMeshActor::OnRep_ReplicatedMovement()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

/** Called right before being marked for destruction due to network replication */
// Clean up our objects so that they aren't sitting around for GC.
void AGrippableSkeletalMeshActor::PreDestroyFromReplication()
{
	Super::PreDestroyFromReplication();

	// Destroy any sub-objects we created
	for (int32 i = 0; i < GripLogicScripts.Num(); ++i)
	{
		if (UObject *SubObject = GripLogicScripts[i])
		{
			OnSubobjectDestroyFromReplication(SubObject); //-V595
			SubObject->PreDestroyFromReplication();
			SubObject->MarkPendingKill();
		}
	}

	for (UActorComponent * ActorComp : GetComponents())
	{
		// Pending kill components should have already had this called as they were network spawned and are being killed
		if (ActorComp && !ActorComp->IsPendingKill() && ActorComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			ActorComp->PreDestroyFromReplication();
	}

	GripLogicScripts.Empty();
}

void AGrippableSkeletalMeshActor::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableSkeletalMeshActor, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableSkeletalMeshActor, GameplayTags, bRepGripSettingsAndGameplayTags);
}

void AGrippableSkeletalMeshActor::PostNetReceivePhysicState()
{
	if (VRGripInterfaceSettings.bIsHeld && bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::PostNetReceivePhysicState();
}

bool AGrippableSkeletalMeshActor::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script && !Script->IsPendingKill())
		{
			WroteSomething |= Channel->ReplicateSubobject(Script, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

// IVRGripInterface Implementation.

bool AGrippableSkeletalMeshActor::AddToClientReplicationBucket()
{
	if (ShouldWeSkipAttachmentReplication(false))
	{
		// The subsystem automatically removes entries with the same function signature so its safe to just always add here
		GEngine->GetEngineSubsystem<UBucketUpdateSubsystem>()->AddObjectToBucket(ClientAuthReplicationData.UpdateRate, this, FName(TEXT("PollReplicationEvent")));
		ClientAuthReplicationData.bIsCurrentlyClientAuth = true;

		if (UWorld * World = GetWorld())
			ClientAuthReplicationData.TimeAtInitialThrow = World->GetTimeSeconds();

		return true;
	}

	return false;
}

FBPAdvGripSettings AGrippableSkeletalMeshActor::AdvancedGripSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedGripSettings;
}

bool AGrippableSkeletalMeshActor::AllowsMultipleGrips_Implementation()
{
	return VRGripInterfaceSettings.bAllowMultipleGrips;
}

void AGrippableSkeletalMeshActor::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool& bHadSlotInRange, FTransform& SlotWorldTransform, UGripMotionControllerComponent* CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(OverridePrefix, this, WorldLocation, bSecondarySlot ? VRGripInterfaceSettings.SecondarySlotRange : VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool AGrippableSkeletalMeshActor::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}

/*FBPInteractionSettings AGrippableSkeletalMeshActor::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}*/

bool AGrippableSkeletalMeshActor::GetGripScripts_Implementation(TArray<UVRGripScriptBase*>& ArrayReference)
{
	ArrayReference = GripLogicScripts;
	return GripLogicScripts.Num() > 0;
}

void AGrippableSkeletalMeshActor::GetGripStiffnessAndDamping_Implementation(float& GripStiffnessOut, float& GripDampingOut)
{
	GripStiffnessOut = VRGripInterfaceSettings.ConstraintStiffness;
	GripDampingOut = VRGripInterfaceSettings.ConstraintDamping;
}

EGripCollisionType AGrippableSkeletalMeshActor::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return bIsSlot ? VRGripInterfaceSettings.SlotDefaultGripType : VRGripInterfaceSettings.FreeDefaultGripType;
}

float AGrippableSkeletalMeshActor::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

EGripLateUpdateSettings AGrippableSkeletalMeshActor::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

EGripMovementReplicationSettings AGrippableSkeletalMeshActor::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

void AGrippableSkeletalMeshActor::IsHeld_Implementation(TArray<FBPGripPair>& HoldingControllers, bool& bIsHeld)
{
	HoldingControllers = VRGripInterfaceSettings.HoldingControllers;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

bool AGrippableSkeletalMeshActor::RemoveFromClientReplicationBucket()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
	{
		GEngine->GetEngineSubsystem<UBucketUpdateSubsystem>()->RemoveObjectFromBucketByFunctionName(this, FName(TEXT("PollReplicationEvent")));
		CeaseReplicationBlocking();
		return true;
	}

	return false;
}

ESecondaryGripType AGrippableSkeletalMeshActor::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}

void AGrippableSkeletalMeshActor::SetHeld_Implementation(UGripMotionControllerComponent* HoldingController, uint8 GripID, bool bIsHeld)
{
	if (bIsHeld)
	{
		VRGripInterfaceSettings.HoldingControllers.AddUnique(FBPGripPair(HoldingController, GripID));
		RemoveFromClientReplicationBucket();

		VRGripInterfaceSettings.bWasHeld = true;
	}
	else
	{
		VRGripInterfaceSettings.HoldingControllers.Remove(FBPGripPair(HoldingController, GripID));
		if (ClientAuthReplicationData.bUseClientAuthThrowing && ShouldWeSkipAttachmentReplication(false))
		{
			if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
			{
				if (PrimComp->IsSimulatingPhysics())
				{
					AddToClientReplicationBucket();
				}
			}
		}
	}

	VRGripInterfaceSettings.bIsHeld = VRGripInterfaceSettings.HoldingControllers.Num() > 0;
}

bool AGrippableSkeletalMeshActor::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripInterfaceTeleportBehavior AGrippableSkeletalMeshActor::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

// Event

void AGrippableSkeletalMeshActor::OnChildGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void AGrippableSkeletalMeshActor::OnChildGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}

void AGrippableSkeletalMeshActor::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void AGrippableSkeletalMeshActor::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}

void AGrippableSkeletalMeshActor::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}

bool AGrippableSkeletalMeshActor::RequestsSocketing_Implementation(USceneComponent*& ParentToSocketTo, FName& OptionalSocketName, FTransform_NetQuantize& RelativeTransform) { return false; }
void AGrippableSkeletalMeshActor::TickGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation, float DeltaTime) {}

// Interaction Functions
void AGrippableSkeletalMeshActor::OnEndSecondaryUsed_Implementation() {}
void AGrippableSkeletalMeshActor::OnEndUsed_Implementation() {}

void AGrippableSkeletalMeshActor::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}

void AGrippableSkeletalMeshActor::OnSecondaryUsed_Implementation() {}
void AGrippableSkeletalMeshActor::OnUsed_Implementation() {}

/*void AGrippableSkeletalMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	DOREPLIFETIME(AGrippableSkeletalMeshActor, VRGripInterfaceSettings);
}*/

void AGrippableSkeletalMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME/*_CONDITION*/(AGrippableSkeletalMeshActor, GripLogicScripts);// , COND_Custom);
	DOREPLIFETIME(AGrippableSkeletalMeshActor, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME(AGrippableSkeletalMeshActor, bAllowIgnoringAttachOnOwner);
	DOREPLIFETIME(AGrippableSkeletalMeshActor, ClientAuthReplicationData);
	DOREPLIFETIME_CONDITION(AGrippableSkeletalMeshActor, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(AGrippableSkeletalMeshActor, GameplayTags, COND_Custom);
}




