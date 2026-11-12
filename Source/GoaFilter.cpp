#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// GoaFilter: resonanter Sweep-Filter für Goa/Psytrance
// Tempo-synchroner LFO (5 Formen, Stereo-Versatz), Envelope-Follower, Drive, LP12/LP24/BP/HP
class GoaFilter : public juce::AudioProcessor
{
public:
    GoaFilter()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", createLayout())
    {
        ftype  = apvts.getRawParameterValue("ftype");
        cutoff = apvts.getRawParameterValue("cutoff");
        res    = apvts.getRawParameterValue("res");
        drive  = apvts.getRawParameterValue("drive");
        rate   = apvts.getRawParameterValue("rate");
        shape  = apvts.getRawParameterValue("shape");
        depth  = apvts.getRawParameterValue("depth");
        stereo = apvts.getRawParameterValue("stereo");
        envAmt = apvts.getRawParameterValue("envAmt");
        envRel = apvts.getRawParameterValue("envRel");
        mix    = apvts.getRawParameterValue("mix");
        outDb  = apvts.getRawParameterValue("out");
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "ftype", 1 }, "Filter Type",
            StringArray{ "Lowpass 12", "Lowpass 24", "Bandpass", "Highpass" }, 1));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "cutoff", 1 }, "Cutoff",
            NormalisableRange<float>(30.0f, 18000.0f, 0.1f, 0.3f), 600.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "res", 1 }, "Resonance",
            NormalisableRange<float>(0.5f, 15.0f, 0.01f, 0.5f), 5.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "drive", 1 }, "Drive",
            NormalisableRange<float>(0.0f, 1.0f), 0.2f));

        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "rate", 1 }, "LFO Rate",
            StringArray{ "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32" }, 1));
        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "shape", 1 }, "LFO Shape",
            StringArray{ "Sine", "Triangle", "Saw Up", "Saw Down", "Square" }, 0));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "depth", 1 }, "LFO Depth",
            NormalisableRange<float>(0.0f, 1.0f), 0.6f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "stereo", 1 }, "LFO Stereo",
            NormalisableRange<float>(0.0f, 1.0f), 0.25f));

        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "envAmt", 1 }, "Env Amount",
            NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "envRel", 1 }, "Env Release (ms)",
            NormalisableRange<float>(10.0f, 600.0f, 0.1f, 0.5f), 120.0f));

        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "mix", 1 }, "Mix",
            NormalisableRange<float>(0.0f, 1.0f), 1.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "out", 1 }, "Output (dB)",
            NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

        return { p.begin(), p.end() };
    }

    void prepareToPlay(double sr, int blockSize) override
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec mono{ sr, (juce::uint32) blockSize, 1 };
        for (auto& ch : f)
            for (auto& stage : ch) { stage.prepare(mono); stage.reset(); }

        for (int c = 0; c < 2; ++c) { env[c] = 0.0f; octSm[c] = 0.0f; }
        freeBeats = 0.0;
    }

    static float lfoShape(int shape, float p) // p: 0..1, Ausgabe -1..1
    {
        switch (shape)
        {
            case 0:  return std::sin(juce::MathConstants<float>::twoPi * p);
            case 1:  return 4.0f * std::abs(p - 0.5f) - 1.0f;
            case 2:  return 2.0f * p - 1.0f;
            case 3:  return 1.0f - 2.0f * p;
            default: return p < 0.5f ? 1.0f : -1.0f;
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin(buffer.getNumChannels(), 2);
        const int n = buffer.getNumSamples();

        // --- Host-Tempo / Position ---
        double bpm = 145.0, ppq = 0.0;
        bool playing = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto q = pos->getPpqPosition()) { ppq = *q; playing = pos->getIsPlaying(); }
            }
        const double beatsPerSample = bpm / (60.0 * sampleRate);
        if (!playing) ppq = freeBeats;

        static const double lens[] = { 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
        const double lenBeats = lens[(int) *rate];

        const int type = (int) *ftype;
        const bool lp24 = (type == 1);
        const auto fType = (type == 2) ? juce::dsp::StateVariableTPTFilterType::bandpass
                         : (type == 3) ? juce::dsp::StateVariableTPTFilterType::highpass
                                       : juce::dsp::StateVariableTPTFilterType::lowpass;
        for (auto& ch : f)
        {
            ch[0].setType(fType);  ch[0].setResonance(*res);
            ch[1].setType(fType);  ch[1].setResonance(0.8f);
        }

        const float baseCut = *cutoff;
        const float lfoDepth = *depth * 3.0f;   // bis +/-3 Oktaven
        const float envOct = *envAmt * 4.0f;    // bis +/-4 Oktaven
        const float stereoOff = *stereo * 0.5f; // bis halber Zyklus
        const int shapeIdx = (int) *shape;
        const float g = 1.0f + *drive * 8.0f;
        const float gNorm = 1.0f / std::tanh(g);
        const bool useDrive = *drive > 0.001f;
        const float mixAmt = *mix;
        const float outGain = juce::Decibels::decibelsToGain(*outDb);
        const float maxCut = (float) (sampleRate * 0.45);

        const float atk = 1.0f - std::exp(-1.0f / (0.003f * (float) sampleRate));
        const float rel = 1.0f - std::exp(-1.0f / (0.001f * *envRel * (float) sampleRate));
        const float smCoef = 1.0f - std::exp(-1.0f / (0.003f * (float) sampleRate)); // glättet Square/Saw-Sprünge

        for (int i = 0; i < n; ++i)
        {
            const double cyc = ppq / lenBeats;
            const float basePhase = (float) (cyc - std::floor(cyc));

            for (int c = 0; c < numCh; ++c)
            {
                auto* data = buffer.getWritePointer(c);
                const float in = data[i];

                float p = basePhase + (c == 1 ? stereoOff : 0.0f);
                p -= std::floor(p);

                // Envelope-Follower
                const float level = std::abs(in);
                env[c] += (level > env[c] ? atk : rel) * (level - env[c]);

                const float oct = lfoDepth * lfoShape(shapeIdx, p)
                                + envOct * juce::jmin(1.0f, env[c] * 2.0f);
                octSm[c] += smCoef * (oct - octSm[c]);

                const float fc = juce::jlimit(30.0f, maxCut, baseCut * std::pow(2.0f, octSm[c]));
                f[c][0].setCutoffFrequency(fc);
                f[c][1].setCutoffFrequency(fc);

                float x = in;
                if (useDrive) x = std::tanh(x * g) * gNorm;

                float y = f[c][0].processSample(0, x);
                if (lp24) y = f[c][1].processSample(0, y);

                data[i] = (in * (1.0f - mixAmt) + y * mixAmt) * outGain;
            }
            ppq += beatsPerSample;
        }

        if (!playing) freeBeats = ppq;
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GoaFilter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
    }
    void setStateInformation(const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary(data, size))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> *ftype, *cutoff, *res, *drive, *rate, *shape, *depth,
                       *stereo, *envAmt, *envRel, *mix, *outDb;

    // [Kanal][Stufe]: getrennte Filter pro Kanal, damit L/R unterschiedliche Cutoffs haben können
    juce::dsp::StateVariableTPTFilter<float> f[2][2];

    double sampleRate = 44100.0;
    double freeBeats = 0.0;
    float env[2] = { 0.0f, 0.0f };
    float octSm[2] = { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoaFilter)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GoaFilter(); }
