///@todo FIXME My dynamic init header is a hack. Change the test to use
/// BOOST_AUTO_TEST_CASE!
#include "boost_dynamic_init_test.h"

#include <algorithm>
#include <boost/bind/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/function.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/make_shared.hpp>
#include <math.h>

#include "BackendFactory.h"
#include "Device.h"
#include "OneDRegisterAccessor.h"

using namespace boost::unit_test_framework;
namespace ChimeraTK {
  using namespace ChimeraTK;
}
using namespace ChimeraTK;

/**********************************************************************************************************************/
class OneDRegisterTest {
 public:
  OneDRegisterTest() {
    device = boost::shared_ptr<Device>(new Device());
    device->open("DUMMYD1");
  }

  /// test the register accessor
  void testRegisterAccessor();

 private:
  boost::shared_ptr<Device> device;
  friend class OneDRegisterTestSuite;
};

/**********************************************************************************************************************/
class OneDRegisterTestSuite : public test_suite {
 public:
  OneDRegisterTestSuite() : test_suite("OneDRegisterAccessor test suite") {
    BackendFactory::getInstance().setDMapFilePath(TEST_DMAP_FILE_PATH);
    boost::shared_ptr<OneDRegisterTest> bufferingRegisterTest(new OneDRegisterTest);

    add(BOOST_CLASS_TEST_CASE(&OneDRegisterTest::testRegisterAccessor, bufferingRegisterTest));
  }
};

/**********************************************************************************************************************/
bool init_unit_test() {
  framework::master_test_suite().p_name.value = "DummyRegister test suite";
  framework::master_test_suite().add(new OneDRegisterTestSuite);

  return true;
}

/**********************************************************************************************************************/
void OneDRegisterTest::testRegisterAccessor() {
  std::cout << "testRegisterAccessor" << std::endl;

  // obtain register accessor with integral type
  OneDRegisterAccessor<int> intRegister = device->getOneDRegisterAccessor<int>("APP0/MODULE0");
  BOOST_CHECK(intRegister.isReadOnly() == false);
  BOOST_CHECK(intRegister.isReadable());
  BOOST_CHECK(intRegister.isWriteable());

  // check number of elements getter
  BOOST_CHECK(intRegister.getNElements() == 3);

  // test operator[] on r.h.s.
  device->write<int>("APP0/MODULE0", std::vector<int>({5, -77, 99}));
  intRegister.read();
  BOOST_CHECK(intRegister[0] == 5);
  BOOST_CHECK(intRegister[1] == -77);
  BOOST_CHECK(intRegister[2] == 99);

  // test operator[] on l.h.s.
  intRegister[0] = -666;
  intRegister[1] = 999;
  intRegister[2] = 222;
  intRegister.write();
  BOOST_CHECK(device->read<int>("APP0/MODULE0", 3) == std::vector<int>({-666, 999, 222}));

  // test data() function
  int* ptr = intRegister.data();
  BOOST_CHECK(ptr[0] = -666);
  BOOST_CHECK(ptr[1] = 999);
  BOOST_CHECK(ptr[2] = 222);
  ptr[0] = 123;
  ptr[1] = 456;
  ptr[2] = 789;
  BOOST_CHECK(intRegister[0] = 123);
  BOOST_CHECK(intRegister[1] = 456);
  BOOST_CHECK(intRegister[2] = 789);

  // test iterators with begin and end
  int ic = 0;
  for(OneDRegisterAccessor<int>::iterator it = intRegister.begin(); it != intRegister.end(); ++it) {
    *it = 1000 * (ic + 1);
    ic++;
  }
  intRegister.write();
  BOOST_CHECK(device->read<int>("APP0/MODULE0", 3) == std::vector<int>({1000, 2000, 3000}));

  // test iterators with rbegin and rend
  ic = 0;
  for(OneDRegisterAccessor<int>::reverse_iterator it = intRegister.rbegin(); it != intRegister.rend(); ++it) {
    *it = 333 * (ic + 1);
    ic++;
  }
  intRegister.write();
  BOOST_CHECK(device->read<int>("APP0/MODULE0", 3) == std::vector<int>({999, 666, 333}));

  // test const iterators in both directions
  device->write("APP0/MODULE0", std::vector<int>({1234, 2468, 3702}));
  intRegister.read();
  const OneDRegisterAccessor<int> const_intRegister = intRegister;
  ic = 0;
  for(OneDRegisterAccessor<int>::const_iterator it = const_intRegister.begin(); it != const_intRegister.end(); ++it) {
    BOOST_CHECK(*it == 1234 * (ic + 1));
    ic++;
  }
  ic = 0;
  for(OneDRegisterAccessor<int>::const_reverse_iterator it = const_intRegister.rbegin(); it != const_intRegister.rend();
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
  BOOST_CHECK(x[2] == 3702);
  BOOST_CHECK(intRegister[0] == 11);
  BOOST_CHECK(intRegister[1] == 22);
  BOOST_CHECK(intRegister[2] == 33);

  // obtain register accessor with fractional type, to check if fixed-point
  // conversion is working (3 fractional bits)
  OneDRegisterAccessor<double> floatRegister = device->getOneDRegisterAccessor<double>("MODULE0/WORD_USER1");

  // test operator[] on r.h.s.
  device->write("APP0/MODULE0", std::vector<int>({-120, 2468}));
  floatRegister.read();
  BOOST_CHECK(floatRegister[0] == -120. / 8.);

  // test operator[] on l.h.s.
  floatRegister[0] = 42. / 8.;
  floatRegister.write();
  BOOST_CHECK(device->read<int>("APP0/MODULE0", 2) == std::vector<int>({42, 2468}));
}
