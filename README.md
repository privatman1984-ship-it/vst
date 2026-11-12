# PsyPlugins

Drei kostenlose VST3-Plugins (plus Standalone) für Goa- und Psytrance, gebaut mit [JUCE](https://juce.com).

## GoaGater – Step Gater + Filter + Delay

- 16 Steps, Raster 1/8 · 1/16 · 1/32, Host-Sync (Fallback: 145 BPM), Smooth & Depth
- Filter: Lowpass / Bandpass / Highpass, Cutoff, Resonanz, *Gate→Cutoff*
- Delay: 1/16 · 1/8 · 1/8 punktiert · 1/4, Feedback, Mix
- Default-Pattern: Goa-Galopp

Tipp: Rate `1/16`, Delay `1/8 dotted`, Resonanz 4–6, Gate→Cutoff ca. 0.4.

## PsyDelay – Stereo-Ping-Pong-Delay

- Sync-Zeiten getrennt für links und rechts: 1/16 · 1/8 · 1/8 punktiert · 1/4 · 1/4 punktiert · 1/2
- Ping-Pong-Schalter
- Feedback-Pfad mit Highpass, Lowpass und Sättigung (Drive), damit die Echos mit jedem Durchlauf dunkler und wärmer werden
- Modulation (Rate/Depth), links und rechts mit 90° Phasenversatz für Breite
- Ducking: Das Delay geht zurück, solange das Eingangssignal laut ist (z. B. bei Leads, Vocals oder Hi-Hats)
- Default: links 1/8 punktiert, rechts 1/4, Ping-Pong an

Tipp für Leads: Feedback 0.55–0.7, FB Highpass 150–250 Hz, FB Lowpass 3–5 kHz, Ducking 0.5, Mix 0.3.

## GoaFilter – Sweep-Filter mit Sync-LFO

- Filter: Lowpass 12 · Lowpass 24 · Bandpass · Highpass, Cutoff, Resonanz, Drive (Sättigung vor dem Filter)
- LFO tempo-synchron: 2 Bars bis 1/32, Formen Sine · Triangle · Saw Up · Saw Down · Square, Depth bis ±3 Oktaven
- Stereo-Versatz des LFO für breite, wandernde Sweeps
- Envelope-Follower (Amount ±, Release): Das Filter öffnet oder schließt sich mit der Lautstärke des Eingangs
- Mix und Output-Gain

Tipp für den Goa-Bass/Acid-Sweep: Lowpass 24, Cutoff ca. 500 Hz, Resonanz 6–9, Rate 1 Bar, Shape Saw Up oder Sine, Depth 0.6, Drive 0.3.
Für Rolling-Bässe: Rate 1/16, Shape Triangle, Depth 0.3.

## Build

Voraussetzungen: CMake ≥ 3.22 und ein C++17-Compiler (Visual Studio, Xcode oder GCC/Clang).

```bash
git clone https://github.com/privatman1984-ship-it/vst.git
cd vst
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE wird beim ersten Konfigurieren automatisch geladen. Die VST3s werden danach in den Plugin-Ordner deines Systems kopiert (abschaltbar mit `-DCOPY_AFTER_BUILD=OFF`).

Unter Linux werden zusätzlich diese Pakete gebraucht:

```bash
sudo apt install libasound2-dev libjack-jackd2-dev ladspa-sdk libfreetype-dev \
  libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```

## Fertige Builds

Bei jedem Push baut GitHub Actions alle Plugins für Windows, macOS und Linux. Die Dateien findest du unter *Actions → Workflow-Lauf → Artifacts*.

## Lizenz

Quellcode: MIT (siehe `LICENSE`). JUCE hat eine eigene Lizenz (AGPLv3 oder kommerziell). Beachte sie, wenn du die Plugins verteilst.
