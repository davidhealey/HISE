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
		obj->setProperty("version", sourceData["version"]);
		obj->setProperty("architecture", sourceData["architecture"]);
		obj->setProperty("config", sourceData["config"]);
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
					"hise::CompiledNeuralNetworkHelpers::FastMathsProvider" : "default";
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

#if USE_BACKEND
	static Result writeLayerWeights(snex::cppgen::Base& code, const String& id,
		const nlohmann::json& layer, int layerIndex)
	{
		code.addComment("Layer Weights", snex::cppgen::Base::CommentType::FillTo80Light);

		if(!layer.contains("weights"))
			return Result::fail("Layer " + String(layerIndex) + " is missing weights");

		const auto& weights = layer["weights"];
		auto type = String(layer["type"].get<std::string>());

		for(size_t i = 0; i < weights.size(); i++)
		{
			auto arrayName = id + "_l" + String(layerIndex) + "_w" + String((int)i);
			code << String("alignas(16) static const unsigned char ") + arrayName + "[] = { " +
				getFloatBytes(weights[i]) + " };";
			code << String("static constexpr int ") + arrayName + "NumFloats = " +
				String(getFlatNumElements(weights[i])) + ";";
		}

		if(type == "lstm" && weights.size() != 3)
			return Result::fail("LSTM layer " + String(layerIndex) + " must have three weight tensors");

		if((type == "dense" || type == "time-distributed-dense") && weights.size() < 1)
			return Result::fail("Dense layer " + String(layerIndex) + " must have at least one weight tensor");

		return Result::ok();
	}

	static Result writeLoadLayerCall(snex::cppgen::Base& code, const String& id,
		const nlohmann::json& layer, const RTNeural::StaticLayerInfo& info, int layerIndex)
	{
		auto type = String(layer["type"].get<std::string>());
		auto prefix = "neural_" + id + "::" + id + "_l" + String(layerIndex) + "_w";

		if(type == "lstm")
		{
			code << String("obj.template get<") + String(layerIndex) + ">().setWVals(" +
				"loadMatrix(" + prefix + "0, " + prefix + "0NumFloats, " +
				String(info.weights[0].shape[0]) + ", " + String(info.weights[0].shape[1]) + ")); ";
			code << String("obj.template get<") + String(layerIndex) + ">().setUVals(" +
				"loadMatrix(" + prefix + "1, " + prefix + "1NumFloats, " +
				String(info.weights[1].shape[0]) + ", " + String(info.weights[1].shape[1]) + ")); ";
			code << String("{ auto b = loadVector(") + prefix + "2, " + prefix + "2NumFloats); " +
				"obj.template get<" + String(layerIndex) + ">().setBVals(b); }";

			return Result::ok();
		}

		if(type == "dense" || type == "time-distributed-dense")
		{
			code << String("obj.template get<") + String(layerIndex) + ">().setWeights(" +
				"loadMatrixTransposed(" + prefix + "0, " + prefix + "0NumFloats, " +
				String(info.weights[0].shape[0]) + ", " + String(info.weights[0].shape[1]) + ")); ";

			if(layer["weights"].size() > 1)
			{
				code << String("{ auto b = loadVector(") + prefix + "1, " + prefix + "1NumFloats); " +
					"obj.template get<" + String(layerIndex) + ">().setBias(b.data()); }";
			}

			return Result::ok();
		}

		return Result::fail("Unsupported static neural layer: " + type);
	}
#endif
}

namespace NAMHelpers
{
	struct LayerInfo
	{
		int inputSize = 0;
		int conditionSize = 0;
		int headSize = 0;
		int channels = 0;
		int kernelSize = 0;
		std::vector<int> dilations;
		bool headBias = false;
	};

	struct ModelInfo
	{
		std::vector<LayerInfo> layers;
		int numWeights = 0;
	};

	static String getLayerSignature(const LayerInfo& l)
	{
		String s;
		s << "input=" << l.inputSize << ", condition=" << l.conditionSize;
		s << ", head=" << l.headSize << ", channels=" << l.channels;
		s << ", kernel=" << l.kernelSize << ", dilations=[";

		for(size_t i = 0; i < l.dilations.size(); i++)
		{
			if(i != 0)
				s << ",";

			s << l.dilations[i];
		}

		s << "], head_bias=" << (l.headBias ? "true" : "false");
		return s;
	}

	static String getModelSignature(const ModelInfo& info)
	{
		String s;
		s << info.layers.size() << " WaveNet layer array(s)";

		for(size_t i = 0; i < info.layers.size(); i++)
			s << "; layer " << (int)i << ": " << getLayerSignature(info.layers[i]);

		return s;
	}

	static int getExpectedWeightCount(const ModelInfo& info)
	{
		int total = 0;

		for(const auto& l: info.layers)
		{
			total += l.channels * l.inputSize;
			total += (int)l.dilations.size() * (l.channels * l.channels * l.kernelSize + l.channels +
				l.channels * l.conditionSize + l.channels * l.channels + l.channels);
			total += l.headSize * l.channels;

			if(l.headBias)
				total += l.headSize;
		}

		return total + 1;
	}

	static Result parseLayer(const nlohmann::json& layer, LayerInfo& info, int index)
	{
		auto layerName = "NAM WaveNet layer " + String(index);

		if(!layer.contains("activation") || !layer["activation"].is_string())
			return Result::fail(layerName + " must use a string activation");

		if(layer.value("activation", std::string()) != "Tanh")
			return Result::fail("Unsupported NAM activation in layer " + String(index) + ": " +
				String(layer.value("activation", std::string())));

		if(layer.value("gated", false))
			return Result::fail(layerName + " uses gated=true, which is not supported");

		const char* unsupportedFields[] = { "film_params", "packing", "slimmable", "head_1x1_config", "head1x1",
			"layer_1x1_config", "layer1x1", "bottleneck" };

		for(auto field: unsupportedFields)
		{
			if(layer.contains(field))
				return Result::fail(layerName + " uses unsupported field `" + String(field) + "`");
		}

		if(layer.value("groups_input", 1) != 1)
			return Result::fail(layerName + " uses unsupported groups_input=" + String(layer.value("groups_input", 1)));

		if(layer.value("groups_input_mixin", 1) != 1)
			return Result::fail(layerName + " uses unsupported groups_input_mixin=" + String(layer.value("groups_input_mixin", 1)));

		info.inputSize = layer.value("input_size", 0);
		info.conditionSize = layer.value("condition_size", 0);
		info.channels = layer.value("channels", 0);
		info.kernelSize = layer.value("kernel_size", 0);

		if(layer.contains("head"))
		{
			const auto& head = layer["head"];

			if(!head.is_object())
				return Result::fail(layerName + " head must be an object");

			if(head.value("kernel_size", 1) != 1)
				return Result::fail(layerName + " uses unsupported head.kernel_size=" + String(head.value("kernel_size", 1)));

			info.headSize = head.value("out_channels", 0);
			info.headBias = head.value("bias", false);
		}
		else
		{
			info.headSize = layer.value("head_size", 0);
			info.headBias = layer.value("head_bias", false);
		}

		if(info.inputSize <= 0 || info.conditionSize != 1 || info.headSize <= 0 ||
			info.channels <= 0 || info.kernelSize <= 0)
			return Result::fail("Invalid " + layerName + " dimensions: " + getLayerSignature(info));

		if(!layer.contains("dilations") || !layer["dilations"].is_array() || layer["dilations"].empty())
			return Result::fail(layerName + " is missing dilations");

		for(const auto& d: layer["dilations"])
			info.dilations.push_back(d.get<int>());

		return Result::ok();
	}

	static Result parseModel(const nlohmann::json& data, ModelInfo& info)
	{
		if(data.contains("architecture") && data.value("architecture", std::string()) != "WaveNet")
			return Result::fail("Unsupported NAM architecture `" + String(data.value("architecture", std::string())) +
				"`. Only plain WaveNet models are supported");

		if(!data.contains("config") || !data["config"].is_object())
			return Result::fail("NAM model is missing the config object");

		const auto& config = data["config"];

		if(config.contains("condition_dsp"))
			return Result::fail("Unsupported NAM field `config.condition_dsp`");

		if(config.contains("head") && !config["head"].is_null())
			return Result::fail("Unsupported NAM top-level field `config.head`");

		if(!config.contains("layers") || !config["layers"].is_array() || config["layers"].empty())
			return Result::fail("NAM model is missing config.layers");

		for(size_t i = 0; i < config["layers"].size(); i++)
		{
			LayerInfo l;
			auto r = parseLayer(config["layers"][(int)i], l, (int)i);

			if(r.failed())
				return r;

			if(i != 0 && l.inputSize != info.layers.back().channels)
				return Result::fail("NAM WaveNet layer " + String((int)i) +
					" input_size does not match previous channels. Detected " + getLayerSignature(l));

			if(i != 0 && l.channels != info.layers.back().headSize)
				return Result::fail("NAM WaveNet layer " + String((int)i) +
					" channels do not match previous head size. Detected " + getLayerSignature(l));

			info.layers.push_back(l);
		}

		if(!data.contains("weights") || !data["weights"].is_array())
			return Result::fail("NAM model is missing the flat weights array");

		info.numWeights = (int)data["weights"].size();
		auto expected = getExpectedWeightCount(info);

		if(info.numWeights != expected)
			return Result::fail("NAM weight count mismatch. Expected " + String(expected) + ", got " +
				String(info.numWeights) + " for " + getModelSignature(info));

		return Result::ok();
	}

	static bool hasDilations(const LayerInfo& l, std::initializer_list<int> values)
	{
		if(l.dilations.size() != values.size())
			return false;

		int i = 0;

		for(auto v: values)
		{
			if(l.dilations[(size_t)i++] != v)
				return false;
		}

		return true;
	}

	static bool matchesRuntimeModel1(const ModelInfo& info)
	{
		return info.layers.size() == 2 &&
			info.layers[0].inputSize == 1 && info.layers[0].conditionSize == 1 &&
			info.layers[0].headSize == 8 && info.layers[0].channels == 16 &&
			info.layers[0].kernelSize == 3 && !info.layers[0].headBias &&
			hasDilations(info.layers[0], { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 }) &&
			info.layers[1].inputSize == 16 && info.layers[1].conditionSize == 1 &&
			info.layers[1].headSize == 1 && info.layers[1].channels == 8 &&
			info.layers[1].kernelSize == 3 && info.layers[1].headBias &&
			hasDilations(info.layers[1], { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 });
	}

	static bool matchesRuntimeModel2(const ModelInfo& info)
	{
		return info.layers.size() == 4 &&
			info.layers[0].inputSize == 1 && info.layers[0].headSize == 8 &&
			info.layers[0].channels == 8 && info.layers[0].kernelSize == 6 && !info.layers[0].headBias &&
			hasDilations(info.layers[0], { 1, 3, 9, 27, 81, 243, 729 }) &&
			info.layers[1].inputSize == 8 && info.layers[1].headSize == 8 &&
			info.layers[1].channels == 8 && info.layers[1].kernelSize == 6 && !info.layers[1].headBias &&
			hasDilations(info.layers[1], { 1, 3, 9, 27, 81, 243, 729 }) &&
			info.layers[2].inputSize == 8 && info.layers[2].headSize == 8 &&
			info.layers[2].channels == 8 && info.layers[2].kernelSize == 15 && !info.layers[2].headBias &&
			hasDilations(info.layers[2], { 1, 16 }) &&
			info.layers[3].inputSize == 8 && info.layers[3].headSize == 1 &&
			info.layers[3].channels == 8 && info.layers[3].kernelSize == 6 && info.layers[3].headBias &&
			hasDilations(info.layers[3], { 1, 3, 9, 27, 81, 243, 729 });
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

#if USE_BACKEND
static Result createCompiledNAMModelHeader(const File& sourceFile, const nlohmann::json& jsonData, String& code)
{
	auto id = sourceFile.getFileNameWithoutExtension();

	if(!Identifier::isValidIdentifier(id))
		return Result::fail("Invalid neural network ID: " + id);

	if(jsonData.contains("hise") && jsonData["hise"].is_object())
	{
		const auto& hiseData = jsonData["hise"];
		auto metadataId = String(hiseData.value("id", std::string()));

		if(metadataId.isNotEmpty() && metadataId != id)
			return Result::fail(sourceFile.getFileName() + " metadata ID does not match its filename");

		if(hiseData.value("modelType", std::string()) != "nam")
			return Result::fail(sourceFile.getFileName() + " is not a NAM compiled model JSON file");
	}

	NAMHelpers::ModelInfo info;
	Result validation = Result::ok();

	try
	{
		validation = NAMHelpers::parseModel(jsonData, info);
	}
	catch(std::exception& e)
	{
		return Result::fail("Could not validate NAM model " + sourceFile.getFileName() + ": " + String(e.what()));
	}

	if(validation.failed())
		return Result::fail("Could not compile NAM model " + sourceFile.getFileName() + ": " + validation.getErrorMessage());

	auto className = NeuralJsonHelpers::getClassName(id, "default");

	snex::cppgen::Base b(snex::cppgen::Base::OutputType::AddTabs);
	b.setHeader([]() { return "#pragma once\n\n#include \"JuceHeader.h\"\n#include \"hi_tools/hi_neural/CompiledRTNeuralIncludes.h\""; });
	b.addEmptyLine();

	{
		snex::cppgen::Namespace projectNamespace(b, "project", false);

		{
			snex::cppgen::Namespace neuralNamespace(b, "neural_" + id, false);

			b << String("alignas(16) static const unsigned char ") + id + "_weights[] = { " +
				NeuralJsonHelpers::getFloatBytes(jsonData["weights"]) + " };";
			b << String("static constexpr int ") + id + "_weightsNumFloats = " + String(info.numWeights) + ";";
			b.addEmptyLine();

			for(size_t i = 0; i < info.layers.size(); i++)
			{
				const auto& l = info.layers[i];
				String dilationType = id + "_dilations_" + String((int)i);
				String layerType = id + "_layer_" + String((int)i) + "_t";
				String dilations;

				for(size_t j = 0; j < l.dilations.size(); j++)
				{
					if(j != 0)
						dilations << ", ";

					dilations << l.dilations[j];
				}

				b << String("using ") + dilationType + " = wavenet::Dilations<" + dilations + ">;";
				b << String("using ") + layerType + " = wavenet::Layer_Array<float, " +
					String(l.inputSize) + ", " + String(l.conditionSize) + ", " + String(l.headSize) + ", " +
					String(l.channels) + ", " + String(l.kernelSize) + ", " + dilationType + ", " +
					String(l.headBias ? "true" : "false") + ", hise::CompiledNeuralNetworkHelpers::FastMathsProvider>;";
			}

			b.addEmptyLine();
			b << String("using ") + id + "_t = wavenet::Wavenet_Model<float, 1";

			for(size_t i = 0; i < info.layers.size(); i++)
				b.append(String(", ") + id + "_layer_" + String((int)i) + "_t");

			b.append(">;");
		}

		b << String("struct ") + className + ": public hise::CompiledNeuralNetworkHelpers::ModelBase<1, 1>";

		{
			snex::cppgen::StatementBlock classBlock(b, true);
			b << String("SN_NEURAL_NETWORK_ID(\"") + id + "\", \"default\");";
			b << String("SN_NEURAL_CONSTRUCTOR(") + className + ");";
			b << "void reset() override { obj.prewarm(); }";
			b << "void process(const float* input, float* output) override";

			{
				snex::cppgen::StatementBlock processBlock(b);
				b << "*output = obj.forward(*input);";
			}

			b << "void loadBakedWeights()";

			{
				snex::cppgen::StatementBlock weightBlock(b);
				b << String("loadWavenetWeights(obj, neural_") + id + "::" + id + "_weights, neural_" +
					id + "::" + id + "_weightsNumFloats);";
			}

			b << String("neural_") + id + "::" + id + "_t obj;";
		}

		b.addEmptyLine();
		b << String("inline void registerCompiledNeuralNetworks_") + id + "(hise::NeuralNetwork::Factory* f)";

		{
			snex::cppgen::StatementBlock registerBlock(b);
			b << String("f->registerModel<") + className + ">();";
		}
	}

	code = b.toString();
	return Result::ok();
}
#endif

Result NeuralNetwork::createCompiledModelHeader(const File& sourceFile, String& code)
{
#if USE_BACKEND
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
	{
		if(sourceFile.hasFileExtension("nam"))
			return createCompiledNAMModelHeader(sourceFile, jsonData, code);

		return Result::fail(sourceFile.getFileName() + " is missing the hise metadata object");
	}

	const auto& hiseData = jsonData["hise"];
	auto modelType = hiseData.value("modelType", std::string());

	if(modelType == "nam")
		return createCompiledNAMModelHeader(sourceFile, jsonData, code);

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

	struct GeneratedQualityInfo
	{
		String qualityId;
		String className;
		RTNeural::StaticTypeOptions options;
		RTNeural::StaticModelInfo info;
	};

	std::vector<GeneratedQualityInfo> generatedInfos;

	for(const auto& qc: qualityConfigurations)
	{
		GeneratedQualityInfo generatedInfo;
		generatedInfo.qualityId = qc.first;
		generatedInfo.className = NeuralJsonHelpers::getClassName(id, qc.first);
		generatedInfo.options = NeuralJsonHelpers::createStaticTypeOptions(qc.first, qc.second);
		generatedInfo.info = model->getStaticTypeInfo(generatedInfo.options);

		if(!generatedInfo.info.supported)
			return Result::fail(id + " cannot be statically compiled: " + String(generatedInfo.info.error));

		generatedInfos.push_back(generatedInfo);
	}

	snex::cppgen::Base b(snex::cppgen::Base::OutputType::AddTabs);
	b.setHeader([]() { return "#pragma once\n\n#include \"JuceHeader.h\"\n#include \"hi_tools/hi_neural/CompiledRTNeuralIncludes.h\""; });
	b.addEmptyLine();

	{
		snex::cppgen::Namespace projectNamespace(b, "project", false);

		{
			snex::cppgen::Namespace neuralNamespace(b, "neural_" + id, false);

			for(size_t i = 0; i < jsonData["layers"].size(); i++)
			{
				auto r = NeuralJsonHelpers::writeLayerWeights(b, id, jsonData["layers"][(int)i], (int)i);

				if(r.failed())
					return r;
			}

			for(const auto& generatedInfo: generatedInfos)
			{
				StringArray layerTypes;

				for(size_t i = 0; i < generatedInfo.info.layers.size(); i++)
					layerTypes.add(String(generatedInfo.info.layers[i].typeName));

				for(int i = 0; i < layerTypes.size(); i++)
					b << String("using ") + generatedInfo.className + "_l" + String(i) + "_t = " + layerTypes[i] + ";";

				b << String("using ") + generatedInfo.className + "_t = RTNeural::ModelT<float, ";
				b.append(String(generatedInfo.info.inputSize) + ", " + String(generatedInfo.info.outputSize));

				for(int i = 0; i < layerTypes.size(); i++)
					b.append(String(", ") + generatedInfo.className + "_l" + String(i) + "_t");

				b.append(">;");
				b.addEmptyLine();
			}
		}

		for(const auto& generatedInfo: generatedInfos)
		{
			const auto& className = generatedInfo.className;
			const auto& info = generatedInfo.info;
			const auto& options = generatedInfo.options;

			b.addComment("Neural Network definition", snex::cppgen::Base::CommentType::FillTo80Light);

			b << String("struct ") + className + ": public hise::CompiledNeuralNetworkHelpers::ModelBase<";
			b.append(String(info.inputSize) + ", " + String(info.outputSize) + ">");

			{
				snex::cppgen::StatementBlock classBlock(b, true);
				b << String("SN_NEURAL_NETWORK_ID(\"") + id + "\", \"" + generatedInfo.qualityId + "\");";
				b << String("SN_NEURAL_CONSTRUCTOR(") + className + ");";
				b << String("SN_DEFAULT_RESET(") + className + ");";
				b << "void process(const float* input, float* output) override";

				{
					snex::cppgen::StatementBlock processBlock(b);
					b << "obj.forward(input);";
					b << "std::memcpy(output, obj.getOutputs(), sizeof(float) * (size_t)getNumOutputs());";
				}

				b << "void loadBakedWeights()";

				{
					snex::cppgen::StatementBlock weightBlock(b);

					for(size_t i = 0; i < info.layers.size(); i++)
					{
						auto r = NeuralJsonHelpers::writeLoadLayerCall(b, id,
							jsonData["layers"][(int)i], info.layers[i], (int)i);

						if(r.failed())
							return r;
					}

					if(options.sampleRateCorrection != "none")
					{
						for(size_t i = 0; i < info.layers.size(); i++)
						{
							if(String(jsonData["layers"][(int)i]["type"].get<std::string>()) == "lstm")
								b << String("obj.template get<") + String((int)i) + ">().prepare(1.0f);";
						}
					}
				}

				b << String("neural_") + id + "::" + className + "_t obj;";
			}

			b.addEmptyLine();
		}

		b.addComment("Factory registration", snex::cppgen::Base::CommentType::FillTo80Light);

		b << String("inline void registerCompiledNeuralNetworks_") + id + "(hise::NeuralNetwork::Factory* f)";

		{
			snex::cppgen::StatementBlock registerBlock(b);

			for(const auto& generatedInfo: generatedInfos)
				b << String("f->registerModel<") + generatedInfo.className + ">();";
		}
	}

	code = b.toString();
#endif

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



struct NAMMathsProvider
{
	#if RTNEURAL_USE_EIGEN
	template <typename Matrix> static auto tanh(const Matrix& x)
	{
		// See: math_approx::tanh<3>
		const auto x_poly = x.array() * (1.0f + 0.183428244899f * x.array().square());
		return x_poly.array() * (x_poly.array().square() + 1.0f).array().rsqrt();
	}
	#elif RTNEURAL_USE_XSIMD
	template <typename T> static T tanh(const T& x)
	{
		return math_approx::tanh<3>(x);
	}
	#endif
};

using NAMRuntimeDilations10 = wavenet::Dilations<1, 2, 4, 8, 16, 32, 64, 128, 256, 512>;
using NAMRuntimeDilations7 = wavenet::Dilations<1, 3, 9, 27, 81, 243, 729>;
using NAMRuntimeDilations2 = wavenet::Dilations<1, 16>;

using NAMRuntimeModel1 = wavenet::Wavenet_Model<float,
	1,
	wavenet::Layer_Array<float, 1, 1, 8, 16, 3, NAMRuntimeDilations10, false, NAMMathsProvider>,
	wavenet::Layer_Array<float, 16, 1, 1, 8, 3, NAMRuntimeDilations10, true, NAMMathsProvider>>;

using NAMRuntimeModel2 = wavenet::Wavenet_Model<float,
	1,
	wavenet::Layer_Array<float, 1, 1, 8, 8, 6, NAMRuntimeDilations7, false, NAMMathsProvider>,
	wavenet::Layer_Array<float, 8, 1, 8, 8, 6, NAMRuntimeDilations7, false, NAMMathsProvider>,
	wavenet::Layer_Array<float, 8, 1, 8, 8, 15, NAMRuntimeDilations2, false, NAMMathsProvider>,
	wavenet::Layer_Array<float, 8, 1, 1, 8, 6, NAMRuntimeDilations7, true, NAMMathsProvider>>;

template <typename ModelType> struct NAMModel: public NeuralNetwork::ModelBase
{
	NAMModel(const var& data_):
		ModelBase(),
		jsonData(data_)
	{
		auto s = JSON::toString(jsonData, true);
		loadWeights(s);
	}

	void reset() final
	{
		obj.prewarm();
	}

	void process(const float* input, float* output) final
	{
		*output = obj.forward(*input);
	}

	int getNumInputs() const final
	{
		return 1;
	}

	int getNumOutputs() const final
	{
		return 1;
	}

	ModelBase* clone() final
	{
		return new NAMModel(jsonData);
	}

	Result loadWeights(const String& jsonData) final
	{
		try
		{
			auto j = nlohmann::json::parse(jsonData.toStdString());
			obj.load_weights(j);
		}
		catch(std::exception& e)
		{
			return Result::fail(e.what());
		}
        
		return Result::ok();
	}

	ModelType obj;

	var jsonData;
};

Result NeuralNetwork::loadNAMModel(const var& jsonData)
{
	OwnedArray<ModelBase> nm;
	nlohmann::json parsedJson;
	NAMHelpers::ModelInfo info;

	try
	{
		parsedJson = nlohmann::json::parse(JSON::toString(jsonData, true).toStdString());
	}
	catch(std::exception& e)
	{
		return Result::fail("Could not parse NAM model JSON: " + String(e.what()));
	}

	Result validation = Result::ok();

	try
	{
		validation = NAMHelpers::parseModel(parsedJson, info);
	}
	catch(std::exception& e)
	{
		return Result::fail("Could not validate NAM model JSON: " + String(e.what()));
	}

	if(validation.failed())
		return Result::fail("Could not load NAM model: " + validation.getErrorMessage());

	if(NAMHelpers::matchesRuntimeModel1(info))
		nm.add(new NAMModel<NAMRuntimeModel1>(jsonData));
	else if(NAMHelpers::matchesRuntimeModel2(info))
		nm.add(new NAMModel<NAMRuntimeModel2>(jsonData));
	else
		return Result::fail("Unsupported runtime NAM WaveNet layout: " + NAMHelpers::getModelSignature(info));

	try
	{
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
