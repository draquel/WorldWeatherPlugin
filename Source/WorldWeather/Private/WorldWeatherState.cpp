// Copyright Daniel Raquel. All Rights Reserved.

#include "WorldWeatherState.h"
#include "Net/UnrealNetwork.h"

AWorldWeatherState::AWorldWeatherState()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.0f);
}

void AWorldWeatherState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWorldWeatherState, Sync);
	DOREPLIFETIME(AWorldWeatherState, bLocked);
}

void AWorldWeatherState::Publish(const FWorldWeatherSyncPoint& InSync, bool bInLocked)
{
	Sync = InSync;
	bLocked = bInLocked;
	ForceNetUpdate();
}
