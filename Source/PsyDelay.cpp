#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// PsyDelay: Stereo-Ping-Pong-Delay für Psytrance
// Sync-Zeiten pro Seite, gefiltertes + gesättigtes Feedback, Tape-artige Modulation, Ducking
class PsyDelay : public juce::AudioProcessor
{
public:
    PsyDelay()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "STATE", createLayout())
    {
        timeL    = apvts.getRawParameterValue("timeL");
        timeR    = apvts.getRawParameterValue("timeR");
        pingpong = apvts.getRawParameterValue("pingpong");
        feedback = apvts.getRawParameterValue("feedback");
        hpFreq   = apvts.getRawParameterValue("hp");
        lpFreq   = apvts.getRawParameterValue("lp");
        drive    = apvts.getRawParameterValue("drive");
        modRate  = apvts.getRawParameterValue("modRate");
        modDepth = apvts.getRawParameterValue("modDepth");
        duck     = apvts.getRawParameterValue("duck");
        mix      = apvts.getRawParameterValue("mix");
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;
        const StringArray times{ "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" };

        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "timeL", 1 }, "Time Left", times, 2));
        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ "timeR", 1 }, "Time Right", times, 3));
        p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ "pingpong", 1 }, "Ping-Pong", true));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "feedback", 1 }, "Feedback",
            NormalisableRange<float>(0.0f, 0.95f), 0.55f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "hp", 1 }, "FB Highpass",
            NormalisableRange<float>(20.0f, 2000.0f, 0.1f, 0.4f), 150.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "lp", 1 }, "FB Lowpass",
            NormalisableRange<float>(500.0f, 20000.0f, 0.1f, 0.4f), 4500.0f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "drive", 1 }, "FB Drive",
            NormalisableRange<float>(0.0f, 1.0f), 0.25f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "modRate", 1 }, "Mod Rate (Hz)",
            NormalisableRange<float>(0.05f, 5.0f, 0.01f, 0.5f), 0.4f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "modDepth", 1 }, "Mod Depth (ms)",
            NormalisableRange<float>(0.0f, 8.0f, 0.01f), 1.5f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "duck", 1 }, "Ducking",
            NormalisableRange<float>(0.0f, 1.0f), 0.5f));
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ "mix", 1 }, "Mix",
            NormalisableRange<float>(0.0f, 1.0f), 0.35f));

        return { p.begin(), p.end() };
    }

    void prepareToPlay(double sr, int blockSize) override
    {
        sampleRate = sr;
        maxDelaySamples = (float) (sr * 3.8);

        juce::dsp::ProcessSpec mono{ sr, (juce::uint32) blockSize, 1 };
        juce::dsp::ProcessSpec stereo{ sr, (juce::uint32) blockSize, 2 };

        // Zwei getrennte Delaylines, weil L und R unterschiedliche Zeiten haben
        dl.prepare(mono);  dl.setMaximumDelayInSamples((int) (sr * 4.0)); dl.reset();
        dr.prepare(mono);  dr.setMaximumDelayInSamples((int) (sr * 4.0)); dr.reset();

        hp.prepare(stereo); hp.setType(juce::dsp::StateVariableTPTFilterType::highpass); hp.setResonance(0.707f);
        lp.prepare(stereo); lp.setType(juce::dsp::StateVariableTPTFilterType::lowpass);  lp.setResonance(0.707f);
        hp.reset(); lp.reset();

        lfoPhase = 0.0f;
        env = 0.0f;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numCh = juce::jmin(buffer.getNumChannels(), 2);
        const int n = buffer.getNumSamples();
        if (numCh == 0) return;

        double bpm = 145.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;

        static const double beats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        const double secPerBeat = 60.0 / bpm;
        const float baseL = (float) juce::jmin((double) maxDelaySamples, beats[(int) *timeL] * secPerBeat * sampleRate);
        const float baseR = (float) juce::jmin((double) maxDelaySamples, beats[(int) *timeR] * secPerBeat * sampleRate);

        hp.setCutoffFrequency(*hpFreq);
        lp.setCutoffFrequency(juce::jmin(*lpFreq, (float) (sampleRate * 0.45)));

        const bool  pp = *pingpong > 0.5f;
        const float fb = *feedback;
        const float g = 1.0f + *drive * 5.0f;
        const float gNorm = 1.0f / std::tanh(g);
        const float mixAmt = *mix;
        const float duckAmt = *duck;
        const float lfoInc = juce::MathConstants<float>::twoPi * *modRate / (float) sampleRate;
        const float modSamples = *modDepth * 0.001f * (float) sampleRate;
        const float atk = 1.0f - std::exp(-1.0f / (0.002f * (float) sampleRate));
        const float rel = 1.0f - std::exp(-1.0f / (0.150f * (float) sampleRate));

        auto* left = buffer.getWritePointer(0);
        auto* right = numCh > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = right ? right[i] : inL;

            // Ducking: Delay-Signal wird leiser, solange das Eingangssignal laut ist
            const float level = juce::jmax(std::abs(inL), std::abs(inR));
            env += (level > env ? atk : rel) * (level - env);
            const float duckGain = 1.0f - duckAmt * juce::jmin(1.0f, env * 3.0f);

            // Modulation (R mit 90 Grad Versatz)
            const float lfoL = std::sin(lfoPhase);
            const float lfoR = std::sin(lfoPhase + juce::MathConstants<float>::halfPi);

            const float wetL = dl.popSample(0, juce::jlimit(2.0f, maxDelaySamples, baseL + lfoL * modSamples), true);
            const float wetR = dr.popSample(0, juce::jlimit(2.0f, maxDelaySamples, baseR + lfoR * modSamples), true);

            // Feedback-Pfad: HP -> LP -> Sättigung -> Feedback-Gain
            float fL = lp.processSample(0, hp.processSample(0, wetL));
            float fR = lp.processSample(1, hp.processSample(1, wetR));
            fL = std::tanh(fL * g) * gNorm * fb;
            fR = std::tanh(fR * g) * gNorm * fb;

            if (pp)
            {
                dl.pushSample(0, 0.5f * (inL + inR) + fR);
                dr.pushSample(0, fL);
            }
            else
            {
                dl.pushSample(0, inL + fL);
                dr.pushSample(0, inR + fR);
            }

            left[i] = inL + wetL * mixAmt * duckGain;
            if (right) right[i] = inR + wetR * mixAmt * duckGain;

            lfoPhase += lfoInc;
            if (lfoPhase > juce::MathConstants<float>::twoPi) lfoPhase -= juce::MathConstants<float>::twoPi;
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PsyDelay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
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
    std::atomic<float> *timeL, *timeR, *pingpong, *feedback, *hpFreq, *lpFreq,
                       *drive, *modRate, *modDepth, *duck, *mix;

    using Line = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
    Line dl{ 192000 }, dr{ 192000 };
    juce::dsp::StateVariableTPTFilter<float> hp, lp;

    double sampleRate = 44100.0;
    float maxDelaySamples = 100000.0f;
    float lfoPhase = 0.0f;
    float env = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PsyDelay)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PsyDelay(); }
