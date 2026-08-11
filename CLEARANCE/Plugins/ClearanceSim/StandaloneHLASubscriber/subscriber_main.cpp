// ============================================================================
// clearance_hla_subscriber - standalone HLA federate that joins the CLEARANCE
// federation and prints every ATCManagedAircraft attribute update it receives.
// Companion / sibling to StandaloneDDSSubscriber - same "prove the wire works
// end-to-end from a second process" pattern, but on the IEEE 1516-2010 HLA
// side.
//
// Live demo: launch rtinode.exe, run CLEARANCE and `clearance.hla.join`,
// then run this executable in another terminal. As CLEARANCE publishes
// attribute updates for its aircraft, this subscriber prints one line per
// received sample. The two federates are in the same federation execution;
// the standard HLA plumbing (createFederationExecution -> joinFederationExecution
// -> subscribeObjectClassAttributes -> reflectAttributeValues callbacks) is
// what proves the interoperability wire is real. - TripleA
// ============================================================================

#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>

#include <RTI/RTI1516.h>
#include <RTI/RTIambassador.h>
#include <RTI/RTIambassadorFactory.h>
#include <RTI/NullFederateAmbassador.h>
#include <RTI/encoding/BasicDataElements.h>

using namespace rti1516e;

namespace
{
	std::atomic<bool> GShutdown{ false };

	// ANSI colour codes so the subscriber's console output stands out on
	// the demo video. Purple accent for HLA to match the panel activity
	// indicator convention used elsewhere in the sim. - TripleA
	constexpr const char* kReset  = "\033[0m";
	constexpr const char* kPurple = "\033[95m";
	constexpr const char* kCyan   = "\033[96m";
	constexpr const char* kDim    = "\033[90m";
	constexpr const char* kGreen  = "\033[92m";
	constexpr const char* kYellow = "\033[93m";

	inline std::string Narrow(const std::wstring& W)
	{
		return std::string(W.begin(), W.end());
	}

	void OnSignal(int)
	{
		GShutdown = true;
	}
}

// Subscriber's callback receiver. Every reflectAttributeValues fired by the
// RTI when a peer federate updates an ATCManagedAircraft instance lands
// here - we decode the four attribute values and pretty-print one line.
// - TripleA
class ClearanceHLASubscriberAmbassador : public NullFederateAmbassador
{
public:
	AttributeHandle AttrSquawkCode;
	AttributeHandle AttrFlightPhase;
	AttributeHandle AttrActiveClearance;
	AttributeHandle AttrATCFacility;

	std::atomic<std::uint64_t> UpdatesReceived{ 0 };

	// FlightPhase enum decoder - matches the CLEARANCE FOM's FlightPhaseEnum
	// datatype (Enroute=0, Approach=1, Landing=2, GoAround=3, Departing=4,
	// Exiting=5). - TripleA
	static const char* PhaseName(std::uint16_t P)
	{
		switch (P)
		{
			case 0: return "Enroute";
			case 1: return "Approach";
			case 2: return "Landing";
			case 3: return "GoAround";
			case 4: return "Departing";
			case 5: return "Exiting";
			default: return "Unknown";
		}
	}

	virtual void reflectAttributeValues(
		ObjectInstanceHandle /*Object*/,
		const AttributeHandleValueMap& Values,
		const VariableLengthData& /*UserTag*/,
		OrderType /*SentOrder*/,
		TransportationType /*Transport*/,
		SupplementalReflectInfo /*Info*/) override
	{
		std::uint16_t Squawk = 0;
		std::uint16_t Phase  = 0;
		std::wstring  Clearance;
		std::wstring  Facility;

		auto ReadInt = [](const VariableLengthData& V) -> std::uint16_t
		{
			HLAinteger16BE E;
			E.decode(V);
			return static_cast<std::uint16_t>(E.get());
		};
		auto ReadStr = [](const VariableLengthData& V) -> std::wstring
		{
			HLAunicodeString E;
			E.decode(V);
			return E.get();
		};

		if (auto it = Values.find(AttrSquawkCode);      it != Values.end()) Squawk    = ReadInt(it->second);
		if (auto it = Values.find(AttrFlightPhase);     it != Values.end()) Phase     = ReadInt(it->second);
		if (auto it = Values.find(AttrActiveClearance); it != Values.end()) Clearance = ReadStr(it->second);
		if (auto it = Values.find(AttrATCFacility);     it != Values.end()) Facility  = ReadStr(it->second);

		const std::uint64_t Total = ++UpdatesReceived;
		std::cout
			<< kPurple << "[HLA-SUB] " << kReset
			<< "#" << kDim << Total << kReset << "  "
			<< kCyan << Narrow(Clearance) << kReset
			<< kDim << " -> ATCManagedAircraft " << kReset
			<< " Squawk="   << kGreen  << Squawk                   << kReset
			<< " Phase="    << kYellow << PhaseName(Phase)         << kReset
			<< " Facility=" << Narrow(Facility)
			<< std::endl;
	}
};

int main(int Argc, char** Argv)
{
	std::signal(SIGINT, OnSignal);

	// Args: [federation] [federate] [fom-path]. Defaults match the CLEARANCE
	// panel / console defaults so double-click launch just works when a
	// CLEARANCE instance is publishing on the standard federation. - TripleA
	std::string FederationName = (Argc >= 2) ? Argv[1] : "CLEARANCE";
	std::string FederateName   = (Argc >= 3) ? Argv[2] : "CLEARANCE-Subscriber";
	std::string FomPath        = (Argc >= 4) ? Argv[3]
		: "..\\FOM\\ClearanceRPR-FOM.xml";   // relative to the exe location

	std::cout
		<< kPurple << "clearance_hla_subscriber" << kReset << "\n"
		<< kDim << "IEEE 1516-2010 federate - subscribes to ATCManagedAircraft in federation '"
		<< FederationName << "'\n" << kReset
		<< kDim << "(Ctrl+C to resign)\n\n" << kReset;

	try
	{
		RTIambassadorFactory Factory;
		auto Amb = Factory.createRTIambassador();
		ClearanceHLASubscriberAmbassador Fed;

		Amb->connect(Fed, HLA_EVOKED);
		std::cout << kDim << "[connect] rtinode contact established" << kReset << std::endl;

		// createFederationExecution is idempotent - the CLEARANCE publisher
		// almost certainly created it already. Catch AlreadyExists and
		// continue. - TripleA
		try
		{
			std::vector<std::wstring> FomModules;
			FomModules.emplace_back(FomPath.begin(), FomPath.end());
			Amb->createFederationExecution(std::wstring(FederationName.begin(), FederationName.end()), FomModules);
			std::cout << kDim << "[create] federation execution created" << kReset << std::endl;
		}
		catch (const FederationExecutionAlreadyExists&)
		{
			std::cout << kDim << "[create] federation exists (fine)" << kReset << std::endl;
		}

		Amb->joinFederationExecution(
			std::wstring(FederateName.begin(),   FederateName.end()),
			std::wstring(FederationName.begin(), FederationName.end()));
		std::cout << kDim << "[join] joined as '" << FederateName << "'" << kReset << std::endl;

		ObjectClassHandle AircraftClass = Amb->getObjectClassHandle(
			L"HLAobjectRoot.BaseEntity.PhysicalEntity.Platform.Aircraft.ATCManagedAircraft");
		Fed.AttrSquawkCode      = Amb->getAttributeHandle(AircraftClass, L"SquawkCode");
		Fed.AttrFlightPhase     = Amb->getAttributeHandle(AircraftClass, L"FlightPhase");
		Fed.AttrActiveClearance = Amb->getAttributeHandle(AircraftClass, L"ActiveClearance");
		Fed.AttrATCFacility     = Amb->getAttributeHandle(AircraftClass, L"ATCFacility");

		AttributeHandleSet SubAttrs;
		SubAttrs.insert(Fed.AttrSquawkCode);
		SubAttrs.insert(Fed.AttrFlightPhase);
		SubAttrs.insert(Fed.AttrActiveClearance);
		SubAttrs.insert(Fed.AttrATCFacility);
		Amb->subscribeObjectClassAttributes(AircraftClass, SubAttrs);
		std::cout << kGreen << "[subscribe] listening for ATCManagedAircraft updates..." << kReset
		          << std::endl << std::endl;

		// Pump the ambassador so reflectAttributeValues callbacks fire.
		// HLA_EVOKED requires manual tick calls - 10ms cycles matches a
		// 100Hz callback frequency ceiling, well over the CLEARANCE
		// publisher's ~5Hz emit rate. - TripleA
		while (!GShutdown)
		{
			Amb->evokeMultipleCallbacks(0.010, 0.100);
		}

		std::cout << std::endl << kDim << "[resign]" << kReset << std::endl;
		Amb->resignFederationExecution(NO_ACTION);
		Amb->disconnect();
		return 0;
	}
	catch (const Exception& Ex)
	{
		std::wcerr << L"[FATAL] " << Ex.what() << std::endl;
		return 1;
	}
	catch (const std::exception& Ex)
	{
		std::cerr << "[FATAL] " << Ex.what() << std::endl;
		return 1;
	}
}
