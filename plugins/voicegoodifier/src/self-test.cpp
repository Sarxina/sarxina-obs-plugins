// VoiceGoodifier's self-test (see shared/src/self-test.hpp and tests.hpp): each stage on its own, then the chain.
#include "tests.hpp"

bool runSelfTest(const char *picturesFolder)
{
	using namespace sarxina::voice::test;
	const bool ok = gateTest() & eqTest(picturesFolder) & limiterTest() & chainTest();
	sarxina::test::report(ok, ok ? "passed" : "FAILED");
	return ok;
}
