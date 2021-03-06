///@todo FIXME My dynamic init header is a hack. Change the test to use
/// BOOST_AUTO_TEST_CASE!
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "boost_dynamic_init_test.h"

#include <algorithm>
#include <boost/bind/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/function.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/make_shared.hpp>
#include <math.h>

#include "BackendFactory.h"
#include "BufferingRegisterAccessor.h"
#include "Device.h"

namespace ChimeraTK {
  using namespace ChimeraTK;
}

using namespace boost::unit_test_framework;
using namespace ChimeraTK;

/**********************************************************************************************************************/
class BufferingRegisterTest {
 public:
  BufferingRegisterTest() {
    device = boost::shared_ptr<Device>(new Device());
    device->open("DUMMYD1");
  }

  /// test the register accessor
  void testRegisterAccessor();

 private:
  boost::shared_ptr<Device> device;
  friend class BufferingRegisterTestSuite;
};

/**********************************************************************************************************************/
class BufferingRegisterTestSuite : public test_suite {
 public:
  BufferingRegisterTestSuite() : test_suite("DummyRegister test suite") {
    BackendFactory::getInstance().setDMapFilePath(TEST_DMAP_FILE_PATH);
    boost::shared_ptr<BufferingRegisterTest> bufferingRegisterTest(new BufferingRegisterTest);

    add(BOOST_CLASS_TEST_CASE(&BufferingRegisterTest::testRegisterAccessor, bufferingRegisterTest));
  }
};

/**********************************************************************************************************************/
bool init_unit_test() {
  framework::master_test_suite().p_name.value = "DummyRegister test suite";
  framework::master_test_suite().add(new BufferingRegisterTestSuite);

  return true;
}

/**********************************************************************************************************************/
void BufferingRegisterTest::testRegisterAccessor() {
  std::cout << "testRegisterAccessor" << std::endl;

  // obtain register accessor with integral type
  BufferingRegisterAccessor<int> intRegister = device->getBufferingRegisterAccessor<int>("APP0", "MODULE0");

  // variable to read to for comparison
  int compare;

  // check number of elements getter
  BOOST_CHECK(intRegister.getNumberOfElements() == 3);

  // test operator[] on r.h.s.
  compare = 5;
  device->writeReg("MODULE0", "APP0", &compare, sizeof(int), 0);
  compare = -77;
  device->writeReg("MODULE0", "APP0", &compare, sizeof(int), sizeof(int));
  intRegister.read();
  BOOST_CHECK(intRegister[0] == 5);
  BOOST_CHECK(intRegister[1] == -77);

  // test operator[] on l.h.s.
  intRegister[0] = -666;
  intRegister[1] = 999;
  intRegister.write();
  device->readReg("MODULE0", "APP0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == -666);
  device->readReg("MODULE0", "APP0", &compare, sizeof(int), sizeof(int));
  BOOST_CHECK(compare == 999);

  // test iterators with begin and end
  int ic = 0;
  for(BufferingRegisterAccessor<int>::iterator it = intRegister.begin(); it != intRegister.end(); ++it) {
    *it = 1000 * (ic + 1);
    ic++;
  }
  intRegister.write();
  device->readReg("MODULE0", "APP0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 1000);
  device->readReg("MODULE0", "APP0", &compare, sizeof(int), sizeof(int));
  BOOST_CHECK(compare == 2000);

  // test iterators with rbegin and rend
  ic = 0;
  for(BufferingRegisterAccessor<int>::reverse_iterator it = intRegister.rbegin(); it != intRegister.rend(); ++it) {
    *it = 333 * (ic + 1);
    ic++;
  }
  intRegister.write();
  device->readReg("MODULE0", "APP0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 999);
  device->readReg("MODULE0", "APP0", &compare, sizeof(int), sizeof(int));
  BOOST_CHECK(compare == 666);

  // test const iterators in both directions
  compare = 1234;
  device->writeReg("MODULE0", "APP0", &compare, sizeof(int), 0);
  compare = 2468;
  device->writeReg("MODULE0", "APP0", &compare, sizeof(int), sizeof(int));
  compare = 3 * 1234;
  device->writeReg("MODULE0", "APP0", &compare, sizeof(int), 2 * sizeof(int));

  intRegister.read();
  const BufferingRegisterAccessor<int> const_intRegister = intRegister;
  ic = 0;
  for(BufferingRegisterAccessor<int>::const_iterator it = const_intRegister.begin(); it != const_intRegister.end();
      ++it) {
    BOOST_CHECK(*it == 1234 * (ic + 1));
    ic++;
  }
  ic = 0;
  for(BufferingRegisterAccessor<int>::const_reverse_iterator it = const_intRegister.rbegin();
      it != const_intRegister.rend();
      ++it) {
    BOOST_CHECK(*it == 1234 * (3 - ic));
    ic++;
  }

  // test swap with std::vector
  std::vector<int> x(3);
  x[0] = 11;
  x[1] = 22;
  x[2] = 33;
  intRegister.swap(x);
  BOOST_CHECK(x[0] == 1234);
  BOOST_CHECK(x[1] == 2468);
  BOOST_CHECK(x[2] == 3 * 1234);
  BOOST_CHECK(intRegister[0] == 11);
  BOOST_CHECK(intRegister[1] == 22);
  BOOST_CHECK(intRegister[2] == 33);

  // obtain register accessor with fractional type, to check if fixed-point
  // conversion is working (3 fractional bits)
  BufferingRegisterAccessor<double> floatRegister =
      device->getBufferingRegisterAccessor<double>("MODULE0", "WORD_USER1");

  // test operator[] on r.h.s.
  compare = -120;
  device->writeReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  floatRegister.read();
  BOOST_CHECK(floatRegister[0] == -120. / 8.);

  // test operator[] on l.h.s.
  floatRegister[0] = 42. / 8.;
  floatRegister.write();
  device->readReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 42);

  // test implicit type conversion operator
  compare = -77;
  device->writeReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  floatRegister.read();
  BOOST_CHECK(floatRegister + 5. == -77. / 8. + 5.);

  // test assignment operator
  floatRegister = 22.;
  BOOST_CHECK(floatRegister == 22.);
  floatRegister.write();
  device->readReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 22. * 8.);

  // test pre-increment operator
  BufferingRegisterAccessor<double> copy = ++floatRegister;
  boost::shared_ptr<NDRegisterAccessor<double>> impl, implCopy;
  impl = boost::dynamic_pointer_cast<NDRegisterAccessor<double>>(floatRegister.getHighLevelImplElement());
  implCopy = boost::dynamic_pointer_cast<NDRegisterAccessor<double>>(copy.getHighLevelImplElement());

  BOOST_CHECK(implCopy->getHardwareAccessingElements()[0] == impl->getHardwareAccessingElements()[0]);
  BOOST_CHECK(floatRegister == 23.);
  floatRegister.write();
  device->readReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 23. * 8.);

  // test pre-decrement operator
  copy.replace(--floatRegister);
  BOOST_CHECK(implCopy->getHardwareAccessingElements()[0] == impl->getHardwareAccessingElements()[0]);
  BOOST_CHECK(floatRegister == 22.);
  BOOST_CHECK(copy == 22.);
  floatRegister.write();
  device->readReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 22. * 8.);

  // test post-increment operator
  float oldValue = floatRegister++;
  BOOST_CHECK(oldValue == 22.);
  BOOST_CHECK(floatRegister == 23.);
  BOOST_CHECK(implCopy->getHardwareAccessingElements()[0] == impl->getHardwareAccessingElements()[0]);
  floatRegister.write();
  device->readReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 23. * 8.);

  // test post-decrement operator
  oldValue = floatRegister--;
  BOOST_CHECK(oldValue == 23.);
  BOOST_CHECK(floatRegister == 22.);
  BOOST_CHECK(implCopy->getHardwareAccessingElements()[0] == impl->getHardwareAccessingElements()[0]);
  floatRegister.write();
  device->readReg("WORD_USER1", "MODULE0", &compare, sizeof(int), 0);
  BOOST_CHECK(compare == 22. * 8.);
}
