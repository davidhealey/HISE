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

namespace scriptnode {
using namespace juce;
using namespace hise;


NodeProperty::NodeProperty(const Identifier& baseId_, const var& defaultValue_, bool isPublic_):
	baseId(baseId_),
	defaultValue(defaultValue_),
	isPublic(isPublic_)
{}

NodeProperty::~NodeProperty()
{}

ValueTree NodeProperty::getPropertyTree() const
{ return d; }

juce::Value NodeProperty::asJuceValue()
{
	return d.getPropertyAsValue(PropertyIds::Value, um);
}

bool NodeProperty::initialise(UndoManager* um_, ValueTree v)
{
	jassert(v.getType() == PropertyIds::Node);

	valueTreePropertyid = baseId;

	um = um_;

	auto propTree = v.getOrCreateChildWithName(PropertyIds::Properties, um);

	d = propTree.getChildWithProperty(PropertyIds::ID, getValueTreePropertyId().toString());

	if (!d.isValid())
	{
		d = ValueTree(PropertyIds::Property);
		d.setProperty(PropertyIds::ID, getValueTreePropertyId().toString(), nullptr);
		d.setProperty(PropertyIds::Value, defaultValue, nullptr);
		propTree.addChild(d, -1, um);
	}

	postInit();

	return true;
}

juce::Identifier NodeProperty::getValueTreePropertyId() const
{
	return valueTreePropertyid;
}

template <class T>
NodePropertyT<T>::NodePropertyT(const Identifier& id, T defaultValue) :
	NodeProperty(id, defaultValue, false),
	value(defaultValue)
{}

template <class T>
void NodePropertyT<T>::postInit()
{
	updater.setCallback(getPropertyTree(), { PropertyIds::Value }, valuetree::AsyncMode::Synchronously,
		BIND_MEMBER_FUNCTION_2(NodePropertyT::update));
}

template <class T>
void NodePropertyT<T>::storeValue(const T& newValue, UndoManager* um)
{
	if (getPropertyTree().isValid())
		getPropertyTree().setPropertyExcludingListener(&updater, PropertyIds::Value, newValue, um);

	value = newValue;
}

template <class T>
void NodePropertyT<T>::update(Identifier id, var newValue)
{
	value = newValue;

	if (additionalCallback)
		additionalCallback(id, newValue);
}

template <class T>
void NodePropertyT<T>::setAdditionalCallback(const valuetree::PropertyListener::PropertyCallback& c, bool callWithValue)
{
	additionalCallback = c;

	if (callWithValue && additionalCallback)
		additionalCallback(PropertyIds::Value, var(value));
}

template <class T>
T NodePropertyT<T>::getValue() const
{
	return value;
}

template struct NodePropertyT<int>;
template struct NodePropertyT<String>;
template struct NodePropertyT<bool>;

#if HISE_INCLUDE_SCRIPTNODE_UI
ComboBoxWithModeProperty::ComboBoxWithModeProperty(String defaultValue, const Identifier& id):
	ComboBox(),
	mode(id, defaultValue)
{
	addListener(this);
	setLookAndFeel(&plaf);
	setColour(ColourIds::textColourId, Colour(0xFFAAAAAA));
}

void ComboBoxWithModeProperty::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
	if (initialised)
		mode.storeValue(getText(), um);
}

void ComboBoxWithModeProperty::valueTreeCallback(Identifier id, var newValue)
{
	SafeAsyncCall::call<ComboBoxWithModeProperty>(*this, [newValue](ComboBoxWithModeProperty& c)
	{
		c.setText(newValue.toString(), dontSendNotification);
	});
}

void ComboBoxWithModeProperty::mouseDown(const MouseEvent& e)
{
	CHECK_MIDDLE_MOUSE_DOWN(e);
	ComboBox::mouseDown(e);
}

void ComboBoxWithModeProperty::mouseDrag(const MouseEvent& e)
{
	CHECK_MIDDLE_MOUSE_DRAG(e);
	ComboBox::mouseDrag(e);
}

void ComboBoxWithModeProperty::mouseUp(const MouseEvent& e)
{
	CHECK_MIDDLE_MOUSE_UP(e);
	ComboBox::mouseUp(e);
}

void ComboBoxWithModeProperty::initModes(const StringArray& modes, UndoManager* um_, ValueTree v)
{
	if (initialised)
		return;

	clear(dontSendNotification);
	addItemList(modes, 1);

	um = um_;
	mode.initialise(um, v);
	mode.setAdditionalCallback(BIND_MEMBER_FUNCTION_2(ComboBoxWithModeProperty::valueTreeCallback), true);
	initialised = true;
}
#endif

}
