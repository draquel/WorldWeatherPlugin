// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "WorldWeatherTypes.h"
#include "WorldWeatherState.generated.h"

/**
 * The weather's replication carrier: one always-relevant info actor the authority spawns, holding the last sync point and
 * the lock. Clients resolve the present from it through UWorldWeatherSubsystem; nothing ticks or replicates per frame.
 */
UCLASS(NotBlueprintable)
class WORLDWEATHER_API AWorldWeatherState : public AInfo
{
	GENERATED_BODY()

public:
	AWorldWeatherState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority: publish a new sync point (its Serial already advanced by the subsystem). */
	void Publish(const FWorldWeatherSyncPoint& InSync, bool bInLocked);

	const FWorldWeatherSyncPoint& GetSync() const { return Sync; }
	bool IsLocked() const { return bLocked; }
	bool HasSync() const { return Sync.IsValid(); }

protected:
	UPROPERTY(Replicated)
	FWorldWeatherSyncPoint Sync;

	/** The schedule is stopped. */
	UPROPERTY(Replicated)
	bool bLocked = false;
};
