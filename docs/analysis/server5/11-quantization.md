# 11. Quantization

**File:** `Core/Utils/Quantization.cs` (50 lines)

Range-based: `float [min,max] → short [0,32767]`. Resolution = (max-min)/32767. World [-10000,10000]: 0.61 units/step. Rotation [-180,180]: 0.011 degrees/step.
