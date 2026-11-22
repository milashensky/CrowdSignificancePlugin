// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "MovementSignificanceSubsystem.generated.h"

UENUM(BlueprintType)
enum class ECrowdSignificance : uint8
{
    Near,
    Mid,
    Far,
    Hidden
};

USTRUCT(BlueprintType)
struct FSignificanceTierSettings
{
    GENERATED_BODY()

    // CharacterMovement tick interval, sec. 0 = every frame
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float MovementTickInterval = 0.f;

    // tick the mesh at MovementTickInterval too, so animation only runs on frames movement did.
    // off = mesh keeps its own tick interval
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    bool isSyncMeshTickInterval = true;

    // bigger step = fewer CMC sub-iterations when ticking with a large DeltaTime
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.01", ClampMax = "0.5"), Category = "Movement Significance")
    float MaxSimulationTimeStep = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    bool isUseNavWalking = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    bool isSweepWhileNavWalking = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    bool isUseAvoidance = true;
    // navigation invoker settings for this tier. 0 = invoker component's own values
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float InvokerGenerationRadius = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float InvokerRemovalRadius = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCrowdSignificanceChanged, ECrowdSignificance, NewTier, ECrowdSignificance, OldTier);

/**
 * per-character LOD for movement.
 * evaluated by UMovementSignificanceSubsystem, to ensure evaluation is spread out
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CROWDSIGNIFICANCEPLUGIN_API UMovementSignificanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UMovementSignificanceComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float NearDistanceUpperBound = 3500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float MidDistanceUpperBound = 6000.f;

    // tier boundary is pushed this far outward while inside it, so tiers don't flicker
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float Hysteresis = 500.f;
    // not rendered for this long = hidden, sec
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    float HiddenGraceTime = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    FSignificanceTierSettings NearSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    FSignificanceTierSettings MidSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    FSignificanceTierSettings FarSettings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Significance")
    FSignificanceTierSettings HiddenSettings;

    // a hook for BP-side throttling
    UPROPERTY(BlueprintAssignable, Category = "Movement Significance")
    FOnCrowdSignificanceChanged OnSignificanceChanged;

    UFUNCTION(BlueprintPure, Category = "Movement Significance")
    ECrowdSignificance GetSignificance() const { return Tier; }

    const FSignificanceTierSettings& GetSettings(ECrowdSignificance InTier) const;
    bool Evaluate(const FVector& ViewLocation);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    ECrowdSignificance ComputeTier(const FVector& ViewLocation) const;
    void ApplyTier(ECrowdSignificance NewTier);
    void SyncMovementMode();

    ECrowdSignificance Tier = ECrowdSignificance::Near;
    bool isTierApplied = false;

    void ApplyInvokerSettings(const FSignificanceTierSettings& Settings);

    bool isNavInvoker = false;
    float DefaultInvokerGenerationRadius = 0.f;
    float DefaultInvokerRemovalRadius = 0.f;

    float DefaultMeshTickInterval = 0.f;
};

/**
 *
 */
UCLASS()
class CROWDSIGNIFICANCEPLUGIN_API UMovementSignificanceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
    // one full pass over all registered characters takes this long, sec
    float FullUpdatePeriodDuration = 0.5f;

    void Register(UMovementSignificanceComponent* Component);
    void Unregister(UMovementSignificanceComponent* Component);

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCrowdSignificanceSubsystem, STATGROUP_Tickables); }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override {
        return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
    }

private:
    TArray<TWeakObjectPtr<UMovementSignificanceComponent>> Components;
    int32 Cursor = 0;
    float Accumulator = 0.f;
    int32 MaxEvaluationsPerFrame = 32;
};
