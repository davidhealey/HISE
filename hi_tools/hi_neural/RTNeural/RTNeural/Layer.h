#ifndef LAYER_H_INCLUDED
#define LAYER_H_INCLUDED

#include <cstddef>
#include <string>
#include <vector>

namespace RTNEURAL_NAMESPACE
{

struct StaticWeightInfo
{
    std::string name;
    std::vector<int> shape;
    size_t flatNumElements = 0;
};

struct StaticTypeOptions
{
    std::string qualityId = "default";
    std::string scalarType = "float";
    std::string mathProvider = "default";
    std::string sampleRateCorrection = "none";

    std::string getMathProviderTypeName() const
    {
        if(mathProvider == "default")
            return "RTNeural::DefaultMathsProvider";

        return mathProvider;
    }

    std::string getSampleRateCorrectionTypeName() const
    {
        if(sampleRateCorrection == "linear")
            return "RTNeural::SampleRateCorrectionMode::LinInterp";

        if(sampleRateCorrection == "noInterp")
            return "RTNeural::SampleRateCorrectionMode::NoInterp";

        return "RTNeural::SampleRateCorrectionMode::None";
    }
};

struct StaticLayerInfo
{
    std::string typeName;
    std::vector<StaticWeightInfo> weights;
    bool isActivation = false;
    bool supported = false;
    std::string error;
};

struct StaticModelInfo
{
    int inputSize = 0;
    int outputSize = 0;
    std::vector<StaticLayerInfo> layers;
    bool supported = false;
    std::string error;
};

/** Virtual base class for a generic neural network layer. */
template <typename T>
class Layer
{
public:
    /** Constructs a layer with given input and output size. */
    Layer(int in_size, int out_size)
        : in_size(in_size)
        , out_size(out_size)
    {
    }

    virtual ~Layer() = default;

    /** Returns the name of this layer. */
    virtual std::string getName() const noexcept { return ""; }

    /** Returns static code generation metadata for this layer. */
    virtual StaticLayerInfo getStaticTypeInfo(const StaticTypeOptions&) const
    {
        StaticLayerInfo info;
        info.error = "Layer \"" + getName() + "\" does not support static generation";
        return info;
    }

    /** Resets the state of this layer. */
    virtual void reset() { }

    /** Implements the forward propagation step for this layer. */
    virtual void forward(const T* input, T* out) noexcept = 0;

    const int in_size;
    const int out_size;
};

} // namespace RTNEURAL_NAMESPACE

#endif // LAYER_H_INCLUDED
