// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

// Parent Header
#include "Grippables/GrippableStaticMeshActor.h"

// Unreal
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

// VREP




// Public

// #TODO: Pull request this? This macro could be very useful
/*#define DOREPLIFETIME_CHANGE_NOTIFY(c,v,rncond) \
{ \
	static UProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v));	\
	bool bFound = false;																							\
	for ( int32 i = 0; i < OutLifetimeProps.Num(); i++ )															\
	{																												\
		if ( OutLifetimeProps[i].RepIndex == sp##v->RepIndex )														\
		{																											\
			for ( int32 j = 0; j < sp##v->ArrayDim; j++ )															\
			{																										\
				OutLifetimeProps[i + j].RepNotifyCondition = rncond;															\
			}																										\
			bFound = true;																							\
			break;																									\
		}																											\
	}																												\
	check( bFound );																								\
}*/


UOptionalRepStaticMeshComponent::UOptionalRepStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicateMovement = true;
}

void UOptionalRepStaticMeshComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UOptionalRepStaticMeshComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOptionalRepStaticMeshComponent, bReplicateMovement);
}

// Constructor & Destructor

//=============================================================================
AGrippableStaticMeshActor::AGrippableStaticMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UOptionalRepStaticMeshComponent>(TEXT("StaticMeshComponent0")))
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::TeleportAllComponents;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.SecondaryGripType = ESecondaryGripType::SG_None;
	VRGripInterfaceSettings.MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	VRGripInterfaceSettings.LateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping;
	VRGripInterfaceSettings.ConstraintStiffness = 1500.0f;
	VRGripInterfaceSettings.ConstraintDamping = 200.0f;
	VRGripInterfaceSettings.ConstraintBreakDistance = 100.0f;
	VRGripInterfaceSettings.SecondarySlotRange = 20.0f;
	VRGripInterfaceSettings.PrimarySlotRange = 20.0f;

	VRGripInterfaceSettings.bIsHeld = false;

	this->SetMobility(EComponentMobility::Movable);

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

AGrippableStaticMeshActor::~AGrippableStaticMeshActor()
{
}

// Functions

void AGrippableStaticMeshActor::SetDenyGripping(bool bDenyGripping)
{
	VRGripInterfaceSettings.bDenyGripping = bDenyGripping;
}

// ------------------------------------------------
// Client Auth Throwing Data and functions 
// ------------------------------------------------

void AGrippableStaticMeshActor::CeaseReplicationBlocking()
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

bool AGrippableStaticMeshActor::PollReplicationEvent()
{
	if (!ClientAuthReplicationData.bIsCurrentlyClientAuth)
		return false; // Tell the bucket subsystem to remove us from consideration

	UWorld* OurWorld = GetWorld();
	if (!OurWorld)
		return false; // Tell the bucket subsystem to remove us from consideration

	if ((OurWorld->GetTimeSeconds() - ClientAuthReplicationData.TimeAtInitialThrow) > 10.0f)
	{
		// Lets time out sending, its been 10 seconds since we threw the object and its likely that it is conflicting with some server
		// Authed movement that is forcing it to keep momentum.
		return false; // Tell the bucket subsystem to remove us from consideration
	}

	// Store current transform for resting check
	FTransform CurTransform = this->GetActorTransform();

	if (!CurTransform.GetRotation().Equals(ClientAuthReplicationData.LastActorTransform.GetRotation()) || !CurTransform.GetLocation().Equals(ClientAuthReplicationData.LastActorTransform.GetLocation()))
	{
		ClientAuthReplicationData.LastActorTransform = CurTransform;

		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(RootComponent))
		{
			// Need to clamp to a max time since start, to handle cases with conflicting collisions
			if (PrimComp->IsSimulatingPhysics() && ShouldWeSkipAttachmentReplication(false))
			{
				FRepMovementVR ClientAuthMovementRep;
				if (ClientAuthMovementRep.GatherActorsMovement(this))
				{
					Server_GetClientAuthReplication(ClientAuthMovementRep);

					if (PrimComp->RigidBodyIsAwake())
						return true;
				}
			}
		}
		else
		{
			return false; // Tell the bucket subsystem to remove us from consideration
		}
	}
	else
	{
		// Difference is too small, lets end sending location
		ClientAuthReplicationData.LastActorTransform = FTransform::Identity;
	}

	AActor* TopOwner = GetOwner();

	if (TopOwner != nullptr)
	{
		AActor* tempOwner = TopOwner->GetOwner();

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
				OurWorld->GetTimerManager().SetTimer(ClientAuthReplicationData.ResetReplicationHandle, this, &AGrippableStaticMeshActor::CeaseReplicationBlocking, clampedPing, false);
			}
		}
	}

	return false; // Tell the bucket subsystem to remove us from consideration
}

bool AGrippableStaticMeshActor::Server_GetClientAuthReplication_Validate(const FRepMovementVR& newMovement)
{
	return true;
}

void AGrippableStaticMeshActor::Server_GetClientAuthReplication_Implementation(const FRepMovementVR& newMovement)
{
	newMovement.CopyTo(ReplicatedMovement);
	OnRep_ReplicatedMovement();
}

// End client auth throwing data and functions. //

// Actor Overlords

// On Destory clean up our objects
void AGrippableStaticMeshActor::BeginDestroy()
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

void AGrippableStaticMeshActor::BeginPlay()
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

void AGrippableStaticMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
	{
		GEngine->GetEngineSubsystem<UBucketUpdateSubsystem>()->RemoveObjectFromBucketByFunctionName(this, FName(TEXT("PollReplicationEvent")));
	}

	CeaseReplicationBlocking();

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

void AGrippableStaticMeshActor::MarkComponentsAsPendingKill()
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

void AGrippableStaticMeshActor::OnRep_AttachmentReplication()
{
	if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	// None of our overrides are required, lets just pass it on now
	Super::OnRep_AttachmentReplication();
}

void AGrippableStaticMeshActor::OnRep_ReplicateMovement()
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

void AGrippableStaticMeshActor::OnRep_ReplicatedMovement()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AGrippableStaticMeshActor::PreDestroyFromReplication()
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

	for (UActorComponent* ActorComp : GetComponents())
	{
		// Pending kill components should have already had this called as they were network spawned and are being killed
		// We only call this on our interfaced components since they are the only ones that should implement grip scripts
		if (ActorComp && !ActorComp->IsPendingKill() && ActorComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			ActorComp->PreDestroyFromReplication();
	}

	GripLogicScripts.Empty();
}

void AGrippableStaticMeshActor::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableStaticMeshActor, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableStaticMeshActor, GameplayTags, bRepGripSettingsAndGameplayTags);
}

void AGrippableStaticMeshActor::PostNetReceivePhysicState()
{
	if (VRGripInterfaceSettings.bIsHeld && bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::PostNetReceivePhysicState();
}

bool AGrippableStaticMeshActor::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch* Bunch, FReplicationFlags* RepFlags)
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

//=============================================================================

// IVRGripInterface Implementation.

bool AGrippableStaticMeshActor::AddToClientReplicationBucket()
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

FBPAdvGripSettings AGrippableStaticMeshActor::AdvancedGripSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedGripSettings;
}

bool AGrippableStaticMeshActor::AllowsMultipleGrips_Implementation()
{
	return VRGripInterfaceSettings.bAllowMultipleGrips;
}

void AGrippableStaticMeshActor::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool& bHadSlotInRange, FTransform& SlotWorldTransform, UGripMotionControllerComponent* CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(OverridePrefix, this, WorldLocation, bSecondarySlot ? VRGripInterfaceSettings.SecondarySlotRange : VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool AGrippableStaticMeshActor::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}

bool AGrippableStaticMeshActor::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	ArrayReference = GripLogicScripts;
	return GripLogicScripts.Num() > 0;
}

void AGrippableStaticMeshActor::GetGripStiffnessAndDamping_Implementation(float& GripStiffnessOut, float& GripDampingOut)
{
	GripStiffnessOut = VRGripInterfaceSettings.ConstraintStiffness;
	GripDampingOut = VRGripInterfaceSettings.ConstraintDamping;
}

EGripCollisionType AGrippableStaticMeshActor::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return bIsSlot ? VRGripInterfaceSettings.SlotDefaultGripType : VRGripInterfaceSettings.FreeDefaultGripType;
}

float AGrippableStaticMeshActor::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

EGripMovementReplicationSettings AGrippableStaticMeshActor::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings AGrippableStaticMeshActor::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

void AGrippableStaticMeshActor::IsHeld_Implementation(TArray<FBPGripPair>& HoldingControllers, bool& bIsHeld)
{
	HoldingControllers = VRGripInterfaceSettings.HoldingControllers;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

bool AGrippableStaticMeshActor::RemoveFromClientReplicationBucket()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
	{
		GEngine->GetEngineSubsystem<UBucketUpdateSubsystem>()->RemoveObjectFromBucketByFunctionName(this, FName(TEXT("PollReplicationEvent")));
		CeaseReplicationBlocking();
		return true;
	}

	return false;
}

ESecondaryGripType AGrippableStaticMeshActor::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}

void AGrippableStaticMeshActor::SetHeld_Implementation(UGripMotionControllerComponent* HoldingController, uint8 GripID, bool bIsHeld)
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

bool AGrippableStaticMeshActor::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripInterfaceTeleportBehavior AGrippableStaticMeshActor::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

// Events

void AGrippableStaticMeshActor::OnChildGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void AGrippableStaticMeshActor::OnChildGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}

void AGrippableStaticMeshActor::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void AGrippableStaticMeshActor::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}

void AGrippableStaticMeshActor::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void AGrippableStaticMeshActor::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}

bool AGrippableStaticMeshActor::RequestsSocketing_Implementation(USceneComponent*& ParentToSocketTo, FName& OptionalSocketName, FTransform_NetQuantize& RelativeTransform) { return false; }
void AGrippableStaticMeshActor::TickGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation, float DeltaTime) {}

// Interaction Functions
void AGrippableStaticMeshActor::OnEndSecondaryUsed_Implementation() {}
void AGrippableStaticMeshActor::OnEndUsed_Implementation() {}
void AGrippableStaticMeshActor::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
void AGrippableStaticMeshActor::OnSecondaryUsed_Implementation() {}
void AGrippableStaticMeshActor::OnUsed_Implementation() {}

void AGrippableStaticMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrippableStaticMeshActor, GripLogicScripts);
	DOREPLIFETIME(AGrippableStaticMeshActor, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME(AGrippableStaticMeshActor, bAllowIgnoringAttachOnOwner);
	DOREPLIFETIME(AGrippableStaticMeshActor, ClientAuthReplicationData);
	DOREPLIFETIME_CONDITION(AGrippableStaticMeshActor, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(AGrippableStaticMeshActor, GameplayTags, COND_Custom);
}



