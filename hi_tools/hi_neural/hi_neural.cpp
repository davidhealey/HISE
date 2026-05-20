#define RTNEURAL_DEFAULT_ALIGNMENT 16
#define RTNEURAL_USE_XSIMD 1

#include <cstring>
#include <functional>

#include "CompiledRTNeuralIncludes.h"

namespace hise
{
using namespace juce;

#define DECLARE_ID(x) static const Identifier x(#x)
namespace PytorchIds
{
	// Containers
	DECLARE_ID(Sequential);

	// Layers
	DECLARE_ID(Linear);

	// Activations
	DECLARE_ID(Tanh);
	DECLARE_ID(ReLU);
	DECLARE_ID(Sigmoid);

	DECLARE_ID(in_features);
	DECLARE_ID(out_features);

	struct Helpers
	{
		static std::pair<Identifier, bool> getTypeIdAndIsActivation(RTNeural::Layer<float>* l)
		{
			if(dynamic_cast<RTNeural::Dense<float>*>(l))
				return { Linear, false };
			if(dynamic_cast<RTNeural::TanhActivation<float>*>(l))
				return { Tanh, true };
			if(dynamic_cast<RTNeural::ReLuActivation<float>*>(l))
				return { ReLU, true };
			if(dynamic_cast<RTNeural::SigmoidActivation<float>*>(l))
				return { Sigmoid, true };

			return { {}, false };
		}
	};
}

#undef DECLARE_ID

namespace NeuralJsonHelpers
{
	static Identifier getQualityConfigurationsId()
	{
		static const Identifier id("qualityConfigurations");
		return id;
	}

	static bool isEmptyObject(const var& data)
	{
		if(auto obj = data.getDynamicObject())
			return obj->getProperties().size() == 0;

		return data.isVoid() || data.isUndefined();
	}

	static Result validateQualityConfiguration(const Identifier& qualityId, const var& data)
	{
		if(data.getDynamicObject() == nullptr)
			return Result::fail("Quality configuration \"" + qualityId.toString() + "\" must be an object");

		auto mathProvider = data["mathProvider"].toString();
		if(mathProvider.isEmpty())
			mathProvider = "default";

		if(mathProvider != "default" && mathProvider != "fastMath")
		{
			return Result::fail("Unsupported mathProvider \"" + mathProvider +
				"\" in quality configuration \"" + qualityId.toString() + "\"");
		}

		if(data.hasProperty("sampleRateCorrection"))
		{
			auto sampleRateCorrection = data["sampleRateCorrection"].toString();

			if(sampleRateCorrection != "none" && sampleRateCorrection != "linear")
			{
				return Result::fail("Unsupported sampleRateCorrection \"" + sampleRateCorrection +
					"\" in quality configuration \"" + qualityId.toString() + "\"");
			}
		}

		return Result::ok();
	}

	static Result validateQualityConfigurations(const var& qualityConfigurations)
	{
		if(isEmptyObject(qualityConfigurations))
			return Result::ok();

		auto obj = qualityConfigurations.getDynamicObject();

		if(obj == nullptr)
			return Result::fail("writeCompiledModelJSON() expects an object with quality configurations");

		for(const auto& nv: obj->getProperties())
		{
			if(!Identifier::isValidIdentifier(nv.name.toString()))
				return Result::fail("Invalid quality configuration name: " + nv.name.toString());

			auto r = validateQualityConfiguration(nv.name, nv.value);

			if(r.failed())
				return r;
		}

		return Result::ok();
	}

	static var withQualityConfigurations(const var& sourceData, const var& qualityConfigurations, Result& result)
	{
		result = validateQualityConfigurations(qualityConfigurations);

		if(result.failed())
			return {};

		var target;
		result = JSON::parse(JSON::toString(sourceData, true), target);

		if(result.failed())
			return {};

		auto root = target.getDynamicObject();
		auto hise = target["hise"].getDynamicObject();

		if(root == nullptr || hise == nullptr)
		{
			result = Result::fail("Compiled model JSON is missing the HISE metadata object");
			return {};
		}

		if(isEmptyObject(qualityConfigurations))
		{
			hise->removeProperty(getQualityConfigurationsId());
			return target;
		}

		hise->setProperty(getQualityConfigurationsId(), qualityConfigurations);
		return target;
	}

	static StringArray getQualityConfigurationsFromJSON(const var& sourceData)
	{
		StringArray ids;

		if(auto hise = sourceData["hise"].getDynamicObject())
		{
			if(auto obj = hise->getProperty(getQualityConfigurationsId()).getDynamicObject())
			{
				for(const auto& nv: obj->getProperties())
					ids.add(nv.name.toString());
			}
		}

		if(ids.isEmpty())
			ids.add("default");

		return ids;
	}

	static var createMetadata(const Identifier& id, const String& modelType, const String& sourceFormat, int numInputs, int numOutputs)
	{
		auto obj = new DynamicObject();
		obj->setProperty("format", "HiseCompiledNeuralNetwork");
		obj->setProperty("version", 1);
		obj->setProperty("id", id.toString());
		obj->setProperty("modelType", modelType);
		obj->setProperty("sourceFormat", sourceFormat);
		obj->setProperty("createdBy", "ScriptNeuralNetwork.writeCompiledModelJSON");
		obj->setProperty("numInputs", numInputs);
		obj->setProperty("numOutputs", numOutputs);

		return var(obj);
	}

	static var createRTNeuralJSON(const Identifier& id, const var& sourceData, const String& sourceFormat, int numInputs, int numOutputs)
	{
		auto obj = new DynamicObject();
		obj->setProperty("hise", createMetadata(id, "rtneural", sourceFormat, numInputs, numOutputs));
		obj->setProperty("in_shape", sourceData["in_shape"]);
		obj->setProperty("layers", sourceData["layers"]);

		return var(obj);
	}

	static var createNAMJSON(const Identifier& id, const var& sourceData)
	{
		auto obj = new DynamicObject();
		obj->setProperty("hise", createMetadata(id, "nam", "NAM", 1, 1));
		obj->setProperty("weights", sourceData["weights"]);

		return var(obj);
	}

	static String getFloatBytes(const nlohmann::json& data)
	{
		String s;
		bool first = true;

		auto appendFloat = [&](float v)
		{
			uint32 bits = 0;
			static_assert(sizeof(float) == sizeof(uint32), "Unexpected float size");
			std::memcpy(&bits, &v, sizeof(float));

			for(int i = 0; i < 4; i++)
			{
				if(!first)
					s << ", ";

				first = false;
				auto byteValue = (int)((bits >> (i * 8)) & 0xFF);
				s << "0x";

				if(byteValue < 16)
					s << "0";

				s << String::toHexString(byteValue);
			}
		};

		std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& v)
		{
			if(v.is_array())
			{
				for(const auto& child: v)
					walk(child);
			}
			else if(v.is_number())
			{
				appendFloat(v.get<float>());
			}
		};

		walk(data);
		return s;
	}

	static int getFlatNumElements(const nlohmann::json& data)
	{
		int numElements = 0;

		std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& v)
		{
			if(v.is_array())
			{
				for(const auto& child: v)
					walk(child);
			}
			else if(v.is_number())
			{
				numElements++;
			}
		};

		walk(data);
		return numElements;
	}

	static String getClassName(const String& id, const String& qualityId)
	{
		return id + "_" + qualityId;
	}

	static RTNeural::StaticTypeOptions createStaticTypeOptions(const String& qualityId,
		const nlohmann::json& qualityData)
	{
		RTNeural::StaticTypeOptions options;
		options.qualityId = qualityId.toStdString();
		options.scalarType = "float";
		options.mathProvider = "default";
		options.sampleRateCorrection = "none";

		if(qualityData.is_object())
		{
			if(qualityData.contains("mathProvider"))
			{
				auto mathProvider = String(qualityData["mathProvider"].get<std::string>());
				options.mathProvider = mathProvider == "fastMath" ?
					"project::FastMathsProvider" : "default";
			}

			if(qualityData.contains("sampleRateCorrection"))
				options.sampleRateCorrection = qualityData["sampleRateCorrection"].get<std::string>();
		}

		return options;
	}

	static Result getQualityConfigurations(const nlohmann::json& data,
		std::vector<std::pair<String, nlohmann::json>>& configs)
	{
		if(!data.contains("hise") || !data["hise"].is_object())
			return Result::fail("Compiled neural JSON is missing the hise metadata object");

		const auto& hiseData = data["hise"];

		if(!hiseData.contains("qualityConfigurations"))
		{
			configs.push_back({ "default", nlohmann::json::object() });
			return Result::ok();
		}

		const auto& qualityData = hiseData["qualityConfigurations"];

		if(!qualityData.is_object())
			return Result::fail("hise.qualityConfigurations must be an object");

		for(auto it = qualityData.begin(); it != qualityData.end(); ++it)
		{
			auto qualityId = String(it.key());

			if(!Identifier::isValidIdentifier(qualityId))
				return Result::fail("Invalid quality configuration name: " + qualityId);

			if(!it.value().is_object())
				return Result::fail("Quality configuration \"" + qualityId + "\" must be an object");

			if(it.value().contains("mathProvider"))
			{
				auto mathProvider = String(it.value()["mathProvider"].get<std::string>());

				if(mathProvider != "default" && mathProvider != "fastMath")
					return Result::fail("Unsupported mathProvider in quality configuration \"" + qualityId + "\"");
			}

			if(it.value().contains("sampleRateCorrection"))
			{
				auto src = String(it.value()["sampleRateCorrection"].get<std::string>());

				if(src != "none" && src != "linear")
					return Result::fail("Unsupported sampleRateCorrection in quality configuration \"" + qualityId + "\"");
			}

			configs.push_back({ qualityId, it.value() });
		}

		if(configs.empty())
			configs.push_back({ "default", nlohmann::json::object() });

		return Result::ok();
	}

	static Result writeLayerWeights(String& code, const String& className,
		const nlohmann::json& layer, int layerIndex)
	{
		if(!layer.contains("weights"))
			return Result::fail("Layer " + String(layerIndex) + " is missing weights");

		const auto& weights = layer["weights"];
		auto type = String(layer["type"].get<std::string>());

		for(size_t i = 0; i < weights.size(); i++)
		{
			auto arrayName = className + "_l" + String(layerIndex) + "_w" + String((int)i);
			code << "alignas(16) static const unsigned char " << arrayName << "[] = { ";
			code << getFloatBytes(weights[i]) << " };\n";
			code << "static constexpr int " << arrayName << "NumFloats = ";
			code << getFlatNumElements(weights[i]) << ";\n";
		}

		if(type == "lstm" && weights.size() != 3)
			return Result::fail("LSTM layer " + String(layerIndex) + " must have three weight tensors");

		if((type == "dense" || type == "time-distributed-dense") && weights.size() < 1)
			return Result::fail("Dense layer " + String(layerIndex) + " must have at least one weight tensor");

		return Result::ok();
	}

	static Result writeLoadLayerCall(String& code, const String& namespaceName, const String& className,
		const nlohmann::json& layer, const RTNeural::StaticLayerInfo& info, int layerIndex)
	{
		auto type = String(layer["type"].get<std::string>());
		auto prefix = namespaceName + "::" + className + "_l" + String(layerIndex) + "_w";

		if(type == "lstm")
		{
			code << "\t\tobj.template get<" << String(layerIndex) << ">().setWVals(";
			code << namespaceName << "::loadMatrix(";
			code << prefix << "0, " << prefix << "0NumFloats, " << info.weights[0].shape[0];
			code << ", " << info.weights[0].shape[1] << "));\n";
			code << "\t\tobj.template get<" << String(layerIndex) << ">().setUVals(";
			code << namespaceName << "::loadMatrix(";
			code << prefix << "1, " << prefix << "1NumFloats, " << info.weights[1].shape[0];
			code << ", " << info.weights[1].shape[1] << "));\n";
			code << "\t\t{ auto b = " << namespaceName << "::loadVector(";
			code << prefix << "2, " << prefix << "2NumFloats); ";
			code << "obj.template get<" << String(layerIndex) << ">().setBVals(b); }\n";

			return Result::ok();
		}

		if(type == "dense" || type == "time-distributed-dense")
		{
			code << "\t\tobj.template get<" << String(layerIndex) << ">().setWeights(";
			code << namespaceName << "::loadMatrixTransposed(";
			code << prefix << "0, " << prefix << "0NumFloats, " << info.weights[0].shape[0];
			code << ", " << info.weights[0].shape[1] << "));\n";

			if(layer["weights"].size() > 1)
			{
				code << "\t\t{ auto b = " << namespaceName << "::loadVector(";
				code << prefix << "1, " << prefix << "1NumFloats); ";
				code << "obj.template get<" << String(layerIndex) << ">().setBias(b.data()); }\n";
			}

			return Result::ok();
		}

		return Result::fail("Unsupported static neural layer: " + type);
	}
}

class PytorchParser
{
	struct LayerInfo
	{
		void parseArgs(const Identifier& id, const String& value)
		{
			if(id == PytorchIds::in_features)
				inputs = value.getIntValue();
			if(id == PytorchIds::out_features)
				outputs = value.getIntValue();
		}

		RTNeural::Layer<float>* createLayer() const
		{
			if(type == PytorchIds::Linear)
				return new RTNeural::Dense<float>(inputs, outputs);
			if(type == PytorchIds::Tanh)
				return new RTNeural::TanhActivation<float>(inputs);
			if(type == PytorchIds::ReLU)
				return new RTNeural::ReLuActivation<float>(inputs);
			if(type == PytorchIds::Sigmoid)
				return new RTNeural::SigmoidActivation<float>(inputs);
			
			throw Result::fail("Can't create layer with ID " + type.toString());
		}

		Identifier type;
		String name;
		int inputs = 0;
		int outputs = 0;
		bool isActivationFunction = false;

		var toJSON() const
		{
			DynamicObject* obj = new DynamicObject();

			obj->setProperty("type", type.toString());
			obj->setProperty("name", name);
			obj->setProperty("inputs", inputs);
			obj->setProperty("outputs", outputs);
			obj->setProperty("isActivation", isActivationFunction);

			return var(obj);
		}

		void fromJSON(const var& layerData)
		{
			type = layerData["type"].toString();
			name = layerData["name"];
			inputs = layerData["inputs"];
			outputs = layerData["outputs"];
			isActivationFunction = layerData["isActivation"];
		}
	};

	var toJSON() const
	{
		Array<var> list;

		for(const auto& l: layers)
			list.add(l.toJSON());

		return var(list);
	}

public:

	/** Creates a parser from a previously exported JSON list. */
	explicit PytorchParser(const var& layerList)
	{
		if(auto ar = layerList.getArray())
		{
			for(const auto& l: *ar)
			{
				LayerInfo newInfo;
				newInfo.fromJSON(l);
				layers.add(std::move(newInfo));
			}
		}

	}

	/** Creates a parser from a previously exported JSON list. */
	explicit PytorchParser(const String& modelLayout)
	{
		parseLayers(modelLayout);
	}

	static var createJSONModel(const String& modelLayout)
	{
		PytorchParser p(modelLayout);
		return p.toJSON();
	}

	using ModelPtr = std::unique_ptr<RTNeural::Model<float>>;

	std::pair<int, int> getNumConnections() const
	{
		return { layers.getFirst().inputs, layers.getLast().outputs };
	}

	ModelPtr createModel()
	{
		auto numInputs = layers.getFirst().inputs;

		auto model = std::make_unique<RTNeural::Model<float>>(numInputs);

		for(const auto& l: layers)
		{
			model->addLayer(l.createLayer());
		}

		return model;
	}


	Result loadWeights(const ModelPtr& model, const nlohmann::json& modelJson)
	{
		try
		{
			for(int i = 0; i < layers.size(); i++)
			{
				auto l = layers[i];

				if(l.isActivationFunction)
					continue;

				std::string prefix; prefix.append(l.name.toStdString()); prefix.append(".");

				auto thisLayer = model->layers[i];
				
				if(l.type == PytorchIds::Linear)
				{
					RTNeural::torch_helpers::loadDense<float> (modelJson, prefix, *dynamic_cast<RTNeural::Dense<float>*>(thisLayer));
				}
				else
				{
					throw Result::fail("Unknown type " + l.type.toString());
				}
			}
		}
		catch(Result& r)
		{
			return r;
		}

		return Result::ok();
	}

	/** Creates a JSON representation of all the layers that can be fed into the C++ code generator. */
	static var toJSON(const ModelPtr& model)
	{
		Array<var> list;

		for(auto l: model->layers)
		{
			LayerInfo newInfo;

			newInfo.name = l->getName();
			newInfo.inputs = l->in_size;
			newInfo.outputs = l->out_size;

			auto i2 = PytorchIds::Helpers::getTypeIdAndIsActivation(l);

			newInfo.type = i2.first;
			newInfo.isActivationFunction = i2.second;

			list.add(newInfo.toJSON());
		}

		return var(list);
	}

private:

	void parseLayers(const String& modelLayout)
	{
		auto lines = StringArray::fromLines(modelLayout);

		layers.ensureStorageAllocated(lines.size() - 2);

		String currentSequentialId;

		for(auto l: lines)
		{
			auto t = l.trim();

			if(t.startsWithChar(')') && currentSequentialId.isNotEmpty())
			{
				currentSequentialId = {};
			}

			if(!t.startsWithChar('('))
				continue;

			

			auto t1 = StringArray::fromTokens(t, ":", "");

			LayerInfo info;

			if(currentSequentialId.isNotEmpty())
				info.name << currentSequentialId << ".";

			info.name << t1[0].removeCharacters("()");

			info.type = Identifier(t1[1].upToFirstOccurrenceOf("(", false, false).trim());

			

			auto t2 = t1[1].fromFirstOccurrenceOf("(", false, false).upToLastOccurrenceOf(")", false, false).trim();
			auto t3 = StringArray::fromTokens(t2, ",", "");
			t3.trim();

			for(auto arg: t3)
			{
				auto t4 = StringArray::fromTokens(arg, "=", "");
				auto key = Identifier(t4[0]);
				auto value = t4[1];
				info.parseArgs(key, value);
			}

			if(info.type == PytorchIds::Sequential)
				currentSequentialId = info.name;

			layers.add(std::move(info));
		}

		for(int i = 0; i < layers.size(); i++)
			if(layers[i].type == PytorchIds::Sequential)
				layers.remove(i--);

		for(int i = 1; i < layers.size(); i++)
		{
			auto& ref = layers.getReference(i);

			if(ref.inputs == 0)
			{
				ref.isActivationFunction = true;
				ref.inputs = layers[i-1].outputs;;
				ref.outputs = ref.inputs;
			}
		}
	}

	Array<LayerInfo> layers;
};

struct EmptyModel: public NeuralNetwork::ModelBase
{
	ModelBase* clone() { return new EmptyModel(); }

	void process(const float*, float*) override {};
	void reset() override {};
	int getNumInputs() const override { return 0; }
	int getNumOutputs() const override { return 0; };
	Result loadWeights(const String& jsonData) override
	{
		return Result::fail("network is not initialised");
	}
};

struct TensorFlowModel: public NeuralNetwork::ModelBase
{
	TensorFlowModel(const nlohmann::json& jsonData):
	  modelData(jsonData)
	{
		model = RTNeural::json_parser::parseJson<float>(modelData);
		numInputs = model->getInSize();
		numOutputs = model->getOutSize();
		model->reset();
	}

	TensorFlowModel(const var& obj)
	{
		auto s = JSON::toString(obj, false).toStdString();
		modelData = nlohmann::json::parse(s);
		model = RTNeural::json_parser::parseJson<float>(modelData);

		numInputs = model->getInSize();
		numOutputs = model->getOutSize();
		model->reset();
	}

	ModelBase* clone() { return new TensorFlowModel(modelData); }

	Result loadWeights(const String& jsonData)
	{
		return Result::fail("Tensor Flow models will initialise their weights with the model JSON");
	}

	void reset() final
	{
		 model->reset();
	}

	void process(const float* input, float* output) final
	{
		model->forward(input);
		memcpy(output, model->getOutputs(), sizeof(float) * numOutputs);
	}

	int getNumInputs() const final { return numInputs; }
	int getNumOutputs() const final { return numOutputs; }

	int numInputs = 0;
	int numOutputs = 0;

	PytorchParser::ModelPtr model;

	nlohmann::json modelData;
};

struct DynamicModel: public NeuralNetwork::ModelBase
{
	DynamicModel(const var& modelJSON):
	  p(modelJSON),
	  layoutData(modelJSON)
	{
		auto c = p.getNumConnections();
		numInputs = c.first;
		numOutputs = c.second;
		model = p.createModel();
	}

	DynamicModel* clone()
	{
		auto nt = new DynamicModel(layoutData);
		nt->loadWeightsInternal(weights);
		return nt;
	}

	Result loadWeightsInternal(const nlohmann::json& weights_)
	{
		weights = weights_;
		return p.loadWeights(model, weights);
	}

	Result loadWeights(const String& jsonData) final
	{
		return loadWeightsInternal(nlohmann::json::parse(jsonData.toStdString()));
	}

	void reset() final
	{
		 model->reset();
	}

	void process(const float* input, float* output) final
	{
		model->forward(input);
		memcpy(output, model->getOutputs(), sizeof(float) * numOutputs);
	}

	int getNumInputs() const final { return numInputs; }
	int getNumOutputs() const final { return numOutputs; }

	nlohmann::json weights;

	PytorchParser p;
	PytorchParser::ModelPtr model;

	int numInputs = 0;
	int numOutputs = 0;

	var layoutData;
	String weightData;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicModel);
};


NeuralNetwork::Ptr NeuralNetwork::Holder::getOrCreate(const Identifier& id)
{
	for(auto nn: networks)
		if(id == nn->getId())
			return nn;

	auto nn = new NeuralNetwork(id, getFactory());

	networks.add(nn);
	return Ptr(nn);
}

StringArray NeuralNetwork::Holder::getIdList() const
{
	StringArray sa;

	for(auto nn: networks)
		sa.add(nn->getId().toString());

	return sa;
}

NeuralNetwork::NeuralNetwork(const Identifier& id_, Factory* f):
	id(id_),
    factory(f)
{
    jassert(f != nullptr);
	backendState = f->hasModel(id) ? BackendState::CompiledLinked : BackendState::Empty;

	if(backendState == BackendState::CompiledLinked && !f->hasModel(id, activeQualityConfiguration))
	{
		auto ids = f->getQualityIds(id);

		if(!ids.isEmpty())
			activeQualityConfiguration = Identifier(ids[0]);
	}

	currentModels.add(f->create(id, activeQualityConfiguration));
}

NeuralNetwork::~NeuralNetwork()
{
    SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
    currentModels.clear();
}

NeuralNetwork::Ptr NeuralNetwork::clone(int numNetworks)
{
    auto nn = new NeuralNetwork(getId(), factory);
    
    nn->currentModels.clear();
    
    nn->context = context;
	nn->backendState = backendState;
	nn->activeQualityConfiguration = activeQualityConfiguration;
	nn->compiledModelJSON = compiledModelJSON;
    
    for(int i = 0; i < numNetworks; i++)
        nn->currentModels.add(currentModels.getFirst()->clone());
    
    return nn;
}

var NeuralNetwork::parseModelJSON(const File& modelFile)
{
	return PytorchParser::createJSONModel(modelFile.loadFileAsString());
}

Result NeuralNetwork::createCompiledModelHeader(const File& sourceFile, String& code)
{
	nlohmann::json jsonData;

	try
	{
		jsonData = nlohmann::json::parse(sourceFile.loadFileAsString().toStdString());
	}
	catch(std::exception& e)
	{
		return Result::fail("Could not parse neural JSON file " + sourceFile.getFileName() + ": " + String(e.what()));
	}

	if(!jsonData.contains("hise") || !jsonData["hise"].is_object())
		return Result::fail(sourceFile.getFileName() + " is missing the hise metadata object");

	const auto& hiseData = jsonData["hise"];
	auto modelType = hiseData.value("modelType", std::string());

	if(modelType != "rtneural")
		return Result::fail(sourceFile.getFileName() + " is not an RTNeural compiled model JSON file");

	auto id = sourceFile.getFileNameWithoutExtension();
	auto metadataId = String(hiseData.value("id", std::string()));

	if(metadataId.isNotEmpty() && metadataId != id)
		return Result::fail(sourceFile.getFileName() + " metadata ID does not match its filename");

	if(!Identifier::isValidIdentifier(id))
		return Result::fail("Invalid neural network ID: " + id);

	std::unique_ptr<RTNeural::Model<float>> model;

	try
	{
		model = RTNeural::json_parser::parseJson<float>(jsonData);
	}
	catch(std::exception& e)
	{
		return Result::fail("Could not build dynamic reference model for " + id + ": " + String(e.what()));
	}

	std::vector<std::pair<String, nlohmann::json>> qualityConfigurations;
	auto qualityResult = NeuralJsonHelpers::getQualityConfigurations(jsonData, qualityConfigurations);

	if(qualityResult.failed())
		return qualityResult;

	code.clear();
	code << "#pragma once\n\n";
	code << "#include <cmath>\n";
	code << "#include <cstring>\n";
	code << "#include <vector>\n";
	code << "#include \"JuceHeader.h\"\n";
	code << "#include \"hi_tools/hi_neural/CompiledRTNeuralIncludes.h\"\n";
	code << "#include \"hi_tools/hi_neural/hi_neural.h\"\n";
	code << "\n";
	code << "namespace project\n{\n\n";
	code << "namespace neural_" << id << "\n{\n\n";
	code << "struct FastMathsProvider\n{\n";
	code << "\ttemplate <typename T> static T tanh(T x) { return math_approx::tanh<3>(x); }\n";
	code << "\ttemplate <typename T> static T sigmoid(T x) { return math_approx::sigmoid<3>(x); }\n";
	code << "\ttemplate <typename T> static T exp(T x)\n\t{\n";
	code << "\t\tusing std::exp;\n";
	code << "#if RTNEURAL_USE_XSIMD\n\t\tusing xsimd::exp;\n#endif\n";
	code << "\t\treturn exp(x);\n\t}\n";
	code << "};\n\n";
	code << "static std::vector<float> loadVector(const unsigned char* data, int numFloats)\n{\n";
	code << "\tstatic_assert(sizeof(float) == 4, \"Unexpected float size\");\n";
	code << "\tstd::vector<float> values((size_t)numFloats);\n";
	code << "\tstd::memcpy(values.data(), data, values.size() * sizeof(float));\n";
	code << "\treturn values;\n}\n\n";
	code << "static std::vector<std::vector<float>> loadMatrix(const unsigned char* data, int numFloats, int rows, int cols)\n{\n";
	code << "\tauto flat = loadVector(data, numFloats);\n";
	code << "\tstd::vector<std::vector<float>> m((size_t)rows, std::vector<float>((size_t)cols, 0.0f));\n";
	code << "\tfor(int r = 0; r < rows; r++) for(int c = 0; c < cols; c++) m[(size_t)r][(size_t)c] = flat[(size_t)(r * cols + c)];\n";
	code << "\treturn m;\n}\n\n";
	code << "static std::vector<std::vector<float>> loadMatrixTransposed(const unsigned char* data, int numFloats, int rows, int cols)\n{\n";
	code << "\tauto flat = loadVector(data, numFloats);\n";
	code << "\tstd::vector<std::vector<float>> m((size_t)cols, std::vector<float>((size_t)rows, 0.0f));\n";
	code << "\tfor(int r = 0; r < rows; r++) for(int c = 0; c < cols; c++) m[(size_t)c][(size_t)r] = flat[(size_t)(r * cols + c)];\n";
	code << "\treturn m;\n}\n\n";

	for(const auto& qc: qualityConfigurations)
	{
		auto className = NeuralJsonHelpers::getClassName(id, qc.first);
		auto options = NeuralJsonHelpers::createStaticTypeOptions(qc.first, qc.second);

		if(options.mathProvider == "project::FastMathsProvider")
			options.mathProvider = ("project::neural_" + id + "::FastMathsProvider").toStdString();

		auto info = model->getStaticTypeInfo(options);

		if(!info.supported)
			return Result::fail(id + " cannot be statically compiled: " + String(info.error));

		StringArray layerTypes;

		for(size_t i = 0; i < info.layers.size(); i++)
		{
			layerTypes.add(String(info.layers[i].typeName));

			auto r = NeuralJsonHelpers::writeLayerWeights(code, className, jsonData["layers"][(int)i], (int)i);

			if(r.failed())
				return r;
		}

		for(int i = 0; i < layerTypes.size(); i++)
			code << "using " << className << "_l" << String(i) << "_t = " << layerTypes[i] << ";\n";

		code << "using " << className << "_t = RTNeural::ModelT<float, ";
		code << info.inputSize << ", " << info.outputSize;

		for(int i = 0; i < layerTypes.size(); i++)
			code << ", " << className << "_l" << String(i) << "_t";

		code << ">;\n\n";
	}

	code << "} // namespace neural_" << id << "\n\n";

	for(const auto& qc: qualityConfigurations)
	{
		auto className = NeuralJsonHelpers::getClassName(id, qc.first);
		auto options = NeuralJsonHelpers::createStaticTypeOptions(qc.first, qc.second);

		if(options.mathProvider == "project::FastMathsProvider")
			options.mathProvider = ("project::neural_" + id + "::FastMathsProvider").toStdString();

		auto info = model->getStaticTypeInfo(options);

		code << "struct " << className << ": public hise::NeuralNetwork::ModelBase\n{\n";
		code << "\t" << className << "() { loadBakedWeights(); reset(); }\n";
		code << "\tstatic juce::Identifier getStaticId() { return juce::Identifier(\"" << id << "\"); }\n";
		code << "\tstatic hise::NeuralNetwork::ModelBase* create() { return new " << className << "(); }\n";
		code << "\tvoid reset() override { obj.reset(); }\n";
		code << "\tint getNumInputs() const override { return " << info.inputSize << "; }\n";
		code << "\tint getNumOutputs() const override { return " << info.outputSize << "; }\n";
		code << "\thise::NeuralNetwork::ModelBase* clone() override { return new " << className << "(); }\n";
		code << "\tjuce::Result loadWeights(const juce::String&) override\n\t{\n";
		code << "\t\treturn juce::Result::fail(\"Compiled neural models use baked weights\");\n\t}\n";
		code << "\tvoid process(const float* input, float* output) override\n\t{\n";
		code << "\t\tobj.forward(input);\n";
		code << "\t\tstd::memcpy(output, obj.getOutputs(), sizeof(float) * (size_t)getNumOutputs());\n\t}\n";
		code << "\tvoid loadBakedWeights()\n\t{\n";

		for(size_t i = 0; i < info.layers.size(); i++)
		{
			auto r = NeuralJsonHelpers::writeLoadLayerCall(code, "neural_" + id, className,
				jsonData["layers"][(int)i], info.layers[i], (int)i);

			if(r.failed())
				return r;
		}

		if(options.sampleRateCorrection != "none")
		{
			for(size_t i = 0; i < info.layers.size(); i++)
			{
				if(String(jsonData["layers"][(int)i]["type"].get<std::string>()) == "lstm")
					code << "\t\tobj.template get<" << String((int)i) << ">().prepare(1.0f);\n";
			}
		}

		code << "\t}\n";
		code << "\tneural_" << id << "::" << className << "_t obj;\n";
		code << "};\n\n";
	}

	code << "inline void registerCompiledNeuralNetworks_" << id << "(hise::NeuralNetwork::Factory* f)\n{\n";

	for(const auto& qc: qualityConfigurations)
	{
		auto className = NeuralJsonHelpers::getClassName(id, qc.first);
		code << "\tf->registerModel<" << className << ">(juce::Identifier(\"" << id;
		code << "\"), juce::Identifier(\"" << qc.first << "\"));\n";
	}

	code << "}\n\n";
	code << "} // namespace project\n";

	return Result::ok();
}

Result NeuralNetwork::loadPytorchModel(const var& fullJson)
{
	auto layerData = fullJson["layers"].toString();
	auto weightData = JSON::toString(fullJson["weights"]);

	auto modelJson = PytorchParser::createJSONModel(layerData);

	auto ok = build(modelJson);

	if(!ok.wasOk())
		return ok;

	auto weightsOk = loadWeights(weightData);

	if(weightsOk.wasOk())
	{
		compiledModelJSON = var();
		activeQualityConfiguration = Identifier("default");
		backendState = BackendState::Dynamic;
	}

	return weightsOk;
}



struct NAMModel: public NeuralNetwork::ModelBase
{
	using Dilations = wavenet::Dilations<1, 2, 4, 8, 16, 32, 64, 128, 256, 512>;

	struct NAMMathsProvider
	{
	#if RTNEURAL_USE_EIGEN
	    template <typename Matrix>
	    static auto tanh (const Matrix& x)
	    {
	        // See: math_approx::tanh<3>
	        const auto x_poly = x.array() * (1.0f + 0.183428244899f * x.array().square());
	        return x_poly.array() * (x_poly.array().square() + 1.0f).array().rsqrt();
	    }
	#elif RTNEURAL_USE_XSIMD
	    template <typename T>
	    static T tanh (const T& x)
	    {
	        return math_approx::tanh<3> (x);
	    }
	#endif
	};

	NAMModel(const var& data_):
	  ModelBase(),
	  jsonData(data_)
	{
		auto s = JSON::toString(jsonData, true);
		loadWeights(s);
	};

	void reset() final
	{
		obj.prewarm();
	};

	void process(const float* input, float* output) final
	{
		*output = obj.forward(*input);
	};

	int getNumInputs() const final
	{
		return 1;
	};

	int getNumOutputs() const final
	{
		return 1;
	};

	ModelBase* clone() final
	{
		return new NAMModel(jsonData);
	};

	Result loadWeights(const String& jsonData) final
	{
		nlohmann::json model_json {};
		auto j = nlohmann::json::parse(jsonData.toStdString());

		try
		{
			obj.load_weights(j);
		}
		catch(std::exception& e)
		{
			return Result::fail(e.what());
		}
        
        return Result::ok();
	};

	wavenet::Wavenet_Model<float,
                           1,
                           wavenet::Layer_Array<float, 1, 1, 8, 16, 3, Dilations, false, NAMMathsProvider>,
                           wavenet::Layer_Array<float, 16, 1, 1, 8, 3, Dilations, true, NAMMathsProvider>>

	obj;

	var jsonData;
};

Result NeuralNetwork::loadNAMModel(const var& jsonData)
{
	OwnedArray<ModelBase> nm;

	try
	{
		nm.add(new NAMModel(jsonData));

		for(int i = 1; i < getNumNetworks(); i++)
			nm.add(nm.getFirst()->clone());
	}
	catch(Result& r)
	{
		return r;
	}

	{
		SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
		currentModels.swapWith(nm);
	}

	compiledModelJSON = NeuralJsonHelpers::createNAMJSON(id, jsonData);
	activeQualityConfiguration = Identifier("default");
	backendState = BackendState::Dynamic;
	
	return Result::ok();
}

void NeuralNetwork::clearModel()
{
	OwnedArray<ModelBase> nm;

	for(int i = 0; i < getNumNetworks(); i++)
		nm.add(new EmptyModel());

	{
		SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
		currentModels.swapWith(nm);
	}

	compiledModelJSON = var();
	activeQualityConfiguration = Identifier("default");
	backendState = BackendState::Empty;
}

Result NeuralNetwork::build(const var& modelJSON)
{
	OwnedArray<ModelBase> nm;

	try
	{
		nm.add(new DynamicModel(modelJSON));

		for(int i = 1; i < getNumNetworks(); i++)
			nm.add(nm.getFirst()->clone());
	}
	catch(Result& r)
	{
		return r;
	}

	{
		SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
		currentModels.swapWith(nm);
	}

	compiledModelJSON = var();
	activeQualityConfiguration = Identifier("default");
	backendState = BackendState::Dynamic;
	
	return Result::ok();
}

Result NeuralNetwork::loadWeights(const String& jsonData)
{
	auto ok = Result::ok();

	{
		SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);

		for(auto l: currentModels)
			ok = l->loadWeights(jsonData);
	}
	
	reset(-1);
	return ok;
}

var NeuralNetwork::getModelJSON() const
{
	if(auto d = dynamic_cast<DynamicModel*>(currentModels.getFirst()))
	{
		return PytorchParser::toJSON(d->model);
	}
	if(auto d = dynamic_cast<TensorFlowModel*>(currentModels.getFirst()))
	{
		return PytorchParser::toJSON(d->model);
	}

	return {};
}

var NeuralNetwork::getCompiledModelJSON() const
{
	return compiledModelJSON;
}

Result NeuralNetwork::writeCompiledModelJSON(const File& targetDirectory, const var& qualityConfigurations) const
{
	if(compiledModelJSON.isVoid() || compiledModelJSON.isUndefined())
		return Result::fail("The current neural network cannot be written as compiled model JSON");

	if(!targetDirectory.isDirectory())
	{
		if(!targetDirectory.createDirectory())
			return Result::fail("Can't create target directory: " + targetDirectory.getFullPathName());
	}

	auto targetFile = targetDirectory.getChildFile(id.toString()).withFileExtension("json");
	Result jsonResult = Result::ok();
	auto dataToWrite = NeuralJsonHelpers::withQualityConfigurations(compiledModelJSON, qualityConfigurations, jsonResult);

	if(jsonResult.failed())
		return jsonResult;

	auto text = JSON::toString(dataToWrite, false);

	if(!targetFile.replaceWithText(text))
		return Result::fail("Can't write compiled model JSON: " + targetFile.getFullPathName());

	return Result::ok();
}

String NeuralNetwork::getBackendStateName() const
{
	switch(backendState)
	{
	case BackendState::Empty: return "empty";
	case BackendState::Dynamic: return "dynamic";
	case BackendState::CompiledLinked:
	case BackendState::CompiledDll: return "compiled";
	default: jassertfalse; return "empty";
	}
}

StringArray NeuralNetwork::getQualityConfigurations() const
{
	if(backendState == BackendState::CompiledLinked || backendState == BackendState::CompiledDll)
	{
		auto ids = factory->getQualityIds(id);

		if(ids.isEmpty())
			ids.add("default");

		return ids;
	}

	return NeuralJsonHelpers::getQualityConfigurationsFromJSON(compiledModelJSON);
}

Result NeuralNetwork::setQualityConfiguration(const Identifier& qualityId)
{
	if(backendState == BackendState::Empty)
	{
		return Result::fail("Neural network \"" + id.toString() +
			"\" is not compiled. Quality configurations require a compiled model.");
	}

	if(backendState == BackendState::Dynamic)
	{
		return Result::fail("Neural network \"" + id.toString() +
			"\" is dynamic. Quality configurations only apply to compiled models.");
	}

	if(!factory->hasModel(id, qualityId))
	{
		auto available = factory->getQualityIds(id);
		return Result::fail("Unknown quality configuration \"" + qualityId.toString() +
			"\" for neural network \"" + id.toString() + "\". Available configurations: " +
			available.joinIntoString(", "));
	}

	OwnedArray<ModelBase> newModels;

	newModels.add(factory->create(id, qualityId));

	for(int i = 1; i < getNumNetworks(); i++)
		newModels.add(newModels.getFirst()->clone());

	for(auto m: newModels)
		m->reset();

	{
		SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
		currentModels.swapWith(newModels);
	}

	activeQualityConfiguration = qualityId;
	return Result::ok();
}

void NeuralNetwork::setNumNetworks(int numNetworks, bool forceClone)
{
	if(numNetworks == 0)
		return;

	if(!forceClone && context.fixChannelSize > 0)
		return;

	if(getNumNetworks() != numNetworks)
	{
		OwnedArray<ModelBase> newModels;

		auto toCopy = currentModels.getFirst();

		newModels.ensureStorageAllocated(numNetworks);

		for(int i = 0; i < numNetworks; i++)
		{
			newModels.add(toCopy->clone());
			newModels.getLast()->reset();
		}

		{
			SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
			newModels.swapWith(currentModels);
		}
	}
}

int NeuralNetwork::getNumInputs() const
{
	SimpleReadWriteLock::ScopedReadLock sl(lock);
	return currentModels.getFirst()->getNumInputs();
}

int NeuralNetwork::getNumOutputs() const
{
	SimpleReadWriteLock::ScopedReadLock sl(lock);
	return currentModels.getFirst()->getNumOutputs();
}

void NeuralNetwork::warmup(int networkIndex/*=-1*/, int warmupSize/*=2048*/)
{
	if(warmupSize == 0)
		return;

	SimpleReadWriteLock::ScopedReadLock sl(lock);

	if (networkIndex == -1)
	{
		for (auto m : currentModels)
		{
			for(int i = 0; i < warmupSize; i++)
			{
				float s = 0.0f;
				m->process(&s, &s);
			}
		}
			
	}
	else if (auto cm = currentModels[networkIndex])
	{
		for (int i = 0; i < warmupSize; i++)
		{
			float s = 0.0f;
			cm->process(&s, &s);
		}
	}
}

void NeuralNetwork::reset(int networkIndex)
{
	SimpleReadWriteLock::ScopedReadLock sl(lock);

	if(networkIndex == -1)
	{
		for(auto m: currentModels)
			m->reset();
	}
	else if(auto cm = currentModels[networkIndex])
		cm->reset();
}

void NeuralNetwork::process(int networkIndex, const float* input, float* output)
{
	if(auto sl = SimpleReadWriteLock::ScopedTryReadLock(lock))
	{
		if(auto cm = currentModels[networkIndex])
			cm->process(input, output);
	}
}

Result NeuralNetwork::loadTensorFlowModel(const var& jsonData)
{
	OwnedArray<ModelBase> nt;

	nt.add(new TensorFlowModel(jsonData));

	for(int i = 1; i < getNumNetworks(); i++)
		nt.add(nt.getFirst()->clone());
		

	{
		SimpleReadWriteLock::ScopedMultiWriteLock sl(lock);
		currentModels.swapWith(nt);
	}

	compiledModelJSON = NeuralJsonHelpers::createRTNeuralJSON(id, jsonData, "TensorFlow", getNumInputs(), getNumOutputs());
	activeQualityConfiguration = Identifier("default");
	backendState = BackendState::Dynamic;
	
	return Result::ok();
}

#define RTN_PARSE_MODEL_JSON(x) auto modelJson = nlohmann::json::parse(x.toStdString());
#define RTN_MODEL_ID(x) SN_NODE_ID(#x); static NeuralNetwork::ModelBase* create() { return new x(); }
#define RTN_LOAD_MODEL_LAYER(index, name) RTNeural::torch_helpers::loadDense<float>(modelJson, name, this->obj.get<index>());

template <typename ModelType> struct CompiledModel: public NeuralNetwork::ModelBase
{
	void reset() final { obj.reset(); }

	int getNumInputs() const final { return ModelType::input_size; }
	int getNumOutputs() const final { return ModelType::output_size; }

protected:

	template <int Idx> void loadLayer(const nlohmann::json& modelJson, std::string name)
	{
		name.append(".");
		RTNeural::torch_helpers::loadDense<float>(modelJson, name, this->obj.template get<Idx>());
	}

	void process(const float* input, float* output) final
	{
		this->obj.forward(input);
		memcpy(output, obj.getOutputs(), sizeof(float) * getNumOutputs());
	}

	ModelType obj;
};


#if 0 // Example output of the CPP builder
namespace pimpl
{
using l1_t = RTNeural::DenseT<float, 1, 16>;
using t1_t = RTNeural::ReLuActivationT<float, 16>;
using l2_t = RTNeural::DenseT<float, 16, 4>;
using t2_t = RTNeural::ReLuActivationT<float, 4>;
using l3_t = RTNeural::DenseT<float, 4, 1>;
using MyFunk_t = RTNeural::ModelT<float, 1, 1, l1_t, t1_t, 
                                  l2_t, t2_t, l3_t>;
}

// ===============| Class definition |===============

struct MyFunk: public CompiledModel<pimpl::MyFunk_t>
{
	RTN_MODEL_ID(MyFunk);
	
	Result loadWeights(const String& jsonData) final
	{
		RTN_PARSE_MODEL_JSON(jsonData);
		
		RTN_LOAD_MODEL_LAYER(0, "l1.");
		RTN_LOAD_MODEL_LAYER(2, "l2.");
		RTN_LOAD_MODEL_LAYER(4, "l3.");
		
		return Result::ok();
	};
};
#endif


NeuralNetwork::Factory::Factory()
{
	defaultFunction = [](){ return new EmptyModel(); };
}

bool NeuralNetwork::Factory::hasModel(const Identifier& id) const
{
	for(const auto& entry: registeredModels)
	{
		if(entry.id == id)
			return true;
	}

	return false;
}

bool NeuralNetwork::Factory::hasModel(const Identifier& id, const Identifier& qualityId) const
{
	for(const auto& entry: registeredModels)
	{
		if(entry.id == id && entry.qualityId == qualityId)
			return true;
	}

	return false;
}

StringArray NeuralNetwork::Factory::getQualityIds(const Identifier& id) const
{
	StringArray ids;

	for(const auto& entry: registeredModels)
	{
		if(entry.id == id)
			ids.addIfNotAlreadyThere(entry.qualityId.toString());
	}

	ids.sort(true);
	return ids;
}

NeuralNetwork::ModelBase* NeuralNetwork::Factory::create(const Identifier& id)
{
	return create(id, Identifier("default"));
}

NeuralNetwork::ModelBase* NeuralNetwork::Factory::create(const Identifier& id, const Identifier& qualityId)
{
	for(const auto& entry: registeredModels)
	{
		if(entry.id == id && entry.qualityId == qualityId)
		{
			return entry.create();
		}
	}

	return defaultFunction();
}

NeuralNetwork::CppBuilder::CppBuilder(const Identifier& id_, const var& modelJson):
	id(id_)
{
	if(modelJson.isArray())
		layers = *modelJson.getArray();
}

String NeuralNetwork::CppBuilder::createCppModel() const
{
#if HISE_INCLUDE_SNEX
	HashMap<String, NamespacedIdentifier> layerClasses;
	NamespacedIdentifier rt("RTNeural");
	layerClasses.set("Linear", rt.getChildId("DenseT"));
	layerClasses.set("ReLU", rt.getChildId("ReLuActivationT"));

	using namespace snex::cppgen;
	Base b(Base::OutputType::AddTabs);
	UsingTemplate mt(b, Identifier(id + "_t"), rt.getChildId("ModelT"));
	b.addComment("Type definitions for the model layers", Base::CommentType::FillTo80);
	{
		Namespace n(b, "pimpl", false);

		StringArray list;

		for(const auto& l: layers)
		{
			auto type = l["type"].toString();
			auto name = l["name"].toString();
			auto inputs = (int)l["inputs"];
			auto outputs = (int)l["outputs"];
			auto isActivationFunction = (bool)l["isActivation"];
			auto typeName = name + "_t";

			UsingTemplate ut(b, Identifier(typeName), layerClasses[type]);
			ut << "float";
			ut << inputs;

			if(!isActivationFunction)
				ut << outputs;

			ut.flushIfNot();
			list.add(typeName);
		}

		mt << "float";
		mt << (int)layers.getFirst()["inputs"];
		mt << (int)layers.getLast()["outputs"];

		for(auto& l: list)
			mt << l;

		mt.flushIfNot();
	}

	b.addComment("Class definition", Base::CommentType::FillTo80);
	Struct s(b, id, { NamespacedIdentifier::fromString("CompiledModel<pimpl::" + id.toString() + "_t>") }, {}, true);
	{
		b << String("RTN_MODEL_ID(" + id.toString() + ");");
		b.addEmptyLine();
		b << "Result loadWeights(const String& jsonData) final";
		{
			StatementBlock sb(b, true);
			b << "RTN_PARSE_MODEL_JSON(jsonData);";
			b.addEmptyLine();
			int idx = 0;

			for(const auto& l: layers)
			{
				if(l["isActivation"])
				{
					idx++;
					continue;
				}
					
				String s;
				s << "RTN_LOAD_MODEL_LAYER(" << String(idx) << ", \"" << l["name"].toString() << ".\");";
				b << s;
				idx++;
			}

			b.addEmptyLine();
			b << "return Result::ok();";
		}
	}

	s.flushIfNot();
	return b.toString();
#else
	jassertfalse;
	return {};
#endif
}



}
