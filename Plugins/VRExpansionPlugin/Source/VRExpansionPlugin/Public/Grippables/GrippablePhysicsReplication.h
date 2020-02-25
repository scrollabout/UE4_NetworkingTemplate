// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

// Includes

// Unreal
#include "CoreMinimal.h"
#include "Engine/Classes/GameFramework/PlayerController.h"
#include "Engine/Classes/GameFramework/PlayerState.h"
#include "Engine/Player.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Components/SkeletalMeshComponent.h"

#if WITH_PHYSX

	#include "Physics/PhysicsInterfaceUtils.h"
	#include "Physics/PhysScene_PhysX.h"
	#include "PhysicsReplication.h"

	//#include "Physics/Experimental/PhysScene_ImmediatePhysX.h"

#endif

// VREP
#include "VRGlobalSettings.h"

// UHeader Tool
#include "GrippablePhysicsReplication.generated.h"
//#include "GrippablePhysicsReplication.generated.h"



// Structures

USTRUCT()
struct VREXPANSIONPLUGIN_API FRepMovementVR : public FRepMovement
{
	GENERATED_USTRUCT_BODY()

public:

	// Constructors

	FRepMovementVR() : FRepMovement()
	{
		LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
		VelocityQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
		RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
	}

	FRepMovementVR(FRepMovement& other) : FRepMovement()
	{
		FRepMovementVR();

		AngularVelocity       = other.AngularVelocity      ;
		bRepPhysics           = other.bRepPhysics          ;
		bSimulatedPhysicSleep = other.bSimulatedPhysicSleep;
		LinearVelocity        = other.LinearVelocity       ;
		Location              = other.Location             ;
		Rotation              = other.Rotation             ;
	}

	
	// Functions
	
	void CopyTo(FRepMovement& other) const
	{
		other.AngularVelocity       = AngularVelocity      ;
		other.bRepPhysics           = bRepPhysics          ;
		other.bSimulatedPhysicSleep = bSimulatedPhysicSleep;
		other.LinearVelocity        = LinearVelocity       ;
		other.Location              = Location             ;
		other.Rotation              = Rotation             ;
	}

	bool GatherActorsMovement(AActor* OwningActor)
	{
		//if (/*bReplicateMovement || (RootComponent && RootComponent->GetAttachParent())*/)
		{
			UPrimitiveComponent* RootPrimComp = Cast<UPrimitiveComponent>(OwningActor->GetRootComponent());

			if (RootPrimComp && RootPrimComp->IsSimulatingPhysics())
			{
				FRigidBodyState RBState;

				RootPrimComp->GetRigidBodyState(RBState);

				FillFrom(RBState, OwningActor);

				// Don't replicate movement if we're welded to another parent actor.
				// Their replication will affect our position indirectly since we are attached.
				bRepPhysics = !RootPrimComp->IsWelded();
			}
			else if (RootPrimComp != nullptr)
			{
				// If we are attached, don't replicate absolute position, use AttachmentReplication instead.
				if (RootPrimComp->GetAttachParent() != nullptr)
				{
					return false; // We don't handle attachment rep.

				}
				else
				{
					Location        = FRepMovement::RebaseOntoZeroOrigin(RootPrimComp->GetComponentLocation(), OwningActor);
					Rotation        = RootPrimComp->GetComponentRotation                                                 ();
					LinearVelocity  = OwningActor ->GetVelocity                                                          ();
					AngularVelocity = FVector::ZeroVector                                                                  ;
				}

				bRepPhysics = false;
			}
		}

		/*if (const UWorld* World = GetOwningWorld())
		{
			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				if (APlayerState* PlayerState = PlayerController->PlayerState)
				{
					CurrentPing = PlayerState->ExactPing;
				}
			}
		}*/

		return true;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		return FRepMovement::NetSerialize(Ar, Map, bOutSuccess);
	}
};

template<>
struct TStructOpsTypeTraits<FRepMovementVR> : public TStructOpsTypeTraitsBase2<FRepMovementVR>
{
	enum
	{
		WithNetSerializer          = true,
		WithNetSharedSerialization = true,
	};
};

USTRUCT(BlueprintType)
struct VREXPANSIONPLUGIN_API FVRClientAuthReplicationData
{
	GENERATED_BODY()

public:

	// Constructor

	FVRClientAuthReplicationData() :
		bIsCurrentlyClientAuth(false               ),
		LastActorTransform    (FTransform::Identity),
		TimeAtInitialThrow    (0.0f                ),
		bUseClientAuthThrowing(false               ),
		UpdateRate            (30                  )
	{ }


	// Declares

	bool         bIsCurrentlyClientAuth;
	FTransform   LastActorTransform    ;
	FTimerHandle ResetReplicationHandle;
	float        TimeAtInitialThrow    ;

	// If True and we are using a client auth grip type then we will replicate our throws on release.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRReplication")
		bool bUseClientAuthThrowing;

	// Rate that we will be sending throwing events to the server, not replicated, only serialized.
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadOnly, Category = "VRReplication", meta = (ClampMin = "0", UIMin = "0", ClampMax = "100", UIMax = "100"))
		int32 UpdateRate;
};



// Classes

#if WITH_PHYSX

	class FPhysicsReplicationVR : public FPhysicsReplication
	{
	public:

		// Constructor

		FPhysicsReplicationVR(FPhysScene* PhysScene);


		// Functions

		static bool IsInitialized();

		virtual void OnTick(float DeltaSeconds, TMap<TWeakObjectPtr<UPrimitiveComponent>, FReplicatedPhysicsTarget>& ComponentsToTargets) override
		{
			// Skip all of the custom logic if we aren't the server.
			if (const UWorld* World = GetOwningWorld())
			{
				if (World->GetNetMode() == ENetMode::NM_Client)
				{
					return FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
				}
			}

			const FRigidBodyErrorCorrection& PhysicErrorCorrection = UPhysicsSettings::Get()->PhysicErrorCorrection;

			// Get the ping between this PC & the server.
			const float LocalPing = 0.0f;   //GetLocalPing();

			float CurrentTimeSeconds = 0.0f;

			if (UWorld* OwningWorld = GetOwningWorld())
			{
				CurrentTimeSeconds = OwningWorld->GetTimeSeconds();
			}

			for (auto Itr = ComponentsToTargets.CreateIterator(); Itr; ++Itr)
			{
				/*
				Its been more than half a second since the last update, lets cease using the target as a failsafe.
				Clients will never update with that much latency, and if they somehow are, then they are dropping so many.
				packets that it will be useless to use their data anyway.
				*/
				if ((CurrentTimeSeconds - Itr.Value().ArrivedTimeSeconds) > 0.5f)
				{
					OnTargetRestored(Itr.Key().Get(), Itr.Value());

					Itr.RemoveCurrent();
				}
				else if (UPrimitiveComponent* PrimComp = Itr.Key().Get())
				{
					bool bRemoveItr = false;

					if (FBodyInstance* BI = PrimComp->GetBodyInstance(Itr.Value().BoneName))
					{
						FReplicatedPhysicsTarget& PhysicsTarget = Itr.Value();

						FRigidBodyState& UpdatedState = PhysicsTarget.TargetState;

						bool bUpdated = false;

						if (AActor* OwningActor = PrimComp->GetOwner())
						{
							// Deleted everything here, we will always be the server, I already filtered out clients to default logic.
							{
								/*const*/ float OwnerPing = 0.0f;   // GetOwnerPing(OwningActor, PhysicsTarget);
						
								/*if (UPlayer* OwningPlayer = OwningActor->GetNetOwningPlayer())
								{
									if (APlayerController* PlayerController = OwningPlayer->GetPlayerController(nullptr))
									{
										if (APlayerState* PlayerState = PlayerController->PlayerState)
										{
											OwnerPing = PlayerState->ExactPing;
										}
									}
								}*/

								/*
								Get the total ping - this approximates the time since the update was
								actually generated on the machine that is doing the authoritative sim.
								// NOTE: We divide by 2 to approximate 1-way ping from 2-way ping.
								*/
								const float PingSecondsOneWay = 0.0f;   // (LocalPing + OwnerPing) * 0.5f * 0.001f;


								if (UpdatedState.Flags & ERigidBodyFlags::NeedsUpdate)
								{
									const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay);

									// Need to update the component to match new position.
									static const auto CVarSkipSkeletalRepOptimization = IConsoleManager::Get().FindConsoleVariable(TEXT("p.SkipSkeletalRepOptimization"));

									//Simulated skeletal mesh does its own polling of physics results so we don't need to call this as it'll happen at the end of the physics sim.
									if (/*PhysicsReplicationCVars::SkipSkeletalRepOptimization*/CVarSkipSkeletalRepOptimization->GetInt() == 0 || Cast<USkeletalMeshComponent>(PrimComp) == nullptr)	
									{
										PrimComp->SyncComponentToRBPhysics();
									}

									// Added a sleeping check from the input state as well, we always want to cease activity on sleep.
									if (bRestoredState || ((UpdatedState.Flags & ERigidBodyFlags::Sleeping) != 0))
									{
										bRemoveItr = true;
									}
								}
							}
						}
					}

					if (bRemoveItr)
					{
						OnTargetRestored(Itr.Key().Get(), Itr.Value());

						Itr.RemoveCurrent();
					}
				}
			}

			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Phys Rep Tick!"));
			//FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
		}
	};

	class IPhysicsReplicationFactoryVR : public IPhysicsReplicationFactory
	{
	public:

		virtual FPhysicsReplication* Create(FPhysScene* OwningPhysScene)
		{
			return new FPhysicsReplicationVR(OwningPhysScene);
		}

		virtual void Destroy(FPhysicsReplication* PhysicsReplication)
		{
			if (PhysicsReplication)
			{
				delete PhysicsReplication;
			}
		}
	};

#endif





//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRPhysicsReplicationDelegate, void, Return);

/*static TAutoConsoleVariable<int32> CVarEnableCustomVRPhysicsReplication(
	TEXT("vr.VRExpansion.EnableCustomVRPhysicsReplication"),
	0,
	TEXT("Enable valves input controller that overrides legacy input.\n")
	TEXT(" 0: use the engines default input mapping (default), will also be default if vr.SteamVR.EnableVRInput is enabled\n")
	TEXT(" 1: use the valve input controller. You will have to define input bindings for the controllers you want to support."),
	ECVF_ReadOnly);
*/
