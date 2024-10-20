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

namespace hise {
using namespace juce;


namespace HiseModuleDatabase
{
	static constexpr char moduleWildcard[] = "/hise-modules";

	class CommonData
	{
	protected:

		struct Data
		{
			Data()
			{
				
			}

			~Data()
			{
                bp->setAllowFlakyThreading(true);
				allProcessors.clear();
                bp->setAllowFlakyThreading(false);
			}

			void createAllProcessors();
			void addFromFactory(FactoryType* f);

			BackendProcessor* bp = nullptr;
			OwnedArray<Processor> allProcessors;

			struct CachedImage
			{
				MarkdownLink url;
				Image cachedImage;
			};

			Array<CachedImage> cachedImage;
		};

		SharedResourcePointer<Data> data;

		String getProcessorIdFromURL(const MarkdownLink& url);
		Processor* getProcessorForURL(const MarkdownLink& url);
	};

	class ItemGenerator : public hise::MarkdownDataBase::ItemGeneratorBase,
		public CommonData
	{
	public:

		ItemGenerator(File root_, BackendProcessor& bp) :
			ItemGeneratorBase(root_)
		{
			data->bp = &bp;
			colour = Colour(0xFF1088CC);
		}

		~ItemGenerator()
		{

		}

		MarkdownDataBase::Item createRootItem(MarkdownDataBase& parent) override;

	private:

		MarkdownDataBase::Item createItemForProcessor(Processor* p, const MarkdownDataBase::Item& parent);
		MarkdownDataBase::Item createItemForFactory(FactoryType* owned, const String& factoryName, MarkdownDataBase::Item& parent);

		MarkdownDataBase::Item createItemForCategory(const String& categoryName, const MarkdownDataBase::Item& parent);

	};

	class Resolver : public MarkdownParser::LinkResolver,
		public CommonData
	{
	public:

		Resolver(File root_);;

		MarkdownParser::ResolveType getPriority() const override { return MarkdownParser::ResolveType::Autogenerated; }
		Identifier getId() const override { RETURN_STATIC_IDENTIFIER("ModuleDescription"); }
		LinkResolver* clone(MarkdownParser* ) const override { return new Resolver(root); }

		String getContent(const MarkdownLink& url) override;

		File root;
	};

	class ScreenshotProvider : public MarkdownParser::ImageProvider,
		public CommonData
	{
	public:

		ScreenshotProvider(MarkdownParser* parent, BackendProcessor* bp_);
		~ScreenshotProvider();;

		MarkdownParser::ResolveType getPriority() const override { return MarkdownParser::ResolveType::Autogenerated; }
		Identifier getId() const override { RETURN_STATIC_IDENTIFIER("ModuleScreenshotProvider"); }
		ImageProvider* clone(MarkdownParser* newParent) const override { return new ScreenshotProvider(newParent, bp); }

		Image getImage(const MarkdownLink& url, float width) override;

		Component::SafePointer<BackendRootWindow> w;
		BackendProcessor* bp;
	};
};

}

namespace scriptnode
{

namespace doc
{

using namespace juce;
using namespace hise;

struct CommonData
{
	static constexpr uint32 colour = 0xFFF15761;

	ScopedPointer<SineSynth> sine;
	WeakReference<DspNetwork> network;
	WeakReference<JavascriptMasterEffect> effect;
};

struct Base
{
    static String getWildcard()
    {
        const static String scriptnodeWildcard = "/scriptnode";
        return scriptnodeWildcard;
    }
    
	
	SharedResourcePointer<CommonData> data;
};

struct ItemGenerator : public MarkdownDataBase::ItemGeneratorBase,
					   public Base
{

	ItemGenerator(File root, BackendProcessor& bp);
	MarkdownDataBase::Item createRootItem(MarkdownDataBase& parent) override;
	void addNodeFactoryItem(ValueTree factoryTree, MarkdownDataBase::Item& root);
	void addNodeItem(ValueTree nodeTree, MarkdownDataBase::Item& factory);

	SharedResourcePointer<CommonData> data;
};

struct Resolver : public MarkdownParser::LinkResolver,
					  public Base
{
	Resolver(File root_);;

	MarkdownParser::ResolveType getPriority() const override { return MarkdownParser::ResolveType::Autogenerated; }
	Identifier getId() const override { RETURN_STATIC_IDENTIFIER("ScriptNodeDoc"); }
	LinkResolver* clone(MarkdownParser*) const override { return new Resolver(root); }

	String getContent(const MarkdownLink& url) override;

	bool inlineDocMode = false;

	MarkdownLink rootUrl;
	File root;
};

struct ScreenshotProvider : public MarkdownParser::ImageProvider,
							public Base
{
	ScreenshotProvider(MarkdownParser* parent): ImageProvider(parent) {};
	MarkdownParser::ResolveType getPriority() const override { return MarkdownParser::ResolveType::Autogenerated; }
	Identifier getId() const override { RETURN_STATIC_IDENTIFIER("ScriptnodeScreenshotProvider"); }
	ImageProvider* clone(MarkdownParser* newParent) const override { return new ScreenshotProvider(newParent); }

	Image getImage(const MarkdownLink& url, float width) override;
};

}

}
