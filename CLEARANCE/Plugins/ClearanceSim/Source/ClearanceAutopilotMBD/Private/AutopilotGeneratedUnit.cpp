// Compilation shim for the Embedded-Coder-generated C source. UE's
// Unreal Build Tool compiles every .cpp under Private/ as part of the
// module - but the generated code lives at
// ThirdParty/AutopilotGenerated/src/autopilot.c, outside that scope.
// Rather than add a UBT-level per-file compilation rule (fragile,
// version-sensitive), this TU wraps the raw .c file in an
// `extern "C"` block and lets the C++ compiler ingest it directly.
// Build.cs adds the generated src/ path to PrivateIncludePaths so the
// include below resolves. - TripleA

#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
extern "C"
{
	#include "autopilot.c"
}
#endif
