// The part of VoiceGoodifier's self-test that needs OBS (VOICEGOODIFIER_SELFTEST=1): every filter is in OBS's list
// under its name (the replaced plugins' ids hidden from the + menu), and each, made by OBS the way a scene collection
// makes it, turns a tone the way its settings say. The audio goes through the filter only, never to a speaker.
#pragma once

namespace sarxina::voice::test {

bool inObsTest();

} // namespace sarxina::voice::test
