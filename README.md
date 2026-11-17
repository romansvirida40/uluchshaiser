# ULUCHSHAISER – Pneumatic Pitch Rectification Engine
Auto-tune style VST3 plugin (JUCE 8, C++17) with a steampunk interface.

## Build on Windows (for FL Studio)
1. Install Visual Studio 2022 (workload "Desktop development with C++"), CMake and Git.
2. Open "x64 Native Tools Command Prompt for VS 2022" in this folder and run:
       cmake -B build -G "Visual Studio 17 2022"
       cmake --build build --config Release --target ULUCHSHAISER_VST3
3. The plugin is copied to C:\Program Files\Common Files\VST3\ULUCHSHAISER.vst3
   (run the prompt as Administrator, or copy the file there by hand).
4. FL Studio: Options > Manage plugins > Find plugins (Scan). Add it to a mixer insert slot.

## Controls
KEY / SCALE    notes the voice is allowed to snap to
RETUNE SPEED   how fast pitch moves to the target (low = robotic, high = natural)
AMOUNT         strength of the correction
STABILIZER     how far a note may drift before the target note changes
DOUBLER        MIX / DETUNE / WIDTH - two detuned, delayed copies for thick vocals
LEVEL          output gain
Gauge = cents off target, Nixie box = target note, Glasses = which notes you sing.
Plugin latency is about 15 ms (reported to FL Studio automatically).
