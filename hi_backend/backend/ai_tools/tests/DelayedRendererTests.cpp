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
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#if HI_RUN_UNIT_TESTS

namespace hise {

/** Stress test for DelayedRenderer::processWrapped with MuseScore-style host behaviour.

	MuseScore's VST host splits every audio callback at each event timestamp and calls the
	plugin once per gap, so the plugin sees many short, variable-length blocks (1 sample and up,
	rarely a multiple of HISE_EVENT_RASTER) with all events at sample offset 0. This test drives
	the same call pattern into DelayedRenderer without needing MuseScore.

	If a scenario crashes, the process dies. The last chunks handed to the renderer are written to
	"delayed_renderer_last_calls.txt" in the temp directory, and the scenario parameters are logged
	before it starts, so the crash can be reproduced with the same seed.
*/
class DelayedRendererTest : public juce::UnitTest
{
public:

	DelayedRendererTest() : UnitTest("DelayedRenderer variable block size Tests", "DelayedRenderer") {}

	void runTest() override
	{
		ScopedValueSetter<bool> svs(MainController::unitTestMode, true);

		for (double sampleRate : { 44100.0, 48000.0 })
		{
			for (int hostBlock : { 512, 1024 })
			{
				runEventScenario("sparse events", sampleRate, hostBlock, EventDensity::Sparse, 1);
				runEventScenario("dense events", sampleRate, hostBlock, EventDensity::Dense, 2);
				runContinuityScenario(sampleRate, hostBlock);
			}
		}
	}

private:

	enum class EventDensity
	{
		Sparse,	// 0-4 events per host block at random positions
		Dense	// an event every 1-12 samples, so most chunks are shorter than the event raster
	};

	struct TestContext
	{
		TestContext(double sampleRate, int hostBlock)
		{
			CompileExporter::setSkipAudioDriverInitialisation();
			sp = new StandaloneProcessor();
			mc = dynamic_cast<MainController*>(sp->createProcessor());
			threadSuspender = new MainController::ScopedBadBabysitter(mc);
			bp = dynamic_cast<BackendProcessor*>(mc.get());
			bp->prepareToPlay(sampleRate, hostBlock);

			raw::Builder b(mc);
			b.create<WaveSynth>(mc->getMainSynthChain());

			// let the kill state handler settle with a couple of aligned blocks
			for (int i = 0; i < 2; i++)
			{
				AudioSampleBuffer silence(2, hostBlock);
				MidiBuffer empty;
				process(silence, 0, hostBlock, empty);
			}
		}

		~TestContext()
		{
			threadSuspender = nullptr;
			mc = nullptr;
			sp = nullptr;
		}

		/** Calls the renderer exactly like the frontend processor does for a host callback. */
		void process(AudioSampleBuffer& buffer, int offset, int numSamples, MidiBuffer& midi)
		{
			float* ptrs[2] = { buffer.getWritePointer(0, offset), buffer.getWritePointer(1, offset) };
			AudioSampleBuffer chunk(ptrs, 2, numSamples);

			logCall(numSamples, midi);
			mc->getDelayedRenderer().processWrapped(chunk, midi);
		}

		void logCall(int numSamples, const MidiBuffer& midi)
		{
			String line;
			line << "samples=" << numSamples << " events=" << midi.getNumEvents();

			recentCalls.add(line);

			if (recentCalls.size() > 32)
				recentCalls.remove(0);

			auto f = File::getSpecialLocation(File::tempDirectory).getChildFile("delayed_renderer_last_calls.txt");
			f.replaceWithText(recentCalls.joinIntoString("\n"));
		}

		ScopedPointer<StandaloneProcessor> sp;
		ScopedPointer<MainController> mc;
		ScopedPointer<MainController::ScopedBadBabysitter> threadSuspender;
		BackendProcessor* bp = nullptr;
		StringArray recentCalls;
	};

	/** Builds the list of chunk lengths for one host block and fills the events for each chunk.

		Events that fall on a zero-length chunk are carried over to the next one, which is what
		MuseScore does (it queues them and processes zero samples).
	*/
	struct ChunkPlan
	{
		Array<int> lengths;
		Array<MidiBuffer> events;
	};

	ChunkPlan createChunkPlan(Random& r, int hostBlock, EventDensity density, Array<int>& heldNotes)
	{
		Array<int> positions;

		if (density == EventDensity::Sparse)
		{
			int numEvents = r.nextInt(5);

			for (int i = 0; i < numEvents; i++)
				positions.add(r.nextInt(hostBlock));
		}
		else
		{
			int pos = r.nextInt(4);

			while (pos < hostBlock)
			{
				positions.add(pos);
				pos += 1 + r.nextInt(12);
			}
		}

		std::sort(positions.begin(), positions.end());

		ChunkPlan plan;
		MidiBuffer pending;
		int lastPos = 0;

		auto flush = [&](int length)
		{
			plan.lengths.add(length);
			plan.events.add(pending);
			pending.clear();
		};

		// the gap before the first event is rendered without events
		for (int i = 0; i < positions.size(); i++)
		{
			auto length = positions[i] - lastPos;

			if (length > 0)
			{
				flush(length);
				lastPos = positions[i];
			}

			pending.addEvent(createEvent(r, heldNotes), 0);
		}

		auto remainder = hostBlock - lastPos;

		if (remainder > 0)
			flush(remainder);

		return plan;
	}

	MidiMessage createEvent(Random& r, Array<int>& heldNotes)
	{
		auto shouldRelease = !heldNotes.isEmpty() && (r.nextBool() || heldNotes.size() >= 8);

		if (shouldRelease)
		{
			auto index = r.nextInt(heldNotes.size());
			auto note = heldNotes[index];
			heldNotes.remove(index);
			return MidiMessage::noteOff(1, note);
		}

		// repeated notes without a release and velocity 0 both happen with MuseScore
		auto note = 48 + r.nextInt(37);
		auto velocity = r.nextInt(20) == 0 ? 0 : 1 + r.nextInt(127);
		heldNotes.add(note);
		return MidiMessage::noteOn(1, note, (uint8)velocity);
	}

	bool isCleanAudio(const AudioSampleBuffer& b)
	{
		for (int c = 0; c < b.getNumChannels(); c++)
		{
			auto data = b.getReadPointer(c);

			for (int i = 0; i < b.getNumSamples(); i++)
			{
				if (!std::isfinite(data[i]) || std::abs(data[i]) > 100.0f)
					return false;
			}
		}

		return true;
	}

	/** Setup: Host block split at event timestamps, all events at offset 0, random notes.
	 *  Scenario: processWrapped is called with the resulting short, variable-length chunks.
	 *  Expected: no crash and the output stays finite and bounded.
	 */
	void runEventScenario(const String& name, double sampleRate, int hostBlock, EventDensity density, int seed)
	{
		String title;
		title << "MuseScore chunking, " << name << ", " << sampleRate << " Hz, host block " << hostBlock;
		beginTest(title);
		logMessage(title + " (seed " + String(seed) + ")");

		TestContext ctx(sampleRate, hostBlock);
		Random r(seed);
		Array<int> heldNotes;

		const int numHostBlocks = 400;
		int numCalls = 0;
		bool allClean = true;

		for (int b = 0; b < numHostBlocks; b++)
		{
			AudioSampleBuffer hostBuffer(2, hostBlock);
			hostBuffer.clear();

			auto plan = createChunkPlan(r, hostBlock, density, heldNotes);
			int pos = 0;

			for (int i = 0; i < plan.lengths.size(); i++)
			{
				auto events = plan.events[i];
				ctx.process(hostBuffer, pos, plan.lengths[i], events);
				pos += plan.lengths[i];
				numCalls++;
			}

			expectEquals(pos, hostBlock, "chunk plan must cover the whole host block");
			allClean &= isCleanAudio(hostBuffer);
		}

		expect(allClean, "output contains NaN, infinity or huge values");
		logMessage("  survived " + String(numCalls) + " calls");
	}

	/** Renders a held note, either in aligned host blocks or in random short chunks. */
	Array<float> renderHeldNote(double sampleRate, int hostBlock, bool chunked, int numHostBlocks)
	{
		TestContext ctx(sampleRate, hostBlock);
		Random r(1234);
		Array<float> result;

		// The note on goes into an aligned block in both runs so the onset is identical
		AudioSampleBuffer first(2, hostBlock);
		first.clear();
		MidiBuffer noteOn;
		noteOn.addEvent(MidiMessage::noteOn(1, 60, (uint8)100), 0);
		ctx.process(first, 0, hostBlock, noteOn);

		for (int b = 0; b < numHostBlocks; b++)
		{
			AudioSampleBuffer buffer(2, hostBlock);
			buffer.clear();

			if (chunked)
			{
				int pos = 0;

				while (pos < hostBlock)
				{
					auto maxLength = r.nextInt(4) == 0 ? 9 : hostBlock / 2;
					auto length = jmin(hostBlock - pos, 1 + r.nextInt(maxLength));
					MidiBuffer empty;
					ctx.process(buffer, pos, length, empty);
					pos += length;
				}
			}
			else
			{
				MidiBuffer empty;
				ctx.process(buffer, 0, hostBlock, empty);
			}

			result.addArray(buffer.getReadPointer(0), hostBlock);
		}

		return result;
	}

	/** Setup: A held note rendered once in aligned blocks and once in random short chunks.
	 *  Scenario: Both runs produce one continuous sample stream with no further MIDI events.
	 *  Expected: The streams are identical, so the leftover buffer loses or repeats no samples.
	 */
	void runContinuityScenario(double sampleRate, int hostBlock)
	{
		String title;
		title << "Chunked output equals aligned output, " << sampleRate << " Hz, host block " << hostBlock;
		beginTest(title);
		logMessage(title);

		const int numHostBlocks = 50;

		auto reference = renderHeldNote(sampleRate, hostBlock, false, numHostBlocks);
		auto chunked = renderHeldNote(sampleRate, hostBlock, true, numHostBlocks);

		expectEquals(chunked.size(), reference.size(), "different number of samples rendered");

		auto energy = 0.0f;

		for (auto s : reference)
			energy += std::abs(s);

		expect(energy > 0.01f, "reference run is silent, the test note was not played");

		for (int i = 0; i < jmin(chunked.size(), reference.size()); i++)
		{
			if (std::abs(chunked[i] - reference[i]) > 1e-4f)
			{
				expect(false, "first mismatch at sample " + String(i) + ": aligned " + String(reference[i]) +
							  ", chunked " + String(chunked[i]));
				return;
			}
		}
	}
};

static DelayedRendererTest delayedRendererTest;

} // namespace hise

#endif // HI_RUN_UNIT_TESTS
