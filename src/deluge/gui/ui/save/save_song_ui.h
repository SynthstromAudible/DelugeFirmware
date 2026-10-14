/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/ui/save/save_ui.h"
#include "io/file.hpp"
#include "storage/readback_check.h"
#include "storage/save_dispatch.h"
#include "util/boot.h"
#include <expected>

class AudioFile;
#include <string>
#include <vector>

class ContextMenu;

class SaveSongUI final : public SaveUI {
	/// The sample-collect gate drives the real ENTER gesture and turns collect-media on.
	friend class SampleCollectProbe;
	/// The sample-load tree gate saves its fixture song through the same gesture, with collect-media on.
	friend class SampleLoadTreeProbe;

public:
	SaveSongUI();
	bool opened() override;
	/// @brief The file a save under the entered name writes: songs save in kSongSaveFormat, whatever the selected
	/// file was saved in.
	[[nodiscard]] std::string getSaveFilePath() override;
	void focusRegained() override;
	// void selectEncoderAction(int8_t offset);
	/// @brief Start a song save: check the target name on a later tick, then plan and collect.
	[[nodiscard]] bool beginUnstagedSave(const deluge::storage::SaveRequest& request) override;

	/// @brief What one AudioFile needs done, decided but not yet performed.
	///
	/// ONE ENTRY PER FILE carrying up to TWO operations, because the second's input depends on the
	/// first's OUTCOME. A recording still at its temporary path is renamed into place, and that rename
	/// clears `tempFilePathForRecording` on success -- which is exactly what the copy then consults to
	/// choose its source. A failed rename is not fatal: it warns, leaves the temporary path set, and
	/// the copy reads from there instead. A flat list of independent transfers cannot express that.
	struct PlannedTransfer {
		AudioFile* file{};

		/// Set when a recording must be moved out of its temporary location first.
		bool renameTemp = false;
		std::string renameFrom;
		std::string renameTo;

		/// Set when the file must also be copied to or from an alternate location.
		bool copy = false;
		/// The source to use IF the rename succeeded, or if there was none.
		std::string copyFromAfterRename;
		/// The source to use if the rename FAILED and the temporary file is still where it was.
		std::string copyFromIfRenameFailed;
		std::string copyTo;

		/// What `filePath` becomes, empty if unchanged. COMPUTED here, applied later.
		std::string newFilePath;
		/// What `loadedFromAlternatePath` becomes.
		std::string newAlternatePath;
		/// Whether `loadedFromAlternatePath` must be CLEARED, which is what a successful copy does
		/// when this save is not collecting media. Distinct from "unchanged", hence not just an empty
		/// string above.
		bool clearAlternatePath = false;
		/// Whether `tempFilePathForRecording` must be cleared -- the rename's own mutation, deferred.
		bool clearTempPath = false;
	};

	/// @brief Decide what every referenced audio file needs, WITHOUT performing or applying any of it.
	///
	/// @param into               Receives one entry per file that needs work.
	/// @param collectingSamples  Whether this save is collecting media into the song's own folder.
	/// @param songAlternatePath  The song's alternate-media folder, as the existence resolution built it.
	/// @param alternateDirLength Its length, which setupAlternateAudioFilePath wants.
	/// @return false if a path could not be built, which fails the save exactly as it did before.
	[[nodiscard]] bool planTransfers(std::vector<PlannedTransfer>& into, bool collectingSamples,
	                                 const std::string& songAlternatePath, int32_t alternateDirLength);

	/// @brief Drive one step of a sample collection. Called from SaveUI::saveRoutine().
	void collectRoutine();

	/// @brief Whether a sample collection is in flight. NOT the same as the save UI being open.
	[[nodiscard]] bool collectInFlight() const { return collectPhase_ != CollectPhase::NONE; }

	/// @brief Drive the staged document write one step.
	void docRoutine();

	/// @brief Whether a staged document write is in flight. NOT the same as the UI being open.
	[[nodiscard]] bool docInFlight() const { return docPhase_ != DocPhase::NONE; }

	/// @brief Document steps the CHANNEL served, and the ones the INLINE arm served before boot finished.
	///
	/// Reported together and held to account for each other by the gate: on a build where half the
	/// steps quietly took the other arm, either number alone reads as a success.
	[[nodiscard]] static int32_t documentChannelServedOps() { return documentChannelServedOps_; }
	[[nodiscard]] static int32_t documentInlineServedOps() { return documentInlineServedOps_; }

	/// @brief Document steps the whole-document write served: the write from the staged buffer, and nothing else.
	///
	/// Every other document step rides the byte channel's copying operations. The song-save gate requires this above
	/// zero: the document write went through the channel from the staged buffer.
	[[nodiscard]] static int32_t documentWriteChannelServedOps() { return documentWriteChannelServedOps_; }

	/// @brief Card steps the staged document write took, whichever arm carried them.
	[[nodiscard]] static int32_t documentSteps() { return documentSteps_; }

	/// @brief Staged saves that ran all the way to completion.
	///
	/// A save that fails its read-back has ALREADY written its file, so the file being present says
	/// nothing about whether the save succeeded. Only reaching the end says that.
	[[nodiscard]] static int32_t documentSavesCompleted() { return documentSavesCompleted_; }

	/// @brief Ticks on which a document step went through a channel, after boot.
	[[nodiscard]] static int32_t channelDocumentTicks() { return channelDocumentTicks_; }

	/// @brief The document machine's stall bound, in microseconds (`deluge::io::kChannelStallUs`).
	///
	/// Read-only, for a gate that must hold a document step past it to prove the save does not give up.
	[[nodiscard]] uint64_t documentStallLimitUs() const { return docStallBound_.limitUs(); }

	/// @brief How many times the staging buffer doubled for the last staged document.
	///
	/// Reported because a fixture whose song never exceeds the buffer's initial 32768 bytes leaves
	/// the growth path unexercised and every growth check vacuous.
	[[nodiscard]] static int32_t stagedDocumentGrowths() { return stagedDocumentGrowths_; }

	/// @brief Transfers the CHANNEL served, and ticks on which a transfer step went through it, after boot.
	[[nodiscard]] static int32_t transferChannelServedOps() { return transferChannelServedOps_; }
	[[nodiscard]] static int32_t channelTransferTicks() { return channelTransferTicks_; }

	/// @brief Transfers the INLINE arm served, synchronously, before boot finished. Zero on a booted build.
	///
	/// The counterpart to transferChannelServedOps(). Reported alongside it because the two must
	/// account for every transfer between them: a build where half the files quietly took the other
	/// arm reads as a success from either number alone.
	[[nodiscard]] static int32_t transferInlineServedOps() { return transferInlineServedOps_; }

private:
	/// Which part of a sample collection runs next.
	///
	/// The song save's work before its document write: the existence check on the target name, then
	/// the sample collection.
	/// Separate from SaveUI::SavePhase deliberately: the song save's flow differs enough that one
	/// shared enum would make both harder to read.
	enum class CollectPhase : uint8_t {
		NONE,     ///< No collection in flight.
		EXISTS,   ///< Asking whether the target name is taken, before anything is planned or copied.
		TRANSFER, ///< Moving and copying, one entry at a time.
		APPLY,    ///< Writing the decided paths back, in one step.
		FINISH,   ///< Handing the save on to the document machine.
	};

	/// Which part of the staged document write runs next.
	///
	/// Separate from CollectPhase for the reason the collection's enum is separate from the staged
	/// preset save's: the flows differ, and sharing one would make both harder to follow. TEMPNAME,
	/// UNLINK and RENAME have no counterpart in a preset save.
	enum class DocPhase : uint8_t {
		NONE,      ///< No staged document write in flight.
		SERIALIZE, ///< Building the document in memory, on the interaction tier.
		TEMPNAME,  ///< Looking for a free SONGS/TEMPnnnn name. Overwrite path only.
		WRITE,     ///< One whole-document write.
		VERIFY,    ///< Reading the written file back: length, head and tail.
		UNLINK,    ///< Removing the file being replaced. Overwrite path only.
		RENAME,    ///< Moving the temp file onto the target. Overwrite path only.
		FINISH,    ///< Closing the save and reporting to the user.
	};

	/// @brief Resolve the target-name existence check by one step.
	void runExistsPhase();
	/// @brief Act on a known existence answer: refuse, prompt, or plan and start the collection.
	void resolveExists(const std::expected<bool, deluge::io::Status>& presence);
	/// @brief End an existence check that stopped making progress, failing the dispatch.
	void abandonExists();
	void runTransferPhase();
	void applyTransfers();
	/// @brief End the collection having applied NOTHING, and fail the save.
	void abandonCollection();
	/// @brief Perform the current entry with blocking calls. Legal ONLY before boot finishes (the inline arm).
	bool transferOneSynchronously();
	/// @brief Defer one more tick, or give up if the collection has stopped making progress.
	bool deferCollection();
	/// @brief Everything the save does after its samples are in place.
	bool finishSaveAfterCollection();

	/// @brief The serialiser songs are saved through (kSongSaveFormat), as the FileWriter base, which is where the
	/// memory-based surface lives.
	static FileWriter& activeSongWriter();

	/// @brief Build the whole song document into the shared serialiser's memory buffer.
	///
	/// Pure computation: touches no storage, which is what makes it legal on the interaction tier.
	/// @return false if the document could not be built, in which case nothing was staged.
	[[nodiscard]] bool serializeSongToBuffer();

	/// @brief Hand the shared serialiser back to writing through a file.
	///
	/// MUST run on every exit path. The serialiser is global; left memory-based it silently stops
	/// reaching the card for every writer after this save.
	void releaseSongWriter();

	void runSerializePhase();
	void runTempNamePhase();
	void runWritePhase();
	void runVerifyPhase();
	/// @brief What the written song must match: its own head, and its closing tag when it has one.
	[[nodiscard]] deluge::storage::ReadBackExpectation songReadBackExpectation() const;
	void runUnlinkPhase();
	void runRenamePhase();
	void runDocFinishPhase();
	/// @brief End the staged write having failed, releasing the serialiser and reporting the error.
	void abandonDocument(Error error);
	/// @brief Retry next tick, or give up if the stall bound is exhausted.
	[[nodiscard]] bool deferDocument();
	/// @brief Whether this step must run inline: during boot, when nothing yet answers a channel.
	[[nodiscard]] static bool docInline();
	/// @brief Run the current phase synchronously, during boot. @return false if the step failed.
	[[nodiscard]] bool runOneDocStep();
	/// @brief One document step, blocking. Only legal during boot.
	[[nodiscard]] bool docStepSynchronously();

	CollectPhase collectPhase_{CollectPhase::NONE};
	std::vector<PlannedTransfer> transfers_;
	size_t transferCursor_{};
	bool renameIssued_{};
	bool copyIssued_{};
	/// Whether the CURRENT entry's rename succeeded, which decides where its copy reads from.
	bool renameSucceeded_{true};
	deluge::io::StallBound collectStallBound_{};

	/// @brief The staged song document, pointing into the shared serialiser's buffer.
	///
	/// Borrowed, not owned: the buffer belongs to the serialiser, which is handed back by
	/// releaseSongWriter(). Valid only between serializeSongToBuffer() and that release.
	const uint8_t* stagedDocument_{};
	uint32_t stagedLength_{};

	DocPhase docPhase_{DocPhase::NONE};
	/// The channel read-back of the written document. The stall bound stays in deferDocument().
	deluge::storage::ReadBackCheck docReadBack_;
	std::string docWritePath_; ///< Where the bytes actually go: the temp name, or the target.
	int32_t docTempNumber_{};
	deluge::io::StallBound docStallBound_{};
	bool docWriteIssued_{};

	/// The bytes the head and tail must match, copied verbatim from the strings the streaming writer
	/// passed to closeFileAfterWriting -- including their newlines. The tail's LEADING and trailing
	/// newline are both load-bearing: the document's last byte is a newline, so a comparison against
	/// a bare "</song>" reads seven bytes ending one short and never matches.
	static constexpr const char* kSongHeadXml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n";
	static constexpr const char* kSongHeadJson = "{\"song\": {\n";
	static constexpr const char* kSongTailXml = "\n</song>\n";

	static inline int32_t documentChannelServedOps_{};
	static inline int32_t documentInlineServedOps_{};
	static inline int32_t documentWriteChannelServedOps_{};
	static inline int32_t documentSteps_{};
	static inline int32_t documentSavesCompleted_{};
	static inline int32_t channelDocumentTicks_{};
	static inline int32_t stagedDocumentGrowths_{};

	/// Persisted across ticks: this save resolves over many calls, not one, so these locals must live
	/// as members rather than on a call stack.
	std::string pendingFilePath_;
	bool pendingFileAlreadyExisted_{};
	/// The dispatch this song save is resolving. Held from beginUnstagedSave() until the existence
	/// answer ends it, because the answer arrives on a later tick than the request.
	deluge::storage::SaveRequest existsRequest_{};
	bool anyErrorMovingTempFiles_{};

	static inline int32_t transferChannelServedOps_{};
	static inline int32_t transferInlineServedOps_{};
	static inline int32_t channelTransferTicks_{};

public:
	bool collectingSamples{};
	[[nodiscard]] bool collectingSamplesForDispatch() const override { return collectingSamples; }
	// ui
	UIType getUIType() override { return UIType::SAVE_SONG; }

protected:
	void onBrowserOpened() override;
	// int32_t arrivedInNewFolder(int32_t direction);
};

extern SaveSongUI saveSongUI;
