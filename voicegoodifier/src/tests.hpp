// VoiceGoodifier's self-test is one check per stage and one of the whole chain, each in its own file. None needs OBS
// or plays a sound; `in-obs-test` adds the filters as OBS makes them, inside OBS.
#pragma once

#include "self-test.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace sarxina::voice::test {

// Counts checks and keeps the first few that failed, for one line of results.
struct Tally {
	int passed = 0, total = 0;
	std::string failed;

	void expect(bool ok, const std::string &what)
	{
		total++;
		if (ok)
			passed++;
		else if (failed.size() < 300)
			failed += (failed.empty() ? "; failed: " : ", ") + what;
	}
	void near(double got, double want, double within, const std::string &what)
	{
		char line[200];
		std::snprintf(line, sizeof line, "%s %.4f, expected %.4f", what.c_str(), got, want);
		expect(std::abs(got - want) <= within, line);
	}
	bool report(const char *what)
	{
		const bool ok = passed == total;
		sarxina::test::report(ok, "%s: %d of %d as expected%s: %s", what, passed, total, failed.c_str(),
				      ok ? "OK" : "FAILED");
		return ok;
	}
};

bool eqTest(const char *picturesFolder);
bool gateTest();
bool limiterTest();
bool chainTest();

} // namespace sarxina::voice::test
