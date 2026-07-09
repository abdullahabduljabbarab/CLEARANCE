#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "RadarWrapper.h"
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

	// Emission signature - what this radar looks like to external ELINT.
	// Carried on the DIS Emission PDU (Type 23) beam block. Defaults to a
	// civil airport surveillance profile (ASR-9-ish). Set per-radar for
	// mixed civil/military deployments. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Emission")
	FEmissionSignature EmissionSignature;

	// --- Physics ------------------------------------------------------------
	// Everything below feeds ClearanceRadarEquation::ComputeDetection. When
	// bUsePhysicsDetection is on (default), the raw distance-gate is replaced
	// by a probabilistic paint whose Pd falls out of the monostatic radar
	// equation - target RCS, wavelength, receiver noise floor, and system
	// loss all drive whether a paint lands. Turn it off to get the legacy
	// binary range gate back for A/B testing or scenario debugging.
	// - TripleA

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	bool bUsePhysicsDetection = true;

	// Peak transmit power at the antenna port (kilowatts). ASR-9 civil
	// airport surveillance radar is ~1.4 MW peak (0.1% duty on a 1 us
	// pulse at 1000 PRF - average power is only ~1.4 kW). Long-range
	// en-route (ARSR-4) is ~60 kW peak with pulse compression.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float PeakPowerKilowatts = 1400.f;

	// Transmit antenna gain (dBi). 34 dB is representative of a
	// medium-aperture parabolic reflector at S-band.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float TransmitAntennaGainDb = 34.f;

	// Receive antenna gain (dBi). Same antenna in monostatic operation, so
	// this defaults to matching TransmitAntennaGainDb - split for asymmetric
	// designs (e.g. active-electronically-steered arrays with different
	// TX/RX taper).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float ReceiveAntennaGainDb = 34.f;

	// System loss (dB). Rolled-up feedline + processing + atmospheric
	// absorption. 6 dB is a defensible round number for S-band ground
	// surveillance in clear air; scale up in rain / long slant paths.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float SystemLossDb = 6.f;

	// Receiver noise figure (dB). Modern solid-state receiver front end.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float NoiseFigureDb = 3.f;

	// System noise temperature (K). IEEE reference is 290 K.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float SystemNoiseTemperatureK = 290.f;

	// Receiver matched-filter bandwidth (MHz). B ~= 1/tau for a matched
	// receiver on a rectangular pulse of width tau. 1 MHz matches a 1 us
	// pulse - the ASR-9 baseline.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float ReceiverBandwidthMhz = 1.f;

	// Minimum SNR for reliable detection (dB). Classic threshold for a
	// Swerling-0 non-fluctuating target at Pd = 0.9, Pfa = 1e-6 is 13 dB.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float MinimumDetectableSnrDb = 13.f;

	// Detection-curve slope (dB). Larger = softer transition. 3-6 dB is
	// typical for a search radar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float DetectionSlopeDb = 4.f;

	// Reference RCS for coverage-overlay rendering (m^2). Not used for
	// per-aircraft detection - that reads the aircraft's own wake
	// category. 10 m^2 = "typical narrowbody airliner side-on", a
	// realistic reference target for what an ATC operator expects the
	// scope to cover.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|Physics")
	float CoverageReferenceRcsSqM = 10.f;

	// --- Simulink DSP integration ---------------------------------------
	// When on (default), the site's Tracks map is populated by the
	// Embedded-Coder-generated radar model from the standalone radar-mbd
	// repo instead of the built-in analytic sweep. Synthesises an I/Q
	// cube from the live aircraft state, runs radar_step at
	// SimulinkStepIntervalSeconds cadence, converts detections into
	// FRadarTrack entries matched to the nearest aircraft by aliased
	// range + Doppler. Toggle via clearance.radar.mbd.disable/enable
	// live to A/B against the analytic path. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|MBD")
	bool bUseSimulinkDSP = true;

	// Wall-clock seconds between radar_step calls. Not tied to CPI
	// cadence - one radar_step is a single 4 ms coherent burst but the
	// game only needs an updated picture at a few Hz to feel real.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar|MBD")
	float SimulinkStepIntervalSeconds = 0.5f;

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	UPROPERTY()
	TMap<FName, FRadarTrack> Tracks;

	bool bEnabled = false;
	float SweepAngleDeg = 0.f;   // 0..360, advances each tick
	float SweepPrevDeg = 0.f;
	double RadarClockSeconds = 0.0; // accumulated real time, drives paint timestamps + fade

	// Simulink DSP instance - one per radar site so a fleet runs
	// concurrently without shared globals (matches the reusable-
	// function packaging on the generated C). - TripleA
	FRadarWrapper SimulinkWrapper;
	double LastSimulinkStepSeconds = -1000.0;

	static bool SweepCrossed(float Prev, float Now, float BearingDeg);
	static float BearingDeg(const FVector& PosNm);

	// Runs the Simulink DSP against the current airspace state and
	// replaces the Tracks map with matched detections. Called from
	// Tick() when bUseSimulinkDSP is true. - TripleA
	void UpdateTracksFromSimulinkDSP(float NowSeconds);
};
