// Bridges CLEARANCE's live airspace state into the Simulink-generated
// radar DSP. Console command `clearance.radar.mbd.probe`:
//   1. Finds all AClearanceRadarSite actors in the world.
//   2. For each site, grabs the current FAircraftState list from the
//      airspace manager.
//   3. Synthesises a receive-window IQ cube from the aircraft returns
//      (delayed LFM chirp copies with per-element steering-vector
//      phase + slow-time Doppler phase + AWGN).
//   4. Steps the Simulink model once (radar_step) via FRadarWrapper.
//   5. Logs the detections and compares to ground-truth ranges.
//
// This is the money integration: same simulator, same aircraft, real
// Simulink signal processor detecting them. Portfolio demonstrates
// "Simulink autopilot flies the aircraft; Simulink radar detects
// them; same generated-C pipeline for both." - TripleA

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Safety/ClearanceRadarSite.h"
#include "Safety/ClearanceRadar.h"
#include "Core/CLEARANCETypes.h"
#include "RadarWrapper.h"
#include <cmath>

namespace ClearanceRadarMBDProbe
{
	// Match the Simulink model's compiled-in constants. Any drift here
	// vs the .slx configuration would silently corrupt detection
	// output - keep this table synced with model/radar_params.m and
	// model/waveform_params.m. - TripleA
	constexpr int32  NSpp        = FClearanceRadarConstants::NRangeSamples;   // 2500
	constexpr int32  NEl         = FClearanceRadarConstants::NElements;       // 8
	constexpr int32  NPulses     = FClearanceRadarConstants::NPulses;         // 16
	constexpr int32  NTx         = FClearanceRadarConstants::NTxSamples;      // 25
	constexpr double FsHz        = 250.0e3;
	constexpr double PriSec      = 10.0e-3;
	constexpr double TauSec      = 100.0e-6;
	constexpr double BwHz        = 100.0e3;
	constexpr double Fc          = 2.8e9;
	constexpr double C           = 2.99792458e8;
	constexpr double LambdaM     = C / Fc;                                    // ~0.107 m
	constexpr double ElemSpacing = 0.5 * LambdaM;                             // lambda/2
	constexpr double PI2         = 6.283185307179586;
	constexpr double NoiseStd    = 1.0;

	// Column-major indexing to match Simulink port dimension
	// [Nspp x NEl x NPulses]. Element (r, e, p) with 0-based indices.
	FORCEINLINE int32 CubeIdx(int32 r, int32 e, int32 p)
	{
		return (p * NEl + e) * NSpp + r;
	}

	// Zero-centred element positions along array x-axis. Matches
	// array_params.m: x_elements = ((0:N-1) - (N-1)/2) * lambda/2.
	FORCEINLINE double ElementX(int32 ElemIdx)
	{
		return (static_cast<double>(ElemIdx) - 0.5 * (NEl - 1)) * ElemSpacing;
	}

	// Build the LFM chirp reference into the wrapper's TxReal/TxImag
	// arrays. Same math as model/lfm_waveform.m. - TripleA
	static void FillLfmChirp(FClearanceRadarInputs& In)
	{
		const double ChirpRate = BwHz / TauSec;   // Hz/s
		for (int32 k = 0; k < NTx; ++k)
		{
			const double t = static_cast<double>(k) / FsHz;
			const double phi = PI2 * (-0.5 * BwHz * t + 0.5 * ChirpRate * t * t);
			In.TxReal[k] = FMath::Cos(phi);
			In.TxImag[k] = FMath::Sin(phi);
		}
	}

	// Simple Gaussian noise generator - Box-Muller from two uniforms.
	FORCEINLINE void GaussianComplex(double& OutRe, double& OutIm)
	{
		const double u1 = FMath::Max(1e-12, FMath::FRand());
		const double u2 = FMath::FRand();
		const double r  = FMath::Sqrt(-2.0 * FMath::Loge(u1));
		OutRe = r * FMath::Cos(PI2 * u2) / FMath::Sqrt(2.0);
		OutIm = r * FMath::Sin(PI2 * u2) / FMath::Sqrt(2.0);
	}

	struct FTargetGeometry
	{
		double RangeM       = 0.0;
		double AngleRad     = 0.0;      // radians from array broadside
		double VelocityMps  = 0.0;      // positive = closing
		double Amplitude    = 1.0;
		FName  Callsign;
	};

	// Turn a live FAircraftState + radar-site pose into the geometry
	// values the IQ synthesiser needs. Radar site sits at
	// Site->SitePositionNm; broadside is defined by the sim's east/
	// north convention (X = east, Y = north). - TripleA
	static FTargetGeometry TargetFromAircraft(
		const FAircraftState& Ac,
		const FVector2D&      SitePosNm,
		const UClearanceRadar* Radar)
	{
		FTargetGeometry T;
		T.Callsign = Ac.Callsign;

		// Range (aircraft position is in nm relative to sector centre)
		const FVector2D RelNm = FVector2D(Ac.Position.X, Ac.Position.Y) - SitePosNm;
		const double    RangeNm = RelNm.Size();
		T.RangeM = RangeNm * 1852.0;

		// Bearing from radar site to aircraft (rad from broadside, +x)
		T.AngleRad = FMath::Atan2(RelNm.X, RelNm.Y);   // 0 = north = +y

		// Radial velocity (closing positive). Aircraft velocity in nm/s
		// projected onto the range direction (unit vector RelNm/|RelNm|).
		const FVector2D VelNmPerSec(Ac.Velocity.X, Ac.Velocity.Y);
		const FVector2D VelMpsVec  = VelNmPerSec * 1852.0;
		const double    RangeM     = FMath::Max(1.0, RelNm.Size() * 1852.0);
		const double    UnitX      = (RelNm.X * 1852.0) / RangeM;
		const double    UnitY      = (RelNm.Y * 1852.0) / RangeM;
		// Radial closing = -velocity dot range-unit
		T.VelocityMps = -(VelMpsVec.X * UnitX + VelMpsVec.Y * UnitY);

		// Amplitude - fixed unit for the probe; realistic radar equation
		// scaling would use RCS + range^-4 but we're testing the DSP,
		// not the link budget here.
		T.Amplitude = 1.0;

		return T;
	}

	static void SynthesiseCube(
		FClearanceRadarInputs&        In,
		const TArray<FTargetGeometry>& Targets)
	{
		const double ChirpRate = BwHz / TauSec;

		// Zero out the cube first
		const int32 CubeSize = NSpp * NEl * NPulses;
		FMemory::Memzero(In.RxCubeReal, sizeof(double) * CubeSize);
		FMemory::Memzero(In.RxCubeImag, sizeof(double) * CubeSize);

		// Precompute chirp samples once (identical for every target)
		TArray<double> ChirpRe; ChirpRe.SetNumUninitialized(NTx);
		TArray<double> ChirpIm; ChirpIm.SetNumUninitialized(NTx);
		for (int32 k = 0; k < NTx; ++k)
		{
			const double t = static_cast<double>(k) / FsHz;
			const double phi = PI2 * (-0.5 * BwHz * t + 0.5 * ChirpRate * t * t);
			ChirpRe[k] = FMath::Cos(phi);
			ChirpIm[k] = FMath::Sin(phi);
		}

		for (const FTargetGeometry& T : Targets)
		{
			const int32 DelaySamples = static_cast<int32>(FMath::RoundToInt(2.0 * T.RangeM / C * FsHz));
			if (DelaySamples < 0 || DelaySamples + NTx > NSpp)
			{
				continue;   // target outside unambiguous receive window
			}

			const double FDoppler = 2.0 * T.VelocityMps / LambdaM;

			for (int32 p = 0; p < NPulses; ++p)
			{
				const double DopPhase = PI2 * FDoppler * static_cast<double>(p) * PriSec;
				const double DopRe    = FMath::Cos(DopPhase);
				const double DopIm    = FMath::Sin(DopPhase);

				for (int32 e = 0; e < NEl; ++e)
				{
					const double SteerPhase = PI2 * ElementX(e) * FMath::Sin(T.AngleRad) / LambdaM;
					const double SteerRe    = FMath::Cos(SteerPhase);
					const double SteerIm    = FMath::Sin(SteerPhase);

					// Combined per-element per-pulse complex weight
					const double WRe = DopRe * SteerRe - DopIm * SteerIm;
					const double WIm = DopRe * SteerIm + DopIm * SteerRe;

					for (int32 k = 0; k < NTx; ++k)
					{
						// tx sample scaled by amplitude and weighted
						const double TxRe = ChirpRe[k];
						const double TxIm = ChirpIm[k];
						const double AmpRe = T.Amplitude * (WRe * TxRe - WIm * TxIm);
						const double AmpIm = T.Amplitude * (WRe * TxIm + WIm * TxRe);

						const int32 Idx = CubeIdx(DelaySamples + k, e, p);
						In.RxCubeReal[Idx] += AmpRe;
						In.RxCubeImag[Idx] += AmpIm;
					}
				}
			}
		}

		// Per-element AWGN over the whole cube
		for (int32 p = 0; p < NPulses; ++p)
		{
			for (int32 e = 0; e < NEl; ++e)
			{
				for (int32 r = 0; r < NSpp; ++r)
				{
					double NRe, NIm;
					GaussianComplex(NRe, NIm);
					const int32 Idx = CubeIdx(r, e, p);
					In.RxCubeReal[Idx] += NoiseStd * NRe;
					In.RxCubeImag[Idx] += NoiseStd * NIm;
				}
			}
		}
	}

	static void RunProbe(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RadarMBDProbe] No world."));
			return;
		}

		int32 NumSitesFound = 0;
		for (TActorIterator<AClearanceRadarSite> It(World); It; ++It)
		{
			AClearanceRadarSite* Site = *It;
			if (!Site || !Site->Radar || !Site->Radar->IsEnabled()) { continue; }
			++NumSitesFound;

			// Find the airspace manager
			AClearanceAirspaceManager* Manager = nullptr;
			for (TActorIterator<AClearanceAirspaceManager> Am(World); Am; ++Am)
			{
				Manager = *Am;
				break;
			}
			if (!Manager)
			{
				UE_LOG(LogTemp, Warning, TEXT("[RadarMBDProbe] No airspace manager."));
				return;
			}

			// Gather aircraft into radar-relative geometry. Range beyond
			// the unambiguous window gets wrapped (real surveillance
			// radars alias distant targets into the ambig window - a
			// target at 2*R_unamb appears at zero range). - TripleA
			const TArray<FAircraftState> All = Manager->GetAllAircraftStates();
			const FVector2D SitePosNm = Site->Radar->SitePositionNm;
			const double RUnambM = C * PriSec * 0.5;   // ~37.5 km

			TArray<FTargetGeometry> Targets;
			Targets.Reserve(All.Num());
			UE_LOG(LogTemp, Warning, TEXT("=== [%s] RadarMBDProbe: %d aircraft in sector ==="),
				*Site->SiteName.ToString(), All.Num());

			for (const FAircraftState& Ac : All)
			{
				FTargetGeometry T = TargetFromAircraft(Ac, SitePosNm, Site->Radar);
				if (T.RangeM <= 0.0) { continue; }

				const double TrueRangeM = T.RangeM;
				T.RangeM = FMath::Fmod(TrueRangeM, RUnambM);   // range aliasing

				UE_LOG(LogTemp, Warning,
					TEXT("  truth  %s : R=%.2f km (true), R'=%.2f km (aliased), v=%.1f m/s, angle=%.1f deg"),
					*T.Callsign.ToString(), TrueRangeM / 1000.0, T.RangeM / 1000.0,
					T.VelocityMps, FMath::RadiansToDegrees(T.AngleRad));

				Targets.Add(T);
			}

			// Build wrapper input
			FClearanceRadarInputs In;
			In.LookAngleRad = 0.0;   // broadside look for the probe
			FillLfmChirp(In);
			SynthesiseCube(In, Targets);

			// Run the model
			FRadarWrapper Wrapper;
			Wrapper.Initialize();
			const double T0 = FPlatformTime::Seconds();
			const FClearanceRadarOutputs Out = Wrapper.Step(In);
			const double ElapsedMs = (FPlatformTime::Seconds() - T0) * 1000.0;

			UE_LOG(LogTemp, Warning,
				TEXT("  Simulink radar_step: %d detections, %.2f ms"),
				Out.NumDetections, ElapsedMs);
			for (int32 i = 0; i < Out.NumDetections; ++i)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("  detect %d : R=%.2f km, v=%.1f m/s, SNR=%.1f dB"),
					i,
					Out.Detections[i].RangeMetres / 1000.f,
					Out.Detections[i].VelocityMps,
					Out.Detections[i].SnrDb);
			}
			Wrapper.Shutdown();
		}

		if (NumSitesFound == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RadarMBDProbe] No enabled radar sites in world."));
		}
	}
}

static FAutoConsoleCommandWithWorld GClearanceRadarMBDProbeCmd(
	TEXT("clearance.radar.mbd.probe"),
	TEXT("Run one CPI of the Simulink radar DSP against the live aircraft state. Logs detections + ground truth for A/B comparison."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&ClearanceRadarMBDProbe::RunProbe));

// -----------------------------------------------------------------------
// Live toggle for the Simulink DSP path on all radar sites in the world.
// Enable to route every site's Tracks map through radar_step; disable
// to hand control back to the built-in analytic sweep. The scope UI
// reads the Tracks map either way, so the toggle is invisible to the
// operator except for the source of the blips changing. - TripleA
// -----------------------------------------------------------------------
namespace ClearanceRadarMBDToggle
{
	static void SetAll(UWorld* World, bool bEnable)
	{
		if (!World) { return; }
		int32 N = 0;
		for (TActorIterator<AClearanceRadarSite> It(World); It; ++It)
		{
			AClearanceRadarSite* Site = *It;
			if (Site && Site->Radar)
			{
				Site->Radar->bUseSimulinkDSP = bEnable;
				++N;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[RadarMBD] %s Simulink DSP on %d radar site(s)"),
			bEnable ? TEXT("ENABLED") : TEXT("disabled"), N);
	}
}

static FAutoConsoleCommandWithWorld GClearanceRadarMBDEnableCmd(
	TEXT("clearance.radar.mbd.enable"),
	TEXT("Route every radar site's Tracks map through the Simulink DSP. Scope blips become Simulink detections."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W){ ClearanceRadarMBDToggle::SetAll(W, true); }));

static FAutoConsoleCommandWithWorld GClearanceRadarMBDDisableCmd(
	TEXT("clearance.radar.mbd.disable"),
	TEXT("Revert every radar site to the built-in analytic sweep for scope blips."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W){ ClearanceRadarMBDToggle::SetAll(W, false); }));
