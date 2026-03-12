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

namespace hise {
using namespace juce;

// Use -1 (infinite) for the read timeout. JUCE's NamedPipe::read() returns -1
// for BOTH timeouts and real disconnects, so a short timeout gets misinterpreted
// as a disconnect. With infinite timeout, read() blocks until data arrives or
// pipe.close() signals the cancel event (which unblocks waitForIO).
static const int kPipeReadTimeoutMs = -1;
static const int kPipeWriteTimeoutMs = 2000;
static const int kPipeReadBufferSize = 4096;

// ---------------------------------------------------------------------------

ReplServer::ReplServer(BackendProcessor& bp)
	: Thread("HISE REPL Server"),
	  processor(bp)
{
}

ReplServer::~ReplServer()
{
	stop();
}

void ReplServer::start(bool shouldStopOnClose)
{
	if (isThreadRunning())
		return;

	stopOnClose = shouldStopOnClose;
	// Try the default pipe name first; fall back to PID-suffixed name
	// if another HISE instance already owns it.
	pipeName = "hise-repl";

	if (!pipe.createNewPipe(pipeName, true))
	{
		jassertfalse;
		//pipeName = "hise-repl-" + juce::Process String(SystemStats::getProcessID());
		pipe.createNewPipe(pipeName, false);
	}

	if (!pipe.isOpen())
	{
		DBG("ReplServer: failed to create pipe '" + pipeName + "'");
		return;
	}

	DBG("ReplServer: listening on pipe '" + pipeName + "'");
	startThread(5);
}

void ReplServer::stop()
{
	if (!isThreadRunning())
		return;

	signalThreadShouldExit();
	pipe.close(); // Unblocks any pending read
	stopThread(2000);
	lineBuffer.clear();
}

bool ReplServer::isActive() const
{
	return isThreadRunning();
}

String ReplServer::getPipeName() const
{
	return pipeName;
}

// ---------------------------------------------------------------------------
// CLI client launcher

bool ReplServer::launchCliClient()
{
	auto hisePath = File(GET_HISE_SETTING(processor.getMainSynthChain(),
	                     HiseSettings::Compiler::HisePath).toString());

	auto cliDir = hisePath.getChildFile("tools/hise-cli");

#if JUCE_WINDOWS
	auto launcher = cliDir.getChildFile("hise-cli.bat");
#elif JUCE_MAC
	auto launcher = cliDir.getChildFile("hise-cli.command");
#elif JUCE_LINUX
	auto launcher = cliDir.getChildFile("hise-cli.sh");
#endif

	if (!launcher.existsAsFile())
	{
		DBG("ReplServer: CLI launcher not found: " + launcher.getFullPathName());
		return false;
	}

#if JUCE_LINUX
	// Linux: x-terminal-emulator doesn't have a file association,
	// so we launch it explicitly as a child process.
	ChildProcess terminal;
	return terminal.start("x-terminal-emulator -e " + launcher.getFullPathName().quoted());
#else
	// Windows: ShellExecute opens .bat in a new cmd.exe window.
	// macOS: NSWorkspace opens .command in Terminal.app.
	return launcher.startAsProcess("");
#endif
}

// ---------------------------------------------------------------------------
// Thread loop

void ReplServer::run()
{
	while (!threadShouldExit())
	{
		// readLine() blocks until a complete line arrives or the
		// client disconnects / server shuts down.
		auto line = readLine();

		if (threadShouldExit())
			break;

		if (line.isEmpty())
		{
			// Client disconnected. Clear the line buffer and
			// re-create the pipe to accept the next client.
			lineBuffer.clear();

			if (threadShouldExit())
				break;

			if (!pipe.createNewPipe(pipeName, false))
			{
				DBG("ReplServer: failed to re-create pipe after disconnect");
				break;
			}
		}

		auto response = processCommand(line);
		
		if (!writeLine(response))
		{
			// Write failed (client likely disconnected mid-response).
			// Will be caught as disconnect on next readLine().
		}
	}
}

// ---------------------------------------------------------------------------
// Pipe I/O with line buffering

String ReplServer::readLine()
{
	char buffer[kPipeReadBufferSize];

	while (!threadShouldExit())
	{
		// Check if we already have a complete line in the buffer.
		int newlinePos = lineBuffer.indexOfChar('\n');

		if (newlinePos >= 0)
		{
			auto line = lineBuffer.substring(0, newlinePos).trim();
			lineBuffer = lineBuffer.substring(newlinePos + 1);
			return line;
		}

		// Read more data from the pipe with a timeout so we can
		// periodically check threadShouldExit().
		int bytesRead = pipe.read(buffer, kPipeReadBufferSize, kPipeReadTimeoutMs);

		if (bytesRead > 0)
		{
			lineBuffer += String::fromUTF8(buffer, bytesRead);
		}
		else if (bytesRead < 0)
		{
			// Error or disconnect
			return {};
		}
		// bytesRead == 0: timeout, loop back and check exit flag
	}

	return {};
}

bool ReplServer::writeLine(const String& json)
{
	auto data = json + "\n";
	auto utf8 = data.toUTF8();
	int len = (int)utf8.sizeInBytes() - 1; // exclude null terminator

	int written = pipe.write(utf8.getAddress(), len, kPipeWriteTimeoutMs);
	return written == len;
}

// ---------------------------------------------------------------------------
// JSON response helpers

String ReplServer::makeProgress(const String& message)
{
	auto obj = DynamicObject::Ptr(new DynamicObject());
	obj->setProperty("progress", true);
	obj->setProperty("message", message);
	return JSON::toString(var(obj.get()), true);
}

String ReplServer::makeProgress(double normalised, const String& message)
{
	auto obj = DynamicObject::Ptr(new DynamicObject());
	obj->setProperty("progress", jlimit(0.0, 1.0, normalised));
	obj->setProperty("message", message);
	return JSON::toString(var(obj.get()), true);
}

String ReplServer::makeSuccessResponse(const var& result)
{
	auto obj = DynamicObject::Ptr(new DynamicObject());
	obj->setProperty("success", true);
	obj->setProperty("result", result);
	return JSON::toString(var(obj.get()), true);
}

String ReplServer::makeSuccessMessage(const String& message)
{
	auto obj = DynamicObject::Ptr(new DynamicObject());
	obj->setProperty("success", true);
	obj->setProperty("message", message);
	return JSON::toString(var(obj.get()), true);
}

String ReplServer::makeErrorResponse(const String& error)
{
	auto obj = DynamicObject::Ptr(new DynamicObject());
	obj->setProperty("success", false);
	obj->setProperty("error", error);
	return JSON::toString(var(obj.get()), true);
}

// ---------------------------------------------------------------------------
// Command dispatch

String ReplServer::processCommand(const String& jsonCommand)
{
	auto parsed = JSON::parse(jsonCommand);

	if (!parsed.isObject())
		return makeErrorResponse("Invalid JSON");

	auto cmd = parsed.getProperty("cmd", "").toString();

	if (cmd == "status")
	{
		auto result = DynamicObject::Ptr(new DynamicObject());
		result->setProperty("pipe", pipeName);

		auto& handler = *processor.getActiveFileHandler();
		result->setProperty("project", handler.getRootFolder().getFileName());
		result->setProperty("projectPath", handler.getRootFolder().getFullPathName());

		return makeSuccessResponse(var(result.get()));
	}

	if (cmd == "spin")
	{
		// Dummy command that demonstrates progress reporting.
		// Sends determinate progress (0..1) over 3 seconds.
		static const int kSpinDurationMs = 3000;
		static const int kSpinIntervalMs = 50;

		auto startTime = Time::getMillisecondCounter();

		for (;;)
		{
			if (threadShouldExit())
				return makeErrorResponse("Interrupted");

			auto elapsed = (int)(Time::getMillisecondCounter() - startTime);
			double progress = jlimit(0.0, 1.0, (double)elapsed / kSpinDurationMs);

			

			writeLine(makeProgress(progress, "Working..."));

			if (elapsed >= kSpinDurationMs)
				break;

			Thread::sleep(kSpinIntervalMs);

			writeLine(makeProgress(progress, "Funky"));

			Thread::sleep(kSpinIntervalMs);
			Thread::sleep(kSpinIntervalMs);
			Thread::sleep(kSpinIntervalMs);
			Thread::sleep(kSpinIntervalMs);
			Thread::sleep(kSpinIntervalMs);
		}

		return makeSuccessMessage("Done");
	}

	if (cmd == "project.info")
	{
		auto result = DynamicObject::Ptr(new DynamicObject());
		auto& handler = *processor.getActiveFileHandler();
		result->setProperty("name", handler.getRootFolder().getFileName());
		result->setProperty("path", handler.getRootFolder().getFullPathName());

		return makeSuccessResponse(var(result.get()));
	}

	if (cmd == "quit")
	{
		MessageManager::callAsync([this]()
		{
			stop();
			processor.getCommandManager()->commandStatusChanged();
		});

		return makeSuccessMessage("bye");
	}

	if (cmd == "shutdown")
	{
		auto responseStr = makeSuccessMessage("Shutting down HISE");

		// Schedule the quit on the message thread after we've sent the response.
		MessageManager::callAsync([]() { JUCEApplicationBase::quit(); });

		return responseStr;
	}

	return makeErrorResponse("Unknown command: " + cmd);
}

} // namespace hise
