// Fill out your copyright notice in the Description page of Project Settings.

// Parent Header
#include "GripScripts/VRGripScriptBase.h"

// Unreal
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/NetDriver.h"

// VREP
#include "GripMotionControllerComponent.h"


// UVRGripScriptBase

// Public

// Constructor & Destructor

//=============================================================================
UVRGripScriptBase::UVRGripScriptBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
//	PrimaryComponentTick.bCanEverTick = false;
//	PrimaryComponentTick.bStartWithTickEnabled = false;
//	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	WorldTransformOverrideType = EGSTransformOverrideType::None;
	bDenyAutoDrop = false;
	bDenyLateUpdates = false;
	bForceDrop = false;
	bIsActive = false;

	bCanEverTick = false;
	bAllowTicking = false;
}

// Functions

void UVRGripScriptBase::BeginPlay(UObject* CallingOwner)
{
	// Notify the subscripts about begin play
	OnBeginPlay(CallingOwner);
}

bool UVRGripScriptBase::CallRemoteFunction(UFunction* Function, void* Parms, FOutParmRec* OutParms, FFrame* Stack)
{
	bool bProcessed = false;

	if (AActor* MyOwner = GetOwner())
	{
		FWorldContext* const Context = GEngine->GetWorldContextFromWorld(GetWorld());
		if (Context != nullptr)
		{
			for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
			{
				if (Driver.NetDriver != nullptr && Driver.NetDriver->ShouldReplicateFunction(MyOwner, Function))
				{
					Driver.NetDriver->ProcessRemoteFunction(MyOwner, Function, Parms, OutParms, Stack, this);

					bProcessed = true;
				}
			}
		}
	}

	return bProcessed;
}

void UVRGripScriptBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnEndPlay(EndPlayReason);
}

int32 UVRGripScriptBase::GetFunctionCallspace(UFunction * Function, FFrame * Stack)
{
	AActor* Owner = GetOwner();// Cast<AActor>(GetOuter());
	return (Owner ? Owner->GetFunctionCallspace(Function, Stack) : FunctionCallspace::Local);
}

FTransform UVRGripScriptBase::GetGripTransform(const FBPActorGripInformation& Grip, const FTransform& ParentTransform)
{
	return Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;
}

void UVRGripScriptBase::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	// Uobject has no replicated props
	//Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate here if required
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != NULL)
	{
		BPClass->GetLifetimeBlueprintReplicationList(OutLifetimeProps);
	}
}

UObject * UVRGripScriptBase::GetParent()
{
	return this->GetOuter();
}

FTransform UVRGripScriptBase::GetParentTransform(bool bGetWorldTransform)
{
	UObject * ParentObj = this->GetParent();

	if (USceneComponent * PrimParent = Cast<USceneComponent>(ParentObj))
	{
		return bGetWorldTransform ? PrimParent->GetComponentTransform() : PrimParent->GetRelativeTransform();
	}
	else if (AActor * ParentActor = Cast<AActor>(ParentObj))
	{
		return ParentActor->GetActorTransform();
	}

	return FTransform::Identity;
}

AActor * UVRGripScriptBase::GetOwner()
{
	UObject * myOuter = this->GetOuter();

	if (!myOuter)
		return nullptr;

	if (AActor * ActorOwner = Cast<AActor>(myOuter))
	{
		return ActorOwner;
	}
	else if (UActorComponent * ComponentOwner = Cast<UActorComponent>(myOuter))
	{
		return ComponentOwner->GetOwner();
	}

	return nullptr;
}

TStatId UVRGripScriptBase::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVRGripScriptBase, STATGROUP_Tickables);
}

UWorld* UVRGripScriptBase::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

ETickableTickType UVRGripScriptBase::GetTickableTickType() const
{
	if (IsTemplate(RF_ClassDefaultObject))
		return ETickableTickType::Never;

	return bCanEverTick ? ETickableTickType::Conditional : ETickableTickType::Never;
}

UWorld* UVRGripScriptBase::GetWorld() const
{
	if (IsTemplate())
	{
		return nullptr;
	}

	UObject* myOuter = this->GetOuter();
	return myOuter->GetWorld();
}

bool UVRGripScriptBase::HasAuthority()
{
	if (AActor * MyOwner = GetOwner())
	{
		return MyOwner->Role == ROLE_Authority;
	}

	return false;
}

bool UVRGripScriptBase::IsServer()
{
	if (AActor * MyOwner = GetOwner())
	{
		return MyOwner->GetNetMode() < ENetMode::NM_Client;
	}

	return false;
}

bool UVRGripScriptBase::IsTickable() const
{
	return bAllowTicking;
}

bool UVRGripScriptBase::IsTickableInEditor() const
{
	return false;
}

bool UVRGripScriptBase::IsTickableWhenPaused() const
{
	return false;
}

void UVRGripScriptBase::SetTickEnabled(bool bTickEnabled)
{
	bAllowTicking = bTickEnabled;
}

void UVRGripScriptBase::Tick(float DeltaTime)
{
	// Do nothing by default
}

// Not currently compiling in editor builds....not entirely sure why...
/*
void UVRGripScriptBase::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{

	// In the grippables pre replication to pass it on
#ifndef WITH_EDITOR
	// Run pre-replication for any grip scripts
	if (GripLogicScripts.Num())
	{
		if (UNetDriver* NetDriver = GetNetDriver())
		{
			for (UVRGripScriptBase* Script : GripLogicScripts)
			{
				if (Script && !Script->IsPendingKill())
				{
					Script->PreReplication(*((IRepChangedPropertyTracker *)NetDriver->FindOrCreateRepChangedPropertyTracker(Script).Get()));
				}
			}
		}
	}
#endif

	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != NULL)
	{
		BPClass->InstancePreReplication(this, ChangedPropertyTracker);
	}
}*/

void UVRGripScriptBaseBP::Tick(float DeltaTime)
{
	ReceiveTick(DeltaTime);
}

//=============================================================================
void UVRGripScriptBase::OnEndPlay_Implementation(const EEndPlayReason::Type EndPlayReason) {};
void UVRGripScriptBase::OnBeginPlay_Implementation(UObject* CallingOwner) {};

bool UVRGripScriptBase::GetWorldTransform_Implementation(UGripMotionControllerComponent* GrippingController, float DeltaTime, FTransform& WorldTransform, const FTransform& ParentTransform, FBPActorGripInformation& Grip, AActor* actor, UPrimitiveComponent* root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) { return true; }
void UVRGripScriptBase::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void UVRGripScriptBase::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}
void UVRGripScriptBase::OnSecondaryGrip_Implementation(UGripMotionControllerComponent* Controller, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) {}
void UVRGripScriptBase::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent* Controller, USceneComponent* ReleasingSecondaryGripComponent, const FBPActorGripInformation& GripInformation) {}

EGSTransformOverrideType UVRGripScriptBase::GetWorldTransformOverrideType() { return WorldTransformOverrideType; }
bool UVRGripScriptBase::IsScriptActive() { return bIsActive; }
//bool UVRGripScriptBase::Wants_DenyAutoDrop() { return bDenyAutoDrop; }
//bool UVRGripScriptBase::Wants_DenyLateUpdates() { return bDenyLateUpdates; }
//bool UVRGripScriptBase::Wants_ToForceDrop() { return bForceDrop; }
//bool UVRGripScriptBase::Wants_DenyTeleport_Implementation() { return false; }
void UVRGripScriptBase::HandlePrePhysicsHandle(FBPActorPhysicsHandleInformation * HandleInfo, FTransform & KinPose) {}
void UVRGripScriptBase::HandlePostPhysicsHandle(FBPActorPhysicsHandleInformation * HandleInfo) {}