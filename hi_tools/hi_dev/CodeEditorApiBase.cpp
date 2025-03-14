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



juce::AttributedString ValueTreeApiHelpers::createAttributedStringFromApi(const ValueTree &method, const String &, bool multiLine, Colour textColour)
{
	AttributedString help;

	const String name = method.getProperty(Identifier("name")).toString();
	const String arguments = method.getProperty(Identifier("arguments")).toString();
	const String description = method.getProperty(Identifier("description")).toString();
	const String returnType = method.getProperty("returnType", "void");


	help.setWordWrap(AttributedString::byWord);


	if (multiLine)
	{
		help.setJustification(Justification::topLeft);
		help.setLineSpacing(1.5f);
		help.append("Name:\n  ", GLOBAL_BOLD_FONT(), textColour);
		help.append(name, GLOBAL_MONOSPACE_FONT(), textColour.withAlpha(0.8f));
		help.append(arguments + "\n\n", GLOBAL_MONOSPACE_FONT(), textColour.withAlpha(0.6f));
		help.append("Description:\n  ", GLOBAL_BOLD_FONT(), textColour);
		help.append(description + "\n\n", GLOBAL_FONT(), textColour.withAlpha(0.8f));

		help.append("Return Type:\n  ", GLOBAL_BOLD_FONT(), textColour);
		help.append(method.getProperty("returnType", "void"), GLOBAL_MONOSPACE_FONT(), textColour.withAlpha(0.8f));
	}

	else
	{
		help.setJustification(Justification::centredLeft);
		help.append(description, GLOBAL_BOLD_FONT(), textColour.withAlpha(0.8f));

		const String oneLineReturnType = method.getProperty("returnType", "");

		if (oneLineReturnType.isNotEmpty())
		{
			help.append("\nReturn Type: ", GLOBAL_BOLD_FONT(), textColour);
			help.append(oneLineReturnType, GLOBAL_MONOSPACE_FONT(), textColour.withAlpha(0.8f));
		}
	}

	return help;
}

String ValueTreeApiHelpers::createCodeToInsert(const ValueTree &method, const String &className)
{
	const String name = method.getProperty(Identifier("name")).toString();

	if (name == "setMouseCallback")
	{
		const String argumentName = "event";
		String functionDef = className;
		functionDef << "." << name + "(function(" << argumentName << ")\n";
		functionDef << "{\n\t\n});\n";

		return functionDef;
	}
	else if (name == "setLoadingCallback")
	{
		const String argumentName = "isPreloading";
		String functionDef = className;
		functionDef << "." << name + "(function(" << argumentName << ")\n";
		functionDef << "{\n\t\n});\n";

		return functionDef;
	}
	else if (name == "setTimerCallback")
	{
		const String argumentName = "";
		String functionDef = className;
		functionDef << "." << name + "(function(" << argumentName << ")\n";
		functionDef << "{\n\t\n});\n";

		return functionDef;
	}
	else if (name == "setPaintRoutine")
	{
	const String argumentName = "g";
	String functionDef = className;
	functionDef << "." << name + "(function(" << argumentName << ")\n";
	functionDef << "{\n\t\n});\n";

	return functionDef;
	}
	else
	{
	const String arguments = method.getProperty(Identifier("arguments")).toString();

	return String(className + "." + name + arguments);
	}
}

void ValueTreeApiHelpers::getColourAndCharForType(int type, char &c, Colour &colour)
{
	const float alpha = 0.6f;
	const float brightness = 0.8f;

	switch (type)
	{
	case (int)0:	c = 'R'; break;
	case (int)1:		c = 'V'; break;
	case (int)2:			c = 'C'; break;
	case (int)3:	c = 'I'; break;
	case (int)4:			c = 'G'; break;
	case (int)5:			c = 'F'; break;
	case (int)6:			c = 'A'; break;
	case (int)7:		c = 'F'; break;
	case (int)8:		c = 'N'; break;
	default:											c = 'V'; break;
	}

	switch (c)
	{
	case 'I': colour = Colours::blue.withAlpha(alpha).withBrightness(brightness); break;
	case 'V': colour = Colours::cyan.withAlpha(alpha).withBrightness(brightness); break;
	case 'G': colour = Colours::green.withAlpha(alpha).withBrightness(brightness); break;
	case 'C': colour = Colours::yellow.withAlpha(alpha).withBrightness(brightness); break;
	case 'R': colour = Colours::red.withAlpha(alpha).withBrightness(brightness); break;
	case 'A': colour = Colours::orange.withAlpha(alpha).withBrightness(brightness); break;
	case 'F': colour = Colours::purple.withAlpha(alpha).withBrightness(brightness); break;
	case 'E': colour = Colours::chocolate.withAlpha(alpha).withBrightness(brightness); break;
	case 'N': colour = Colours::pink.withAlpha(alpha).withBrightness(brightness); break;
	}
}

void DebugableObjectBase::writeAsJSON(OutputStream& outputStream, int indentLevel, bool allOnOneLine, int maximumDecimalPlaces)
{
	outputStream.writeString(getDebugName() + " - " + getDebugValue());
}

void ApiProviderBase::getColourAndLetterForType(int type, Colour& colour, char& letter)
{
	colour = Colours::white;
	letter = 'U'; // You know for, like "unused"...
}

hise::DebugableObjectBase* ApiProviderBase::getDebugObject(const String& token)
{
	if (token.isNotEmpty())
	{
		int numObjects = getNumDebugObjects();

		Identifier id(token);

		for (int i = 0; i < numObjects; i++)
		{
			auto info = getDebugInformation(i);

			if (auto obj = info->getObject())
			{
				if (obj->getObjectName() == id || obj->getInstanceName() == id)
				{
					// Don't spit out internal objects by default...
					if (obj->isInternalObject())
						continue;

					return obj;
				}
			}
		}

		return nullptr;
	}

	return nullptr;
}

ApiProviderBase::Holder::~Holder()
{}

void ApiProviderBase::Holder::addEditor(Component* editor)
{
	repaintUpdater.editors.add(editor);
}

void ApiProviderBase::Holder::removeEditor(Component* editor)
{
	repaintUpdater.editors.removeAllInstancesOf(editor);
}

int ApiProviderBase::Holder::getCodeFontSize() const
{ return 15; }

void ApiProviderBase::Holder::setActiveEditor(JavascriptCodeEditor* e, CodeDocument::Position pos)
{}

JavascriptCodeEditor* ApiProviderBase::Holder::getActiveEditor()
{ return nullptr; }

ValueTree ApiProviderBase::Holder::createApiTree()
{ return {}; }

void ApiProviderBase::Holder::jumpToDefinition(const String& token, const String& namespaceId)
{
	ignoreUnused(token, namespaceId);
}

bool ApiProviderBase::Holder::handleKeyPress(const KeyPress& k, Component* c)
{
	ignoreUnused(k, c);
	return false; 
}

void ApiProviderBase::Holder::addPopupMenuItems(PopupMenu& m, Component* c, const MouseEvent& e)
{
	ignoreUnused(m, c, e);
}

bool ApiProviderBase::Holder::performPopupMenuAction(int menuId, Component* c)
{
	ignoreUnused(menuId, c);
	return false;
}

void ApiProviderBase::Holder::handleBreakpoints(const Identifier& codeFile, Graphics& g, Component* c)
{
	ignoreUnused(codeFile, g, c);
}

void ApiProviderBase::Holder::handleBreakpointClick(const Identifier& codeFile, CodeEditorComponent& ed,
	const MouseEvent& e)
{
	ignoreUnused(codeFile, ed, e);
}

juce::ReadWriteLock& ApiProviderBase::Holder::getDebugLock()
{ return debugLock; }

bool ApiProviderBase::Holder::shouldReleaseDebugLock() const
{ return wantsToCompile; }

ApiProviderBase::Holder::CompileDebugLock::CompileDebugLock(Holder& h):
	p(h),
	prevValue(h.wantsToCompile, true),
	sl(h.getDebugLock())
{
}

ApiProviderBase::Holder::CompileDebugLock::~CompileDebugLock()
{

}

void ApiProviderBase::Holder::RepaintUpdater::update(int index)
{
	if (lastIndex != index)
	{
		lastIndex = index;
		triggerAsyncUpdate();
	}
}

void ApiProviderBase::Holder::RepaintUpdater::handleAsyncUpdate()
{
	for (int i = 0; i < editors.size(); i++)
	{
		editors[i]->repaint();
	}
}

ApiProviderBase* ApiProviderBase::ApiComponentBase::getProviderBase()
{
	if (holder != nullptr)
		return holder->getProviderBase();

	return nullptr;
}

void ApiProviderBase::ApiComponentBase::providerWasRebuilt()
{}

void ApiProviderBase::ApiComponentBase::providerCleared()
{}

void ApiProviderBase::ApiComponentBase::registerAtHolder()
{
	if (holder != nullptr)
		holder->registeredComponents.addIfNotAlreadyThere(this);
}

void ApiProviderBase::ApiComponentBase::deregisterAtHolder()
{
	if (holder != nullptr)
		holder->registeredComponents.removeAllInstancesOf(this);
}

ApiProviderBase::ApiComponentBase::ApiComponentBase(Holder* h):
	holder(h)
{
	registerAtHolder();
}

ApiProviderBase::ApiComponentBase::~ApiComponentBase()
{
	deregisterAtHolder();
}

ApiProviderBase::~ApiProviderBase()
{}

String ApiProviderBase::getHoverString(const String& token)
{ 
	if (auto obj = getDebugObject(token))
	{
		String s;

		s << obj->getDebugDataType() << " " << obj->getDebugName() << ": " << obj->getDebugValue();
		return s;
	}

	return "";
}

void ApiProviderBase::Holder::rebuild()
{
	for (auto c : registeredComponents)
	{
		if (c != nullptr)
			c->providerWasRebuilt();
	}
}

void ApiProviderBase::Holder::sendClearMessage()
{
    for (auto c : registeredComponents)
    {
        if (c != nullptr)
            c->providerCleared();
    }
}

void DebugableObjectBase::updateLocation(Location& l, var possibleObject)
{
	if (auto obj = dynamic_cast<DebugableObjectBase*>(possibleObject.getObject()))
	{
		auto newLocation = obj->getLocation();
		if (newLocation.charNumber != 0)
			l = newLocation;
	}
}

DynamicDebugableObjectWrapper::DynamicDebugableObjectWrapper(DynamicObject::Ptr obj_, const Identifier& className_,
	const Identifier& instanceId_):
	obj(obj_),
	className(className_),
	instanceId(instanceId_)
{

}

Identifier DynamicDebugableObjectWrapper::getObjectName() const
{ return className; }

Identifier DynamicDebugableObjectWrapper::getInstanceName() const
{ return instanceId; }

String DynamicDebugableObjectWrapper::getDebugValue() const
{ return getInstanceName().toString(); }

void DynamicDebugableObjectWrapper::getAllFunctionNames(Array<Identifier>& functions) const
{
	for (const auto& p : obj->getProperties())
	{
		if (p.value.isMethod())
			functions.add(p.name);
	}
}

void DynamicDebugableObjectWrapper::getAllConstants(Array<Identifier>& ids) const
{
	for (const auto& p : obj->getProperties())
	{
		if (p.value.isMethod())
			continue;

		ids.add(p.name);
	}
}

const var DynamicDebugableObjectWrapper::getConstantValue(int index) const
{ return obj->getProperties().getValueAt(index); }

void DebugInformationBase::doubleClickCallback(const MouseEvent& e, Component* componentToNotify)
{
	if (auto obj = getObject())
		getObject()->doubleClickCallback(e, componentToNotify);
}

int DebugInformationBase::getType() const
{ 
	if (auto obj = getObject())
		return obj->getTypeNumber();

	return 0; 
}

int DebugInformationBase::getNumChildElements() const
{ 
	if (auto obj = getObject())
	{
		auto numCustom = obj->getNumChildElements();

		if (numCustom != -1)
			return numCustom;
	}

	return 0; 
}

DebugInformationBase::Ptr DebugInformationBase::getChildElement(int index)
{ 
	if (auto obj = getObject())
	{
		return obj->getChildElement(index);

	}
	return nullptr; 
}

String DebugInformationBase::getTextForName() const
{
	if (auto obj = getObject())
		return obj->getDebugName();

	return "undefined";
}

String DebugInformationBase::getCategory() const
{ 
	if (auto obj = getObject())
		return obj->getCategory();

	return ""; 
}

DebugableObjectBase::Location DebugInformationBase::getLocation() const
{
	if (auto obj = getObject())
		return obj->getLocation();

	return DebugableObjectBase::Location();
}

String DebugInformationBase::getTextForType() const
{ return "unknown"; }

String DebugInformationBase::getTextForDataType() const
{ 
	if (auto obj = getObject()) 
		return obj->getDebugDataType(); 

	return "undefined";
}

String DebugInformationBase::getTextForValue() const
{
	if (auto obj = getObject())
		return obj->getDebugValue();

	return "empty";
}

bool DebugInformationBase::isWatchable() const
{ 
	if (auto obj = getObject())
		return obj->isWatchable();

	return true; 
}

bool DebugInformationBase::isAutocompleteable() const
{
	if (auto obj = getObject())
		return obj->isAutocompleteable();

	return true;
}

String DebugInformationBase::getCodeToInsert() const
{ 
	return ""; 
}

AttributedString DebugInformationBase::getDescription() const
{
	if (auto obj = getObject())
		return obj->getDescription();

	return AttributedString();
}

DebugableObjectBase* DebugInformationBase::getObject()
{ return nullptr; }

const DebugableObjectBase* DebugInformationBase::getObject() const
{ return nullptr; }

DebugInformationBase::~DebugInformationBase()
{}

String DebugInformationBase::replaceParentWildcard(const String& id, const String& parentId)
{
	static const String pWildcard = "%PARENT%";

	if (id.contains(pWildcard))
	{
		String s;
		s << parentId << id.fromLastOccurrenceOf(pWildcard, false, false);
		return s;
	}

	return id;
}

String DebugInformationBase::getVarType(const var& v)
{
	if (v.isUndefined())	return "undefined";
	else if (v.isArray())	return "Array";
	else if (v.isBool())	return "bool";
	else if (v.isInt() ||
		v.isInt64())	return "int";
	else if (v.isBuffer()) return "Buffer";
	else if (v.isObject())
	{
		if (auto d = dynamic_cast<DebugableObjectBase*>(v.getObject()))
		{
			return d->getDebugDataType();
		}
		else return "Object";
	}
	else if (v.isDouble()) return "double";
	else if (v.isString()) return "String";
	else if (v.isMethod()) return "function";

	return "undefined";
}

bool DebugInformationBase::callRecursive(const std::function<bool(DebugInformationBase&)>& f)
{
	if(f(*this))
		return true;

	for(int i = 0; i < getNumChildElements(); i++)
	{
		auto c = getChildElement(i);
		if(c->callRecursive(f))
			return true;
	}

	return false;
}

StringArray DebugInformationBase::createTextArray() const
{
	StringArray sa;

	sa.add(getTextForType());
	sa.add(getTextForDataType());
	sa.add(getTextForName());
	sa.add(getTextForValue());

	return sa;
}

ObjectDebugInformation::ObjectDebugInformation(DebugableObjectBase* b, int type_):
	obj(b),
	type(type_)
{

}

String ObjectDebugInformation::getCodeToInsert() const
{ 
	if(obj != nullptr)
		return obj->getInstanceName().toString(); 

	return {};

}

Component* DebugInformationBase::createPopupComponent(const MouseEvent& e, Component* componentToNotify)
{
	if (auto obj = getObject())
	{
		obj->setCurrentExpression(getCodeToInsert());
		return getObject()->createPopupComponent(e, componentToNotify);
	}
		

	return nullptr;
}

ComponentForDebugInformation::ComponentForDebugInformation(DebugableObjectBase* obj_, ApiProviderBase::Holder* h) :
	holder(h),
	obj(obj_)
{
	expression = obj->currentExpression;
	jassert(expression.isNotEmpty());
}

void ComponentForDebugInformation::search()
{
	if (obj == nullptr)
	{
		if (holder == nullptr)
			return;

		ScopedReadLock sl(holder->getDebugLock());

		auto provider = holder->getProviderBase();

		if (provider == nullptr)
			return;

		for (int i = 0; i < provider->getNumDebugObjects(); i++)
		{
			if (searchRecursive(provider->getDebugInformation(i).get()))
				break;
		}
	}
}

bool ComponentForDebugInformation::searchRecursive(DebugInformationBase* b)
{
	if (b == nullptr)
		return false;

	if (holder->shouldReleaseDebugLock())
		return true;

	if (b->getCodeToInsert() == expression)
	{
		obj = b->getObject();
		refresh();
		return true;
	}

	for (int i = 0; i < b->getNumChildElements(); i++)
	{
		if (searchRecursive(b->getChildElement(i).get()))
			return true;
	}

	return false;
}

String ComponentForDebugInformation::getTitle() const
{
	String s;

	if (obj != nullptr)
		s << obj->getDebugName() << ": ";

	s << expression;
	return s;
}

struct ZoomableDataViewer: public AbstractZoomableView,
						   public Component
{
	static constexpr int LabelHeight = 20;

	struct DrawContext
	{
		Rectangle<float> fullBounds;
		Rectangle<float> legend;
		Range<double> dataRangeToShow;
	};

	enum class IndexDomain
	{
		Absolute,
		Relative,
		Time
	};

	static StringArray getIndexDomainTypes() { return { "Absolute", "Relative", "Time" }; }

	enum class ValueDomain
	{
		Absolute,
		Relative,
		Decibel
	};

	static StringArray getValueDomainTypes() { return { "Absolute", "Relative", "Decibel" }; }

	struct Data
	{
		Colour getColour() const
		{
			return Colour(label.hash()).withSaturation(0.5).withBrightness(0.7f).withAlpha(1.0f);
		}

		void draw(Graphics& g, DrawContext& ctx)
		{
			auto tw = (float)LabelHeight + GLOBAL_MONOSPACE_FONT().getStringWidthFloat(label) + 20.0f;
			
			auto tl = ctx.legend.removeFromLeft(tw);

			auto c = getColour();

			g.setColour(c);

			auto thisRange = ctx.dataRangeToShow;//.expanded(1.0).getIntersectionWith({0.0, (double)size});

			if(!thisRange.isEmpty() && enabled)
			{
				bool first = true;

				float xDelta = ctx.fullBounds.getWidth() / (ctx.dataRangeToShow.getLength() - 1);

				if(xDelta < 0.25)
				{
					RectangleList<float> list;

					auto samplesPerPixel = 1.0 / xDelta;
					auto sampleIndex = thisRange.getStart();

					for(int i = (int)ctx.fullBounds.getX(); i < (int)ctx.fullBounds.getRight(); i++)
					{
						auto thisIndex = (int)(sampleIndex);
						auto numThisTime = jmin<int>((int)size - sampleIndex, roundToInt(samplesPerPixel + 1.0));

						auto range = FloatVectorOperations::findMinAndMax(data.get() + thisIndex, numThisTime);

						auto yMax = valueRange.convertTo0to1(range.getEnd());
						auto yMin = valueRange.convertTo0to1(range.getStart());

						auto y = ctx.fullBounds.getY() + (1.0f - yMax) * ctx.fullBounds.getHeight();
						auto bottom = ctx.fullBounds.getY() + (1.0f - yMin) * ctx.fullBounds.getHeight();
						auto h = bottom - y;

						auto x = (float)i - 0.25f;
						auto w = 1.5f;

						list.addWithoutMerging({x, y, w, h});

						sampleIndex += samplesPerPixel;
					}

					g.fillRectList(list);
				}
				else
				{
					Path p;

					g.saveState();
					g.reduceClipRegion(ctx.fullBounds.toNearestInt());

					RectangleList<float> points;

					for(int i = (int)thisRange.getStart(); i < ((int)thisRange.getEnd() + 1); i++)
					{
						auto x = -1.0 *  (thisRange.getStart() - (double)i) * xDelta;
						x += ctx.fullBounds.getX();

						auto y = 1.0f - valueRange.convertTo0to1(data[i]);

						Point<float> pos { (float)x, ctx.fullBounds.getY() + y * ctx.fullBounds.getHeight() };

						if(thisRange.getLength() < 20.0)
							points.add(Rectangle<float>(pos, pos).withSizeKeepingCentre(6.0f, 6.0f));

						if(first)
						{
							p.startNewSubPath(pos);
							first = false;
						}
						else
							p.lineTo(pos);
					}

					g.strokePath(p, PathStrokeType(1.5f));
					g.restoreState();

					if(!points.isEmpty())
						g.fillRectList(points);
				}
			}


			g.setColour(c);

			if(enabled)
				g.fillRoundedRectangle(tl.removeFromLeft(tl.getHeight()).reduced(2.0f), 4.0f);
			else
				g.drawRoundedRectangle(tl.removeFromLeft(tl.getHeight()).reduced(2.0f), 4.0f, 2.0f);
			
			tl.removeFromLeft(5.0f);
			g.setFont(GLOBAL_MONOSPACE_FONT());
			g.setColour(Colours::white.withAlpha(enabled ? 0.7f : 0.4f));
			g.drawText(label, tl, Justification::left);
		}

		int index;
		String label;
		HeapBlock<float> data;
		NormalisableRange<float> valueRange;
		size_t size;
		bool enabled = true;
	};

	ZoomableDataViewer()
	{
		setOpaque(true);
		setAsParentComponent(this);
	}

	UndoManager um;
	OwnedArray<Data> data;

	double getTotalLength() const override { return (double)maxLength; }

	UndoManager* getUndoManager() override { return &um; }

	

	void paint(Graphics& g) override
	{
		DrawContext ctx;
		auto b = getLocalBounds().toFloat().reduced(10.0f);
		ctx.legend = b.removeFromTop(24.0f);

		drawBackgroundGrid(g, b.toNearestInt());

		drawDragDistance(g, getViewportPosition());

		b.removeFromTop((float)FirstItemOffset);
		
		ctx.fullBounds = b;
		ctx.dataRangeToShow = getScaledZoomRange();

		for(auto d: data)
			d->draw(g, ctx);

		String v;

		auto zr = getScaledZoomRange();
		auto vr = yRange;

		v << "X: [" << getXString(zr.getStart(), timeDomain,  xRange) << " - " << getXString(zr.getEnd(), timeDomain, xRange) << "]";
		v << "Y: [" << getYString(vr.getStart(), valueDomain, vr)     << " - " << getYString(vr.getEnd(), valueDomain, vr)    << "]";

		g.setFont(GLOBAL_MONOSPACE_FONT());
		g.drawText(v, ctx.legend, Justification::right);

		if(currentTooltipData)
		{
			g.setColour(currentTooltipData.d->getColour());

			auto r = Rectangle<int>(currentTooltipData.position, currentTooltipData.position).withSizeKeepingCentre(14, 14).toFloat();
			g.drawEllipse(r, 3.0f);
		}
	}

	static String getXString(float xValue, IndexDomain d, Range<float> contextValue)
	{
		switch(d)
		{
		case IndexDomain::Absolute:
			return String(xValue, 1);
		case IndexDomain::Relative:
			return String(xValue / contextValue.getLength(), 1) + "%";
		case IndexDomain::Time:
			return String(1000.0 * xValue / contextValue.getLength()) + "ms";
		}

		return String(xValue, 1);
	}

	static String getYString(float yValue, ValueDomain d, Range<float> contextRange)
	{
		switch(d)
		{
		case ValueDomain::Absolute:
			return String(yValue);
		case ValueDomain::Relative:
			return String(100.0 * NormalisableRange<float>(contextRange).convertTo0to1(yValue), 1) + "%";
		case ValueDomain::Decibel:
			return String(Decibels::gainToDecibels(std::abs(yValue)), 1) + "dB";
		default: ;
		}

		return String(yValue);
	}

	struct TooltipData
	{
		operator bool() const { return d != nullptr; }
		Data* d = nullptr;

		String toString(ValueDomain vd, Range<float> r) const
		{
			jassert(*this);

			String s;
			s << d->label << "[";
			s << String(value.first) << "] = " << getYString(value.second, vd, r);
			return s;
		}
		std::pair<int, float> value;
		Point<int> position;
	};

	Rectangle<int> getViewportPosition() const
	{
		auto b = getLocalBounds().reduced(10);
		b.removeFromTop(24 + FirstItemOffset);
		return b;
	}

	TooltipData getTooltipData(const MouseEvent& e)
	{
		auto vp = getViewportPosition();

		if(!vp.contains(e.getPosition()))
			return {};

		auto normX = (double)(e.getPosition().getX() - vp.getX()) / (double)vp.getWidth();

		auto zr = getScaledZoomRange();

		if(zr.getLength() == 0.0)
			return {};

		zr.setEnd(zr.getEnd() - 1);

		NormalisableRange<double> scaled(zr);
		auto index = roundToInt((float)scaled.convertFrom0to1(normX));

		auto xPos = scaled.convertTo0to1((double)index);

		auto x = vp.getX() + roundToInt(xPos * (float)vp.getWidth());

		std::vector<TooltipData> matches;

		for(auto d: data)
		{
			TooltipData nd;
			nd.d = d;
			auto v = d->data[index];
			nd.value = { index, v };
			auto yPos = vp.getY() + roundToInt((1.0f - d->valueRange.convertTo0to1(v)) * (float)vp.getHeight());
			nd.position = { x, yPos};
			matches.push_back(nd);
		}

		

		auto distance = INT_MAX;
		TooltipData bestMatch;

		for(const auto& d: matches)
		{
			auto thisDistance = d.position.getDistanceFrom(e.getPosition());
			if(thisDistance < distance)
			{
				distance = thisDistance;
				bestMatch = d;
			}
		}

		return distance < 30 ? bestMatch : TooltipData();
	}

	TooltipData currentTooltipData;

	String getTooltip() override
	{
		if(currentTooltipData)
			return currentTooltipData.toString(valueDomain, yRange);

		return "";
	}

	String getTextForDistance(float width) const override
	{
		return getXString(width, timeDomain, xRange);
	}

	void resized() override
	{
		auto b = getLocalBounds();
		auto legend = b.removeFromTop(24);
		onResize(b);
	}

	void mouseDown(const MouseEvent& e) override
	{
		if(e.mods.isRightButtonDown())
		{
			PopupLookAndFeel plaf;
			PopupMenu m;
			m.setLookAndFeel(&plaf);

			m.addSectionHeader("Toggle graphs");

			for(int i = 0; i < data.size(); i++)
			{
				m.addItem(i + 30, data[i]->label, true, data[i]->enabled);
			}

			m.addSectionHeader("Select X Domain");

			auto xdomains = getIndexDomainTypes();
			auto ydomains = getValueDomainTypes();

			for(int i = 0; i < xdomains.size(); i++)
				m.addItem(i+10, xdomains[i], true, (int)timeDomain == i);

			m.addSectionHeader("Select Y Domain");

			for(int i = 0; i < ydomains.size(); i++)
				m.addItem(i+20, ydomains[i], true, (int)valueDomain == i);

			auto r = m.show();

			if(r >= 30)
			{
				data[r-30]->enabled = !data[r-30]->enabled;
			}
			if(r >= 20)
				valueDomain = (ValueDomain)(r - 20);
			else if(r >= 10)
				timeDomain = (IndexDomain)(r - 10);

			repaint();
			return;
		}

		handleMouseEvent(e, MouseEventType::MouseDown);
	}
	void mouseDrag(const MouseEvent& e) override { handleMouseEvent(e, MouseEventType::MouseDrag); }
	void mouseDoubleClick(const MouseEvent& e) override { handleMouseEvent(e, MouseEventType::MouseDoubleClick); }
	void mouseUp(const MouseEvent& e) override { handleMouseEvent(e, MouseEventType::MouseUp); }
	void mouseMove(const MouseEvent& e) override
	{
		handleMouseEvent(e, MouseEventType::MouseMove);
		currentTooltipData = getTooltipData(e);
		repaint();
	}
	void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) override { handleMouseWheelEvent(e, wheel); }

	void applyPosition(const Rectangle<int>& screenBoundsOfTooltipClient, Rectangle<int>& tooltipRectangleAtOrigin) override
	{
		if(currentTooltipData)
		{
			auto pos = screenBoundsOfTooltipClient.getPosition();
			auto center = pos + currentTooltipData.position;
			center = center.translated(0, 30);
			tooltipRectangleAtOrigin.setCentre(center.x, center.y);
		}
	}

	void setData(const var& var, const float* ptr, size_t size)
	{
		auto index = (int)var["index"];
		auto label = var["graph"].toString();
		bool found = false;

		for(auto d: data)
		{
			if(d->index == index)
			{
				d->label = label;
				d->data.allocate(size, false);
				d->size = size;
				FloatVectorOperations::copy(d->data.get(), ptr, (int)size);
				d->valueRange = {FloatVectorOperations::findMinAndMax(ptr, (int)size)};
				found = true;
			}
		}

		if(!found)
		{
			auto d = new Data();
			d->index = index;
			d->label = label;
			d->data.allocate(size, false);
			d->size = size;
			d->valueRange = {FloatVectorOperations::findMinAndMax(ptr, (int)size)};

			FloatVectorOperations::copy(d->data.get(), ptr, (int)size);
			data.add(d);
		}

		maxLength = 0;
		yRange = {};
		xRange = {};

		for(auto d: data)
		{
			yRange = yRange.getUnionWith(d->valueRange.getRange());
			maxLength = jmax(maxLength, d->size);
			xRange = xRange.getUnionWith({0.0f, (float)d->size});
		}
			

		updateIndexToShow();
	}

	Range<float> yRange;
	Range<float> xRange;

	size_t maxLength = 0;

	ValueDomain valueDomain = ValueDomain::Absolute;
	IndexDomain timeDomain = IndexDomain::Absolute;
};

BufferViewer::BufferViewer(DebugInformationBase* info, ApiProviderBase::Holder* holder_):
	ApiComponentBase(holder_),
	Component("Graph Viewer: " + info->getCodeToInsert()),
	resizer(this, nullptr)
{
	addAndMakeVisible(newViewer = new ZoomableDataViewer());
	addAndMakeVisible(resizer);
	setFromDebugInformation(info);
	startTimer(30);
	setSize(700, 500);
}

void BufferViewer::providerCleared()
{
	dataToShow = var();
}

void BufferViewer::providerWasRebuilt()
{
	if (auto p = getProviderBase())
	{
		for (int i = 0; i < p->getNumDebugObjects(); i++)
		{
			auto di = p->getDebugInformation(i);

			di->callRecursive([&](DebugInformationBase& info)
			{
				if (info.getCodeToInsert() == codeToInsert)
				{
					setFromDebugInformation(&info);
					dirty = true;
					return true;
				}

				return false;
			});
		};
	}
}

void BufferViewer::removeSizeOneArrays(var& dataToReduce)
{
	if(dataToReduce.isArray())
	{
		if(dataToReduce.size() == 1)
		{
			auto firstValue = dataToReduce[0];
			dataToReduce = firstValue;
		}
		else
		{
			for(auto& v: *dataToReduce.getArray())
				removeSizeOneArrays(v);
		}
	}
}

void BufferViewer::setFromDebugInformation(DebugInformationBase* info)
{
	if (info != nullptr)
	{
		codeToInsert = info->getCodeToInsert();
		dataToShow = info->getVariantCopy();

		labels.clear();

		for(int i = 0; i < info->getNumChildElements(); i++)
			labels.add(info->getChildElement(i)->getCodeToInsert());
	}
}

std::string BufferViewer::btoa(const void* data, size_t numBytes)
{
	auto isBase64 = [](uint8 c)
	{
		return (std::isalnum(c) || (c == '+') || (c == '/'));
	};

	static const std::string b64Characters =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	auto bytesToEncode = reinterpret_cast<const uint8*>(data);

	std::string ret;
	ret.reserve(numBytes);
	int i = 0;
	int j = 0;
	uint8 d1[3];
	uint8 d2[4];

	while (numBytes--) 
	{
		d1[i++] = *(bytesToEncode++);
		if (i == 3) 
		{
			d2[0] = (d1[0] & 0xfc) >> 2;
			d2[1] = ((d1[0] & 0x03) << 4) + ((d1[1] & 0xf0) >> 4);
			d2[2] = ((d1[1] & 0x0f) << 2) + ((d1[2] & 0xc0) >> 6);
			d2[3] = d1[2] & 0x3f;

			for(i = 0; (i <4) ; i++)
				ret += b64Characters[d2[i]];

			i = 0;
		}
	}

	if (i)
	{
		for(j = i; j < 3; j++)
			d1[j] = '\0';

		d2[0] = (d1[0] & 0xfc) >> 2;
		d2[1] = ((d1[0] & 0x03) << 4) + ((d1[1] & 0xf0) >> 4);
		d2[2] = ((d1[1] & 0x0f) << 2) + ((d1[2] & 0xc0) >> 6);
		d2[3] = d1[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += b64Characters[d2[j]];

		while((i++ < 3))
			ret += '=';
	}

	return ret;
}

struct JSONViewer: public Component
{
	static std::string toString(const var& obj)
	{
		std::string x;

		if(auto dbg = dynamic_cast<DebugableObjectBase*>(obj.getObject()))
		{
			x += dbg->getDebugName().toStdString();
			x += ": ";
			x += dbg->getDebugValue().toStdString();
		}
	}

	JSONViewer(const var& obj)
	{
		auto data = JSON::toString(obj, false);
		doc.setDisableUndo(true);
		doc.replaceAllContent(data);

		editor = new CodeEditorComponent(doc, &tokeniser);
		editor->setReadOnly(true);
		editor->setColour(CodeEditorComponent::ColourIds::backgroundColourId, Colour(0xFF242424));
		editor->setColour(CodeEditorComponent::ColourIds::lineNumberBackgroundId, Colour(0xFF333333));
		editor->setColour(CodeEditorComponent::ColourIds::lineNumberTextId, Colour(0xFF666666));

		addAndMakeVisible(editor);

		sf.addScrollBarToAnimate(editor->getScrollbar(false));
		sf.addScrollBarToAnimate(editor->getScrollbar(true));

		auto height = jmin(14, doc.getNumLines() + 2) * editor->getLineHeight();

		setSize(600, height);
	}

	void resized() override
	{
		editor->setBounds(getLocalBounds());
	}

	ScrollbarFader sf;
	CodeDocument doc;
	JavascriptTokeniser tokeniser;
	ScopedPointer<CodeEditorComponent> editor;
};


bool BufferViewer::isArrayOrBuffer(const var& v, bool numbersAreOK)
{
	if(v.isArray())
	{
		auto isArray = true;

		for(const auto& c: *v.getArray())
			isArray &= isArrayOrBuffer(c, true);

		return isArray;
	}
	if(v.isBuffer())
		return true;
	if(auto obj = v.getDynamicObject())
	{
		return false;
	}
	if(v.isInt() || v.isDouble() || v.isInt64() || v.isBool())
		return numbersAreOK;

	return false;
}

void BufferViewer::timerCallback()
{
	if (dirty && !dataToShow.isUndefined())
	{
		if(!isArrayOrBuffer(dataToShow))
			return;

		removeSizeOneArrays(dataToShow);

		auto createCode = [&](int index, const String& name, const var& d)
		{
			DynamicObject::Ptr obj = new DynamicObject();
			obj->setProperty("index", index);
			obj->setProperty("graph", name);

			auto header = JSON::toString(var(obj.get()), true);

			if(d.isBuffer())
			{
				auto ptr = d.getBuffer()->buffer.getReadPointer(0);
				auto size = d.getBuffer()->size;

				setData(var(obj.get()), ptr, size);
			}
				
			if(d.isArray())
			{
				heap<float> data;
				data.setSize(d.size());
				for(int i = 0; i < d.size(); i++)
					data[i] = (float)d[i];

				auto ptr = data.begin();
				auto size = data.size();

				setData(var(obj.get()), ptr, size);
			}
		};

		if(dataToShow.isBuffer())
		{
			createCode(0, codeToInsert, dataToShow);
		}

		if(dataToShow.isArray())
		{
			auto numElements = dataToShow.size();

			if(numElements == 0)
				createCode(0, "empty", var(Array<var>()));
			else if(numElements == 1)
			{
				if(isArrayOrBuffer(dataToShow[0]))
					createCode(0, labels[0], dataToShow[0]);
			}
			else
			{
				auto multiArray = dataToShow[0].isArray() || dataToShow[0].isBuffer();

				if(multiArray)
				{
					int idx = 0;
					for(const auto& d: *dataToShow.getArray())
					{
						createCode(idx, labels[idx], d);
						idx++;
					}
				}
				else
					createCode(0, codeToInsert, dataToShow);
			}
		}
		dirty = false;
	}
}

void BufferViewer::resized()
{
	auto b = getLocalBounds();
	resizer.setBounds(b.removeFromBottom(20).removeFromRight(20));
	newViewer->setBounds(b);
}

void BufferViewer::setData(const var& obj, const float* data, size_t numElements)
{
	dynamic_cast<ZoomableDataViewer*>(newViewer.get())->setData(obj, data, numElements);
}
} // namespace hise
