#include <errno.h>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>

// the io constants and struct for the driver
// FIXME: they should come from the installed driver
#include "PcieBackend.h"
#include "llrfdrv_io_compat.h"
#include "pciedev_io.h"
#include "pcieuni_io_compat.h"

namespace ChimeraTK {

  PcieBackend::PcieBackend(std::string deviceNodeName, std::string mapFileName)
  : NumericAddressedBackend(mapFileName), _deviceID(0), _ioctlPhysicalSlot(0), _ioctlDriverVersion(0), _ioctlDMA(0),
    _deviceNodeName(deviceNodeName) {}

  PcieBackend::~PcieBackend() { close(); }

  void PcieBackend::open() {
#ifdef _DEBUG
    std::cout << "open pcie dev" << std::endl;
#endif
    if(_opened) {
      if(isFunctional()) return;
      ::close(_deviceID);
    }
    _deviceID = ::open(_deviceNodeName.c_str(), O_RDWR);
    if(_deviceID < 0) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot open device: "));
    }

    determineDriverAndConfigureIoctl();

    _opened = true;
    _hasActiveException = false;
  }

  void PcieBackend::determineDriverAndConfigureIoctl() {
    // determine the driver by trying the physical slot ioctl
    device_ioctrl_data ioctlData = {0, 0, 0, 0};

    if(ioctl(_deviceID, PCIEDEV_PHYSICAL_SLOT, &ioctlData) >= 0) {
      // it's the pciedev driver
      _ioctlPhysicalSlot = PCIEDEV_PHYSICAL_SLOT;
      _ioctlDriverVersion = PCIEDEV_DRIVER_VERSION;
      _ioctlDMA = PCIEDEV_READ_DMA;
      _readDMAFunction = boost::bind(&PcieBackend::readDMAViaIoctl, this, boost::placeholders::_1,
          boost::placeholders::_2, boost::placeholders::_3, boost::placeholders::_4);
      _writeFunction = boost::bind(&PcieBackend::writeWithStruct, this, boost::placeholders::_1,
          boost::placeholders::_2, boost::placeholders::_3, boost::placeholders::_4);
      _readFunction = boost::bind(&PcieBackend::readWithStruct, this, boost::placeholders::_1, boost::placeholders::_2,
          boost::placeholders::_3, boost::placeholders::_4);
      return;
    }

    if(ioctl(_deviceID, LLRFDRV_PHYSICAL_SLOT, &ioctlData) >= 0) {
      // it's the llrf driver
      _ioctlPhysicalSlot = LLRFDRV_PHYSICAL_SLOT;
      _ioctlDriverVersion = LLRFDRV_DRIVER_VERSION;
      _ioctlDMA = 0;
      _readDMAFunction = boost::bind(&PcieBackend::readDMAViaStruct, this, boost::placeholders::_1,
          boost::placeholders::_2, boost::placeholders::_3, boost::placeholders::_4);
      _writeFunction = boost::bind(&PcieBackend::writeWithStruct, this, boost::placeholders::_1,
          boost::placeholders::_2, boost::placeholders::_3, boost::placeholders::_4);
      _readFunction = boost::bind(&PcieBackend::readWithStruct, this, boost::placeholders::_1, boost::placeholders::_2,
          boost::placeholders::_3, boost::placeholders::_4);
      return;
    }

    if(ioctl(_deviceID, PCIEUNI_PHYSICAL_SLOT, &ioctlData) >= 0) {
      // it's the pcieuni
      _ioctlPhysicalSlot = PCIEUNI_PHYSICAL_SLOT;
      _ioctlDriverVersion = PCIEUNI_DRIVER_VERSION;
      _ioctlDMA = PCIEUNI_READ_DMA;
      _readDMAFunction = boost::bind(&PcieBackend::readDMAViaIoctl, this, boost::placeholders::_1,
          boost::placeholders::_2, boost::placeholders::_3, boost::placeholders::_4);
      _writeFunction = boost::bind(&PcieBackend::directWrite, this, boost::placeholders::_1, boost::placeholders::_2,
          boost::placeholders::_3, boost::placeholders::_4);
      _readFunction = boost::bind(&PcieBackend::directRead, this, boost::placeholders::_1, boost::placeholders::_2,
          boost::placeholders::_3, sizeof(int32_t));
      _readFunction = boost::bind(&PcieBackend::directRead, this, boost::placeholders::_1, boost::placeholders::_2,
          boost::placeholders::_3, boost::placeholders::_4);
      return;
    }

    // No working driver. Close the device and throw an exception.
    std::cerr << "Unsupported driver. " << createErrorStringWithErrnoText("Error is ") << std::endl;
    ::close(_deviceID);
    throw ChimeraTK::runtime_error("Unsupported driver in device" + _deviceNodeName);
  }

  void PcieBackend::close() {
    if(_opened) {
      ::close(_deviceID);
    }
    _opened = false;
  }

  bool PcieBackend::isFunctional() const {
    if(!_opened) return false;
    if(_hasActiveException) return false;

    // Note: This expects byte 0 of bar 0 to be readable. This is currently guaranteed by our firmware framework. If
    // other firmware needs to be supported, this should be made configurable (via CDD). If a map file is used, we could
    // also use the first readable address specified in the map file.

    // read word 0 from bar 0 to check if device works
    device_rw l_RW;
    l_RW.barx_rw = 0;
    l_RW.mode_rw = RW_D8;
    l_RW.offset_rw = 0;
    l_RW.size_rw = 0; // does not overwrite the struct but writes one word back to data
    l_RW.data_rw = -1;
    l_RW.rsrvd_rw = 0;
    if(::read(_deviceID, &l_RW, sizeof(device_rw)) != sizeof(device_rw)) {
      return false;
    }
    return true;
  }

  void PcieBackend::readInternal(uint8_t bar, uint32_t address, int32_t* data) {
    device_rw l_RW;
    assert(_opened);
    l_RW.barx_rw = bar;
    l_RW.mode_rw = RW_D32;
    l_RW.offset_rw = address;
    l_RW.size_rw = 0; // does not overwrite the struct but writes one word back to data
    l_RW.data_rw = -1;
    l_RW.rsrvd_rw = 0;

    if(::read(_deviceID, &l_RW, sizeof(device_rw)) != sizeof(device_rw)) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read data from device: "));
    }
    *data = l_RW.data_rw;
  }

  void PcieBackend::directRead(uint8_t bar, uint32_t address, int32_t* data, size_t sizeInBytes) {
    assert(_opened);
    assert(bar <= 5);
    loff_t virtualOffset = PCIEUNI_BAR_OFFSETS[bar] + address;

    if(pread(_deviceID, data, sizeInBytes, virtualOffset) != static_cast<int>(sizeInBytes)) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read data from device: "));
    }
  }

  void PcieBackend::writeInternal(uint8_t bar, uint32_t address, int32_t const* data) {
    device_rw l_RW;
    assert(_opened);
    l_RW.barx_rw = bar;
    l_RW.mode_rw = RW_D32;
    l_RW.offset_rw = address;
    l_RW.data_rw = *data;
    l_RW.rsrvd_rw = 0;
    l_RW.size_rw = 0;

    if(::write(_deviceID, &l_RW, sizeof(device_rw)) != sizeof(device_rw)) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot write data to device: "));
    }
  }

  // direct write allows to read areas directly, without a loop in user space
  void PcieBackend::directWrite(uint8_t bar, uint32_t address, int32_t const* data, size_t sizeInBytes) {
    assert(_opened);
    assert(bar <= 5);
    loff_t virtualOffset = PCIEUNI_BAR_OFFSETS[bar] + address;

    if(pwrite(_deviceID, data, sizeInBytes, virtualOffset) != static_cast<int>(sizeInBytes)) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot write data to device: "));
    }
  }

  void PcieBackend::readWithStruct(uint8_t bar, uint32_t address, int32_t* data, size_t sizeInBytes) {
    assert(_opened);
    assert(sizeInBytes % 4 == 0);
    for(uint32_t i = 0; i < sizeInBytes / 4; i++) {
      readInternal(bar, address + i * 4, data + i);
    }
  }

  void PcieBackend::read(uint8_t bar, uint32_t address, int32_t* data, size_t sizeInBytes) {
    if(_hasActiveException) {
      throw ChimeraTK::runtime_error("previous, unrecovered fault");
    }
    if(bar != 0xD) {
      _readFunction(bar, address, data, sizeInBytes);
    }
    else {
      _readDMAFunction(bar, address, data, sizeInBytes);
    }
  }

  void PcieBackend::writeWithStruct(uint8_t bar, uint32_t address, int32_t const* data, size_t sizeInBytes) {
    assert(_opened);
    assert(sizeInBytes % 4 == 0);
    for(uint32_t i = 0; i < sizeInBytes / 4; i++) {
      writeInternal(bar, address + i * 4, (data + i));
    }
  }

  void PcieBackend::write(uint8_t bar, uint32_t address, int32_t const* data, size_t sizeInBytes) {
    if(_hasActiveException) {
      throw ChimeraTK::runtime_error("previous, unrecovered fault");
    }
    _writeFunction(bar, address, data, sizeInBytes);
  }

  void PcieBackend::readDMAViaStruct(uint8_t /*bar*/, uint32_t address, int32_t* data, size_t sizeInBytes) {
    ssize_t ret;
    device_rw l_RW;
    device_rw* pl_RW;

    assert(_opened);

    if(sizeInBytes < sizeof(device_rw)) {
      pl_RW = &l_RW;
    }
    else {
      pl_RW = (device_rw*)data;
    }

    pl_RW->data_rw = 0;
    pl_RW->barx_rw = 0;
    pl_RW->size_rw = sizeInBytes;
    pl_RW->mode_rw = RW_DMA;
    pl_RW->offset_rw = address;
    pl_RW->rsrvd_rw = 0;

    ret = ::read(_deviceID, pl_RW, sizeof(device_rw));
    if(ret != (ssize_t)sizeInBytes) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read data from device: "));
    }
    if(sizeInBytes < sizeof(device_rw)) {
      memcpy(data, pl_RW, sizeInBytes);
    }
  }

  void PcieBackend::readDMAViaIoctl(uint8_t /*bar*/, uint32_t address, int32_t* data, size_t sizeInBytes) {
    assert(_opened);

    // prepare the struct
    device_ioctrl_dma DMA_RW;
    DMA_RW.dma_cmd = 0;     // FIXME: Why is it 0? => read driver code
    DMA_RW.dma_pattern = 0; // FIXME: Why is it 0? => read driver code
    DMA_RW.dma_size = sizeInBytes;
    DMA_RW.dma_offset = address;
    DMA_RW.dma_reserved1 = 0; // FIXME: is this a correct value?
    DMA_RW.dma_reserved2 = 0; // FIXME: is this a correct value?

    if(sizeInBytes >= sizeof(device_ioctrl_dma)) {
      // the ioctrl_dma struct is copied to the beginning of the data buffer,
      // so the information about size and offset are passed to the driver.
      memcpy((void*)data, &DMA_RW, sizeof(device_ioctrl_dma));
      int ret = ioctl(_deviceID, _ioctlDMA, (void*)data);
      if(ret) {
        throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read data from device "));
      }
    }
    else {
      // the ioctrl_dma struct is used as a dma buffer and the read data is later copied out
      int ret = ioctl(_deviceID, _ioctlDMA, (void*)&DMA_RW);
      if(ret) {
        throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read data from device "));
      }
      memcpy(&DMA_RW, (void*)data, sizeInBytes);
    }
  }

  std::string PcieBackend::readDeviceInfo() {
    if(!_opened) throw ChimeraTK::logic_error("Device not opened.");
    std::ostringstream os;
    device_ioctrl_data ioctlData = {0, 0, 0, 0};
    if(ioctl(_deviceID, _ioctlPhysicalSlot, &ioctlData) < 0) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read device info: "));
    }
    os << "SLOT: " << ioctlData.data;
    if(ioctl(_deviceID, _ioctlDriverVersion, &ioctlData) < 0) {
      throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot read device info: "));
    }
    os << " DRV VER: " << (float)(ioctlData.offset / 10.0) + (float)ioctlData.data;
    return os.str();
  }

  std::string PcieBackend::createErrorStringWithErrnoText(std::string const& startText) {
    char errorBuffer[255];
    return startText + _deviceNodeName + ": " + strerror_r(errno, errorBuffer, sizeof(errorBuffer));
  }

  boost::shared_ptr<DeviceBackend> PcieBackend::createInstance(
      std::string address, std::map<std::string, std::string> parameters) {
    if(address.size() == 0) {
      throw ChimeraTK::logic_error("Device address not specified.");
    }

    return boost::shared_ptr<DeviceBackend>(new PcieBackend("/dev/" + address, parameters["map"]));
  }

} // namespace ChimeraTK
