/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

#define JUCE_MAKE_STREAMABLE_OBJECT(streamMarker) static char getStreamMarker() { return streamMarker; };
#define JUCE_WRITE_STREAMABLE_OBJECT_MARKERS(output) os.writeByte(getStreamMarker());


/** This base class allows any (reference counted) object to be converted and printed as JSON string.
 *
 *  It also allows serialization of the object (if possible).
 *
 *  In order to use this interface class, subclass your class from this class and override the methods
 *
 *  - writeAsJSON
 *  - writeToStream
 *
 *  Also use the JUCE_MAKE_STREAMABLE_OBJECT macro defined above to add a unique UUID to this object type
 *  (search the codebase for all occurrences of this macro to find a free one.)
 *
 *  In the writeToStream method, use the JUCE_WRITE_STREAMABLE_OBJECT_MARKERS() macro to automatically write
 *  the stream marker at the beginning of your writeToStreamMethod
 *
 *  Then you need to create a static function with the signature
 *
 *  static ObjectWithJSONConverter* createFromStream(InputStream& input);
 *
 *  that will create an object of your type. Note that the input stream is already moved beyond the type marker
 *  and points directly to the beginning of the user data. */
class JUCE_API  ObjectWithJSONConverter
{
public:

	virtual ~ObjectWithJSONConverter() {};

    /** Implement this and write a JSON representation of this object. This is mostly used for the trace() call
     *  in order to create a textual representation of this object. */
    virtual void writeAsJSON (OutputStream&, int indentLevel, bool allOnOneLine, int maximumDecimalPlaces) = 0;

    /** Overwrite this and write the stream marker with JUCE_WRITE_STREAMABLE_OBJECT_MARKERS as well as the data.
     */
    virtual void writeToStream(OutputStream& os) = 0;

    static std::pair<char, ObjectWithJSONConverter*> createFromMarker(InputStream& os);

     template <typename SubClass> static void registerStreamCreatorStatic()
    {
	    static_assert(std::is_base_of<ObjectWithJSONConverter, SubClass>(), "only call this from subclasses");

        auto marker = SubClass::getStreamMarker();

        for(int i = 0; i < 32; i++)
        {
            if(streamCreators[i].first == marker)
                return;

	        if(streamCreators[i].first == 0)
	        {
		        streamCreators[i] = { marker, SubClass::createFromStream};
                return;
	        }
        }
    }

protected:

    template <typename SubClass> void registerStreamCreator(SubClass* obj)
    {
        registerStreamCreatorStatic<SubClass>();
    }

private:

    typedef ObjectWithJSONConverter*(*StreamCreatorFunctions)(InputStream&);
    static std::array<std::pair<char, StreamCreatorFunctions>, 32> streamCreators;
};

// Small helper function that declares two lambdas, nl() and ind() based on the indentLevel and allInOneLine function
#ifndef JSON_DECLARE_NL_IND
#define JSON_DECLARE_NL_IND std::function<void()> nl, ind; ind = [&out, indentLevel](){ for(int i = 0; i < indentLevel; i++) out.writeByte(' '); }; nl = [&out](){ out << NewLine(); }; if(allOnOneLine) { nl = [&out](){ out.writeByte(' '); }; ind = [&out]() {}; };
#endif

//==============================================================================
/**
    Represents a dynamically implemented object.

    This class is primarily intended for wrapping scripting language objects,
    but could be used for other purposes.

    An instance of a DynamicObject can be used to store named properties, and
    by subclassing hasMethod() and invokeMethod(), you can give your object
    methods.

    @tags{Core}
*/
class JUCE_API  DynamicObject  : public ReferenceCountedObject,
								 public ObjectWithJSONConverter
{
public:
    //==============================================================================
    DynamicObject();
    DynamicObject (const DynamicObject&);

    using Ptr = ReferenceCountedObjectPtr<DynamicObject>;

    //==============================================================================
    /** Returns true if the object has a property with this name.
        Note that if the property is actually a method, this will return false.
    */
    virtual bool hasProperty (const Identifier& propertyName) const;

    /** Returns a named property.
        This returns var() if no such property exists.
    */
    virtual const var& getProperty (const Identifier& propertyName) const;

    /** Sets a named property. */
    virtual void setProperty (const Identifier& propertyName, const var& newValue);

    /** Removes a named property. */
    virtual void removeProperty (const Identifier& propertyName);

    //==============================================================================
    /** Checks whether this object has a property with the given name that has a
        value of type NativeFunction.
    */
    virtual bool hasMethod (const Identifier& methodName) const;

    /** Invokes a named method on this object.

        The default implementation looks up the named property, and if it's a method
        call, then it invokes it.
    */
    virtual var invokeMethod (Identifier methodName,
                      const var::NativeFunctionArgs& args);

    /** Adds a method to the class.

        This is basically the same as calling setProperty (methodName, (var::NativeFunction) myFunction), but
        helps to avoid accidentally invoking the wrong type of var constructor. It also makes
        the code easier to read.
    */
    void setMethod (Identifier methodName, var::NativeFunction function);

    //==============================================================================
    /** Removes all properties and methods from the object. */
    void clear();

    /** Returns the NamedValueSet that holds the object's properties. */
    NamedValueSet& getProperties() noexcept;

    /** Returns the NamedValueSet that holds the object's properties. */
    const NamedValueSet& getProperties() const noexcept     { return properties; }

    void swapProperties(NamedValueSet&& other);

    /** Calls var::clone() on all the properties that this object contains. */
    void cloneAllProperties();

    //==============================================================================
    /** Returns a clone of this object.
        The default implementation of this method just returns a new DynamicObject
        with a (deep) copy of all of its properties. Subclasses can override this to
        implement their own custom copy routines.
    */
    virtual DynamicObject::Ptr clone() const;

    //==============================================================================
    /** Writes this object to a text stream in JSON format.
        This method is used by JSON::toString and JSON::writeToStream, and you should
        never need to call it directly, but it's virtual so that custom object types
        can stringify themselves appropriately.
    */
    virtual void writeAsJSON (OutputStream&, const JSON::FormatOptions&);

    void writeAsJSON (OutputStream&, int indentLevel, bool allOnOneLine, int maximumDecimalPlaces) override;

    void writeToStream(OutputStream& os) override;

    static ObjectWithJSONConverter* createFromStream(InputStream& os);

    JUCE_MAKE_STREAMABLE_OBJECT(1);

private:
    /** Derived classes may override this function to take additional actions after
        properties are assigned or removed.

        @param name         the name of the property that changed
        @param value        if non-null, the value of the property after assignment
                            if null, indicates that the property was removed
    */
    virtual void didModifyProperty ([[maybe_unused]] const Identifier& name,
                                    [[maybe_unused]] const std::optional<var>& value) {}

    //==============================================================================
    NamedValueSet properties;

    JUCE_LEAK_DETECTOR (DynamicObject)
};

} // namespace juce
