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

#pragma once

/** TODO:
 *
 * add containers: viewport / flexbox (maybe with flexboxviewport) OK
 * - remove methods from root class OK
 * * check image provider OK
 * * - use component width / height property when dimension property is auto in css::flexbox OK
 * fix duplicate stuff OK
 ** - add components: textbox with markdown support OK
 * - add sendRepaintMessage() OK
 * - fix class property not working OK
 * add connection to processors / scriptnode - use HiSlider or other class OK
 * * add persistence (maybe user preset model?) OK
 *
 * add logic callback to drag container
 * - make examples: file browser, FX section, 
 * - add color properties
 * - add component properties to CSS stylesheet
 * - use BorderPanel for panel with paint routine OK
 */


namespace hise {
namespace dyncomp
{

using namespace hise;
using namespace juce;

#define DECLARE_ID(x) static const Identifier x(#x);

namespace dcid
{
	DECLARE_ID(ContentProperties);
	DECLARE_ID(Root);

	DECLARE_ID(id);
	DECLARE_ID(type);
	DECLARE_ID(text);
	DECLARE_ID(enabled);
	DECLARE_ID(visible);
	DECLARE_ID(tooltip);
	DECLARE_ID(defaultValue);
	DECLARE_ID(useUndoManager);

    static const Identifier class_("class");
    DECLARE_ID(elementStyle);

	DECLARE_ID(parentComponent);
	DECLARE_ID(x);
	DECLARE_ID(y);
	DECLARE_ID(width);
	DECLARE_ID(height);
	
	DECLARE_ID(isMomentary);
	DECLARE_ID(radioGroupId);
	DECLARE_ID(setValueOnClick);

	DECLARE_ID(useCustomPopup);
	DECLARE_ID(items);

	DECLARE_ID(editable);
	DECLARE_ID(multiline);
	DECLARE_ID(updateEachKey);

	DECLARE_ID(min);
	DECLARE_ID(max);
	DECLARE_ID(middlePosition);
	DECLARE_ID(stepSize);
	DECLARE_ID(mode);
	DECLARE_ID(suffix);
    DECLARE_ID(style);
	DECLARE_ID(showValuePopup);

	DECLARE_ID(processorId);
	DECLARE_ID(parameterId);

	DECLARE_ID(filmstripImage);
    DECLARE_ID(numStrips);
	DECLARE_ID(isVertical);
	DECLARE_ID(scaleFactor);

    DECLARE_ID(animationSpeed);
    DECLARE_ID(dragMargin);

	DECLARE_ID(FloatingTileData);

	DECLARE_ID(bgColour);
	DECLARE_ID(itemColour);
	DECLARE_ID(itemColour2);
	DECLARE_ID(textColour);

	struct Helpers
	{
		static const Array<Identifier>& getProperties();

		static const Array<Identifier>& getBasicProperties()
		{
			static const Array<Identifier> basicProperties({ 
			dcid::enabled,
			dcid::visible,
			dcid::class_,
			dcid::elementStyle,
			dcid::bgColour,
			dcid::itemColour,
			dcid::itemColour2,
			dcid::textColour
			});

			return basicProperties;
		}

		static var getDefaultValue(const Identifier& p);

		static bool isValidProperty(const Identifier& p)
		{
			return getProperties().contains(p);
		}
	};

}

#undef DECLARE_ID

struct Factory;
struct Base;

/** This is the data model that is used by the dynamic container.
 *
 *  It holds a ValueTree for the data and one for the values.
 */	
struct Data: public ReferenceCountedObject,
			 public ControlledObject
{
	enum class TreeType
	{
		Data,
		Values
	};

	enum class RefreshType
	{
		repaint,
		changed,
		updateValueFromProcessorConnection,
		loseFocus,
		resetValueToDefault,
		numRefreshTypes
	};

	static RefreshType getRefreshType(const var& t);

	using Ptr = ReferenceCountedObjectPtr<Data>;
	using ValueCallback = std::function<void(const Identifier&, const var&)>;

	using ImageProvider = std::function<Image(const String&)>;
	using FontProvider = std::function<Font(const String&, const String&)>;

	Data(MainController* mc, const var& obj, Rectangle<int> position);

	void setValues(const var& valueObject);

	Image getImage(const String& ref);

	simple_css::StyleSheet::Collection::DataProvider* createDataProvider() const;

	/** Converts a JSON object coming from HISE's interface designer to a ValueTree. */
	static ValueTree fromJSON(const var& obj, Rectangle<int> position);

	ReferenceCountedObjectPtr<Base> create(const ValueTree& v);

	void onValueChange(const Identifier& id, const var& newValue, bool useUndoManager);

	const ValueTree& getValueTree(TreeType t) const;

	void setValueCallback(const ValueCallback& v);
	void setDataProviderCallbacks(const ImageProvider& ip_, const FontProvider& fp_);

	var getFloatingTileData(const Identifier& id)
	{
		return floatingTileData->getProperty(id);
	}

	LambdaBroadcaster<ValueTree, RefreshType, bool> refreshBroadcaster;

	ReferenceCountedObjectPtr<ScriptingObjects::GraphicsObject> createGraphicsObject(const ValueTree& dataTree, ConstScriptingObject* obj)
	{
		jassert(dataTree.getType() == Identifier("Component"));

		for(auto r: registeredDrawHandlers)
		{
			if(r->first == dataTree)
				return r->second;
		}

		auto n = new std::pair<ValueTree, ReferenceCountedObjectPtr<ScriptingObjects::GraphicsObject>>();
		n->first = dataTree;
		n->second = new ScriptingObjects::GraphicsObject(obj->getScriptProcessor(), obj);
		registeredDrawHandlers.add(n);
		return registeredDrawHandlers.getLast()->second;
	}

	DrawActions::Handler* getDrawHandler(const ValueTree& dataTree) const
	{
		for(auto r: registeredDrawHandlers)
		{
			if(r->first == dataTree)
				return &r->second->getDrawHandler();
		}

		return nullptr;
	}

    Factory* getFactory();
    
private:

	OwnedArray<std::pair<ValueTree, ReferenceCountedObjectPtr<ScriptingObjects::GraphicsObject>>> registeredDrawHandlers;

	DynamicObject::Ptr floatingTileData;

	ValueCallback valueCallback;
	ImageProvider ip;
	FontProvider fp;
	UndoManager* um = nullptr;

	ValueTree data;
	ValueTree values;

	ReferenceCountedObjectPtr<ReferenceCountedObject> factory;

};

struct Base: public Component,
		     public ReferenceCountedObject
{
	using Ptr = ReferenceCountedObjectPtr<Base>;
	using WeakPtr = WeakReference<Base>;
	using List = ReferenceCountedArray<Base>;
	using WeakList = Array<WeakPtr>;

	Base(Data::Ptr d, const ValueTree& v);
	~Base() override;
	

	Identifier getId() const;

	virtual void onValue(const var& newValue) = 0;
	virtual void updateChild(const ValueTree& v, bool wasAdded);

	void updateBasicProperties(const Identifier& id, const var& newValue);
	virtual void updatePosition(const Identifier&, const var&);
	virtual void baseChildChanged(Base::Ptr c, bool wasAdded);
	virtual void resizeChild(Base::Ptr b, Rectangle<int> newBounds);
	virtual void hideChild(Base::Ptr b, bool shouldBeVisible)
	{
		b->setVisible(shouldBeVisible);
	}

	static void onRefreshStatic(Base& b, const ValueTree& v, Data::RefreshType rt, bool isRecursive)
	{
		if(b.dataTree == v)
			b.onRefresh(rt, isRecursive);
	}

	void initCSSForChildComponent();

	virtual void onRefresh(Data::RefreshType rt, bool recursive);

	void addChild(Base::Ptr c);

	bool operator==(const Base& other) const noexcept
	{
		return dataTree == other.dataTree;
	}

	bool operator==(const ValueTree& otherData) const noexcept
	{
		return dataTree == otherData;
	}

	void writePositionInValueTree(Rectangle<int> tb, bool useUndoManager);

	var getValueOrDefault() const;
	var getPropertyOrDefault(const Identifier& id) const;

protected:

    virtual Component* getCSSTarget() { return simple_css::FlexboxComponent::Helpers::getComponentForStyleSheet(this); }
    
	Data::Ptr data;
	ValueTree dataTree;
	ValueTree valueReference;

private:

	valuetree::PropertyListener basicPropertyListener;
	valuetree::PropertyListener positionListener;
	valuetree::PropertyListener valueListener;
	valuetree::ChildListener childListener;

	List children;

	JUCE_DECLARE_WEAK_REFERENCEABLE(Base);
};

struct Root: public Base
{
	Root(Data::Ptr d);

	void onValue(const var& newValue) override {}

	void paint(Graphics& g) override
	{
		if(auto slaf = dynamic_cast<simple_css::StyleSheetLookAndFeel*>(&getLookAndFeel()))
			slaf->drawComponentBackground(g, this);
	}

	void updatePosition(const Identifier&, const var&) override
	{
		
	}
};

} // namespace dyncomp
} // namespace hise
