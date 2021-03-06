#include "DeviceFile.h"

#include <iostream>
#include <utility>

#include <cstring>
#include <fcntl.h>

#include "Exception.h"

namespace ChimeraTK {

  DeviceFile::DeviceFile(const std::string& deviceFilePath, int flags)
  : _fd{::open(deviceFilePath.c_str(), flags)}, _path{deviceFilePath} {
#ifdef _DEBUG
    std::cout << "opening device file:  " << _path << std::endl;
#endif
    if(_fd < 0) {
      throw runtime_error(_strerror("Cannot open device: "));
    }
  }

  DeviceFile::DeviceFile(DeviceFile&& d) : _fd(std::exchange(d._fd, 0)) {}

  DeviceFile::~DeviceFile() {
#ifdef _DEBUG
    std::cout << "closing device file:  " << _path << std::endl;
#endif
    if(_fd > 0) {
      ::close(_fd);
    }
  }

  std::string DeviceFile::_strerror(const std::string& msg) const {
    char tmp[255];
    return msg + _path + ": " + ::strerror_r(errno, tmp, sizeof(tmp));
  }

  DeviceFile::operator int() const { return _fd; }

} // namespace ChimeraTK
