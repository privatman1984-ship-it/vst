#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class GoaGater : public juce::AudioProcessor
{
public:
    GoaGater()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", createLayout())
    {
        for (int i = 0; i < 16; ++i)
            steps[i] = apvts.getRawParameterValue("s" + juce::String(i + 1));
        rate     = apvts.getRawParameterValue("rate");
        smooth   = apvts.getRawParameterValue("smooth");
        depth    = apvts.getRawParameterValue("depth");
        ftype    = apvts.getRawParameterValue("ftype");
        cutoff   = apvts.getRawParameterValue("cutoff");
        res      = apvts.getRawParameterValue("res");
        fmod     = apvts.getRawParameterValue("fmod");
        dtime    = apvts.getRawParameterValue("dtime");
        dfb      = apvts.getRawParameterValue("dfb");
        dmix     = apvts.getRawParameterValue("dmix");
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        // Typischer Goa-Offbeat/Galopp-Pattern als Default
        const bool def[16] = { 1,0,1,1, 1,0,1,1, 1,0,1,1, 1,0,1,1 };
        for (int i = 0; i < 16; ++i)
            p.push_back(std::make_unique<AudioParameterBool>(
                ParameterID{ "s" + String(i + 1), 1 }, "Step " + String(i + 1), def[i] != 0));

        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "rate", 1 }, "Rate",
            StringArray{ "1/8", "1/16", "1/32" }, 1));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "smooth", 1 }, "Smooth (ms)",
            NormalisableRange<float>(0.2f, 40.0f, 0.01f, 0.5f), 3.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "depth", 1 }, "Depth",
            NormalisableRange<float>(0.0f, 1.0f), 1.0f));

        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "ftype", 1 }, "Filter Type",
            StringArray{ "Lowpass", "Bandpass", "Highpass" }, 0));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "cutoff", 1 }, "Cutoff",
            NormalisableRange<float>(30.0f, 18000.0f, 0.1f, 0.3f), 1200.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "res", 1 }, "Resonance",
            NormalisableRange<float>(0.5f, 12.0f, 0.01f, 0.5f), 3.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "fmod", 1 }, "Gate->Cutoff",
            NormalisableRange<float>(0.0f, 1.0f), 0.4f));

        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "dtime", 1 }, "Delay Time",
            StringArray{ "1/16", "1/8", "1/8 dotted", "1/4" }, 2));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "dfb", 1 }, "Feedback",
            NormalisableRange<float>(0.0f, 0.95f), 0.45f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "dmix", 1 }, "Delay Mix",
            NormalisableRange<float>(0.0f, 1.0f), 0.3f));

        return { p.begin(), p.end() };
    }

    void prepareToPlay(double sr, int blockSize) override
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec{ sr, (juce::uint32) blockSize, 2 };
        filter.prepare(spec);
        delay.prepare(spec);
        delay.setMaximumDelayInSamples((int) (sr * 2.0));
        delay.reset();
        gateEnv = 1.0f;
        freeRunBeats = 0.0;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin(buffer.getNumChannels(), 2);
        const int n = buffer.getNumSamples();

        // --- Host-Tempo / Position ---
        double bpm = 145.0;
        double ppq = 0.0;
        bool hostPlaying = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = *b;
                if (auto q = pos->getPpqPosition()) { ppq = *q; hostPlaying = pos->getIsPlaying(); }
            }

        const double beatsPerSample = bpm / (60.0 * sampleRate);
        if (!hostPlaying) ppq = freeRunBeats;

        static const double rateBeats[] = { 0.5, 0.25, 0.125 };
        const double stepLen = rateBeats[(int) *rate];

        static const double delayBeats[] = { 0.25, 0.5, 0.75, 1.0 };
        const float delaySamples = (float) juce::jlimit(
            1.0, sampleRate * 1.99, delayBeats[(int) *dtime] * 60.0 / bpm * sampleRate);
        delay.setDelay(delaySamples);

        const float smoothCoef = 1.0f - std::exp(-1.0f / (0.001f * *smooth * (float) sampleRate));
        const float dp = *depth;
        const float fb = *dfb;
        const float mix = *dmix;
        const float q = *res;
        const float baseCut = *cutoff;
        const float mod = *fmod;
        const int type = (int) *ftype;

        filter.setType(type == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                     : type == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                 : juce::dsp::StateVariableTPTFilterType::highpass);
        filter.setResonance(q);

        const float maxCut = (float) (sampleRate * 0.45);

        for (int i = 0; i < n; ++i)
        {
            const int idx = ((int) std::floor(ppq / stepLen)) & 15;
            const float target = (*steps[idx] > 0.5f) ? 1.0f : (1.0f - dp);
            gateEnv += smoothCoef * (target - gateEnv);

            // Gate-Hüllkurve zieht den Cutoff bis zu 4 Oktaven nach oben
            const float cut = juce::jlimit(30.0f, maxCut,
                baseCut * std::pow(2.0f, mod * 4.0f * gateEnv));
            filter.setCutoffFrequency(cut);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* data = buffer.getWritePointer(ch);
                const float dry = data[i];

                float x = dry * gateEnv;
                x = filter.processSample(ch, x);

                const float echo = delay.popSample(ch);
                delay.pushSample(ch, x + echo * fb);

                data[i] = x + echo * mix;
            }

            ppq += beatsPerSample;
        }

        if (!hostPlaying) freeRunBeats = ppq;
    }

    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor(*this);
    }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GoaGater"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
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
    std::atomic<float>* steps[16];
    std::atomic<float>*rate, *smooth, *depth, *ftype, *cutoff, *res, *fmod, *dtime, *dfb, *dmix;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay{ 96000 };

    double sampleRate = 44100.0;
    float gateEnv = 1.0f;
    double freeRunBeats = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoaGater)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GoaGater(); }
