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

#ifndef REPL_SERVER_H_INCLUDED
#define REPL_SERVER_H_INCLUDED

namespace hise {
using namespace juce;

class BackendProcessor;

/** A named pipe server that provides a REPL (Read-Eval-Print Loop) interface
    for HISE. Clients connect to the pipe, send JSON commands, and receive
    JSON responses.

    The pipe name defaults to "hise-repl". If that name is already in use
    (another HISE instance), the server falls back to "hise-repl-{pid}".

    Wire protocol: line-delimited JSON (each message is a JSON object
    terminated by a newline character).

    Request:  {"cmd": "status"}\n
    Response: {"success": true, "result": {...}}\n

    Clients can connect and disconnect freely without affecting HISE.
    The server accepts one client at a time and waits for the next
    client after a disconnect.
*/
class ReplServer : public Thread
{
public:

	ReplServer(BackendProcessor& bp);
	~ReplServer();

	/** Starts the named pipe server. If already running, this is a no-op. */
	void start(bool stopOnClose);

	/** Stops the server and closes the pipe. Safe to call when already stopped. */
	void stop();

	/** Returns true if the server thread is running. */
	bool isActive() const;

	/** Returns the pipe name that clients should connect to. */
	String getPipeName() const;

	/** Launch the hise-cli Node.js client in a terminal window.
	    Uses the HisePath setting to locate the launcher script.
	    On Windows, opens hise-cli.bat via ShellExecute.
	    On macOS, opens hise-cli.command in Terminal.app.
	    On Linux, launches hise-cli.sh via x-terminal-emulator. */
	bool launchCliClient();

private:

	void run() override;

	/** Read one newline-terminated line from the pipe.
	    Returns an empty string on disconnect or shutdown. */
	String readLine();

	/** Write a newline-terminated string to the pipe. Returns false on error. */
	bool writeLine(const String& json);

	/** Parse a JSON command and dispatch it. Returns a JSON response string. */
	String processCommand(const String& jsonCommand);

	// JSON response helpers
	/** Build {"progress": true, "message": "..."} (indeterminate spinner). */
	static String makeProgress(const String& message);

	/** Build {"progress": 0.5, "message": "..."} (determinate progress bar). */
	static String makeProgress(double normalised, const String& message);

	/** Build {"success": true, "result": ...} */
	static String makeSuccessResponse(const var& result);

	/** Build {"success": true, "message": "..."} */
	static String makeSuccessMessage(const String& message);

	/** Build {"success": false, "error": "..."} */
	static String makeErrorResponse(const String& error);

	BackendProcessor& processor;
	NamedPipe pipe;
	String pipeName;
	bool stopOnClose = false;

	/** Residual bytes from previous reads that didn't end with a newline. */
	String lineBuffer;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReplServer)
};

} // namespace hise

#endif // REPL_SERVER_H_INCLUDED
