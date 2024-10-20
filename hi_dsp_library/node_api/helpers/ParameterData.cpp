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
 *   which also must be licenced for commercial applications:
 *
 *   http://www.juce.com
 *
 *   ===========================================================================
 */

#pragma once

namespace scriptnode
{
using namespace juce;
using namespace hise;




bool RangeHelpers::isRangeId(const Identifier& id)
{
	using namespace PropertyIds;
	return id == MinValue || id == MaxValue || id == StepSize || id == SkewFactor;
}


bool RangeHelpers::isBypassIdentity(InvertableParameterRange d)
{
	if (d.rng.start == 0.5 && d.rng.end == 1.0 && d.rng.skew == 1.0 && !d.inv)
		return true;

	return false;
}

bool RangeHelpers::isIdentity(InvertableParameterRange d)
{
	if (d.rng.start == 0.0 && d.rng.end == 1.0 && d.rng.skew == 1.0 && !d.inv)
		return true;

	return false;
}


void RangeHelpers::removeRangeProperties(ValueTree v, UndoManager* um)
{
	for (auto id : getRangeIds(false))
		v.removeProperty(id, um);

	v.removeProperty(Identifier("Enabled"), um);
}

juce::Array<juce::Identifier> RangeHelpers::getRangeIds(bool includeValue)
{
	using namespace PropertyIds;

	if(includeValue)
		return { MinValue, MaxValue, StepSize, SkewFactor, Value};
	else
		return { MinValue, MaxValue, StepSize, SkewFactor };
}


bool RangeHelpers::isInverted(const ValueTree& v)
{
    if (!v.isValid())
		return false;

	using namespace PropertyIds;
    
    if (v.getType() == Connection || v.getType() == ModulationTarget)
    {
        // this must not happen (the connection does not have a range anymore, but just take the
        // target's parameter range
        jassertfalse;
    }
    
	auto max = (double)v[MaxValue];
	auto min = (double)v[MinValue];

	return min > max;
}

void RangeHelpers::storeDoubleRange(ValueTree& d, InvertableParameterRange r, UndoManager* um)
{
	using namespace PropertyIds;
    
    if (d.getType() == Connection || d.getType() == ModulationTarget)
    {
        // this must not happen (the connection does not have a range anymore, but just take the
        // target's parameter range
        jassertfalse;
    }

	d.setProperty(r.inv ? MaxValue : MinValue, r.rng.start, um);
	d.setProperty(r.inv ? MinValue : MaxValue, r.rng.end, um);
	d.setProperty(StepSize, r.rng.interval, um);
	d.setProperty(SkewFactor, r.rng.skew, um);
}

bool RangeHelpers::equalsWithError(const InvertableParameterRange& r1, const InvertableParameterRange& r2, double maxError)
{
	if (isEqual(r1, r2))
		return true;

    if(r1.inv != r2.inv)
        return false;
    
	auto startError = hmath::abs(r1.getRange().getStart() - r2.getRange().getStart());
	auto endError = hmath::abs(r1.getRange().getEnd() - r2.getRange().getEnd());
	auto skewError = hmath::abs(r1.rng.skew - r2.rng.skew);
	auto intervalError = hmath::abs(r1.rng.interval - r2.rng.interval);

	auto thisError = jmax(startError, endError, skewError, intervalError);

	return thisError < hmath::abs(maxError);

}

scriptnode::InvertableParameterRange RangeHelpers::getDoubleRange(const ValueTree& t)
{
	using namespace PropertyIds;

	InvertableParameterRange r;

	if (t.getType() == Connection || t.getType() == ModulationTarget)
	{
		// this must not happen (the connection does not have a range anymore, but just take the 
		// target's parameter range
		jassertfalse;
	}

	auto minValue = (double)t.getProperty(MinValue, 0.0);
	auto maxValue = (double)t.getProperty(MaxValue, 1.0);

	if (minValue == maxValue)
		maxValue += 0.01;

	if (minValue > maxValue)
	{
		std::swap(minValue, maxValue);
		r.inv = true;
	}
	
	r.rng.start = minValue;
	r.rng.end = maxValue;
	r.rng.interval = jlimit(0.0, 1.0, (double)PropertyIds::Helpers::getWithDefault(t, PropertyIds::StepSize));
	r.rng.skew = jlimit(0.001, 100.0, (double)PropertyIds::Helpers::getWithDefault(t, PropertyIds::SkewFactor));

	return r;
}

namespace parameter
{

	data::data(const String& id_)
	{
		info.setId(id_);
		info.setRange({ 0.0, 1.0 });
		info.ok = id_.isNotEmpty();
	}

	data::data(const String& id_, InvertableParameterRange r)
	{
		info.setId(id_);
		info.setRange(r);
		info.ok = id_.isNotEmpty();
	}

	data::data()
	{

	}

	juce::ValueTree data::createValueTree() const
	{
		ValueTree p(PropertyIds::Parameter);

		RangeHelpers::storeDoubleRange(p, info.toRange(), nullptr);

		p.setProperty(PropertyIds::ID, info.getId(), nullptr);
		p.setProperty(PropertyIds::Value, info.defaultValue, nullptr);

		return p;
	}

	data data::withRange(InvertableParameterRange r)
	{
		data copy(*this);
		copy.info.setRange(r);
		return copy;
	}

	void data::setParameterValueNames(const StringArray& valueNames)
	{
		parameterNames = valueNames;

		if (valueNames.size() > 1)
			setRange({ 0.0, (double)valueNames.size() - 1.0, 1.0 });
	}

	void data::init()
	{
		callback(info.defaultValue);
	}

	dynamic::dynamic() :
		f(nullptr),
		obj(nullptr)
	{

	}

	void dynamic::call(double v) const
	{
		if (f != nullptr && obj != nullptr)
			f(getObjectPtr(), v);
	}

	void dynamic::referTo(void* p, Function f_)
	{
		f = f_;
		obj = p;
	}

	void dynamic::operator()(double v) const
	{
		call(v);
	}

	void* dynamic::getObjectPtr() const
	{
		return obj;
	}



	encoder::encoder(ValueTree& v)
	{
		for (auto c : v)
			items.add(pod(c));
	}

	encoder::encoder(const MemoryBlock& m)
	{
		parseItems(m);
	}

	void encoder::parseItems(const MemoryBlock& mb)
	{
		MemoryInputStream mis(mb, true);

		while (!mis.isExhausted())
		{
			pod item(mis);

			if (item.ok)
				items.add(item);
		}
	}

	MemoryBlock encoder::writeItems()
	{
		MemoryBlock data;

		MemoryOutputStream mos(data, false);

		for (auto& d : items)
			d.writeToStream(mos);

		int numToPad = 4 - mos.getPosition() % 4;

		for (int i = 0; i < numToPad; i++)
			mos.writeByte(0);

		mos.flush();

		return data;
	}

	pod::pod(const ValueTree& v)
	{
		clearParameterName();

		index = v.getParent().indexOf(v);
		ok = setId(v[PropertyIds::ID].toString());

		auto range = RangeHelpers::getDoubleRange(v);
		min = (DataType)range.rng.start;
		max = (DataType)range.rng.end;
		inverted = range.inv;
		skew = (DataType)range.rng.skew;
		interval = (DataType)range.rng.interval;
		defaultValue = (DataType)v[PropertyIds::Value];
	}

	pod::pod(MemoryInputStream& mis)
	{
		clearParameterName();

		auto safe = mis.readByte();

		if (safe == 91)
		{
			ok = true;
			index = mis.readInt();
			auto s = mis.readString();

			ok = setId(s);

			min = mis.readFloat();
			max = mis.readFloat();
			defaultValue = mis.readFloat();
			skew = mis.readFloat();
			interval = mis.readFloat();
		}
	}

	String pod::toString()
	{
		String s;
		String nl;

		s << "index: " << index << nl;
		s << "id: " << parameterName << nl;
		s << "min: " << min << nl;
		s << "max: " << max << nl;

		return s;
	}

	void pod::setRange(const InvertableParameterRange& r)
	{
		min =	   r.rng.start;
		max =	   r.rng.end;
		interval = r.rng.interval;
		skew =	   r.rng.skew;
		inverted = r.inv;
	}

	scriptnode::InvertableParameterRange pod::toRange() const
	{
		InvertableParameterRange r;
		r.rng.start = min;
		r.rng.end = max;
		r.rng.skew = skew;
		r.rng.interval = interval;
		r.inv = inverted;

		return r;
	}

	bool pod::setId(const String& id)
	{
		if (id.isNotEmpty() && isPositiveAndBelow(id.length(), MaxParameterNameLength))
		{
			memcpy(parameterName, id.getCharPointer().getAddress(), id.length());
			return true;
		}
		else
			clearParameterName();

		return false;
	}

	void pod::writeToStream(MemoryOutputStream& b)
	{
		b.writeByte(91);
		b.writeInt(index);

		String id(parameterName);

		b.writeString(id);
		b.writeFloat(min);
		b.writeFloat(max);
		b.writeFloat(defaultValue);
		b.writeFloat(skew);
		b.writeFloat(interval);
	}


}

InvertableParameterRange::InvertableParameterRange(const ValueTree& v)
{
	*this = RangeHelpers::getDoubleRange(v);
}

double InvertableParameterRange::convertFrom0to1(double input, bool applyInversion) const
{
	if (isIdentity)
		return input;

    auto inversionFactor = (double)(int)(applyInversion && inv);
    
    // branchless like a pro...
    input = input * (1.0 - inversionFactor) + (1.0 - input) * inversionFactor;
    
	if (rng.skew != 1.0)
		return ranges::RangeBase<double>::from0To1Skew(rng.start, rng.end, rng.skew, input);
	else if (rng.interval != 0.0)
		return ranges::RangeBase<double>::from0To1Step(rng.start, rng.end, rng.interval, input);
	else
		return ranges::RangeBase<double>::from0To1(rng.start, rng.end, input);
}

double InvertableParameterRange::convertTo0to1(double input, bool applyInversion) const
{
	if (isIdentity)
		return input;

    double v;
    
	if (rng.skew != 1.0)
		v = ranges::RangeBase<double>::to0To1Skew(rng.start, rng.end, rng.skew, input);
	else if (rng.interval != 0.0)
		v = ranges::RangeBase<double>::to0To1Step(rng.start, rng.end, rng.interval, input);
	else
		v = ranges::RangeBase<double>::to0To1(rng.start, rng.end, input);
    
    auto inversionFactor = (double)(int)(applyInversion && inv);
    
    // branchless like a pro...
    v = v * (1.0 - inversionFactor) + (1.0 - v) * inversionFactor;
    
    return v;
}

bool InvertableParameterRange::operator==(const InvertableParameterRange& other) const
{
	return RangeHelpers::isEqual(*this, other);
}

void InvertableParameterRange::checkIfIdentity()
{
	isIdentity = RangeHelpers::isIdentity(*this);
}

void InvertableParameterRange::store(ValueTree& v, UndoManager* um)
{
	RangeHelpers::storeDoubleRange(v, *this, um);
}

}
