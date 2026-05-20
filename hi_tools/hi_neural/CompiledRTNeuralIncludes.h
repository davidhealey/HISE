#pragma once

#include <cmath>
#include <cstring>
#include <vector>

#ifndef RTNEURAL_DEFAULT_ALIGNMENT
#define RTNEURAL_DEFAULT_ALIGNMENT 16
#endif

#ifndef RTNEURAL_USE_XSIMD
#define RTNEURAL_USE_XSIMD 1
#endif

#include "RTNeural/modules/xsimd/xsimd.hpp"
#include "RTNeural/RTNeural/RTNeural.h"
#include "RTNeural/modules/math_approx/math_approx.hpp"
#include "RTNeural/RTNeural/wavenet/wavenet_model.hpp"
#include "hi_neural.h"

#define SN_NEURAL_NETWORK_ID(id, config) SN_NODE_ID(id); static Identifier getConfigId() { return config; }

#define SN_NEURAL_CONSTRUCTOR(ClassName) ClassName() { loadBakedWeights(); reset(); }\
	static hise::NeuralNetwork::ModelBase* create() { return new ClassName(); } \
	hise::NeuralNetwork::ModelBase* clone() override { return new ClassName(); }

namespace hise {

	struct CompiledNeuralNetworkHelpers
	{
		struct FastMathsProvider
		{
		#if RTNEURAL_USE_EIGEN
			template <typename Matrix> static auto tanh(const Matrix& x)
			{
				const auto x_poly = x.array() * (1.0f + 0.183428244899f * x.array().square());
				return x_poly.array() * (x_poly.array().square() + 1.0f).array().rsqrt();
			}
		#else
			template <typename T> static T tanh(T x) { return math_approx::tanh<3>(x); }
		#endif
			template <typename T> static T sigmoid(T x) { return math_approx::sigmoid<3>(x); }
			template <typename T> static T exp(T x)
			{
				using std::exp;
#if RTNEURAL_USE_XSIMD
				using xsimd::exp;
#endif
				return exp(x);
			}
		};

		template <int NumInputs, int NumOutputs> struct ModelBase : public NeuralNetwork::ModelBase
		{
			int getNumInputs() const override { return NumInputs; }
			int getNumOutputs() const override { return NumOutputs; }

			static Identifier getConfigId() { return "default"; }

			juce::Result loadWeights(const juce::String&) override
			{
				return juce::Result::fail("Compiled neural models use baked weights");
			}

			static std::vector<float> loadVector(const unsigned char* data, int numFloats)
			{
				static_assert(sizeof(float) == 4, "Unexpected float size");
				std::vector<float> values((size_t)numFloats);
				std::memcpy(values.data(), data, values.size() * sizeof(float));
				return values;
			}

			static std::vector<std::vector<float>> loadMatrix(const unsigned char* data, int numFloats, int rows, int cols)
			{
				auto flat = loadVector(data, numFloats);
				std::vector<std::vector<float>> m((size_t)rows, std::vector<float>((size_t)cols, 0.0f));
				for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) m[(size_t)r][(size_t)c] = flat[(size_t)(r * cols + c)];
				return m;
			}

			static std::vector<std::vector<float>> loadMatrixTransposed(const unsigned char* data, int numFloats, int rows, int cols)
			{
				auto flat = loadVector(data, numFloats);
				std::vector<std::vector<float>> m((size_t)cols, std::vector<float>((size_t)rows, 0.0f));
				for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) m[(size_t)c][(size_t)r] = flat[(size_t)(r * cols + c)];
				return m;
			}

			template <typename WavenetType> static void loadWavenetWeights(WavenetType& obj,
				const unsigned char* data, int numFloats)
			{
				auto weights = loadVector(data, numFloats);
				auto it = weights.begin();

				RTNeural::modelt_detail::forEachInTuple(
					[&it](auto& layer, size_t)
					{
						layer.load_weights(it);
					},
					obj.layer_arrays);

				obj.head_scale = *it++;
				jassert((int)std::distance(weights.begin(), it) == (int)weights.size());
			}
		};
	};

}
