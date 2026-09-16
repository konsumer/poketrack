/* Standard-tuning TuningState for the vendored msfa engine.
 *
 * Replaces dexed's Source/msfa/tuning.cc, which can't be vendored verbatim:
 * besides the standard tuning below (copied from it unchanged) it implements
 * SCL/KBM tunings through surgesynthteam's tuning-library and raises JUCE
 * AlertWindows on parse failure — JUCE, and a file picker, are both out of
 * reach in a sandboxed WCLAP plugin.
 *
 * StandardTuning is the state this plugin always runs in, and its numbers are
 * dexed's own (base = (1 << 24) * (log2(440) - 69/12), step = (1 << 24) / 12),
 * so pitch comes out identical to Dexed with no external tuning applied.
 */
#include "../msfa/tuning.h"

struct StandardTuning : public TuningState {
  StandardTuning() {
    const int base = 50857777;  // (1 << 24) * (log(440) / log(2) - 69/12)
    const int step = (1 << 24) / 12;
    for (int mn = 0; mn < 128; ++mn)
      current_logfreq_table_[mn] = base + step * mn;
  }

  int32_t midinote_to_logfreq(int midinote) override { return current_logfreq_table_[midinote]; }

  int current_logfreq_table_[128];
};

std::shared_ptr<TuningState> createStandardTuning() {
  return std::make_shared<StandardTuning>();
}

std::shared_ptr<TuningState> createTuningFromSCLData(const std::string &sclData) {
  (void)sclData;
  return nullptr;
}

std::shared_ptr<TuningState> createTuningFromKBMData(const std::string &kbmData) {
  (void)kbmData;
  return nullptr;
}

std::shared_ptr<TuningState> createTuningFromSCLAndKBMData(const std::string &sclData, const std::string &kbmData) {
  (void)sclData;
  (void)kbmData;
  return nullptr;
}
