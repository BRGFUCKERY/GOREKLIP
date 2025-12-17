This zip contains a fix so the DIGITAL clipper path uses the Fruity knee LUT.

Change made:
- PluginProcessor.cpp now includes fruity_knee_lut_8192-2.h
- fruityClipperDigital(float) now returns FruityMatch::processSample(sample)

Drop PluginProcessor.cpp into your project (replace existing).
