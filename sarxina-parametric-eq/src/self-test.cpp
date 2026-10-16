// Sarxina Parametric EQ's self-test (see shared/src/self-test.hpp). It needs no OBS and plays no sound.
//
//   1. Each kind of band against the textbook: a bell's gain at its centre and half of it at its edges, a shelf's
//      full gain past its corner and half of it at the corner, a Butterworth pass filter's -3 dB at its cutoff and
//      its attenuation an octave out, for every slope from 6 to 48 dB per octave.
//   2. The filter as it runs: an impulse through the processor, block by block as OBS hands audio over, measured
//      at 400 frequencies against the curve the editor draws, for Alec's stream voice rack.
//   3. The settings survive a trip through OBS's settings data.
// Given a folder, it also paints the editor there (editor.png, editor-96db.png) and writes response.csv: the stream
// voice rack's curve as drawn and as measured from the processor, for plotting against Audition's.
#include "self-test.hpp"

#include "curve-editor.hpp"
#include "eq-design.hpp"
#include "eq-settings.hpp"

#include <QDir>
#include <QFile>
#include <QLayout>
#include <QPixmap>
#include <QTextStream>

#include <cmath>
#include <complex>
#include <string>
#include <vector>

using namespace sarxina;
using namespace sarxina::eq;

namespace {

constexpr double rate = 48000;
constexpr double pi = 3.14159265358979323846;

// Counts checks and keeps the first few that failed, for one line of results.
struct Tally {
	int passed = 0, total = 0;
	std::string failed;

	void expect(double got, double want, double within, const std::string &what)
	{
		total++;
		if (std::abs(got - want) <= within) {
			passed++;
		} else if (failed.size() < 300) {
			char line[160];
			std::snprintf(line, sizeof line, "%s %.3f dB, expected %.3f", what.c_str(), got, want);
			failed += (failed.empty() ? "; failed: " : ", ") + std::string(line);
		}
	}
	bool report(const char *what)
	{
		const bool ok = passed == total;
		test::report(ok, "%s: %d of %d as expected%s: %s", what, passed, total, failed.c_str(), ok ? "OK" : "FAILED");
		return ok;
	}
};

Settings flat()
{
	Settings s = Settings::defaults();
	for (BandSettings &b : s.bands)
		b.on = false;
	return s;
}

double dbOf(const Settings &s, double hz)
{
	return Design::of(s, rate).db(hz, rate);
}

// Alec's stream voice EQ (Audition session SarxinaFinalRealStreamVoice, slot 1), as parse_rack.py reads it and as
// Audition's own window showed it on 2026-10-16: Constant Width, gains +2.1, -2.7, -2.8, -2.7 and +10.6 dB.
Settings streamVoice()
{
	Settings s = Settings::defaults();
	s.constantWidth = true;
	auto set = [&s](int band, bool on, double hz, double db, double q, double width) {
		s.bands[band].on = on;
		s.bands[band].hz = hz;
		s.bands[band].db = db;
		s.bands[band].q = q;
		s.bands[band].width = width;
	};
	set(HighPass, true, 79.55, 0, 2, 40);
	s.bands[HighPass].slope = 48;
	set(LowShelf, true, 40, 0, 2, 20);
	set(Bell1, true, 130.84, 2.053, 2.0, 65.42);
	set(Bell2, true, 246.75, -2.684, 1.5, 164.5);
	set(Bell3, true, 2230.57, -2.842, 10.8, 206.53);
	set(Bell4, true, 4312.46, -2.684, 9.3, 463.71);
	set(Bell5, true, 12800, 0, 2.0, 6400);
	set(HighShelf, true, 13884.18, 10.579, 2, 6942);
	s.bands[HighShelf].steep = true;
	set(LowPass, false, 18000.01, 0, 2, 9000);
	return s;
}

bool textbook()
{
	Tally t;
	// Bells: the gain at the centre; half of it at the edges a Q of 2 puts at f (sqrt(1 + 1/16) -+ 1/4).
	for (double db : {6.0, -12.0}) {
		Settings s = flat();
		s.bands[Bell3] = {true, 1000, db, 2};
		t.expect(dbOf(s, 1000), db, 0.001, "bell centre");
		const double root = std::sqrt(1 + 1.0 / 16);
		t.expect(dbOf(s, 1000 * (root - 0.25)), db / 2, 0.05, "bell lower edge");
		t.expect(dbOf(s, 1000 * (root + 0.25)), db / 2, 0.05, "bell upper edge");
		t.expect(dbOf(s, 40), 0, 0.05, "bell far below");
	}
	// Shelves, both orders: half the gain at the corner, all of it far past the corner, none far before it.
	for (bool steep : {false, true}) {
		Settings s = flat();
		s.bands[LowShelf] = {true, 200, 6, 2, 12, steep};
		t.expect(dbOf(s, 200), 3, 0.01, "low shelf corner");
		t.expect(dbOf(s, 2), 6, 0.05, "low shelf below");
		t.expect(dbOf(s, 20000), 0, 0.05, "low shelf above");
		s = flat();
		s.bands[HighShelf] = {true, 5000, -6, 2, 12, steep};
		t.expect(dbOf(s, 5000), -3, 0.01, "high shelf corner");
		t.expect(dbOf(s, 23999), -6, 0.05, "high shelf at Nyquist");
		t.expect(dbOf(s, 50), 0, 0.05, "high shelf below");
	}
	// Butterworth of N poles: -3.01 dB at the cutoff, and 10 log10(1 + r^(2N)) down an octave beyond it, where r is
	// the octave's frequency ratio after the bilinear transform's warping (tan(pi f / rate) over the cutoff's).
	auto warped = [](double hz) { return std::tan(pi * hz / rate); };
	for (int slope = 6; slope <= 48; slope += 6) {
		const int n = slope / 6;
		auto butterworth = [n](double ratio) { return -10 * std::log10(1 + std::pow(ratio, 2 * n)); };
		Settings s = flat();
		s.bands[HighPass] = {true, 100, 0, 2, slope};
		t.expect(dbOf(s, 100), -3.0103, 0.005, std::to_string(slope) + " dB/oct high-pass cutoff");
		t.expect(dbOf(s, 50), butterworth(warped(100) / warped(50)), 0.01,
			 std::to_string(slope) + " dB/oct high-pass octave");
		s = flat();
		s.bands[LowPass] = {true, 2000, 0, 2, slope};
		t.expect(dbOf(s, 2000), -3.0103, 0.005, std::to_string(slope) + " dB/oct low-pass cutoff");
		t.expect(dbOf(s, 4000), butterworth(warped(4000) / warped(2000)), 0.01,
			 std::to_string(slope) + " dB/oct low-pass octave");
	}
	// Constant Width: a width w at f is the bell of Q f / w, and moving f keeps w (so the Q follows).
	{
		Settings q = flat(), w = flat();
		q.bands[Bell2] = {true, 1500, -6, 1500 / 300.0};
		w.constantWidth = true;
		w.bands[Bell2] = {true, 1500, -6, 9.9, 12, true, 300};
		for (double hz : {300.0, 1200.0, 1500.0, 1800.0, 5000.0})
			t.expect(dbOf(w, hz), dbOf(q, hz), 1e-9, "width 300 Hz against Q 5 at " + std::to_string(int(hz)) + " Hz");
		w.bands[Bell2].hz = 3000;
		t.expect(w.qOf(Bell2), 10, 1e-9, "Q of a 300 Hz width moved to 3 kHz");
	}
	// The master gain moves everything.
	Settings s = flat();
	s.gainDb = -4.5;
	t.expect(dbOf(s, 1000), -4.5, 0.001, "master gain");
	return t.report("the bands against the textbook");
}

// Runs an impulse through the processor in 480-sample blocks (10 ms, as OBS sends them) on two channels.
std::vector<float> impulseResponse(const Settings &settings, size_t length, bool &channelsAgree)
{
	Processor processor;
	processor.setDesign(Design::of(settings, rate));
	std::vector<float> left(length, 0.0f), right(length, 0.0f);
	left[0] = right[0] = 1.0f;
	for (size_t at = 0; at < length; at += 480) {
		const size_t frames = std::min<size_t>(480, length - at);
		processor.process(left.data() + at, frames, 0);
		processor.process(right.data() + at, frames, 1);
	}
	channelsAgree = left == right;
	return left;
}

double measuredDb(const std::vector<float> &response, double hz)
{
	std::complex<double> sum = 0;
	const std::complex<double> step = std::polar(1.0, -2 * pi * hz / rate);
	std::complex<double> turn = 1;
	for (float sample : response) {
		sum += static_cast<double>(sample) * turn;
		turn *= step;
	}
	return 20 * std::log10(std::max(std::abs(sum), 1e-12));
}

bool processing(const char *folder)
{
	const Settings settings = streamVoice();
	const Design design = Design::of(settings, rate);
	bool channelsAgree = false;
	const std::vector<float> response = impulseResponse(settings, 1 << 16, channelsAgree);

	Tally t;
	double worst = 0;
	QString csv = "hz,drawn_db,measured_db\n";
	for (int i = 0; i < 400; i++) {
		const double hz = 20 * std::pow(1000.0, i / 399.0);
		const double drawn = design.db(hz, rate), measured = measuredDb(response, hz);
		// Far down the high-pass the level is below float precision; compare where there is signal.
		if (drawn > -90) {
			t.expect(measured, drawn, 0.01, QString::number(hz, 'f', 0).toStdString() + " Hz");
			worst = std::max(worst, std::abs(measured - drawn));
		}
		csv += QString("%1,%2,%3\n").arg(hz, 0, 'f', 3).arg(drawn, 0, 'f', 5).arg(measured, 0, 'f', 5);
	}
	test::report(channelsAgree, "two channels through the same design come out the same: %s",
		     channelsAgree ? "OK" : "FAILED");
	test::report(true, "stream voice rack: the processor is at most %.4f dB from the drawn curve", worst);
	if (folder && *folder) {
		QDir().mkpath(folder);
		QFile file(QDir(folder).filePath("response.csv"));
		if (file.open(QIODevice::WriteOnly | QIODevice::Text))
			QTextStream(&file) << csv;
	}
	return t.report("stream voice rack, processor against the drawn curve") && channelsAgree;
}

bool roundTrip()
{
	Settings s = streamVoice();
	s.gainDb = 1.5;
	s.wideRange = true;
	s.bands[LowPass] = {true, 9000, 0, 2, 30};
	obs_data_t *data = obs_data_create();
	setDefaults(data);
	write(data, s);
	const Settings back = read(data);
	obs_data_release(data);

	Tally t;
	for (int band = 0; band < BandCount; band++) {
		const BandSettings &a = s.bands[band], &b = back.bands[band];
		const std::string name = bandKey(band);
		const Kind kind = kindOf(band);
		t.expect(a.on == b.on ? 0 : 1, 0, 0, name + " on");
		t.expect(b.hz, a.hz, 1e-9, name + " Hz");
		if (kind == Kind::HighPass || kind == Kind::LowPass)
			t.expect(b.slope, a.slope, 0, name + " slope");
		else
			t.expect(b.db, a.db, 1e-9, name + " gain");
		if (kind == Kind::Bell)
			t.expect(b.q, a.q, 1e-9, name + " Q");
		if (kind == Kind::LowShelf || kind == Kind::HighShelf)
			t.expect(a.steep == b.steep ? 0 : 1, 0, 0, name + " order");
	}
	t.expect(back.gainDb, s.gainDb, 1e-9, "master gain");
	t.expect(back.constantWidth ? 1 : 0, s.constantWidth ? 1 : 0, 0, "constant width");
	for (int band = Bell1; band <= Bell5; band++)
		t.expect(back.bands[band].width, s.bands[band].width, 1e-9, std::string(bandKey(band)) + " width");
	t.expect(back.wideRange ? 1 : 0, 1, 0, "range");
	return t.report("settings through OBS's settings data and back");
}

void pictures(const char *folder)
{
	if (!folder || !*folder)
		return;
	QDir().mkpath(folder);
	int written = 0;
	for (bool wide : {false, true}) {
		Settings s = streamVoice();
		s.wideRange = wide;
		CurveEditor editor(s, rate, nullptr);
		editor.resize(980, 560);
		editor.layout()->activate();
		if (editor.grab().save(QDir(folder).filePath(wide ? "editor-96db.png" : "editor.png")))
			written++;
	}
	test::report(written == 2, "%d pictures of the editor in %s", written, folder);
}

} // namespace

bool runSelfTest(const char *picturesFolder)
{
	const bool ok1 = textbook();
	const bool ok2 = processing(picturesFolder);
	const bool ok3 = roundTrip();
	pictures(picturesFolder);
	const bool passed = ok1 && ok2 && ok3;
	test::report(passed, passed ? "passed" : "FAILED");
	return passed;
}
