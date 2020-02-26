// Fill out your copyright notice in the Description page of Project Settings.


#include "UI_VRMenu.h"




//Public

// Sets default values
AUI_VRMenu::AUI_VRMenu()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SetupComponents();
}


// Called every frame
void AUI_VRMenu::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


// Protected

// Called when the game starts or when spawned
void AUI_VRMenu::BeginPlay()
{
	Super::BeginPlay();

}


// Private

void AUI_VRMenu::SetupComponents()
{
	WidgetRootComponent = CreateDefaultSubobject<USceneComponent >(TEXT("WidgetRootComponent"));
	MainMenuWidgetRef   = CreateDefaultSubobject<UWidgetComponent>(TEXT("MainMenuWidgetRef"  ));
	ServerListWidgetRef = CreateDefaultSubobject<UWidgetComponent>(TEXT("ServerListWidgetRef"));

	RootComponent = WidgetRootComponent;

	MainMenuWidgetRef  ->SetupAttachment(RootComponent);
	ServerListWidgetRef->SetupAttachment(RootComponent);
}