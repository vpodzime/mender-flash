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

#include <gtest/gtest.h>
#include <filesystem>
#include "fileio.hpp"
#include "testing.hpp"
#include "optimized_writer.hpp"

class OptimizedWriterTest : public testing::Test {
protected:
	void SetUp() override {
	}
};

class StringFileReader : public mender::io::FileReader {
public:
	StringFileReader(const std::string &str) :
		mender::io::FileReader(-1),
		mString(str) {
	}
	virtual ExpectedSize Tell() const override {
		return mBytesRead;
	}
	virtual ExpectedSize Read(vector<uint8_t> &dst) override {
		size_t bytes_to_copy = std::min(dst.size(), mString.size() - mBytesRead);
		for (size_t i = 0; i < bytes_to_copy; i++) {
			dst[i] = mString[i];
		}
		mBytesRead += bytes_to_copy;
		return bytes_to_copy;
	}

private:
	std::string mString;
	size_t mBytesRead {0};
};

TEST_F(OptimizedWriterTest, TestFlushingLimitWriterWrite) {
	// prepare a temp dir
	mender::common::testing::TemporaryDirectory tempDir;
	auto path = tempDir.Path() + "/foo";

	// Write some junk to the file
	auto s = mender::io::WriteFile(path, {'a', 'b', 'x', 'd', 'r', 'z', '1', '2', '3', '4'});
	ASSERT_TRUE(s) << s.error().message;

	auto f = mender::io::Open(path, true, true);
	ASSERT_TRUE(f) << f.error().message;
	mender::io::File fd = f.value();

	// set a limit to 10bytes
	mender::io::LimitedFlushingWriter writer(fd, 10);

	mender::io::Bytes payloadBuf {'f', 'o', 'o', 'b', 'a', 'r'};
	auto expectedBytesWritten = payloadBuf.size();

	auto res = writer.Write(payloadBuf);
	ASSERT_TRUE(res) << res.error().message;
	ASSERT_EQ(res.value(), expectedBytesWritten);

	auto err = mender::io::Close(fd);
	ASSERT_EQ(err, NoError);
}

TEST_F(OptimizedWriterTest, TestFlushingLimitWriterWriteNegative) {
	// prepare a temp dir
	mender::common::testing::TemporaryDirectory tempDir;
	auto path = tempDir.Path() + "/foo";

	auto f = mender::io::Open(path, true, true);
	ASSERT_TRUE(f) << f.error().message;
	mender::io::File fd = f.value();

	// set a limit to 10bytes
	mender::io::LimitedFlushingWriter writer(fd, 10);

	// create a 12 byte buffer
	mender::io::Bytes payloadBuf {'f', 'o', 'o', 'b', 'a', 'r', 'f', 'o', 'o', 'b', 'a', 'r'};

	auto res = writer.Write(payloadBuf);
	ASSERT_FALSE(res) << "Data written beyound the device limit";

	auto err = mender::io::Close(fd);
	ASSERT_EQ(err, NoError);
}

TEST_F(OptimizedWriterTest, TestOptimizedWriter) {
	// prepare a temp dir
	mender::common::testing::TemporaryDirectory tempDir;
	auto path = tempDir.Path() + "/foo";

	std::stringstream ss;
	ss << "dd if=/dev/urandom of=" << path << " bs=1M count=10 status=none";
	system(ss.str().c_str());

	// create a reader
	auto f = mender::io::Open(path, true, false);
	ASSERT_TRUE(f) << f.error().message;
	mender::io::File fd = f.value();
	mender::io::FileReader reader(fd);

	// create writer
	auto path2 = path + ".copy";
	auto f2 = mender::io::Open(path2, true, true);
	ASSERT_TRUE(f2) << f.error().message;
	mender::io::File fd2 = f2.value();
	mender::io::FileWriter writer(fd2);

	// create read-writer
	mender::io::FileReadWriterSeeker readWriter(writer);

	// creawte optimized-writer
	mender::OptimizedWriter optWriter(reader, readWriter);
	auto copyRes = optWriter.Copy();
	ASSERT_EQ(copyRes, NoError) << copyRes.message;

	auto stats = optWriter.GetStatistics();
	ASSERT_EQ(stats.mBlocksWritten, 10);
	ASSERT_EQ(stats.mBlocksOmitted, 0);
	ASSERT_EQ(stats.mBytesWritten, 10 * 1024 * 1024);

	mender::io::SeekSet(fd, 0);
	mender::io::SeekSet(fd2, 0);

	// create optimized-writer
	auto copyRes2 = optWriter.Copy();
	ASSERT_EQ(copyRes2, NoError) << copyRes.message;

	auto stats2 = optWriter.GetStatistics();
	ASSERT_EQ(stats2.mBlocksWritten, 0);
	ASSERT_EQ(stats2.mBlocksOmitted, 10);
	ASSERT_EQ(stats2.mBytesWritten, 0);

	auto err = mender::io::Close(fd);
	ASSERT_EQ(err, NoError);
	auto err2 = mender::io::Close(fd2);
	ASSERT_EQ(err, NoError);
}

TEST_F(OptimizedWriterTest, TestOptimizedWriterLimit) {
	// prepare a temp dir
	mender::common::testing::TemporaryDirectory tempDir;

	struct {
		std::string input;
		int inputSize;
		int blockSize;
		int expectedBlockWritten;
		int expectedBytesWritten;
	} tests[] = {{"foobarfoobarfoobar", 10, 6, 1, 6}, {"fo", 0, 2, 1, 2}, {"foobar", 0, 4, 2, 6}};

	for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
		auto path = tempDir.Path() + "/foo" + std::to_string(i);
		StringFileReader reader(tests[i].input);

		// create writer
		auto f = mender::io::Open(path, true, true);
		ASSERT_TRUE(f) << f.error().message;
		mender::io::File fd = f.value();
		mender::io::FileWriter writer(fd);

		// create read-writer
		mender::io::FileReadWriterSeeker readWriter(writer);

		// create optimized-writer
		mender::OptimizedWriter optWriter(
			reader, readWriter, tests[i].blockSize, tests[i].inputSize);
		optWriter.Copy();

		auto stats = optWriter.GetStatistics();
		ASSERT_EQ(stats.mBlocksWritten, tests[i].expectedBlockWritten);
		ASSERT_EQ(stats.mBlocksOmitted, 0);
		ASSERT_EQ(stats.mBytesWritten, tests[i].expectedBytesWritten);

		auto err = mender::io::Close(fd);
		ASSERT_EQ(err, NoError);
	}
}

int main(int argc, char *argv[]) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
