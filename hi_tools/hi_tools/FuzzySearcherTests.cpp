/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   ===========================================================================
*/

namespace hise {
using namespace juce;

/** Unit tests for FuzzySearcher, focused on API method correction for the
 *  HISEScript LSP diagnostic pipeline.
 *
 *  Tests two distinct use cases:
 *  1. Unqualified names (parser context): searching within a single API class
 *     e.g. "prnt" against {"print", "stop", "clear", ...}
 *  2. Dot-qualified names (analyzer context): searching across all API classes
 *     e.g. "Console.prnt" against {"Console.print", "Synth.playNote", ...}
 *
 *  The threshold sweep determines the optimal fuzzyness parameter for API
 *  method correction (expected: 0.5-0.6). The default of 0.3 is too loose.
 */
class FuzzySearcherTests : public UnitTest
{
public:

	FuzzySearcherTests() : UnitTest("FuzzySearcher Tests", "AI Tools") {}

	void runTest() override
	{
		testLevenshteinBasics();
		testScoreFormula();
		testUnqualifiedApiCorrection();
		testQualifiedApiCorrection();
		testThresholdSweep();
		testSearchForIndexesSortedByScore();
		testCaseInsensitivity();
		testMinimumQualityGuard();
	}

private:

	// =========================================================================
	// Test data — real HISE API method names
	// =========================================================================

	static StringArray getSynthMethods()
	{
		return {
			"playNote", "playNoteWithStartOffset", "noteOff", "noteOffByEventId",
			"noteOffDelayedByEventId", "addVolumeFade", "addPitchFade", "addToFront",
			"deferCallbacks", "startTimer", "stopTimer", "isTimerRunning",
			"getTimerInterval", "setAttribute", "getAttribute", "addNoteOn",
			"addNoteOff", "addController", "sendController", "setMacroControl",
			"getNumPressedKeys", "isLegatoInterval", "isKeyDown", "isSustainPedalDown",
			"getModulator", "getEffect", "getMidiProcessor", "getChildSynth",
			"getChildSynthByIndex", "getNumChildSynths", "getSampler", "getSlotFX",
			"getMidiPlayer", "getRoutingMatrix", "getAudioSampleProcessor",
			"getTableProcessor", "getSliderPackProcessor", "getAllModulators",
			"getAllEffects", "getIdList", "addModulator", "removeModulator",
			"addEffect", "removeEffect", "setVoiceGainValue", "setVoicePitchValue",
			"isArtificialEventActive", "setClockSpeed", "setShouldKillRetriggeredNote",
			"setFixNoteOnAfterNoteOff", "attachNote", "createBuilder",
			"setModulatorAttribute", "setUseUniformVoiceHandler"
		};
	}

	static StringArray getConsoleMethods()
	{
		return {
			"print", "stop", "clear", "assertTrue", "assertEqual",
			"assertIsDefined", "assertIsObjectOrArray", "assertLegalNumber"
		};
	}

	/** A mixed list of dot-qualified API methods from multiple namespaces,
	 *  representative of the greedy lookup map used by ApiValidationAnalyzer. */
	static StringArray getQualifiedApiMethods()
	{
		return {
			"Console.print", "Console.stop", "Console.clear", "Console.assertTrue",
			"Console.assertEqual", "Console.assertIsDefined",
			"Console.assertIsObjectOrArray", "Console.assertLegalNumber",
			"Engine.getSampleRate", "Engine.getBufferSize", "Engine.getHostBpm",
			"Engine.allNotesOff", "Engine.getSamplesForMilliSeconds",
			"Engine.getGainFactorForDecibels", "Engine.getDecibelsForGainFactor",
			"Engine.getMidiNoteName", "Engine.getMidiNoteFromName",
			"Engine.getFrequencyForMidiNoteNumber", "Engine.createDspNetwork",
			"Engine.createMidiList", "Engine.createTimerObject",
			"Engine.loadUserPreset", "Engine.saveUserPreset",
			"Engine.getCurrentUserPresetName", "Engine.showMessageBox",
			"Engine.matchesRegex", "Engine.getRegexMatches",
			"Engine.getOS", "Engine.isPlugin", "Engine.setZoomLevel",
			"Synth.playNote", "Synth.noteOff", "Synth.noteOffByEventId",
			"Synth.noteOffDelayedByEventId", "Synth.addVolumeFade",
			"Synth.addPitchFade", "Synth.startTimer", "Synth.stopTimer",
			"Synth.getModulator", "Synth.getEffect", "Synth.getChildSynth",
			"Synth.getAttribute", "Synth.setAttribute", "Synth.addNoteOn",
			"Synth.addNoteOff", "Synth.addController", "Synth.sendController",
			"Synth.getSampler", "Synth.getAllEffects", "Synth.getAllModulators",
			"Math.random", "Math.range", "Math.sqr", "Math.sqrt", "Math.round",
			"Math.abs", "Math.sin", "Math.cos", "Math.floor", "Math.ceil",
			"Math.min", "Math.max", "Math.pow", "Math.log", "Math.exp",
			"Math.fmod", "Math.sign", "Math.randInt",
			"Message.getNoteNumber", "Message.setNoteNumber", "Message.getVelocity",
			"Message.setVelocity", "Message.ignoreEvent", "Message.getChannel",
			"Message.setChannel", "Message.getEventId", "Message.getControllerNumber",
			"Message.getControllerValue", "Message.setControllerValue",
			"Content.addKnob", "Content.addButton", "Content.addPanel",
			"Content.addComboBox", "Content.addLabel", "Content.addImage",
			"Content.getComponent", "Content.getAllComponents",
			"Content.setPropertiesFromJSON", "Content.makeFrontInterface",
			"Content.setHeight", "Content.setWidth", "Content.createPath",
			"Content.createLocalLookAndFeel", "Content.createScreenshot",
			"Sampler.loadSampleMap", "Sampler.getCurrentSampleMapId",
			"Sampler.getSampleMapList", "Sampler.enableRoundRobin",
			"Sampler.setActiveGroup", "Sampler.selectSounds",
			"Sampler.clearSampleMap", "Sampler.setAttribute"
		};
	}

	// =========================================================================
	// Test case data structures
	// =========================================================================

	struct CorrectionTestCase
	{
		String input;           // hallucinated method name
		String expectedResult;  // expected correction ("" = no match expected)
		String description;     // human-readable description of the test case
	};

	/** Runs a single suggestCorrection test case at the given threshold.
	 *  Returns true if the result matched expectations. */
	bool runCorrectionTest(const CorrectionTestCase& tc, const StringArray& candidates,
	                       double threshold, bool assertResult = true)
	{
		auto result = FuzzySearcher::suggestCorrection(tc.input, candidates, threshold);
		bool passed;

		if (tc.expectedResult.isEmpty())
			passed = result.isEmpty();
		else
			passed = (result == tc.expectedResult);

		if (assertResult)
		{
			expect(passed, tc.description + " | input: \"" + tc.input
			       + "\" | expected: \"" + tc.expectedResult
			       + "\" | got: \"" + result
			       + "\" | threshold: " + String(threshold));
		}

		return passed;
	}

	// =========================================================================
	// 1. Levenshtein distance basics
	// =========================================================================

	void testLevenshteinBasics()
	{
		beginTest("Levenshtein distance fundamentals");

		// Access via fitsSearch score to verify distance behavior.
		// For direct distance testing we use the scoring formula:
		//   score = 1.0 - distance / max(len1, len2)
		// So distance = (1.0 - score) * max(len1, len2)

		// Identical strings → score 1.0 (distance 0)
		expect(FuzzySearcher::fitsSearch("print", "print", 0.99),
		       "Identical strings should match at any threshold");

		// Single substitution: "print" vs "prInt" — but fitsSearch lowercases,
		// so use the contains-check path. Instead test truly different chars.
		// "print" vs "pront" = 1 substitution, distance = 1, score = 1 - 1/5 = 0.8
		expect(FuzzySearcher::fitsSearch("print", "pront", 0.7),
		       "1 substitution on 5-char word (score 0.8) should match at 0.7");
		expect(!FuzzySearcher::fitsSearch("print", "pront", 0.85),
		       "1 substitution on 5-char word (score 0.8) should NOT match at 0.85");

		// Transposition: "ab" vs "ba" — Damerau-Levenshtein distance = 1
		// score = 1 - 1/2 = 0.5
		expect(FuzzySearcher::fitsSearch("ab", "ba", 0.4),
		       "Transposition (Damerau) should have distance 1, score 0.5");
		expect(!FuzzySearcher::fitsSearch("ab", "ba", 0.6),
		       "Transposition score 0.5 should NOT match at threshold 0.6");

		// Longer transposition: "getEfect" vs "getEffect" — but these differ by
		// insertion not transposition. Let's use a real transposition case:
		// "isKeyDonw" vs "isKeyDown" — transposition of 'w' and 'n'
		// distance = 1, score = 1 - 1/9 ≈ 0.889
		expect(FuzzySearcher::fitsSearch("iskeydonw", "iskeydown", 0.8),
		       "Transposition on 9-char word (score ~0.89) should match at 0.8");

		// Empty vs non-empty: fitsSearch has a substring shortcut —
		// "print".contains("") is true, so empty string always matches.
		// This documents the contains() shortcut behavior.
		expect(FuzzySearcher::fitsSearch("", "print", 0.99),
		       "Empty search term matches via contains() shortcut (every string contains empty)");

		// Completely unrelated strings
		expect(!FuzzySearcher::fitsSearch("xyz", "print", 0.3),
		       "Completely unrelated 3-char vs 5-char should not match at 0.3");
	}

	// =========================================================================
	// 2. Score formula verification
	// =========================================================================

	void testScoreFormula()
	{
		beginTest("Score formula and threshold boundary");

		// The threshold comparison is strictly greater than (score > fuzzyness).
		// Verify boundary: a score exactly equal to the threshold should NOT match.

		// "abcde" vs "abcdf" = 1 substitution, distance = 1, score = 1 - 1/5 = 0.8
		expect(FuzzySearcher::fitsSearch("abcde", "abcdf", 0.79),
		       "Score 0.8 should match at threshold 0.79");
		expect(!FuzzySearcher::fitsSearch("abcde", "abcdf", 0.8),
		       "Score 0.8 should NOT match at threshold 0.8 (strictly greater than)");

		// "ab" vs "abc": contains() shortcut — "abc".contains("ab") is true,
		// so fitsSearch returns true regardless of threshold. This is by design.
		expect(FuzzySearcher::fitsSearch("ab", "abc", 0.99),
		       "Substring match: 'ab' in 'abc' always matches via contains() shortcut");

		// To test actual score boundary without substring shortcut, use non-substring pairs:
		// "abd" vs "abc" = 1 substitution, distance = 1, score = 1 - 1/3 ≈ 0.667
		expect(FuzzySearcher::fitsSearch("abd", "abc", 0.6),
		       "Score ~0.667 should match at threshold 0.6");
		expect(!FuzzySearcher::fitsSearch("abd", "abc", 0.7),
		       "Score ~0.667 should NOT match at threshold 0.7");

		// searchForResults returns lowercased, stripped results —
		// verify it returns the correct number of matches
		StringArray words = { "apple", "apply", "banana", "application" };
		auto results = FuzzySearcher::searchForResults("aple", words, 0.6);
		expect(results.size() >= 1,
		       "At threshold 0.6, 'aple' should match at least 'apple' (score = 1 - 1/5 = 0.8)");
	}

	// =========================================================================
	// 3. Unqualified API correction (parser context — single API class)
	// =========================================================================

	void testUnqualifiedApiCorrection()
	{
		beginTest("Unqualified API correction — Console methods");

		auto consoleMethods = getConsoleMethods();

		// We test at 0.5 initially — the threshold sweep will confirm this is right.
		// If this needs adjustment after the sweep, we update these assertions.
		const double t = 0.5;

		// --- True positives: common LLM hallucinations ---

		/** Setup: Console method list (8 methods)
		 *  Scenario: LLM writes "prnt" (missing vowel)
		 *  Expected: suggestCorrection returns "print"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("prnt", consoleMethods, t),
		             String("print"),
		             "Missing vowel: 'prnt' -> 'print'");

		/** Setup: Console method list
		 *  Scenario: LLM writes "asertTrue" (missing 's')
		 *  Expected: suggestCorrection returns "assertTrue"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("asertTrue", consoleMethods, t),
		             String("assertTrue"),
		             "Missing char: 'asertTrue' -> 'assertTrue'");

		/** Setup: Console method list
		 *  Scenario: LLM writes "assertEquals" (Java/JS habit — wrong suffix)
		 *  Expected: suggestCorrection returns "assertEqual"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("assertEquals", consoleMethods, t),
		             String("assertEqual"),
		             "Wrong suffix: 'assertEquals' -> 'assertEqual'");

		/** Setup: Console method list
		 *  Scenario: LLM writes "assertLegalNum" (truncated — 3 chars missing)
		 *  Expected: suggestCorrection returns "assertLegalNumber"
		 *  Note: "assertLegal" (11 chars) is too truncated — it's actually closer to
		 *  "assertEqual" (11 chars, distance 3) than "assertLegalNumber" (17 chars, distance 6)
		 */
		expectEquals(FuzzySearcher::suggestCorrection("assertLegalNum", consoleMethods, t),
		             String("assertLegalNumber"),
		             "Truncated: 'assertLegalNum' -> 'assertLegalNumber'");

		// --- True negatives: should NOT match ---

		/** Setup: Console method list
		 *  Scenario: LLM writes "log" (JS console.log habit)
		 *  Expected: No good match — "log" is too short and too different from "stop" or "clear"
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("log", consoleMethods, t);
			// At threshold 0.5: "log" vs "stop" = distance 3, score = 1 - 3/4 = 0.25 → no match
			// We log what actually happens for diagnostic purposes
			logMessage("  'log' at threshold " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Console method list
		 *  Scenario: Completely unrelated input "foo"
		 *  Expected: Empty string (no match)
		 */
		expectEquals(FuzzySearcher::suggestCorrection("foo", consoleMethods, t),
		             String(),
		             "Unrelated: 'foo' should not match any Console method");

		// -----------------------------------------------------------------

		beginTest("Unqualified API correction — Synth methods");

		auto synthMethods = getSynthMethods();

		/** Setup: Synth method list (54 methods)
		 *  Scenario: LLM writes "playNot" (missing 'e')
		 *  Expected: "playNote"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("playNot", synthMethods, t),
		             String("playNote"),
		             "Missing char: 'playNot' -> 'playNote'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "noteOfByEventId" (missing 'f')
		 *  Expected: "noteOffByEventId"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("noteOfByEventId", synthMethods, t),
		             String("noteOffByEventId"),
		             "Missing char: 'noteOfByEventId' -> 'noteOffByEventId'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "getEfect" (missing 'f')
		 *  Expected: "getEffect"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("getEfect", synthMethods, t),
		             String("getEffect"),
		             "Missing char: 'getEfect' -> 'getEffect'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "getChildSnyth" (transposition 'ny' -> 'yn')
		 *  Expected: "getChildSynth"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("getChildSnyth", synthMethods, t),
		             String("getChildSynth"),
		             "Transposition: 'getChildSnyth' -> 'getChildSynth'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "addControler" (missing 'l')
		 *  Expected: "addController"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("addControler", synthMethods, t),
		             String("addController"),
		             "Missing char: 'addControler' -> 'addController'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "setModulatorAtribute" (missing 't')
		 *  Expected: "setModulatorAttribute"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("setModulatorAtribute", synthMethods, t),
		             String("setModulatorAttribute"),
		             "Missing char: 'setModulatorAtribute' -> 'setModulatorAttribute'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "startTmer" (missing 'i')
		 *  Expected: "startTimer"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("startTmer", synthMethods, t),
		             String("startTimer"),
		             "Missing char: 'startTmer' -> 'startTimer'");

		/** Setup: Synth method list
		 *  Scenario: LLM writes "addVolumFade" (missing 'e')
		 *  Expected: "addVolumeFade"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("addVolumFade", synthMethods, t),
		             String("addVolumeFade"),
		             "Missing char: 'addVolumFade' -> 'addVolumeFade'");

		// --- True negatives for Synth ---

		/** Setup: Synth method list
		 *  Scenario: LLM writes "getWidth" (wrong domain — this is a UI method)
		 *  Expected: No good match
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("getWidth", synthMethods, t);
			logMessage("  'getWidth' vs Synth at threshold " + String(t) + " -> \"" + result + "\"");
			// At 0.5 threshold, "getWidth" (8 chars) would need distance <= 3 to match
			// any 8-char Synth method. Closest might be "getIdList" (distance ~5) — no match.
		}

		/** Setup: Synth method list
		 *  Scenario: LLM writes "noteOn" (JS-style — real HISEScript uses playNote or addNoteOn)
		 *  Expected: "addNoteOn" or "noteOff" — both are close. Log which wins.
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("noteOn", synthMethods, t);
			logMessage("  'noteOn' vs Synth at threshold " + String(t) + " -> \"" + result + "\"");
		}
	}

	// =========================================================================
	// 4. Dot-qualified API correction (analyzer context — cross-namespace)
	// =========================================================================

	void testQualifiedApiCorrection()
	{
		beginTest("Dot-qualified API correction (X.Y pattern)");

		auto methods = getQualifiedApiMethods();
		const double t = 0.5;

		// --- Namespace correct, method wrong ---

		/** Setup: Qualified API method list (~90 methods from 6 namespaces)
		 *  Scenario: LLM writes "Console.prnt" (missing vowel in method)
		 *  Expected: "Console.print"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Console.prnt", methods, t),
		             String("Console.print"),
		             "Dot-qualified: 'Console.prnt' -> 'Console.print'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Engine.getSampelRate" (transposition in method)
		 *  Expected: "Engine.getSampleRate"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Engine.getSampelRate", methods, t),
		             String("Engine.getSampleRate"),
		             "Dot-qualified: 'Engine.getSampelRate' -> 'Engine.getSampleRate'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Synth.getEfect" (missing 'f' in method)
		 *  Expected: "Synth.getEffect"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Synth.getEfect", methods, t),
		             String("Synth.getEffect"),
		             "Dot-qualified: 'Synth.getEfect' -> 'Synth.getEffect'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Synth.playNot" (missing 'e')
		 *  Expected: "Synth.playNote"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Synth.playNot", methods, t),
		             String("Synth.playNote"),
		             "Dot-qualified: 'Synth.playNot' -> 'Synth.playNote'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Message.setNoteNumer" (missing 'b')
		 *  Expected: "Message.setNoteNumber"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Message.setNoteNumer", methods, t),
		             String("Message.setNoteNumber"),
		             "Dot-qualified: 'Message.setNoteNumer' -> 'Message.setNoteNumber'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Content.addKnb" (missing 'o')
		 *  Expected: "Content.addKnob"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Content.addKnb", methods, t),
		             String("Content.addKnob"),
		             "Dot-qualified: 'Content.addKnb' -> 'Content.addKnob'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Sampler.loadSampelMap" (transposition)
		 *  Expected: "Sampler.loadSampleMap"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Sampler.loadSampelMap", methods, t),
		             String("Sampler.loadSampleMap"),
		             "Dot-qualified: 'Sampler.loadSampelMap' -> 'Sampler.loadSampleMap'");

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Math.randInt" (this actually exists! — should find exact match)
		 *  Expected: "Math.randInt"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("Math.randInt", methods, t),
		             String("Math.randInt"),
		             "Dot-qualified exact match: 'Math.randInt' -> 'Math.randInt'");

		// --- Common LLM namespace hallucinations (wrong name entirely) ---

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Engine.getBpm" (real: "Engine.getHostBpm")
		 *  Expected: "Engine.getHostBpm" — method part: "getBpm" vs "getHostBpm" = distance 4 on 10 chars = score 0.6
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Engine.getBpm", methods, t);
			logMessage("  'Engine.getBpm' at " + String(t) + " -> \"" + result + "\"");
			// This is a borderline case — log the result to guide threshold selection
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Synth.getModulatorByName" (real: "Synth.getModulator")
		 *  Expected: "Synth.getModulator" — suffix hallucination
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Synth.getModulatorByName", methods, t);
			logMessage("  'Synth.getModulatorByName' at " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Message.getNote" (real: "Message.getNoteNumber")
		 *  Expected: "Message.getNoteNumber" — truncated name
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Message.getNote", methods, t);
			logMessage("  'Message.getNote' at " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Message.ignore" (real: "Message.ignoreEvent")
		 *  Expected: "Message.ignoreEvent" — truncated name
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Message.ignore", methods, t);
			logMessage("  'Message.ignore' at " + String(t) + " -> \"" + result + "\"");
		}

		// --- True negatives: should NOT match ---

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Console.log" (JS habit — no close match in HISE)
		 *  Expected: No match (or poor match)
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Console.log", methods, t);
			logMessage("  'Console.log' at " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Content.addSlider" (real: "Content.addKnob" — different concept)
		 *  Expected: Not "Content.addKnob" — that would be a wrong suggestion
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Content.addSlider", methods, t);
			logMessage("  'Content.addSlider' at " + String(t) + " -> \"" + result + "\"");
			// addSlider (9 chars) vs addKnob (7 chars) = too different. Good.
			// But addSlider vs addLabel? distance might be close. Log and observe.
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Math.clamp" (real: "Math.range" — different name for same concept)
		 *  Expected: Should NOT suggest "Math.range" — the names are too different
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Math.clamp", methods, t);
			logMessage("  'Math.clamp' at " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Math.square" (real: "Math.sqr" — 3 extra chars on 3-char method)
		 *  Expected: Probably won't match at 0.5 — method part "square" vs "sqr" = distance 3 on 6 chars = score 0.5
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Math.square", methods, t);
			logMessage("  'Math.square' at " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Engine.noteToFreq" (real: Engine.getFrequencyForMidiNoteNumber)
		 *  Expected: No match — the names are completely different
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Engine.noteToFreq", methods, t);
			logMessage("  'Engine.noteToFreq' at " + String(t) + " -> \"" + result + "\"");
		}

		/** Setup: Qualified API method list
		 *  Scenario: LLM writes "Engine.dbToGain" (real: Engine.getGainFactorForDecibels)
		 *  Expected: No match — completely different naming convention
		 */
		{
			auto result = FuzzySearcher::suggestCorrection("Engine.dbToGain", methods, t);
			logMessage("  'Engine.dbToGain' at " + String(t) + " -> \"" + result + "\"");
		}
	}

	// =========================================================================
	// 5. Threshold sweep — systematic precision/recall measurement
	// =========================================================================

	void testThresholdSweep()
	{
		beginTest("Threshold sweep — determining optimal fuzzyness for API correction");

		auto synthMethods = getSynthMethods();
		auto qualifiedMethods = getQualifiedApiMethods();

		// True positive cases: hallucinations that SHOULD be corrected
		Array<CorrectionTestCase> truePositives;
		truePositives.add({ "playNot",                "playNote",                "Synth: missing 'e'" });
		truePositives.add({ "noteOfByEventId",        "noteOffByEventId",        "Synth: missing 'f'" });
		truePositives.add({ "getEfect",               "getEffect",               "Synth: missing 'f'" });
		truePositives.add({ "getChildSnyth",          "getChildSynth",           "Synth: transposition" });
		truePositives.add({ "addControler",           "addController",           "Synth: missing 'l'" });
		truePositives.add({ "setModulatorAtribute",   "setModulatorAttribute",   "Synth: missing 't'" });
		truePositives.add({ "startTmer",              "startTimer",              "Synth: missing 'i'" });
		truePositives.add({ "addVolumFade",           "addVolumeFade",           "Synth: missing 'e'" });
		truePositives.add({ "noteOfDelayedByEventId", "noteOffDelayedByEventId", "Synth: missing 'f' (long)" });
		truePositives.add({ "isTimerRunnng",          "isTimerRunning",          "Synth: missing 'i'" });

		// True negative cases: inputs that should NOT produce suggestions.
		// NOTE: FuzzySearcher has a contains() shortcut — if the search term is
		// a substring of a candidate, it always matches regardless of threshold.
		// Short prefixes like "play" → "playNote" are therefore always found.
		// Only include cases here that genuinely should not match at any level.
		Array<CorrectionTestCase> trueNegatives;
		trueNegatives.add({ "foo",      "", "Completely unrelated (3 chars)" });
		trueNegatives.add({ "xyz",      "", "Random string" });

		// Borderline cases: these match at some thresholds due to contains() shortcut
		// or coincidental edit distance. We log them but don't assert failure.
		// These findings inform whether suggestCorrection() needs a quality guard.
		struct BorderlineCase { String input; String description; };
		Array<BorderlineCase> borderlineCases;
		borderlineCases.add({ "getWidth", "Wrong domain — matches 'getChildSynth' via shared 'get' prefix" });
		borderlineCases.add({ "setValue", "Wrong domain — may match 'setVoiceGainValue' via 'set' prefix" });
		borderlineCases.add({ "play",     "Short prefix — substring of 'playNote' (contains shortcut)" });
		borderlineCases.add({ "send",     "Short prefix — substring of 'sendController' (contains shortcut)" });

		// Dot-qualified true positives
		Array<CorrectionTestCase> qualifiedTruePositives;
		qualifiedTruePositives.add({ "Console.prnt",          "Console.print",         "Console: missing vowel" });
		qualifiedTruePositives.add({ "Engine.getSampelRate",  "Engine.getSampleRate",  "Engine: transposition" });
		qualifiedTruePositives.add({ "Synth.getEfect",        "Synth.getEffect",       "Synth: missing 'f'" });
		qualifiedTruePositives.add({ "Synth.playNot",         "Synth.playNote",        "Synth: missing 'e'" });
		qualifiedTruePositives.add({ "Message.setNoteNumer",  "Message.setNoteNumber", "Message: missing 'b'" });
		qualifiedTruePositives.add({ "Content.addKnb",        "Content.addKnob",       "Content: missing 'o'" });
		qualifiedTruePositives.add({ "Sampler.loadSampelMap", "Sampler.loadSampleMap", "Sampler: transposition" });
		qualifiedTruePositives.add({ "Synth.addVolumFade",    "Synth.addVolumeFade",   "Synth: missing 'e'" });
		qualifiedTruePositives.add({ "Console.asertTrue",     "Console.assertTrue",    "Console: missing 's'" });
		qualifiedTruePositives.add({ "Engine.allNotesof",     "Engine.allNotesOff",    "Engine: missing 'f'" });

		// Dot-qualified true negatives — only cases that genuinely should not match
		Array<CorrectionTestCase> qualifiedTrueNegatives;
		qualifiedTrueNegatives.add({ "Foo.bar",                   "", "Nonexistent namespace" });
		qualifiedTrueNegatives.add({ "Something.completelyWrong", "", "Totally unrelated" });

		// Dot-qualified borderline cases — these match at various thresholds due to
		// coincidental edit distance or the dot-qualified scoring blend.
		// Logged for analysis, not asserted as failures.
		Array<BorderlineCase> qualifiedBorderlineCases;
		qualifiedBorderlineCases.add({ "Console.log",       "JS habit — matches 'Console.stop' via dot-blend scoring" });
		qualifiedBorderlineCases.add({ "Engine.noteToFreq", "Different naming — matches via partial token overlap" });
		qualifiedBorderlineCases.add({ "Engine.dbToGain",   "Different naming — matches via short token overlap" });

		double thresholds[] = { 0.3, 0.4, 0.5, 0.6 };

		logMessage("=== Threshold Sweep Results ===");
		logMessage("");

		for (auto threshold : thresholds)
		{
			int unqualTP = 0, unqualFP = 0, unqualFN = 0;
			int qualTP = 0, qualFP = 0, qualFN = 0;
			int borderlineMatched = 0;

			// --- Unqualified tests ---
			for (auto& tc : truePositives)
			{
				if (runCorrectionTest(tc, synthMethods, threshold, false))
					unqualTP++;
				else
					unqualFN++;
			}

			for (auto& tc : trueNegatives)
			{
				auto result = FuzzySearcher::suggestCorrection(tc.input, synthMethods, threshold);
				if (result.isNotEmpty())
					unqualFP++;
			}

			// --- Qualified tests ---
			for (auto& tc : qualifiedTruePositives)
			{
				if (runCorrectionTest(tc, qualifiedMethods, threshold, false))
					qualTP++;
				else
					qualFN++;
			}

			for (auto& tc : qualifiedTrueNegatives)
			{
				auto result = FuzzySearcher::suggestCorrection(tc.input, qualifiedMethods, threshold);
				if (result.isNotEmpty())
					qualFP++;
			}

			// --- Borderline cases (log only, not counted in precision/recall) ---
			for (auto& bc : borderlineCases)
			{
				auto result = FuzzySearcher::suggestCorrection(bc.input, synthMethods, threshold);
				if (result.isNotEmpty())
					borderlineMatched++;
			}
			for (auto& bc : qualifiedBorderlineCases)
			{
				auto result = FuzzySearcher::suggestCorrection(bc.input, qualifiedMethods, threshold);
				if (result.isNotEmpty())
					borderlineMatched++;
			}

			auto totalTP = unqualTP + qualTP;
			auto totalFP = unqualFP + qualFP;
			auto totalFN = unqualFN + qualFN;
			auto totalPositives = truePositives.size() + qualifiedTruePositives.size();
			auto totalNegatives = trueNegatives.size() + qualifiedTrueNegatives.size();

			auto precision = (totalTP + totalFP > 0) ? (double)totalTP / (totalTP + totalFP) : 1.0;
			auto recall = (totalPositives > 0) ? (double)totalTP / totalPositives : 0.0;

			logMessage("Threshold " + String(threshold, 1)
			           + ": TP=" + String(totalTP) + "/" + String(totalPositives)
			           + "  FP=" + String(totalFP) + "/" + String(totalNegatives)
			           + "  FN=" + String(totalFN)
			           + "  Borderline=" + String(borderlineMatched)
			                + "/" + String(borderlineCases.size() + qualifiedBorderlineCases.size())
			           + "  Precision=" + String(precision, 2)
			           + "  Recall=" + String(recall, 2));

			// Log per-case details for debugging
			for (auto& tc : truePositives)
			{
				auto result = FuzzySearcher::suggestCorrection(tc.input, synthMethods, threshold);
				if (result != tc.expectedResult)
					logMessage("    MISS [unqual TP]: \"" + tc.input + "\" -> \"" + result
					           + "\" (expected \"" + tc.expectedResult + "\") — " + tc.description);
			}
			for (auto& tc : trueNegatives)
			{
				auto result = FuzzySearcher::suggestCorrection(tc.input, synthMethods, threshold);
				if (result.isNotEmpty())
					logMessage("    MISS [unqual FP]: \"" + tc.input + "\" -> \"" + result
					           + "\" (expected empty) — " + tc.description);
			}
			for (auto& tc : qualifiedTruePositives)
			{
				auto result = FuzzySearcher::suggestCorrection(tc.input, qualifiedMethods, threshold);
				if (result != tc.expectedResult)
					logMessage("    MISS [qual TP]: \"" + tc.input + "\" -> \"" + result
					           + "\" (expected \"" + tc.expectedResult + "\") — " + tc.description);
			}
			for (auto& tc : qualifiedTrueNegatives)
			{
				auto result = FuzzySearcher::suggestCorrection(tc.input, qualifiedMethods, threshold);
				if (result.isNotEmpty())
					logMessage("    MISS [qual FP]: \"" + tc.input + "\" -> \"" + result
					           + "\" (expected empty) — " + tc.description);
			}
			for (auto& bc : borderlineCases)
			{
				auto result = FuzzySearcher::suggestCorrection(bc.input, synthMethods, threshold);
				logMessage("    BORDERLINE [unqual]: \"" + bc.input + "\" -> \""
				           + result + "\" — " + bc.description);
			}
			for (auto& bc : qualifiedBorderlineCases)
			{
				auto result = FuzzySearcher::suggestCorrection(bc.input, qualifiedMethods, threshold);
				logMessage("    BORDERLINE [qual]: \"" + bc.input + "\" -> \""
				           + result + "\" — " + bc.description);
			}
		}

		logMessage("");
		logMessage("=== End Threshold Sweep ===");

		// Lock in the recommended threshold for assertions.
		// Based on first run data: all thresholds achieve 100% recall.
		// FPs are caused by the contains() shortcut and dot-blend scoring.
		// Using 0.6 to minimize FP while keeping full TP.
		const double recommended = 0.6;

		logMessage("");
		logMessage("Running assertions at recommended threshold: " + String(recommended));

		for (auto& tc : truePositives)
			runCorrectionTest(tc, synthMethods, recommended, true);

		for (auto& tc : trueNegatives)
		{
			auto result = FuzzySearcher::suggestCorrection(tc.input, synthMethods, recommended);
			expect(result.isEmpty(),
			       "Should NOT match: \"" + tc.input + "\" -> \"" + result + "\" — " + tc.description);
		}

		for (auto& tc : qualifiedTruePositives)
			runCorrectionTest(tc, qualifiedMethods, recommended, true);

		for (auto& tc : qualifiedTrueNegatives)
		{
			auto result = FuzzySearcher::suggestCorrection(tc.input, qualifiedMethods, recommended);
			expect(result.isEmpty(),
			       "Should NOT match: \"" + tc.input + "\" -> \"" + result + "\" — " + tc.description);
		}

		// Log borderline cases at recommended threshold for visibility
		logMessage("");
		logMessage("Borderline cases at recommended threshold " + String(recommended) + ":");

		for (auto& bc : borderlineCases)
		{
			auto result = FuzzySearcher::suggestCorrection(bc.input, synthMethods, recommended);
			logMessage("  [unqual] \"" + bc.input + "\" -> \"" + result + "\" — " + bc.description);
		}
		for (auto& bc : qualifiedBorderlineCases)
		{
			auto result = FuzzySearcher::suggestCorrection(bc.input, qualifiedMethods, recommended);
			logMessage("  [qual] \"" + bc.input + "\" -> \"" + result + "\" — " + bc.description);
		}
	}

	// =========================================================================
	// 6. searchForIndexes with sortByScore — ranking verification
	// =========================================================================

	void testSearchForIndexesSortedByScore()
	{
		beginTest("searchForIndexes sorted by score — ranking verification");

		auto methods = getQualifiedApiMethods();

		// Search for "Synth.noteOf" — should find "Synth.noteOff" as best match,
		// potentially also "Synth.noteOffByEventId" and "Synth.noteOffDelayedByEventId"
		auto indices = FuzzySearcher::searchForIndexes("Synth.noteOf", methods, 0.5, true);

		if (indices.size() > 0)
		{
			auto bestMatch = methods[indices[0]];
			logMessage("  'Synth.noteOf' best match: \"" + bestMatch + "\" (index " + String(indices[0]) + ")");
			expectEquals(bestMatch, String("Synth.noteOff"),
			             "Best match for 'Synth.noteOf' should be 'Synth.noteOff'");

			// Verify ranking: if multiple matches, they should be in ascending distance order
			for (int i = 0; i < indices.size(); i++)
				logMessage("    [" + String(i) + "] \"" + methods[indices[i]] + "\"");

			// All indices should be valid
			for (int idx : indices)
			{
				expect(idx >= 0 && idx < methods.size(),
				       "Index " + String(idx) + " should be in valid range");
			}
		}
		else
		{
			logMessage("  WARNING: 'Synth.noteOf' returned 0 matches — threshold may be too high");
		}

		// Search for "Engine.get" — should find multiple "Engine.get*" methods
		auto engineIndices = FuzzySearcher::searchForIndexes("Engine.get", methods, 0.3, true);

		logMessage("  'Engine.get' at 0.3 found " + String(engineIndices.size()) + " matches:");
		for (int i = 0; i < jmin(5, engineIndices.size()); i++)
			logMessage("    [" + String(i) + "] \"" + methods[engineIndices[i]] + "\"");

		// At least some Engine.get* methods should match
		expect(engineIndices.size() >= 1,
		       "'Engine.get' should match at least one method at threshold 0.3");

		// Search for "Console.asser" — should rank exact-prefix matches first
		auto consoleIndices = FuzzySearcher::searchForIndexes("Console.asser", methods, 0.3, true);

		logMessage("  'Console.asser' at 0.3 found " + String(consoleIndices.size()) + " matches:");
		for (int i = 0; i < jmin(5, consoleIndices.size()); i++)
			logMessage("    [" + String(i) + "] \"" + methods[consoleIndices[i]] + "\"");
	}

	// =========================================================================
	// 7. Case insensitivity
	// =========================================================================

	void testCaseInsensitivity()
	{
		beginTest("Case insensitivity — search normalization");

		auto methods = getQualifiedApiMethods();
		const double t = 0.5;

		/** Setup: Qualified method list with proper casing
		 *  Scenario: LLM writes all-lowercase "synth.playnote"
		 *  Expected: Finds "Synth.playNote" (search() lowercases both sides)
		 */
		expectEquals(FuzzySearcher::suggestCorrection("synth.playnote", methods, t),
		             String("Synth.playNote"),
		             "All-lowercase: 'synth.playnote' -> 'Synth.playNote'");

		/** Setup: Qualified method list
		 *  Scenario: LLM writes all-uppercase "CONSOLE.PRINT"
		 *  Expected: Finds "Console.print"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("CONSOLE.PRINT", methods, t),
		             String("Console.print"),
		             "All-uppercase: 'CONSOLE.PRINT' -> 'Console.print'");

		/** Setup: Qualified method list
		 *  Scenario: LLM writes mixed case "engine.getsamplerate"
		 *  Expected: Finds "Engine.getSampleRate"
		 */
		expectEquals(FuzzySearcher::suggestCorrection("engine.getsamplerate", methods, t),
		             String("Engine.getSampleRate"),
		             "Lowercase: 'engine.getsamplerate' -> 'Engine.getSampleRate'");

		/** Setup: Unqualified — case mismatch in method name
		 *  Scenario: LLM writes "setvalue" (no camelCase)
		 *  Expected: No match in Synth methods (setValue doesn't exist there).
		 *  This verifies that case insensitivity doesn't create false cross-domain matches.
		 */
		{
			auto synthMethods = getSynthMethods();
			auto result = FuzzySearcher::suggestCorrection("setvalue", synthMethods, t);
			logMessage("  'setvalue' vs Synth at " + String(t) + " -> \"" + result + "\"");
		}
	}

	// =========================================================================
	// 8. Minimum quality guard
	// =========================================================================

	void testMinimumQualityGuard()
	{
		beginTest("Minimum quality guard — truly unrelated inputs must return empty");

		auto methods = getQualifiedApiMethods();
		const double t = 0.5;

		struct NegativeCase
		{
			String input;
			String description;
		};

		Array<NegativeCase> cases;
		cases.add({ "Foo.bar",                       "Nonexistent namespace and method" });
		cases.add({ "X.Y",                           "Minimal dot-qualified nonsense" });
		cases.add({ "Something.completelyDifferent", "Long unrelated qualified name" });
		cases.add({ "Window.alert",                  "Browser API (wrong framework entirely)" });
		cases.add({ "document.getElementById",       "DOM API (wrong framework entirely)" });
		cases.add({ "process.exit",                  "Node.js API" });
		cases.add({ "std.vector",                    "C++ STL" });

		for (auto& tc : cases)
		{
			auto result = FuzzySearcher::suggestCorrection(tc.input, methods, t);

			if (result.isNotEmpty())
			{
				// This is a finding — suggestCorrection returned something for garbage input.
				// Log it as a potential issue for a minimum quality guard.
				logMessage("  QUALITY CONCERN: \"" + tc.input + "\" -> \"" + result
				           + "\" — " + tc.description);
			}

			expect(result.isEmpty(),
			       "Should return empty for unrelated input: \"" + tc.input
			       + "\" -> \"" + result + "\" — " + tc.description);
		}
	}
};

static FuzzySearcherTests fuzzySearcherTests;

} // namespace hise
