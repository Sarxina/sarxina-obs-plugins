// Runs KeepTalking's word counter (src/word-counter.hpp) over a WAV file, to check it against a transcript:
//     keeptalking-wordcount <file.wav> [minutes per line]
// Takes 16-bit or float WAV at any rate; with more than one channel it listens to the first, as the plugin does.
#include "word-counter.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: keeptalking-wordcount <file.wav> [minutes per line]\n");
		return 2;
	}
	const double minutes = argc > 2 ? std::atof(argv[2]) : 5;
	FILE *file = std::fopen(argv[1], "rb");
	if (!file) {
		std::fprintf(stderr, "cannot open %s\n", argv[1]);
		return 1;
	}
	char riff[12];
	if (std::fread(riff, 1, 12, file) != 12 || std::memcmp(riff, "RIFF", 4) || std::memcmp(riff + 8, "WAVE", 4)) {
		std::fprintf(stderr, "not a WAV file\n");
		return 1;
	}
	uint16_t format = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	for (;;) {
		char id[4];
		uint32_t size = 0;
		if (std::fread(id, 1, 4, file) != 4 || std::fread(&size, 4, 1, file) != 1) {
			std::fprintf(stderr, "no data in the WAV file\n");
			return 1;
		}
		if (!std::memcmp(id, "fmt ", 4)) {
			std::vector<unsigned char> fmt(size);
			std::fread(fmt.data(), 1, size, file);
			std::memcpy(&format, &fmt[0], 2);
			std::memcpy(&channels, &fmt[2], 2);
			std::memcpy(&rate, &fmt[4], 4);
			std::memcpy(&bits, &fmt[14], 2);
			if (format == 0xFFFE && size >= 26)
				std::memcpy(&format, &fmt[24], 2);
		} else if (!std::memcmp(id, "data", 4)) {
			break;
		} else {
			std::fseek(file, size + (size & 1), SEEK_CUR);
		}
	}
	if (!((format == 1 && bits == 16) || (format == 3 && bits == 32)) || !channels || !rate) {
		std::fprintf(stderr, "only 16-bit or 32-bit float WAV\n");
		return 1;
	}
	sarxina::keeptalking::WordCounter counter(rate);
	const size_t perLine = static_cast<size_t>(minutes * 60 * rate);
	const size_t chunk = 1024, width = bits / 8 * channels;
	std::vector<unsigned char> bytes(chunk * width);
	std::vector<float> mono(chunk);
	size_t heard = 0, lineStart = 0;
	uint64_t lineSyllables = 0;
	for (;;) {
		const size_t got = std::fread(bytes.data(), width, chunk, file);
		if (!got)
			break;
		for (size_t i = 0; i < got; i++) {
			if (format == 1) {
				int16_t v;
				std::memcpy(&v, &bytes[i * width], 2);
				mono[i] = v / 32768.f;
			} else {
				std::memcpy(&mono[i], &bytes[i * width], 4);
			}
		}
		counter.add(mono.data(), got);
		heard += got;
		if (heard - lineStart >= perLine) {
			std::printf("%6.0f min  %7.0f words\n", lineStart / (60.0 * rate),
				    (counter.syllables() - lineSyllables) / sarxina::keeptalking::WordCounter::kSyllablesPerWord);
			lineStart = heard;
			lineSyllables = counter.syllables();
		}
	}
	std::printf("total: %llu syllables, %.0f words in %.1f minutes\n",
		    static_cast<unsigned long long>(counter.syllables()), counter.words(), counter.seconds() / 60);
	return 0;
}
