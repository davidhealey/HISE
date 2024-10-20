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

namespace hise { using namespace juce;

#if USE_BACKEND


Console::ConsoleEditorComponent::ConsoleEditorComponent(CodeDocument &doc, CodeTokeniser* tok) :
CodeEditorComponent(doc, tok)
{
	setReadOnly(true);
	setColour(CodeEditorComponent::ColourIds::backgroundColourId, Colour(0xFF282828));
	getDocument().getUndoManager().setMaxNumberOfStoredUnits(0, 0);

    
	setColour(CodeEditorComponent::ColourIds::defaultTextColourId, Colours::white.withBrightness(0.7f));
	setLineNumbersShown(false);
}


Console::Console(MainController* mc_):
	mc(mc_)
{
	setName("Console");

	auto consoleDoc = mc->getConsoleHandler().getConsoleData();

	consoleDoc->addListener(this);

	setTokeniser(new ConsoleTokeniser());

	mc->getFontSizeChangeBroadcaster().addListener(*this, updateFontSize);
}

Console::~Console()
{
	mc->getConsoleHandler().getConsoleData()->removeListener(this);

	newTextConsole = nullptr;
	tokeniser = nullptr;
};


void Console::resized()
{
	newTextConsole->setBounds(getLocalBounds());
}



void Console::clear()
{
	mc->getConsoleHandler().clearConsole();

	
}

void Console::mouseDown(const MouseEvent &e)
{

    if(e.mods.isRightButtonDown())
    {
        PopupLookAndFeel plaf;
        PopupMenu m;
        
        m.setLookAndFeel(&plaf);
        
        m.addItem(1, "Clear Console");
        m.addItem(2, "Scroll down");
		
		const String id = newTextConsole->getDocument().getLine(newTextConsole->getCaretPos().getLineNumber()).upToFirstOccurrenceOf(":", false, false);

		if (id.isNotEmpty() && findParentComponentOfClass<ComponentWithBackendConnection>() != nullptr)
		{
			JavascriptProcessor *jsp = dynamic_cast<JavascriptProcessor*>(ProcessorHelpers::getFirstProcessorWithName(GET_BACKEND_ROOT_WINDOW(this)->getMainPanel()->getMainSynthChain(), id));

			const int SNIPPET_OFFSET = 1000;
			const int FILE_OFFSET = 2000;

			if (jsp != nullptr)
			{
				const String selectedText = newTextConsole->getTextInRange(newTextConsole->getHighlightedRegion());

				int snippet = -1;

				for (int i = 0; i < jsp->getNumSnippets(); i++)
				{
					if (jsp->getSnippet(i)->getCallbackName().toString() == selectedText)
					{
						snippet = i;
						break;
					}
				}

				if (snippet != -1)
				{
					m.addItem(SNIPPET_OFFSET + snippet, "Go to callback " + selectedText);
				}

				int fileIndex = -1;

				for (int i = 0; i < jsp->getNumWatchedFiles(); i++)
				{
					if (jsp->getWatchedFile(i).getFileName() == selectedText)
					{
						fileIndex = i;
						break;
					}
				}

				if (fileIndex != -1)
				{
					m.addItem(FILE_OFFSET + fileIndex, "Go to file " + selectedText);
				}
			}

			const int result = m.show();

			if (result == 1)
			{
				newTextConsole->getDocument().replaceAllContent("");
				newTextConsole->scrollToLine(0);

			}
			else if (result == 2)
			{
				newTextConsole->moveCaretToEnd(false);
			}
			else if (result >= FILE_OFFSET)
			{
				jsp->showPopupForFile(result - FILE_OFFSET);
			}
			else if (result >= SNIPPET_OFFSET)
			{
				Processor *js = dynamic_cast<Processor*>(jsp);

				const int editorStateOffset = dynamic_cast<ProcessorWithScriptingContent*>(js)->getCallbackEditorStateOffset() + 1;

				const int editorStateIndex = (result - SNIPPET_OFFSET);

				for (int i = 0; i < jsp->getNumSnippets(); i++)
				{
					js->setEditorState(editorStateOffset + i, editorStateIndex == i, dontSendNotification);
				}

				GET_BACKEND_ROOT_WINDOW(this)->getMainPanel()->setRootProcessorWithUndo(js);
			}
		}
    }
    else if (e.mods.isAltDown())
    {
#if USE_BACKEND
        
		CodeDocument::Position pos = newTextConsole->getCaretPos();

		String name = newTextConsole->getDocument().getLine(pos.getLineNumber()).upToFirstOccurrenceOf(":", false, false);

        if(name.isNotEmpty())
        {
			auto editor = GET_BACKEND_ROOT_WINDOW(this)->getMainPanel();

            Processor *p = ProcessorHelpers::getFirstProcessorWithName(editor->getMainSynthChain(), name);
            
            if(p != nullptr)
            {
                editor->setRootProcessorWithUndo(p);
            }
        }
#endif
        
    }
}


void Console::mouseMove(const MouseEvent &e)
{
	if (e.mods.isAltDown())
	{
		setMouseCursor(MouseCursor::PointingHandCursor);
	}
}

void Console::mouseDoubleClick(const MouseEvent& /*e*/)
{
	CodeDocument::Position selectionStart = newTextConsole->getSelectionStart();

	const String line = newTextConsole->getDocument().getLine(selectionStart.getLineNumber());

	DebugableObject::Helpers::gotoLocation(mc->getMainSynthChain(), line);

	
};;

void Console::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel)
{
	if (e.mods.isCommandDown())
	{
		auto f = newTextConsole->getFont();
		auto oldHeight = f.getHeight();
		oldHeight += wheel.deltaY;
		oldHeight = jlimit(12.0f, 30.0f, oldHeight);
		newTextConsole->setFont(f.withHeight(oldHeight));
	}
}

Console::ConsoleTokeniser::ConsoleTokeniser()
{
	s.set("id", Colours::white);
	s.set("default", Colours::white.withBrightness(0.75f));
	s.set("error", JUCE_LIVE_CONSTANT_OFF(Colour(0xffff3939)));
	s.set("url", Colour(0xFF555555));
	s.set("callstack", JUCE_LIVE_CONSTANT_OFF(Colour(0xffAA3939)));
}

int Console::ConsoleTokeniser::readNextToken(CodeDocument::Iterator& source)
{
	while (state == 0 && source.nextChar() != ':')
	{
		return state;
	}

	auto c = source.nextChar();

	switch (c)
	{
	case '!':
	{
		state = 2;
		break;
	}
	case '{':
	{
		state = 3;
		break;
	}
	case '\t':
	{
		state = 4;
		break;
	}
	case '\n':
	{
		state = 0;
		break;
	}
	}
	
	return state;
}


#endif

} // namespace hise