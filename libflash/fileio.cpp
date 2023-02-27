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

#include "fileio.hpp"

#include <sstream>

mender::io::FileReader::FileReader(mender::io::File f) :
	mFd(f) {
}

ExpectedSize mender::io::FileReader::Read(vector<uint8_t> &dst) {
	return mender::io::Read(mFd, dst);
}

ExpectedSize mender::io::FileReader::Tell() const {
	return mender::io::Tell(mFd);
}

mender::io::InputStreamReader::InputStreamReader() :
	FileReader(GetInputStream()) {
}

ExpectedSize mender::io::InputStreamReader::Read(vector<uint8_t> &dst) {
	auto res = FileReader::Read(dst);
	if (!res) {
		return res.error();
	}
	mReadBytes += res.value();
	return res;
}

ExpectedSize mender::io::InputStreamReader::Tell() const {
	return mReadBytes;
}

mender::io::LimitedFlushingWriter::LimitedFlushingWriter(
	mender::io::File f, size_t limit, uint32_t flushInterval) :
	FileWriter(f),
	mWritingLimit(limit),
	mFlushIntervalBytes(flushInterval) {
}

ExpectedSize mender::io::LimitedFlushingWriter::Write(const vector<uint8_t> &dst) {
	auto pos = mender::io::Tell(mFd);
	if (!pos) {
		return pos.error();
	}
	if (mWritingLimit && pos.value() + dst.size() > mWritingLimit) {
		return Error(std::error_condition(std::errc::io_error), "Error writing beyound the limit");
	}
	auto res = FileWriter::Write(dst);
	if (res) {
		mUnflushedBytesWritten += res.value();
		if (mUnflushedBytesWritten >= mFlushIntervalBytes) {
			auto flushRes = mender::io::Flush(mFd);
			if (NoError != flushRes) {
				return Error(std::error_condition(std::errc::io_error), flushRes.message);
			} else {
				mUnflushedBytesWritten = 0;
			}
		}
	}
	return res;
}

mender::io::FileWriter::FileWriter(File f) :
	mFd(f) {
}

ExpectedSize mender::io::FileWriter::Write(const vector<uint8_t> &dst) {
	return mender::io::Write(mFd, dst);
}

mender::io::FileReadWriter::FileReadWriter(File f) :
	mFd(f) {
}

ExpectedSize mender::io::FileReadWriter::Read(vector<uint8_t> &dst) {
	return mender::io::Read(mFd, dst);
}

ExpectedSize mender::io::FileReadWriter::Write(const vector<uint8_t> &dst) {
	return mender::io::Write(mFd, dst);
}

mender::io::FileReadWriterSeeker::FileReadWriterSeeker(FileWriter &writer) :
	FileReadWriter(writer.GetFile()),
	mWriter(writer) {
}

ExpectedSize mender::io::FileReadWriterSeeker::Write(const vector<uint8_t> &dst) {
	return mWriter.Write(dst);
}

Error mender::io::FileReadWriterSeeker::SeekSet(uint64_t pos) {
	return mender::io::SeekSet(mFd, pos);
}

ExpectedSize mender::io::FileReadWriterSeeker::Tell() const {
	return mender::io::Tell(mFd);
}
