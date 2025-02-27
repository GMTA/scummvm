/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "sci/resource/resource.h"
#include "sci/engine/kernel.h"
#include "sci/engine/seg_manager.h"
#include "sci/sound/audio.h"

#include "backends/audiocd/audiocd.h"

#include "common/config-manager.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/system.h"

#include "audio/audiostream.h"
#include "audio/decoders/aiff.h"
#include "audio/decoders/flac.h"
#include "audio/decoders/mac_snd.h"
#include "audio/decoders/mp3.h"
#include "audio/decoders/raw.h"
#include "audio/decoders/vorbis.h"
#include "audio/decoders/wave.h"

namespace Sci {

AudioPlayer::AudioPlayer(ResourceManager *resMan) : _resMan(resMan), _audioRate(11025),
		_audioCdStart(0), _initCD(false), _playCounter(0) {

	_mixer = g_system->getMixer();
	_wPlayFlag = false;
}

AudioPlayer::~AudioPlayer() {
	stopAllAudio();
}

void AudioPlayer::stopAllAudio() {
	stopAudio();
	if (_audioCdStart > 0)
		audioCdStop();
}

/**
 * Handles the sciAudio calls in fanmade games.
 * sciAudio is an external .NET library for playing MP3 files in fanmade games.
 * It runs in the background, and obtains sound commands from the
 * currently running game via text files (called "conductor files").
 * For further info, check: http://sciprogramming.com/community/index.php?topic=634.0
 */
void AudioPlayer::handleFanmadeSciAudio(reg_t sciAudioObject, SegManager *segMan) {
	// TODO: This is a bare bones implementation. Only the play/playx and stop commands
	// are handled for now - the other commands haven't been observed in any fanmade game
	// yet. All the volume related and fading functionality is currently missing.
	Kernel *kernel = g_sci->getKernel();

	// get the sciAudio command from the "command" selector.
	// this property is a string in 1.0 and an integer in 1.1.
	enum FanmadeSciAudioCommand {
		kFanmadeSciAudioCommandNone = -1,
		kFanmadeSciAudioCommandPlayX,
		kFanmadeSciAudioCommandPlay,
		kFanmadeSciAudioCommandStop
	};
	FanmadeSciAudioCommand sciAudioCommand = kFanmadeSciAudioCommandNone;
	reg_t commandReg = readSelector(segMan, sciAudioObject, kernel->findSelector("command"));
	Common::String commandString;
	if (commandReg.isNumber()) {
		// sciAudio 1.1
		sciAudioCommand = (FanmadeSciAudioCommand)commandReg.toUint16();
	} else {
		// sciAudio 1.0
		commandString = segMan->getString(commandReg);
		if (commandString == "playx") {
			sciAudioCommand = kFanmadeSciAudioCommandPlayX;
		} else if (commandString == "play") {
			sciAudioCommand = kFanmadeSciAudioCommandPlay;
		} else if (commandString == "stop") {
			sciAudioCommand = kFanmadeSciAudioCommandStop;
		}
	}

	if (sciAudioCommand == kFanmadeSciAudioCommandPlayX ||
		sciAudioCommand == kFanmadeSciAudioCommandPlay) {
		reg_t fileNameReg = readSelector(segMan, sciAudioObject, kernel->findSelector("fileName"));
		Common::String fileName = segMan->getString(fileNameReg);

		reg_t loopCountReg = readSelector(segMan, sciAudioObject, kernel->findSelector("loopCount"));
		int16 loopCount;
		if (loopCountReg.isNumber()) {
			// sciAudio 1.1
			loopCount = loopCountReg.toSint16();
		} else {
			// sciAudio 1.0
			Common::String loopCountStr = segMan->getString(loopCountReg);
			loopCount = atoi(loopCountStr.c_str());
		}

		// Adjust loopCount for ScummVM's LoopingAudioStream semantics
		if (loopCount == -1) {
			loopCount = 0; // loop endlessly
		} else if (loopCount >= 0) {
			// sciAudio loopCount == 0 -> play 1 time  -> ScummVM's loopCount should be 1
			// sciAudio loopCount == 1 -> play 2 times -> ScummVM's loopCount should be 2
			loopCount++;
		} else {
			loopCount = 1; // play once in case the value makes no sense
		}

		// Determine sound type
		Audio::Mixer::SoundType soundType = Audio::Mixer::kSFXSoundType;
		if (fileName.hasPrefix("music"))
			soundType = Audio::Mixer::kMusicSoundType;
		else if (fileName.hasPrefix("speech"))
			soundType = Audio::Mixer::kSpeechSoundType;

		// Determine compression
		uint32 audioCompressionType = 0;
		if ((fileName.hasSuffix(".mp3")) || (fileName.hasSuffix(".sciAudio")) || (fileName.hasSuffix(".sciaudio"))) {
			audioCompressionType = MKTAG('M','P','3',' ');
		} else if (fileName.hasSuffix(".wav")) {
			audioCompressionType = MKTAG('W','A','V',' ');
		} else if (fileName.hasSuffix(".aiff")) {
			audioCompressionType = MKTAG('A','I','F','F');
		} else {
			error("sciAudio: unsupported file type");
		}

		Common::File *sciAudioFile = new Common::File();
		// Replace backwards slashes
		for (uint i = 0; i < fileName.size(); i++) {
			if (fileName[i] == '\\')
				fileName.setChar('/', i);
		}
		sciAudioFile->open("sciAudio/" + fileName);

		Audio::RewindableAudioStream *audioStream = nullptr;

		switch (audioCompressionType) {
		case MKTAG('M','P','3',' '):
#ifdef USE_MAD
			audioStream = Audio::makeMP3Stream(sciAudioFile, DisposeAfterUse::YES);
#endif
			break;
		case MKTAG('W','A','V',' '):
			audioStream = Audio::makeWAVStream(sciAudioFile, DisposeAfterUse::YES);
			break;
		case MKTAG('A','I','F','F'):
			audioStream = Audio::makeAIFFStream(sciAudioFile, DisposeAfterUse::YES);
			break;
		default:
			break;
		}

		if (!audioStream) {
			error("sciAudio: requested compression not compiled into ScummVM");
		}

		// We only support one audio handle
		_mixer->playStream(soundType, &_audioHandle,
							Audio::makeLoopingAudioStream((Audio::RewindableAudioStream *)audioStream, loopCount));
	} else if (sciAudioCommand == kFanmadeSciAudioCommandStop) {
		_mixer->stopHandle(_audioHandle);
	} else {
		if (commandReg.isNumber()) {
			warning("Unhandled sciAudio command: %u", commandReg.getOffset());
		} else {
			warning("Unhandled sciAudio command: %s", commandString.c_str());
		}
	}
}

int AudioPlayer::startAudio(uint16 module, uint32 number) {
	int sampleLen;
	Audio::AudioStream *audioStream = getAudioStream(number, module, &sampleLen);

	if (audioStream) {
		_wPlayFlag = false;
		Audio::Mixer::SoundType soundType = (module == 65535) ? Audio::Mixer::kSFXSoundType : Audio::Mixer::kSpeechSoundType;
		_mixer->playStream(soundType, &_audioHandle, audioStream);
		return sampleLen;
	} else {
		// Don't throw a warning in this case. getAudioStream() already has. Some games
		// do miss audio entries (perhaps because of a typo, or because they were simply
		// forgotten).
		return 0;
	}
}

int AudioPlayer::wPlayAudio(uint16 module, uint32 tuple) {
	// Get the audio sample length and set the wPlay flag so we return 0 on
	// position. SSCI pre-loads the audio here, but it's much easier for us to
	// just get the sample length and return that. wPlayAudio should *not*
	// actually start the sample.

	int sampleLen = 0;
	Audio::AudioStream *audioStream = getAudioStream(tuple, module, &sampleLen);
	if (!audioStream)
		warning("wPlayAudio: unable to create stream for audio tuple %d, module %d", tuple, module);
	delete audioStream;
	_wPlayFlag = true;
	return sampleLen;
}

void AudioPlayer::stopAudio() {
	_mixer->stopHandle(_audioHandle);
}

void AudioPlayer::pauseAudio() {
	_mixer->pauseHandle(_audioHandle, true);
}

void AudioPlayer::resumeAudio() {
	_mixer->pauseHandle(_audioHandle, false);
}

int AudioPlayer::getAudioPosition() {
	if (_mixer->isSoundHandleActive(_audioHandle))
		return _mixer->getSoundElapsedTime(_audioHandle) * 6 / 100; // return elapsed time in ticks
	else if (_wPlayFlag)
		return 0; // Sound has "loaded" so return that it hasn't started
	else
		return -1; // Sound finished
}

enum SolFlags {
	kSolFlagCompressed = 1 << 0,
	kSolFlagUnknown    = 1 << 1,
	kSolFlag16Bit      = 1 << 2,
	kSolFlagIsSigned   = 1 << 3
};

// FIXME: Move this to sound/adpcm.cpp?
// Note that the 16-bit version is also used in coktelvideo.cpp
static const uint16 tableDPCM16[128] = {
	0x0000, 0x0008, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060, 0x0070, 0x0080,
	0x0090, 0x00A0, 0x00B0, 0x00C0, 0x00D0, 0x00E0, 0x00F0, 0x0100, 0x0110, 0x0120,
	0x0130, 0x0140, 0x0150, 0x0160, 0x0170, 0x0180, 0x0190, 0x01A0, 0x01B0, 0x01C0,
	0x01D0, 0x01E0, 0x01F0, 0x0200, 0x0208, 0x0210, 0x0218, 0x0220, 0x0228, 0x0230,
	0x0238, 0x0240, 0x0248, 0x0250, 0x0258, 0x0260, 0x0268, 0x0270, 0x0278, 0x0280,
	0x0288, 0x0290, 0x0298, 0x02A0, 0x02A8, 0x02B0, 0x02B8, 0x02C0, 0x02C8, 0x02D0,
	0x02D8, 0x02E0, 0x02E8, 0x02F0, 0x02F8, 0x0300, 0x0308, 0x0310, 0x0318, 0x0320,
	0x0328, 0x0330, 0x0338, 0x0340, 0x0348, 0x0350, 0x0358, 0x0360, 0x0368, 0x0370,
	0x0378, 0x0380, 0x0388, 0x0390, 0x0398, 0x03A0, 0x03A8, 0x03B0, 0x03B8, 0x03C0,
	0x03C8, 0x03D0, 0x03D8, 0x03E0, 0x03E8, 0x03F0, 0x03F8, 0x0400, 0x0440, 0x0480,
	0x04C0, 0x0500, 0x0540, 0x0580, 0x05C0, 0x0600, 0x0640, 0x0680, 0x06C0, 0x0700,
	0x0740, 0x0780, 0x07C0, 0x0800, 0x0900, 0x0A00, 0x0B00, 0x0C00, 0x0D00, 0x0E00,
	0x0F00, 0x1000, 0x1400, 0x1800, 0x1C00, 0x2000, 0x3000, 0x4000
};

static const byte tableDPCM8[8] = {0, 1, 2, 3, 6, 10, 15, 21};

static void deDPCM16(byte *soundBuf, Common::SeekableReadStream &audioStream, uint32 n) {
	int16 *out = (int16 *) soundBuf;

	int32 s = 0;
	for (uint32 i = 0; i < n; i++) {
		byte b = audioStream.readByte();
		if (b & 0x80)
			s -= tableDPCM16[b & 0x7f];
		else
			s += tableDPCM16[b];

		s = CLIP<int32>(s, -32768, 32767);
		*out++ = TO_LE_16(s);
	}
}

static void deDPCM8Nibble(byte *soundBuf, int32 &s, byte b) {
	if (b & 8) {
		s -= tableDPCM8[7 - (b & 7)];
	} else
		s += tableDPCM8[b & 7];
	s = CLIP<int32>(s, 0, 255);
	*soundBuf = s;
}

static void deDPCM8(byte *soundBuf, Common::SeekableReadStream &audioStream, uint32 n) {
	int32 s = 0x80;

	for (uint i = 0; i < n; i++) {
		byte b = audioStream.readByte();

		deDPCM8Nibble(soundBuf++, s, b >> 4);
		deDPCM8Nibble(soundBuf++, s, b & 0xf);
	}
}

// Sierra SOL audio file reader
// Check here for more info: http://wiki.multimedia.cx/index.php?title=Sierra_Audio
static bool readSOLHeader(Common::SeekableReadStream *audioStream, int headerSize, uint32 &size, uint16 &audioRate, byte &audioFlags, uint32 resSize) {
	if (headerSize != 7 && headerSize != 11 && headerSize != 12) {
		warning("SOL audio header of size %i not supported", headerSize);
		return false;
	}

	uint32 tag = audioStream->readUint32BE();

	if (tag != MKTAG('S','O','L',0)) {
		warning("No 'SOL' FourCC found");
		return false;
	}

	audioRate = audioStream->readUint16LE();
	audioFlags = audioStream->readByte();

	// For the QFG3 demo format, just use the resource size
	// Otherwise, load it from the header
	if (headerSize == 7)
		size = resSize;
	else
		size = audioStream->readUint32LE();
	return true;
}

static byte *readSOLAudio(Common::SeekableReadStream *audioStream, uint32 &size, byte audioFlags, byte &flags) {
	byte *buffer;

	// Convert the SOL stream flags to our own format
	flags = 0;
	if (audioFlags & kSolFlag16Bit)
		flags |= Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN;

	if (!(audioFlags & kSolFlagIsSigned))
		flags |= Audio::FLAG_UNSIGNED;

	if (audioFlags & kSolFlagCompressed) {
		buffer = (byte *)malloc(size * 2);
		assert(buffer);

		if (audioFlags & kSolFlag16Bit)
			deDPCM16(buffer, *audioStream, size);
		else
			deDPCM8(buffer, *audioStream, size);

		size *= 2;
	} else {
		// We assume that the sound data is raw PCM
		buffer = (byte *)malloc(size);
		assert(buffer);
		audioStream->read(buffer, size);
	}

	return buffer;
}

Audio::RewindableAudioStream *AudioPlayer::getAudioStream(uint32 number, uint32 volume, int *sampleLen) {
	Audio::SeekableAudioStream *audioSeekStream = nullptr;
	Audio::RewindableAudioStream *audioStream = nullptr;
	uint32 size = 0;
	byte *data = nullptr;
	byte flags = 0;
	Sci::Resource *audioRes;

	*sampleLen = 0;

	if (volume == 65535) {
		audioRes = _resMan->findResource(ResourceId(kResourceTypeAudio, number), false);
		if (!audioRes) {
			warning("Failed to find audio entry %i", number);
			return nullptr;
		}
	} else {
		audioRes = _resMan->findResource(ResourceId(kResourceTypeAudio36, volume, number), false);
		if (!audioRes) {
			warning("Failed to find audio entry (%i, %i, %i, %i, %i)", volume, (number >> 24) & 0xff,
					(number >> 16) & 0xff, (number >> 8) & 0xff, number & 0xff);
			return nullptr;
		}
	}

	// We copy over the audio data in our own buffer. We have to do
	// this, because ResourceManager may free the original data late,
	// and audio decompression may work on-the-fly instead.
	byte *audioBuffer = (byte *)malloc(audioRes->size());
	assert(audioBuffer);
	audioRes->unsafeCopyDataTo(audioBuffer);
	Common::SeekableReadStream *memoryStream = new Common::MemoryReadStream(audioBuffer, audioRes->size(), DisposeAfterUse::YES);

	byte audioFlags;
	uint32 audioCompressionType = audioRes->getAudioCompressionType();

	if (audioCompressionType) {
#if (defined(USE_MAD) || defined(USE_VORBIS) || defined(USE_FLAC))
		// Compressed audio made by our tool
		switch (audioCompressionType) {
		case MKTAG('M','P','3',' '):
#ifdef USE_MAD
			audioSeekStream = Audio::makeMP3Stream(memoryStream, DisposeAfterUse::YES);
#endif
			break;
		case MKTAG('O','G','G',' '):
#ifdef USE_VORBIS
			audioSeekStream = Audio::makeVorbisStream(memoryStream, DisposeAfterUse::YES);
#endif
			break;
		case MKTAG('F','L','A','C'):
#ifdef USE_FLAC
			audioSeekStream = Audio::makeFLACStream(memoryStream, DisposeAfterUse::YES);
#endif
			break;
		default:
			break;
		}
#else
		error("Compressed audio file encountered, but no appropriate decoder is compiled in");
#endif
	} else {
		// Original source file
		if (audioRes->size() > 6 &&
			(audioRes->getUint8At(0) & 0x7f) == kResourceTypeAudio &&
			audioRes->getUint32BEAt(2) == MKTAG('S','O','L',0)) {
			// SCI1.1
			delete memoryStream;
			const uint8 headerSize = audioRes->getUint8At(1);
			Common::MemoryReadStream headerStream = audioRes->subspan(kResourceHeaderSize, headerSize).toStream();

			if (readSOLHeader(&headerStream, headerSize, size, _audioRate, audioFlags, audioRes->size())) {
				Common::MemoryReadStream dataStream(audioRes->subspan(kResourceHeaderSize + headerSize).toStream());
				data = readSOLAudio(&dataStream, size, audioFlags, flags);
				audioSeekStream = Audio::makeRawStream(data, size, _audioRate, flags);
			}
		} else if (audioRes->size() > 4 && audioRes->getUint32BEAt(0) == MKTAG('R','I','F','F')) {
			// WAVE detected

			// Calculate samplelen from WAVE header
			int waveSize = 0, waveRate = 0;
			byte waveFlags = 0;
			bool ret = Audio::loadWAVFromStream(*memoryStream, waveSize, waveRate, waveFlags);
			if (!ret)
				error("Failed to load WAV from stream");

			*sampleLen = (waveFlags & Audio::FLAG_16BITS ? waveSize >> 1 : waveSize) * 60 / waveRate;

			memoryStream->seek(0, SEEK_SET);
			audioStream = Audio::makeWAVStream(memoryStream, DisposeAfterUse::YES);
		} else if (audioRes->size() > 14 &&
				   audioRes->getUint16BEAt(0) == 1 &&
				   audioRes->getUint16BEAt(2) == 1 &&
				   audioRes->getUint16BEAt(4) == 5 &&
				   audioRes->getUint32BEAt(10) == 0x00018051) {

			// Mac snd detected
			audioSeekStream = Audio::makeMacSndStream(memoryStream, DisposeAfterUse::YES);
			if (!audioSeekStream)
				error("Failed to load Mac sound stream");

		} else {
			// SCI1 raw audio
			flags = Audio::FLAG_UNSIGNED;
			_audioRate = 11025;
			audioSeekStream = Audio::makeRawStream(memoryStream, _audioRate, flags);
		}
	}

	if (audioSeekStream) {
		*sampleLen = (audioSeekStream->getLength().msecs() * 60) / 1000; // we translate msecs to ticks
		audioStream = audioSeekStream;
	}

	// We have to make sure that we don't depend on resource manager pointers
	// after this point, because the actual audio resource may get unloaded by
	// resource manager at any time.
	return audioStream;
}

int AudioPlayer::audioCdPlay(int track, int start, int duration) {
	if (!_initCD) {
		// Initialize CD mode if we haven't already
		g_system->getAudioCDManager()->open();
		_initCD = true;
	}

	if (getSciVersion() == SCI_VERSION_1_1) {
		// King's Quest VI CD Audio format
		_audioCdStart = g_system->getMillis();

		// Subtract one from track. KQ6 starts at track 1, while ScummVM
		// ignores the data track and considers track 2 to be track 1.
		return g_system->getAudioCDManager()->play(track - 1, 1, start, duration) ? 1 : 0;
	} else {
		// Jones in the Fast Lane and Mothergoose256 CD Audio format
		uint32 length = 0;

		audioCdStop();

		Common::File audioMap;
		if(!audioMap.open("cdaudio.map"))
			error("Could not open cdaudio.map");

		while (audioMap.pos() < audioMap.size()) {
			uint16 res = audioMap.readUint16LE();
			res &= 0x1fff; // Upper bits are always set in Mothergoose256
			uint32 startFrame = audioMap.readUint16LE();
			startFrame += audioMap.readByte() << 16;
			audioMap.readByte(); // Unknown, always 0x20 in Jones, 0x04 in Mothergoose256
			length = audioMap.readUint16LE();
			length += audioMap.readByte() << 16;
			audioMap.readByte(); // Unknown, always 0x00

			// The track is the resource value in the map
			if (res == track) {
				g_system->getAudioCDManager()->play(1, 1, startFrame, length);
				_audioCdStart = g_system->getMillis();
				break;
			}
		}

		audioMap.close();

		return length * 60 / 75; // return sample length in ticks
	}
}

void AudioPlayer::audioCdStop() {
	_audioCdStart = 0;
	g_system->getAudioCDManager()->stop();
}

void AudioPlayer::audioCdUpdate() {
	g_system->getAudioCDManager()->update();
}

int AudioPlayer::audioCdPosition() {
	// Return -1 if the sample is done playing. Converting to frames to compare.
	if (((g_system->getMillis() - _audioCdStart) * 75 / 1000) >= (uint32)g_system->getAudioCDManager()->getStatus().duration)
		return -1;

	// Return the position otherwise (in ticks).
	return (g_system->getMillis() - _audioCdStart) * 60 / 1000;
}

void AudioPlayer::incrementPlayCounter() {
	_playCounter++;
}

uint16 AudioPlayer::getPlayCounter() {
	return _playCounter;
}

} // End of namespace Sci
