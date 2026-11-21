// Fill out your copyright notice in the Description page of Project Settings.


#include "MovementSignificanceSubsystem.h"
#include "NavigationInvokerComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

static TAutoConsoleVariable<bool> CVarCrowdSignificanceDebug(
    TEXT("crowd.Significance.Debug"),
    false,
    TEXT("Draw crowd significance tier above each registered character")
);

UMovementSignificanceComponent::UMovementSignificanceComponent() {
    PrimaryComponentTick.bCanEverTick = false;

    MidSettings.MovementTickInterval = 0.033f;

    FarSettings.MovementTickInterval = 0.08f;
    FarSettings.MaxSimulationTimeStep = 0.1f;
    FarSettings.isUseNavWalking = true;
    FarSettings.isUseAvoidance = false;
    FarSettings.InvokerGenerationRadius = 1000.f;
    FarSettings.InvokerRemovalRadius = 3000.f;

    HiddenSettings.MovementTickInterval = 0.25f;
    HiddenSettings.MaxSimulationTimeStep = 0.125f;
    HiddenSettings.isUseNavWalking = true;
    HiddenSettings.isSweepWhileNavWalking = false;
    HiddenSettings.isUseAvoidance = false;
    HiddenSettings.InvokerGenerationRadius = 1500.f;
    HiddenSettings.InvokerRemovalRadius = 3000.f;
}

void UMovementSignificanceComponent::BeginPlay() {
    Super::BeginPlay();
    if (UMovementSignificanceSubsystem* Subsystem = GetWorld()->GetSubsystem<UMovementSignificanceSubsystem>()) {
        Subsystem->Register(this);
    }
    if (const UNavigationInvokerComponent* Invoker = GetOwner()->FindComponentByClass<UNavigationInvokerComponent>()) {
        isNavInvoker = true;
        DefaultInvokerGenerationRadius = Invoker->GetGenerationRadius();
        DefaultInvokerRemovalRadius = Invoker->GetRemovalRadius();
    }
    if (UMovementSignificanceSubsystem* Subsystem = GetWorld()->GetSubsystem<UMovementSignificanceSubsystem>()) {
        Subsystem->Register(this);
    }
}

void UMovementSignificanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (UMovementSignificanceSubsystem* Subsystem = GetWorld()->GetSubsystem<UMovementSignificanceSubsystem>()) {
        Subsystem->Unregister(this);
    }
    Super::EndPlay(EndPlayReason);
}

const FSignificanceTierSettings& UMovementSignificanceComponent::GetSettings(ECrowdSignificance InTier) const {
    switch (InTier)
    {
        case ECrowdSignificance::Mid: return MidSettings;
        case ECrowdSignificance::Far: return FarSettings;
        case ECrowdSignificance::Hidden: return HiddenSettings;
        default: return NearSettings;
    }
}

ECrowdSignificance UMovementSignificanceComponent::ComputeTier(const FVector& ViewLocation) const {
    const ACharacter* Character = GetOwner<ACharacter>();
    if (!Character || Character->IsPlayerControlled()) {
        return ECrowdSignificance::Near;
    }

    const float Distance = FVector::Dist(ViewLocation, Character->GetActorLocation());

    auto CheckIsInTier = [&](float Boundary, ECrowdSignificance BoundaryTier) {
        float HysteresisOffset = Tier <= BoundaryTier ? Hysteresis : -Hysteresis;
        return Distance < (Boundary + HysteresisOffset);
    };

    if (CheckIsInTier(NearDistanceUpperBound, ECrowdSignificance::Near)) {
        return ECrowdSignificance::Near;
    }
    const USkeletalMeshComponent* Mesh = Character->GetMesh();
    if (Mesh && !Mesh->WasRecentlyRendered(HiddenGraceTime)) {
        return ECrowdSignificance::Hidden;
    }
    return CheckIsInTier(MidDistanceUpperBound, ECrowdSignificance::Mid) ? ECrowdSignificance::Mid : ECrowdSignificance::Far;
}

bool UMovementSignificanceComponent::Evaluate(const FVector& ViewLocation) {
    const ECrowdSignificance NewTier = ComputeTier(ViewLocation);
    bool isChanged = false;
    if (NewTier != Tier || !isTierApplied) {
        ApplyTier(NewTier);
        isChanged = true;
    }
    // retried every evaluation: mode can't be swapped while falling/grabbing/swimming/etc
    SyncMovementMode();
    return isChanged;
}

void UMovementSignificanceComponent::ApplyTier(ECrowdSignificance NewTier) {
    const ECrowdSignificance OldTier = Tier;
    Tier = NewTier;
    isTierApplied = true;

    const FSignificanceTierSettings& Settings = GetSettings(NewTier);
    ACharacter* Character = GetOwner<ACharacter>();
    if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr) {
        Movement->SetComponentTickInterval(Settings.MovementTickInterval);
        Movement->MaxSimulationTimeStep = Settings.MaxSimulationTimeStep;
        Movement->bSweepWhileNavWalking = Settings.isSweepWhileNavWalking;
        Movement->SetAvoidanceEnabled(Settings.isUseAvoidance);
    }
    if (isNavInvoker) {
        ApplyInvokerSettings(Settings);
    }
    if (OldTier != NewTier) {
        OnSignificanceChanged.Broadcast(NewTier, OldTier);
    }
}

void UMovementSignificanceComponent::ApplyInvokerSettings(const FSignificanceTierSettings& Settings) {
    UNavigationInvokerComponent* Invoker = GetOwner()->FindComponentByClass<UNavigationInvokerComponent>();
    if (!Invoker) {
        return;
    }
    const float Generation = Settings.InvokerGenerationRadius > 0.f ? Settings.InvokerGenerationRadius : DefaultInvokerGenerationRadius;
    const float Removal = Settings.InvokerRemovalRadius > 0.f ? Settings.InvokerRemovalRadius : DefaultInvokerRemovalRadius;
    if (FMath::IsNearlyEqual(Generation, Invoker->GetGenerationRadius()) && FMath::IsNearlyEqual(Removal, Invoker->GetRemovalRadius())) {
        return;
    }
    Invoker->SetGenerationRadii(Generation, Removal);
    // SetGenerationRadii only stores the values; re-activation re-registers the invoker with the new radii
    if (Invoker->IsActive()) {
        Invoker->Deactivate();
        Invoker->Activate(true);
    }
}

void UMovementSignificanceComponent::SyncMovementMode() {
    ACharacter* Character = GetOwner<ACharacter>();
    UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
    if (!Movement) {
        return;
    }
    const EMovementMode WantedMode = GetSettings(Tier).isUseNavWalking ? MOVE_NavWalking : MOVE_Walking;
    // only swap between the two ground modes; falling, custom (grab) etc. are left alone.
    // landing returns to the last ground mode, so a nav walker that jumps lands nav walking
    const bool isOnGround = Movement->MovementMode == MOVE_Walking || Movement->MovementMode == MOVE_NavWalking;
    if (!isOnGround) {
        return;
    }
    if (WantedMode == MOVE_NavWalking) {
        // if no navmesh here yet (invoker tiles not built, outside generated area) exit.
        // Movement component would drop back to Walking anyway
        FNavLocation NavFloor;
        if (!Movement->FindNavFloor(Character->GetNavAgentLocation(), NavFloor)) {
            return;
        }
    }
    if (Movement->MovementMode != WantedMode) {
        Movement->SetMovementMode(WantedMode);
    }
}


/*
 * Subsystem
 */
void UMovementSignificanceSubsystem::Register(UMovementSignificanceComponent* Component) {
    Components.AddUnique(Component);
}

void UMovementSignificanceSubsystem::Unregister(UMovementSignificanceComponent* Component) {
    Components.RemoveSingleSwap(Component);
}

void UMovementSignificanceSubsystem::Tick(float DeltaTime) {
    Super::Tick(DeltaTime);

    APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
    if (!PlayerController || Components.IsEmpty()) {
        return;
    }
    FVector ViewLocation;
    FRotator ViewRotation;
    PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

    const auto ComponentsCount = Components.Num();
    // spread a full pass over FullUpdatePeriodDuration: each frame evaluates its share of the list
    Accumulator = FMath::Min(
        Accumulator + ((ComponentsCount * DeltaTime) / FullUpdatePeriodDuration),
        (float)ComponentsCount
    );
    int32 EvalsCount = FMath::Min(
        FMath::FloorToInt(Accumulator),
        MaxEvaluationsPerFrame
    );
    Accumulator -= EvalsCount;
    while (EvalsCount > 0 && !Components.IsEmpty()) {
        EvalsCount -= 1;
        if (Cursor >= Components.Num()) {
            Cursor = 0;
        }
        if (UMovementSignificanceComponent* Component = Components[Cursor].Get()) {
            Component->Evaluate(ViewLocation);
            ++Cursor;
        }
        else {
            Components.RemoveAtSwap(Cursor);
        }
    }

#if ENABLE_DRAW_DEBUG
    if (CVarCrowdSignificanceDebug.GetValueOnGameThread())
    {
        static const FColor TierColors[] = { FColor::Green, FColor::Yellow, FColor::Orange, FColor::Red };
        static const TCHAR* TierNames[] = { TEXT("Near"), TEXT("Mid"), TEXT("Far"), TEXT("Hidden") };
        for (const TWeakObjectPtr<UMovementSignificanceComponent>& Weak : Components)
        {
            if (const UMovementSignificanceComponent* Component = Weak.Get())
            {
                const int32 TierIndex = (int32)Component->GetSignificance();
                DrawDebugString(GetWorld(), Component->GetOwner()->GetActorLocation() + FVector(0, 0, 120),
                    TierNames[TierIndex], nullptr, TierColors[TierIndex], 0.f, true);
            }
        }
    }
#endif
}
