## Description
Please include a summary of the changes and the related issue (if applicable).

Fixes #(issue)

## Type of Change
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing hardware/firmware behavior to change)
- [ ] Documentation update (improving guides, README, or logs)
- [ ] Hardware/Calibration tuning (adjusting servo pulse limits or timings)

## Testing Performed
- [ ] Firmware compiled successfully for ESP32 without errors
- [ ] Tested on actual hardware (Servos / PCA9685 / Mic)
- [ ] Verified bounds protection (no servo stalling or out-of-range pulses)
- [ ] Tested web interface / endpoints over Wi-Fi
- [ ] Tested Python speech server (`arm_server.py`)

## Checklist
- [ ] My code follows the style guidelines of this project
- [ ] I have performed a self-review of my own code
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have made corresponding changes to the documentation
- [ ] My changes introduce no new warnings
- [ ] No personal credentials, Wi-Fi passwords, or API keys are included in this PR
