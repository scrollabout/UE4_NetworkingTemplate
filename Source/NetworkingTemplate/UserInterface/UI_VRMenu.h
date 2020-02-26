// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
// Includes

// Unreal
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/WidgetComponent.h"
#include "UserInterface/UI_WidgetActorInterface_Base.h"
#include "UI_VRMenu.generated.h"

/**
 * 
 */
UCLASS()
class NETWORKINGTEMPLATE_API AUI_VRMenu : public AUI_WidgetActorInterface_Base
{
	GENERATED_BODY()
	
public:

	// Constructors

	// Sets default values for this actor's properties
	AUI_VRMenu();

	// Functions

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite) USceneComponent*  WidgetRootComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite) UWidgetComponent* MainMenuWidgetRef;
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite) UWidgetComponent* ServerListWidgetRef;

protected:
	// AActor

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

private:

	void SetupComponents();
};
