#define BOOST_TEST_DYN_LINK

#define BOOST_TEST_MODULE SequenceDeMultiplexerTest
#include <boost/test/unit_test.hpp>
using namespace boost::unit_test_framework;

#include <iostream>
#include <sstream>

#include "BackendFactory.h"
#include "Device.h"
#include "DummyBackend.h"
#include "MapFileParser.h"
#include "NumericAddressedBackendMuxedRegisterAccessor.h"
#include "TwoDRegisterAccessor.h"

namespace ChimeraTK {
  using namespace ChimeraTK;
}
using namespace ChimeraTK;

static const std::string DMAP_FILE_NAME("dummies.dmap");
static const std::string DEVICE_ALIAS("SEQUENCES");
static const std::string DEVICE_INVALID_SEQUENCES_ALIAS("INVALID_SEQUENCES");
static const std::string DEVICE_MIXED_ALIAS("MIXED_SEQUENCES");

static const std::string MAP_FILE_NAME("sequences.map");
static const std::string TEST_MODULE_NAME("TEST");
static const std::string INVALID_MODULE_NAME("INVALID");
static const RegisterPath TEST_MODULE_PATH(TEST_MODULE_NAME);
static const RegisterPath INVALID_MODULE_PATH(INVALID_MODULE_NAME);

BOOST_AUTO_TEST_SUITE(SequenceDeMultiplexerTestSuite)

BOOST_AUTO_TEST_CASE(testConstructor) {
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_ALIAS);
  TwoDRegisterAccessor<double> deMultiplexer = device.getTwoDRegisterAccessor<double>(TEST_MODULE_PATH / "FRAC_INT");
  BOOST_CHECK(deMultiplexer[0].size() == 5);

  device.close();
  device.open(DEVICE_INVALID_SEQUENCES_ALIAS);

  BOOST_CHECK_THROW(device.getTwoDRegisterAccessor<double>(INVALID_MODULE_PATH / "NO_WORDS"), ChimeraTK::logic_error);
  BOOST_CHECK_THROW(device.getTwoDRegisterAccessor<double>(INVALID_MODULE_PATH / "WRONG_SIZE"), ChimeraTK::logic_error);
  BOOST_CHECK_THROW(
      device.getTwoDRegisterAccessor<double>(INVALID_MODULE_PATH / "WRONG_NELEMENTS"), ChimeraTK::logic_error);
  BOOST_CHECK_THROW(
      device.getTwoDRegisterAccessor<double>(INVALID_MODULE_PATH / "DOES_NOT_EXIST"), ChimeraTK::logic_error);
}

// test the de-multiplexing itself, with 'identity' fixed point conversion
template<class SequenceWordType>
void testDeMultiplexing(std::string areaName) {
  // open a dummy device with the sequence map file
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_ALIAS);

  std::vector<SequenceWordType> ioBuffer(15);
  auto area = device.getOneDRegisterAccessor<int32_t>(TEST_MODULE_NAME + "/AREA_MULTIPLEXED_SEQUENCE_" + areaName);
  size_t nBytes = std::min(area.getNElements() * sizeof(int32_t), 15 * sizeof(SequenceWordType));
  ioBuffer[0] = 'A';
  ioBuffer[1] = 'a';
  ioBuffer[2] = '0';
  ioBuffer[3] = 'B';
  ioBuffer[4] = 'b';
  ioBuffer[5] = '1';
  ioBuffer[6] = 'C';
  ioBuffer[7] = 'c';
  ioBuffer[8] = '2';
  ioBuffer[9] = 'D';
  ioBuffer[10] = 'd';
  ioBuffer[11] = '3';
  ioBuffer[12] = 'E';
  ioBuffer[13] = 'e';
  ioBuffer[14] = '4';
  memcpy(&(area[0]), ioBuffer.data(), nBytes);
  area.write();

  TwoDRegisterAccessor<SequenceWordType> deMultiplexer =
      device.getTwoDRegisterAccessor<SequenceWordType>(TEST_MODULE_PATH / areaName);

  BOOST_CHECK(deMultiplexer.isReadOnly() == false);
  BOOST_CHECK(deMultiplexer.isReadable());
  BOOST_CHECK(deMultiplexer.isWriteable());

  deMultiplexer.read();

  BOOST_CHECK(deMultiplexer[0][0] == 'A');
  BOOST_CHECK(deMultiplexer[0][1] == 'B');
  BOOST_CHECK(deMultiplexer[0][2] == 'C');
  BOOST_CHECK(deMultiplexer[0][3] == 'D');
  BOOST_CHECK(deMultiplexer[0][4] == 'E');
  BOOST_CHECK(deMultiplexer[1][0] == 'a');
  BOOST_CHECK(deMultiplexer[1][1] == 'b');
  BOOST_CHECK(deMultiplexer[1][2] == 'c');
  BOOST_CHECK(deMultiplexer[1][3] == 'd');
  BOOST_CHECK(deMultiplexer[1][4] == 'e');
  BOOST_CHECK(deMultiplexer[2][0] == '0');
  BOOST_CHECK(deMultiplexer[2][1] == '1');
  BOOST_CHECK(deMultiplexer[2][2] == '2');
  BOOST_CHECK(deMultiplexer[2][3] == '3');
  BOOST_CHECK(deMultiplexer[2][4] == '4');

  for(size_t sequenceIndex = 0; sequenceIndex < 3; ++sequenceIndex) {
    for(size_t i = 0; i < 5; ++i) {
      deMultiplexer[sequenceIndex][i] += 5;
    }
  }

  deMultiplexer.write();
  area.read();
  memcpy(ioBuffer.data(), &(area[0]), nBytes);

  BOOST_CHECK(ioBuffer[0] == 'F');
  BOOST_CHECK(ioBuffer[1] == 'f');
  BOOST_CHECK(ioBuffer[2] == '5');
  BOOST_CHECK(ioBuffer[3] == 'G');
  BOOST_CHECK(ioBuffer[4] == 'g');
  BOOST_CHECK(ioBuffer[5] == '6');
  BOOST_CHECK(ioBuffer[6] == 'H');
  BOOST_CHECK(ioBuffer[7] == 'h');
  BOOST_CHECK(ioBuffer[8] == '7');
  BOOST_CHECK(ioBuffer[9] == 'I');
  BOOST_CHECK(ioBuffer[10] == 'i');
  BOOST_CHECK(ioBuffer[11] == '8');
  BOOST_CHECK(ioBuffer[12] == 'J');
  BOOST_CHECK(ioBuffer[13] == 'j');
  BOOST_CHECK(ioBuffer[14] == '9');
}

BOOST_AUTO_TEST_CASE(testDeMultiplexing32) {
  testDeMultiplexing<int32_t>("INT");
}
BOOST_AUTO_TEST_CASE(testDeMultiplexing16) {
  testDeMultiplexing<int16_t>("SHORT");
}
BOOST_AUTO_TEST_CASE(testDeMultiplexing8) {
  testDeMultiplexing<int8_t>("CHAR");
}

// test the de-multiplexing itself, with  fixed point conversion
// and using the factory function
template<class SequenceWordType>
void testWithConversion(std::string multiplexedSequenceName) {
  // open a dummy device with the sequence map file
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_ALIAS);

  std::vector<SequenceWordType> ioBuffer(15);
  auto area =
      device.getOneDRegisterAccessor<int32_t>(TEST_MODULE_PATH / MULTIPLEXED_SEQUENCE_PREFIX + multiplexedSequenceName);
  size_t nBytes = std::min(area.getNElements() * sizeof(int32_t), 15 * sizeof(SequenceWordType));

  for(size_t i = 0; i < ioBuffer.size(); ++i) {
    ioBuffer[i] = i;
  }
  memcpy(&(area[0]), ioBuffer.data(), nBytes);
  area.write();

  TwoDRegisterAccessor<float> accessor =
      device.getTwoDRegisterAccessor<float>(TEST_MODULE_PATH / multiplexedSequenceName);
  accessor.read();

  BOOST_CHECK(accessor[0][0] == 0);
  BOOST_CHECK(accessor[1][0] == 0.25);  // 1 with 2 frac bits
  BOOST_CHECK(accessor[2][0] == 0.25);  // 2 with 3 frac bits
  BOOST_CHECK(accessor[0][1] == 1.5);   // 3 with 1 frac bits
  BOOST_CHECK(accessor[1][1] == 1);     // 4 with 2 frac bits
  BOOST_CHECK(accessor[2][1] == 0.625); // 5 with 3 frac bits
  BOOST_CHECK(accessor[0][2] == 3.);    // 6 with 1 frac bits
  BOOST_CHECK(accessor[1][2] == 1.75);  // 7 with 2 frac bits
  BOOST_CHECK(accessor[2][2] == 1.);    // 8 with 3 frac bits
  BOOST_CHECK(accessor[0][3] == 4.5);   // 9 with 1 frac bits
  BOOST_CHECK(accessor[1][3] == 2.5);   // 10 with 2 frac bits
  BOOST_CHECK(accessor[2][3] == 1.375); // 11 with 3 frac bits
  BOOST_CHECK(accessor[0][4] == 6.);    // 12 with 1 frac bits
  BOOST_CHECK(accessor[1][4] == 3.25);  // 13 with 2 frac bits
  BOOST_CHECK(accessor[2][4] == 1.75);  // 14 with 3 frac bits

  for(size_t sequenceIndex = 0; sequenceIndex < 3; ++sequenceIndex) {
    for(size_t i = 0; i < 5; ++i) {
      accessor[sequenceIndex][i] += 1.;
    }
  }

  accessor.write();

  area.read();
  memcpy(ioBuffer.data(), &(area[0]), nBytes);

  for(size_t i = 0; i < 15; ++i) {
    // with i%3+1 fractional bits the added floating point value of 1
    // corresponds to 2^(i%3+1) in fixed point represetation
    int addedValue = 1 << (i % 3 + 1);
    std::stringstream message;
    message << "ioBuffer[" << i << "] is " << ioBuffer[i] << ", expected " << i + addedValue;
    BOOST_CHECK_MESSAGE(ioBuffer[i] == static_cast<SequenceWordType>(i + addedValue), message.str());
  }
}

BOOST_AUTO_TEST_CASE(testWithConversion32) {
  testWithConversion<int32_t>("FRAC_INT");
}
BOOST_AUTO_TEST_CASE(testWithConversion16) {
  testWithConversion<int16_t>("FRAC_SHORT");
}
BOOST_AUTO_TEST_CASE(testWithConversion8) {
  testWithConversion<int8_t>("FRAC_CHAR");
}

BOOST_AUTO_TEST_CASE(testMixed) {
  // open a dummy device with the sequence map file
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_MIXED_ALIAS);

  TwoDRegisterAccessor<double> myMixedData = device.getTwoDRegisterAccessor<double>("APP0/DAQ0_BAM");
  auto myRawData =
      device.getOneDRegisterAccessor<int32_t>("APP0/AREA_MULTIPLEXED_SEQUENCE_DAQ0_BAM", 0, 0, {AccessMode::raw});

  BOOST_CHECK(myMixedData.getNChannels() == 17);
  BOOST_CHECK(myMixedData.getNElementsPerChannel() == 372);
  BOOST_CHECK(myMixedData[0].size() == 372);

  myMixedData[0][0] = -24673; // 1001 1111 1001 1111
  myMixedData[1][0] = -13724; // 1100 1010 0110 0100
  myMixedData[2][0] = 130495;
  myMixedData[3][0] = 513;
  myMixedData[4][0] = 1027;
  myMixedData[5][0] = -56.4;
  myMixedData[6][0] = 78;
  myMixedData[7][0] = 45.2;
  myMixedData[8][0] = -23.9;
  myMixedData[9][0] = 61.3;
  myMixedData[10][0] = -12;

  myMixedData.write();

  myRawData.read();
  BOOST_CHECK(myRawData[0] == -899375201);
  BOOST_CHECK(myRawData[1] == 130495);
  BOOST_CHECK(myRawData[2] == 67305985);
  BOOST_CHECK(myRawData[3] == 5112008);
  BOOST_CHECK(myRawData[4] == -197269459);

  myMixedData.read();

  BOOST_CHECK(myMixedData[0][0] == -24673);
  BOOST_CHECK(myMixedData[1][0] == -13724);
  BOOST_CHECK(myMixedData[2][0] == 130495);
  BOOST_CHECK(myMixedData[3][0] == 513);
  BOOST_CHECK(myMixedData[4][0] == 1027);
  BOOST_CHECK(myMixedData[5][0] == -56);
  BOOST_CHECK(myMixedData[6][0] == 78);
  BOOST_CHECK(myMixedData[7][0] == 45);
  BOOST_CHECK(myMixedData[8][0] == -24);
  BOOST_CHECK(myMixedData[9][0] == 61);
  BOOST_CHECK(myMixedData[10][0] == -12);
}

BOOST_AUTO_TEST_CASE(testNumberOfSequencesDetected) {
  boost::shared_ptr<RegisterInfoMap> registerMap = MapFileParser().parse(MAP_FILE_NAME);
  // open a dummy device with the sequence map file
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_ALIAS);

  TwoDRegisterAccessor<double> deMuxedData = device.getTwoDRegisterAccessor<double>(TEST_MODULE_PATH / "FRAC_INT");

  BOOST_CHECK(deMuxedData.getNChannels() == 3);
}

BOOST_AUTO_TEST_CASE(testAreaOfInterestOffset) {
  // open a dummy device with the sequence map file
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_MIXED_ALIAS);

  // There are 44 bytes per block. In total the area is 4096 bytes long
  // => There are 372 elements (=4092 bytes) in the area, the last 4 bytes are unused.
  const size_t nWordsPerBlock = 44 / sizeof(int32_t); // 32 bit raw words on the transport layer

  // we only request 300 of the 372 elements, with 42 elements offset, so we just cut out an area in the middle
  TwoDRegisterAccessor<double> myMixedData = device.getTwoDRegisterAccessor<double>("APP0/DAQ0_BAM", 300, 42);
  auto myRawData = device.getOneDRegisterAccessor<int32_t>(
      "APP0/AREA_MULTIPLEXED_SEQUENCE_DAQ0_BAM", 300 * nWordsPerBlock, 42 * nWordsPerBlock, {AccessMode::raw});

  BOOST_CHECK(myMixedData.getNChannels() == 17);
  BOOST_CHECK(myMixedData.getNElementsPerChannel() == 300);
  BOOST_CHECK(myMixedData[0].size() == 300);

  for(unsigned int i = 0; i < myMixedData.getNElementsPerChannel(); ++i) {
    myMixedData[0][i] = -24673; // 1001 1111 1001 1111
    myMixedData[1][i] = -13724; // 1100 1010 0110 0100
    myMixedData[2][i] = 130495;
    myMixedData[3][i] = 513;
    myMixedData[4][i] = 1027;
    myMixedData[5][i] = -56.4;
    myMixedData[6][i] = 78;
    myMixedData[7][i] = 45.2;
    myMixedData[8][i] = -23.9;
    myMixedData[9][i] = 61.3;
    myMixedData[10][i] = -12;

    myMixedData.write();

    myRawData.read();
    BOOST_CHECK(myRawData[0 + i * nWordsPerBlock] == -899375201);
    BOOST_CHECK(myRawData[1 + i * nWordsPerBlock] == 130495);
    BOOST_CHECK(myRawData[2 + i * nWordsPerBlock] == 67305985);
    BOOST_CHECK(myRawData[3 + i * nWordsPerBlock] == 5112008);
    BOOST_CHECK(myRawData[4 + i * nWordsPerBlock] == -197269459);

    myMixedData.read();

    BOOST_CHECK(myMixedData[0][i] == -24673);
    BOOST_CHECK(myMixedData[1][i] == -13724);
    BOOST_CHECK(myMixedData[2][i] == 130495);
    BOOST_CHECK(myMixedData[3][i] == 513);
    BOOST_CHECK(myMixedData[4][i] == 1027);
    BOOST_CHECK(myMixedData[5][i] == -56);
    BOOST_CHECK(myMixedData[6][i] == 78);
    BOOST_CHECK(myMixedData[7][i] == 45);
    BOOST_CHECK(myMixedData[8][i] == -24);
    BOOST_CHECK(myMixedData[9][i] == 61);
    BOOST_CHECK(myMixedData[10][i] == -12);

    myMixedData[0][i] = i;
    myMixedData[1][i] = 0;
    myMixedData[2][i] = 0;
    myMixedData[3][i] = 0;
    myMixedData[4][i] = 0;
    myMixedData[5][i] = 0;
    myMixedData[6][i] = 0;
    myMixedData[7][i] = 0;
    myMixedData[8][i] = 0;
    myMixedData[9][i] = 0;
    myMixedData[10][i] = 0;

    myMixedData.write();

    myRawData.read();
    BOOST_CHECK(myRawData[0 + i * nWordsPerBlock] == static_cast<int>(i));
    BOOST_CHECK(myRawData[1 + i * nWordsPerBlock] == 0);
    BOOST_CHECK(myRawData[2 + i * nWordsPerBlock] == 0);
    BOOST_CHECK(myRawData[3 + i * nWordsPerBlock] == 0);
    BOOST_CHECK(myRawData[4 + i * nWordsPerBlock] == 0);

    myMixedData.read();

    BOOST_CHECK(myMixedData[0][i] == i);
    BOOST_CHECK(myMixedData[1][i] == 0);
    BOOST_CHECK(myMixedData[2][i] == 0);
    BOOST_CHECK(myMixedData[3][i] == 0);
    BOOST_CHECK(myMixedData[4][i] == 0);
    BOOST_CHECK(myMixedData[5][i] == 0);
    BOOST_CHECK(myMixedData[6][i] == 0);
    BOOST_CHECK(myMixedData[7][i] == 0);
    BOOST_CHECK(myMixedData[8][i] == 0);
    BOOST_CHECK(myMixedData[9][i] == 0);
    BOOST_CHECK(myMixedData[10][i] == 0);
  }
}

BOOST_AUTO_TEST_CASE(testAreaOfInterestLength) {
  // open a dummy device with the sequence map file
  BackendFactory::getInstance().setDMapFilePath(DMAP_FILE_NAME);
  Device device;
  device.open(DEVICE_MIXED_ALIAS);

  const size_t nWordsPerBlock = 44 / sizeof(int32_t);

  TwoDRegisterAccessor<double> myMixedData = device.getTwoDRegisterAccessor<double>("APP0/DAQ0_BAM", 120);
  auto myRawData =
      device.getOneDRegisterAccessor<int32_t>("APP0/AREA_MULTIPLEXED_SEQUENCE_DAQ0_BAM", 0, 0, {AccessMode::raw});

  BOOST_CHECK(myMixedData.getNChannels() == 17);
  BOOST_CHECK(myMixedData.getNElementsPerChannel() == 120);
  BOOST_CHECK(myMixedData[0].size() == 120);

  for(unsigned int i = 0; i < myMixedData.getNElementsPerChannel(); ++i) {
    myMixedData[0][i] = -24673; // 1001 1111 1001 1111
    myMixedData[1][i] = -13724; // 1100 1010 0110 0100
    myMixedData[2][i] = 130495;
    myMixedData[3][i] = 513;
    myMixedData[4][i] = 1027;
    myMixedData[5][i] = -56.4;
    myMixedData[6][i] = 78;
    myMixedData[7][i] = 45.2;
    myMixedData[8][i] = -23.9;
    myMixedData[9][i] = 61.3;
    myMixedData[10][i] = -12;

    myMixedData.write();

    myRawData.read();
    BOOST_CHECK(myRawData[0 + i * nWordsPerBlock] == -899375201);
    BOOST_CHECK(myRawData[1 + i * nWordsPerBlock] == 130495);
    BOOST_CHECK(myRawData[2 + i * nWordsPerBlock] == 67305985);
    BOOST_CHECK(myRawData[3 + i * nWordsPerBlock] == 5112008);
    BOOST_CHECK(myRawData[4 + i * nWordsPerBlock] == -197269459);

    myMixedData.read();

    BOOST_CHECK(myMixedData[0][i] == -24673);
    BOOST_CHECK(myMixedData[1][i] == -13724);
    BOOST_CHECK(myMixedData[2][i] == 130495);
    BOOST_CHECK(myMixedData[3][i] == 513);
    BOOST_CHECK(myMixedData[4][i] == 1027);
    BOOST_CHECK(myMixedData[5][i] == -56);
    BOOST_CHECK(myMixedData[6][i] == 78);
    BOOST_CHECK(myMixedData[7][i] == 45);
    BOOST_CHECK(myMixedData[8][i] == -24);
    BOOST_CHECK(myMixedData[9][i] == 61);
    BOOST_CHECK(myMixedData[10][i] == -12);

    myMixedData[0][i] = i;
    myMixedData[1][i] = 0;
    myMixedData[2][i] = 0;
    myMixedData[3][i] = 0;
    myMixedData[4][i] = 0;
    myMixedData[5][i] = 0;
    myMixedData[6][i] = 0;
    myMixedData[7][i] = 0;
    myMixedData[8][i] = 0;
    myMixedData[9][i] = 0;
    myMixedData[10][i] = 0;

    myMixedData.write();

    myRawData.read();
    BOOST_CHECK(myRawData[0 + i * nWordsPerBlock] == (int)i);
    BOOST_CHECK(myRawData[1 + i * nWordsPerBlock] == 0);
    BOOST_CHECK(myRawData[2 + i * nWordsPerBlock] == 0);
    BOOST_CHECK(myRawData[3 + i * nWordsPerBlock] == 0);
    BOOST_CHECK(myRawData[4 + i * nWordsPerBlock] == 0);

    myMixedData.read();

    BOOST_CHECK(myMixedData[0][i] == i);
    BOOST_CHECK(myMixedData[1][i] == 0);
    BOOST_CHECK(myMixedData[2][i] == 0);
    BOOST_CHECK(myMixedData[3][i] == 0);
    BOOST_CHECK(myMixedData[4][i] == 0);
    BOOST_CHECK(myMixedData[5][i] == 0);
    BOOST_CHECK(myMixedData[6][i] == 0);
    BOOST_CHECK(myMixedData[7][i] == 0);
    BOOST_CHECK(myMixedData[8][i] == 0);
    BOOST_CHECK(myMixedData[9][i] == 0);
    BOOST_CHECK(myMixedData[10][i] == 0);
  }
}

BOOST_AUTO_TEST_SUITE_END()
