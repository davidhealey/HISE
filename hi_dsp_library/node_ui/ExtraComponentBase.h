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
*   information about commercial licencing:
*
*   http://www.hartinstruments.net/hise/
*
*   HISE is based on the JUCE library,
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#pragma once

namespace scriptnode {

using namespace hise;
using namespace juce;

struct ScriptnodeExtraComponentBase: public ComponentWithMiddleMouseDrag
{
	virtual ~ScriptnodeExtraComponentBase() {};

	virtual void initialise(ObjectWithValueTree* o) {};

};

/** This can be used to query parameter values from the parent ParameterSourceObject. */
struct ParameterWatcherBase
{
	virtual ~ParameterWatcherBase()
	{

	}

protected:

	/** Use this to setup the parameter indexes you want to watch. */
	void setWatchedParameters(const Array<int>& numWatchedParameters)
	{
		watchedParameters.clear();

		for (auto& i : numWatchedParameters)
			watchedParameters.push_back({ i, {} });

		initialised = false;
	}

	/** Call this in the timer callback and it will check whether any parameter has changed. */
	bool checkWatchedParameters()
	{
		auto changed = false;

		if (auto p = dynamic_cast<Component*>(this)->findParentComponentOfClass<ParameterSourceObject>())
		{
			for (auto& v : watchedParameters)
			{
				auto newValue = p->getParameterValue(v.first);
				changed |= v.second.setModValueIfChanged(newValue);
			}
		}

		initialised = true;
		return changed;
	}

	/** Use this to fetch the current parameter value. */
	double getWatchedParameterValue(int parameterIndex) const
	{
		if(!initialised)
			return 0.0;

		for (const auto& v : watchedParameters)
		{
			if (v.first == parameterIndex)
				return v.second.getModValue();
		}

		jassertfalse;
		return 0.0;
	}

private:

	bool initialised = false;
	std::vector<std::pair<int, ModValue>> watchedParameters;
};

template <class T> class ScriptnodeExtraComponent : 
	public ScriptnodeExtraComponentBase,
	public PooledUIUpdater::SimpleTimer
{
public:

	using ObjectType = T;

	ObjectType* getObject() const
	{
		return object.get();
	}

protected:

	ScriptnodeExtraComponent(ObjectType* t, PooledUIUpdater* updater) :
		SimpleTimer(updater),
		object(t)
	{};

	Colour getNodeColour() const
	{
		auto c = findColour(complex_ui_laf::NodeColourId, true);

		if(c.isTransparent())
			return Colour(0xFFAAAAAA);

		return c;
	}

private:

	WeakReference<ObjectType> object;
};



struct simple_visualiser : public ScriptnodeExtraComponent<mothernode>
{
	simple_visualiser(mothernode*, PooledUIUpdater* u);

	double getParameter(int index);

	InvertableParameterRange getParameterRange(int index);

	void timerCallback() override;
	virtual void rebuildPath(Path& path) = 0;
	void paint(Graphics& g) override;

	Path original;
	Path gridPath;
	Path p;

	bool stroke = true;
	bool drawBackground = true;
	float thickness = 1.0f;
};

struct UIFactory
{
	using CreateFunction = std::function<Component*(void*, PooledUIUpdater* u)>;

	UIFactory() {};

	Component* createExtraComponent(ObjectWithValueTree* o, void* unused, PooledUIUpdater* updater)
	{
		if(auto f = getCreateFunction(o->getValueTree()))
		{
			if(auto c = f(unused, updater))
			{
				if(auto ec = dynamic_cast<ScriptnodeExtraComponentBase*>(c))
					ec->initialise(o);

				return c;
			}
		}

		return nullptr;
	}

	CreateFunction getCreateFunction(const ValueTree& v) const
	{
		auto path = v[PropertyIds::FactoryPath].toString();
		
		if(data->registeredFunctions.find(path) != data->registeredFunctions.end())
			return data->registeredFunctions.at(path);

		return {};
	}

private:

	struct Data
	{
		Data();

		template <typename T> void registerComponent(const String& t)
		{
			registeredFunctions[t] = T::createExtraComponent;
		}

		std::map<String, CreateFunction> registeredFunctions;
	};

	SharedResourcePointer<Data> data;

	
};



}