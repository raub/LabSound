#include "LabSound/extended/WaveTableOsc.h"
#include "../internal/WaveformOrgan2.h"
#include "../internal/WaveformPiano.h"
#include "../internal/WaveformFuzzy.h"
#include "../internal/WaveformAhh.h"

namespace lab
{
	WaveTableBank WaveTableOsc::bank;

	WaveTableBank::WaveTableBank()
	{
		std::vector<double> organ_real = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		std::vector<double> organ_imag = {0, 1, 1, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1};

		std::vector<double> bass_real = {0, 1, 0.8144329896907216, 0.20618556701030927, 0.020618556701030927};
		std::vector<double> bass_imag = {0, 0, 0, 0, 0};

		addWave(WaveTableWaveType::SINE, sinOsc());
		addWave(WaveTableWaveType::TRIANGLE, triangleOsc());
		addWave(WaveTableWaveType::SQUARE, squareOsc());
		addWave(WaveTableWaveType::SAWTOOTH, sawOsc());
        addWave(WaveTableWaveType::FUZZY, periodicWaveOsc(fuzzy_real, fuzzy_imag));
        addWave(WaveTableWaveType::ORGAN, periodicWaveOsc(organ_real, organ_imag));
        addWave(WaveTableWaveType::ORGAN2, periodicWaveOsc(organ2_real, organ2_imag));
        addWave(WaveTableWaveType::PIANO, periodicWaveOsc(piano_real, piano_imag));
		addWave(WaveTableWaveType::BASS, periodicWaveOsc(bass_real, bass_imag));
        addWave(WaveTableWaveType::VOCAL_AHH, periodicWaveOsc(ahh_real, ahh_imag));
        
	}
}
