// Compilation shim for the Embedded-Coder-generated missile C source.
// Same pattern as AutopilotGeneratedUnit.cpp: a single TU wraps the raw
// .c files in an `extern "C"` block so UBT compiles them as part of the
// module without needing per-file compilation rules. Build.cs adds the
// generated src/ path to PrivateIncludePaths. - TripleA

#if CLEARANCE_MISSILE_MBD_HAVE_CODEGEN
extern "C"
{
	#include "missile.c"
	#include "rtGetInf.c"
	#include "rt_nonfinite.c"

	// The generated missile.h declares this symbol as extern; Embedded
	// Coder defines it inside ert_main.c for standalone builds. We don't
	// want ert_main.c's main() here (would clash with UE's), so we define
	// the symbol directly in this shim TU. Only referenced when the
	// RT_MALLOC factory fails; needs to link cleanly regardless. - TripleA
	const char* RT_MEMORY_ALLOCATION_ERROR = "memory allocation error";
}
#endif
