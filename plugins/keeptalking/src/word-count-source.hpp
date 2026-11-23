// The word count as an OBS source, offered under Sarxina Source: a speaking-head emoji and "1,234 words" in plain white
// Segoe UI with a soft shadow, quiet enough to sit in any streamer's layout. It only shows KeepTalking's count. The plugin hands the count over from the UI thread; the sources paint and draw it on
// OBS's graphics thread.
#pragma once

namespace sarxina::keeptalking {

inline constexpr const char *kWordCountSourceId = "keeptalking_word_count";

void registerWordCountSource();

// What every word count source shows from now on.
void showWordCount(long long words);

} // namespace sarxina::keeptalking
