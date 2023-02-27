// Copyright 2023 Northern.tech AS
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#include "optimized_writer.hpp"
#include <iostream>

using namespace mender;

OptimizedWriter::OptimizedWriter(
	io::FileReader &reader, io::FileReadWriterSeeker &writer, size_t blockSize, size_t limit) :
	mBlockSize(blockSize),
	mReader(reader),
	mReadWriter(writer),
	mInputLimit(limit) {
}

Error OptimizedWriter::Copy() {
	mStatistics.mBlocksWritten = 0;
	mStatistics.mBlocksOmitted = 0;
	mStatistics.mBytesWritten = 0;

	io::Bytes rv;
	rv.resize(mBlockSize);
	io::Bytes wv;
	wv.resize(mBlockSize);

	while (true) {
		if (rv.size() != mBlockSize) {
			rv.resize(mBlockSize);
		}

		auto pos = mReader.Tell();
		if (!pos) {
			return pos.error();
		}
		auto position = pos.value();

		if (mInputLimit && ((position + mBlockSize) > mInputLimit)) {
			return NoError;
		}

		auto result = mReader.Read(rv);
		if (!result) {
			return result.error();
		} else if (result.value() == 0) {
			return NoError;
		} else if (result.value() > rv.size()) {
			return mender::common::error::MakeError(
				mender::common::error::ProgrammingError,
				"Read returned more bytes than requested. This is a bug in the Read function.");
		}

		auto readBytes = result.value();
		if (readBytes != rv.size()) {
			// Because we only ever resize down, this should be very cheap. Resizing
			// back up to capacity below is then also cheap.
			rv.resize(readBytes);
		}

		if (wv.size() != readBytes) {
			wv.resize(readBytes);
		}

		bool skipWriting = false;

		if (NoError != mReadWriter.SeekSet(position)) {
			return Error(
				std::error_condition(std::errc::io_error),
				"Failed to set seek on the destination file");
		}

		auto readResult = mReadWriter.Read(wv);
		if (readResult && readResult.value() == readBytes) {
			wv.resize(readResult.value());
			skipWriting = std::equal(rv.begin(), rv.end(), wv.data());
			if (skipWriting) {
				++mStatistics.mBlocksOmitted;
			}
		}

		if (!skipWriting && !mBypassWriting) {
			mReadWriter.SeekSet(position);
			auto res = mReadWriter.Write(rv);
			if (res) {
				++mStatistics.mBlocksWritten;
				mStatistics.mBytesWritten += res.value();
			} else if (result.value() == 0) {
				return Error(
					std::error_condition(std::errc::io_error), "Zero write when copying data");
			} else if (result.value() != rv.size()) {
				return Error(
					std::error_condition(std::errc::io_error), "Short write when copying data");
			}
		}
	}
	return NoError;
}
