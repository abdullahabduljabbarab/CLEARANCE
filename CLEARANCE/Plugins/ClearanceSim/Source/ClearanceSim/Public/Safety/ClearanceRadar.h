#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceRadar.generated.h"

class AClearanceAirspaceManager;

// A modelled rotating radar sitting on top of the Airspace Manager. Reads truth,
// produces TRACKS - what the radar believes - with detection range, sweep, primary
// vs secondary returns, position jitter and fade-on-loss-of-contact. The visual
// layer renders these instead of the truth, turning an ATC game into a sensor
// simulation. Defence-standard concept. - TripleA
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceRadar : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Radar")
	void SetReferences(AClearanceAirspaceManager* InManager);

	// One tick of the sweep: advance the antenna, paint any aircraft the beam just
	// crossed within range, fade tracks that haven't been painted recently. The radar
	// is a physical device - sweep + fade run on REAL time, never sim-scaled. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Radar")
	void Tick(float RealDeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Radar")
	TArray<FRadarTrack> GetTracks() const;

	UFUNCTION(BlueprintCallable, Category = "Radar")
	bool IsEnabled() const { return bEnabled; }

	UFUNCTION(BlueprintCallable, Category = "Radar")
	void SetEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Radar")
	float GetSweepAngleDeg() const { return SweepAngleDeg; }

	UFUNCTION(BlueprintCallable, Category = "Radar")
	void Reset();

	// --- Tuning -------------------------------------------------------------

	// How far the radar can see, nm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	float RangeNm = 80.f;

	// Antenna rotations per minute. 12rpm = one sweep every 5s, typical for enroute.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	float SweepRpm = 12.f;

	// Probability per paint that the secondary (transponder) return is good. Anything
	// less than this becomes a primary-only blip with no callsign. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	float SecondaryReturnChance = 0.95f;

	// Sensor noise on the painted position (nm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	float PositionJitterNm = 0.1f;

	// Seconds without a paint before a track is dropped entirely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	float TrackFadeSeconds = 8.f;

	// Where this radar sits in sim coordinates (nm). Default (0,0) = sector centre,
	// which matches the original single-radar behaviour. Multiple radars at
	// different sites can set this independently. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	FVector2D SitePositionNm = FVector2D::ZeroVector;

	// Human-readable name for the radar site (shown in fused track tags).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Tuning")
	FName SiteName = TEXT("CENTRE");

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	UPROPERTY()
	TMap<FName, FRadarTrack> Tracks;

	bool bEnabled = false;
	float SweepAngleDeg = 0.f;   // 0..360, advances each tick
	float SweepPrevDeg = 0.f;
	double RadarClockSeconds = 0.0; // accumulated real time, drives paint timestamps + fade

	static bool SweepCrossed(float Prev, float Now, float BearingDeg);
	static float BearingDeg(const FVector& PosNm);
};
