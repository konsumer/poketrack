/* Stand-in for MTS-ESP's libMTSClient.h, which dexed's msfa/dx7note.{h,cc}
 * include for its MTS-ESP microtuning support.
 *
 * MTS-ESP is a host-wide microtuning standard: an MTS master (another plugin)
 * broadcasts scale data, and clients pick it up. There's no MTS master in a
 * WCLAP-sandboxed plugin with no plugin-to-plugin communication, so every call
 * here reports "no master" — which is exactly the state dexed itself runs in
 * when nothing has registered as an MTS master: dx7note.cc then takes the
 * standard-tuning path instead of the MTS path, byte for byte.
 *
 * That path is what makes this shim faithful and not just a stub: the MTS
 * branch in Dx7Note::osc_freq() is entered only when
 * tuning_state_->is_standard_tuning() && MTS_HasMaster(), and
 * updateBasePitches() only runs under the same condition (see
 * PluginProcessor::processBlock's checkMTSESPRetuning). With MTS_HasMaster()
 * false, neither ever executes.
 *
 * MTS_NoteToFrequency() still has to return *something* correct-ish because
 * Dx7Note::updateBasePitches() assigns it to mtsFreq unconditionally; returning
 * dexed's own "neutral" A440 (the value MTS_NoteToFrequency yields for note 69
 * at 12-TET) keeps that dead branch's arithmetic identical to dexed-with-no-
 * master rather than introducing a NaN.
 */
#ifndef DEXED_COMPAT_LIBMTS_CLIENT_H
#define DEXED_COMPAT_LIBMTS_CLIENT_H

struct MTSClient;

static inline MTSClient *MTS_RegisterClient(void) { return 0; }
static inline void MTS_DeregisterClient(MTSClient *client) { (void)client; }
static inline bool MTS_HasMaster(MTSClient *client) {
  (void)client;
  return false;
}
static inline bool MTS_ShouldFilterNote(MTSClient *client, int midiNote, int midiChannel) {
  (void)client;
  (void)midiNote;
  (void)midiChannel;
  return false;
}
static inline double MTS_NoteToFrequency(MTSClient *client, int midiNote, int midiChannel) {
  (void)client;
  (void)midiNote;
  (void)midiChannel;
  return 440.0;
}

#endif /* DEXED_COMPAT_LIBMTS_CLIENT_H */
