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

#ifndef AUDIO_MT32GM_H
#define AUDIO_MT32GM_H

#include "audio/mididrv.h"
#include "audio/mididrv_ms.h"
#include "common/mutex.h"
#include "common/queue.h"

/**
 * @defgroup audio_mt32_gm MIDI driver for MT-32 and GM
 * @ingroup audio
 *
 * @brief MIDI driver for MT-32 and GM compatible emulators and devices.
 * @{
 */

/**
 * MIDI driver for MT-32 and GM compatible emulators and devices.
 *
 * This class contains some commonly needed functionality for these devices and
 * the MIDI data that targets them. It wraps the MidiDriver instance that does
 * the actual communication with the MT-32 or GM device.
 *
 * This driver has the following features:
 *
 * - MIDI device initialization
 *	 Construct the driver with the type of MIDI data that will be sent to it.
 *   When the driver is opened, it will create an output MIDI driver appropriate
 *   for the user configuration settings and the type of MIDI data. You can also
 *   create the output MIDI driver yourself and pass it to the open function.
 *   The driver will take care of initializing the MIDI device and setting up
 *   for playback of MT-32 data on a GM/GS device or the other way around.
 *
 * - MT-32 <> GM conversion
 *   If the incoming MIDI data has been set to MT-32 and the output device is
 *   GM, the driver will map MT-32 instruments to GM equivalents. GM playback
 *   on an MT-32 device is also supported. Set the _mt32ToGMInstrumentMap and
 *   _gmToMT32InstrumentMap variables to override the standard instrument maps,
 *   or override the mapMT32InstrumentToGM and mapGMInstrumentToMT32 functions
 *   for more advanced mapping algorithms.
 *
 * - Reverse stereo
 *   If the game has MIDI data with reversed stereo compared to the targeted
 *   output device, set the MIDI_DATA_REVERSE_PANNING property to reverse
 *   stereo. The driver wil automatically reverse stereo when MT-32 data is
 *   sent to a GM/GS device or the other way around.
 *
 * - Correct Roland GS bank and drumkit selects
 *   Some games' MIDI data relies on a feature of the Roland SC-55 MIDI module
 *	 which automatically corrects invalid bank selects and drumkit program
 *	 changes. The driver replicates this feature to ensure correct instrument
 *	 banks and drumkits on other hardware or softsynths.
 *
 * - SysEx queue
 *   The sysExQueue function will queue a SysEx message and return immediately.
 *   You can send more messages to the queue while the driver sends the
 *   messages asynchronously with the necessary delays to the MIDI device. Use
 *   the isReady function to check if the device has received all messages and
 *   is ready to start playback. Use this instead of the sysEx function to
 *   prevent the main game loop from being blocked while the driver waits the
 *   necessary amount of time for the MIDI device to process the message.
 *   Use clearSysExQueue to remove all messages from the queue, in case device
 *   initialization has to be aborted.
 *
 * - Multiple MIDI sources
 *   If the game plays multiple streams of MIDI data at the same time, each
 *   stream can be marked with a source number. This enables the following
 *   feature:
 *   - Channel mapping
 *     If multiple sources use the same MIDI channels, the driver can map the
 *	   data channels to different output channels to avoid conflicts. Use
 *	   allocateSourceChannels to allocate output channels to a source. The
 *	   data channels are automatically mapped to the allocated output channels
 *	   during playback. The allocated channels are freed when the source is
 *	   deinitialized.
 *	   If you only have one source of MIDI data or the sources do not use
 *	   conflicting channels, you do not need to allocate channels - the channels
 *	   in the MIDI data will be used directly. If you do use this feature, you
 *	   have to use it for all MIDI sources to avoid channel conflicts.
 *	   The standard channel allocation scheme will allocate the available output
 *	   channels with the lowest numbers and will fail if not enough channels are
 *	   available. You can override the allocateSourceChannels and
 *	   mapSourceChannel functions to customize the allocation and mapping
 *	   algorithms.
 *	   Note that you can also use the "standard" way of allocating channels
 *	   using the allocateChannel function and MidiChannel objects. These two
 *	   methods are not coordinated in any way, so don't use both at the same
 *	   time.
 */
class MidiDriver_MT32GM : public MidiDriver_Multisource {
public:
	static const byte MT32_DEFAULT_INSTRUMENTS[8];
	static const byte MT32_DEFAULT_PANNING[8];
	static const uint8 MT32_DEFAULT_CHANNEL_VOLUME = 98;
	static const uint8 GM_DEFAULT_CHANNEL_VOLUME = 100;
	// Map for correcting Roland GS drumkit numbers.
	static const uint8 GS_DRUMKIT_FALLBACK_MAP[128];

	static const uint8 MT32_DISPLAY_NUM_CHARS = 20;
	static const uint32 MT32_DISPLAY_MEMORY_ADDRESS = 0x20 << 14;

protected:
	static const uint8 MAXIMUM_MT32_ACTIVE_NOTES = 48;
	static const uint8 MAXIMUM_GM_ACTIVE_NOTES = 96;

protected:
	/**
	 * This stores the values of the MIDI controllers for
	 * a MIDI channel. It is used to keep track of controller
	 * values while a channel is locked, so they can be
	 * restored when the channel is unlocked.
	 */
	struct MidiChannelControlData {
		// The source that last sent an event to this channel
		int8 source;
		// True if the source volume has been applied to this channel
		bool sourceVolumeApplied;

		uint16 pitchWheel;
		byte program;
		// The Roland GS instrument bank
		byte instrumentBank;
		byte channelPressure;

		byte modulation;
		// The volume specified by the MIDI data
		byte volume;
		// The volume set on the MIDI device. This is scaled using the source
		// volume and optionally the user-specified volume setting.
		byte scaledVolume;
		byte panPosition;
		byte expression;
		bool sustain;

		// The currently selected Registered Parameter Number
		uint16 rpn;
		byte pitchBendSensitivity;

		MidiChannelControlData() : source(-1),
			sourceVolumeApplied(false),
			pitchWheel(MIDI_PITCH_BEND_DEFAULT),
			program(0),
			instrumentBank(0),
			channelPressure(0),
			modulation(0),
			volume(0),
			scaledVolume(0),
			panPosition(MIDI_PANNING_DEFAULT),
			expression(MIDI_EXPRESSION_DEFAULT),
			sustain(false),
			rpn(MIDI_RPN_NULL),
			pitchBendSensitivity(0) { }
	};

	/**
	 * This stores data about a specific source of MIDI data.
	 */
	struct MidiSource {
		// Whether this source sends music or SFX MIDI data.
		SourceType type;
		// The source volume (relative volume for this source as defined by the game).
		// Default is the default neutral value (255).
		uint16 volume;
		// The source volume level at which no scaling is performed (volume as defined
		// in MIDI data is used directly). Volume values below this decrease volume,
		// values above increase volume (up to the maximum MIDI channel volume).
		// Set this to match the volume values used by the game engine to avoid having
		// to convert them. Default value is 255; minimum value is 1.
		uint16 neutralVolume;
		// The volume level at which the fade started.
		uint16 fadeStartVolume;
		// The target volume level for the fade.
		uint16 fadeEndVolume;
		// How much us has passed since the start of the fade.
		int32 fadePassedTime;
		// The total duration of the fade (us).
		int32 fadeDuration;
		// The mapping of MIDI data channels to output channels for this source.
		int8 channelMap[MIDI_CHANNEL_COUNT];
		// Bitmask specifying which MIDI channels are available for use by this source.
		uint16 availableChannels;

		MidiSource() : type(SOURCE_TYPE_UNDEFINED), volume(DEFAULT_SOURCE_NEUTRAL_VOLUME),
				neutralVolume(DEFAULT_SOURCE_NEUTRAL_VOLUME), fadeStartVolume(0),
				fadeEndVolume(0), fadePassedTime(0), fadeDuration(0), availableChannels(0xFFFF) {
			memset(channelMap, 0, sizeof(channelMap));
		}
	};

	/**
	 * This stores information about a note currently playing on the MIDI
	 * device.
	 */
	struct ActiveNote {
		int8 source;
		uint8 channel;
		uint8 note;
		// True if the note is sustained. The note will turn off when the
		// sustain controller for the MIDI channel is turned off.
		bool sustain;

		ActiveNote() { clear(); }

		void clear() {
			source = 0x7F;
			channel = 0xFF;
			note = 0xFF;
			sustain = false;
		}

	};

	/**
	 * Stores data which is to be transmitted as a SysEx message to a MIDI
	 * device. Neither data nor length should include the SysEx start and stop
	 * bytes.
	 */
	struct SysExData {
		byte data[270];
		uint16 length;
		SysExData() : length(0) {
			memset(data, 0, sizeof(data));
		}
	};

public:
	// Callback hooked up to the driver wrapped by the MIDI driver
	// object. Executes onTimer and the external callback set by
	// the setTimerCallback function.
	static void timerCallback(void *data);

	MidiDriver_MT32GM(MusicType midiType);
	~MidiDriver_MT32GM();

	// MidiDriver interface
	int open() override;
	// Open the driver wrapping the specified MidiDriver instance.
	virtual int open(MidiDriver *driver, bool nativeMT32);
	void close() override;
	bool isOpen() const override { return _isOpen; }
	bool isReady() override {
		Common::StackLock lock(_sysExQueueMutex);
		return _sysExQueue.empty();
	}
	uint32 property(int prop, uint32 param) override;

	using MidiDriver_BASE::send;
	void send(uint32 b) override;
	void send(int8 source, uint32 b) override;
	void sysEx(const byte *msg, uint16 length) override;
	uint16 sysExNoDelay(const byte *msg, uint16 length) override;
	/**
	 * Puts a SysEx message on the SysEx queue. The message will be sent when
	 * the device is ready to receive it, without blocking the thread.
	 * Use the isReady function to determine if the SysEx has been sent. Other
	 * MIDI messages (not using the queue) should not be sent until the queue
	 * is empty.
	 */
	void sysExQueue(const byte *msg, uint16 length);
	/**
	 * Write data to an MT-32 memory location using a SysEx message.
	 * This function will add the necessary header and checksum bytes.
	 *
	 * @param msg Pointer to the data to write to a memory location
	 * @param length The data length
	 * @param targetAddress The start memory address in 8 bit format.
	 * Note that MT-32 memory addresses are sometimes specified in 7 bit format;
	 * these must be converted (f.e. System Area: 10 00 00 -> 04 00 00).
	 * @param queue Specify this parameter to use the SysEx queue to send the
	 * message (see sysExQueue for more information).
	 * @param delay Set this to false to disable the delay to ensure that the
	 * MT-32 has enough time to process the message. This parameter has no
	 * effect if queue is true.
	 * @return The delay in ms that must pass before the next SysEx message is
	 * sent to the MT-32. If delay or queue is true this will be 0; otherwise
	 * it is the caller's responsibility to make sure that the next SysEx is
	 * not sent before this time has passed.
	 */
	uint16 sysExMT32(const byte *msg, uint16 length, const uint32 targetAddress, bool queue = false, bool delay = true);
	void metaEvent(int8 source, byte type, byte *data, uint16 length) override;

	void stopAllNotes(bool stopSustainedNotes = false) override;
	/**
	 * Removes all SysEx messages in the SysEx queue.
	 */
	void clearSysExQueue();
	MidiChannel *allocateChannel() override;
	MidiChannel *getPercussionChannel() override;
	uint32 getBaseTempo() override;

	/**
	 * Allocates a number of MIDI channels for use by the specified source.
	 * By default this implements a simple algorithm which allocates the
	 * unallocated channel(s) with the lowest numbers. The channel numbers in
	 * the MIDI data sent by this source will be mapped to the allocated MIDI
	 * output channels. The function can be overridden to implement more
	 * complex channel allocation algorithms.
	 * Channels are freed when the source is deinitialized.
	 * Note that sources are not required to allocate channels, so if sources
	 * use conflicting MIDI channels, make sure to use this function
	 * consistently.
	 *
	 * @param source The source for which to allocate channels
	 * @param numChannels The number of channels to allocate
	 * @return True if allocation was successful, false otherwise (usually
	 * because insufficent channels were available)
	 */
	virtual bool allocateSourceChannels(uint8 source, uint8 numChannels);
	/**
	 * Deinitializes a source. This will abort active fades, free any output
	 * channels allocated to the source and stop active notes.
	 */
	void deinitSource(uint8 source) override;

protected:
	/**
	 * This will initialize the _controlData array with the default values for
	 * MT-32 or GM (depending on the _nativeMT32 value).
	 */
	virtual void initControlData();
	/**
	 * Initializes the MIDI device. Will call initMT32 or initGM.
	 */
	virtual void initMidiDevice();
	/**
	 * Initializes the MT-32 MIDI device. The device will be reset and,
	 * if the parameter is specified, set up for General MIDI data.
	 *
	 * @param initForGM True if the MT-32 should be initialized for GM mapping
	 */
	virtual void initMT32(bool initForGM);
	/**
	 * Initializes the General MIDI device. The device will be reset.
	 * If the initForMT32 parameter is specified, the device will be set up for
	 * MT-32 MIDI data. If the device supports Roland GS, the enableGS
	 * parameter can be specified for enhanced GS MT-32 compatiblity.
	 *
	 * @param initForMT32 True if the device should be initialized for MT-32 mapping
	 * @param enableGS True if the device should be initialized for GS MT-32 mapping
	 */
	virtual void initGM(bool initForMT32, bool enableGS);
	/**
	 * Processes a MIDI event. The type of event is determined and the
	 * corresponding function is called to handle the event.
	 * This function is called after mapping the MIDI data channel to an output
	 * channel, so the specified output channel is used and not the channel in
	 * the event bytes.
	 *
	 * @param source The source of the event
	 * @param b The event MIDI bytes
	 * @param outputChannel The output channel for the event
	 * @param controlData The control data set to use when processing the event
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. This will prevent the event from actually being sent
	 * to the MIDI device, but controlData will be updated. Default is false.
	 */
	virtual void processEvent(int8 source, uint32 b, uint8 outputChannel,
		MidiChannelControlData &controlData, bool channelLockedByOtherSource = false);
	/**
	 * Applies the controller default settings to the specified output channel
	 * for the specified source.
	 * This will set all default values specified on _controllerDefaults on the
	 * channel except sustain, which is set by deinitSource.
	 * 
	 * @param source The source triggering the default settings
	 * @param controlData The control data set to use when setting the defaults
	 * @param outputChannel The output channel on which the defaults should be
	 * set
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. This will prevent the defaults from actually being
	 * sent to the MIDI device, but controlData will be updated. Default is
	 * false.
	 */
	virtual void applyControllerDefaults(uint8 source, MidiChannelControlData &controlData, uint8 outputChannel, bool channelLockedByOtherSource);
	/**
	 * Processes a note on or off MIDI event.
	 * This will apply source volume if necessary, update the active note
	 * registration and send the event to the MIDI device.
	 *
	 * @param outputChannel The MIDI output channel for the event
	 * @param command The MIDI command byte
	 * @param controlData The control data set that will be used for applying
	 * source volume
	 */
	virtual void noteOnOff(byte outputChannel, byte command, byte note, byte velocity,
		int8 source, MidiChannelControlData &controlData);
	/**
	 * Processes a polyphonic aftertouch MIDI event.
	 * This implementation will just send the event to the MIDI device.
	 * 
	 * @param outputChannel The MIDI output channel for the event
	 * @param note The note on which aftertouch should be applied
	 * @param pressure The amount of pressure which should be applied
	 * @param source The source of the event
	 * @param controlData The control data set for the MIDI channel
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. Default is false.
	 */
	virtual void polyAftertouch(byte outputChannel, byte note, byte pressure,
		int8 source, MidiChannelControlData &controlData, bool channelLockedByOtherSource = false);
	/**
	 * Process a control change MIDI event.
	 * This will update the specified control data set and apply other
	 * processing if necessary, and then send the event to the MIDI device.
	 *
	 * @param outputChannel The MIDI output channel for the event
	 * @param controlData The control data set that the new controller value
	 * should be stored on
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. Default is false.
	 */
	virtual void controlChange(byte outputChannel, byte controllerNumber, byte controllerValue,
		int8 source, MidiChannelControlData &controlData, bool channelLockedByOtherSource = false);
	/**
	 * Process a program change MIDI event.
	 * This will update the specified control data set, apply MT-32 <> GM
	 * instrument mapping and other processing, and send the event to the MIDI
	 * device.
	 *
	 * @param outputChannel The MIDI output channel for the event
	 * @param controlData The control data set that the new program value
	 * should be stored on
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. Default is false.
	 */
	virtual void programChange(byte outputChannel, byte patchId, int8 source,
		MidiChannelControlData &controlData, bool channelLockedByOtherSource = false);
	/**
	 * Processes a channel aftertouch MIDI event.
	 * This whil update the specified control data set and send the event to
	 * the MIDI device.
	 * 
	 * @param outputChannel The MIDI output channel for the event
	 * @param pressure The amount of pressure which should be applied
	 * @param source The source of the event
	 * @param controlData The control data set for the MIDI channel
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. Default is false.
	 */
	virtual void channelAftertouch(byte outputChannel, byte pressure, int8 source,
		MidiChannelControlData &controlData, bool channelLockedByOtherSource = false);
	/**
	 * Processes a pitch bend MIDI event.
	 * This whil update the specified control data set and send the event to
	 * the MIDI device.
	 * 
	 * @param outputChannel The MIDI output channel for the event
	 * @param pitchBendLsb The pitch bend LSB
	 * @param pitchBendMsb The pitch bend MSB
	 * @param source The source of the event
	 * @param controlData The control data set for the MIDI channel
	 * @param channelLockedByOtherSource True if the output channel is locked
	 * by another source. Default is false.
	 */
	virtual void pitchBend(byte outputChannel, uint8 pitchBendLsb, uint8 pitchBendMsb,
		int8 source, MidiChannelControlData &controlData, bool channelLockedByOtherSource = false);

	/**
	 * Adds a note to the active note registration.
	 */
	virtual bool addActiveNote(uint8 outputChannel, uint8 note, int8 source);
	/**
	 * Removes a note from the active note registration.
	 */
	virtual bool removeActiveNote(uint8 outputChannel, uint8 note, int8 source);
	/**
	 * Removes all sustained or all non-sustained notes on the specified MIDI
	 * channel from the active note registration.
	 */
	virtual void removeActiveNotes(uint8 outputChannel, bool sustainedNotes);
	/**
	 * Returns true if the MIDI device uses the specified MIDI channel.
	 */
	bool isOutputChannelUsed(int8 outputChannel);
	/**
	 * Maps the specified MT-32 instrument to an equivalent GM instrument.
	 * This implementation looks up the instrument in the _mt32ToGMInstrumentMap
	 * array. Override this function to implement more complex mapping schemes.
	 */
	virtual byte mapMT32InstrumentToGM(byte mt32Instrument);
	/**
	 * Maps the specified GM instrument to an equivalent MT-32 instrument.
	 * This implementation looks up the instrument in the _gmToMT32InstrumentMap
	 * array. Override this function to implement more complex mapping schemes.
	 */
	virtual byte mapGMInstrumentToMT32(byte gmInstrument);
	/**
	 * Checks if the currently selected GS bank / instrument variation
	 * on the specified channel is valid for the specified patch.
	 * If this is not the case, the correct bank will be returned which
	 * can be set by sending a bank select message. If no correction is
	 * needed, 0xFF will be returned.
	 * This emulates the fallback functionality of the Roland SC-55 v1.2x,
	 * on which some games rely to correct wrong bank selects.
	 */
	byte correctInstrumentBank(byte outputChannel, byte patchId);

	/**
	 * Returns the MIDI output channel mapped to the specified data channel.
	 * If the data channel has not been mapped yet, a new mapping to one of the
	 * output channels available to the source will be created.
	 *
	 * @param source The source using the data channel
	 * @param dataChannel The data channel to map
	 * @return The mapped output channel, or -1 if no mapping is possible
	*/
	virtual int8 mapSourceChannel(uint8 source, uint8 dataChannel);

	void applySourceVolume(uint8 source) override;
	void stopAllNotes(uint8 source, uint8 channel) override;
	/**
	 * Runs the MIDI driver's timer related functionality. Will update volume
	 * fades and sends messages from the SysEx queue if necessary.
	 */
	void onTimer() override;

	Common::Mutex _allocationMutex; // For operations on MIDI channel allocation
	Common::Mutex _activeNotesMutex; // For operations on active notes registration

	// The wrapped MIDI driver.
	MidiDriver *_driver;
	// The type of MIDI data supplied to the driver: MT-32 or General MIDI.
	MusicType _midiType;
	// True if the MIDI output is an MT-32 (hardware or 100% emulated),
	// false if the MIDI output is a General MIDI device.
	bool _nativeMT32;
	// True if the General MIDI output supports Roland GS for improved MT-32 mapping.
	bool _enableGS;
	// Indicates if the stereo panning in the MIDI data is reversed
	// compared to the stereo panning of the intended MIDI device.
	bool _midiDataReversePanning;
	// Indicates if the stereo panning of the output MIDI device is
	// reversed compared to the stereo panning of the type of MIDI
	// device targeted by the MIDI data (i.e. MT-32 data playing on
	// a GM device or the other way around).
	bool _midiDeviceReversePanning;
	// True if GS percussion channel volume should be scaled to match MT-32 volume.
	bool _scaleGSPercussionVolumeToMT32;

	// True if this MIDI driver has been opened.
	bool _isOpen;
	// Bitmask of the MIDI channels in use by the output device.
	uint16 _outputChannelMask;
	int _baseFreq;

	// stores the controller values for each MIDI channel
	MidiChannelControlData *_controlData[MIDI_CHANNEL_COUNT];
	// The mapping of MIDI data channels to output channels for each source.
	int8 _channelMap[MAXIMUM_SOURCES][MIDI_CHANNEL_COUNT];
	// Bitmask specifying which MIDI channels are available for use by each source.
	uint16 _availableChannels[MAXIMUM_SOURCES];

	// Maps used for MT-32 <> GM instrument mapping. Set these to an alternate
	// 128 byte array to customize the mapping.
	const byte *_mt32ToGMInstrumentMap;
	const byte *_gmToMT32InstrumentMap;
	// The maximum active notes for the current MIDI device.
	uint8 _maximumActiveNotes;
	// Active note registration
	ActiveNote *_activeNotes;

	// The current number of microseconds that have to elapse before the next
	// SysEx message can be sent.
	uint32 _sysExDelay;
	// Queue of SysEx messages to be sent to the MIDI device.
	Common::Queue<SysExData> _sysExQueue;
	// Mutex for write access to the SysEx queue.
	Common::Mutex _sysExQueueMutex;
};
/** @} */
#endif
