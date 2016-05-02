#include <cstdint>
#include "audiosourceproxy.h"

class PulseAudioSource : public AudioSource
{
public:
	PulseAudioSource(AudioDeviceInfo& dev) {}
	~PulseAudioSource() {}
	//get buffer, converted to 16bit int format
	uint32_t GetBuffer(int16_t *buff, uint32_t len)
	{
		return 0;
	}
	/*
		Get how many frames has been recorded so that caller knows 
		how much to allocated for 16-bit buffer.
	*/
	bool GetFrames(uint32_t *size)
	{
		return false;
	}

	void SetResampling(int samplerate)
	{
	}
	
	uint32_t GetChannels()
	{
		return 0;
	}

	void Start(){}
	void Stop(){}

	static const wchar_t* Name()
	{
		return L"PulseAudio";
	}
	
	static void AudioDevices(std::vector<AudioDeviceInfo> &devices) {}
	static bool AudioInit() { return false; }
	static void AudioDeinit() {}
};

REGISTER_AUDIOSRC(pulse, PulseAudioSource);